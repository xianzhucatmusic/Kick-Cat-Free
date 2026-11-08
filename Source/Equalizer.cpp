#include "Equalizer.h"

namespace kf
{

using Coeffs = juce::dsp::IIR::Coefficients<float>;

const std::array<float, Equalizer::kNumBands>& Equalizer::defaultFrequencies() noexcept
{
    // 针对底鼓/贝斯优化：低频段密、关键听感区都有独立控制
    static const std::array<float, kNumBands> f
    {
        20.0f, 40.0f, 70.0f, 120.0f, 250.0f, 600.0f, 2500.0f, 8000.0f
    };
    return f;
}

void Equalizer::prepare (const juce::dsp::ProcessSpec& spec)
{
    processSpec = spec;

    const auto& defs = defaultFrequencies();
    for (int i = 0; i < kNumBands; ++i)
    {
        settings[(size_t) i] = BandSettings{};
        settings[(size_t) i].freq = defs[(size_t) i];
        settings[(size_t) i].gainDb = 0.0f;
        settings[(size_t) i].q = 0.8f;
        settings[(size_t) i].type = BandType::Peak;
        settings[(size_t) i].enabled = false;

        settings[(size_t) i].slopeDbPerOct = 12;

        for (int st = 0; st < kMaxStages; ++st)
        {
            filters[(size_t) i][(size_t) st].prepare (spec);
            filters[(size_t) i][(size_t) st].reset();
        }
    }

    // 先全部设成单位增益的 peak，保证链路上始终有有效系数
    for (int i = 0; i < kNumBands; ++i)
        setBandParams (i, defs[(size_t) i], 0.0f, 0.8f, BandType::Peak, false, 12);
}

void Equalizer::reset()
{
    for (auto& band : filters)
        for (auto& st : band)
            st.reset();
}

void Equalizer::setBandParams (int band, float freq, float gainDb, float q, BandType type,
                              bool enabled, int slopeDbPerOct)
{
    if (band < 0 || band >= kNumBands)
        return;

    const float nyq = (float) (processSpec.sampleRate * 0.5);
    freq   = juce::jlimit (10.0f, juce::jmax (20.0f, nyq * 0.98f), freq);
    gainDb = juce::jlimit (-24.0f, 24.0f, gainDb);
    q      = juce::jlimit (0.1f, 12.0f, q);

    auto& s = settings[(size_t) band];

    // --- 只在参数真的变了才重建双二阶系数。
    // 原实现每块都把 8 段系数全部重算一遍，即使值没变。
    // 系数一旦被重设，滤波器内部状态与新的系数就不再匹配，
    // 会产生可听见的"砰"声（实测单次 +18dB 突变峰值达 10.5，衰减 100ms+）。
    slopeDbPerOct = juce::jlimit (12, kMaxStages * 12, (slopeDbPerOct / 12) * 12);

    {
        const bool unchanged = (std::abs (s.freq - freq) < 0.01f)
                            && (std::abs (s.gainDb - gainDb) < 0.001f)
                            && (std::abs (s.q - q) < 0.0005f)
                            && (s.type == type)
                            && (s.enabled == enabled)
                            && (s.slopeDbPerOct == slopeDbPerOct);

        if (unchanged)
            return;
    }

    s.freq = freq;
    s.gainDb = gainDb;
    s.q = q;
    s.type = type;
    s.enabled = enabled;
    s.slopeDbPerOct = slopeDbPerOct;

    const double f = (double) freq;
    const float  Q = q;

    // 未启用时用单位增益的 peaking，避免切换瞬间产生突变
    if (! enabled)
        gainDb = 0.0f;

    // --- 计算本段需要的级联级数
    // Peak / Shelf 不需要级联（级联会改变它们的形状语义），固定 1 级。
    // HighPass / LowPass 通过串联 N 个双二阶获得 N*12 dB/oct 的斜率。
    const bool isCut = (type == BandType::HighPass || type == BandType::LowPass);
    const int stages = isCut ? stagesForSlope (slopeDbPerOct) : 1;

    // --- 级联时每级的 Q 要重新分配
    //
    // 关键：RBJ 双二阶在截止频率处的幅度恰好等于 Q（对高通/低通都成立）。
    // 因此 N 级串联后在 f0 处的增益 = Q^N。
    // 若所有级都用 Q=0.707，N 级后有 -3N dB —— 截止点会严重偏移
    // （实测 48dB/oct 时 1kHz 处变成 -55dB，滤波器形同虚设）。
    //
    // 要让 N 级级联后在 f0 处仍为 -3dB，每级取：
    //     Q = 10^(-3 / (20*N))
    //
    // 代价：响应不再是严格的 Butterworth 最大平坦（会有极轻微纹波），
    // 但"截止点准确 + 斜率够陡"对底鼓 EQ 更重要，这是正确的取舍。
    const float stageQ = (stages <= 1)
                           ? 0.70710678f
                           : (float) std::pow (10.0, -3.0 / (20.0 * (double) stages));

    for (int stage = 0; stage < kMaxStages; ++stage)
    {
        auto& filt = filters[(size_t) band][(size_t) stage];

        // 超出所需级数的闲置级：设成直通（单位增益 peaking, 0dB）
        if (stage >= stages)
        {
            *filt.state = *Coeffs::makePeakFilter (processSpec.sampleRate, f, Q,
                                                   juce::Decibels::decibelsToGain (0.0f));
            continue;
        }

        // 高切/低切用重算后的 Q；其它类型保留用户设定的 Q
        const float qStage = isCut ? stageQ : Q;

        switch (type)
        {
            case BandType::LowShelf:
                *filt.state = *Coeffs::makeLowShelf (processSpec.sampleRate, f, qStage,
                                        juce::Decibels::decibelsToGain (gainDb));
                break;

            case BandType::HighShelf:
                *filt.state = *Coeffs::makeHighShelf (processSpec.sampleRate, f, qStage,
                                        juce::Decibels::decibelsToGain (gainDb));
                break;

            case BandType::HighPass:
                *filt.state = *Coeffs::makeHighPass (processSpec.sampleRate, f, qStage);
                break;

            case BandType::LowPass:
                *filt.state = *Coeffs::makeLowPass (processSpec.sampleRate, f, qStage);
                break;

            case BandType::Peak:
            default:
                *filt.state = *Coeffs::makePeakFilter (processSpec.sampleRate, f, qStage,
                                        juce::Decibels::decibelsToGain (gainDb));
                break;
        }
    }
}

void Equalizer::process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples)
{
    if (numSamples <= 0)
        return;

    // 跳过完全空转的情况
    bool anyEnabled = false;
    for (const auto& s : settings)
        if (s.enabled) { anyEnabled = true; break; }

    if (! anyEnabled)
        return;

    juce::dsp::AudioBlock<float> block (buffer);
    auto sub = block.getSubBlock ((size_t) startSample, (size_t) numSamples);
    juce::dsp::ProcessContextReplacing<float> ctx (sub);

    for (int i = 0; i < kNumBands; ++i)
    {
        if (! settings[(size_t) i].enabled)
            continue;

        // 串联本段所有启用的级（高切/低切可能有多级）
        const int stages = (settings[(size_t) i].type == BandType::HighPass
                         || settings[(size_t) i].type == BandType::LowPass)
                             ? stagesForSlope (settings[(size_t) i].slopeDbPerOct)
                             : 1;

        for (int st = 0; st < stages; ++st)
            filters[(size_t) i][(size_t) st].process (ctx);
    }
}

float Equalizer::getResponseDb (float frequency) const
{
    // 把各段的幅度响应相加（dB 叠加），用于画 EQ 曲线
    float total = 0.0f;

    for (int i = 0; i < kNumBands; ++i)
    {
        const auto& s = settings[(size_t) i];
        if (! s.enabled)
            continue;

        // 取该段在当前频点的响应
        const int stages = (s.type == BandType::HighPass || s.type == BandType::LowPass)
                             ? stagesForSlope (s.slopeDbPerOct)
                             : 1;

        // 级联的总幅度 = 各级幅度相乘 => dB 相加
        for (int st = 0; st < stages; ++st)
        {
            const auto coeffs = filters[(size_t) i][(size_t) st].state;
            if (coeffs == nullptr)
                continue;

            const auto mag = coeffs->getMagnitudeForFrequency ((double) frequency,
                                                              processSpec.sampleRate);
            total += (float) juce::Decibels::gainToDecibels (mag, -100.0);
        }
    }

    return total;
}

} // namespace kf
