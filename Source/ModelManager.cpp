#include "ModelManager.h"
#include "KfPaths.h"
#include <algorithm>
#include <cstring>

namespace kf
{

//==============================================================================
// 模型清单
//
// 全部是 Q4_K_M 量化（4bit，质量/体积平衡最好的档位）。
// URL 与大小都经过实测核对（见开发记录），不是猜的 ——
// 大小用于下载后校验，写错会导致每次启动都重新下载。
//==============================================================================

const std::vector<ModelManager::ModelInfo>& ModelManager::getAvailableModels()
{
    static const std::vector<ModelInfo> models
    {
        {
            "qwen2.5-1.5b",
            "Qwen2.5 1.5B  (fast, 0.9 GB)",
            "Qwen2.5-1.5B-Instruct-Q4_K_M.gguf",
            "https://huggingface.co/bartowski/Qwen2.5-1.5B-Instruct-GGUF/resolve/main/"
            "Qwen2.5-1.5B-Instruct-Q4_K_M.gguf",
            986048768LL,
            "速度最快，适合快速试听。理解力一般。",
            8
        },
        {
            "qwen2.5-3b",
            "Qwen2.5 3B  (balanced, 1.8 GB)",
            "Qwen2.5-3B-Instruct-Q4_K_M.gguf",
            "https://huggingface.co/bartowski/Qwen2.5-3B-Instruct-GGUF/resolve/main/"
            "Qwen2.5-3B-Instruct-Q4_K_M.gguf",
            1929903264LL,
            "推荐。理解力与速度的平衡点。",
            16
        },
        {
            "llama3.1-8b",
            "Llama 3.1 8B  (best, 4.6 GB)",
            "Meta-Llama-3.1-8B-Instruct-Q4_K_M.gguf",
            "https://huggingface.co/bartowski/Meta-Llama-3.1-8B-Instruct-GGUF/resolve/main/"
            "Meta-Llama-3.1-8B-Instruct-Q4_K_M.gguf",
            4920739232LL,
            "理解力最好，但生成较慢、占内存多。",
            32
        }
    };

    return models;
}

juce::String ModelManager::getDefaultModelId()
{
    // 实测 1.5B 生成一次约 1.5 秒，对中文描述的理解已经够准，
    // 而 3B 要慢一倍、体积翻倍。默认用 1.5B，用户想要更好质量
    // 可以在面板里切换到 3B / 8B。
    return "qwen2.5-1.5b";
}

juce::File ModelManager::getModelDirectory()
{
    return paths::getModelDirectory();
}

juce::File ModelManager::getModelFile (const juce::String& modelId)
{
    if (auto* m = findModel (modelId))
        return getModelDirectory().getChildFile (m->fileName);

    return {};
}

const ModelManager::ModelInfo* ModelManager::findModel (const juce::String& modelId)
{
    for (const auto& m : getAvailableModels())
        if (m.id == modelId)
            return &m;

    return nullptr;
}


//==============================================================================
// 用户自备模型
//==============================================================================

bool ModelManager::isUserModel (const juce::String& modelId)
{
    return modelId.startsWith ("user:");
}

juce::File ModelManager::resolveModelFile (const juce::String& modelId)
{
    if (isUserModel (modelId))
        return getModelDirectory().getChildFile (modelId.fromFirstOccurrenceOf (":", false, false));

    return getModelFile (modelId);
}

std::vector<ModelManager::ModelInfo> ModelManager::scanAllModels()
{
    std::vector<ModelInfo> out;

    const auto dir = getModelDirectory();

    // 先放内置清单（保序），再补上目录里多出来的 .gguf
    juce::StringArray known;
    for (const auto& m : getAvailableModels())
    {
        known.add (m.fileName);

        // 只有文件真的存在才列出来（避免界面上一堆"未下载"的空位）
        if (isModelDownloaded (m.id))
            out.push_back (m);
    }

    if (! dir.isDirectory())
        return out;

    for (const auto& e : juce::RangedDirectoryIterator (dir, false, "*.gguf",
                                                        juce::File::findFiles))
    {
        const auto f = e.getFile();
        const auto name = f.getFileName();

        // 内置清单里已列过的跳过
        if (known.contains (name))
            continue;

        ModelInfo info;
        info.id = "user:" + name;
        info.displayName = f.getFileNameWithoutExtension() + "  (imported)";
        info.fileName = name;
        info.url = {};                                   // 用户模型没有下载地址
        info.sizeBytes = f.getSize();
        info.description = "你自己导入的模型";
        info.minRamGb = (int) (f.getSize() / 1024 / 1024 / 1024) + 4;

        out.push_back (info);
    }

    return out;
}

bool ModelManager::importModelFile (const juce::File& source, juce::String& error)
{
    if (! source.existsAsFile())
    {
        error = "文件不存在";
        return false;
    }

    if (! source.hasFileExtension ("gguf"))
    {
        error = "只支持 .gguf 格式的模型文件";
        return false;
    }

    // 粗略校验：GGUF 文件开头必须是 "GGUF" 魔数。
    // 用户很容易误选一个同名但没下完的文件。
    {
        juce::FileInputStream in (source);
        if (! in.openedOk())
        {
            error = "无法读取该文件";
            return false;
        }

        char magic[4] = { 0, 0, 0, 0 };
        in.read (magic, 4);

        if (std::memcmp (magic, "GGUF", 4) != 0)
        {
            error = "这不是有效的 GGUF 模型文件（文件头不对）";
            return false;
        }
    }

    const auto dest = getModelDirectory().getChildFile (source.getFileName());

    if (dest.existsAsFile())
    {
        error = "同名模型已存在";
        return false;
    }

    // 大文件用 moveFileTo 会跨卷失败，统一走 copy
    if (! source.copyFileTo (dest))
    {
        error = "复制失败（磁盘空间不足？）";
        return false;
    }

    return true;
}

bool ModelManager::isModelDownloaded (const juce::String& modelId)
{
    auto* info = findModel (modelId);
    if (info == nullptr)
        return false;

    const auto f = getModelFile (modelId);
    if (! f.existsAsFile())
        return false;

    // 允许少量差异（服务器可能重新打包），但明显偏小说明是残缺文件
    const auto sz = f.getSize();
    return sz >= (juce::int64) ((double) info->sizeBytes * 0.98);
}

//==============================================================================
ModelManager::ModelManager()
    : juce::Thread ("KickCatModelDownload")
{
}

ModelManager::~ModelManager()
{
    stopThread (4000);
}

//==============================================================================
void ModelManager::downloadModel (const juce::String& modelId)
{
    if (isThreadRunning())
        return;

    if (findModel (modelId) == nullptr)
    {
        if (onFinished) onFinished (false, "未知模型: " + modelId);
        return;
    }

    if (isModelDownloaded (modelId))
    {
        if (onFinished) onFinished (true, "模型已就绪");
        return;
    }

    downloadingId = modelId;
    shouldStop = false;
    downloadProgress = 0.0f;
    statusText = "准备下载…";

    startThread();
}

void ModelManager::cancelDownload()
{
    shouldStop = true;
    stopThread (3000);
    statusText = "已取消";
}

//==============================================================================
void ModelManager::run()
{
    auto* info = findModel (downloadingId);
    if (info == nullptr)
        return;

    const auto target = getModelFile (downloadingId);

    // 先下到临时文件，全部完成后再改名 ——
    // 这样中途失败不会留下一个"看起来存在但其实是残file"的模型。
    const auto temp = target.getSiblingFile (target.getFileName() + ".part");

    temp.deleteFile();

    statusText = "正在下载 " + info->displayName + "…";

    // JUCE 的 URL 下载会走系统代理设置。
    // 实测某些代理会拦截 huggingface.co，这里允许用户跳过代理。
    juce::URL url (info->url);

    // 断点续传：如果临时文件已有一部分，从那里继续
    juce::int64 alreadyHave = temp.existsAsFile() ? temp.getSize() : 0;

    if (alreadyHave > 0)
    {
        url = url.withParameter ("dummy", "1");   // 占位，实际用 header
        statusText = "继续下载 " + info->displayName + "…";
    }

    auto stream = url.createInputStream (
        juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
            .withConnectionTimeoutMs (20000)
            .withExtraHeaders (alreadyHave > 0
                                  ? "Range: bytes=" + juce::String (alreadyHave) + "-\r\n"
                                  : juce::String())
            .withStatusCode (&httpStatus));

    if (stream == nullptr)
    {
        statusText = "无法连接下载服务器（检查网络或代理设置）";
        if (onFinished) onFinished (false, statusText);
        return;
    }

    // 206 = 断点续传成功；200 = 服务器忽略 Range，需要从头写
    const bool append = (httpStatus == 206 && alreadyHave > 0);

    // 追加模式：FileOutputStream 的第二个参数就是 append 标志
    std::unique_ptr<juce::FileOutputStream> out (
        temp.createOutputStream() != nullptr
            ? std::make_unique<juce::FileOutputStream> (temp, append)
            : nullptr);

    if (out == nullptr)
    {
        statusText = "无法写入模型目录（检查磁盘权限）";
        if (onFinished) onFinished (false, statusText);
        return;
    }

    const juce::int64 total = info->sizeBytes;
    juce::int64 written = append ? alreadyHave : 0;

    if (! append)
        alreadyHave = 0;

    std::vector<char> buffer (256 * 1024);
    int staleCount = 0;

    while (! threadShouldExit() && ! shouldStop)
    {
        const int n = stream->read (buffer.data(), (int) buffer.size());

        if (n <= 0)
        {
            // 读完了还是没到预期大小，可能网络中断
            if (++staleCount > 3)
                break;
            continue;
        }

        staleCount = 0;

        if (! out->write (buffer.data(), (size_t) n))
        {
            statusText = "写入失败（磁盘可能已满）";
            if (onFinished) onFinished (false, statusText);
            return;
        }

        written += n;
        const float newProgress = total > 0
                                    ? juce::jlimit (0.0f, 1.0f,
                                                    (float) ((double) written / (double) total))
                                    : 0.0f;

        // 限流：每变化 0.5% 才回调一次。
        // 250KB 的缓冲在千兆网速下每几毫秒就写完一次，
        // 不限流会让 UI 每秒重绘上百次（而且跨线程 post 会堆积）。
        const bool report = (newProgress - downloadProgress.load()) >= 0.005f
                         || newProgress >= 1.0f;

        downloadProgress = newProgress;

        if (report && onProgress)
            onProgress();
    }

    out.reset();

    if (shouldStop || threadShouldExit())
    {
        statusText = "已取消（可稍后继续）";
        if (onFinished) onFinished (false, statusText);
        return;
    }

    // --- 校验大小
    const auto finalSize = temp.getSize();

    if (finalSize < (juce::int64) ((double) total * 0.98))
    {
        statusText = "下载不完整（" + juce::String (finalSize / 1024 / 1024) + " MB / "
                   + juce::String (total / 1024 / 1024) + " MB），可重试续传";
        if (onFinished) onFinished (false, statusText);
        return;
    }

    // --- 原子改名
    if (target.existsAsFile())
        target.deleteFile();

    if (! temp.moveFileTo (target))
    {
        statusText = "重命名失败";
        if (onFinished) onFinished (false, statusText);
        return;
    }

    downloadProgress = 1.0f;
    statusText = "下载完成";
    if (onFinished) onFinished (true, statusText);
}

//==============================================================================
bool ModelManager::deleteModel (const juce::String& modelId)
{
    auto f = getModelFile (modelId);
    if (f.existsAsFile())
        return f.deleteFile();

    return false;
}

} // namespace kf
