#include "PluginEditor.h"
#include "FactoryPresets.h"
#include "Theme.h"
#include "SampleLibrary.h"

namespace kf
{

//==============================================================================
// SampleBrowser
//==============================================================================

SampleBrowser::SampleBrowser (KickForgeProcessor& p, int targetLayer)
    : processor (p), layer (juce::jlimit (0, kNumClickLayers - 1, targetLayer))
{
    list.setRowHeight (19);
    list.setColour (juce::ListBox::backgroundColourId, theme::panelDeep);
    list.setColour (juce::ListBox::outlineColourId, theme::outline);
    list.setOutlineThickness (1);
    addAndMakeVisible (list);

    infoLabel.setText ("Click " + juce::String (layer + 1)
                       + "   ·   click to preview   ·   double-click to load",
                       juce::dontSendNotification);
    infoLabel.setJustificationType (juce::Justification::centredLeft);
    infoLabel.setColour (juce::Label::textColourId, theme::textDim);
    infoLabel.setFont (juce::Font (juce::FontOptions (11.0f)));
    addAndMakeVisible (infoLabel);

    filterBox.setTextToShowWhenEmpty ("filter by name...", theme::textFaint);
    filterBox.onTextChange = [this] { rebuildFilter(); };
    addAndMakeVisible (filterBox);

    loadButton.onClick = [this] { loadRowIntoLayer (list.getSelectedRow()); };
    addAndMakeVisible (loadButton);

    closeButton.onClick = [this]
    {
        if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
            dw->exitModalState (0);
    };
    addAndMakeVisible (closeButton);

    rebuildFilter();
    setSize (440, 500);
}

SampleBrowser::~SampleBrowser() = default;

void SampleBrowser::rebuildFilter()
{
    visibleIndices.clear();
    const auto filter = filterBox.getText().trim().toLowerCase();

    for (int i = 0; i < processor.getSampleLibrary().getNumSamples(); ++i)
    {
        const auto& e = processor.getSampleLibrary().getEntry (i);
        if (filter.isEmpty() || e.name.toLowerCase().contains (filter))
            visibleIndices.push_back (i);
    }

    list.updateContent();
    list.repaint();
}

void SampleBrowser::paint (juce::Graphics& g)
{
    g.fillAll (theme::panel);
}

void SampleBrowser::resized()
{
    auto area = getLocalBounds().reduced (9);
    infoLabel.setBounds (area.removeFromTop (18));
    area.removeFromTop (5);
    filterBox.setBounds (area.removeFromTop (24));
    area.removeFromTop (6);

    auto bottom = area.removeFromBottom (26);
    loadButton.setBounds (bottom.removeFromLeft (bottom.getWidth() / 2 - 3));
    bottom.removeFromLeft (6);
    closeButton.setBounds (bottom);

    area.removeFromBottom (7);
    list.setBounds (area);
}

int SampleBrowser::getNumRows()
{
    return (int) visibleIndices.size();
}

void SampleBrowser::paintListBoxItem (int rowNumber, juce::Graphics& g,
                                      int width, int height, bool rowIsSelected)
{
    if (rowNumber < 0 || rowNumber >= (int) visibleIndices.size())
        return;

    const auto& e = processor.getSampleLibrary().getEntry (visibleIndices[(size_t) rowNumber]);

    if (rowIsSelected)
        g.fillAll (theme::cyan);
    else if (rowNumber % 2 == 0)
        g.fillAll (theme::panelDeep);

    static const juce::Colour catColours[SampleLibrary::kNumCategories]
    {
        theme::cyan, theme::cyan, theme::orangeBright,
        theme::meterHigh, theme::cyanBright, theme::cyan
    };

    g.setColour (catColours[juce::jlimit (0, SampleLibrary::kNumCategories - 1, e.category)]);
    g.fillRect (4, 4, 3, height - 8);

    g.setColour (rowIsSelected ? juce::Colours::white : theme::text);
    g.setFont (juce::Font (juce::FontOptions (11.5f)));
    g.drawText (e.name, 14, 0, width - 18, height, juce::Justification::centredLeft, false);
}

void SampleBrowser::loadRowIntoLayer (int row)
{
    if (row < 0 || row >= (int) visibleIndices.size())
        return;

    processor.getSampleLibrary().loadIntoLayer (visibleIndices[(size_t) row],
                                                processor.getClickLayerRef (layer));
    processor.notifySampleChanged();
}

void SampleBrowser::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    loadRowIntoLayer (row);
    processor.triggerAudition (0.9f);
}

void SampleBrowser::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    loadRowIntoLayer (row);
    if (auto* dw = findParentComponentOfClass<juce::DialogWindow>())
        dw->exitModalState (1);
}

void SampleBrowser::showFor (KickForgeProcessor& p, juce::Component* parent, int targetLayer)
{
    juce::ignoreUnused (parent);

    auto* browser = new SampleBrowser (p, targetLayer);

    juce::DialogWindow::LaunchOptions opts;
    opts.content.setOwned (browser);
    opts.dialogTitle = "Click Sample Library  ·  180 built-in";
    opts.dialogBackgroundColour = theme::panel;
    opts.escapeKeyTriggersCloseButton = true;
    opts.useNativeTitleBar = true;
    opts.resizable = true;
    opts.launchAsync();
}

//==============================================================================
// KickForgeEditor
//==============================================================================

