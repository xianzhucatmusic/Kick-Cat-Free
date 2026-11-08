#include "SubSynth.h"
#include <cmath>

namespace kf
{

//==============================================================================
// 振荡器索引约定：
//   0            -> 基频 (1x)
//   1 .. 8       -> 泛音 2x .. 9x      （kMaxHarmonics 个）
//   9 .. 11      -> sub harmonics 0.5x / 1/3x / 0.25x
//==============================================================================
float SubSynth::ratioFor (int index) noexcept
{
    if (index == 0)
        return 1.0f;

    if (index <= kMaxHarmonics)
        return (float) (index + 1);   // 2x, 3x, ... 9x

    switch (index - kMaxHarmonics)
    {
        case 1:  return 0.5f;         // 低八度
        case 2:  return 1.0f / 3.0f;  // 低十二度（五度下方再低八度）
        case 3:  return 0.25f;        // 低两个八度
        default: return 1.0f;
    }
}

SubSynth::SubSynth()
{
    phases.fill (0.0);
    for (size_t i = 0; i < noiseGens.size(); ++i)
        noiseGens[i].reset ((uint32_t) (0x9E3779B9u * (i + 1)));
}

//==============================================================================
// 取当前波形值
//
// 波形选择是这次新增的核心功能：不同波形给底鼓完全不同的性格。
// Square/Pulse 用 shape 改占空比，Saw 用 shape 做对称性弯曲。
//==============================================================================
float SubSynth::oscValue (int index) noexcept
{
    const float ph = (float) phases[(size_t) index];

    if (wave::isNoise (waveform))
        return noiseGens[(size_t) index].next();

    if (waveform == Waveform::Pulse || waveform == Waveform::Square)
    {
        // shape 0 -> 50% 占空比；shape 1 -> 5%（极窄脉冲）
        //
        // Square 与 Pulse 现在都响应 Shape：
        // 原先 Square 被写死 50%，导致 Shape 旋钮在 Square 下完全没有反应
        // （用户看到两个选项，行为却一个能调、一个不能，很费解）。
        // 现在 Square 也跟随 Shape，只是默认值更"正"。
        // 用零均值脉冲，避免低占空比时产生巨大直流
        const float duty = juce::jmap (shape, 0.5f, 0.05f);
        return wave::pulseValue (ph, duty);
    }

    if (waveform == Waveform::Saw)
    {
        // shape 让锯齿弯曲：0.5 为标准锯齿，偏两端则形状更极端。
        // 用零均值版本，避免直流偏置。
        const float bend = juce::jmap (shape, 0.2f, 0.8f);
        return wave::sawValue (ph, bend);
    }

    if (waveform == Waveform::Triangle)
    {
        const float bend = juce::jmap (shape, 0.3f, 0.7f);
        const float tri = 4.0f * std::abs (ph - 0.5f) - 1.0f;
        // 与 sine 混合可以做出介于三角和正弦之间的形状
        const float sine = std::sin (ph * juce::MathConstants<float>::twoPi);
        return tri * bend + sine * (1.0f - bend);
    }

    // Sine（默认）
    return std::sin (ph * juce::MathConstants<float>::twoPi);
}

void SubSynth::prepare (double newSampleRate, int /*maxBlockSize*/)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    reset();
}

void SubSynth::reset()
{
    phases.fill (0.0);
    active = false;
    elapsedSamples = 0.0;
}

void SubSynth::randomizePhases (float amount)
{
    const float a = juce::jlimit (0.0f, 1.0f, amount);
    if (a <= 1.0e-4f)
        return;

    juce::Random& rng = juce::Random::getSystemRandom();
    for (auto& ph : phases)
        ph = (double) rng.nextFloat() * (double) a;
}

void SubSynth::setStartPhase (float degrees) noexcept
{
    // 归一化到 0..1（一个周期）
    const float norm = degrees / 360.0f;
    startPhase = (double) (norm - std::floor (norm));
}

