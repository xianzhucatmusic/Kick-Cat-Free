#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"
#include "EnvelopeEditor.h"
#include "ModulationPanel.h"
#include "WaveDisplay.h"
#include "EqCurveEditor.h"
#include "LevelMeter.h"
#include "Theme.h"
#include "KickLookAndFeel.h"
#include "WavExporter.h"
#include "AiPatch.h"
#include "LocalPatchEngine.h"
#include "AiConfig.h"
#include "AiChatPanel.h"

namespace kf
{

//==============================================================================
/** 采样库浏览器：列出 180 个内置采样，点击试听、双击载入。 */
class SampleBrowser : public juce::Component,
                      private juce::ListBoxModel
{
public:
    SampleBrowser (KickForgeProcessor& p, int targetLayer);
    ~SampleBrowser() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    static void showFor (KickForgeProcessor& p, juce::Component* parent, int targetLayer);

private:
    int getNumRows() override;
    void paintListBoxItem (int rowNumber, juce::Graphics& g, int width, int height, bool rowIsSelected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent& e) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent& e) override;

    void loadRowIntoLayer (int row);

    KickForgeProcessor& processor;
    int layer;
    juce::ListBox list { "samples", this };
    juce::TextButton loadButton { "LOAD" };
    juce::TextButton closeButton { "CLOSE" };
    juce::Label infoLabel;
    juce::TextEditor filterBox;

    /** 过滤后的行 -> 库内索引 */
    std::vector<int> visibleIndices;
    void rebuildFilter();

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleBrowser)
};

//==============================================================================
//==============================================================================
//==============================================================================
/** 可点击通知的 ComboBox。
 *
 *  juce::ComboBox 没有 onClick，而预设下拉框需要"点任意位置都弹菜单"。
 *  这里重写 mouseDown 并回调，避免为了一个点击事件包一层自定义组件。
 */
class ClickableComboBox : public juce::ComboBox
{
public:
    std::function<void()> onClicked;

    void mouseDown (const juce::MouseEvent& e) override
    {
        // 关键：**不要**调用 ComboBox::mouseDown ——
        // 它会打开 ComboBox 自己的弹出列表，与我们弹出的风格菜单
        // 同时出现、互相重叠（这就是菜单重合的原因）。
        //
        // 这个下拉框只是"显示当前预设名 + 菜单入口"，
        // 列表完全由外部菜单负责，所以这里只转发点击、然后什么都不做。
        if (onClicked)
            onClicked();
        juce::ignoreUnused (e);
    }

    /** 彻底禁用下拉框自身的弹出行为（双保险）。 */
    void showPopup() override {}
};