KickForgeEditor::KickForgeEditor (KickForgeProcessor& p)
    : AudioProcessorEditor (&p), proc (p)
{
    setLookAndFeel (&lnf);

    // --- 顶部：标题 + 品牌
    titleLabel.setText ("KICK CAT", juce::dontSendNotification);
    titleLabel.setFont (juce::Font (juce::FontOptions (18.0f).withStyle ("Bold")));
    titleLabel.setColour (juce::Label::textColourId, theme::cyan);
    addAndMakeVisible (titleLabel);

    brandLabel.setText ("XianzhuCatMusic", juce::dontSendNotification);
    brandLabel.setFont (juce::Font (juce::FontOptions (9.5f)));
    brandLabel.setColour (juce::Label::textColourId, theme::textDim);
    brandLabel.setJustificationType (juce::Justification::centredLeft);
    addAndMakeVisible (brandLabel);


    // --- 预设栏
    //
    // 预设下拉框同时充当**菜单入口**：点它弹出按风格分组的菜单，
    // 而不是弹出一个 480 项的原生下拉列表（那样根本没法用）。
    //
    // 实现方式：把 ComboBox 设为"只读显示"，setPopupMenu 不行，
    // 所以这里在 onClick 里接管 —— 用一个不可交互的 Label 显示当前
    // 预设名，点它弹菜单；< > 仍用于前后浏览。
    presetSelector.setTextWhenNothingSelected ("Preset");
    presetSelector.setTooltip ("Click to browse presets by style");
    // 下拉框自身不承载列表（预设由弹出菜单管理），
    // 所以 onChange 不做加载，避免与菜单的选择重复触发。
    presetSelector.onChange = nullptr;

    // 点击下拉框任意位置都弹菜单（包括点箭头）
    presetSelector.onClicked = [this] { showPresetMenu(); };

    addAndMakeVisible (presetSelector);

    presetPrevButton.onClick = [this]
    {
        const int n = proc.getPresetManager().getNextIndex (-1);
        if (n >= 0) loadPresetByIndex (n);
    };
    addAndMakeVisible (presetPrevButton);

    presetNextButton.onClick = [this]
    {
        const int n = proc.getPresetManager().getNextIndex (1);
        if (n >= 0) loadPresetByIndex (n);
    };
    addAndMakeVisible (presetNextButton);

    presetSaveButton.onClick = [this] { savePresetWithDialog(); };
    addAndMakeVisible (presetSaveButton);

    // --- 撤销 / 重做
    // APVTS 已经接了 UndoManager，所以参数改动会自动进栈。
    // 这里只提供两个按钮触发。
    undoButton.setTooltip ("Undo the last parameter change");
    undoButton.onClick = [this] { proc.undo(); };
    addAndMakeVisible (undoButton);

    redoButton.setTooltip ("Redo");
    redoButton.onClick = [this] { proc.redo(); };
    addAndMakeVisible (redoButton);

    //----------------------------------------------------------------------
    // AI 生成栏
    //----------------------------------------------------------------------
    aiPromptEditor.setMultiLine (false);
    aiPromptEditor.setReturnKeyStartsNewLine (false);
    aiPromptEditor.setTextToShowWhenEmpty (
        juce::String::fromUTF8 ("描述你想要的底鼓，例如：有冲击力、低频很长的 Trap 底鼓"),
        theme::textDim);
    aiPromptEditor.setFont (juce::Font (juce::FontOptions (11.5f)));
    aiPromptEditor.setColour (juce::TextEditor::backgroundColourId, theme::panel.brighter (0.06f));
    aiPromptEditor.setColour (juce::TextEditor::textColourId, theme::text);
    aiPromptEditor.setColour (juce::TextEditor::outlineColourId, theme::outline);
    aiPromptEditor.setColour (juce::TextEditor::focusedOutlineColourId, theme::cyan);
    aiPromptEditor.onReturnKey = [this] { runAiGenerate(); };
    addAndMakeVisible (aiPromptEditor);

    aiGenerateButton.setTooltip ("Generate a kick from your description (offline)");
    aiGenerateButton.setColour (juce::TextButton::buttonColourId, theme::cyan.withAlpha (0.22f));
    aiGenerateButton.onClick = [this] { runAiGenerate(); };
    addAndMakeVisible (aiGenerateButton);

    aiUndoButton.setTooltip ("Revert to the state before the last generation");
    aiUndoButton.onClick = [this] { revertAiGenerate(); };
    addAndMakeVisible (aiUndoButton);

    aiHistoryButton.setTooltip ("Recent descriptions");
    aiHistoryButton.onClick = [this] { showAiHistoryMenu(); };
    addAndMakeVisible (aiHistoryButton);

    aiProgress.setPercentageDisplay (false);
    aiProgress.setColour (juce::ProgressBar::backgroundColourId, theme::panel.brighter (0.05f));
    aiProgress.setColour (juce::ProgressBar::foregroundColourId, theme::cyan);
    addAndMakeVisible (aiProgress);
    aiProgress.setVisible (false);

    // 快捷标签
    for (const auto& tag : LocalPatchEngine::getQuickTags())
    {
        // 用 juce::String 构造，不要传 const char* ——
        // TextButton 的 const char* 构造会按 Latin-1 解释中文，显示成乱码。
        auto b = std::make_unique<juce::TextButton> (juce::String (tag));
        b->setTooltip ("Insert this into the description");
        b->setColour (juce::TextButton::buttonColourId, theme::panel.brighter (0.08f));
        b->onClick = [this, tag]
        {
            // 追加而不是覆盖 —— 用户常常要点好几个标签组合描述
            auto cur = aiPromptEditor.getText().trim();
            if (cur.isEmpty())
                aiPromptEditor.setText (tag, juce::dontSendNotification);
            else if (! cur.contains (tag))
                aiPromptEditor.setText (cur + juce::String::fromUTF8 ("、") + tag,
                                        juce::dontSendNotification);

            aiPromptEditor.grabKeyboardFocus();
        };
        addAndMakeVisible (*b);
        aiQuickTagButtons.push_back (std::move (b));
    }

    //----------------------------------------------------------------------
    // 右侧 AI 聊天面板
    //----------------------------------------------------------------------
    aiPanelToggle.setTooltip ("Show/Hide the AI chat panel");
    aiPanelToggle.setClickingTogglesState (true);
    aiPanelToggle.setColour (juce::ToggleButton::tickColourId, theme::cyan);
    aiPanelToggle.onClick = [this]
    {
        aiPanelVisible = aiPanelToggle.getToggleState();
        if (aiPanelVisible && aiChatPanel == nullptr)
        {
            aiChatPanel = std::make_unique<AiChatPanel> (proc);
            aiChatPanel->onPatchReady = [this] (const juce::String& prompt,
                                                const juce::String& patchJson)
            {
                // AI 返回的补丁走同一条应用路径（平滑过渡 + 参数高亮）
                bool ok = false;
                auto patch = AiPatch::fromJson (patchJson, &ok);
                if (! ok)
                    return;

                aiPreGenerateState = proc.getStateAsValueTree().createCopy();
                applyAiPatch (patch);

                if (aiConfig.autoAudition)
                    proc.triggerAudition();
            };
            addAndMakeVisible (*aiChatPanel);
        }

        if (aiChatPanel != nullptr)
            aiChatPanel->setVisible (aiPanelVisible);

        resized();
    };
    addAndMakeVisible (aiPanelToggle);

    // 应用器的回调：过渡中刷新 UI；结束时记一次信息
    proc.getAiApplier().onFinished = [this]
    {
        refreshAiHighlights();
        repaint();
    };


    // --- 导出 WAV
    wavExporter = std::make_unique<WavExporter> (proc);
    exportWavButton.onClick = [this] { exportWav(); };
    exportWavButton.setTooltip ("Export the current sound as a WAV file");
    addAndMakeVisible (exportWavButton);

    // --- 试听
    auditionButton.onClick = [this] { proc.triggerAudition (1.0f); };
    addAndMakeVisible (auditionButton);

    // --- 包络页标签（比下拉更直观，一眼看到全部页）
    static const char* pageNames[] { "PITCH", "AMP", "CLICK 1", "CLICK 2", "CLICK 3", "CLICK 4" };
    static_assert (sizeof (pageNames) / sizeof (pageNames[0]) == (size_t) kNumEnvPages,
                   "包络页标签数量必须与 kNumEnvPages 一致");
    for (int i = 0; i < kNumEnvPages; ++i)
    {
        envPageTabs[(size_t) i].setButtonText (pageNames[i]);
        envPageTabs[(size_t) i].setClickingTogglesState (true);
        envPageTabs[(size_t) i].setRadioGroupId (9001);
        envPageTabs[(size_t) i].setToggleState (i == 0, juce::dontSendNotification);
        envPageTabs[(size_t) i].onClick = [this, i] { selectEnvelopePage (i); };
        addAndMakeVisible (envPageTabs[(size_t) i]);
    }

    // 保留下拉作为无障碍/紧凑备选，但默认隐藏不用
    envPageSelector.setVisible (false);
    addChildComponent (envPageSelector);

    // --- 功能页标签（SUB / CLICK / EQ / DYNAMICS）
    {
        static const char* names[] { "SUB", "CLICK", "EQ", "DYNAMICS", "MOD" };

        for (int i = 0; i < (int) Page::NumPages; ++i)
        {
            auto& b = pageTabs[(size_t) i];
            b.setButtonText (names[i]);

            // 不用 radio group：JUCE 会自己判断"点击是否改变状态"，
            // 与我们手动的 setToggleState 混用时可能吞掉点击，
            // 导致切走后再切回来不触发 onClick（页面卡住）。
            // 这里改成手工管理选中态：点击一律触发切换。
            b.setClickingTogglesState (false);
            b.setToggleState (i == 0, juce::dontSendNotification);

            // 每个页签用自己的功能色，与面板标题条一致
            const auto col = (i == 1) ? theme::orange
                                     : (i == 3) ? theme::orange
                                     : (i == 4) ? ModulationPanel::accentColour()
                                                : theme::cyan;
            b.setColour (juce::ToggleButton::tickColourId, col);

            b.onClick = [this, i] { selectPage ((Page) i); };
            addAndMakeVisible (b);
        }
    }

    // --- 包络编辑器
    envEditor = std::make_unique<EnvelopeEditor> ("PITCH ENVELOPE", theme::cyan);
    envEditor->setEnvelope (&proc.getEnvelope (EnvelopeTarget::SubPitch));
    envEditor->setValueRange (0.0f, 1.0f, "0 st", "24 st");
    envEditor->addChangeListener (this);
    addAndMakeVisible (*envEditor);

    // --- 波形显示 + 开关
    addAndMakeVisible (waveDisplay);

    static const char* waveNames[4] { "SUB", "C1", "C2", "C3" };
    for (int i = 0; i < 4; ++i)
    {
        waveToggleButtons[(size_t) i].setButtonText (waveNames[i]);
        waveToggleButtons[(size_t) i].setToggleState (i == 0, juce::dontSendNotification);
        waveToggleButtons[(size_t) i].setClickingTogglesState (true);
        waveToggleButtons[(size_t) i].onClick = [this, i]
        {
            waveDisplay.setSourceVisible (i, waveToggleButtons[(size_t) i].getToggleState());
        };
        addAndMakeVisible (waveToggleButtons[(size_t) i]);
    }

    // --- EQ 曲线（可拖动）
    eqCurve.onBandChanged = [this] (int band, const EqCurveEditor::BandState& s)
    {
        pushEqBandToParams (band, s);

    // 点击曲线上的节点时，下方旋钮跟着切到那一段
    eqCurve.onSelectionChanged = [this] (int band)
    {
        if (band >= 0)
            bindEqKnobs (band);
    };
        eqStateCache[(size_t) band] = s;
    };
    eqCurve.onBandEnableChanged = [this] (int band, bool enabled)
    {
        setEqBandEnabled (band, enabled);
        eqStateCache[(size_t) band].enabled = enabled;
    };
    addAndMakeVisible (eqCurve);

    // --- 电平表
    addAndMakeVisible (levelMeter);

    // --- 采样载入按钮
    for (int i = 0; i < kNumClickLayers; ++i)
    {
        loadButtons[(size_t) i].setButtonText ("LOAD");
        loadButtons[(size_t) i].onClick = [this, i] { openSampleBrowser (i); };
        addAndMakeVisible (loadButtons[(size_t) i]);

        clickNameLabels[(size_t) i].setText ("CLICK " + juce::String (i + 1) + "  -  (empty)",
                                             juce::dontSendNotification);
        clickNameLabels[(size_t) i].setInterceptsMouseClicks (false, false);
        clickNameLabels[(size_t) i].setJustificationType (juce::Justification::centredLeft);
        clickNameLabels[(size_t) i].setColour (juce::Label::textColourId, theme::textDim);
        clickNameLabels[(size_t) i].setFont (juce::Font (juce::FontOptions (9.5f)));
        addAndMakeVisible (clickNameLabels[(size_t) i]);
    }

    // --- 波形选择器
    // Sub：选项直接就是 6 种波形，无占位，offset = 0
    subWaveSelector.attachTo (proc.getAPVTS(), pid::subWave, 0, wave::allNames());
    subWaveSelector.setAccent (theme::cyan);
    addAndMakeVisible (subWaveSelector);

    // Click 层：第一项是 "Sample"，波形从第二项开始，offset = 1
    {
        juce::StringArray clickWaveNames;
        clickWaveNames.add ("Sample");
        clickWaveNames.addArray (wave::allNames());

        for (int i = 0; i < kNumClickLayers; ++i)
        {
            clickWaveSelectors[(size_t) i].attachTo (proc.getAPVTS(),
                                                     pid::click (i, "wave"),
                                                     1, clickWaveNames);
            clickWaveSelectors[(size_t) i].setAccent (theme::clickColour (i));
            addAndMakeVisible (clickWaveSelectors[(size_t) i]);
        }
    }

    // --- 一次性创建所有参数控件
    buildAllControls();

    // 构造末尾应用一次分页可见性，保证初始状态正确
    // （buildAllControls 创建时所有控件都是可见的）
    applyPageVisibility();

    // --- 预设列表
    proc.getPresetManager().onPresetLoaded = [this] { onPresetLoaded(); };
    {
        presetSelector.clear (juce::dontSendNotification);
        const auto& list = proc.getPresetManager().getPresets();
        for (int i = 0; i < (int) list.size(); ++i)
            presetSelector.addItem (list[(size_t) i].name, i + 1);

        presetSelector.setSelectedId (1, juce::dontSendNotification);
    }

    // --- 默认采样，保证打开就有声音
    // 给**每一层**都铺一个不同的采样：原来只给第 1 层加载，
    // 其余三层是空的，用户打开后听不到声音，很难判断是"参数没生效"
    // 还是"这一层本来就没内容"。
    proc.getSampleLibrary().initialise (proc.getCurrentSampleRate());
    for (int i = 0; i < kNumClickLayers; ++i)
        loadLibrarySampleInto (i * 7, i);   // 各层取库里不同位置的采样

    pullEqStateFromParams();

    setResizable (true, true);

    // -----------------------------------------------------------------------
    // 窗口尺寸
    //
    // 高度上限很关键：分页后每页内容固定（CLICK 最多约 206px），
    // 窗口一旦被拉高，多出来的空间无法被内容消化，只会变成大片空白
    // （实测 1000px 高的窗口里，SUB 面板被拉到 760px，控件只占中间 200px）。
    //
    // 因此严格限制高度区间：默认 520，最高 640。
    // 宽度方向不影响观感（旋钮会自动居中），可以放得宽一些。
    // -----------------------------------------------------------------------
    setResizeLimits (960, 560, 1800, 720);
    setSize (1080, 620);

    startTimerHz (24);
    proc.requestWaveCapture();
}

KickForgeEditor::~KickForgeEditor()
{
    stopTimer();
    proc.getPresetManager().onPresetLoaded = nullptr;
    setLookAndFeel (nullptr);
}

//==============================================================================
// 控件创建
//==============================================================================

void KickForgeEditor::addKnob (juce::Rectangle<int> bounds, const juce::String& paramID,
                               const juce::String& displayName, juce::Colour accentOverride)
{
    auto k = std::make_unique<Knob>();
    k->paramID = paramID;

    k->slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false,
                               juce::jmin (bounds.getWidth(), 54), 11);
    // 数值框：透明底、无描边，弱化视觉权重，让拨盘成为主体
    k->slider.setColour (juce::Slider::textBoxTextColourId, theme::text);
    k->slider.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    k->slider.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    k->slider.setColour (juce::Slider::textBoxHighlightColourId, theme::accent.withAlpha (0.4f));

    k->label.setText (displayName, juce::dontSendNotification);
    k->label.setJustificationType (juce::Justification::centred);
    k->label.setFont (juce::Font (juce::FontOptions (8.5f)));
    k->label.setColour (juce::Label::textColourId, theme::textDim);

    if (proc.getAPVTS().getParameter (paramID) != nullptr)
    {
        k->attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
            proc.getAPVTS(), paramID, k->slider);
    }
    else
    {
        jassertfalse;   // 参数 ID 写错
    }

    // --- 功能色：频率类参数用橙色，其余用青色。
    // 这样一眼能看出"这个旋钮是调频率的"还是"调别的"（学参考图的做法）。
    auto isFrequencyParam = [&paramID]
    {
        return paramID.contains ("freq") || paramID.contains ("_lp")
            || paramID.contains ("_hp")   || paramID.contains ("tune")
            || paramID.contains ("wavetune") || paramID.contains ("ceiling")
            || paramID.contains ("harmonics");
    };

    k->slider.setColour (KickLookAndFeel::knobAccentColourId,
                         accentOverride.isTransparent()
                             ? (isFrequencyParam() ? theme::orange : theme::cyan)
                             : accentOverride);

    addAndMakeVisible (k->slider);
    addAndMakeVisible (k->label);

    auto labelArea = bounds.removeFromBottom (10);
    k->slider.setBounds (bounds);
    k->label.setBounds (labelArea);

    knobs.push_back (std::move (k));
}

void KickForgeEditor::addToggle (juce::Rectangle<int> bounds, const juce::String& paramID,
                                 const juce::String& displayName)
{
    auto t = std::make_unique<Toggle>();
    t->paramID = paramID;
    t->button.setButtonText (displayName);

    if (proc.getAPVTS().getParameter (paramID) != nullptr)
    {
        t->attachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
            proc.getAPVTS(), paramID, t->button);
    }
    else
    {
        jassertfalse;
    }

    addAndMakeVisible (t->button);
    t->button.setBounds (bounds);
    toggles.push_back (std::move (t));
}

void KickForgeEditor::addCombo (juce::Rectangle<int> bounds, const juce::String& paramID,
                                const juce::String& displayName)
{
    juce::ignoreUnused (displayName);

    auto c = std::make_unique<Combo>();
    c->paramID = paramID;
    auto* raw = &c->box;

    if (auto* choice = dynamic_cast<juce::AudioParameterChoice*> (proc.getAPVTS().getParameter (paramID)))
    {
        raw->addItemList (choice->choices, 1);
        comboAttachments.push_back (
            std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (
                proc.getAPVTS(), paramID, *raw));
    }
    else
    {
        jassertfalse;
    }

    addAndMakeVisible (raw);
    raw->setBounds (bounds);
    combos.push_back (std::move (c));
}

