#include "ModulationPanel.h"
#include "PluginProcessor.h"

namespace kf
{

//==============================================================================
namespace
{
    // 与其它面板保持一致的配色
    const juce::Colour kBg      { 0xff1a1d21 };
    const juce::Colour kPanel   { 0xff23272d };
    const juce::Colour kBorder  { 0xff2f353d };
    const juce::Colour kText    { 0xffc8ced6 };
    const juce::Colour kDim     { 0xff7b838d };
    const juce::Colour kAccent  { 0xffd98a2b };   // 调制页的强调色（橙）

    void styleCombo (juce::ComboBox& c)
    {
        c.setColour (juce::ComboBox::backgroundColourId, kPanel);
        c.setColour (juce::ComboBox::textColourId, kText);
        c.setColour (juce::ComboBox::outlineColourId, kBorder);
        c.setColour (juce::ComboBox::arrowColourId, kDim);
    }

    void styleSlider (juce::Slider& s)
    {
        s.setColour (juce::Slider::rotarySliderFillColourId, kAccent);
        s.setColour (juce::Slider::rotarySliderOutlineColourId, kBorder);
        s.setColour (juce::Slider::thumbColourId, kText);
        s.setColour (juce::Slider::textBoxTextColourId, kText);
        s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        s.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    }
}

//==============================================================================
juce::Colour ModulationPanel::accentColour() { return kAccent; }

//==============================================================================
ModulationPanel::ModulationPanel (KickForgeProcessor& proc)
    : processor (proc)
{
    auto& mod = processor.getModulation();

    //--- 包络页签
    for (int i = 0; i < kNumEnvEditors; ++i)
    {
        auto& b = envTabs[(size_t) i];
        b.setButtonText ("ENV " + juce::String (i + 1));
        b.setClickingTogglesState (false);
        b.setColour (juce::TextButton::buttonColourId, kPanel);
        b.setColour (juce::TextButton::buttonOnColourId, kAccent);
        b.setColour (juce::TextButton::textColourOffId, kDim);
        b.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        b.onClick = [this, i] { selectEnv (i); };
        addAndMakeVisible (b);

        auto ed = std::make_unique<EnvelopeEditor> ("", kAccent);
        ed->setEnvelope (&mod.getEnvelope (i));
        ed->setValueRange (0.0f, 1.0f, "0", "1");
        ed->addChangeListener (this);
        addAndMakeVisible (*ed);
        envEditors[(size_t) i] = std::move (ed);
    }

    //--- 包络时长
    envLengthLabel.setText ("ENV LENGTH", juce::dontSendNotification);
    envLengthLabel.setColour (juce::Label::textColourId, kDim);
    envLengthLabel.setFont (juce::Font (juce::FontOptions (9.5f)));
    envLengthLabel.setJustificationType (juce::Justification::centred);
    addAndMakeVisible (envLengthLabel);

    envLength.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    envLength.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 58, 14);
    envLength.setRange (0.005, 5.0, 0.001);
    envLength.setSkewFactorFromMidPoint (0.15);
    envLength.setValue (mod.getEnvelopeLengthSeconds (0), juce::dontSendNotification);
    envLength.setTextValueSuffix (" s");
    styleSlider (envLength);
    envLength.onValueChange = [this]
    {
        // 作用于**当前选中的包络**（不是全部）
        processor.getModulation().setEnvelopeLengthSeconds (
            selectedEnv, (float) envLength.getValue());
    };
    addAndMakeVisible (envLength);

