#include "ClickLayer.h"

namespace kf
{

//==============================================================================
// ClickLayer
//==============================================================================

ClickLayer::ClickLayer() = default;

void ClickLayer::prepare (double newSampleRate, int /*maxBlockSize*/)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    reset();
}

void ClickLayer::reset()
{
    active = false;
    playbackPos = 0.0f;
    elapsedSamples = 0.0;
    for (auto& f : filters)
        f = OnePole{};
}

bool ClickLayer::loadFromBuffer (const juce::AudioBuffer<float>& source, double sourceSampleRate)
{
    if (source.getNumSamples() <= 0 || source.getNumChannels() <= 0)
        return false;

    const int maxLen = (int) (kMaxSampleLengthSeconds * sampleRate);
    const int srcLen = juce::jmin (source.getNumSamples(), maxLen);

    // 统一转成单声道存储（click 层不需要立体声；省内存且相位更可控）
    sampleData.setSize (1, srcLen, false, true, false);
    sampleData.clear();

    auto* dst = sampleData.getWritePointer (0);

    for (int ch = 0; ch < source.getNumChannels(); ++ch)
    {
        const auto* src = source.getReadPointer (ch);
        for (int i = 0; i < srcLen; ++i)
            dst[i] += src[i];
    }

    if (source.getNumChannels() > 1)
    {
        const float g = 1.0f / (float) source.getNumChannels();
        for (int i = 0; i < srcLen; ++i)
            dst[i] *= g;
    }

    sampleRateRatio = (sourceSampleRate > 0.0) ? (sourceSampleRate / sampleRate) : 1.0;

    // 若源采样率与工程不同，这里做一次线性重采样，
    // 让播放时无需实时换算（click 很短，一次性处理代价可忽略）。
    if (std::abs (sampleRateRatio - 1.0) > 1.0e-6)
    {
        const int outLen = juce::jmax (1, (int) std::floor ((double) srcLen / sampleRateRatio));

        // 注意：必须先把重采样结果算到独立 buffer，再赋回 sampleData。
        // 若先 setSize 再读 dst，dst 已指向被重新分配（释放）的内存。
        juce::AudioBuffer<float> resampled (1, outLen);
        auto* out = resampled.getWritePointer (0);
        for (int i = 0; i < outLen; ++i)
            out[i] = dsp::readInterpolated (dst, srcLen, (double) i * sampleRateRatio);

        sampleData.setSize (1, outLen, false, true, false);
        sampleData.copyFrom (0, 0, resampled, 0, 0, outLen);
        sampleRateRatio = 1.0;
    }

    return true;
}

bool ClickLayer::loadFromFile (const juce::File& file)
{
    if (! file.existsAsFile())
        return false;

    juce::AudioFormatManager fmt;
    fmt.registerBasicFormats();

    std::unique_ptr<juce::AudioFormatReader> reader (fmt.createReaderFor (file));
    if (reader == nullptr)
        return false;

    const int len = juce::jmin ((int) reader->lengthInSamples,
                                (int) (kMaxSampleLengthSeconds * reader->sampleRate));
    if (len <= 0)
        return false;

    juce::AudioBuffer<float> buf ((int) reader->numChannels, len);
    reader->read (&buf, 0, len, 0, true, true);

    if (loadFromBuffer (buf, reader->sampleRate))
    {
        loadedName = file.getFileNameWithoutExtension();
        return true;
    }
    return false;
}

void ClickLayer::setWaveformSource (bool use, Waveform w, float baseHz, float shape)
{
    bool changedType = (use != useWaveform) || (w != waveType);

    useWaveform = use;
    waveType = w;
    waveBaseHz = juce::jlimit (20.0f, 12000.0f, baseHz);
    waveShape = juce::jlimit (0.0f, 1.0f, shape);

    // 换波形时重置相位/噪声，避免残留状态产生爆音
    if (changedType)
    {
        wavePhase = 0.0;
        waveNoise.reset (0x51ED270Bu);
    }
}