void KickForgeEditor::addButton (juce::Rectangle<int> bounds, const juce::String& text,
                                 std::function<void()> onClick, juce::Colour bg)
{
    auto b = std::make_unique<PlainButton>();
    b->button.setButtonText (text);

    if (! bg.isTransparent())
        b->button.setColour (juce::TextButton::buttonColourId, bg);

    b->onClick = std::move (onClick);
    b->button.onClick = [fn = b->onClick] { if (fn) fn(); };

    addAndMakeVisible (b->button);
    b->button.setBounds (bounds);
    buttons.push_back (std::move (b));
}

//==============================================================================

void KickForgeEditor::placeKnob (const juce::String& paramID, juce::Rectangle<int> bounds)
{
    for (auto& k : knobs)
    {
        if (k->paramID == paramID)
        {
            // 旋钮的纵向构成：拨盘 + 下方数值框(12) + 名称标签(10)。
            // 数值框与标签由 Slider/Label 自己画，这里必须先扣掉它们的高度，
            // 否则拨盘会吃掉全部空间、把标签挤出面板。
            // 名称(9) + 数值(11) 之外全部给拨盘
            auto labelArea = bounds.removeFromBottom (9);
            auto valueArea = bounds.removeFromBottom (11);

            k->slider.setBounds (bounds);
            k->slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false,
                                       valueArea.getWidth(), valueArea.getHeight());
            k->label.setBounds (labelArea);
            return;
        }
    }
}

void KickForgeEditor::placeToggle (const juce::String& paramID, juce::Rectangle<int> bounds)
{
    for (auto& t : toggles)
        if (t->paramID == paramID) { t->button.setBounds (bounds); return; }
}

void KickForgeEditor::placeCombo (const juce::String& paramID, juce::Rectangle<int> bounds)
{
    for (auto& c : combos)
        if (c->paramID == paramID) { c->box.setBounds (bounds); return; }
}

//==============================================================================
// 一次性创建全部控件
//==============================================================================

void KickForgeEditor::buildAllControls()
{
    // ---------------- SUB ----------------
    // 一行旋钮：音量 / 衰减 / pitch 量 / 泛音数 / sub harmonics / 相位 / tune / glide
    {
        auto mk = [this] (const char* id, const char* nm) { addKnob ({}, id, nm); };
        mk (pid::subLevel,     "LEVEL");
        mk (pid::subDecay,     "DECAY");
        mk (pid::subPitchAmt,  "PITCH AMT");
        mk (pid::subHarmonics, "HARMONICS");
        mk (pid::subSubHarm,   "SUB HARM");
        mk (pid::subPhase,     "PHASE");
        mk (pid::tune,         "TUNE");
        mk (pid::glide,        "GLIDE");

        // 第二行：音色细节。
        // 注意必须在这里**创建**，placeKnob() 只是定位已有控件；
        // 漏掉创建的话布局会静默留空（这个坑在 Click 开关上踩过一次）。
        mk (pid::subShape,      "WAVE SHAPE");
        mk (pid::subHarmTilt,   "HARM TILT");
        mk (pid::subPitchDecay, "PITCH DECAY");
        mk (pid::subAmpPunch,   "AMP PUNCH");
    }

    // ---------------- CLICK 层 ----------------
    for (int i = 0; i < kNumClickLayers; ++i)
    {
        addKnob   ({}, pid::click (i, "level"), "LEVEL");
        addKnob   ({}, pid::click (i, "pitch"), "PITCH");
        addKnob   ({}, pid::click (i, "decay"), "DECAY");
        addKnob   ({}, pid::click (i, "lp"),    "LO PASS");
        addKnob   ({}, pid::click (i, "hp"),    "HI PASS");
        // 采样切片：只在采样模式下有意义
        addKnob   ({}, pid::click (i, "start"), "START");
        addKnob   ({}, pid::click (i, "end"),   "END");
        addKnob   ({}, pid::click (i, "shape"),    "SHAPE");
        addKnob   ({}, pid::click (i, "wavetune"), "WAVE TUNE");
        addKnob   ({}, pid::click (i, "fadein"),   "FADE IN");
    }

    // ---------------- EQ 频点旋钮（一组，跟随选中频点）----------------
    {
        auto setupKnob = [this] (juce::Slider& sl, juce::Label& lb,
                                 const juce::String& name, juce::Colour accent)
        {
            sl.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            sl.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 62, 12);
            sl.setColour (juce::Slider::textBoxTextColourId, theme::text);
            sl.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            sl.setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
            sl.setColour (KickLookAndFeel::knobAccentColourId, accent);
            addAndMakeVisible (sl);

            lb.setText (name, juce::dontSendNotification);
            lb.setJustificationType (juce::Justification::centred);
            lb.setFont (juce::Font (juce::FontOptions (8.5f)));
            lb.setColour (juce::Label::textColourId, theme::textDim);
            addAndMakeVisible (lb);
        };

        setupKnob (eqKnobs.freq, eqKnobs.freqLabel, "FREQ", theme::orange);
        setupKnob (eqKnobs.gain, eqKnobs.gainLabel, "GAIN", theme::cyan);
        setupKnob (eqKnobs.q,    eqKnobs.qLabel,    "Q",    theme::cyan);

        eqKnobs.onButton.setButtonText ("ON");
        eqKnobs.onButton.setClickingTogglesState (true);
        eqKnobs.onButton.setColour (juce::ToggleButton::tickColourId, theme::cyan);
        addAndMakeVisible (eqKnobs.onButton);

        // 初始绑定第 1 段
        bindEqKnobs (0);
    }

    // ---------------- DYNAMICS / 输出 ----------------
    addKnob ({}, pid::masterGain,    "MASTER");
    addKnob ({}, pid::compThresh,    "THRESH");
    addKnob ({}, pid::compRatio,     "RATIO");
    addKnob ({}, pid::compAttack,    "ATTACK");
    addKnob ({}, pid::compRelease,   "RELEASE");
    addKnob ({}, pid::compMakeup,    "MAKEUP");
    addKnob ({}, pid::limCeiling,    "CEILING");
    addKnob ({}, pid::distDrive,     "DRIVE");
    addKnob ({}, pid::distMix,       "MIX");
    // 调制 / 动态
    addKnob ({}, pid::subFilterHz,   "SUB FILTER");
    addKnob ({}, pid::subFilterQ,    "FILTER Q");
    addKnob ({}, pid::subFilterEnv,  "FILTER ENV");
    addKnob ({}, pid::subOddEven,    "ODD/EVEN");
    addKnob ({}, pid::subNoiseAmt,   "NOISE ATK");
    addKnob ({}, pid::subNoiseDecay, "NOISE DEC");
    addKnob ({}, pid::subNoiseHp,    "NOISE HP");
    addKnob ({}, pid::subPitchCurve, "PITCH CURVE");
    addKnob ({}, pid::subHold,       "HOLD");
    addKnob ({}, pid::subAttack,     "ATTACK");
    addKnob ({}, pid::subDetune,     "DETUNE");
    addKnob ({}, pid::subPhaseRand,  "PHASE RND");
    addKnob ({}, pid::subPitchDelay, "PITCH DLY");
    addKnob ({}, pid::subStage2Amt,  "STAGE2 AMT");
    addKnob ({}, pid::subStage2Time, "STAGE2 AT");
    addKnob ({}, pid::subGlideCurve, "GLIDE CRV");
    addKnob ({}, pid::subVelToDecay, "VEL>DECAY");
    addKnob ({}, pid::subWidth,      "WIDTH");
    addKnob ({}, pid::transAttack,   "ATTACK");
    addKnob ({}, pid::transSustain,  "SUSTAIN");
    addKnob ({}, pid::velAmount,     "VEL AMT");
    addKnob ({}, pid::compScHp,      "SC HP");
    addKnob ({}, pid::compLookahead, "LOOKAHEAD");

    // ---------------- 开关 ----------------
    addToggle ({}, pid::subOn,            "SUB ON");
    addToggle ({}, pid::subSubHarmOn,     "SUB HARM");
    addToggle ({}, pid::keytrack,         "KEYTRACK");
    addToggle ({}, pid::phaseReset,       "PHASE RST");
    addToggle ({}, pid::oversample,       "2x OS");

    for (int i = 0; i < kNumClickLayers; ++i)
    {
        // 每层的电源开关。这里必须显式创建：placeToggle() 是按 paramID
        // 在已有控件里查找的，如果没创建过就会静默跳过，
        // 结果是布局留了位置却什么都不画（表现为"开关不见了"）。
        addToggle ({}, pid::click (i, "on"), "ON");
        addToggle ({}, pid::click (i, "keytrack"), "KEYTRACK");
    }

    addToggle ({}, pid::transOn, "TRANSIENT");
    addCombo  ({}, pid::velTarget, "VEL TGT");
    addCombo  ({}, pid::subFilterType, "FILT TYPE");
    addToggle ({}, pid::compOn, "COMP ON");
    addToggle ({}, pid::limOn,  "LIMIT ON");
    addToggle ({}, pid::distOn, "DIST ON");

    // ---------------- 下拉 ----------------
    addCombo ({}, pid::glideMode, "GLIDE MODE");
    addCombo ({}, pid::distType,  "COLOUR MODEL");

    // 染色扩展参数
    addKnob ({}, pid::distBias,     "BIAS");       // 偶次谐波量
    addKnob ({}, pid::distTone,     "TONE");       // 明暗
    addToggle ({}, pid::distAutoGain, "AUTO GAIN");
}

//==============================================================================
// 布局
//==============================================================================

void KickForgeEditor::layoutSub (juce::Rectangle<int> area)
{
    // SUB 页两行旋钮：
    //   第 1 行 = 核心（音量/包络/音高/泛音数）
    //   第 2 行 = 音色细节（波形形状 / 泛音衰减 / Sub Harmonics 电平 / 相位 …）
    //
    // 面板有 300px 上限，原来只用了一行旋钮（135px），下半部大片空白。
    // 这里把已经实现但**从未暴露**的参数补上（Sub Wave Shape、Harmonic Tilt），
    // 让空间被真正有用的控件填满。
    static const char* row1[]
    {
        "sub_level", "sub_decay", "sub_pitchamt", "sub_harmonics",
        "sub_subharm", "sub_phase", "tune", "glide"
    };
    static const char* row2[]
    {
        "sub_shape", "sub_harmtilt", "sub_pitchdecay", "sub_amppunch",
        "sub_filterhz", "sub_filterq", "sub_filterenv"
    };
    static const char* row3[]
    {
        "sub_oddeven", "sub_noiseamt", "sub_noisedecay", "sub_noisehp",
        "sub_pitchcurve", "sub_hold", "sub_width",
        "sub_attack", "sub_detune", "sub_phaserand"
    };
    static const char* row4[]
    {
        "sub_pitchdelay", "sub_stage2amt", "sub_stage2time",
        "sub_glidecurve", "sub_veltodecay"
    };

    // 旋钮尺寸由面板实际空间反推，而不是写死 68px。
    //
    // 之前固定 68px 格宽 + 居中，在 1080px 窗口下两侧各空 443px
    // —— 面板一大半是空的，看起来"太空"。
    // 现在：
    //   格宽 = 行可用宽度 / 该行旋钮数（上限 132，避免超宽屏拉成椭圆）
    //   行高 = 可用高度 / 3 行（上限 96，避免超高窗口下拨盘过大）
    constexpr int kControlH = 28;

    const int n1 = (int) (sizeof (row1) / sizeof (row1[0]));
    const int n2 = (int) (sizeof (row2) / sizeof (row2[0]));
    const int n3 = (int) (sizeof (row3) / sizeof (row3[0]));
    const int n4 = (int) (sizeof (row4) / sizeof (row4[0]));
    const int maxKnobs = juce::jmax (juce::jmax (n1, n2), juce::jmax (n3, n4));

    // 行高：3 行旋钮 + 控件行 + 间距，按可用高度均分
    // 4 行旋钮 + 控件行
    const int gapsAndCtl = 6 + 6 + 6 + 10 + kControlH;
    const int knobRowH = juce::jlimit (58, 88,
                                       (area.getHeight() - gapsAndCtl) / 4);

    // 每行的格宽按"最宽的那一行"统一，这样三行左右对齐、视觉稳定。
    // 上限 132px：再宽拨盘就变成椭圆了。
    const int cellW = juce::jlimit (60, 132, area.getWidth() / juce::jmax (1, maxKnobs));

    auto layoutKnobRow = [&] (const char** ids, int n)
    {
        auto r = area.removeFromTop (knobRowH);
        const int totalW = cellW * n;
        const int startX = r.getX() + juce::jmax (0, (r.getWidth() - totalW) / 2);

        for (int i = 0; i < n; ++i)
            placeKnob (ids[i], { startX + i * cellW, r.getY(), cellW, knobRowH });
    };

    layoutKnobRow (row1, n1);
    area.removeFromTop (6);
    layoutKnobRow (row2, n2);
    area.removeFromTop (6);
    layoutKnobRow (row3, n3);
    area.removeFromTop (6);
    layoutKnobRow (row4, n4);
    area.removeFromTop (10);

    // --- 控件行（波形选择器 + 开关组），整体居中
    auto rCtl = area.removeFromTop (kControlH);

    const int waveW = 150;
    const int gap = 8;
    const int switchesW = 82 + gap + 92 + gap + 92 + gap + 84 + gap + 84 + gap + 70;
    const int totalW3ctl = waveW + gap + 104 + gap + switchesW;

    auto sr = rCtl.withSizeKeepingCentre (juce::jmin (rCtl.getWidth(), totalW3ctl), kControlH);

    subWaveSelector.setBounds (sr.removeFromLeft (waveW).reduced (0, 1));
    sr.removeFromLeft (gap);

    // 滤波类型紧挨波形选择器（都是"选择型"控件）
    placeCombo ("sub_filtertype", sr.removeFromLeft (104).reduced (0, 1));
    sr.removeFromLeft (gap);

    placeToggle ("sub_on",        sr.removeFromLeft (82).reduced (0, 1));
    sr.removeFromLeft (gap);
    placeToggle ("sub_subharmon", sr.removeFromLeft (92).reduced (0, 1));
    sr.removeFromLeft (gap);
    placeToggle ("keytrack",      sr.removeFromLeft (92).reduced (0, 1));
    sr.removeFromLeft (gap);
    placeCombo  ("glidemode",     sr.removeFromLeft (84).reduced (0, 1));
    sr.removeFromLeft (gap);
    placeToggle ("phase_reset",   sr.removeFromLeft (84).reduced (0, 1));
    sr.removeFromLeft (gap);
    placeToggle ("oversample",    sr.removeFromLeft (70).reduced (0, 1));
}

