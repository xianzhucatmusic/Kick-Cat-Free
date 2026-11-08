#include "EnvelopeEditor.h"
#include "Theme.h"

namespace kf
{

EnvelopeEditor::EnvelopeEditor (const juce::String& titleText, juce::Colour accentColour)
    : title (titleText), accent (accentColour)
{
    setWantsKeyboardFocus (false);
    setMouseCursor (juce::MouseCursor::CrosshairCursor);
}

void EnvelopeEditor::setEnvelope (Envelope* env)
{
    envelope = env;
    hoveredNode = -1;
    dragMode = DragMode::None;
    repaint();
}

void EnvelopeEditor::setValueRange (float lo, float hi, const juce::String& loLabel, const juce::String& hiLabel)
{
    axisLo = lo;
    axisHi = hi;
    axisLoLabel = loLabel;
    axisHiLabel = hiLabel;
    repaint();
}

juce::Rectangle<float> EnvelopeEditor::getPlotArea() const
{
    // 左边留出刻度标签空间，上方留给标题
    return getLocalBounds().toFloat().reduced (26.0f, 8.0f).withTrimmedTop (10.0f);
}

float EnvelopeEditor::timeTolerance() const
{
    // 命中容差按像素折算，保证在任何尺寸下手感一致
    const auto area = getPlotArea();
    return area.getWidth() > 1.0f ? (kHitRadius / area.getWidth()) : 0.05f;
}

float EnvelopeEditor::valueTolerance() const
{
    const auto area = getPlotArea();
    return area.getHeight() > 1.0f ? (kHitRadius / area.getHeight()) : 0.05f;
}

juce::Point<float> EnvelopeEditor::toScreen (float time, float value) const
{
    const auto area = getPlotArea();
    // 值轴：内部 0..1 映射到 [axisLo, axisHi] 再归一化到屏幕
    const float span = juce::jmax (1.0e-6f, axisHi - axisLo);
    const float norm = (value - axisLo) / span;

    return { area.getX() + time * area.getWidth(),
             area.getBottom() - norm * area.getHeight() };
}

juce::Point<float> EnvelopeEditor::toData (juce::Point<float> screenPos) const
{
    const auto area = getPlotArea();
    if (area.getWidth() <= 1.0f || area.getHeight() <= 1.0f)
        return { 0.0f, 0.0f };

    const float time = juce::jlimit (0.0f, 1.0f, (screenPos.x - area.getX()) / area.getWidth());
    const float norm = juce::jlimit (0.0f, 1.0f, (area.getBottom() - screenPos.y) / area.getHeight());
    const float span = axisHi - axisLo;
    const float value = juce::jlimit (0.0f, 1.0f, axisLo + norm * span);

    return { time, value };
}

//==============================================================================

void EnvelopeEditor::drawCurve (juce::Graphics& g, juce::Rectangle<float> area)
{
    if (envelope == nullptr || envelope->getNumNodes() < 2)
        return;

    juce::Path path;
    constexpr int kSteps = 256;

    // 曲线填充
    juce::Path fillPath;
    bool first = true;

    for (int i = 0; i <= kSteps; ++i)
    {
        const float t = (float) i / (float) kSteps;
        const float v = envelope->getValueAt (t);
        const auto p = toScreen (t, v);

        if (first)
        {
            path.startNewSubPath (p);
            fillPath.startNewSubPath (area.getX(), area.getBottom());
            fillPath.lineTo (p);
            first = false;
        }
        else
        {
            path.lineTo (p);
            fillPath.lineTo (p);
        }
    }

    fillPath.lineTo (area.getRight(), area.getBottom());
    fillPath.closeSubPath();

    // 渐变填充让曲线下方有体积感
    juce::ColourGradient grad (accent.withAlpha (0.28f), area.getCentreX(), area.getY(),
                               accent.withAlpha (0.02f), area.getCentreX(), area.getBottom(), false);
    g.setGradientFill (grad);
    g.fillPath (fillPath);

    g.setColour (accent);
    g.strokePath (path, juce::PathStrokeType (2.0f));
}

void EnvelopeEditor::drawNodes (juce::Graphics& g, juce::Rectangle<float> area, int hovered)
{
    juce::ignoreUnused (area);
    if (envelope == nullptr)
        return;

    const int n = envelope->getNumNodes();

    for (int i = 0; i < n; ++i)
    {
        const auto& node = envelope->getNode (i);
        const auto p = toScreen (node.time, node.value);

        // --- 手柄线（只画当前悬停或正在拖动的节点，避免太乱）
        const bool showHandles = (i == hovered) || (i == dragNodeIndex && dragMode != DragMode::None);

        if (showHandles)
        {
            float inX, inY, outX, outY;
            envelope->getHandlePositions (i, inX, inY, outX, outY);

            const auto inP  = toScreen (inX, inY);
            const auto outP = toScreen (outX, outY);

            g.setColour (accent.withAlpha (0.5f));
            g.drawLine (p.x, p.y, inP.x, inP.y, 1.0f);
            g.drawLine (p.x, p.y, outP.x, outP.y, 1.0f);

            // 手柄圆点
            if (i > 0)
            {
                g.setColour (juce::Colours::white.withAlpha (0.85f));
                g.fillEllipse (inP.x - kHandleRadius, inP.y - kHandleRadius,
                               kHandleRadius * 2.0f, kHandleRadius * 2.0f);
                g.setColour (accent.darker (0.4f));
                g.drawEllipse (inP.x - kHandleRadius, inP.y - kHandleRadius,
                               kHandleRadius * 2.0f, kHandleRadius * 2.0f, 1.0f);
            }

            if (i < n - 1)
            {
                g.setColour (juce::Colours::white.withAlpha (0.85f));
                g.fillEllipse (outP.x - kHandleRadius, outP.y - kHandleRadius,
                               kHandleRadius * 2.0f, kHandleRadius * 2.0f);
                g.setColour (accent.darker (0.4f));
                g.drawEllipse (outP.x - kHandleRadius, outP.y - kHandleRadius,
                               kHandleRadius * 2.0f, kHandleRadius * 2.0f, 1.0f);
            }
        }

        // --- 节点本体
        const bool isHovered = (i == hovered);
        const float r = isHovered ? kNodeRadius + 1.5f : kNodeRadius;

        g.setColour (isHovered ? juce::Colours::white : accent.brighter (0.3f));
        g.fillEllipse (p.x - r, p.y - r, r * 2.0f, r * 2.0f);

        g.setColour (theme::bg);
        g.drawEllipse (p.x - r, p.y - r, r * 2.0f, r * 2.0f, 1.5f);
    }
}

void EnvelopeEditor::paint (juce::Graphics& g)
{
    const auto bounds = getLocalBounds().toFloat();
    const auto area = getPlotArea();

    // 背景
    g.setColour (theme::panelDeep);
    g.fillRoundedRectangle (bounds, 4.0f);

    // 网格：横向 4 等分（时间），纵向 4 等分（值）
    g.setColour (theme::outline);
    for (int i = 1; i < 4; ++i)
    {
        const float x = area.getX() + area.getWidth() * (float) i / 4.0f;
        g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
    }
    for (int i = 1; i < 4; ++i)
    {
        const float y = area.getY() + area.getHeight() * (float) i / 4.0f;
        g.drawHorizontalLine ((int) y, area.getX(), area.getRight());
    }

    g.setColour (theme::outlineSoft);
    g.drawRect (area, 1.0f);

    // 曲线 + 节点
    drawCurve (g, area);
    drawNodes (g, area, hoveredNode);

    // 标题
    g.setColour (theme::text);
    g.setFont (juce::Font (juce::FontOptions (11.0f).withStyle ("Bold")));
    g.drawText (title, getLocalBounds().removeFromTop (16).reduced (6, 2),
                juce::Justification::centredLeft, false);

    // 值轴刻度
    g.setFont (juce::Font (juce::FontOptions (9.0f)));
    g.setColour (theme::textDim);
    g.drawText (axisHiLabel, juce::Rectangle<float> (area.getX() - 25.0f, area.getY() - 5.0f, 24.0f, 12.0f),
                juce::Justification::centredRight, false);
    g.drawText (axisLoLabel, juce::Rectangle<float> (area.getX() - 25.0f, area.getBottom() - 6.0f, 24.0f, 12.0f),
                juce::Justification::centredRight, false);
}

void EnvelopeEditor::resized() {}

//==============================================================================

EnvelopeEditor::HandleSide EnvelopeEditor::hitTestHandle (juce::Point<float> pos, int& nodeIndexOut) const
{
    nodeIndexOut = -1;
    if (envelope == nullptr)
        return HandleSide::None;

    const int n = envelope->getNumNodes();

    for (int i = 0; i < n; ++i)
    {
        float inX, inY, outX, outY;
        envelope->getHandlePositions (i, inX, inY, outX, outY);

        if (i > 0)
        {
            const auto p = toScreen (inX, inY);
            if (p.getDistanceFrom (pos) <= kHitRadius)
            {
                nodeIndexOut = i;
                return HandleSide::In;
            }
        }

        if (i < n - 1)
        {
            const auto p = toScreen (outX, outY);
            if (p.getDistanceFrom (pos) <= kHitRadius)
            {
                nodeIndexOut = i;
                return HandleSide::Out;
            }
        }
    }

    return HandleSide::None;
}

void EnvelopeEditor::mouseDown (const juce::MouseEvent& e)
{
    if (envelope == nullptr)
        return;

    const auto pos = e.position;
    const auto data = toData (pos);
    hoveredNode = -1;

    // 1) 先测手柄（手柄画在节点外侧，优先级更高）
    int handleNode = -1;
    const auto side = hitTestHandle (pos, handleNode);
    if (side != HandleSide::None)
    {
        dragMode = (side == HandleSide::In) ? DragMode::InHandle : DragMode::OutHandle;
        dragNodeIndex = handleNode;
        dragHandle = side;
        repaint();
        return;
    }

    // 2) 再测节点本体
    const int hit = envelope->findNodeAt (data.x, data.y, timeTolerance(), valueTolerance());
    if (hit >= 0)
    {
        dragMode = DragMode::Node;
        dragNodeIndex = hit;
        hoveredNode = hit;
        repaint();
        return;
    }

    dragMode = DragMode::None;
    dragNodeIndex = -1;
    repaint();
}

void EnvelopeEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (envelope == nullptr || dragMode == DragMode::None || dragNodeIndex < 0)
        return;

