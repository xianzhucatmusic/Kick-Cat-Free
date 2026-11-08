#include "LlamaBackend.h"
#include "AiPrompts.h"

// 工具目标（自测程序）不链接 llama.cpp，用一个空实现占位，
// 这样它们仍能编译整个 Source/ 目录而不会因为找不到 llama.h 失败。
#ifndef KF_LLM_COMPILED

namespace kf
{
LlamaBackend::LlamaBackend() : juce::Thread ("KickCatLLM") {}
LlamaBackend::~LlamaBackend() { stopThread (1000); }
void LlamaBackend::setThreadCount (int) {}
bool LlamaBackend::loadModel (const juce::File&, juce::String& e)
{
    e = "本构建未启用本地 LLM（KF_HAS_LLM 未定义）";
    return false;
}
void LlamaBackend::unload() {}
void LlamaBackend::send (const juce::String&, const ChatSession&)
{
    if (onReply) onReply ({}, {}, "本构建未启用本地 LLM");
}
void LlamaBackend::cancel() {}
void LlamaBackend::run() {}
} // namespace kf

#else

#include <llama.h>
#include <cmath>
#include <cstdio>
#include <thread>

namespace kf
{

//==============================================================================
// llama.cpp 初始化（全进程一次）
//==============================================================================
namespace
{
    std::once_flag gBackendInitFlag;

    /** 默认的回调会把大量 Graph/张量信息打到 stderr。
     *  插件在宿主里运行，这些噪音会污染宿主的日志窗口，
     *  而且日志本身有开销（几千行格式化字符串）。
     *
     *  只保留**错误**级别，并且过滤掉模型加载时的进度点。
     *
     *  为什么单独过滤进度点：llama.cpp 在加载 GGUF 时会把
     *  "." 逐字节打到 INFO 级别，一次加载产生几百行
     *  "[llama] ." —— 实测 639 字节纯噪音。它在 INFO 级别，
     *  按理已被 level 判断挡掉，但某些版本会走 WARN 通道。
     *  这里做一次内容判断，双保险。 */
    void quietLogCallback (ggml_log_level level, const char* text, void*)
    {
        if (text == nullptr)
            return;

        if (level < GGML_LOG_LEVEL_ERROR)
            return;

        // 过滤纯进度点（"." / ".." / 带换行的点）
        bool onlyDots = true;
        for (const char* p = text; *p != 0; ++p)
        {
            if (*p != '.' && *p != '\n' && *p != '\r' && *p != ' ')
                { onlyDots = false; break; }
        }
        if (onlyDots)
            return;

        std::fprintf (stderr, "[llama] %s", text);
    }

    void initLlamaBackendOnce()
    {
        std::call_once (gBackendInitFlag, []
        {
            llama_log_set (quietLogCallback, nullptr);
            llama_backend_init();
        });
    }