void KickForgeEditor::layoutClick (juce::Rectangle<int> area)
{
    const int colW = area.getWidth() / kNumClickLayers;

    for (int i = 0; i < kNumClickLayers; ++i)
    {
        auto col = area.removeFromLeft (colW).reduced (5, 0);

        // 标题行：电源开关 + 采样名 + 载入按钮
        auto head = col.removeFromTop (24);
        placeToggle (pid::click (i, "on"), head.removeFromLeft (50).reduced (0, 2));
        head.removeFromLeft (5);

        // 载入按钮靠右，采样名占中间剩余空间
        loadButtons[(size_t) i].setBounds (head.removeFromRight (48).reduced (0, 1));
        head.removeFromRight (4);
        clickNameLabels[(size_t) i].setBounds (head);

        col.removeFromTop (3);

        // 波形选择器：占据本层顶部一整行，带波形缩略图
        clickWaveSelectors[(size_t) i].setBounds (col.removeFromTop (22).reduced (0, 1));

        col.removeFromTop (2);

        // 两行旋钮：LEVEL/PITCH/DECAY 与 LO PASS/HI PASS
        // 旋钮均分列宽（3 + 2），并在第二行右侧放 KEYTRACK，
        // 这样每层都填满、不会右侧留空。
        auto r1 = col.removeFromTop (62);
        const int kw = r1.getWidth() / 3;
        placeKnob (pid::click (i, "level"), r1.removeFromLeft (kw));
        placeKnob (pid::click (i, "pitch"), r1.removeFromLeft (kw));
        placeKnob (pid::click (i, "decay"), r1);

        auto r2 = col.removeFromTop (62);
        const int kw2 = r2.getWidth() / 3;
        placeKnob (pid::click (i, "lp"), r2.removeFromLeft (kw2));
        placeKnob (pid::click (i, "hp"), r2.removeFromLeft (kw2));
        // 第二行第三格给 KEYTRACK 开关（垂直居中）
        placeToggle (pid::click (i, "keytrack"),
                     r2.reduced (2, 0).withSizeKeepingCentre (r2.getWidth() - 4,
                                                              juce::jmin (26, r2.getHeight())));

        // 第三行：波形形状 / 波形调音 / 起音淡化 / 切片起止
        // 这些参数早就存在（shape / wavetune），但一直没接到界面上。
        auto r3 = col.removeFromTop (62);
        const int kw3 = r3.getWidth() / 4;
        placeKnob (pid::click (i, "shape"),    r3.removeFromLeft (kw3));
        placeKnob (pid::click (i, "wavetune"), r3.removeFromLeft (kw3));
        placeKnob (pid::click (i, "fadein"),   r3.removeFromLeft (kw3));
        placeKnob (pid::click (i, "start"),    r3.removeFromLeft (kw3));
    }
}


//==============================================================================
// EQ 频点旋钮：把这一组旋钮重新绑定到指定频点
//==============================================================================
void KickForgeEditor::bindEqKnobs (int band)
{
    if (band < 0 || band >= EqCurveEditor::kNumBands)
        return;

    if (band == eqKnobBand)
        return;

    eqKnobBand = band;

    auto& apvts = proc.getAPVTS();

    // 断开旧绑定，再建新的（Attachment 析构即解绑）
    eqKnobs.freqAtt.reset();
    eqKnobs.gainAtt.reset();
    eqKnobs.qAtt.reset();
    eqKnobs.onAtt.reset();

    eqKnobs.freqAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, pid::eq (band, "freq"), eqKnobs.freq);
    eqKnobs.gainAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, pid::eq (band, "gain"), eqKnobs.gain);
    eqKnobs.qAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        apvts, pid::eq (band, "q"), eqKnobs.q);
    eqKnobs.onAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (
        apvts, pid::eq (band, "on"), eqKnobs.onButton);

    // 标题反映当前频点号
    eqKnobs.freqLabel.setText ("BAND " + juce::String (band + 1) + "  FREQ", juce::dontSendNotification);
    eqKnobs.onButton.setButtonText ("BAND " + juce::String (band + 1) + " ON");

    // 同步曲线上的高亮节点
    eqCurve.setSelectedBand (band, false);
}

void KickForgeEditor::layoutEqPanel (juce::Rectangle<int> area)
{
    // EQ 区 = 上方曲线 + 下方当前频点的旋钮
    //
    // 曲线给全局视野（能同时看到 8 段的整体形状），
    // 旋钮给精确调整（频率/增益/Q 手调比拖动更准）。

    constexpr int kKnobRowH = 74;
    constexpr int kHintH    = 14;

    auto hint = area.removeFromBottom (kHintH);
    juce::ignoreUnused (hint);

    area.removeFromBottom (8);

    // 旋钮行
    auto knobRow = area.removeFromBottom (kKnobRowH);

    // 曲线占剩余空间
    eqCurve.setBounds (area);

    // --- 旋钮行：4 个控件居中排列
    const int cellW = 84;
    const int gap = 14;
    const int totalW = cellW * 3 + gap * 2 + 96;   // 3 旋钮 + 2 间隔 + ON 按钮

    auto kr = knobRow.withSizeKeepingCentre (juce::jmin (knobRow.getWidth(), totalW), kKnobRowH);

    auto placeEq = [&] (juce::Slider& sl, juce::Label& lb)
    {
        auto cell = kr.removeFromLeft (cellW);
        auto labelArea = cell.removeFromBottom (11);
        lb.setBounds (labelArea);
        sl.setBounds (cell);
        kr.removeFromLeft (gap);
    };

    placeEq (eqKnobs.freq, eqKnobs.freqLabel);
    placeEq (eqKnobs.gain, eqKnobs.gainLabel);

    // Q 后面不留 gap（下一个是按钮）
    {
        auto cell = kr.removeFromLeft (cellW);
        auto labelArea = cell.removeFromBottom (11);
        eqKnobs.qLabel.setBounds (labelArea);
        eqKnobs.q.setBounds (cell);
    }

    kr.removeFromLeft (gap);
    eqKnobs.onButton.setBounds (kr.removeFromLeft (96).withSizeKeepingCentre (96, 26));
}

void KickForgeEditor::layoutDynamics (juce::Rectangle<int> area)
{
    // 动态页现在有三行：
    //   1) 主输出 / 压缩 / 限幅 / 失真
    //   2) 瞬态整形 + 力度映射
    //   3) 开关行
    constexpr int kKnobRowH = 72;
    constexpr int kControlH = 28;
    constexpr int kCellW    = 66;

    static const char* row1[]
    {
        "master_gain", "comp_thresh", "comp_ratio", "comp_attack",
        "comp_release", "comp_makeup", "limiter_ceiling", "dist_drive", "dist_mix"
    };
    static const char* row2[]
    {
        "dist_bias", "dist_tone",
        "trans_attack", "trans_sustain", "vel_amount",
        "comp_schp", "comp_lookahead"
    };

    auto layoutRow = [&] (const char** ids, int n, int rowH)
    {
        auto r = area.removeFromTop (rowH);
        const int cellW = juce::jmin (r.getWidth() / juce::jmax (1, n), 118);
        const int totalW = cellW * n;
        const int startX = r.getX() + juce::jmax (0, (r.getWidth() - totalW) / 2);
        for (int i = 0; i < n; ++i)
            placeKnob (ids[i], { startX + i * cellW, r.getY(), cellW, rowH });
    };

    layoutRow (row1, (int) (sizeof (row1) / sizeof (row1[0])), kKnobRowH);
    area.removeFromTop (6);
    layoutRow (row2, (int) (sizeof (row2) / sizeof (row2[0])), kKnobRowH);
    area.removeFromTop (12);

    // 控件行：压缩/限幅/失真/瞬态 开关 + 力度目标 + 失真类型
    auto r3 = area.removeFromTop (kControlH);
    const int gap = 8;
    // 控件行：开关 + 选择器。加宽了以容纳"染色模型"下拉与 AUTO GAIN 开关。
    const int switchesW = 76 + gap + 82 + gap + 76 + gap + 92 + gap + 88
                        + gap + 96 + gap + 78 + gap + 110;
    auto sr = r3.withSizeKeepingCentre (juce::jmin (r3.getWidth(), switchesW), kControlH);

    placeToggle ("comp_on",    sr.removeFromLeft (76).reduced (0, 1));
    sr.removeFromLeft (gap);
    placeToggle ("limiter_on", sr.removeFromLeft (82).reduced (0, 1));
    sr.removeFromLeft (gap);
    placeToggle ("dist_on",    sr.removeFromLeft (76).reduced (0, 1));
    sr.removeFromLeft (gap);
    placeToggle ("trans_on",   sr.removeFromLeft (92).reduced (0, 1));
    sr.removeFromLeft (gap);
    placeToggle ("dist_autogain", sr.removeFromLeft (88).reduced (0, 1));
    sr.removeFromLeft (gap);

    // 染色模型：名字较长（Transformer / Germanium），给足宽度
    placeCombo  ("dist_type",  sr.removeFromLeft (96).reduced (0, 1));
    sr.removeFromLeft (gap);
    placeCombo  ("vel_target", sr.removeFromLeft (78).reduced (0, 1));
    sr.removeFromLeft (gap);
    placeToggle ("sub_on",     sr.removeFromLeft (110).reduced (0, 1));
}

//==============================================================================