void ClickLayer::noteOn (float velocity, float pitchRatio)
{
    // 波形声源不需要载入采样
    if (! useWaveform && ! isLoaded())
        return;

    velocityGain = juce::jlimit (0.0f, 1.0f, velocity);
    playbackInc  = juce::jlimit (0.05f, 20.0f, pitchRatio);
    playbackPos  = 0.0f;
    elapsedSamples = 0.0;
    wavePhase    = 0.0;
    active = true;

    for (auto& f : filters)
        f = OnePole{};
}

void ClickLayer::render (juce::AudioBuffer<float>& output,
                         int startSample, int numSamples,
                         const EnvelopeReader& ampEnv,
                         float level,
                         float decaySeconds,
                         float lowPassHz,
                         float highPassHz,
                         bool  enabled,
                         float sliceStart,
                         float sliceEnd,
                         float fadeInSeconds)
{
    if (! active || ! enabled || (! useWaveform && ! isLoaded()) || numSamples <= 0)
        return;

    const auto* src = sampleData.getReadPointer (0);
    const int   srcLen = sampleData.getNumSamples();

    // --- 采样切片 -----------------------------------------------------------
    // sliceStart / sliceEnd 是 0..1 的比例，映射到采样的起止位置。
    // 只播这一段，就得到"只取咔哒头"或"只取尾巴"的效果。
    // 保证 start < end（至少留 1% 长度），否则会播不出声。
    const float s0 = juce::jlimit (0.0f, 0.99f, sliceStart);
    const float s1 = juce::jlimit (s0 + 0.01f, 1.0f, sliceEnd);

    const int sliceBegin = (int) (s0 * (float) srcLen);
    const int sliceLen   = juce::jmax (2, (int) ((s1 - s0) * (float) srcLen));
    const int sliceLast  = juce::jmin (srcLen - 1, sliceBegin + sliceLen - 1);
    const int   numChannels = output.getNumChannels();

    const float invDecay = (decaySeconds > 1.0e-4f) ? (1.0f / decaySeconds) : 0.0f;

    // 滤波系数（一阶，按 10Hz..20kHz 钳制到稳定范围）
    const float nyq = (float) (sampleRate * 0.5);
    const float lpCut = juce::jlimit (20.0f, nyq * 0.98f, lowPassHz);
    const float hpCut = juce::jlimit (10.0f, nyq * 0.98f, highPassHz);
    const float lpCoeff = juce::jlimit (0.0f, 1.0f,
        1.0f - std::exp (-juce::MathConstants<float>::twoPi * lpCut / (float) sampleRate));
    const float hpCoeff = juce::jlimit (0.0f, 1.0f,
        1.0f - std::exp (-juce::MathConstants<float>::twoPi * hpCut / (float) sampleRate));

    // 波形声源的频率：基频 × 该层的音高比
    const double waveInc = useWaveform
                             ? (double) (waveBaseHz * juce::jlimit (0.05f, 20.0f, playbackInc))
                                 / sampleRate
                             : 0.0;

    for (int i = 0; i < numSamples; ++i)
    {
        // 采样模式：播放到切片末尾就结束；波形模式：由包络决定长度
        if (! useWaveform && (sliceBegin + (int) playbackPos) >= sliceLast)
        {
            active = false;
            break;
        }

        const float envPos = (float) (elapsedSamples / sampleRate) * invDecay;
        const float envValue = juce::jmax (0.0f, ampEnv.read (envPos));

        float s;

        if (useWaveform)
        {
            // --- 内置波形合成
            if (wave::isNoise (waveType))
            {
                s = waveNoise.next();
            }
            else
            {
                wavePhase += waveInc;
                while (wavePhase >= 1.0) wavePhase -= 1.0;

                const float ph = (float) wavePhase;

                if (waveType == Waveform::Pulse || waveType == Waveform::Square)
                {
                    const float duty = (waveType == Waveform::Square)
                                         ? 0.5f
                                         : juce::jmap (waveShape, 0.5f, 0.05f);
                    // 零均值脉冲：避免低占空比带来的直流偏置
                    s = wave::pulseValue (ph, duty);
                }
                else if (waveType == Waveform::Saw)
                {
                    const float bend = juce::jmap (waveShape, 0.2f, 0.8f);
                    s = wave::sawValue (ph, bend);
                }
                else if (waveType == Waveform::Triangle)
                {
                    const float tri = 4.0f * std::abs (ph - 0.5f) - 1.0f;
                    const float sine = std::sin (ph * juce::MathConstants<float>::twoPi);
                    s = tri * waveShape + sine * (1.0f - waveShape);
                }
                else
                {
                    s = std::sin (ph * juce::MathConstants<float>::twoPi);
                }
            }

            // 波形模式需要靠包络收尾，包络走完就停
            if (envPos >= 1.0f && envValue < 1.0e-4f)
            {
                active = false;
                break;
            }
        }
        else
        {
            // playbackPos 是"切片内"的位置，读取时加上切片起点
            s = dsp::readInterpolated (src, srcLen,
                                       (double) (sliceBegin + (int) playbackPos));
        }

        // 滤波：先低通再高通
        s = filters[0].processLowPass (s, lpCoeff);
        s = filters[0].processHighPass (s, hpCoeff);

        s *= envValue * level * velocityGain;

        // 起音淡化：从采样中间切入时（Start != 0）消除咔哒。
        // 淡化长度按采样率折算；fadeIn = 0 时完全不生效。
        if (fadeInSeconds > 1.0e-6f && ! useWaveform)
        {
            const float fadeSamples = fadeInSeconds * (float) sampleRate;
            if (playbackPos < fadeSamples)
                s *= playbackPos / juce::jmax (1.0f, fadeSamples);
        }

        for (int ch = 0; ch < numChannels; ++ch)
            output.addSample (ch, startSample + i, s);

        playbackPos += playbackInc;
        elapsedSamples += 1.0;
    }
}

