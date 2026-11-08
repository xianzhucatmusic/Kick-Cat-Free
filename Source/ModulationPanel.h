#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "ModulationMatrix.h"
#include "EnvelopeEditor.h"

namespace kf
{

class KickForgeProcessor;

//==============================================================================
/** 调制页 UI。
 *
 *  布局：
 *    ┌─ 调制源（左侧）───────────┬─ 路由列表（右侧）──────────────┐
 *    │ ENV 1..4 可编辑曲线       │ [源] -> [目标]  深度旋钮  [X]  │
 *    │ LFO 1..2 形状 + 速率      │ ...                            │
 *    └───────────────────────────┴────────────────────────────────┘
 *
 *  设计取舍：路由用**列表 + 下拉框**而不是 Vital 那种拖拽连线。
 *  拖拽线在 8 个目标时更直观，但实现复杂、且小窗口下很难操作。
 *  下拉框列表虽然朴素，但每个参数都清晰可点，实际更好用。
 */
class ModulationPanel : public juce::Component,
                        public juce::ChangeListener,
                        private juce::Timer
{
public:
    explicit ModulationPanel (KickForgeProcessor& proc);
    ~ModulationPanel() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    /** 包络被拖动后，把新表交给音频线程。
     *
     *  EnvelopeEditor 是 ChangeBroadcaster，用户改节点时会通知。
     *  这里必须 publish，否则音频线程读的还是旧的烘焙表 ——
     *  表现是"拖了曲线但声音不变"。 */
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;

    /** 定时重绘，显示各调制源的实时值。
     *
     *  30Hz 足够看出动态，又不会明显吃 CPU。
     *  只在**有激活路由**时才重绘 —— 没有调制时不浪费。 */
    void timerCallback() override;

    /** 各源当前的实时值（-1..1 或 0..1，取决于源的极性）。 */
    float liveValue (ModulationMatrix::Source s) const;

    /** 外部（编辑器）调用：在别处改了调制状态后刷新显示。 */
    void refreshFromModel();

    /** 页面标题条颜色。 */
    static juce::Colour accentColour();

private:
    KickForgeProcessor& processor;

    //--- 包络编辑
    static constexpr int kNumEnvEditors = ModulationMatrix::kNumEnvs;
    std::array<std::unique_ptr<EnvelopeEditor>, (size_t) kNumEnvEditors> envEditors;
    std::array<juce::TextButton, (size_t) kNumEnvEditors> envTabs;
    int selectedEnv = 0;

    //--- LFO
    std::array<juce::ComboBox, ModulationMatrix::kNumLfos> lfoShape;
    std::array<juce::Slider, ModulationMatrix::kNumLfos> lfoRate;
    std::array<juce::Label, ModulationMatrix::kNumLfos> lfoLabels;

    /** 同步开关 + 分度选择。
     *
     *  同步关闭时显示 rate 滑条（Hz），开启时换成分度下拉框。
     *  两个控件叠在同一位置、互斥显示 —— 比并排放更省空间，
     *  也避免用户困惑"两个都能调到底听谁的"。 */
    std::array<juce::TextButton, ModulationMatrix::kNumLfos> lfoSync;
    std::array<juce::ComboBox, ModulationMatrix::kNumLfos> lfoDivision;

    /** 触发时重置相位。放在 SYNC 旁边，两者都是"LFO 行为"开关。 */
    std::array<juce::TextButton, ModulationMatrix::kNumLfos> lfoReset;

    //--- 包络时长（每条独立）
    //
    // 只显示当前选中那条包络的时长旋钮 ——
    // 4 个旋钮并排会挤掉曲线编辑区，而且同时调 4 个的场合很少。
    juce::Slider envLength;
    juce::Label envLengthLabel;

    //--- 路由列表
    struct RouteRow
    {
        juce::ComboBox source;
        juce::ComboBox target;
        juce::Slider depth;
        juce::TextButton clear { "X" };
    };
    std::array<std::unique_ptr<RouteRow>, (size_t) ModulationMatrix::kMaxRoutes> rows;
    juce::TextButton addRoute { "+ ADD ROUTE" };
    juce::Label routesHeader;
    juce::Label sourcesHeader;

    void buildRouteRow (int index);
    void selectEnv (int index);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModulationPanel)
};

} // namespace kf