void SubSynth::noteOn (float frequency, float velocity)
{
    baseFrequency = juce::jlimit (10.0f, 2000.0f, frequency);
    velocityGain  = juce::jlimit (0.0f, 1.0f, velocity);
    elapsedSamples = 0.0;


    // 相位处理，两个开关分工明确：
    //
    //   phase_reset = 开  -> 相位归零到 Start Phase（波形可完全复现）
    //   phase_reset = 关  -> 保留上次结束的相位（连击更自然，
    //                        避免低频相位突变产生咔哒声）
    //
    // Start Phase 只在归零时起作用 —— 不归零时它没有意义
    // （相位是连续演进的，设"起点"无从谈起）。
    if (resetPhaseOnTrigger)
        phases.fill (startPhase);

    active = true;
}

void SubSynth::noteOff()
{
    // 底鼓是 one-shot：noteOff 不立即停声，交给 Amp 包络自然收尾。
    // 真正停声由 render() 里包络走完决定。
}

bool SubSynth::render (juce::AudioBuffer<float>& output,
                       int startSample, int numSamples,
                       const EnvelopeReader& pitchEnv,
                       const EnvelopeReader& ampEnv,
                       float pitchEnvDepthSemis,
                       float ampGain,
                       float decaySeconds,
                       float pitchDecaySeconds,
                       float ampPunch,
                       float liveFrequency,
                       int   numActiveHarmonics,
                       const std::array<float, kMaxHarmonics>& harmonicLevels,
                       float subHarmonicLevel,
                       bool  subHarmonicsEnabled,
                       float filterCutoffHz,
                       float filterResonance,
                       float filterEnvAmount,
                       float pitchCurve,
                       float holdSeconds,
                       float widthAmount,
                       float noiseAmount,
                       float noiseDecaySeconds,
                       float noiseHighPassHz,
                       float attackSeconds,
                       int   filterType,
                       float detuneCents,
                       float phaseRandomAmount,
                       float pitchDelaySeconds,
                       float stage2Semis,
                       float stage2StartPct)
{
    if (! active || numSamples <= 0)
        return false;

    const int numChannels = output.getNumChannels();
    const double sr = sampleRate;

    // 逐块接收当前基频：Portamento / Keytrack 靠它生效
    baseFrequency = juce::jlimit (10.0f, 2000.0f, liveFrequency);

    // 滤波器参数每块同步一次（截止频率的**调制**在采样循环里逐点算）
    setFilter (filterCutoffHz, filterResonance, filterEnvAmount);

    numActiveHarmonics = juce::jlimit (0, kMaxHarmonics, numActiveHarmonics);
    float maxEnvValue = 0.0f;

    // 音高与音量包络各有自己的时间尺度：
    // 底鼓常见的做法是"音高掉得比音量快"（pitch 先收紧，音量再收尾），
    // 共用一个 decay 就做不出这种效果。pitchDecaySeconds 为 0 时跟随主 decay。
    const float ampDecay   = juce::jmax (1.0e-4f, decaySeconds);
    const float pitchDecay = (pitchDecaySeconds > 1.0e-4f) ? pitchDecaySeconds : ampDecay;

    const float invAmpDecay   = 1.0f / ampDecay;
    const float invPitchDecay = 1.0f / pitchDecay;

    // Amp Punch：对音量包络做指数整形。
    //   0    -> 起音更"软"，尾巴更长（pow < 1）
    //   0.5  -> 原样
    //   1    -> 起音更"实/硬"，尾巴收得快（pow > 1）
    const float punchExp = juce::jmap (juce::jlimit (0.0f, 1.0f, ampPunch), 0.55f, 1.85f);

    // 音量包络的峰值：Hold 段用它作为保持电平。
    // 默认包络从 0 起音，如果直接用 read(0) 会让 Hold 段完全静音。
    float ampPeakValue = 0.0f;
    {
        const float basePos = (float) (elapsedSamples / sr) * invAmpDecay;
        for (int k = 0; k <= 64; ++k)
            ampPeakValue = juce::jmax (ampPeakValue,
                                       juce::jmax (0.0f, ampEnv.read (basePos + (float) k / 64.0f * 0.15f)));
        ampPeakValue = juce::jmax (ampPeakValue, juce::jmax (0.0f, ampEnv.read (basePos)));
    }

    for (int i = 0; i < numSamples; ++i)
    {
        // 包络横轴 0..1 映射到各自的时间尺度；超过 1 后包络自然取末端值
        const float pitchPos = (float) (elapsedSamples / sr) * invPitchDecay;
        const float ampPos   = (float) (elapsedSamples / sr) * invAmpDecay;

        // --- Hold：把包络曲线整体**向后平移** holdSeconds ---
        //
        // 语义：起音瞬间的值被"冻结" hold 时长，之后曲线照常走。
        // 实现上等价于把读取位置减去 holdSeconds 并钳到 0 —— 即在
        // 读取位置 < hold 时一律取位置 0 的值。
        //
        // 注意这里取的是位置 0（包络起点），对音高包络是对的
        // （起点就是最高音）。音量包络如果起点是 0 会导致静音，
        // 所以音量侧改用"起音峰值位置"（envelopePeakPos）。
        const float holdNormP = (holdSeconds > 1.0e-5f) ? holdSeconds * invPitchDecay : 0.0f;
        const float holdNormA = (holdSeconds > 1.0e-5f) ? holdSeconds * invAmpDecay   : 0.0f;

        // --- Pitch Delay：先顶住最高音，延迟一段再开始下滑 ---
        // 通过把包络读取位置额外后移实现：延迟期内一律读起点值。
        const float delayNormP = (pitchDelaySeconds > 1.0e-5f)
                                   ? pitchDelaySeconds * invPitchDecay : 0.0f;
        const float pitchReadPos = pitchPos - holdNormP - delayNormP;

        const float pitchEnvValue = (pitchReadPos <= 0.0f)
                                      ? pitchEnv.read (0.0f)
                                      : pitchEnv.read (pitchReadPos);

        float ampEnvValue;
        if (ampPos < holdNormA)
        {
            // 保持段的电平 = 起音峰值，而不是包络在 0 处的值
            // （后者通常是 0，会让整个 Hold 段静音）
            ampEnvValue = ampPeakValue;
        }
        else
        {
            ampEnvValue = juce::jmax (0.0f, ampEnv.read (ampPos - holdNormA));
        }

        // --- Pitch Curve：弯曲音高包络的下降轨迹 ---
        //   > 0 前段掉得更快（更"clicky"的音头）
        //   < 0 前段掉得更慢（更平滑的滑落）
        float pitchShaped = pitchEnvValue;
        if (std::abs (pitchCurve) > 1.0e-4f && pitchEnvValue > 0.0f)
        {
            const float expo = std::pow (2.0f, pitchCurve * 1.5f);
            pitchShaped = std::pow (pitchEnvValue, expo);
        }

        // 整形（只对正值做幂，避免 0 附近的负数问题）
        if (ampEnvValue > 0.0f)
            ampEnvValue = std::pow (ampEnvValue, punchExp);

        // --- Attack：起音渐入 ---
        // 在 attackSeconds 内把包络线性推到目标值，做出"软起音"。
        // attack = 0 时完全不影响（原来的立即起音行为）。
        if (attackSeconds > 1.0e-5f)
        {
            const float t = (float) (elapsedSamples / sr) / attackSeconds;
            if (t < 1.0f)
                ampEnvValue *= t;          // 0..1 线性渐入
        }

        maxEnvValue = juce::jmax (maxEnvValue, ampEnvValue);

        // --- Pitch 包络：以半音为单位调制基频
        // --- 第二段下坠 ---
        // stage2StartPct 指定"从包络的百分之几开始"进入第二段，
        // 之后在剩余时间里线性补上 stage2Semis 的额外下滑。
        float stage2 = 0.0f;
        if (stage2Semis > 1.0e-4f)
        {
            const float s2Start = juce::jlimit (0.0f, 0.99f, stage2StartPct * 0.01f);
            const float t = (pitchPos - s2Start) / juce::jmax (1.0e-3f, 1.0f - s2Start);
            stage2 = -stage2Semis * juce::jlimit (0.0f, 1.0f, t);
        }

        const float semis = pitchShaped * pitchEnvDepthSemis + stage2;
        const float freq  = baseFrequency * semitonesToRatio (semis);

        float sample = 0.0f;

        // 基频
        {
            const double inc = (double) freq / sr;
            phases[0] += inc;
            while (phases[0] >= 1.0) phases[0] -= 1.0;
            sample += oscValue (0);
        }

        // 泛音
        for (int h = 0; h < numActiveHarmonics; ++h)
        {
            const int idx = h + 1;
            // 泛音失谐：索引越高失谐越多，形成自然的"厚度"。
            //
            // 半音 = 100 cent，所以 N cent 的频率比 = 2^(N/1200)。
            // 之前写成 (detuneCents * idx / 12) / 1200 —— 多除了一个 12，
            // 40 cent 在 idx=8 时只有 1.00015 倍（约 0.26 cent），
            // 完全听不出来。现在按 idx 线性分配到 cent 上。
            const float centsForThis = detuneCents * juce::jlimit (0.0f, 1.0f, (float) idx / 8.0f);
            const float detuneRatio = (idx == 0 || centsForThis <= 0.0f)
                                        ? 1.0f
                                        : std::pow (2.0f, centsForThis / 1200.0f);
            const double inc = (double) (freq * ratioFor (idx) * detuneRatio) / sr;
            phases[(size_t) idx] += inc;
            while (phases[(size_t) idx] >= 1.0) phases[(size_t) idx] -= 1.0;

            sample += harmonicLevels[(size_t) h] * oscValue (idx);
        }

        // Sub harmonics
        if (subHarmonicsEnabled)
        {
            for (int s = 0; s < kNumSubHarmonics; ++s)
            {
                const int idx = kMaxHarmonics + 1 + s;
                // 泛音失谐：索引越高失谐越多，形成自然的"厚度"。
            //
            // 半音 = 100 cent，所以 N cent 的频率比 = 2^(N/1200)。
            // 之前写成 (detuneCents * idx / 12) / 1200 —— 多除了一个 12，
            // 40 cent 在 idx=8 时只有 1.00015 倍（约 0.26 cent），
            // 完全听不出来。现在按 idx 线性分配到 cent 上。
            const float centsForThis = detuneCents * juce::jlimit (0.0f, 1.0f, (float) idx / 8.0f);
            const float detuneRatio = (idx == 0 || centsForThis <= 0.0f)
                                        ? 1.0f
                                        : std::pow (2.0f, centsForThis / 1200.0f);
            const double inc = (double) (freq * ratioFor (idx) * detuneRatio) / sr;
                phases[(size_t) idx] += inc;
                while (phases[(size_t) idx] >= 1.0) phases[(size_t) idx] -= 1.0;

                sample += subHarmonicLevel * oscValue (idx);
            }
        }

        //--- 包络跟随滤波 ---------------------------------------------------
        // 是否需要跑滤波器：截止频率低于 fs/4，或有包络调制
        const float nyquistish = (float) sr * 0.25f;
        const bool filterActive = std::abs (filterEnvAmt) > 1.0e-4f
                                  || filterBaseHz < nyquistish;

        // 截止频率 = 基础值 × 包络调制量。
        // 用音量包络驱动：起音瞬间最大，之后衰减，
        // 于是"起音明亮、尾巴变暗" —— 底鼓最常用的塑形手法之一。
        //
        // 用 **TPT/Zavalishin 状态变量滤波器**（而非 Chamberlin）。
        //
        // 换掉 Chamberlin 的原因：它的稳定条件 f < 2 - 2·damp 与阻尼强耦合，
        // 常规 Q 值（0.707）下 damp = 1/Q = 1.414 已经不满足，必然发散或静音。
        // 我先后在它上面犯过两次错（先发散成 1e38，后钳成静音）。
        // TPT 结构用双线性预畸变，**无条件稳定**，且系数含义直观。
        if (filterActive)
        {
            // 包络对截止频率的调制：+1 -> 向上 4 个八度，-1 -> 向下 4 个八度
            const float octaves = filterEnvAmt * ampEnvValue * 4.0f;
            const float cut = juce::jlimit (20.0f, nyquistish,
                                            filterBaseHz * std::pow (2.0f, octaves));

            // TPT SVF：g = tan(pi * fc / fs)，k = 1/Q
            const float g = std::tan (juce::MathConstants<float>::pi
                                      * juce::jlimit (1.0f, nyquistish, cut) / (float) sr);
            const float k = 1.0f / juce::jlimit (0.5f, 8.0f, filterQ);
            const float a1 = 1.0f / (1.0f + g * (g + k));

            const float v3 = sample - svfIc2;
            const float v1 = a1 * svfIc1 + g * a1 * v3;
            const float v2 = svfIc2 + g * v1;

            svfIc1 = 2.0f * v1 - svfIc1;
            svfIc2 = 2.0f * v2 - svfIc2;

            // 数值保护：即使 TPT 无条件稳定，也防住极端输入
            if (! std::isfinite (v2) || ! std::isfinite (svfIc1) || ! std::isfinite (svfIc2))
            {
                svfIc1 = 0.0f;
                svfIc2 = 0.0f;
            }
            else
            {
                // TPT SVF 同时给出三种输出，切换零成本：
                //   v2 = 带通, lp = v2 - k*v1 不是标准式；
                // 这里用标准关系：hp = in - k*bp - lp
                const float bp = v1;
                const float lp = v2;
                const float hp = sample - k * bp - lp;

                if (filterType == 1)      sample = bp;   // 带通
                else if (filterType == 2) sample = hp;   // 高通
                else                      sample = lp;   // 低通（默认）
            }
        }

        sample *= ampEnvValue * ampGain * velocityGain;

        // --- 噪声瞬态层 ---------------------------------------------------
        // 独立于 Sub 的短噪声，给音头加"啪"感。
        // 用自己的一阶高通去掉低频轰隆，只留"咔"的部分。
        if (noiseAmount > 1.0e-4f)
        {
            const float nTime = (float) (elapsedSamples / sr);
            const float nDecay = juce::jmax (1.0e-4f, noiseDecaySeconds);
            const float nEnv = std::exp (-nTime / nDecay);   // 指数衰减

            const float raw = noiseGen.next();

            // 一阶高通：去掉低频，避免和 Sub 打架
            const float hpCut = juce::jlimit (20.0f, (float) (sr * 0.45), noiseHighPassHz);
            const float hpCoeff = juce::jlimit (0.0f, 1.0f,
                1.0f - std::exp (-juce::MathConstants<float>::twoPi * hpCut / (float) sr));
            noiseHpState += hpCoeff * (raw - noiseHpState);
            const float hpNoise = raw - noiseHpState;

            sample += hpNoise * nEnv * noiseAmount * velocityGain;
        }

        // --- 立体声宽度 ---------------------------------------------------
        // 独立于滤波器实现：用两个**不同截止频率**的一阶高通取出
        // 中高频的差异分量，左右反相相加。
        // 低频几乎不受影响（两路高通在低频输出都接近 0），
        // 因此不会破坏低频的单声道兼容性 —— 这对底鼓很重要。
        if (numChannels >= 2 && widthAmount > 1.0e-4f)
        {
            const float w = juce::jlimit (0.0f, 2.0f, widthAmount);

            // 两路高通，截止频率略有差异
            const float c1 = juce::jlimit (0.0f, 1.0f,
                1.0f - std::exp (-juce::MathConstants<float>::twoPi * 250.0f / (float) sr));
            const float c2 = juce::jlimit (0.0f, 1.0f,
                1.0f - std::exp (-juce::MathConstants<float>::twoPi * 420.0f / (float) sr));

            widthHp1 += c1 * (sample - widthHp1);
            widthHp2 += c2 * (sample - widthHp2);

            if (! std::isfinite (widthHp1)) widthHp1 = 0.0f;
            if (! std::isfinite (widthHp2)) widthHp2 = 0.0f;

            const float high1 = sample - widthHp1;
            const float high2 = sample - widthHp2;
            const float diff  = (high1 - high2) * 0.5f;

            output.addSample (0, startSample + i, sample + diff * w);
            output.addSample (1, startSample + i, sample - diff * w);
        }
        else
        {
            for (int ch = 0; ch < numChannels; ++ch)
                output.addSample (ch, startSample + i, sample);
        }

        elapsedSamples += 1.0;
    }

    // 包络走完且已安静 -> 结束发声，释放 CPU
    if (maxEnvValue < 1.0e-4f && elapsedSamples > (double) sr * 0.01)
    {
        active = false;
        return false;
    }

    return true;
}

} // namespace kf
