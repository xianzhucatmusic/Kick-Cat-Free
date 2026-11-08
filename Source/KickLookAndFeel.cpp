#include "KickLookAndFeel.h"

namespace kf
{

//==============================================================================
// LookAndFeel：灰色扁平
//==============================================================================

KickLookAndFeel::KickLookAndFeel()
{
    setColour (juce::ResizableWindow::backgroundColourId, theme::bg);
    setColour (juce::Label::textColourId, theme::text);
    setColour (juce::Slider::textBoxTextColourId, theme::text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, theme::panelDeep);
    setColour (juce::ComboBox::backgroundColourId, theme::control);
    setColour (juce::ComboBox::textColourId, theme::text);
    setColour (juce::ComboBox::outlineColourId, theme::outline);
    setColour (juce::ComboBox::arrowColourId, theme::textDim);
    setColour (juce::PopupMenu::backgroundColourId, theme::g2);
    setColour (juce::PopupMenu::textColourId, theme::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, theme::accent.withAlpha (0.75f));
    setColour (juce::PopupMenu::highlightedTextColourId, theme::white);
    setColour (juce::TextEditor::backgroundColourId, theme::panelDeep);
    setColour (juce::TextEditor::textColourId, theme::text);
    setColour (juce::TextEditor::outlineColourId, theme::outline);
    setColour (juce::TextEditor::focusedOutlineColourId, theme::accent);
    setColour (juce::ListBox::backgroundColourId, theme::panelDeep);
    setColour (juce::ListBox::outlineColourId, theme::outline);
    setColour (juce::ScrollBar::thumbColourId, theme::g5);
}

const juce::Identifier KickLookAndFeel::modulationAmountProperty { "kfModAmount" };
const juce::Identifier KickLookAndFeel::modulationRangeProperty  { "kfModRange" };

void KickLookAndFeel::setModulationDisplay (juce::Slider& s, float amount, float range)
{
    s.getProperties().set (modulationAmountProperty, juce::jlimit (-1.0f, 1.0f, amount));
    s.getProperties().set (modulationRangeProperty, juce::jlimit (0.0f, 0.5f, range));
}

void KickLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                        float sliderPos, float rotaryStartAngle,
                                        float rotaryEndAngle, juce::Slider& slider)
{
    // Alchemy 风格旋钮：
    //   深灰圆盘底 -> 细环轨道 -> 彩色值弧 -> 中心白色指针线
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (1.0f);
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto cx = bounds.getCentreX();
    const auto cy = bounds.getCentreY();
    const auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    const float ring = juce::jmax (2.0f, radius * 0.16f);
    const float arcR = radius - ring * 0.5f;

    // 1) 圆盘底：比面板略深，形成"凹陷"感（不用阴影，纯色差）
    if (radius > 3.0f)
    {
        const float discR = arcR - ring * 0.55f;
        if (discR > 2.0f)
        {
            g.setColour (theme::knobTrack);
            g.fillEllipse (cx - discR, cy - discR, discR * 2.0f, discR * 2.0f);

            // 一圈极淡的内描边，让圆盘边界清楚
            g.setColour (theme::g3);
            g.drawEllipse (cx - discR, cy - discR, discR * 2.0f, discR * 2.0f, 1.0f);
        }
    }

    // 2) 轨道：完整一圈的暗色细环
    {
        juce::Path track;
        track.addCentredArc (cx, cy, arcR, arcR, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
        g.setColour (theme::g3);
        g.strokePath (track, juce::PathStrokeType (ring,
                                                   juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
    }

    // 3) 值弧：从起点扫到当前值，用功能色
    if (std::abs (angle - rotaryStartAngle) > 0.004f)
    {
        juce::Path val;
        val.addCentredArc (cx, cy, arcR, arcR, 0.0f, rotaryStartAngle, angle, true);
        g.setColour (slider.isEnabled() ? accentFor (slider) : theme::disabled);
        g.strokePath (val, juce::PathStrokeType (ring,
                                                 juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::rounded));
    }

    // 4) 指针：中心指向当前角度的白色细线（Alchemy 的标志性细节）
    {
        const float inner = radius * 0.16f;
        const float outer = radius * 0.74f;
        g.setColour (slider.isEnabled() ? theme::white : theme::disabled);
        g.drawLine (cx + std::sin (angle) * inner, cy - std::cos (angle) * inner,
                    cx + std::sin (angle) * outer, cy - std::cos (angle) * outer,
                    juce::jmax (1.2f, radius * 0.075f));
    }

    // 5) 调制指示环：画在值弧**外侧**，不遮挡原有的值显示。
    //
    // 两个环：
    //   淡色环 = 调制能达到的**范围**（静止时也能看出这个参数被调制了）
    //   实色环 = **当前**被推到的位置
    //
    // 只画实色环的话，调制值为 0 时看起来和"没调制"完全一样，
    // 用户无法判断某个参数到底有没有接线。
    {
        const float amount = (float) slider.getProperties()
                                 .getWithDefault (modulationAmountProperty, 0.0f);
        const float range  = (float) slider.getProperties()
                                 .getWithDefault (modulationRangeProperty, 0.0f);

        if (range > 1.0e-4f && radius > 5.0f)
        {
            const float modRing = juce::jmax (1.6f, ring * 0.55f);
            const float modR = arcR + ring * 0.75f;

            const float span = rotaryEndAngle - rotaryStartAngle;
            const auto clamp01 = [] (float v) { return juce::jlimit (0.0f, 1.0f, v); };

            const float basePos = clamp01 (sliderPos);
            const float loPos = clamp01 (basePos - range);
            const float hiPos = clamp01 (basePos + range);

            // 范围环：淡色
            if (hiPos - loPos > 0.004f)
            {
                juce::Path rp;
                rp.addCentredArc (cx, cy, modR, modR, 0.0f,
                                  rotaryStartAngle + loPos * span,
                                  rotaryStartAngle + hiPos * span, true);
                g.setColour (theme::orange.withAlpha (0.30f));
                g.strokePath (rp, juce::PathStrokeType (modRing,
                                                        juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded));
            }

            // 当前值环：从参数自身位置画到"被调制后"的位置
            const float modPos = clamp01 (basePos + amount * range);
            if (std::abs (modPos - basePos) > 0.004f)
            {
                const float a0 = rotaryStartAngle + juce::jmin (basePos, modPos) * span;
                const float a1 = rotaryStartAngle + juce::jmax (basePos, modPos) * span;
                juce::Path mp;
                mp.addCentredArc (cx, cy, modR, modR, 0.0f, a0, a1, true);
                g.setColour (theme::orange);
                g.strokePath (mp, juce::PathStrokeType (modRing,
                                                        juce::PathStrokeType::curved,
                                                        juce::PathStrokeType::rounded));
            }
        }
    }
}

juce::Colour KickLookAndFeel::accentFor (juce::Slider& slider)
{
    // 用自定义 colourId 取功能色；调用方没设过就回退到主题青。
    const auto c = slider.findColour (knobAccentColourId);
    return c.isTransparent() ? theme::accent : c;
}

void KickLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                                        bool highlighted, bool down)
{
    // Alchemy 风格开关：小圆角胶囊。
    // 开启 = 功能色实底 + 深色字（对比强，一眼可辨）
    // 关闭 = 深灰底 + 灰字
    const auto b = button.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = button.getToggleState();
    const float r = juce::jmin (theme::controlRadius, b.getHeight() * 0.4f);

    const auto accent = button.findColour (juce::ToggleButton::tickColourId);
    const auto onColour = accent.isTransparent() ? theme::accent : accent;

    juce::Colour fill, border, textCol;

    if (on)
    {
        fill   = down ? onColour.darker (0.18f)
                      : (highlighted ? onColour.brighter (0.10f) : onColour);
        border = onColour.brighter (0.30f);
        textCol = theme::bg;                 // 亮底压深字
    }
    else
    {
        fill   = down ? theme::g4 : (highlighted ? theme::controlHover : theme::control);
        border = theme::outline;
        textCol = highlighted ? theme::text : theme::textDim;
    }

    g.setColour (fill);
    g.fillRoundedRectangle (b, r);

    g.setColour (border);
    g.drawRoundedRectangle (b, r, 1.0f);

    g.setColour (textCol);
    g.setFont (juce::Font (juce::FontOptions (9.5f).withStyle ("Bold")));
    g.drawFittedText (button.getButtonText(), b.toNearestInt().reduced (3, 0),
                      juce::Justification::centred, 1);
}

void KickLookAndFeel::drawComboBox (juce::Graphics& g, int width, int height, bool,
                                    int, int, int, int, juce::ComboBox& box)
{
    const auto b = juce::Rectangle<int> (0, 0, width, height).toFloat().reduced (0.5f);

    g.setColour (theme::control);
    g.fillRect (b);

    g.setColour (box.isMouseOver() ? theme::g6 : theme::outline);
    g.drawRect (b, 1.0f);

    // 扁平三角箭头
    juce::Path arrow;
    const float cx = (float) width - 10.0f;
    const float cy = (float) height * 0.5f;
    arrow.startNewSubPath (cx - 3.5f, cy - 1.5f);
    arrow.lineTo (cx, cy + 2.0f);
    arrow.lineTo (cx + 3.5f, cy - 1.5f);
    g.setColour (box.isEnabled() ? theme::textDim : theme::disabled);
    g.strokePath (arrow, juce::PathStrokeType (1.3f));
}

void KickLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button,
                                            const juce::Colour& backgroundColour,
                                            bool highlighted, bool down)
{
    const auto b = button.getLocalBounds().toFloat().reduced (0.5f);

    auto fill = backgroundColour;
    if (fill.isTransparent())
        fill = theme::control;

    if (down)             fill = fill.darker (0.15f);
    else if (highlighted) fill = fill.brighter (0.12f);

    g.setColour (fill);
    g.fillRect (b);

    g.setColour (theme::outline);
    g.drawRect (b, 1.0f);
}

void KickLookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    g.setFont (juce::Font (juce::FontOptions (10.0f).withStyle ("Bold")));
    g.setColour (button.isEnabled() ? theme::text : theme::disabled);
    g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (3, 0),
                      juce::Justification::centred, 1);
}