//==============================================================================
// ClickSection
//==============================================================================

void ClickSection::prepare (double sampleRate, int maxBlockSize)
{
    for (auto& l : layers)
        l.prepare (sampleRate, maxBlockSize);
}

void ClickSection::reset()
{
    for (auto& l : layers)
        l.reset();
}

void ClickSection::noteOn (float velocity, const std::array<float, kNumLayers>& pitchRatios)
{
    for (int i = 0; i < kNumLayers; ++i)
        layers[(size_t) i].noteOn (velocity, pitchRatios[(size_t) i]);
}

void ClickSection::render (juce::AudioBuffer<float>& output,
                           int startSample, int numSamples,
                           const std::array<const EnvelopeReader*, kNumLayers>& ampEnvs,
                           const std::array<float, kNumLayers>& levels,
                           const std::array<float, kNumLayers>& decaySeconds,
                           const std::array<float, kNumLayers>& lowPassHz,
                           const std::array<float, kNumLayers>& highPassHz,
                           const std::array<bool, kNumLayers>& enabled,
                           const std::array<float, kNumLayers>& sliceStart,
                           const std::array<float, kNumLayers>& sliceEnd,
                           const std::array<float, kNumLayers>& fadeInSeconds)
{
    for (int i = 0; i < kNumLayers; ++i)
    {
        if (ampEnvs[(size_t) i] == nullptr)
            continue;

        layers[(size_t) i].render (output, startSample, numSamples,
                                   *ampEnvs[(size_t) i],
                                   levels[(size_t) i],
                                   decaySeconds[(size_t) i],
                                   lowPassHz[(size_t) i],
                                   highPassHz[(size_t) i],
                                   enabled[(size_t) i],
                                   sliceStart[(size_t) i],
                                   sliceEnd[(size_t) i],
                                   fadeInSeconds[(size_t) i] * 0.001f);
    }
}

} // namespace kf