    //--- LFO
    for (int i = 0; i < ModulationMatrix::kNumLfos; ++i)
    {
        auto& lbl = lfoLabels[(size_t) i];
        // 文字里带上实时值 —— 比单独画一个点更容易看清极性
        // （LFO 是双极性的，正负都重要；单看一个圆点分不出方向）
        lbl.setText ("LFO " + juce::String (i + 1), juce::dontSendNotification);
        lbl.setColour (juce::Label::textColourId, kDim);
        lbl.setFont (juce::Font (juce::FontOptions (9.5f)));
        lbl.setJustificationType (juce::Justification::centredLeft);
        addAndMakeVisible (lbl);

        auto& sh = lfoShape[(size_t) i];
        sh.addItemList (juce::StringArray { "Sine", "Triangle", "Saw", "Square", "Random" }, 1);
        sh.setSelectedId ((int) mod.getLfo (i).shape + 1, juce::dontSendNotification);
        styleCombo (sh);
        sh.onChange = [this, i]
        {
            auto s = processor.getModulation().getLfo (i);
            s.shape = (ModulationMatrix::LfoSettings::Shape) (lfoShape[(size_t) i].getSelectedId() - 1);
            processor.getModulation().setLfo (i, s);
        };
        addAndMakeVisible (sh);

        // 速率用**横向滑条**而不是旋钮 ——
        // 旋转旋钮在 26px 行高里放不下（实测只能看到数字，控件本身被压没了）。
        // 横向滑条在这个高度里清晰可用。
        auto& rt = lfoRate[(size_t) i];
        rt.setSliderStyle (juce::Slider::LinearHorizontal);
        rt.setTextBoxStyle (juce::Slider::TextBoxRight, false, 50, 16);
        rt.setRange (0.05, 30.0, 0.01);
        rt.setSkewFactorFromMidPoint (2.0);
        rt.setValue (mod.getLfo (i).rateHz, juce::dontSendNotification);
        rt.setTextValueSuffix (" Hz");
        styleSlider (rt);
        rt.onValueChange = [this, i]
        {
            auto s = processor.getModulation().getLfo (i);
            s.rateHz = (float) lfoRate[(size_t) i].getValue();
            processor.getModulation().setLfo (i, s);
        };
        addAndMakeVisible (rt);

        //--- 同步开关
        auto& sy = lfoSync[(size_t) i];
        sy.setButtonText ("SYNC");
        sy.setClickingTogglesState (true);
        sy.setColour (juce::TextButton::buttonColourId, kPanel);
        sy.setColour (juce::TextButton::buttonOnColourId, kAccent);
        sy.setColour (juce::TextButton::textColourOffId, kDim);
        sy.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        sy.setToggleState (mod.getLfo (i).sync, juce::dontSendNotification);
        sy.onClick = [this, i]
        {
            auto st = processor.getModulation().getLfo (i);
            st.sync = lfoSync[(size_t) i].getToggleState();
            processor.getModulation().setLfo (i, st);

            // 切换后换掉可见的速率控件
            const bool syncOn = st.sync;
            lfoRate[(size_t) i].setVisible (! syncOn);
            lfoDivision[(size_t) i].setVisible (syncOn);
        };
        addAndMakeVisible (sy);

        //--- 分度选择
        auto& dv = lfoDivision[(size_t) i];
        dv.addItemList ({ "4 bars", "2 bars", "1 bar", "1/2", "1/4", "1/8", "1/16" }, 1);
        // division 值 -> 选项下标
        const float divs[] = { 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f, 0.0625f };
        const float cur = mod.getLfo (i).division;
        int sel = 5;
        for (int k = 0; k < 7; ++k)
            if (std::abs (divs[k] - cur) < 1.0e-4f) sel = k + 1;
        dv.setSelectedId (sel, juce::dontSendNotification);
        styleCombo (dv);
        dv.onChange = [this, i]
        {
            static const float d[7] = { 4.0f, 2.0f, 1.0f, 0.5f, 0.25f, 0.125f, 0.0625f };
            const int idx = juce::jlimit (0, 6, lfoDivision[(size_t) i].getSelectedId() - 1);
            auto st = processor.getModulation().getLfo (i);
            st.division = d[idx];
            processor.getModulation().setLfo (i, st);
        };
        dv.setVisible (mod.getLfo (i).sync);
        rt.setVisible (! mod.getLfo (i).sync);
        addAndMakeVisible (dv);

        //--- 触发重置相位
        auto& rs = lfoReset[(size_t) i];
        rs.setButtonText ("RST");
        rs.setClickingTogglesState (true);
        rs.setColour (juce::TextButton::buttonColourId, kPanel);
        rs.setColour (juce::TextButton::buttonOnColourId, kAccent);
        rs.setColour (juce::TextButton::textColourOffId, kDim);
        rs.setColour (juce::TextButton::textColourOnId, juce::Colours::black);
        rs.setToggleState (mod.getLfo (i).resetOnTrigger, juce::dontSendNotification);
        rs.setTooltip ("Reset LFO phase on every trigger "
                       "(makes each hit identical)");
        rs.onClick = [this, i]
        {
            auto st = processor.getModulation().getLfo (i);
            st.resetOnTrigger = lfoReset[(size_t) i].getToggleState();
            processor.getModulation().setLfo (i, st);
        };
        addAndMakeVisible (rs);
    }

