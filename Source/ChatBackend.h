#pragma once

#include "AiPatch.h"
#include <juce_core/juce_core.h>
#include <functional>
#include <vector>

namespace kf
{

//==============================================================================
/** 聊天消息。 */
struct ChatMessage
{
    enum class Role { User, Assistant, System };

    Role role = Role::User;
    juce::String text;
    juce::String patchJson;      // Assistant 消息可能带一个参数补丁
    bool patchApplied = false;   // 该补丁是否已被用户点"应用"
};

//==============================================================================
/** 对话会话：多轮历史 + 上下文窗口管理。
 *
 *  为什么要单独一个类：
 *   - 上下文长度有限（LLM 通常 4k~32k token），历史必须裁剪
 *   - 裁剪策略会影响"AI 记不记得前面说过什么"
 *   - UI 与推理后端都需要访问同一份历史
 */
class ChatSession
{
public:
    ChatSession() = default;

    void addUser (const juce::String& text);
    void addAssistant (const juce::String& text, const juce::String& patchJson = {});

    const std::vector<ChatMessage>& getMessages() const noexcept { return messages; }

    void clear() { messages.clear(); }

    /** 最近 N 轮（用于构造提示词，避免超出上下文）。
     *  @param maxTurns  保留的对话轮数（一问一答算一轮） */
    std::vector<ChatMessage> getRecentContext (int maxTurns = 6) const;

    /** 拼成给 LLM 的纯文本提示词。 */
    juce::String buildPrompt (const juce::String& systemPrompt,
                              int maxTurns = 6) const;

    bool isEmpty() const noexcept { return messages.empty(); }

    /** 最大保留的历史条数（防止内存无限增长）。 */
    static constexpr int kMaxMessages = 100;

private:
    std::vector<ChatMessage> messages;
};

//==============================================================================
/** 推理后端接口。
 *
 *  目前有两种实现：
 *   - LocalPatchEngine（关键词/维度，毫秒级，无对话能力）
 *   - LlamaBackend（GGUF 模型，真正的对话，秒级）—— 待接入
 *
 *  两者都通过这个接口暴露，UI 不需要关心用的是哪个。
 */
class ChatBackend
{
public:
    virtual ~ChatBackend() = default;

    virtual juce::String getName() const = 0;

    /** 后端是否已就绪（模型已加载）。 */
    virtual bool isReady() const = 0;

    /** 是否正在推理。 */
    virtual bool isBusy() const = 0;

    /** 发起一次推理（异步）。
     *  结果通过 onReply 回调（可能在后台线程）。 */
    virtual void send (const juce::String& userText,
                       const ChatSession& history) = 0;

    /** 取消当前推理。 */
    virtual void cancel() = 0;

    /** 回复回调。
     *  @param replyText  自然语言回复
     *  @param patchJson  参数补丁（空 = 没有补丁）
     *  @param error      出错时的信息（空 = 成功） */
    std::function<void (juce::String replyText,
                        juce::String patchJson,
                        juce::String error)> onReply;

    /** 流式输出的增量回调（可选，用于逐字显示）。 */
    std::function<void (const juce::String& delta)> onPartial;
};

} // namespace kf
