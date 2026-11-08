#include "AiChatPanel.h"
#include "PluginProcessor.h"
#include "Theme.h"
#include <cmath>

namespace kf
{

//==============================================================================
// 消息气泡
//
// 用一个 Label 画一条消息，通过背景色区分用户/助手。
// 为什么不用 ListBox：消息高度不固定（长短不一），
// ListBox 的等高行不适合，自己排更容易控制换行与间距。
//==============================================================================
namespace
{
class MessageBubble : public juce::Label
{
public:
    MessageBubble (const juce::String& text, bool isUser, bool isError, bool canApply)
        : juce::Label ({}, text), user (isUser), error (isError), applyable (canApply)
    {
        setJustificationType (juce::Justification::topLeft);
        setFont (juce::Font (juce::FontOptions (11.0f)));
        setColour (juce::Label::textColourId, theme::text);
        setMinimumHorizontalScale (1.0f);
        setBorderSize ({ 6, 8, 6, 8 });
    }

    /** 计算给定宽度下需要多高（Label 自己不做这个）。 */
    int heightForWidth (int w) const
    {
        juce::AttributedString s;
        s.setJustification (juce::Justification::topLeft);
        s.append (getText(), getFont());

        juce::TextLayout tl;
        tl.createLayout (s, (float) juce::jmax (40, w - 16));
        return (int) std::ceil (tl.getHeight()) + 12;
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();

        juce::Colour bg;
        if (error)       bg = theme::error.withAlpha (0.18f);
        else if (user)   bg = theme::orange.withAlpha (0.16f);
        else             bg = theme::cyan.withAlpha (0.13f);

        g.setColour (bg);
        g.fillRoundedRectangle (r, 4.0f);

        g.setColour (error ? theme::error : (user ? theme::orange : theme::cyan).withAlpha (0.55f));
        g.drawRoundedRectangle (r.reduced (0.5f), 4.0f, 1.0f);

        // 左侧竖条标示角色
        g.setColour (error ? theme::error : (user ? theme::orange : theme::cyan));
        g.fillRect (r.getX(), r.getY() + 3.0f, 2.0f, r.getHeight() - 6.0f);

        juce::Label::paint (g);
    }

    bool applyable = false;

private:
    bool user = false;
    bool error = false;

