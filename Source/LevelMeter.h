#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace kf
{

//==============================================================================
/** 输出电平表 + 增益衰减表头。
 *
 *  专为底鼓设计：
 *   - 峰值表带峰值保持（peak hold），能看清每次击打的真实电平
 *   - 刻度直接标在右侧，-0.3dBFS 的限幅天花板用红线标出
 *   - 增益衰减（GR）用另一条反向的条显示，一眼看出压缩在做什么
 *
 *  数据由音频线程写、UI 线程读，用原子量传递。
 */
class LevelMeter : public juce::Component,
                   private juce::Timer
{
public:
    LevelMeter();
    ~LevelMeter() override;

    /** 音频线程调用：报告本块的峰值（线性 0..1+）。 */
    void pushPeak (float linearPeak) noexcept;

    /** 音频线程调用：报告当前增益衰减（dB，负值）。 */
    void pushGainReduction (float grDb) noexcept;

    /** 峰值保持的清除（点击表头时）。 */
    void resetPeakHold();

    void paint (juce::Graphics& g) override;

private:
    void timerCallback() override;

    std::atomic<float> incomingPeak { 0.0f };
    std::atomic<float> incomingGr { 0.0f };

    float displayPeak = 0.0f;      // 平滑后的电平
    float peakHold = 0.0f;         // 峰值保持
    float peakHoldDecay = 0.0f;
    float displayGr = 0.0f;

    int holdCounter = 0;

    static constexpr float kMinDb = -60.0f;
    static constexpr float kMaxDb = 0.0f;
    /** 限幅天花板（与 limiter_ceiling 默认值一致），画成红线。 */
    static constexpr float kCeilingDb = -0.3f;

    static float dbToNorm (float db) noexcept
    {
        return juce::jlimit (0.0f, 1.0f, (db - kMinDb) / (kMaxDb - kMinDb));
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LevelMeter)
};

} // namespace kf