void KickForgeEditor::resized()
{
    auto bounds = getLocalBounds().reduced (10);

    // -----------------------------------------------------------------------
    // 顶栏（32px）：标题 / 预设 / 导出 / 试听
    // -----------------------------------------------------------------------
    auto top = bounds.removeFromTop (32);
    areaTop = top;

    titleLabel.setBounds (top.removeFromLeft (98).reduced (0, 4));
    top.removeFromLeft (2);
    brandLabel.setBounds (top.removeFromLeft (96));
    top.removeFromLeft (10);

    presetPrevButton.setBounds (top.removeFromLeft (24).reduced (0, 5));
    top.removeFromLeft (2);
    // 下拉框加宽：它现在是唯一的预设入口，要放得下较长的预设名
    presetSelector.setBounds (top.removeFromLeft (204).reduced (0, 6));
    top.removeFromLeft (2);
    presetNextButton.setBounds (top.removeFromLeft (24).reduced (0, 5));
    top.removeFromLeft (6);
    presetSaveButton.setBounds (top.removeFromLeft (54).reduced (0, 6));
    top.removeFromLeft (6);
    undoButton.setBounds (top.removeFromLeft (48).reduced (0, 6));
    top.removeFromLeft (2);
    redoButton.setBounds (top.removeFromLeft (48).reduced (0, 6));
    top.removeFromLeft (8);
    exportWavButton.setBounds (top.removeFromLeft (50).reduced (0, 6));

    // 宽度要容得下 "AI（Beta）"。
    //
    // 实测（12pt 字体）：
    //   文本宽 46.2px
    //   TextButton 默认左右内边距各约 13px
    //   -> 需要约 72px 才不截断；76px 时只剩 4px 余量，实际被截成 "AI（Beta"。
    // 这里给到 96px，留足余量（不同系统字体宽度略有差异）。
    aiPanelToggle.setBounds (top.removeFromRight (96).reduced (0, 6));
    top.removeFromRight (4);
    top.removeFromRight (6);
    auditionButton.setBounds (top.removeFromRight (100).reduced (0, 6));
    top.removeFromRight (8);

    // -----------------------------------------------------------------------
    // 上方固定区：包络 + 波形
    //
    // 包络是 Sub 与 Click 共用的视图，固定在顶部不随页签切换 ——
    // 这样调任何模块时都能同时看到包络，不用来回翻页。
    // -----------------------------------------------------------------------
    // 右侧面板：AI 对话。
    //
    // ⚠️ 必须在**所有顶部行都取完之后**才能占右侧空间。
    //
    // 之前的顺序是错的：先把面板从右侧挖走，再取页签行和 AI 栏行，
    // 导致：
    //   1. 面板顶部从提示条下方开始，**盖住了 AI 栏那一行**
    //      （截图里 MODEL/IMPORT 按钮压在快速标签上）
    //   2. 面板高度吃掉剩余全部高度，把底部的输入行挤出窗口
    //
    // 正确顺序：顶栏 / 提示条 / 页签 / AI 栏 -> 然后剩余区域才分给右侧面板。
    // -----------------------------------------------------------------------
    areaPageTabs = bounds.removeFromTop (28);
    //----------------------------------------------------------------------
    // AI 生成栏（顶栏下方的独立一行）
    //----------------------------------------------------------------------
    bounds.removeFromTop (6);
    auto aiBarArea = bounds.removeFromTop (26);
    areaAiBar = aiBarArea;

    bounds.removeFromTop (6);

    // 现在才划右侧面板 —— bounds 已经不含任何顶部行。
    //
    // 底部再留 20px：右下角有一个窗口缩放手柄（18x18），
    // 面板铺到底会和它视觉上叠在一起（实测重叠检查报出 1 处）。
    // 留出空隙后手柄独立可见、可拖拽。
    if (aiPanelVisible && aiChatPanel != nullptr)
    {
        auto right = bounds.removeFromRight (300);
        bounds.removeFromRight (8);
        aiChatPanel->setBounds (right.withTrimmedBottom (20));
    }

    // AI 栏的控件要在**扣除面板之前**布局，否则右侧按钮会被面板挤掉。
    // 所以把 layoutAiBar 推迟到这里，用保存下来的 aiBarArea。
    layoutAiBar (aiBarArea);

    // 上方区（包络 + 波形）随窗口自适应，但保证可用高度
    // 上方区（包络 + 波形）高度。
    //
    // 关键：必须有**上限**。原来只设了下限，结果窗口一拉高，
    // 上方区就无限膨胀（实测 839px 高的窗口里它吃掉 66%），
    // 而页内容固定 242px 不变 —— 比例完全失衡。
    //
    // 现在按"占可用高度的 42%"取值，并限制在 130~230px：
    // 既保证包络有足够高度看清曲线，又不会喧宾夺主。
    // 各页内容高度取最大值：SUB 需要 4 行旋钮，EQ 需要曲线 + 旋钮行
    constexpr int kMaxPageContent = 292;
    const int pageNeed = kMaxPageContent + 36;    // 标题 + 内边距

    // 用**固定区间**而不是百分比：百分比会随窗口线性增长，
    // 窗口一拉高包络区就成比例膨胀（实测 470->101px、660->201px，翻倍）。
    // 包络能看清曲线就够，不需要跟着窗口长。
    constexpr int kUpperIdeal = 170;   // 理想高度（一般窗口下取这个）
    constexpr int kUpperMax   = 195;   // 上限
    constexpr int kUpperMin   = 130;   // 下限

    // 窗口高时取理想值（不继续长）；窗口矮时按可用空间收缩
    const int room = bounds.getHeight() - pageNeed - 10;
    const int upperFinal = juce::jlimit (kUpperMin,
                                         kUpperMax,
                                         juce::jmin (kUpperIdeal, juce::jmax (kUpperMin, room)));
    auto upper = bounds.removeFromTop (upperFinal);
    bounds.removeFromTop (10);

    // 包络（左）+ 波形（右）
    auto waveArea = upper.removeFromRight (juce::jmax (300, upper.getWidth() * 33 / 100));
    upper.removeFromRight (10);

    areaEnv = upper;
    {
        auto tabs = upper.removeFromTop (24);
        const int tw = 70;
        for (int i = 0; i < kNumEnvPages; ++i)
            envPageTabs[(size_t) i].setBounds (tabs.removeFromLeft (tw).reduced (1, 0));
    }
    upper.removeFromTop (5);
    envEditor->setBounds (upper);

    areaWave = waveArea;
    {
        auto wt = waveArea.removeFromTop (22);
        const int bw = 48;
        for (int i = 0; i < 4; ++i)
            waveToggleButtons[(size_t) i].setBounds (wt.removeFromLeft (bw).reduced (1, 2));
    }
    waveArea.removeFromTop (5);
    waveDisplay.setBounds (waveArea);

    // -----------------------------------------------------------------------
    // 页标签栏
    // -----------------------------------------------------------------------
    {
        const int n = (int) Page::NumPages;
        const int tw = juce::jmax (86, areaPageTabs.getWidth() / n - 6);
        auto t = areaPageTabs;

        for (int i = 0; i < n; ++i)
            pageTabs[(size_t) i].setBounds (t.removeFromLeft (tw).reduced (3, 0));

        // 页标签右侧显示当前页说明
        juce::ignoreUnused (t);
    }

    // -----------------------------------------------------------------------
    // 右侧竖排电平表（KICK 2 的做法）
    //
    // 固定在窗口最右侧、从上到下的整条，任何页都可见。
    // 这样调任何参数时都能看到输出电平，不用切页。
    // -----------------------------------------------------------------------
    {
        constexpr int kMeterW = 76;
        auto meterStrip = bounds.removeFromRight (kMeterW);
        bounds.removeFromRight (8);          // 与页内容区留出间隔

        areaMeter = meterStrip;
        levelMeter.setBounds (meterStrip.reduced (0, 2));
    }

    // -----------------------------------------------------------------------
    // 页内容区
    // -----------------------------------------------------------------------
    areaPageBody = bounds;

    // 面板高度**有上限**，不随窗口无限拉伸。
    //
    // 两个坑都踩过了：
    //   1) "贴合内容高度" -> 窗口拉高时面板上下各留一大块空白
    //   2) "铺满页内容区" -> 窗口拉高时面板被拉成 760px，控件只占中间 200px
    // 正确做法：面板高度 = 内容高度 + 固定留白，上限 300px；
    // 面板之外的剩余空间留给窗口背景（看起来是"布局从容"而不是"控件浮在空面板里"）。
    // 面板高度上限：SUB 页 3 行旋钮 + 控件行最多需要约 340px。
    // 注意这只是**上限** —— 实际高度还会被页内容区可用高度限制，
    // 并且控件会在其中居中，不会出现"面板大、内容小"的空旷感。
    constexpr int kPanelMaxH = 380;

    auto panel = areaPageBody.withHeight (
        juce::jmin (areaPageBody.getHeight(), kPanelMaxH));
    areaSub = areaClick = areaEq = areaDyn = {};

    switch (currentPage)
    {
        case Page::Sub:      areaSub      = panel; break;
        case Page::Click:    areaClick    = panel; break;
        case Page::Eq:       areaEq       = panel; break;
        case Page::Dynamics: areaDyn      = panel; break;
        case Page::Mod:      areaMod      = panel; break;
        default: break;
    }

    // MOD 页自己铺满整块区域（内含左右两栏），不参与下面的"居中放置旋钮"。
    if (currentPage == Page::Mod)
    {
        if (modulationPanel == nullptr)
        {
            modulationPanel = std::make_unique<ModulationPanel> (proc);
            addAndMakeVisible (*modulationPanel);
        }
        modulationPanel->setBounds (panel);
        modulationPanel->setVisible (true);
        applyPageVisibility();
        return;
    }
    else if (modulationPanel != nullptr)
    {
        modulationPanel->setVisible (false);
    }

    // 控件块的高度（不含标题）
    int contentH = 0;
    switch (currentPage)
    {
        // SUB 现在是两行旋钮
        case Page::Sub:      contentH = 58 + 6 + 58 + 6 + 58 + 6 + 58 + 10 + 28; break;
        case Page::Dynamics: contentH = 72 + 6 + 72 + 12 + 28; break;
        case Page::Click:    contentH = 24 + 22 + 62 * 3 + 10; break;
        case Page::Eq:       contentH = 150 + 8 + 74 + 14; break;
        default:             contentH = 72; break;
    }

    // 面板内部可用区（扣掉标题条与内边距），把控件块在其中居中
    auto body = panel.reduced (14, 10).withTrimmedTop (section::titleHeight);
    const int h = juce::jmin (body.getHeight(), contentH);
    const int spare = juce::jmax (0, body.getHeight() - h);

    auto laid = body.withHeight (h).withY (body.getY() + spare / 2);

    if (currentPage == Page::Sub)      layoutSub (laid);
    if (currentPage == Page::Click)    layoutClick (laid);
    if (currentPage == Page::Eq)       layoutEqPanel (laid);
    if (currentPage == Page::Dynamics) layoutDynamics (laid);

    applyPageVisibility();
}

//==============================================================================
// 分页：只显示当前页的控件
//
// JUCE 的控件默认可见，必须显式隐藏非当前页的，否则会叠在一起。
// 这里按 paramID 前缀判断控件属于哪一页 —— 比维护四份列表可靠，
// 新增参数时只要命名规范就自动归类。
//==============================================================================

void KickForgeEditor::applyPageVisibility()
{
    auto pageOfParam = [] (const juce::String& id) -> int
    {
        if (id.startsWith ("sub_"))   return (int) Page::Sub;
        if (id.startsWith ("click"))  return (int) Page::Click;
        if (id.startsWith ("eq"))     return (int) Page::Eq;

        // 音高/滑音/相位/过采样都是 Sub 振荡器的属性，归 SUB 页
        if (id.startsWith ("tune") || id.startsWith ("glide")
            || id.startsWith ("keytrack")
            || id == pid::phaseReset || id == pid::oversample)
            return (int) Page::Sub;

        // 其余（comp_ / limiter_ / dist_ / master_ / oversample / phase_reset）
        return (int) Page::Dynamics;
    };

    const int cur = (int) currentPage;

    for (auto& k : knobs)
        k->slider.setVisible (pageOfParam (k->paramID) == cur),
        k->label.setVisible  (pageOfParam (k->paramID) == cur);

    for (auto& t : toggles)
        t->button.setVisible (pageOfParam (t->paramID) == cur);

    for (auto& c : combos)
        c->box.setVisible (pageOfParam (c->paramID) == cur);

    // Click 页专有控件
    const bool onClickPage = (currentPage == Page::Click);
    for (int i = 0; i < kNumClickLayers; ++i)
    {
        loadButtons[(size_t) i].setVisible (onClickPage);
        clickNameLabels[(size_t) i].setVisible (onClickPage);
        clickWaveSelectors[(size_t) i].setVisible (onClickPage);
    }

    // 波形选择器属于 Sub 页
    subWaveSelector.setVisible (currentPage == Page::Sub);

    // EQ 曲线与频点旋钮都属于 EQ 页。
    //
    // 它们都是"自绘组件或动态绑定的控件"，不在上面按 paramID 的循环里，
    // 所以必须在这里显式处理 —— 漏掉就会在所有页都可见、
    // 直接画在 SUB/CLICK 控件上面（EQ 曲线和旋钮都犯过这个错）。
    const bool onEqPage = (currentPage == Page::Eq);

    eqCurve.setVisible (onEqPage);

    eqKnobs.freq.setVisible (onEqPage);
    eqKnobs.gain.setVisible (onEqPage);
    eqKnobs.q.setVisible (onEqPage);
    eqKnobs.freqLabel.setVisible (onEqPage);
    eqKnobs.gainLabel.setVisible (onEqPage);
    eqKnobs.qLabel.setVisible (onEqPage);
    eqKnobs.onButton.setVisible (onEqPage);

    // 电平表常驻（任何页都能看到输出）
    levelMeter.setVisible (true);
}


