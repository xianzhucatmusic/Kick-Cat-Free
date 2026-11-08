#pragma once

#include <juce_dsp/juce_dsp.h>

namespace kf
{

//==============================================================================
/** 共享的小工具：平滑参数、直流阻断、快速音高换算等。 */
namespace dsp
{
    /** 半音 -> 频率比。 */
    inline float semitonesToRatio (float semis) noexcept
    {
        return std::pow (2.0f, semis / 12.0f);
    }

    /** MIDI 音符 -> 频率（A4 = 440Hz，note 69）。 */
    inline float midiNoteToFrequency (float note) noexcept
    {
        return 440.0f * std::pow (2.0f, (note - 69.0f) / 12.0f);
    }

    /** 线性插值读取一段 buffer（用于 click 采样播放）。 */
    inline float readInterpolated (const float* data, int numSamples, double pos) noexcept
    {
        if (data == nullptr || numSamples <= 0)
            return 0.0f;
        if (pos <= 0.0)
            return data[0];
        if (pos >= (double) (numSamples - 1))
            return data[numSamples - 1];

        const int   i0 = (int) pos;
        const float f  = (float) (pos - (double) i0);
        return data[i0] + f * (data[i0 + 1] - data[i0]);
    }

    /** 一阶直流阻断器。底鼓经过失真后容易产生直流偏移，必须清掉。 */
    class DcBlocker
    {
    public:
        /** @param cutoffHz 转折频率。越低越不影响低频，但收敛越慢。
         *                  默认 10Hz；要在单个块内快速压掉暂态直流可用更高值。 */
        void prepare (double sampleRate, double cutoffHz = 10.0) noexcept
        {
            const double fc = juce::jlimit (1.0, 200.0, cutoffHz);
            r = 1.0 - (juce::MathConstants<double>::twoPi * fc / sampleRate);
            r = juce::jlimit (0.5, 0.99999, r);
        }

        void reset() noexcept { x1 = y1 = 0.0f; }

        inline float process (float x) noexcept
        {
            const float y = x - x1 + (float) r * y1;
            x1 = x;
            y1 = y;
            return y;
        }

    private:
        double r = 0.999;
        float x1 = 0.0f, y1 = 0.0f;
    };

    /** 简易 one-pole 参数平滑，避免 UI 拖动时产生 zipper noise。 */
    class SmoothedValue
    {
    public:
        void reset (double sampleRate, double rampSeconds = 0.02) noexcept
        {
            const double n = juce::jmax (1.0, rampSeconds * sampleRate);
            coeff = (float) (1.0 / n);
        }

        void setTarget (float v) noexcept { target = v; }

        void snapTo (float v) noexcept { current = target = v; }

        inline float next() noexcept
        {
            current += coeff * (target - current);
            return current;
        }

        float getCurrent() const noexcept { return current; }

    private:
        float coeff = 0.01f, current = 0.0f, target = 0.0f;
    };

    /** 安全的 tanh 饱和（模拟失真用），带轻微的非对称以获得偶次谐波。 */
    inline float analogSaturate (float x) noexcept
    {
        // 非对称偏置 -> 产生偶次谐波，这是模拟电路「暖」的来源
        const float biased = x + 0.08f * x * x;
        return std::tanh (biased);
    }
}

} // namespace kf
