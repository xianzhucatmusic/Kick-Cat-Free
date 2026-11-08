#include "Distortion.h"

namespace kf
{

void Distortion::prepare (double newSampleRate, int /*maxBlockSize*/)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    for (auto& d : dcBlockers)
        d.prepare (sampleRate);
    reset();
}

void Distortion::reset()
{
    for (auto& d : dcBlockers)
        d.reset();
    for (auto& d : biasBlockers)
        d.reset();
    for (auto& t : tapeFilters)
        t = TapeFilter{};
    for (auto& t : preLp)   t = TapeFilter{};
    for (auto& t : preHp)   t = TapeFilter{};
    for (auto& t : postLp)  t = TapeFilter{};
    for (auto& t : postHp)  t = TapeFilter{};
    for (auto& t : exciterLp) t = TapeFilter{};
    smoothedComp = 1.0f;
}

float Distortion::shape (DistortionType type, float x) noexcept
{
    switch (type)
    {
        case DistortionType::Tube:
            // 电子管：非对称软饱和。正负半周增益不同 -> 偶次谐波，
            // 这是「暖」和「厚」的听感来源。
            return dsp::analogSaturate (x);

        case DistortionType::Tape:
            // 磁带：软压缩 + 轻微折叠，配合后级高频滚降
            {
                const float comp = std::tanh (x * 0.8f) * 1.1f;
                return comp - 0.05f * comp * comp * comp;
            }

        case DistortionType::Hard:
            // 硬削波：干脆的方波化
            return juce::jlimit (-0.85f, 0.85f, x) / 0.85f;

        case DistortionType::Fold:
            // 波折叠：超过 +/-1 后折返，产生密集的金属谐波
            {
                float v = x;
                for (int i = 0; i < 8 && (v > 1.0f || v < -1.0f); ++i)
                {
                    if (v > 1.0f)  v = 2.0f - v;
                    if (v < -1.0f) v = -2.0f - v;
                }
                return v;
            }

        case DistortionType::Transformer:
            // 变压器磁饱和：低频先饱和、高频几乎线性。
            // 曲线本身接近 tanh，但**频响**才是它的特征（见 process）。
            // 这里加一点点三次项模拟磁芯的磁滞。
            return std::tanh (x) + 0.06f * x * x * x;

        case DistortionType::Console:
            // 调音台通道条：极轻的连续饱和。
            // 关键是"几乎不失真"—— 只在峰值附近轻微压缩，
            // 产生若有若无的密度感，而不是明显的失真。
            return x - (x * x * x) / 3.0f * 0.12f;

        case DistortionType::Germanium:
            // 锗二极管：低电压下就开始导通，削波拐点"软但不圆滑"。
            // 用低阈值的 tanh 再叠加一点硬削，得到粗糙的复古脏感。
            {
                const float soft = std::tanh (x * 1.8f) / 1.8f;
                return juce::jlimit (-0.7f, 0.7f, soft) / 0.7f;
            }

        case DistortionType::Fuzz:
            // 锗管法兹：强非对称 + 门限。
            // 负半周比正半周更容易削 -> 偶次谐波很强；
            // 小信号被"掐掉"产生门限感（这是 fuzz 的标志性特征）。
            {
                const float v = x + 0.18f;              // 非对称偏置
                const float clipped = juce::jlimit (-0.75f, 0.75f, v) / 0.75f;
                return clipped - 0.18f;
            }

        case DistortionType::Radio:
            // 无线电/窄带：带通 + 硬削波。
            // 频响由 process 里的 preHp/preLp 实现，这里只做削波。
            return juce::jlimit (-0.6f, 0.6f, x) / 0.6f;

        case DistortionType::Exciter:
            // 谐波激励器：只对高频加谐波，低频原样通过。
            // 分离逻辑在 process 里（低频走干声，高频才过非线性）。
            return dsp::analogSaturate (x);

        case DistortionType::NumTypes:
        default:
            return x;
    }
}