void KickForgeEditor::selectPage (Page p)
{
    currentPage = p;

    for (int i = 0; i < (int) Page::NumPages; ++i)
        pageTabs[(size_t) i].setToggleState (i == (int) p, juce::dontSendNotification);

    resized();
    repaint();
}

void KickForgeEditor::paint (juce::Graphics& g)
{
    // 纯灰底，无渐变 —— 扁平化第一原则
    g.fillAll (theme::bg);

    // --- 顶栏：略亮一档 + 1px 底边，形成层次（不用阴影）
    if (! areaTop.isEmpty())
    {
        const auto t = areaTop.toFloat();

        g.setColour (theme::g2);
        g.fillRect (t);

        g.setColour (theme::outline);
        g.drawRect (t, 1.0f);
    }

    // --- 各分区：统一的标题栏 + 细边框
    section::draw (g, areaEnv,  "ENVELOPE", theme::cyan);
    section::draw (g, areaWave, "WAVEFORM", theme::cyan);

    // --- 页内容区：只画当前页那一块面板
    {
        static const char* titles[] { "SUB OSCILLATOR",
                                      "CLICK LAYERS  -  4 SLOTS",
                                      "MASTER EQ  -  8 BAND  -  DRAG NODES",
                                      "DYNAMICS  -  COMP / LIMIT / DIST" };

        const int pi = juce::jlimit (0, 3, (int) currentPage);
        const auto col = (pi == 1 || pi == 3) ? theme::orange : theme::cyan;

        const juce::Rectangle<int> body = (pi == 0) ? areaSub
                                       : (pi == 1) ? areaClick
                                       : (pi == 2) ? areaEq
                                                   : areaDyn;

        section::draw (g, body, titles[pi], col);
    }

    // --- 拖拽导入：高亮将要载入的那一层
    if (dragHoverLayer >= 0 && currentPage == Page::Click && ! areaClick.isEmpty())
    {
        auto col = areaClick.reduced (12, 8);
        const int colW = col.getWidth() / kNumClickLayers;
        auto target = col.withX (col.getX() + dragHoverLayer * colW).withWidth (colW).reduced (2);

        g.setColour (theme::cyan.withAlpha (0.18f));
        g.fillRoundedRectangle (target.toFloat(), 3.0f);
        g.setColour (theme::cyan);
        g.drawRoundedRectangle (target.toFloat().reduced (0.5f), 3.0f, 1.5f);

        g.setColour (theme::white);
        g.setFont (juce::Font (juce::FontOptions (11.0f).withStyle ("Bold")));
        g.drawText ("DROP TO LOAD", target.removeFromTop (18),
                    juce::Justification::centred, false);
    }

    // --- EQ 操作提示（只在 EQ 页显示）
    if (! areaEq.isEmpty() && currentPage == Page::Eq)
    {
        g.setColour (theme::textFaint);
        g.setFont (juce::Font (juce::FontOptions (8.5f)));
        g.drawText ("drag = freq/gain   wheel = Q   shift+wheel = gain   right-click = menu   double-click = reset",
                    areaEq.reduced (8, 4).removeFromBottom (10),
                    juce::Justification::centredRight, false);
    }
}

//==============================================================================

void KickForgeEditor::selectEnvelopePage (int page)
{
    currentEnvPage = juce::jlimit (0, kNumEnvPages - 1, page);

    for (int i = 0; i < kNumEnvPages; ++i)
        envPageTabs[(size_t) i].setToggleState (i == currentEnvPage, juce::dontSendNotification);

    envPageSelector.setSelectedId (currentEnvPage + 1, juce::dontSendNotification);
    refreshEnvelopeEditor();
}

void KickForgeEditor::refreshEnvelopeEditor()
{
    static const EnvelopeTarget targets[]
    {
        EnvelopeTarget::SubPitch, EnvelopeTarget::SubAmp,
        EnvelopeTarget::Click1Amp, EnvelopeTarget::Click2Amp,
        EnvelopeTarget::Click3Amp, EnvelopeTarget::Click4Amp
    };

    const auto target = targets[(size_t) currentEnvPage];

    envEditor->setEnvelope (&proc.getEnvelope (target));

    if (target == EnvelopeTarget::SubPitch)
    {
        const float amt = proc.getAPVTS().getRawParameterValue (pid::subPitchAmt)->load();
        envEditor->setValueRange (0.0f, 1.0f, "0 st",
                                  (amt >= 0 ? "+" : "") + juce::String (amt, 1) + " st");
    }
    else
    {
        envEditor->setValueRange (0.0f, 1.0f, "0", "1.0");
    }

    envEditor->repaint();
}

void KickForgeEditor::changeListenerCallback (juce::ChangeBroadcaster* source)
{
    if (source == envEditor.get())
    {
        static const EnvelopeTarget targets[]
        {
            EnvelopeTarget::SubPitch, EnvelopeTarget::SubAmp,
            EnvelopeTarget::Click1Amp, EnvelopeTarget::Click2Amp,
            EnvelopeTarget::Click3Amp, EnvelopeTarget::Click4Amp
        };
        proc.commitEnvelope (targets[(size_t) currentEnvPage]);
    }
}

//==============================================================================
// EQ 参数同步
//==============================================================================

void KickForgeEditor::pullEqStateFromParams()
{
    static const EqCurveEditor::Shape shapes[EqCurveEditor::kNumBands]
    {
        EqCurveEditor::Shape::HighPass, EqCurveEditor::Shape::Peak,
        EqCurveEditor::Shape::Peak,     EqCurveEditor::Shape::Peak,
        EqCurveEditor::Shape::Peak,     EqCurveEditor::Shape::Peak,
        EqCurveEditor::Shape::Peak,     EqCurveEditor::Shape::HighShelf
    };

    for (int b = 0; b < EqCurveEditor::kNumBands; ++b)
    {
        EqCurveEditor::BandState s;
        s.freq    = proc.getAPVTS().getRawParameterValue (pid::eq (b, "freq"))->load();
        s.gainDb  = proc.getAPVTS().getRawParameterValue (pid::eq (b, "gain"))->load();
        s.q       = proc.getAPVTS().getRawParameterValue (pid::eq (b, "q"))->load();
        s.enabled = proc.getAPVTS().getRawParameterValue (pid::eq (b, "on"))->load() > 0.5f;
        s.shape   = shapes[b];

        // slope 参数存的是选项索引 0..3 -> 12/24/36/48 dB/oct
        const int slopeIdx = (int) std::lround (
            proc.getAPVTS().getRawParameterValue (pid::eq (b, "slope"))->load());
        s.slopeDbPerOct = (juce::jlimit (0, 3, slopeIdx) + 1) * 12;

        eqStateCache[(size_t) b] = s;
        eqCurve.setBand (b, s);
    }
}

void KickForgeEditor::pushEqBandToParams (int band, const EqCurveEditor::BandState& s)
{
    auto setP = [this, band] (const char* which, float value)
    {
        if (auto* p = proc.getAPVTS().getParameter (pid::eq (band, which)))
            p->setValueNotifyingHost (p->convertTo0to1 (value));
    };

    setP ("freq", s.freq);
    setP ("gain", s.gainDb);
    setP ("q", s.q);

    // 写 slope：convertTo0to1 把索引 0..3 转成 0..1
    if (auto* p = proc.getAPVTS().getParameter (pid::eq (band, "slope")))
    {
        const int idx = juce::jlimit (0, 3, s.slopeDbPerOct / 12 - 1);
        p->setValueNotifyingHost (p->convertTo0to1 ((float) idx));
    }
}

void KickForgeEditor::setEqBandEnabled (int band, bool enabled)
{
    if (auto* p = proc.getAPVTS().getParameter (pid::eq (band, "on")))
        p->setValueNotifyingHost (enabled ? 1.0f : 0.0f);

    eqStateCache[(size_t) band].enabled = enabled;
    eqCurve.setBand (band, eqStateCache[(size_t) band]);
}

//==============================================================================
// 预设
//==============================================================================

void KickForgeEditor::loadPresetByIndex (int index)
{
    if (! proc.getPresetManager().loadPreset (index))
        return;

    presetSelector.setSelectedId (index + 1, juce::dontSendNotification);
}

void KickForgeEditor::onPresetLoaded()
{
    // 预设会改掉几乎所有参数，这里把 UI 侧的缓存同步一遍
    pullEqStateFromParams();
    refreshEnvelopeEditor();

    // 采样名可能变了（统一走 refreshClickLayerLabels，避免两处格式不一致）
    refreshClickLayerLabels();

    // 撤销/重做按钮的可用状态（每帧同步，避免状态过期）
    undoButton.setEnabled (proc.canUndo());
    redoButton.setEnabled (proc.canRedo());

    // AI 进度与参数高亮
    refreshAiHighlights();

    // 参数变化后记录撤销点。
    // 放在定时器里是为了"防抖"：拖动旋钮时每帧都会变，
    // 若每次都压栈会瞬间塞满历史；这里靠 pushUndoSnapshot 内部的
    // 相同性判断合并连续变化。
    proc.pushUndoSnapshot();

    proc.requestWaveCapture();
    repaint();
}


//==============================================================================
// AI 生成
//==============================================================================

void KickForgeEditor::layoutAiBar (juce::Rectangle<int> area)
{
    // 布局：提示词输入框（占大部分） + 生成 / 撤销 / 历史 + 快捷标签
    //
    // 快捷标签在窄窗口下会被挤掉 —— 优先保证输入框和生成按钮可用，
    // 标签是"锦上添花"，放不下就不显示。
    constexpr int kGap = 6;

    auto r = area;

    // 右侧固定按钮
    aiHistoryButton.setBounds (r.removeFromRight (66).reduced (0, 1));
    r.removeFromRight (kGap);
    aiUndoButton.setBounds (r.removeFromRight (66).reduced (0, 1));
    r.removeFromRight (kGap);
    aiGenerateButton.setBounds (r.removeFromRight (86).reduced (0, 1));
    r.removeFromRight (kGap);

    // 进度条叠在输入框位置下方（生成时显示，平时隐藏）
    aiProgress.setBounds (r.removeFromBottom (3));

    // 快捷标签：从左边开始，能放几个放几个
    int tagX = r.getX();
    const int tagW = 58;
    int shown = 0;

    for (auto& b : aiQuickTagButtons)
    {
        if (tagX + tagW > r.getRight() - 220)     // 给输入框留至少 220px
        {
            b->setVisible (false);
            continue;
        }
        b->setBounds (tagX, r.getY(), tagW, r.getHeight());
        b->setVisible (true);
        tagX += tagW + 4;
        ++shown;
    }

    // 输入框占剩下的宽度
    auto editorArea = r.withLeft (juce::jmax (tagX + kGap, r.getRight() - 2000));
    if (shown == 0)
        editorArea = r;                            // 没有标签时输入框铺满

    aiPromptEditor.setBounds (editorArea.reduced (0, 1));
}

//==============================================================================
void KickForgeEditor::runAiGenerate()
{
    const auto prompt = aiPromptEditor.getText().trim();

    if (prompt.isEmpty())
    {
        aiStatusText = juce::String::fromUTF8 ("请先输入描述");
        repaint();
        return;
    }

    // --- 记住生成前的状态，供 REVERT 用
    aiPreGenerateState = proc.getStateAsValueTree().createCopy();

    // --- 解析
    AiPatch patch;
    juce::String err;

    // 每次生成换个种子 -> 同一描述也有微妙变化
    aiSeed = (aiSeed * 1103515245 + 12345) & 0x7fffffff;
    if (aiSeed == 0) aiSeed = 1;

    if (! aiEngine.generate (prompt, aiSeed, patch, err))
    {
        aiStatusText = juce::String::fromUTF8 ("无法生成：") + err;
        repaint();
        return;
    }

    // --- 记录历史
    aiConfig.addPrompt (prompt);

    // --- 显示进度并应用（应用本身带 200ms 平滑过渡）
    aiStatusText = juce::String::fromUTF8 ("AI 正在捏音色…");
    aiProgress.setVisible (true);
    aiProgressValue = 0.0;
    repaint();

    applyAiPatch (patch);
}

