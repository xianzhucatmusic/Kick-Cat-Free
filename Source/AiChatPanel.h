#pragma once

#include "ChatBackend.h"
#include "ModelManager.h"
#include "LlamaBackend.h"
#include "LocalPatchEngine.h"
#include <juce_gui_basics/juce_gui_basics.h>

namespace kf
{

class KickForgeProcessor;

//==============================================================================
/** 右侧 AI 聊天面板。
 *
 *  包含：
 *   - 对话记录（可滚动，用户/助手气泡区分）
 *   - 输入框 + 发送
 *   - 模型选择下拉（含"导入模型…"）
 *   - 模型下载/加载状态与进度
 *   - 快捷标签
 *
 *  设计取舍：
 *   - 推理跑在 LlamaBackend 的后台线程，UI 只通过定时器轮询状态，
 *     绝不在消息线程里等推理结果
 *   - 没有模型时自动回退到本地关键词引擎，保证"开箱即用"
 */
class AiChatPanel : public juce::Component,
                    private juce::Timer,
                    private juce::TextEditor::Listener
{
public:
    explicit AiChatPanel (KickForgeProcessor& p);
    ~AiChatPanel() override;

    void paint (juce::Graphics& g) override;
    void resized() override;

    /** 用户要求把一段描述变成音色（由 generate 按钮/回车触发）。 */
    std::function<void (const juce::String& prompt, const juce::String& patchJson)> onPatchReady;

    /** 面板需要显示的状态文字。 */
    void setStatus (const juce::String& text);

    /** 把一条助手消息加进记录。 */
    void appendAssistant (const juce::String& text, const juce::String& patchJson = {});

    /** 当前会话（供外部读取历史）。 */
    ChatSession& getSession() noexcept { return session; }

    /** 模型是否已加载（决定用 LLM 还是本地引擎）。 */
    bool isLlmReady() const { return llm.isReady(); }

    /** 尝试加载当前选中的模型。 */
    void loadSelectedModel();

private:
    void timerCallback() override;
    void textEditorReturnKeyPressed (juce::TextEditor&) override;

    void sendCurrentInput();
    void rebuildModelList();
    void showModelMenu();
    void importModel();
    void layoutMessages();

    KickForgeProcessor& proc;

    //--- 对话
    ChatSession session;
    juce::Viewport   messageViewport;
    juce::Component  messageHolder;      // 承载所有气泡
    std::vector<std::unique_ptr<juce::Label>> bubbles;

    //--- 输入
    juce::TextEditor input;
    juce::TextButton sendButton { "SEND" };

    //--- 模型
    juce::TextButton modelButton { "MODEL" };
    juce::TextButton importButton { "IMPORT" };
    juce::ProgressBar progress { progressValue };
    double progressValue = 0.0;

    juce::String statusText;
    juce::String selectedModelId;

    //--- 后端
    LlamaBackend llm;
    ModelManager models;
    LocalPatchEngine fallback;

    //--- 生成状态
    bool waitingForReply = false;
    int  dotsPhase = 0;
    juce::String pendingPrompt;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AiChatPanel)
};

} // namespace kf