    JUCE_LEAK_DETECTOR (MessageBubble)
};
} // anonymous namespace

//==============================================================================
AiChatPanel::AiChatPanel (KickForgeProcessor& p)
    : proc (p),
      progress (progressValue)
{
    //--- 消息区
    messageViewport.setViewedComponent (&messageHolder, false);
    messageViewport.setScrollBarsShown (true, false);
    messageViewport.setScrollBarThickness (6);
    addAndMakeVisible (messageViewport);

    //--- 输入
    input.setMultiLine (false);
    input.setReturnKeyStartsNewLine (false);
    input.addListener (this);
    input.setTextToShowWhenEmpty (
        juce::String::fromUTF8 ("描述你想要的底鼓…"),
        theme::textDim);
    input.setFont (juce::Font (juce::FontOptions (11.5f)));
    input.setColour (juce::TextEditor::backgroundColourId, theme::panel.brighter (0.06f));
    input.setColour (juce::TextEditor::textColourId, theme::text);
    input.setColour (juce::TextEditor::outlineColourId, theme::outline);
    input.setColour (juce::TextEditor::focusedOutlineColourId, theme::cyan);
    addAndMakeVisible (input);

    sendButton.setColour (juce::TextButton::buttonColourId, theme::cyan.withAlpha (0.22f));
    sendButton.onClick = [this] { sendCurrentInput(); };
    addAndMakeVisible (sendButton);

    //--- 模型
    modelButton.onClick = [this] { showModelMenu(); };
    addAndMakeVisible (modelButton);

    importButton.setTooltip ("Import a .gguf model file you downloaded yourself");
    importButton.onClick = [this] { importModel(); };
    addAndMakeVisible (importButton);

    progress.setPercentageDisplay (false);
    progress.setColour (juce::ProgressBar::backgroundColourId, theme::panel.brighter (0.05f));
    progress.setColour (juce::ProgressBar::foregroundColourId, theme::cyan);
    addAndMakeVisible (progress);
    progress.setVisible (false);

    //--- 后端回调
    llm.onReply = [this] (juce::String reply, juce::String patchJson, juce::String error)
    {
        // 从推理线程回到消息线程
        juce::MessageManager::callAsync ([this, reply, patchJson, error]
        {
            waitingForReply = false;

            if (error.isNotEmpty())
            {
                appendAssistant (juce::String::fromUTF8 ("生成失败：") + error);
                setStatus (error);
                return;
            }

            appendAssistant (juce::String::fromUTF8 ("已生成音色"), patchJson);
            setStatus (juce::String::fromUTF8 ("完成（")
                       + juce::String (llm.getLastGenerationMs()) + " ms）");

            if (onPatchReady)
                onPatchReady (pendingPrompt, patchJson);
        });
    };

    models.onProgress = [this] { repaint(); };
    models.onFinished = [this] (bool ok, juce::String msg)
    {
        juce::MessageManager::callAsync ([this, ok, msg]
        {
            progress.setVisible (false);
            setStatus (msg);

            if (ok)
                loadSelectedModel();
        });
    };

    // 初始选默认模型
    selectedModelId = ModelManager::getDefaultModelId();

    // 打开面板就尝试加载（如果模型已在本地）
    if (ModelManager::isModelDownloaded (selectedModelId))
        loadSelectedModel();

    appendAssistant (juce::String::fromUTF8 (
        "你好！我可以根据描述捏底鼓。试试「有冲击力、低频很长的 Trap 底鼓」，"
        "或者用下面的快捷标签。"));
    setStatus (llm.isReady() ? juce::String::fromUTF8 ("模型已就绪")
                             : juce::String::fromUTF8 ("未加载模型（正在用本地引擎）"));

    startTimerHz (10);
}

AiChatPanel::~AiChatPanel()
{
    stopTimer();
    llm.onReply = nullptr;
    models.onFinished = nullptr;
    models.cancelDownload();
}

//==============================================================================
void AiChatPanel::timerCallback()
{
    // 下载进度
    if (models.isDownloading())
    {
        progress.setVisible (true);
        progressValue = models.getDownloadProgress();
        statusText = models.getStatusText();
    }

    // 等待回复时让省略号动起来，让用户知道没卡死
    if (waitingForReply)
    {
        ++dotsPhase;
        const int n = (dotsPhase / 3) % 4;
        setStatus (juce::String::fromUTF8 ("AI 正在捏音色")
                   + juce::String::repeatedString (".", n));
    }

    repaint();
}

void AiChatPanel::textEditorReturnKeyPressed (juce::TextEditor&)
{
    sendCurrentInput();
}

//==============================================================================
void AiChatPanel::setStatus (const juce::String& text)
{
    statusText = text;
    repaint();
}

void AiChatPanel::appendAssistant (const juce::String& text, const juce::String& patchJson)
{
    session.addAssistant (text, patchJson);

    auto b = std::make_unique<MessageBubble> (text, false, false, patchJson.isNotEmpty());
    messageHolder.addAndMakeVisible (*b);
    bubbles.push_back (std::move (b));

    resized();
    repaint();

    // 滚到底部
    messageViewport.setViewPosition (0, juce::jmax (0, messageHolder.getHeight()
                                                       - messageViewport.getHeight()));
}

//==============================================================================
void AiChatPanel::sendCurrentInput()
{
    const auto text = input.getText().trim();
    if (text.isEmpty() || waitingForReply)
        return;

    pendingPrompt = text;
    session.addUser (text);

    auto b = std::make_unique<MessageBubble> (text, true, false, false);
    messageHolder.addAndMakeVisible (*b);
    bubbles.push_back (std::move (b));
    input.clear();

    resized();

    if (llm.isReady())
    {
        // 用真正的 LLM（后台线程推理）
        waitingForReply = true;
        setStatus (juce::String::fromUTF8 ("AI 正在捏音色…"));
        llm.send (text, session);
    }
    else
    {
        // 回退：本地关键词引擎（毫秒级，无需等待）
        AiPatch patch;
        juce::String err;

        if (fallback.generate (text, (int) juce::Time::currentTimeMillis() & 0xffff, patch, err))
        {
            const auto json = patch.toJson();
            appendAssistant (juce::String::fromUTF8 ("用本地引擎生成了音色"
                                                     "（未加载模型，理解较简单）"), json);
            setStatus (juce::String::fromUTF8 ("本地引擎完成"));

            if (onPatchReady)
                onPatchReady (text, json);
        }
        else
        {
            appendAssistant (err);
            setStatus (err);
        }
    }
}

//==============================================================================
void AiChatPanel::rebuildModelList()
{
    const auto selected = selectedModelId;
    juce::ignoreUnused (selected);
}

void AiChatPanel::showModelMenu()
{
    juce::PopupMenu m;
    m.addSectionHeader (juce::String::fromUTF8 ("选择模型"));

    const auto list = ModelManager::scanAllModels();

    // 本地已有的模型
    for (int i = 0; i < (int) list.size(); ++i)
    {
        const auto& info = list[(size_t) i];
        const bool isCurrent = (info.id == selectedModelId);

        juce::String label = info.displayName;
        if (ModelManager::isUserModel (info.id))
            label << "  " << juce::String (info.sizeBytes / 1024 / 1024) << " MB";

        m.addItem (70000 + i, label, true, isCurrent);
    }

    m.addSeparator();

    // 可下载但还没有的
    for (const auto& info : ModelManager::getAvailableModels())
    {
        if (ModelManager::isModelDownloaded (info.id))
            continue;

        m.addItem (71000 + (int) (&info - &ModelManager::getAvailableModels()[0]),
                   juce::String::fromUTF8 ("下载 ") + info.displayName,
                   true, false);
    }

    m.addSeparator();
    m.addItem (70001, juce::String::fromUTF8 ("导入 GGUF 文件…"));
    m.addItem (70002, juce::String::fromUTF8 ("打开模型文件夹"));

    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (modelButton),
        [this, list] (int result)
        {
            if (result == 0) return;

            if (result == 70001) { importModel(); return; }

            if (result == 70002)
            {
                ModelManager::getModelDirectory().revealToUser();
                return;
            }

            // 已有模型：直接加载
            if (result >= 70000 && result < 71000)
            {
                const int idx = result - 70000;
                if (idx >= 0 && idx < (int) list.size())
                {
                    selectedModelId = list[(size_t) idx].id;
                    loadSelectedModel();
                }
                return;
            }

            // 需要下载
            if (result >= 71000)
            {
                const auto& avail = ModelManager::getAvailableModels();
                const int idx = result - 71000;
                if (idx >= 0 && idx < (int) avail.size())
                {
                    selectedModelId = avail[(size_t) idx].id;
                    setStatus (juce::String::fromUTF8 ("开始下载…"));
                    progress.setVisible (true);
                    models.downloadModel (selectedModelId);
                }
            }
        });
}