void KickForgeEditor::applyAiPatch (const AiPatch& patch)
{
    auto& applier = proc.getAiApplier();

    applier.onFinished = [this]
    {
        refreshAiHighlights();
        repaint();
    };

    applier.apply (patch, true);

    // 高亮变化过的参数
    aiHighlightedParams = applier.getChangedParams();
    aiHighlightFrames = 180;          // 约 3 秒

    if (aiHighlightedParams.isEmpty())
        aiStatusText = juce::String::fromUTF8 ("参数已是最佳匹配，未做改动");
    else
        aiStatusText = juce::String::fromUTF8 ("已调整 ")
                     + juce::String (aiHighlightedParams.size())
                     + juce::String::fromUTF8 (" 个参数");

    repaint();

    // --- 自动试听
    if (aiConfig.autoAudition)
        proc.triggerAudition();
}

void KickForgeEditor::refreshAiHighlights()
{
    auto& applier = proc.getAiApplier();

    if (applier.isAnimating())
        aiProgressValue = applier.getProgress();
    else if (aiProgress.isVisible())
    {
        // 过渡结束：进度条再留一小会儿，然后隐藏
        aiProgressValue = 1.0;
    }

    if (aiHighlightFrames > 0)
    {
        --aiHighlightFrames;
        if (aiHighlightFrames == 0)
        {
            aiHighlightedParams.clear();
            aiProgress.setVisible (false);
            repaint();
        }
    }
}

void KickForgeEditor::revertAiGenerate()
{
    if (! aiPreGenerateState.isValid())
    {
        aiStatusText = juce::String::fromUTF8 ("没有可撤销的生成");
        repaint();
        return;
    }

    // 取消正在进行的过渡，然后整体恢复
    proc.getAiApplier().cancel();
    proc.setStateFromValueTree (aiPreGenerateState.createCopy());

    aiHighlightedParams.clear();
    aiHighlightFrames = 0;
    aiProgress.setVisible (false);
    aiStatusText = juce::String::fromUTF8 ("已恢复到生成前");

    if (proc.getPresetManager().onPresetLoaded)
        proc.getPresetManager().onPresetLoaded();

    resized();
    repaint();
}

void KickForgeEditor::showAiHistoryMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader (juce::String::fromUTF8 ("最近的描述"));

    const auto& hist = aiConfig.promptHistory;

    if (hist.empty())
    {
        m.addItem (1, juce::String::fromUTF8 ("（还没有历史记录）"), false, false);
    }
    else
    {
        for (int i = 0; i < (int) hist.size(); ++i)
            m.addItem (60000 + i, hist[(size_t) i]);
    }

    m.addSeparator();
    m.addItem (2, juce::String::fromUTF8 ("清空历史"), ! hist.empty());

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (aiHistoryButton),
        [this] (int result)
        {
            if (result == 0) return;

            if (result == 2)
            {
                aiConfig.promptHistory.clear();
                aiConfig.save();
                return;
            }

            if (result >= 60000)
            {
                const int idx = result - 60000;
                const auto& h = aiConfig.promptHistory;
                if (idx >= 0 && idx < (int) h.size())
                {
                    aiPromptEditor.setText (h[(size_t) idx], juce::dontSendNotification);
                    runAiGenerate();
                }
            }
        });
}

void KickForgeEditor::showPresetMenu()
{
    buildPresetMenu();
}


//==============================================================================
// 预设搜索
//
// 480 个预设即使分组也需要滚动。搜索直接用关键词过滤，
// 是最快的定位方式（支持匹配风格名、子类名、变体号）。
//==============================================================================

void KickForgeEditor::showPresetSearch()
{
    auto* aw = new juce::AlertWindow ("Find Preset",
                                      "Type part of a preset name (e.g. \"rolling\", \"808\", \"deep\")",
                                      juce::MessageBoxIconType::NoIcon);

    aw->addTextEditor ("query", "", "Search:");
    aw->addButton ("Find", 1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    aw->enterModalState (true, juce::ModalCallbackFunction::create (
        [this, aw] (int result)
        {
            std::unique_ptr<juce::AlertWindow> owner (aw);   // 负责释放

            if (result == 0)
                return;

            const auto query = aw->getTextEditorContents ("query").trim();
            if (query.isEmpty())
                return;

            const auto& list = proc.getPresetManager().getPresets();

            // 收集匹配项（按名字匹配，忽略大小写）
            std::vector<int> matches;
            for (int i = 0; i < (int) list.size(); ++i)
                if (list[(size_t) i].name.containsIgnoreCase (query))
                    matches.push_back (i);

            juce::PopupMenu m;
            m.addSectionHeader (juce::String (matches.size()) + " match"
                                + (matches.size() == 1 ? "" : "es") + " for \"" + query + "\"");

            if (matches.empty())
            {
                m.addItem (1, "No matches", false, false);
            }
            else
            {
                // 最多列 60 条，避免菜单过长
                const int shown = juce::jmin (60, (int) matches.size());
                for (int k = 0; k < shown; ++k)
                {
                    const int idx = matches[(size_t) k];
                    const bool isCurrent =
                        (list[(size_t) idx].name == proc.getPresetManager().getCurrentPresetName());
                    m.addItem (40000 + idx, list[(size_t) idx].name, true, isCurrent);
                }

                if ((int) matches.size() > shown)
                    m.addItem (1, "... " + juce::String ((int) matches.size() - shown)
                                  + " more (refine your search)", false, false);
            }

            m.showMenuAsync (juce::PopupMenu::Options()
                                 .withTargetComponent (presetSelector)
                                 .withMinimumWidth (260),
                [this] (int r)
                {
                    if (r >= 40000)
                        loadPresetByIndex (r - 40000);
                });
        }));
}

void KickForgeEditor::buildPresetMenu()
{
    juce::PopupMenu m;

    //----------------------------------------------------------------------
    // 两层菜单：风格 -> 该风格下全部预设（平铺）
    //
    // 为什么不是三层（风格 -> 子类 -> 变体）：
    // 三层每个音色要点三次，中间层还把 40 个预设切成 4 个一组，
    // 浏览效率更低。两层展开就是完整列表。
    // 子类信息保留在预设名里（"Rolling 01"），每 4 项之间加分隔线。
    //
    // ID：20000 + styleIdx*100 + slot
    //----------------------------------------------------------------------
    auto tree = FactoryPresets::buildMenuTree();
    const auto& list = proc.getPresetManager().getPresets();

    for (int si = 0; si < (int) tree.styles.size(); ++si)
    {
        const auto& style = tree.styles[(size_t) si];

        std::vector<int> flat;
        for (const auto& c : style.categories)
            for (int idx : c.presetIndices)
                flat.push_back (idx);

        juce::PopupMenu styleMenu;
        int slot = 0;
        int sinceBreak = 0;

        for (int factoryIndex : flat)
        {
            if (factoryIndex < 0 || factoryIndex >= (int) list.size())
                continue;

            const auto& info = list[(size_t) factoryIndex];
            const bool isCurrent = (info.name == proc.getPresetManager().getCurrentPresetName());

            // 去掉风格前缀："Techno - Rolling 01" -> "Rolling 01"
            juce::String display = info.name;
            const int dash = display.indexOfChar ('-');
            if (dash >= 0) display = display.substring (dash + 1).trim();

            styleMenu.addItem (20000 + si * 100 + slot, display, true, isCurrent);
            ++slot;

            if (++sinceBreak == 4 && slot < (int) flat.size())
            {
                styleMenu.addSeparator();
                sinceBreak = 0;
            }
        }

        m.addSubMenu (style.name + "  (" + juce::String ((int) flat.size()) + ")", styleMenu);
    }

    //--- 用户预设
    juce::PopupMenu userMenu;
    int userSlot = 0;

    for (int i = 0; i < (int) list.size(); ++i)
    {
        if (list[(size_t) i].isFactory) continue;

        const bool isCurrent = (list[(size_t) i].name == proc.getPresetManager().getCurrentPresetName());
        userMenu.addItem (30000 + userSlot++, list[(size_t) i].name, true, isCurrent);
    }

    m.addSubMenu ("User  (" + juce::String (userSlot) + ")", userMenu, userSlot > 0);

    //--- 收藏
    {
        juce::PopupMenu favMenu;
        int favSlot = 0;
        const auto& favs = proc.getPresetManager().getFavourites();

        for (const auto& fname : favs)
        {
            // 按名字反查索引（收藏存的是名字，顺序变化不会错位）
            for (int i = 0; i < (int) list.size(); ++i)
            {
                if (list[(size_t) i].name != fname)
                    continue;

                const bool isCurrent = (fname == proc.getPresetManager().getCurrentPresetName());
                favMenu.addItem (50000 + favSlot++, fname, true, isCurrent);
                break;
            }
        }

        if (favSlot > 0)
            m.addSubMenu ("Favourites  (" + juce::String (favSlot) + ")", favMenu);
        else
            m.addItem (50001, "Favourites  (star a preset first)", false, false);
    }

    m.addSeparator();
    m.addSectionHeader ("File");
    m.addItem (6, "Find Preset...");
    m.addItem (7, proc.getPresetManager().isFavourite (proc.getPresetManager().getCurrentPresetName())
                     ? "Remove from Favourites" : "Add to Favourites");
    m.addSeparator();
    m.addItem (1, "Save As User Preset...");
    m.addItem (2, "Export to File...");
    m.addItem (3, "Import from File...");
    m.addSeparator();
    m.addItem (4, "Delete Current Preset", ! list.empty());
    m.addSeparator();
    m.addItem (5, "Open Preset Folder");

    m.showMenuAsync (juce::PopupMenu::Options()
                         .withTargetComponent (presetSelector)
                         .withMinimumWidth (220),
        [this] (int result)
        {
            if (result == 0)
                return;

            // 风格菜单：20000 + style*100 + slot
            if (result >= 20000 && result < 30000)
            {
                const int packed = result - 20000;
                const int styleIdx = packed / 100;
                const int slot = packed % 100;

                auto t2 = FactoryPresets::buildMenuTree();
                if (styleIdx < (int) t2.styles.size())
                {
                    std::vector<int> flat;
                    for (const auto& c : t2.styles[(size_t) styleIdx].categories)
                        for (int idx : c.presetIndices)
                            flat.push_back (idx);

                    if (slot < (int) flat.size())
                        loadPresetByIndex (flat[(size_t) slot]);
                }
                return;
            }

            // 用户预设：30000 + slot
            if (result >= 30000)
            {
                int slot = 0;
                const auto& lst = proc.getPresetManager().getPresets();
                for (int i = 0; i < (int) lst.size(); ++i)
                {
                    if (lst[(size_t) i].isFactory) continue;
                    if (slot == result - 30000) { loadPresetByIndex (i); break; }
                    ++slot;
                }
                return;
            }

            // 收藏菜单：50000 + slot
            if (result >= 50000 && result < 60000)
            {
                const auto& favs = proc.getPresetManager().getFavourites();
                const int slot = result - 50000;
                if (slot >= 0 && slot < (int) favs.size())
                    proc.getPresetManager().loadPresetByName (favs[(size_t) slot]);
                return;
            }

            // 搜索结果：40000 + 预设索引
            if (result >= 40000 && result < 50000)
            {
                loadPresetByIndex (result - 40000);
                return;
            }

            switch (result)
            {
                case 6: showPresetSearch(); break;

                case 7:
                    proc.getPresetManager().toggleFavourite (
                        proc.getPresetManager().getCurrentPresetName());
                    break;

                case 1: savePresetWithDialog(); break;

                case 2: proc.getPresetManager().exportToFile (
                            this, proc.getPresetManager().getCurrentPresetName());
                        break;

                case 3: proc.getPresetManager().importFromFile (this); break;

                case 4:
                {
                    // 删除"当前"预设：在列表里按名字找
                    const auto cur = proc.getPresetManager().getCurrentPresetName();
                    const auto& all = proc.getPresetManager().getPresets();
                    for (int i = 0; i < (int) all.size(); ++i)
                    {
                        if (all[(size_t) i].name == cur && ! all[(size_t) i].isFactory)
                        {
                            proc.getPresetManager().deletePreset (i);
                            break;
                        }
                    }

                    // 重建下拉
                    presetSelector.clear (juce::dontSendNotification);
                    const auto& nl = proc.getPresetManager().getPresets();
                    for (int i = 0; i < (int) nl.size(); ++i)
                        presetSelector.addItem (nl[(size_t) i].name, i + 1);
                    presetSelector.setSelectedId (1, juce::dontSendNotification);
                    break;
                }

                case 5: PresetManager::getUserPresetDirectory().revealToUser(); break;

                default: break;
            }
        });
}

void KickForgeEditor::savePresetWithDialog()
{
    auto* aw = new juce::AlertWindow ("Save Preset",
                                      "Enter a name for this preset:",
                                      juce::MessageBoxIconType::NoIcon);
    aw->addTextEditor ("name", proc.getPresetManager().getCurrentPresetName() + " Copy", "Name:");
    aw->addButton ("Save",   1, juce::KeyPress (juce::KeyPress::returnKey));
    aw->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));

    aw->enterModalState (true, juce::ModalCallbackFunction::create (
        [this, aw] (int result)
        {
            std::unique_ptr<juce::AlertWindow> owned (aw);

            if (result != 1)
                return;

            const auto name = aw->getTextEditorContents ("name");
            if (name.trim().isEmpty())
                return;

            if (proc.getPresetManager().savePreset (name))
            {
                // 重建下拉并选中新预设
                presetSelector.clear (juce::dontSendNotification);
                const auto& nl = proc.getPresetManager().getPresets();
                for (int i = 0; i < (int) nl.size(); ++i)
                {
                    presetSelector.addItem (nl[(size_t) i].name, i + 1);
                    if (nl[(size_t) i].name == proc.getPresetManager().getCurrentPresetName())
                        presetSelector.setSelectedId (i + 1, juce::dontSendNotification);
                }
            }
        }), false);
}