    //--- 路由表头
    sourcesHeader.setText ("MODULATION SOURCES", juce::dontSendNotification);
    sourcesHeader.setColour (juce::Label::textColourId, kDim);
    sourcesHeader.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
    addAndMakeVisible (sourcesHeader);

    routesHeader.setText ("ROUTING", juce::dontSendNotification);
    routesHeader.setColour (juce::Label::textColourId, kDim);
    routesHeader.setFont (juce::Font (juce::FontOptions (10.0f, juce::Font::bold)));
    addAndMakeVisible (routesHeader);

    for (int i = 0; i < ModulationMatrix::kMaxRoutes; ++i)
    {
        rows[(size_t) i] = std::make_unique<RouteRow>();
        buildRouteRow (i);
    }

    addRoute.setColour (juce::TextButton::buttonColourId, kPanel);
    addRoute.setColour (juce::TextButton::textColourOffId, kAccent);
    addRoute.onClick = [this]
    {
        // 找第一个空槽，填一条"待配置"的路由
        auto& mod = processor.getModulation();
        for (int i = 0; i < ModulationMatrix::kMaxRoutes; ++i)
        {
            if (! mod.getRoute (i).isActive())
            {
                ModulationMatrix::Route r;
                r.source = ModulationMatrix::Source::Env1;
                r.target = ModulationMatrix::Target::Eq3Gain;
                r.depth  = 0.5f;
                mod.setRoute (i, r);
                refreshFromModel();
                break;
            }
        }
    };
    addAndMakeVisible (addRoute);

    selectEnv (0);
    refreshFromModel();

    // 实时可视化：30Hz 刷新
    startTimerHz (30);
}

ModulationPanel::~ModulationPanel()
{
    stopTimer();
    for (auto& e : envEditors)
        if (e != nullptr)
            e->removeChangeListener (this);
}

//==============================================================================
void ModulationPanel::timerCallback()
{
    // 没有激活路由时不重绘（功能没在用，没必要吃 CPU）
    if (processor.getModulation().isIdle())
        return;

    // 只重绘"源区"和"路由区"的指示条，不重绘整块 ——
    // 整块重绘会让包络曲线闪烁。
    repaint();
}

float ModulationPanel::liveValue (ModulationMatrix::Source s) const
{
    return processor.getModulation().getMeterValue (s);
}

//==============================================================================
void ModulationPanel::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    // 用户拖动了某条包络的节点 —— 重新烘焙并交给音频线程。
    auto& mod = processor.getModulation();
    for (int i = 0; i < kNumEnvEditors; ++i)
    {
        if (envEditors[(size_t) i].get() == source)
        {
            mod.publishEnvelope (i);
            break;
        }
    }
}

//==============================================================================
void ModulationPanel::buildRouteRow (int index)
{
    auto& row = *rows[(size_t) index];
    auto& mod = processor.getModulation();

    row.source.addItemList (ModulationMatrix::getSourceNames(), 1);
    row.source.setSelectedId ((int) mod.getRoute (index).source + 1, juce::dontSendNotification);
    styleCombo (row.source);
    row.source.onChange = [this, index]
    {
        auto r = processor.getModulation().getRoute (index);
        r.source = (ModulationMatrix::Source) (rows[(size_t) index]->source.getSelectedId() - 1);
        processor.getModulation().setRoute (index, r);
    };
    addAndMakeVisible (row.source);

    row.target.addItemList (ModulationMatrix::getTargetNames(), 1);
    row.target.setSelectedId ((int) mod.getRoute (index).target + 1, juce::dontSendNotification);
    styleCombo (row.target);
    row.target.onChange = [this, index]
    {
        auto r = processor.getModulation().getRoute (index);
        r.target = (ModulationMatrix::Target) (rows[(size_t) index]->target.getSelectedId() - 1);
        processor.getModulation().setRoute (index, r);
    };
    addAndMakeVisible (row.target);

    row.depth.setSliderStyle (juce::Slider::LinearHorizontal);
    row.depth.setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 16);
    row.depth.setRange (-1.0, 1.0, 0.01);
    row.depth.setValue (mod.getRoute (index).depth, juce::dontSendNotification);
    row.depth.setDoubleClickReturnValue (true, 0.0);
    styleSlider (row.depth);
    row.depth.onValueChange = [this, index]
    {
        auto r = processor.getModulation().getRoute (index);
        r.depth = (float) rows[(size_t) index]->depth.getValue();
        processor.getModulation().setRoute (index, r);
    };
    addAndMakeVisible (row.depth);

    row.clear.setColour (juce::TextButton::buttonColourId, kPanel);
    row.clear.setColour (juce::TextButton::textColourOffId, kDim);
    row.clear.onClick = [this, index]
    {
        processor.getModulation().setRoute (index, {});
        refreshFromModel();
    };
    addAndMakeVisible (row.clear);
}