void KickLookAndFeel::drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                                        float sliderPos, float, float,
                                        juce::Slider::SliderStyle style, juce::Slider& slider)
{
    // 扁平滑条（用于 Start / Length 这类参数）
    const bool vertical = (style == juce::Slider::LinearVertical
                        || style == juce::Slider::LinearBarVertical);

    if (vertical)
    {
        juce::Rectangle<float> track ((float) x + width * 0.5f - 1.5f, (float) y,
                                      3.0f, (float) height);
        g.setColour (theme::g3);
        g.fillRect (track);

        juce::Rectangle<float> filled (track.getX(), sliderPos, 3.0f, track.getBottom() - sliderPos);
        g.setColour (accentFor (slider));
        g.fillRect (filled);
    }
    else
    {
        juce::Rectangle<float> track ((float) x, (float) y + height * 0.5f - 1.5f,
                                      (float) width, 3.0f);
        g.setColour (theme::g3);
        g.fillRect (track);

        juce::Rectangle<float> filled (track.getX(), track.getY(), sliderPos - track.getX(), 3.0f);
        g.setColour (accentFor (slider));
        g.fillRect (filled);
    }
}

void KickLookAndFeel::drawLabel (juce::Graphics& g, juce::Label& label)
{
    g.fillAll (label.findColour (juce::Label::backgroundColourId));

    if (! label.isBeingEdited())
    {
        const auto alpha = label.isEnabled() ? 1.0f : 0.5f;
        const auto font = label.getFont();

        g.setColour (label.findColour (juce::Label::textColourId).withMultipliedAlpha (alpha));
        g.setFont (font);

        auto area = label.getBorderSize().subtractedFrom (label.getLocalBounds());
        g.drawFittedText (label.getText(), area,
                          label.getJustificationType(),
                          juce::jmax (1, (int) ((float) area.getHeight() / font.getHeight())),
                          label.getMinimumHorizontalScale());
    }
}

