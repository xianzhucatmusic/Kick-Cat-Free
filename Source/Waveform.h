#pragma once

#include <juce_dsp/juce_dsp.h>
#include <cmath>

namespace kf
{

//==============================================================================
/** 可选波形。
 *
 *  鼓合成里不同波形各有用途：
 *   - Sine      纯正低频，Sub 的默认选择
 *   - Triangle  比 sine 多一点谐波，听起来更"有芯"
 *   - Saw       谐波丰富，做硬派 kick 的振荡器
 *   - Square    更方、更电子，适合 hardstyle / hardcore
 *   - Noise     白噪，做 click 的"沙"感或军鼓底
 *   - Pulse     窄脉冲，做极短的 click 瞬态
 */
enum class Waveform
{
    Sine = 0,
    Triangle,
    Saw,
    Square,
    Noise,
    Pulse,
    NumWaveforms
};

namespace wave
{
    /** 波形名（下拉列表用）。 */
    inline const char* name (Waveform w)
    {
        switch (w)
        {
            case Waveform::Sine:     return "Sine";
            case Waveform::Triangle: return "Triangle";
            case Waveform::Saw:      return "Saw";
            case Waveform::Square:   return "Square";
            case Waveform::Noise:    return "Noise";
            case Waveform::Pulse:    return "Pulse";
            default:                 return "Sine";
        }
    }

    inline juce::StringArray allNames()
    {
        return { "Sine", "Triangle", "Saw", "Square", "Noise", "Pulse" };
    }

    /** 零均值脉冲：占空比 duty 时高电平 +1、低电平 -duty/(1-duty)。
     *  这样无论占空比多少，周期均值都恰好为 0（无直流）。 */
    inline float pulseValue (float phase, float duty) noexcept
    {
        duty = juce::jlimit (0.01f, 0.99f, duty);
        const float lo = -duty / (1.0f - duty);
        phase -= std::floor (phase);
        return phase < duty ? 1.0f : lo;
    }

    /** 由相位 [0,1) 求波形值，范围约 [-1, 1]。
     *  用解析式而非查表：这些波形都很简单，解析式更精确且无插值误差。 */
    inline float evaluate (Waveform w, float phase) noexcept
    {
        // 归一化到 [0,1)
        phase -= std::floor (phase);
        const float twoPi = juce::MathConstants<float>::twoPi;

        switch (w)
        {
            case Waveform::Sine:
                return std::sin (phase * twoPi);

            case Waveform::Triangle:
                // 0 -> 1 -> 0 -> -1 -> 0，端点连续
                return 4.0f * std::abs (phase - 0.5f) - 1.0f;

            case Waveform::Saw:
                // 上升锯齿，范围 [-1, 1)，均值恰好为 0
                return 2.0f * phase - 1.0f;

            case Waveform::Square:
                return phase < 0.5f ? 1.0f : -1.0f;

            case Waveform::Pulse:
                // 窄脉冲：占空比 12.5%，做短促 click。
                // 必须做**零均值**处理：若高电平 +1 / 低电平 -1，占空比 12.5% 时
                // 直流分量高达 -0.75，会让低音单元持续偏置（浪费动态、损伤音箱）。
                // 这里把低电平设为 -duty/(1-duty)，使整个周期的均值为 0。
                return wave::pulseValue (phase, 0.125f);

            case Waveform::Noise:
                // 无状态白噪（调用方应自行决定是否使用相位）
                return 0.0f;   // 由 caller 用随机数替代

            case Waveform::NumWaveforms:
            default:
                return 0.0f;
        }
    }

    /** 带弯曲的锯齿，且严格零均值。
     *
     *  直接对 ph 取幂 (ph^bend) 会让波形均值偏离 0（实测达 0.3~0.6 的直流），
     *  底鼓里出现直流会浪费动态余量、让低音单元持续偏置。
     *  这里对解析式求积分得到均值并减掉，保证任何 bend 下直流都为 0。 */
    inline float sawValue (float phase, float bend) noexcept
    {
        bend = juce::jlimit (0.05f, 3.0f, bend);
        phase -= std::floor (phase);
        const float v = 2.0f * std::pow (phase, bend) - 1.0f;
        // ∫0..1 (2x^bend - 1) dx = 2/(bend+1) - 1
        const float mean = 2.0f / (bend + 1.0f) - 1.0f;
        return v - mean;
    }

    /** Triangle 的零均值混合（tri 与 sine 各自零均值，混合后仍零均值）。 */

    /** 该波形是否需要随机数（噪声类）。 */
    inline bool isNoise (Waveform w) noexcept
    {
        return w == Waveform::Noise;
    }

    /** 该波形的近似峰值，用于归一化（方波/脉冲峰值是高，但听感响度不同）。 */
    inline float peak (Waveform w) noexcept
    {
        switch (w)
        {
            case Waveform::Square: return 1.0f;
            case Waveform::Pulse:  return 1.0f;
            case Waveform::Saw:    return 1.0f;
            case Waveform::Triangle: return 1.0f;
            case Waveform::Noise:  return 1.0f;
            case Waveform::Sine:
            default: return 1.0f;
        }
    }
}

//==============================================================================
/** 简单的无状态噪声源（音频线程安全，每个实例独立）。 */
class NoiseGen
{
public:
    void reset (uint32_t seed = 0x12345678u) noexcept { state = seed | 1u; }

    inline float next() noexcept
    {
        // xorshift32：快且质量足够做噪声
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return (float) ((int32_t) state) * (1.0f / 2147483648.0f);
    }

private:
    uint32_t state = 0x12345678u;
};

} // namespace kf
