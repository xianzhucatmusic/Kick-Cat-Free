#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "Envelope.h"

namespace kf
{

//==============================================================================
/** 可拖拽的节点式包络编辑器。
 *
 *  交互（与 KICK 2 / Serum 一致）：
 *   - 拖动节点本体：移动时间与值
 *   - 拖动节点两侧的圆形手柄：调整 Bezier 曲率
 *   - 空白处双击：新增节点
 *   - 节点上右键 / 双击：删除节点（首尾节点不可删）
 *   - 按住 Shift 拖动：锁定时间轴，只改值
 */
class EnvelopeEditor : public juce::Component,
                       public juce::ChangeBroadcaster
{
public:
    EnvelopeEditor (const juce::String& titleText, juce::Colour accentColour);

    /** 绑定到一条包络（不取得所有权）。 */
    void setEnvelope (Envelope* env);
    Envelope* getEnvelope() const noexcept { return envelope; }

    /** 值轴的单位标签（如 "st" / "" ），仅用于显示。 */
    void setValueSuffix (const juce::String& s) { valueSuffix = s; repaint(); }

    /** 值轴的显示范围（用于刻度标注）。 */
    void setValueRange (float lo, float hi, const juce::String& loLabel, const juce::String& hiLabel);

    void paint (juce::Graphics& g) override;
    void resized() override;

    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;

private:
    enum class DragMode { None, Node, InHandle, OutHandle };
    enum class HandleSide { None, In, Out };

    juce::Rectangle<float> getPlotArea() const;

    juce::Point<float> toScreen (float time, float value) const;
    juce::Point<float> toData (juce::Point<float> screenPos) const;
    float timeTolerance() const;
    float valueTolerance() const;

    void drawCurve (juce::Graphics& g, juce::Rectangle<float> area);
    void drawNodes (juce::Graphics& g, juce::Rectangle<float> area, int hovered);

    /** 命中测试手柄。 */
    HandleSide hitTestHandle (juce::Point<float> pos, int& nodeIndexOut) const;

    Envelope* envelope = nullptr;

    juce::String title;
    juce::String valueSuffix;
    juce::Colour accent;

    float axisLo = 0.0f, axisHi = 1.0f;
    juce::String axisLoLabel, axisHiLabel;

    DragMode dragMode = DragMode::None;
    int dragNodeIndex = -1;
    HandleSide dragHandle = HandleSide::None;
    int hoveredNode = -1;
    HandleSide hoveredHandle = HandleSide::None;

    static constexpr float kHandleRadius = 4.0f;
    static constexpr float kNodeRadius   = 5.0f;
    static constexpr float kHitRadius    = 9.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EnvelopeEditor)
};

} // namespace kf