//==============================================================================
// WaveformSelector
//==============================================================================

WaveformSelector::WaveformSelector()
{
    combo.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (combo);
    combo.onChange = [this] { refreshFromCombo(); };
}

void WaveformSelector::attachTo (juce::AudioProcessorValueTreeState& apvts,
                                 const juce::String& paramID,
                                 int optionOffset,
                                 const juce::StringArray& optionNames)
{
    offset = juce::jmax (0, optionOffset);
    names = optionNames;

    combo.clear (juce::dontSendNotification);
    combo.addItemList (names, 1);

    attachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
        apvts, paramID, combo);

    refreshFromCombo();
}

void WaveformSelector::refreshFromCombo()
{
    const int idx = combo.getSelectedId() - 1 - offset;

    if (idx >= 0 && idx < (int) Waveform::NumWaveforms)
    {
        current = (Waveform) idx;
        isPlaceholder = false;
    }
    else
    {
        isPlaceholder = true;   // 选的是 "Sample" 之类的占位项
    }

    repaint();
}

bool WaveformSelector::getSelectedWaveform (Waveform& out) const
{
    if (isPlaceholder)
        return false;
    out = current;
    return true;
}

void WaveformSelector::resized()
{
    // 左侧下拉，右侧留出波形预览位置（在 paint 里画）
    auto b = getLocalBounds();
    auto preview = b.removeFromRight (juce::jmin (34, b.getWidth() / 3));
    juce::ignoreUnused (preview);
    combo.setBounds (b);
}