    const auto data = toData (e.position);

    if (dragMode == DragMode::Node)
    {
        // Shift：只改值，锁定时间
        if (e.mods.isShiftDown())
        {
            const auto& node = envelope->getNode (dragNodeIndex);
            envelope->moveNode (dragNodeIndex, node.time, data.y);
        }
        else
        {
            envelope->moveNode (dragNodeIndex, data.x, data.y);
        }
    }
    else
    {
        // 拖手柄：按相对本节点的偏移换算成归一化切线
        const auto& node = envelope->getNode (dragNodeIndex);

        if (dragMode == DragMode::OutHandle)
        {
            const int nextIdx = dragNodeIndex + 1;
            if (nextIdx < envelope->getNumNodes())
            {
                const auto& next = envelope->getNode (nextIdx);
                const float segLen = next.time - node.time;
                const float segVal = next.value - node.value;

                const float tx = (segLen > 1.0e-6f) ? (data.x - node.time) / segLen : 0.33f;
                const float ty = (std::abs (segVal) > 1.0e-6f) ? (data.y - node.value) / segVal : 0.0f;

                envelope->setNodeTangent (dragNodeIndex, true, tx, ty);
            }
        }
        else
        {
            const int prevIdx = dragNodeIndex - 1;
            if (prevIdx >= 0)
            {
                const auto& prev = envelope->getNode (prevIdx);
                const float segLen = node.time - prev.time;
                const float segVal = node.value - prev.value;

                const float tx = (segLen > 1.0e-6f) ? (node.time - data.x) / segLen : 0.33f;
                const float ty = (std::abs (segVal) > 1.0e-6f) ? (node.value - data.y) / segVal : 0.0f;

                envelope->setNodeTangent (dragNodeIndex, false, tx, ty);
            }
        }
    }