//==============================================================================
// 每种染色模型的频响特征
//==============================================================================
Distortion::ToneCurve Distortion::toneCurveFor (DistortionType type, float tone) noexcept
{
    // tone 0..1：0 = 暗，1 = 亮。默认 0.5 为各模型的中性点。
    ToneCurve c;

    switch (type)
    {
        case DistortionType::Tube:
            // 电子管：轻微高频滚降 + 低频略收紧（输出变压器）
            c.preLpHz  = 16000.0f;
            c.postHpHz = 18.0f;
            break;

        case DistortionType::Tape:
            // 磁带：明显的高频损失（磁头间隙损耗）+ 低频"顶"（磁头隆起）
            c.preLpHz  = 12000.0f;
            c.postLpHz = 11000.0f;
            c.postHpHz = 30.0f;
            break;

        case DistortionType::Hard:
            // 硬削波：入口先去掉极低频，避免低频把整体推爆
            c.preHpHz  = 30.0f;
            c.postLpHz = 18000.0f;
            break;

        case DistortionType::Fold:
            c.preHpHz  = 40.0f;
            c.postLpHz = 16000.0f;
            break;

        case DistortionType::Transformer:
            // 变压器：低频饱和最明显（20-60Hz 被压缩），
            // 高频有轻微相位偏移（用一阶低通近似）
            c.preHpHz  = 15.0f;
            c.postLpHz = 15000.0f;
            c.lowSaturate = true;      // 低频额外饱和
            break;

        case DistortionType::Console:
            // 调音台：极宽的频响，只有输入/输出变压器的一点点滚降
            c.postLpHz = 20000.0f;
            c.postHpHz = 12.0f;
            break;

        case DistortionType::Germanium:
            c.preLpHz  = 9000.0f;
            c.postLpHz = 8000.0f;
            c.postHpHz = 60.0f;
            break;

        case DistortionType::Fuzz:
            // 法兹：低频被削掉很多（经典 fuzz 的"薄"），中高频突出
            c.preHpHz  = 120.0f;
            c.postLpHz = 10000.0f;
            break;

        case DistortionType::Radio:
            // 无线电：窄带通 —— 这是它最核心的特征
            c.preHpHz  = 500.0f;
            c.preLpHz  = 3000.0f;
            c.postHpHz = 500.0f;
            c.postLpHz = 3000.0f;
            break;

        case DistortionType::Exciter:
            // 激励器：低频原样通过，只处理高频。
            //
            // 注意这里**不能**设 preHp —— 频响已经由 highOnly 的
            // 高通/低通分离逻辑自己实现了。再加一个 preHp(2500Hz)
            // 会把低频在分离**之前**就砍掉，于是"原样通过"变成"完全消失"
            // （实测 100Hz 输出掉到 0.02）。
            c.postLpHz = 20000.0f;
            c.highOnly = true;
            break;

        case DistortionType::NumTypes:
        default:
            break;
    }

    // tone 旋钮对"输出低通"做 ±1 八度调整，让用户能进一步收暗/放亮。
    //
    // 注意 0 表示"不启用该滤波器"，必须单独判断 ——
    // 之前直接对 0 做乘法再 jlimit(800, ...)，会把 0 夹成 800Hz，
    // 于是**每个模型都被加上一个意外的 800Hz 低通**（Tube 高频掉了 17dB）。
    const float octaves = (tone - 0.5f) * 2.0f;          // -1..+1

    // postLp 是"输出低通"。0 表示不启用，此时如果用户要收暗，
    // 就从直通开始建一个（最暗到 1.5kHz）。
    if (c.postLpHz > 0.0f)
    {
        c.postLpHz = juce::jlimit (800.0f, 20000.0f,
                                   c.postLpHz * std::pow (2.0f, octaves));
    }
    else if (tone < 0.5f)
    {
        const float t = tone / 0.5f;                      // 0..1
        c.postLpHz = juce::jlimit (1500.0f, 20000.0f,
                                   20000.0f * std::pow (0.075f, 1.0f - t));
    }

    // preLp 是**进入非线性之前**的低通。它对音色的影响往往比 postLp 更大
    // （决定了哪些频率会被染色）。之前 tone 完全没作用到它，
    // 导致 Tube/Germanium/Fuzz 这些以 preLp 为主的模型"tone 没反应"。
    if (c.preLpHz > 0.0f)
        c.preLpHz = juce::jlimit (800.0f, 20000.0f,
                                  c.preLpHz * std::pow (2.0f, octaves));

    return c;
}

