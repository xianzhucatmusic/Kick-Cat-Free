#pragma once

#include "ChatBackend.h"
#include <atomic>
#include <memory>
#include <mutex>

// 前向声明，避免把 llama.h 泄漏到所有包含者
struct llama_model;
struct llama_context;
struct llama_sampler;

namespace kf
{

//==============================================================================
/** 基于 llama.cpp 的本地 LLM 推理后端。
 *
 *  设计要点：
 *
 *  1. **完全离线**：模型是本地 GGUF 文件，不联网。
 *
 *  2. **后台线程推理**：8B 模型生成一次要十几秒，绝不能占用消息线程
 *     （会让整个界面卡死），更不能碰音频线程。所以推理跑在独立线程，
 *     结果通过回调送回。
 *
 *  3. **线程数限制**：默认只用一半物理核心。全部核心跑满会让音频线程
 *     被抢占，产生爆音 —— 这是音频插件里做推理最容易犯的错。
 *
 *  4. **模型常驻**：加载一次常驻内存。卸载由 unload() 显式触发
 *     （例如用户切换模型或关闭面板）。
 *
 *  5. **单次推理**：同一时刻只允许一个请求在跑，重复请求直接拒绝，
 *     避免显存/内存被多个上下文挤爆。
 */
class LlamaBackend : public ChatBackend, private juce::Thread
{
public:
    LlamaBackend();
    ~LlamaBackend() override;

    juce::String getName() const override { return "Local LLM"; }

    bool isReady() const override { return modelLoaded.load(); }

    bool isBusy() const override { return isThreadRunning(); }

    /** 加载模型（阻塞，应该在后台调用）。
     *  @returns 成功与否；失败原因写入 error。 */
    bool loadModel (const juce::File& ggufFile, juce::String& error);

    /** 卸载模型，释放内存。 */
    void unload();

    void send (const juce::String& userText, const ChatSession& history) override;

    void cancel() override;

    /** 推理线程数（默认 = 物理核心的一半，最少 2）。 */
    void setThreadCount (int n);
    int  getThreadCount() const noexcept { return numThreads; }

    /** 当前加载的模型路径（空 = 未加载）。 */
    juce::String getLoadedModelPath() const { return loadedPath; }

    /** 上一次生成用了多少毫秒（UI 显示用）。 */
    int getLastGenerationMs() const noexcept { return lastGenMs.load(); }

private:
    void run() override;

    /** 把历史 + 系统提示词拼成 Qwen 的 ChatML 格式。 */
    juce::String buildChatML (const juce::String& userText, const ChatSession& history) const;

    std::atomic<bool> modelLoaded { false };
    std::atomic<int> lastGenMs { 0 };
    int numThreads = 4;

    juce::String loadedPath;
    juce::String pendingPrompt;

    // llama.cpp 的不透明句柄
    llama_model*   model = nullptr;
    llama_context* ctx   = nullptr;

    std::mutex loadMutex;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (LlamaBackend)
};

} // namespace kf
