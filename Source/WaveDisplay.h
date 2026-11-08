#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <array>
#include <vector>

namespace kf
{

//==============================================================================
/** 波形叠加显示器。
 *
 *  把 Sub 和最多 3 个 Click 层的波形画在同一张图上，每层可单独开关显示。
 *  用途：对齐各层的相位与包络起音点 —— 这是底鼓"紧"与"散"的关键。
 *
 *  实现：各层把自己的波形写进一个共享的峰值缓存（每层一对 min/max 数组），
 *  显示器只负责画。音频线程不参与 —— 波形由引擎在渲染时顺便捕获。
 */
class WaveDisplay : public juce::Component
{
public:
    static constexpr int kNumSources = 4;   // 0 = Sub，1..3 = Click 层
    static constexpr int kResolution = 1024; // 每条波形的横向采样点

    WaveDisplay();
    ~WaveDisplay() override;

    /** 引擎在渲染时把最新一次的波形峰值写进来（音频线程可调用）。 */
    void captureSource (int sourceIndex, const float* samples, int numSamples);

    /** 清空显示。 */
    void clearWaveforms();

    void setSourceVisible (int sourceIndex, bool visible);
    bool isSourceVisible (int sourceIndex) const;

    void setSourceColour (int sourceIndex, juce::Colour c);

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    struct Source
    {
        std::vector<float> minValues, maxValues;
        bool visible = true;
        juce::Colour colour { juce::Colours::white };
        bool hasData = false;
    };

    std::array<Source, kNumSources> sources;
    juce::SpinLock lock;

    void drawGrid (juce::Graphics& g, juce::Rectangle<float> area);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WaveDisplay)
};

} // namespace kf