void Distortion::process (juce::AudioBuffer<float>& buffer,
                          int startSample, int numSamples,
                          DistortionType type,
                          float drive,
                          float mix,
                          float bias,
                          float tone,
                          bool autoGain)
{
    if (numSamples <= 0)
        return;

    const float d = juce::jlimit (1.0f, 20.0f, drive);
    const float wet = juce::jlimit (0.0f, 1.0f, mix);
    const float biasAmt = juce::jlimit (-0.5f, 0.5f, bias);

    // 完全干声且无偏置时直接跳过，省 CPU
    if (wet <= 1.0e-4f)
        return;

    //--- 频响特征 --------------------------------------------------------
    const auto curve = toneCurveFor (type, tone);

    auto coeffFor = [this] (float hz)
    {
        if (hz <= 0.0f || hz >= 20000.0f)
            return 1.0f;   // 1 = 直通（一阶滤波系数取 1 时状态立刻跟随输入）
        return juce::jlimit (0.0f, 1.0f,
            1.0f - std::exp (-juce::MathConstants<float>::twoPi * hz / (float) sampleRate));
    };

    const float preHpC  = coeffFor (curve.preHpHz);
    const float preLpC  = coeffFor (curve.preLpHz);
    const float postHpC = coeffFor (curve.postHpHz);
    const float postLpC = coeffFor (curve.postLpHz);

    const bool hasPreHp  = curve.preHpHz  > 0.0f;
    const bool hasPreLp  = curve.preLpHz  > 0.0f && curve.preLpHz < 20000.0f;
    const bool hasPostHp = curve.postHpHz > 0.0f;
    const bool hasPostLp = curve.postLpHz > 0.0f && curve.postLpHz < 20000.0f;

    //--- 增益补偿 --------------------------------------------------------
    // 非线性会把平均电平推高。手动补偿按 1/sqrt(drive)，
    // 与原来的行为一致；Auto Gain 额外按模型做微调。
    float compGain = 1.0f / std::sqrt (d);

    if (autoGain)
    {
        switch (type)
        {
            // Console 的曲线几乎线性，但实测输出比其他模型高约 2 倍
            // （它的 shape 不压缩峰值），所以要**压**而不是提。
            case DistortionType::Console:     compGain *= 0.55f; break;
            case DistortionType::Fuzz:        compGain *= 0.9f;  break;
            case DistortionType::Fold:        compGain *= 1.1f;  break;
            case DistortionType::Radio:       compGain *= 1.3f;  break;  // 窄带后能量掉得多
            case DistortionType::Exciter:     compGain *= 1.2f;  break;
            default: break;
        }
    }

    // 平滑补偿增益，避免开关 Auto Gain 时爆音
    const float smoothCoeff = juce::jlimit (0.0f, 1.0f, 1.0f - std::exp (-1.0f / (0.01f * (float) sampleRate)));

    const int numChannels = juce::jmin (buffer.getNumChannels(), 2);

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* data = buffer.getWritePointer (ch, startSample);

        for (int i = 0; i < numSamples; ++i)
        {
            const float dry = data[i];
            float s = dry;

            // --- 染色前频响
            if (hasPreHp)
            {
                preHp[(size_t) ch].lp += preHpC * (s - preHp[(size_t) ch].lp);
                s = s - preHp[(size_t) ch].lp;
            }
            if (hasPreLp)
            {
                preLp[(size_t) ch].lp += preLpC * (s - preLp[(size_t) ch].lp);
                s = preLp[(size_t) ch].lp;
            }

            // --- 偏置（Bias）
            // 给波形加直流偏移再进非线性，正负半周不再对称，
            // 于是产生偶次谐波 —— 这是"暖"与"脏"之间最重要的一个旋钮。
            if (std::abs (biasAmt) > 1.0e-4f)
                s += biasAmt;

            // --- 非线性
            if (curve.highOnly)
            {
                // 激励器：低频**原样通过**，只对高频加谐波。
                //
                // 这里不能乘 compGain —— 补偿增益是针对"整体被非线性压过"
                // 的情况，而低频根本没有进非线性。之前乘了两次，
                // 导致 100Hz 的输出掉到 0.02（几乎没声音）。
                exciterLp[(size_t) ch].lp += 0.15f * (s - exciterLp[(size_t) ch].lp);
                const float low = exciterLp[(size_t) ch].lp;
                const float high = s - low;

                // 只对高频做归一化：让高频部分的电平与原来相当
                const float excited = shape (type, high * d) / std::max (1.0f, d * 0.35f);
                s = low + excited;
            }
            else
            {
                s = shape (type, s * d);
            }

            // --- 去掉偏置带来的直流
            if (std::abs (biasAmt) > 1.0e-4f)
                s = biasBlockers[(size_t) ch].process (s);

            // --- 染色后频响（模拟输出变压器 / 磁头 / 后级）
            if (hasPostLp)
            {
                postLp[(size_t) ch].lp += postLpC * (s - postLp[(size_t) ch].lp);
                s = postLp[(size_t) ch].lp;
            }
            if (hasPostHp)
            {
                postHp[(size_t) ch].lp += postHpC * (s - postHp[(size_t) ch].lp);
                s = s - postHp[(size_t) ch].lp;
            }

            // --- 补偿增益（平滑过渡）
            //
            // highOnly（激励器）跳过这一步：它的低频根本没进非线性，
            // 不需要为"非线性造成的电平变化"做补偿。之前这里仍然乘了
            // compGain(≈0.5)，把原样通过的低频压掉了 45%。
            if (! curve.highOnly)
            {
                smoothedComp += smoothCoeff * (compGain - smoothedComp);
                s *= smoothedComp;
            }

            s = dcBlockers[(size_t) ch].process (s);   // 末端彻底清直流

            data[i] = dry * (1.0f - wet) + s * wet;
        }
    }
}

} // namespace kf