//==============================================================================


//==============================================================================
// 采样管理：拖拽导入
//
// 把 WAV/AIFF 直接拖到某个 Click 层上即可载入。
// 落点判定用层的水平范围 —— 4 层并排，用户拖到哪一层就是哪一层。
//==============================================================================

int KickForgeEditor::layerAtX (int x) const
{
    // Click 层只在 CLICK 页可见；其它页不接受拖拽
    if (currentPage != Page::Click || areaClick.isEmpty())
        return -1;

    auto col = areaClick.reduced (12, 8);
    if (! col.contains (x, col.getCentreY()))
        return -1;

    const int colW = col.getWidth() / kNumClickLayers;
    if (colW <= 0) return -1;

    return juce::jlimit (0, kNumClickLayers - 1, (x - col.getX()) / colW);
}

bool KickForgeEditor::isInterestedInFileDrag (const juce::StringArray& files)
{
    if (files.isEmpty()) return false;
    const juce::File f (files[0]);
    return f.hasFileExtension ("wav;aif;aiff;flac;mp3;ogg");
}

void KickForgeEditor::fileDragEnter (const juce::StringArray&, int x, int y)
{
    dragHoverLayer = layerAtX (x);
    if (dragHoverLayer >= 0) repaint();
}

void KickForgeEditor::fileDragExit (const juce::StringArray&)
{
    dragHoverLayer = -1;
    repaint();
}

void KickForgeEditor::filesDropped (const juce::StringArray& files, int x, int y)
{
    const int layer = layerAtX (x);
    dragHoverLayer = -1;
    repaint();

    if (layer < 0 || files.isEmpty())
        return;

    filesDropped (files, layer);
}

void KickForgeEditor::filesDropped (const juce::StringArray& files, int layer)
{
    if (files.isEmpty() || layer < 0 || layer >= kNumClickLayers)
        return;

    const juce::File f (files[0]);
    if (! f.existsAsFile())
        return;

    auto& cl = proc.getClickLayerRef (layer);

    if (cl.loadFromFile (f))
    {
        // 切成采样模式，否则用户拖进来了却听不到（层还在波形模式）
        if (auto* p = proc.getAPVTS().getParameter (pid::click (layer, "wave")))
            p->setValueNotifyingHost (p->convertTo0to1 (0.0f));   // 0 = Sample

        // 确保这一层是开着的，否则拖进来也没声音
        if (auto* p = proc.getAPVTS().getParameter (pid::click (layer, "on")))
            p->setValueNotifyingHost (1.0f);

        cl.setLoadedName (f.getFileNameWithoutExtension());
        refreshClickLayerLabels();
        resized();
        repaint();
    }
}

void KickForgeEditor::refreshClickLayerLabels()
{
    for (int i = 0; i < kNumClickLayers; ++i)
    {
        const auto nm = proc.getClickLayerRef (i).getLoadedName();

        // 标签以**层号**开头，采样名放在后面。
        // 之前只显示采样名，而库里的采样恰好叫 "Click 08"/"Click 15"，
        // 看起来像是"第 8 层/第 15 层"，与实际的层号混淆。
        const juce::String text = "CLICK " + juce::String (i + 1)
                                + (nm.isNotEmpty() ? "  -  " + nm : "  -  (empty)");

        clickNameLabels[(size_t) i].setText (text, juce::dontSendNotification);
        clickNameLabels[(size_t) i].setTooltip (nm.isNotEmpty() ? nm : juce::String ("(no sample)"));
    }
}

void KickForgeEditor::loadLibrarySampleInto (int libraryIndex, int layer)
{
    proc.getSampleLibrary().loadIntoLayer (libraryIndex, proc.getClickLayerRef (layer));
    proc.notifySampleChanged();
}

void KickForgeEditor::openSampleBrowser (int layer)
{
    SampleBrowser::showFor (proc, this, layer);
}

void KickForgeEditor::exportWav()
{
    if (wavExporter == nullptr)
        return;


    WavExporter::Options opts;
    opts.midiNote = 36;              // C2，底鼓常用音高
    opts.velocity = 1.0f;
    opts.sampleRate = proc.getCurrentSampleRate() > 0.0 ? proc.getCurrentSampleRate() : 44100.0;
    opts.bitDepth = 24;
    opts.maxSeconds = 8.0;
    opts.normalizeToDb = 0.0f;       // 不归一化，保持与试听一致的电平

    wavExporter->exportAsync (opts, [this] (const WavExporter::Result& r)
    {
        if (r.ok)
        {
            // 导出成功后给个提示，并提供"在访达中显示"
            juce::MessageBoxOptions opts2 = juce::MessageBoxOptions()
                .withIconType (juce::MessageBoxIconType::InfoIcon)
                .withTitle ("WAV Exported")
                .withMessage (r.message + "\n\n" + r.file.getFileName())
                .withButton ("Show in Finder")
                .withButton ("OK");

            juce::AlertWindow::showAsync (opts2, [f = r.file] (int result)
            {
                if (result == 1)
                    f.revealToUser();
            });
        }
        else
        {
            juce::AlertWindow::showAsync (
                juce::MessageBoxOptions()
                    .withIconType (juce::MessageBoxIconType::WarningIcon)
                    .withTitle ("Export Failed")
                    .withMessage (r.message)
                    .withButton ("OK"),
                nullptr);
        }
    });
}

//==============================================================================

void KickForgeEditor::timerCallback()
{
    // 波形
    proc.copyWaveCapturesTo (waveDisplay);
    waveDisplay.repaint();

    // 频谱：把主输出的最近一段送给 EQ 显示
    // （只在 EQ 页可见，但喂数据很便宜，所以不做页面判断）
    if (auto* src = proc.getWaveCapture (0))
    {
        eqCurve.setSampleRateForSpectrum (proc.getSampleRate());
        eqCurve.pushSpectrum (src, KickForgeProcessor::kWaveCaptureSize);

        if (currentPage == Page::Eq)
            eqCurve.repaint();
    }

    // 电平表
    levelMeter.pushPeak (proc.getMeterPeak());
    levelMeter.pushGainReduction (proc.getMeterGainReductionDb());

    // 调制指示环：把当前调制量写到各旋钮上。
    //
    // 只在有激活路由时更新 —— 没有调制时把所有环清掉，
    // 避免"上次的残留值"一直画着。
    {
        auto& mod = proc.getModulation();

        for (auto& k : knobs)
        {
            if (k == nullptr)
                continue;

            const auto target = ModulationMatrix::targetForParamID (k->paramID);

            if (target == ModulationMatrix::Target::None || mod.isIdle())
            {
                // 用 range=0 表示"不画环"
                KickLookAndFeel::setModulationDisplay (k->slider, 0.0f, 0.0f);
                continue;
            }

            const float amount = mod.getValue (target);
            const float range  = ModulationMatrix::modulationRangeForTarget (target);
            KickLookAndFeel::setModulationDisplay (k->slider, amount, range);

            // 只在参数真的被推动时才重绘（省 CPU）
            if (std::abs (amount) > 1.0e-3f)
                k->slider.repaint();
        }

        // EQ 面板的 3 个旋钮**不在 knobs 数组里** ——
        // 它们绑定到"当前选中的频点"，paramID 会变，
        // 所以单独处理（漏掉的话 EQ 增益永远看不到调制环）。
        if (eqKnobBand >= 0)
        {
            const auto upd = [&] (juce::Slider& sl, ModulationMatrix::Target t)
            {
                if (t == ModulationMatrix::Target::None || mod.isIdle())
                {
                    KickLookAndFeel::setModulationDisplay (sl, 0.0f, 0.0f);
                    return;
                }
                const float amt = mod.getValue (t);
                KickLookAndFeel::setModulationDisplay (
                    sl, amt, ModulationMatrix::modulationRangeForTarget (t));
                if (std::abs (amt) > 1.0e-3f)
                    sl.repaint();
            };

            upd (eqKnobs.gain,
                 ModulationMatrix::targetForParamID (pid::eq (eqKnobBand, "gain")));
            // 频率与 Q 目前不是调制目标（会返回 None，环自动清掉）
            upd (eqKnobs.freq,
                 ModulationMatrix::targetForParamID (pid::eq (eqKnobBand, "freq")));
            upd (eqKnobs.q,
                 ModulationMatrix::targetForParamID (pid::eq (eqKnobBand, "q")));
        }
    }

    // 采样名（统一走 refreshClickLayerLabels，避免多处格式不一致）
    refreshClickLayerLabels();

    // Pitch 页的刻度跟随 PITCH AMT
    if (currentEnvPage == 0)
    {
        const float amt = proc.getAPVTS().getRawParameterValue (pid::subPitchAmt)->load();
        envEditor->setValueRange (0.0f, 1.0f, "0 st",
                                  (amt >= 0 ? "+" : "") + juce::String (amt, 1) + " st");
    }

    // EQ：参数可能被宿主自动化改动，定期同步到曲线显示
    for (int b = 0; b < EqCurveEditor::kNumBands; ++b)
    {
        const float f = proc.getAPVTS().getRawParameterValue (pid::eq (b, "freq"))->load();
        const float g = proc.getAPVTS().getRawParameterValue (pid::eq (b, "gain"))->load();
        const float q = proc.getAPVTS().getRawParameterValue (pid::eq (b, "q"))->load();
        const bool on = proc.getAPVTS().getRawParameterValue (pid::eq (b, "on"))->load() > 0.5f;

        const int slopeIdx = (int) std::lround (
            proc.getAPVTS().getRawParameterValue (pid::eq (b, "slope"))->load());
        const int slope = (juce::jlimit (0, 3, slopeIdx) + 1) * 12;

        auto& c = eqStateCache[(size_t) b];

        // 只在外部（非本 UI）改动时才写回，避免拖动时打架
        if (std::abs (c.freq - f) > 0.05f
            || std::abs (c.gainDb - g) > 0.005f
            || std::abs (c.q - q) > 0.0005f
            || c.enabled != on
            || c.slopeDbPerOct != slope)
        {
            c.freq = f;
            c.gainDb = g;
            c.q = q;
            c.enabled = on;
            c.slopeDbPerOct = slope;
            eqCurve.setBand (b, c);
        }
    }
}

//==============================================================================

juce::AudioProcessorEditor* KickForgeProcessor::createEditor()
{
    return new KickForgeEditor (*this);
}

} // namespace kf