//==============================================================================
void ModulationPanel::selectEnv (int index)
{
    selectedEnv = juce::jlimit (0, kNumEnvEditors - 1, index);

    for (int i = 0; i < kNumEnvEditors; ++i)
    {
        const bool on = (i == selectedEnv);
        envTabs[(size_t) i].setToggleState (on, juce::dontSendNotification);
        if (envEditors[(size_t) i] != nullptr)
            envEditors[(size_t) i]->setVisible (on);
    }

    // 时长旋钮跟着切换 —— 否则切了包络但旋钮还显示上一条的时长，
    // 一转就把新包络的时长改成旧值（很迷惑）。
    envLength.setValue (
        processor.getModulation().getEnvelopeLengthSeconds (selectedEnv),
        juce::dontSendNotification);
    envLengthLabel.setText ("ENV " + juce::String (selectedEnv + 1) + " LENGTH",
                            juce::dontSendNotification);
}

void ModulationPanel::refreshFromModel()
{
    auto& mod = processor.getModulation();

    for (int i = 0; i < ModulationMatrix::kMaxRoutes; ++i)
    {
        auto& row = *rows[(size_t) i];
        const auto r = mod.getRoute (i);

        row.source.setSelectedId ((int) r.source + 1, juce::dontSendNotification);
        row.target.setSelectedId ((int) r.target + 1, juce::dontSendNotification);
        row.depth.setValue (r.depth, juce::dontSendNotification);
    }

    envLength.setValue (mod.getEnvelopeLengthSeconds (selectedEnv), juce::dontSendNotification);

    for (int i = 0; i < ModulationMatrix::kNumLfos; ++i)
    {
        const auto st = mod.getLfo (i);
        lfoShape[(size_t) i].setSelectedId ((int) st.shape + 1, juce::dontSendNotification);
        lfoRate[(size_t) i].setValue (st.rateHz, juce::dontSendNotification);
        lfoSync[(size_t) i].setToggleState (st.sync, juce::dontSendNotification);
        lfoReset[(size_t) i].setToggleState (st.resetOnTrigger, juce::dontSendNotification);
        lfoRate[(size_t) i].setVisible (! st.sync);
        lfoDivision[(size_t) i].setVisible (st.sync);
    }
}

//==============================================================================
void ModulationPanel::paint (juce::Graphics& g)
{
    g.fillAll (kBg);

    // 左侧源区与右侧路由区的分隔
    const int splitX = juce::jmax (300, getWidth() * 42 / 100);
    g.setColour (kBorder);
    g.drawLine ((float) splitX, 30.0f, (float) splitX, (float) getHeight(), 1.0f);

    //--- 实时值指示条
    //
    // 画在每个源的旁边，让用户看到"包络现在走到哪了"。
    // 静态的曲线图看不出时序，加了这个才知道调制是否真的在动。
    auto& mod = processor.getModulation();

    if (mod.isIdle())
    {
        g.setColour (kDim.withAlpha (0.7f));
        g.setFont (juce::Font (juce::FontOptions (9.5f)));
        g.drawText ("No active routing", getLocalBounds().removeFromBottom (18).reduced (10, 0),
                    juce::Justification::centredLeft, false);
        return;
    }

    // 包络指示：4 条竖条画在页签行下方
    const int tabTop = 8 + 16 + 4;          // 与 resized() 保持一致
    const int tabH = 22;
    {
        auto stripArea = juce::Rectangle<int> (8, tabTop + tabH + 3,
                                               juce::jmax (300, getWidth() * 42 / 100) - 16, 3);
        const int segW = stripArea.getWidth() / ModulationMatrix::kNumEnvs;

        for (int i = 0; i < ModulationMatrix::kNumEnvs; ++i)
        {
            auto seg = stripArea.withX (stripArea.getX() + i * segW)
                                .withWidth (segW - 2);
            // 背景
            g.setColour (kBorder);
            g.fillRect (seg);
            // 实时值
            const float v = juce::jlimit (0.0f, 1.0f,
                mod.getMeterValue ((ModulationMatrix::Source) ((int) ModulationMatrix::Source::Env1 + i)));
            g.setColour (kAccent.withAlpha (0.35f + 0.65f * v));
            g.fillRect (seg.withWidth ((int) (seg.getWidth() * v)));
        }
    }

}