    /** 依据物理核心决定推理线程数。
     *
     *  为什么不用全部核心：
     *  音频线程需要稳定的 CPU 时间片。推理把所有核心占满会导致
     *  音频缓冲欠载 -> 爆音。留一半是保守但安全的选择。 */
    int defaultThreadCount()
    {
        const auto hw = std::thread::hardware_concurrency();
        return juce::jlimit (2, 8, (int) (hw / 2));
    }
} // anonymous namespace

//==============================================================================
LlamaBackend::LlamaBackend()
    : juce::Thread ("KickCatLLM")
{
    initLlamaBackendOnce();
    numThreads = defaultThreadCount();
}

LlamaBackend::~LlamaBackend()
{
    stopThread (5000);
    unload();
}

void LlamaBackend::setThreadCount (int n)
{
    numThreads = juce::jlimit (1, 32, n);
}

//==============================================================================
bool LlamaBackend::loadModel (const juce::File& ggufFile, juce::String& error)
{
    std::lock_guard<std::mutex> lock (loadMutex);

    if (! ggufFile.existsAsFile())
    {
        error = "模型文件不存在：" + ggufFile.getFullPathName();
        return false;
    }

    // 已经加载同一个模型就直接返回（避免重复占用内存）
    if (modelLoaded && loadedPath == ggufFile.getFullPathName())
        return true;

    unload();

    auto mparams = llama_model_default_params();

    // 用 mmap 而不是 mlock：不把整个模型锁进物理内存，
    // 留给音频线程与系统更多余量（mlock 会让 4.6GB 的 8B 模型
    // 一直占用物理页，在宿主里挂多个实例时很快就耗尽内存）。
    mparams.load_mode = LLAMA_LOAD_MODE_MMAP;

    model = llama_model_load_from_file (ggufFile.getFullPathName().toRawUTF8(), mparams);

    if (model == nullptr)
    {
        error = "模型加载失败（文件可能损坏或格式不支持）";
        return false;
    }

    auto cparams = llama_context_default_params();
    // 上下文长度：系统提示词约 800 token + 几轮对话，2048 够用且省内存
    cparams.n_ctx = 2048;
    cparams.n_threads = numThreads;
    cparams.n_threads_batch = numThreads;

    ctx = llama_init_from_model (model, cparams);

    if (ctx == nullptr)
    {
        llama_model_free (model);
        model = nullptr;
        error = "上下文创建失败（内存不足？）";
        return false;
    }

    loadedPath = ggufFile.getFullPathName();
    modelLoaded = true;
    return true;
}

void LlamaBackend::unload()
{
    if (ctx != nullptr)
    {
        llama_free (ctx);
        ctx = nullptr;
    }

    if (model != nullptr)
    {
        llama_model_free (model);
        model = nullptr;
    }

    modelLoaded = false;
    loadedPath.clear();
}

//==============================================================================
juce::String LlamaBackend::buildChatML (const juce::String& userText,
                                        const ChatSession& history) const
{
    // Qwen2.5 用 ChatML 格式。手工拼而不用 llama_chat_apply_template，
    // 是为了避免模板名在不同 llama.cpp 版本间不一致导致失败。
    juce::String out;

    auto sys = prompts::buildSystemPrompt();

    out << "<|im_start|>system\n" << sys << "<|im_end|>\n";

    // 历史（已由 ChatSession 裁剪过轮数）
    for (const auto& m : history.getRecentContext (4))
    {
        if (m.role == ChatMessage::Role::User)
            out << "<|im_start|>user\n" << m.text << "<|im_end|>\n";
        else
            out << "<|im_start|>assistant\n" << m.text << "<|im_end|>\n";
    }

    out << "<|im_start|>user\n" << userText << "<|im_end|>\n";
    out << "<|im_start|>assistant\n";

    return out;
}

//==============================================================================
void LlamaBackend::send (const juce::String& userText, const ChatSession& history)
{
    if (! modelLoaded.load())
    {
        if (onReply)
            onReply ({}, {}, "模型未加载");
        return;
    }

    // 同一时刻只允许一个请求
    if (isThreadRunning())
    {
        if (onReply)
            onReply ({}, {}, "上一个请求还在进行中");
        return;
    }

    pendingPrompt = buildChatML (userText, history);
    startThread();
}

void LlamaBackend::cancel()
{
    signalThreadShouldExit();
}

//==============================================================================
void LlamaBackend::run()
{
    const auto startMs = juce::Time::getMillisecondCounterHiRes();

    auto* vocab = llama_model_get_vocab (model);

    //--- 1. 分词
    const auto promptUtf8 = pendingPrompt.toRawUTF8();
    const int promptLen = (int) std::strlen (promptUtf8);

    // 先问需要多少 token，再分配
    int nTokens = -llama_tokenize (vocab, promptUtf8, promptLen,
                                   nullptr, 0, true, true);

    if (nTokens <= 0)
    {
        if (onReply) onReply ({}, {}, "提示词分词失败");
        return;
    }

    // 上下文保护：太长就截断（保留尾部，因为最新的指令在最后）
    const int nCtx = (int) llama_n_ctx (ctx);
    const int maxPrompt = nCtx - 256;      // 留出生成空间

    std::vector<llama_token> tokens ((size_t) nTokens);
    const int got = llama_tokenize (vocab, promptUtf8, promptLen,
                                    tokens.data(), nTokens, true, true);

    if (got <= 0)
    {
        if (onReply) onReply ({}, {}, "提示词分词失败");
        return;
    }

    tokens.resize ((size_t) got);

    if ((int) tokens.size() > maxPrompt)
    {
        // 从头砍掉多余部分（系统提示词的尾部与最新对话保留）
        const int drop = (int) tokens.size() - maxPrompt;
        tokens.erase (tokens.begin(), tokens.begin() + drop);
    }

    //--- 2. 清空 KV cache，重新喂入
    llama_memory_clear (llama_get_memory (ctx), true);

    const int nBatch = 512;

    for (int i = 0; i < (int) tokens.size(); i += nBatch)
    {
        if (threadShouldExit())
        {
            if (onReply) onReply ({}, {}, "已取消");
            return;
        }

        const int n = juce::jmin (nBatch, (int) tokens.size() - i);
        auto batch = llama_batch_get_one (tokens.data() + i, n);

        if (llama_decode (ctx, batch) != 0)
        {
            if (onReply) onReply ({}, {}, "推理失败（提示词过长？）");
            return;
        }
    }

    //--- 3. 采样器
    // 温度低一点：我们要的是稳定的 JSON，不是创意写作
    auto sparams = llama_sampler_chain_default_params();
    auto* smpl = llama_sampler_chain_init (sparams);
    llama_sampler_chain_add (smpl, llama_sampler_init_top_k (40));
    llama_sampler_chain_add (smpl, llama_sampler_init_top_p (0.95f, 1));
    llama_sampler_chain_add (smpl, llama_sampler_init_temp (0.25f));
    llama_sampler_chain_add (smpl, llama_sampler_init_dist (LLAMA_DEFAULT_SEED));

    //--- 4. 逐 token 生成
    juce::String output;
    const int maxGen = 220;          // JSON 约 150-200 token

    for (int i = 0; i < maxGen; ++i)
    {
        if (threadShouldExit())
        {
            llama_sampler_free (smpl);
            if (onReply) onReply ({}, {}, "已取消");
            return;
        }

        const llama_token id = llama_sampler_sample (smpl, ctx, -1);

        if (llama_vocab_is_eog (vocab, id))
            break;

        char buf[256];
        const int n = llama_token_to_piece (vocab, id, buf, sizeof (buf), 0, true);

        if (n > 0)
        {
            juce::String piece = juce::String::fromUTF8 (buf, n);
            output << piece;

            if (onPartial)
                onPartial (piece);
        }

        // 把刚生成的 token 喂回去，继续下一轮
        auto batch = llama_batch_get_one (const_cast<llama_token*> (&id), 1);

        if (llama_decode (ctx, batch) != 0)
            break;

        // JSON 已经闭合就提前停 —— 省时间，也避免模型画蛇添足
        const auto trimmed = output.trimEnd();
        if (trimmed.endsWith ("}") && trimmed.containsChar ('{'))
            break;
    }

    llama_sampler_free (smpl);

    lastGenMs = (int) (juce::Time::getMillisecondCounterHiRes() - startMs);

    //--- 5. 抽取 JSON
    const auto json = prompts::extractJson (output);

    bool jsonOk = false;
    AiPatch::fromJson (json, &jsonOk);

    if (! jsonOk)
    {
        if (onReply)
            onReply (output, {}, "模型没有返回有效 JSON");
        return;
    }

    if (onReply)
        onReply (output, json, {});
}

} // namespace kf

#endif // KF_LLM_COMPILED
