#include "SampleLibrary.h"
#include "DspUtils.h"
#include <random>
#include <cmath>

namespace kf
{

//==============================================================================
// 180 个 Click 采样的程序化生成
//
// 设计原则：所有采样都「短」（< 60ms）、「干净」（无直流、首尾归零），
// 并且按类别有明显的听感区分，方便用户快速找到想要的瞬态性格。
// 变体之间通过确定性随机（固定种子）改变频率、时长、噪声量等参数，
// 保证每次生成的库完全一致。
//==============================================================================

namespace
{
    /** 确定性 RNG：同一个 (category, variant) 永远得到同样的结果。 */
    struct StableRng
    {
        std::mt19937 gen;
        explicit StableRng (uint32_t seed) : gen (seed) {}
        float next (float lo, float hi)
        {
            std::uniform_real_distribution<float> d (lo, hi);
            return d (gen);
        }
    };

    /** 指数衰减包络，带可调曲线的起音。 */
    inline float decayEnv (float t, float attack, float decay, float curve) noexcept
    {
        if (t < 0.0f) return 0.0f;
        if (t < attack)
            return attack > 1.0e-6f ? (t / attack) : 1.0f;
        const float u = (t - attack) / juce::jmax (1.0e-6f, decay);
        if (u >= 1.0f) return 0.0f;
        return std::pow (1.0f - u, curve);
    }
}

juce::String SampleLibrary::getCategoryName (int category)
{
    switch (category)
    {
        case 0: return "Click";
        case 1: return "Tick";
        case 2: return "Noise";
        case 3: return "Transient";
        case 4: return "Beater";
        case 5: return "Digital";
        default: return "Unknown";
    }
}

SampleLibrary::SampleLibrary()
{
    buildEntries();
}

void SampleLibrary::buildEntries()
{
    entries.clear();
    entries.reserve ((size_t) kTotalSamples);

    for (int c = 0; c < kNumCategories; ++c)
    {
        for (int v = 0; v < kVariantsPerCategory; ++v)
        {
            Entry e;
            e.category = c;
            e.variant = v;
            // 名字带两位编号，便于在列表里排序和识别
            e.name = getCategoryName (c) + " " + juce::String (v + 1).paddedLeft ('0', 2);
            entries.push_back (e);
        }
    }

    cache.clear();
    cache.resize ((size_t) kTotalSamples);
}

void SampleLibrary::setSampleRate (double newSampleRate)
{
    if (std::abs (newSampleRate - sampleRate) < 1.0e-6)
        return;

    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    // 采样率变了，缓存全部失效
    cache.clear();
    cache.resize ((size_t) kTotalSamples);
}

void SampleLibrary::initialise (double newSampleRate)
{
    setSampleRate (newSampleRate);
}

juce::AudioBuffer<float> SampleLibrary::renderSample (int category, int variant) const
{
    const double sr = sampleRate;
    StableRng rng ((uint32_t) (category * 1000 + variant * 7 + 12345));

    // 每个采样的时长：click 类很短，噪声类稍长
    float durMs = 12.0f;
    switch (category)
    {
        case 0: durMs = rng.next (6.0f, 18.0f);  break;   // Click
        case 1: durMs = rng.next (3.0f, 9.0f);   break;   // Tick
        case 2: durMs = rng.next (15.0f, 55.0f); break;   // Noise
        case 3: durMs = rng.next (4.0f, 12.0f);  break;   // Transient
        case 4: durMs = rng.next (20.0f, 60.0f); break;   // Beater
        case 5: durMs = rng.next (5.0f, 25.0f);  break;   // Digital
        default: break;
    }

    const int len = juce::jmax (8, (int) (durMs * 0.001 * sr));
    juce::AudioBuffer<float> buf (1, len);
    buf.clear();
    auto* d = buf.getWritePointer (0);

    // 基础频率：变体之间拉开，覆盖从"闷"到"脆"的范围
    const float baseFreq = rng.next (900.0f, 5200.0f);
    const float attackS  = rng.next (0.0001f, 0.0012f);
    const float decayS   = (float) (durMs * 0.001);
    const float curve    = rng.next (1.6f, 4.5f);

    // 噪声成分（不同类别差异很大）
    float noiseAmt = 0.0f;
    switch (category)
    {
        case 0: noiseAmt = rng.next (0.0f, 0.12f);  break;
        case 1: noiseAmt = rng.next (0.05f, 0.25f); break;
        case 2: noiseAmt = rng.next (0.85f, 1.0f);  break;
        case 3: noiseAmt = rng.next (0.35f, 0.6f);  break;
        case 4: noiseAmt = rng.next (0.10f, 0.35f); break;
        case 5: noiseAmt = rng.next (0.15f, 0.45f); break;
        default: break;
    }

    // 简易带通（状态变量）给噪声塑形，避免宽带白噪听起来像"嘶"。
    //
    // 稳定性要点：Chamberlin 状态变量滤波器的系数 f0 = 2*sin(pi*fc/fs)，
    // 当 f0 接近 2（即 fc 接近 Nyquist）且阻尼 q1 较大时，滤波器会**发散到无穷**，
    // 生成出 NaN 采样。实测 f0 > ~1.4 就会炸。
    // 因此这里把中心频率限制在采样率的 1/8 以内（f0 <= 2*sin(pi/8) ≈ 0.765），
    // 远离不稳定区，同时也更符合"click 采样"的听感需求。
    const float bpMax  = juce::jmax (200.0f, (float) sr * 0.125f);
    const float bpMin  = juce::jmin (500.0f, bpMax * 0.5f);
    const float bpFreq = juce::jlimit (bpMin, bpMax, baseFreq * rng.next (0.8f, 1.5f));
    const float bpQ    = rng.next (0.7f, 2.5f);
    const float w      = (float) (juce::MathConstants<double>::twoPi * bpFreq / sr);
    const float f0     = 2.0f * std::sin (w * 0.5f);
    const float q1     = 1.0f / bpQ;
    float low = 0.0f, band = 0.0f;

    // 数字类的位元量化
    const int bits = juce::jlimit (2, 12, (int) rng.next (3.0f, 11.0f));
    const float quantStep = 2.0f / (float) (1 << bits);

    for (int i = 0; i < len; ++i)
    {
        const float t = (float) (i / sr);
        const float env = decayEnv (t, attackS, decayS, curve);

        // 音调成分：基频 + 一个泛音（让 click 更有"实体感"）
        float tone = std::sin (juce::MathConstants<float>::twoPi * baseFreq * t);
        tone += 0.35f * std::sin (juce::MathConstants<float>::twoPi * baseFreq * rng.next(1.0f, 3.0f) * t);

        // 噪声成分（带通塑形）
        const float white = rng.next (-1.0f, 1.0f);
        band += f0 * (white - band - q1 * low);
        low  += f0 * band;
        const float filtered = band;

        float s = tone * (1.0f - noiseAmt) + filtered * noiseAmt * 1.4f;

        // 类别特有处理
        if (category == 5)  // Digital: 量化 + 轻微 FM 味
            s = std::round (s / quantStep) * quantStep;

        if (category == 4)  // Beater: 加木质共鸣（两个共振峰）
        {
            const float res1 = std::sin (juce::MathConstants<float>::twoPi * rng.next (180.0f, 260.0f) * t);
            const float res2 = std::sin (juce::MathConstants<float>::twoPi * rng.next (380.0f, 520.0f) * t);
            s = s * 0.55f + (res1 * 0.28f + res2 * 0.17f) * env;
        }

        d[i] = s * env;
    }

    // --- 收尾：淡出首尾各 32 个采样，避免播放时边界产生咔哒
    const int fade = juce::jmin (32, len / 4);
    for (int i = 0; i < fade; ++i)
    {
        const float g = (float) i / (float) fade;
        d[i] *= g;
        d[len - 1 - i] *= g;
    }

    // --- 峰值归一化到 -1.5dB，保证库内各采样响度大致一致
    float peak = 0.0f;
    for (int i = 0; i < len; ++i)
        peak = juce::jmax (peak, std::abs (d[i]));

    if (peak > 1.0e-6f)
    {
        const float g = 0.84f / peak;
        for (int i = 0; i < len; ++i)
            d[i] *= g;
    }

    // 最后一道防线：把任何非有限值（NaN/Inf）清零。
    // 生成的采样会被写进内嵌库并长期复用，一旦掺入 NaN 会污染整条信号链，
    // 而且极难定位来源 —— 这里直接堵死。
    for (int i = 0; i < len; ++i)
        if (! std::isfinite (d[i]))
            d[i] = 0.0f;

    return buf;
}

const juce::AudioBuffer<float>* SampleLibrary::getSampleBuffer (int index)
{
    if (index < 0 || index >= kTotalSamples)
        return nullptr;

    auto& slot = cache[(size_t) index];
    if (slot == nullptr)
    {
        const auto& e = entries[(size_t) index];
        slot = std::make_unique<juce::AudioBuffer<float>> (renderSample (e.category, e.variant));
    }

    return slot.get();
}

bool SampleLibrary::loadIntoLayer (int index, ClickLayer& layer)
{
    const auto* buf = getSampleBuffer (index);
    if (buf == nullptr)
        return false;

    if (layer.loadFromBuffer (*buf, sampleRate))
    {
        // 用库里的名字标记这一层，UI 上能看到载入的是哪个
        layer.setLoadedName (getEntry (index).name);
        return true;
    }
    return false;
}

} // namespace kf
