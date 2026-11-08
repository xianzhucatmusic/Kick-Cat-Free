#include "ChatBackend.h"
#include "AiPrompts.h"

namespace kf
{

//==============================================================================
void ChatSession::addUser (const juce::String& text)
{
    if (text.trim().isEmpty())
        return;

    ChatMessage m;
    m.role = ChatMessage::Role::User;
    m.text = text.trim();
    messages.push_back (std::move (m));

    // 限长：超出后丢掉最旧的（保留最近 kMaxMessages 条）
    if ((int) messages.size() > kMaxMessages)
        messages.erase (messages.begin(), messages.begin() + (messages.size() - kMaxMessages));
}

void ChatSession::addAssistant (const juce::String& text, const juce::String& patchJson)
{
    ChatMessage m;
    m.role = ChatMessage::Role::Assistant;
    m.text = text;
    m.patchJson = patchJson;
    messages.push_back (std::move (m));

    if ((int) messages.size() > kMaxMessages)
        messages.erase (messages.begin(), messages.begin() + (messages.size() - kMaxMessages));
}

std::vector<ChatMessage> ChatSession::getRecentContext (int maxTurns) const
{
    if (maxTurns <= 0 || messages.empty())
        return {};

    // 一轮 = 一条 user + 一条 assistant。
    // 从后往前数，找到第 maxTurns 条 user 消息，从**它**开始取。
    //
    // 注意不能"数到第 maxTurns+1 条再取 i+1" —— 那样会把上一轮的
    // assistant 回复也带进来，多出一条（实测 3 轮拿到 7 条而不是 6 条）。
    int userSeen = 0;
    size_t start = 0;

    for (size_t i = messages.size(); i-- > 0;)
    {
        if (messages[i].role != ChatMessage::Role::User)
            continue;

        if (++userSeen == maxTurns)
        {
            start = i;
            break;
        }
    }

    return std::vector<ChatMessage> (messages.begin() + (long) start, messages.end());
}

juce::String ChatSession::buildPrompt (const juce::String& systemPrompt, int maxTurns) const
{
    juce::String out;

    out << systemPrompt << "\n\n";

    for (const auto& m : getRecentContext (maxTurns))
    {
        if (m.role == ChatMessage::Role::User)
            out << juce::String::fromUTF8 ("用户：") << m.text << "\n";
        else
            out << juce::String::fromUTF8 ("输出：") << m.text << "\n";
    }

    out << juce::String::fromUTF8 ("输出：");
    return out;
}

} // namespace kf