class KickForgeEditor : public juce::AudioProcessorEditor,
                        public juce::FileDragAndDropTarget,
                        private juce::Timer,
                        private juce::ChangeListener
{
public:
    //==========================================================================
    // 拖拽导入：把 WAV/AIFF 拖到某个 Click 层上直接载入
    //==========================================================================
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void fileDragEnter (const juce::StringArray& files, int x, int y) override;
    void fileDragExit (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    /** 当前拖拽悬停在哪一层（-1 = 不在任何层上）。 */
    int dragHoverLayer = -1;

    /** 根据 x 坐标判断落在第几个 Click 层。 */
    int layerAtX (int x) const;

    /** 把文件导入指定层（供拖拽与测试调用）。 */
    void filesDropped (const juce::StringArray& files, int layer);


public:
    /** 包络编辑页数：2 个 Sub 页（Pitch/Amp）+ 每个 Click 层一页。 */
    static constexpr int kNumEnvPages = 2 + kNumClickLayers;

    explicit KickForgeEditor (KickForgeProcessor&);
    ~KickForgeEditor() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    /** 把 EQ 旋钮重新绑定到指定频点（公开给测试与外部调用）。 */
    void bindEqKnobs (int band);

private:
    void timerCallback() override;
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;

    // --- 控件工厂（只在构造时创建一次）
    void addKnob (juce::Rectangle<int> bounds, const juce::String& paramID,
                  const juce::String& displayName, juce::Colour accentOverride = {});
    void addToggle (juce::Rectangle<int> bounds, const juce::String& paramID,
                    const juce::String& displayName);
    void addCombo (juce::Rectangle<int> bounds, const juce::String& paramID,
                   const juce::String& displayName);
    void addButton (juce::Rectangle<int> bounds, const juce::String& text,
                    std::function<void()> onClick, juce::Colour bg = {});

    // --- 分区布局
    void buildAllControls();
    void layoutSub    (juce::Rectangle<int> area);
    void layoutClick  (juce::Rectangle<int> area);
    void layoutEqPanel(juce::Rectangle<int> area);
    void layoutDynamics (juce::Rectangle<int> area);

    // --- 定位辅助
    void placeKnob   (const juce::String& paramID, juce::Rectangle<int> bounds);
    void placeToggle (const juce::String& paramID, juce::Rectangle<int> bounds);
    void placeCombo  (const juce::String& paramID, juce::Rectangle<int> bounds);

    // --- 包络页
    void selectEnvelopePage (int page);
    void refreshEnvelopeEditor();

    // --- EQ
    void pullEqStateFromParams();
    void pushEqBandToParams (int band, const EqCurveEditor::BandState& s);
    void setEqBandEnabled (int band, bool enabled);

    // --- 预设
    void buildPresetMenu();

    /** 把 EQ 旋钮重新绑定到指定频点。 */
    /** 弹出按风格分组的预设菜单（预设下拉框的入口）。 */
    void showPresetMenu();

    /** 搜索预设：弹出输入框，按关键词过滤后列出匹配项。
     *  480 个预设靠分组滚动仍然慢，搜索是最快的路径。 */
    void showPresetSearch();
    void loadPresetByIndex (int index);
    void savePresetWithDialog();
    void onPresetLoaded();

    void openSampleBrowser (int layer);
    /** 弹出导出 WAV 对话框。 */
    void exportWav();
    void loadLibrarySampleInto (int libraryIndex, int layer);

    KickForgeProcessor& proc;
    KickLookAndFeel lnf;

    //==========================================================================
    // 顶部栏
    juce::Label      titleLabel;
    juce::Label      brandLabel;

    ClickableComboBox presetSelector;
    juce::TextButton presetPrevButton { "<" };
    juce::TextButton presetNextButton { ">" };
    juce::TextButton presetSaveButton { "SAVE" };
    juce::TextButton undoButton { "UNDO" };
    juce::TextButton redoButton { "REDO" };

    //==========================================================================
    // AI 生成栏
    //==========================================================================
    juce::TextEditor  aiPromptEditor;
    juce::TextButton  aiGenerateButton { "GENERATE" };
    juce::TextButton  aiUndoButton     { "REVERT" };
    juce::TextButton  aiHistoryButton  { "HISTORY" };
    juce::ProgressBar aiProgress { aiProgressValue };

    /** 快捷标签按钮（点一下把描述填进输入框）。 */
    std::vector<std::unique_ptr<juce::TextButton>> aiQuickTagButtons;

    /** 生成过程中高亮的参数 ID（过渡结束后保留一小段时间）。 */
    juce::StringArray aiHighlightedParams;
    int aiHighlightFrames = 0;

    double aiProgressValue = 0.0;

    kf::LocalPatchEngine aiEngine;
    kf::AiConfig         aiConfig;

    // 右侧 AI 聊天面板（可折叠）
    std::unique_ptr<kf::AiChatPanel> aiChatPanel;
    // 标 Beta：本地 LLM 功能仍在演进，且模型质量依赖用户机器性能。
    // 用 fromUTF8 构造 —— 中文/全角括号直接写进 const char* 会按
    // Latin-1 解释而乱码（这个坑踩过多次）。
    juce::TextButton aiPanelToggle { juce::String::fromUTF8 (u8"AI（Beta）") };
    bool aiPanelVisible = false;

private:

    /** 每次生成用不同种子，做出微妙变化。 */
    int aiSeed = 1;

    /** 上一次生成前的完整状态（用于 REVERT）。 */
    juce::ValueTree aiPreGenerateState;

    /** 生成的状态提示文字（"AI 正在捏音色…" / 错误信息）。 */
    juce::String aiStatusText;
    /** 导出当前音色为 WAV。 */
    juce::TextButton exportWavButton { "WAV" };

    /** 离线渲染导出器。 */
    std::unique_ptr<WavExporter> wavExporter;

    juce::TextButton auditionButton { "AUDITION" };

    /** 采样库按钮 */
    std::array<juce::TextButton, kNumClickLayers> loadButtons;

    //==========================================================================
    // 包络区
    juce::ComboBox   envPageSelector;
    std::unique_ptr<EnvelopeEditor> envEditor;
    std::array<juce::TextButton, kNumEnvPages> envPageTabs;

    //==========================================================================
    // 波形区
    WaveDisplay waveDisplay;
    std::array<juce::TextButton, 4> waveToggleButtons;

    //==========================================================================
    // 波形选择器（Sub + 每个 Click 层各一个）
    WaveformSelector subWaveSelector;
    std::array<WaveformSelector, kNumClickLayers> clickWaveSelectors;

    //==========================================================================
    // EQ 区
    EqCurveEditor eqCurve;
    /** EQ 段类型下拉（可选，配合右键菜单） */
    juce::ComboBox eqBandSelector;

    //==========================================================================
    // 电平表
    LevelMeter levelMeter;

    //==========================================================================
    // 控件容器
    struct Knob
    {
        juce::String paramID;
        juce::Slider slider;
        juce::Label  label;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };
    std::vector<std::unique_ptr<Knob>> knobs;

    struct Toggle
    {
        juce::String paramID;
        juce::ToggleButton button;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> attachment;
    };
    std::vector<std::unique_ptr<Toggle>> toggles;

    struct Combo
    {
        juce::String paramID;
        juce::ComboBox box;
    };
    std::vector<std::unique_ptr<Combo>> combos;
    std::vector<std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment>> comboAttachments;

    struct PlainButton
    {
        juce::TextButton button;
        std::function<void()> onClick;
    };
    std::vector<std::unique_ptr<PlainButton>> buttons;

    /** Click 层的采样名标签 */
    std::array<juce::Label, kNumClickLayers> clickNameLabels;

    bool controlsBuilt = false;
    int  currentEnvPage = 0;

    /** 当前悬停/选中的 EQ 段（用于高亮） */
    int selectedEqBand = 0;

    /** 缓存 EQ 参数值，避免每帧从 APVTS 读 32 次 */
    std::array<EqCurveEditor::BandState, EqCurveEditor::kNumBands> eqStateCache;

    //==========================================================================
    // 分区矩形（paint 里画面板背景与标题用）
    juce::Rectangle<int> areaTop, areaEnv, areaWave, areaSub, areaClick, areaEq, areaDyn;
    juce::Rectangle<int> areaMod;

    //==========================================================================
    // 分页
    //
    // 为什么分页：单页里把 8 个旋钮横铺 1120px，每格 140px 但拨盘被行高
    // 锁死在 58px，比例失衡（大格子小旋钮）。分页后每页控件少，
    // 旋钮可以做得更大、间距更匀，也更接近现代插件的组织方式。
    // 包络固定在上方（Sub/Click 共用），不随页签切换。
    enum class Page { Sub = 0, Click, Eq, Dynamics, Mod, NumPages };

    Page currentPage = Page::Sub;

    /** 页签按钮。 */
    std::array<juce::TextButton, (size_t) Page::NumPages> pageTabs;

    /** 调制页（源 + 路由矩阵）。懒创建 —— 不用就不占资源。 */
    std::unique_ptr<ModulationPanel> modulationPanel;

    /** 切换页：只显示当前页的控件。 */
    void selectPage (Page p);
    void applyPageVisibility();

    // --- AI 生成
    void layoutAiBar (juce::Rectangle<int> area);
    void runAiGenerate();
    void applyAiPatch (const kf::AiPatch& patch);
    void showAiHistoryMenu();
    void revertAiGenerate();
    void refreshAiHighlights();

    juce::Rectangle<int> areaPageTabs, areaPageBody, areaAiBar;

    /** 刷新 Click 层显示的采样名。 */
    void refreshClickLayerLabels();
    //==========================================================================
    // EQ 频点旋钮
    //
    // 8 个频点 x 4 个参数 = 32 个控件，全部铺开会挤掉曲线。
    // 做法：只创建**一组**旋钮，跟随当前选中的频点切换绑定。
    // 曲线负责全局视野，旋钮负责精确调整。
    //==========================================================================

    /** 一组 EQ 旋钮（对应一个频点）。 */
    struct EqKnobs
    {
        juce::Slider freq, gain, q;
        juce::Label  freqLabel, gainLabel, qLabel;
        juce::TextButton onButton;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> freqAtt, gainAtt, qAtt;
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> onAtt;
    };

    EqKnobs eqKnobs;

    /** 当前旋钮绑定到哪个频点（-1 = 无）。 */
    int eqKnobBand = -1;



    /** 右侧固定的电平表竖条。 */
    juce::Rectangle<int> areaMeter;


    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KickForgeEditor)
};

} // namespace kf