void AiChatPanel::importModel()
{
    auto chooser = std::make_shared<juce::FileChooser> (
        juce::String::fromUTF8 ("选择 .gguf 模型文件"),
        juce::File::getSpecialLocation (juce::File::userHomeDirectory)
            .getChildFile ("Downloads"),
        "*.gguf");

    chooser->launchAsync (juce::FileBrowserComponent::openMode
                              | juce::FileBrowserComponent::canSelectFiles,
        [this, chooser] (const juce::FileChooser& fc)
        {
            const auto f = fc.getResult();
            if (! f.existsAsFile())
                return;

            juce::String err;
            if (ModelManager::importModelFile (f, err))
            {
                setStatus (juce::String::fromUTF8 ("已导入 ") + f.getFileName());
                selectedModelId = "user:" + f.getFileName();
                loadSelectedModel();
            }
            else
            {
                setStatus (juce::String::fromUTF8 ("导入失败：") + err);
            }
        });
}

void AiChatPanel::loadSelectedModel()
{
    const auto file = ModelManager::resolveModelFile (selectedModelId);

    if (! file.existsAsFile())
    {
        setStatus (juce::String::fromUTF8 ("模型文件不存在，请先下载或导入"));
        return;
    }

    setStatus (juce::String::fromUTF8 ("正在加载模型…"));
    repaint();

    juce::String err;
    if (llm.loadModel (file, err))
        setStatus (juce::String::fromUTF8 ("模型已就绪，可以对话了"));
    else
        setStatus (juce::String::fromUTF8 ("加载失败：") + err);
}

//==============================================================================
void AiChatPanel::layoutMessages()
{
    const int w = messageViewport.getWidth() - 10;

    int y = 4;
    for (auto& b : bubbles)
    {
        const int h = static_cast<MessageBubble*> (b.get())->heightForWidth (w);
        b->setBounds (4, y, w, h);
        y += h + 5;
    }

    messageHolder.setSize (juce::jmax (10, w + 8), y + 4);
}

void AiChatPanel::resized()
{
    auto r = getLocalBounds().reduced (6);

    //--- 顶部：模型行
    auto top = r.removeFromTop (24);
    modelButton.setBounds (top.removeFromLeft (juce::jmax (80, top.getWidth() - 70)));
    top.removeFromLeft (4);
    importButton.setBounds (top.removeFromLeft (66));

    r.removeFromTop (4);
    progress.setBounds (r.removeFromTop (3));
    r.removeFromTop (4);

    //--- 底部：输入行
    auto bottom = r.removeFromBottom (26);
    sendButton.setBounds (bottom.removeFromRight (56));
    bottom.removeFromRight (4);
    input.setBounds (bottom);

    r.removeFromBottom (4);

    //--- 状态行
    r.removeFromBottom (14);

    //--- 中间：消息区
    messageViewport.setBounds (r);
    layoutMessages();
}

//==============================================================================
void AiChatPanel::paint (juce::Graphics& g)
{
    g.setColour (theme::panel);
    g.fillRoundedRectangle (getLocalBounds().toFloat(), 4.0f);

    g.setColour (theme::cyan.withAlpha (0.5f));
    g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 4.0f, 1.0f);

    // 标题
    g.setColour (theme::cyan);
    g.setFont (juce::Font (juce::FontOptions (10.0f).withStyle ("Bold")));
    g.drawText ("AI PATCH  -  DESCRIBE A KICK",
                getLocalBounds().removeFromTop (16).reduced (8, 0),
                juce::Justification::centredLeft);

    // 状态行
    if (statusText.isNotEmpty())
    {
        g.setColour (llm.isReady() ? theme::cyan.withAlpha (0.85f) : theme::textDim);
        g.setFont (juce::Font (juce::FontOptions (9.0f)));
        g.drawText (statusText,
                    getLocalBounds().removeFromBottom (14).reduced (8, 0),
                    juce::Justification::centredLeft);
    }
}

} // namespace kf