    sendChangeMessage();   // 通知外部把新包络提交给音频线程
    repaint();
}

void EnvelopeEditor::mouseUp (const juce::MouseEvent&)
{
    dragMode = DragMode::None;
    dragNodeIndex = -1;
    dragHandle = HandleSide::None;
    repaint();
}

void EnvelopeEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (envelope == nullptr)
        return;

    const auto data = toData (e.position);

    // 双击已有节点 -> 删除；双击空白 -> 新增
    const int hit = envelope->findNodeAt (data.x, data.y, timeTolerance(), valueTolerance());

    if (hit >= 0)
    {
        // 首尾节点不可删（包络必须有始有终）
        const int last = envelope->getNumNodes() - 1;
        if (hit != 0 && hit != last)
        {
            envelope->removeNode (hit);
            hoveredNode = -1;
            sendChangeMessage();
            repaint();
        }
    }
    else
    {
        const int idx = envelope->addNode (data.x, data.y);
        hoveredNode = idx;
        sendChangeMessage();
        repaint();
    }
}

void EnvelopeEditor::mouseMove (const juce::MouseEvent& e)
{
    if (envelope == nullptr)
        return;

    int newHovered = -1;

    // 悬停手柄时也把节点视为悬停，方便看到手柄
    int handleNode = -1;
    const auto side = hitTestHandle (e.position, handleNode);
    if (side != HandleSide::None)
    {
        newHovered = handleNode;
        hoveredHandle = side;
    }
    else
    {
        const auto data = toData (e.position);
        newHovered = envelope->findNodeAt (data.x, data.y, timeTolerance(), valueTolerance());
        hoveredHandle = HandleSide::None;
    }

    if (newHovered != hoveredNode)
    {
        hoveredNode = newHovered;
        repaint();
    }
}

void EnvelopeEditor::mouseExit (const juce::MouseEvent&)
{
    if (hoveredNode >= 0)
    {
        hoveredNode = -1;
        hoveredHandle = HandleSide::None;
        repaint();
    }
}

} // namespace kf
