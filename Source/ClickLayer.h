#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include "Envelope.h"
#include "DspUtils.h"
#include "Waveform.h"

namespace kf
{

//==============================================================================
/** 单个 Click 层。
 *
 *  KICK 2 提供 3 个独立的 Click 槽，每层可以：
 *   - 载入一个采样（内置库或用户自备）
 *   - 独立调整音量、音高、起音延迟、长度
 *   - 独立的 Amp 包络（同样是节点式）
 *   - 独立的一阶滤波（低通/高通）
 *   - 是否跟随 Sub 的音高（keytrack）
 */
class ClickLayer
{
public:
    static constexpr int kMaxSampleLengthSeconds = 4;

    ClickLayer();

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    /** 载入采样。会重采样到当前工程采样率，并做峰值归一化准备。
     *  @returns 是否成功 */
    bool loadFromBuffer (const juce::AudioBuffer<float>& source, double sourceSampleRate);
    bool loadFromFile (const juce::File& file);

    void clear() { sampleData.clear(); sampleRateRatio = 1.0; loadedName.clear(); }
    bool isLoaded() const noexcept { return sampleData.getNumSamples() > 0; }
    juce::String getLoadedName() const { return loadedName; }
    void setLoadedName (const juce::String& n) { loadedName = n; }

    /** 触发。pitchRatio 已由处理器折算好（含 keytrack / 层音高）。 */
    void noteOn (float velocity, float pitchRatio);

    /** 用内置波形而不是采样发声。
     *  @param w       波形（Sine/Triangle/Saw/Square/Noise/Pulse）
     *  @param baseHz  波形基频
     *  @param shape   形状微调 0..1
     *  当 useWaveform 为 true 时忽略已载入的采样。 */
    void setWaveformSource (bool useWaveform, Waveform w, float baseHz, float shape);

    /** 渲染这一层到 output。
     *  @param dryOnly 为 true 时跳过本层的包络？不 —— 该参数不使用，保留签名清晰。 */
    void render (juce::AudioBuffer<float>& output,
                 int startSample, int numSamples,
                 const EnvelopeReader& ampEnv,
                 float level,
                 float decaySeconds,
                 float lowPassHz,
                 float highPassHz,
                 bool  enabled,
                 float sliceStart = 0.0f,
                 float sliceEnd = 1.0f,
                 float fadeInSeconds = 0.0f);

    bool isActive() const noexcept { return active; }

private:
    juce::AudioBuffer<float> sampleData;
    double sampleRate = 44100.0;
    double sampleRateRatio = 1.0;   // 采样源采样率 / 当前采样率
    juce::String loadedName;

    // --- 内置波形声源（与采样互斥：useWaveform 为真时走波形合成）
    bool     useWaveform = false;
    Waveform waveType = Waveform::Sine;
    float    waveBaseHz = 200.0f;
    float    waveShape = 0.5f;
    double   wavePhase = 0.0;
    NoiseGen waveNoise;

    bool  active = false;
    float playbackPos = 0.0f;       // 当前播放位置（源采样点数）
    float playbackInc = 1.0f;       // 每输出采样推进多少源采样点
    float velocityGain = 1.0f;
    double elapsedSamples = 0.0;

    // 每层独立滤波状态（左右声道各一组，用简单的 state-variable 结构）
    /** 一阶滤波器。
     *
     *  低通与高通必须使用**各自独立**的状态变量：
     *  若两者共用同一个 lp 状态，串联后 x-lp 会因为 lp 已跟踪到 x 而恒等于 0，
     *  整个 Click 层的输出会被清零（这个 bug 曾导致 Click 完全无声）。
     */
    struct OnePole
    {
        float lowPassState = 0.0f;
        float highPassState = 0.0f;

        inline float processLowPass (float x, float coeff) noexcept
        {
            lowPassState += coeff * (x - lowPassState);
            return lowPassState;
        }

        inline float processHighPass (float x, float coeff) noexcept
        {
            // 高通 = 输入 - 该输入经过低通的结果（用独立状态）
            highPassState += coeff * (x - highPassState);
            return x - highPassState;
        }
    };
    std::array<OnePole, 2> filters;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ClickLayer)
};

//==============================================================================
/** 三个 Click 层的容器 + 混合。 */
class ClickSection
{
public:
    static constexpr int kNumLayers = 4;

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    ClickLayer& getLayer (int i) { return layers[(size_t) juce::jlimit (0, kNumLayers - 1, i)]; }

    void noteOn (float velocity, const std::array<float, kNumLayers>& pitchRatios);

    void render (juce::AudioBuffer<float>& output,
                 int startSample, int numSamples,
                 const std::array<const EnvelopeReader*, kNumLayers>& ampEnvs,
                 const std::array<float, kNumLayers>& levels,
                 const std::array<float, kNumLayers>& decaySeconds,
                 const std::array<float, kNumLayers>& lowPassHz,
                 const std::array<float, kNumLayers>& highPassHz,
                 const std::array<bool, kNumLayers>& enabled,
                 const std::array<float, kNumLayers>& sliceStart,
                 const std::array<float, kNumLayers>& sliceEnd,
                 const std::array<float, kNumLayers>& fadeInSeconds);

private:
    std::array<ClickLayer, kNumLayers> layers;
};

} // namespace kf
