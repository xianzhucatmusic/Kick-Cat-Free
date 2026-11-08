#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include "Theme.h"
#include "Waveform.h"

namespace kf
{

//==============================================================================
/** Kick Cat 外观：灰色扁平化。
 *
 *  与上一版（圆角+渐变+猫爪）完全相反：
 *   - 无渐变、无圆角、无阴影
 *   - 层次只靠亮度差与 1px 边框
 *   - 旋钮画成"细环 + 刻度点"，不做拟物圆盘
 *   - 开关是方角矩形，开启时填功能色
 */
class KickLookAndFeel : public juce::LookAndFeel_V4
{
public:
    KickLookAndFeel();

    /** 旋钮值弧的功能色专用 colourId。
     *  不能用 JUCE 内置的 rotarySliderFillColourId —— LookAndFeel_V4 的
     *  默认配色方案会给它填一个值，导致"未设置时回退到主题色"的判断失效
     *  （曾经因此所有旋钮都画成了默认的浅色，看不出主题色）。
     *  这里用一个自定义 ID，只要调用方不设，就一定是透明、回退到主题色。 */
    static constexpr int knobAccentColourId = 0x4b46c1;   // 'KF'+1，任意唯一值

    //==========================================================================
    /** 调制指示环的接口。
     *
     *  做法：把"当前调制量"挂在 slider 的 properties 上，画旋钮时读出来
     *  在值弧**外侧**画一段半透明圆弧。
     *
     *  为什么用 properties 而不是新建一个 Slider 子类：
     *  插件里的旋钮控件是统一创建的（addKnob 之类），
     *  换成子类要改动所有创建点。用 properties 只需要在每帧写一个值，
     *  而且天然兼容"没有调制"的情况（属性不存在 = 不画）。 */
    static const juce::Identifier modulationAmountProperty;   // -1..1
    static const juce::Identifier modulationRangeProperty;    // 归一化半宽（0..0.5）

    /** 给旋钮设置当前调制量。
     *  @param amount  归一化偏移 -1..+1（0 = 无调制，不画环）
     *  @param range   调制可能达到的最大归一化幅度（画"可达范围"的淡色环）
     *
     *  画两个环的价值：
     *    - 实心环 = **当前**被推到哪里
     *    - 淡色环 = 调制**最多**能推到哪
     *  只有实心环的话，静止时看不出这个参数被调制了。 */
    static void setModulationDisplay (juce::Slider& s, float amount, float range);

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPosProportional, float rotaryStartAngle,
                           float rotaryEndAngle, juce::Slider& slider) override;

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                           bool shouldDrawButtonAsHighlighted,
                           bool shouldDrawButtonAsDown) override;

    void drawComboBox (juce::Graphics& g, int width, int height, bool isButtonDown,
                       int buttonX, int buttonY, int buttonW, int buttonH,
                       juce::ComboBox& box) override;

    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted,
                               bool shouldDrawButtonAsDown) override;

    void drawButtonText (juce::Graphics& g, juce::TextButton& button,
                         bool shouldDrawButtonAsHighlighted,
                         bool shouldDrawButtonAsDown) override;

    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle style, juce::Slider& slider) override;

    void drawLabel (juce::Graphics& g, juce::Label& label) override;

private:
    /** 取旋钮的功能色：优先用 slider 自己设的 fill 色，否则用主强调色。 */
    static juce::Colour accentFor (juce::Slider& slider);
};

//==============================================================================
/** 波形选择器：下拉框 + 右侧实时波形预览。
 *
 *  为什么单做一个组件：KICK 2 里 Sub 的波形选择带一个缩略图标，
 *  一眼能看出选的是哪种波。用普通 ComboBox 只能看到文字，不够直观。
 *  这里把「名字」和「形状」放在一起，是本插件相对参考实现的改进点。
 */
class WaveformSelector : public juce::Component
{
public:
    WaveformSelector();

    /** 绑定到 APVTS 的一个 choice 参数。
     *  @param offset  选项偏移：0 表示第一项就是 Waveform::Sine；
     *                 1 表示第一项是 "Sample" 之类的占位，波形从第二项开始。 */
    void attachTo (juce::AudioProcessorValueTreeState& apvts,
                   const juce::String& paramID,
                   int optionOffset,
                   const juce::StringArray& optionNames);

    void setAccent (juce::Colour c) { accent = c; repaint(); }

    /** 当前选中的波形；若选中的是占位项（如 Sample）返回 false。 */
    bool getSelectedWaveform (Waveform& out) const;

    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    void refreshFromCombo();

    juce::ComboBox combo;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> attachment;

    juce::StringArray names;
    int offset = 0;
    Waveform current = Waveform::Sine;
    bool isPlaceholder = false;
    juce::Colour accent { theme::cyan };

    /** 在一个矩形里画出该波形的缩略图。 */
    static void drawWaveIcon (juce::Graphics& g, juce::Rectangle<float> area,
                              Waveform w, juce::Colour colour, float shape = 0.5f);
};

//==============================================================================
/** 分区标题栏：左侧功能色竖条 + 名称 + 可选右侧附加控件区。
 *
 *  这是本次扁平化设计的核心视觉元素（对应 KICK 2 的彩色标题条，
 *  但只保留一条 3px 细线做颜色标识，比整块填色更克制、更"灰"）。
 */
namespace section
{
    /** 画一个分区面板：底 + 1px 边框 + 顶部标题行。 */
    void draw (juce::Graphics& g, juce::Rectangle<int> bounds,
               const juce::String& title, juce::Colour accent,
               bool drawFrame = true);

    /** 标题行高度（各分区统一，保证对齐）。 */
    constexpr int titleHeight = 15;
}

} // namespace kf