void ModulationPanel::resized()
{
    auto r = getLocalBounds().reduced (8);
    r.removeFromTop (2);

    const int splitX = juce::jmax (300, getWidth() * 42 / 100);

    //=== 左：调制源 =========================================================
    auto left = r.removeFromLeft (splitX - 8);
    r.removeFromLeft (16);

    sourcesHeader.setBounds (left.removeFromTop (16));
    left.removeFromTop (4);

    // 包络页签
    {
        auto tabRow = left.removeFromTop (22);
        const int tw = tabRow.getWidth() / kNumEnvEditors;
        for (int i = 0; i < kNumEnvEditors; ++i)
            envTabs[(size_t) i].setBounds (tabRow.removeFromLeft (tw).reduced (2, 0));
    }
    left.removeFromTop (4);

    // 包络编辑器 + 时长旋钮
    {
        auto envArea = left.removeFromTop (juce::jmax (110, left.getHeight() - 130));
        auto lenArea = envArea.removeFromRight (72);
        envLengthLabel.setBounds (lenArea.removeFromTop (14));
        envLength.setBounds (lenArea.removeFromTop (66));

        for (auto& e : envEditors)
            if (e != nullptr)
                e->setBounds (envArea);
    }

    left.removeFromTop (10);

    // LFO
    for (int i = 0; i < ModulationMatrix::kNumLfos; ++i)
    {
        auto row = left.removeFromTop (26);
        lfoLabels[(size_t) i].setBounds (row.removeFromLeft (44));
        lfoShape[(size_t) i].setBounds (row.removeFromLeft (82).reduced (0, 2));
        row.removeFromLeft (6);
        lfoSync[(size_t) i].setBounds (row.removeFromLeft (46).reduced (0, 2));
        row.removeFromLeft (4);
        lfoReset[(size_t) i].setBounds (row.removeFromLeft (42).reduced (0, 2));
        row.removeFromLeft (6);
        // 剩余宽度给速率滑条 / 分度下拉框（两者互斥显示，共用同一位置）
        lfoRate[(size_t) i].setBounds (row);
        lfoDivision[(size_t) i].setBounds (row.reduced (0, 2));
        left.removeFromTop (5);
    }

    //=== 右：路由 ===========================================================
    routesHeader.setBounds (r.removeFromTop (16));
    r.removeFromTop (4);

    // 只显示到"最后一个已用路由 + 2 行"，其余隐藏。
    //
    // 12 行空下拉框看起来像界面坏了，而且把真正在用的路由淹没了。
    // 留 2 行空白是给"想再加一条"的用户一个可见的落点。
    auto& mod = processor.getModulation();
    int lastUsed = -1;
    for (int i = 0; i < ModulationMatrix::kMaxRoutes; ++i)
        if (mod.getRoute (i).isActive())
            lastUsed = i;
    const int visibleRows = juce::jlimit (2, ModulationMatrix::kMaxRoutes, lastUsed + 3);

    for (int i = 0; i < ModulationMatrix::kMaxRoutes; ++i)
    {
        auto& row = *rows[(size_t) i];
        const bool show = (i < visibleRows);

        row.source.setVisible (show);
        row.target.setVisible (show);
        row.depth.setVisible (show);
        row.clear.setVisible (show);

        if (! show)
            continue;

        auto rowArea = r.removeFromTop (24);
        row.clear.setBounds (rowArea.removeFromRight (22).reduced (0, 2));
        rowArea.removeFromRight (4);
        row.depth.setBounds (rowArea.removeFromRight (juce::jmin (150, rowArea.getWidth() / 3)));
        rowArea.removeFromRight (6);

        const int half = rowArea.getWidth() / 2;
        row.source.setBounds (rowArea.removeFromLeft (half).reduced (0, 1));
        rowArea.removeFromLeft (4);
        row.target.setBounds (rowArea.reduced (0, 1));

        r.removeFromTop (3);
    }

    r.removeFromTop (6);
    addRoute.setBounds (r.removeFromTop (24).removeFromLeft (140));
}

} // namespace kf