void WaveformSelector::drawWaveIcon (juce::Graphics& g, juce::Rectangle<float> area,
                                     Waveform w, juce::Colour colour, float shape)
{
    // 画 2 个周期的波形缩略图
    juce::Path p;
    const int steps = juce::jmax (24, (int) area.getWidth());
    const float midY = area.getCentreY();
    const float amp = area.getHeight() * 0.40f;

    for (int i = 0; i <= steps; ++i)
    {
        const float t = (float) i / (float) steps;
        const float phase = t * 2.0f;

        float v = 0.0f;
        if (wave::isNoise (w))
        {
            // 噪声用固定伪随机序列画，保证图标稳定不闪
            uint32_t s = (uint32_t) (i * 2654435761u) | 1u;
            s ^= s << 13; s ^= s >> 17; s ^= s << 5;
            v = (float) ((int32_t) s) / 2147483648.0f * 0.8f;
        }
        else if (w == Waveform::Square)
        {
            v = (phase - std::floor (phase)) < 0.5f ? 1.0f : -1.0f;
        }
        else if (w == Waveform::Pulse)
        {
            const float duty = juce::jmap (shape, 0.5f, 0.05f);
            v = (phase - std::floor (phase)) < duty ? 1.0f : -1.0f;
        }
        else
        {
            v = wave::evaluate (w, phase);
        }

        const float x = area.getX() + t * area.getWidth();
        const float y = midY - v * amp;

        if (i == 0) p.startNewSubPath (x, y);
        else        p.lineTo (x, y);
    }

    g.setColour (colour);
    g.strokePath (p, juce::PathStrokeType (1.2f));
}

void WaveformSelector::paint (juce::Graphics& g)
{
    // 右侧预览区
    auto preview = getLocalBounds().removeFromRight (juce::jmin (34, getWidth() / 3)).toFloat();
    preview.reduce (5.0f, 4.0f);

    if (preview.getWidth() < 6.0f)
        return;

    // 预览底
    g.setColour (theme::panelDeep);
    g.fillRect (preview);
    g.setColour (theme::outline);
    g.drawRect (preview, 1.0f);

    if (isPlaceholder)
    {
        // 占位（Sample 模式）画一条短横线示意"来自采样"
        g.setColour (theme::textFaint);
        g.drawLine (preview.getX() + 3.0f, preview.getCentreY(),
                    preview.getRight() - 3.0f, preview.getCentreY(), 1.2f);
        return;
    }

    drawWaveIcon (g, preview.reduced (2.0f, 1.0f), current, accent);
}

//==============================================================================
// 分区绘制
//==============================================================================

namespace section
{
    void draw (juce::Graphics& g, juce::Rectangle<int> bounds,
               const juce::String& title, juce::Colour accent, bool drawFrame)
    {
        if (bounds.isEmpty())
            return;

        const auto rf = bounds.toFloat();
        const float r = theme::panelRadius;

        // --- 面板底：纯色（比窗口底亮一档），小圆角
        g.setColour (theme::panel);
        g.fillRoundedRectangle (rf, r);

        // --- 1px 描边
        if (drawFrame)
        {
            g.setColour (theme::outline);
            g.drawRoundedRectangle (rf.reduced (0.5f), r, 1.0f);
        }

        // --- 标题行：比面板再亮一档，形成"标签条"效果
        auto titleRow = rf.withHeight ((float) titleHeight);
        {
            // 圆角只在上方两角
            juce::Path p;
            p.addRoundedRectangle (titleRow.getX(), titleRow.getY(),
                                   titleRow.getWidth(), titleRow.getHeight(),
                                   r, r, true, true, false, false);
            g.setColour (theme::g2);
            g.fillPath (p);
        }

        // 标题条下沿的分隔线
        g.setColour (theme::outline);
        g.drawHorizontalLine ((int) titleRow.getBottom() - 1,
                              titleRow.getX(), titleRow.getRight());

        // --- 左侧 3px 功能色条（全界面唯一的颜色标识）
        g.setColour (accent);
        g.fillRect (titleRow.getX() + 1.0f, titleRow.getY() + 3.0f,
                    3.0f, titleRow.getHeight() - 6.0f);

        // --- 标题文字：小号、加粗、字距略松（Alchemy 的排版习惯）
        g.setColour (theme::text);
        g.setFont (juce::Font (juce::FontOptions (9.5f).withStyle ("Bold")));
        g.drawText (title,
                    titleRow.withTrimmedLeft (9.0f).withTrimmedRight (5.0f).toNearestInt(),
                    juce::Justification::centredLeft, false);
    }
}

} // namespace kf
