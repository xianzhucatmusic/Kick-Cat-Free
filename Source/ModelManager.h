#pragma once

#include <juce_core/juce_core.h>
#include <functional>
#include <vector>

namespace kf
{

//==============================================================================
/** 可用的 LLM 模型清单与本地文件管理。
 *
 *  存储位置：~/Library/Application Support/XianzhuCatMusic/Kick Cat/Models/
 *
 *  为什么模型不编进插件二进制：
 *   1.8GB 的 .pkg 无法分发，而且宿主加载插件时会去读整个文件。
 *  所以模型放在用户目录，插件只负责"检查 / 下载 / 加载"。
 *
 *  分发时可以额外打一个"含模型"的大安装包，把文件预置到这个目录，
 *  插件检测到文件已存在就直接用，不走下载。
 */
class ModelManager : private juce::Thread
{
public:
    //==========================================================================
    /** 一个可用模型。 */
    struct ModelInfo
    {
        juce::String id;            // 稳定标识（存档用，不要改）
        juce::String displayName;   // 显示名
        juce::String fileName;      // 本地文件名
        juce::String url;           // 下载地址（HuggingFace）
        juce::int64  sizeBytes = 0; // 期望大小（校验用）
        juce::String description;   // 说明
        int          minRamGb = 0;  // 建议内存
    };

    //==========================================================================
    ModelManager();
    ~ModelManager() override;

    /** 全部可选模型（内置清单）。 */
    static const std::vector<ModelInfo>& getAvailableModels();

    /** 默认模型 id（3B）。 */
    static juce::String getDefaultModelId();

    /** 模型目录。 */
    static juce::File getModelDirectory();

    /** 某个模型在本地的路径（可能不存在）。 */
    static juce::File getModelFile (const juce::String& modelId);

    /** 本地是否已有该模型（含大小校验）。 */
    static bool isModelDownloaded (const juce::String& modelId);

    /** 按 id 找模型信息；找不到返回 nullptr。 */
    static const ModelInfo* findModel (const juce::String& modelId);

    //==========================================================================
    // 用户自备模型
    //
    // 安装包不含模型（1.8GB 无法分发），所以有两条获取路径：
    //   1. 面板里点"下载"（内置清单里的 1.5B / 3B / 8B）
    //   2. 用户把自己下的 .gguf 文件复制到 Models 目录
    //
    // 这里负责把 (2) 扫描出来，让界面上也能选。
    //==========================================================================

    /** 扫描 Models 目录里的全部 .gguf（含内置清单之外的）。
     *  用户导入的模型 id 形如 "user:<文件名>"。 */
    static std::vector<ModelInfo> scanAllModels();

    /** 把外部 .gguf 复制进 Models 目录。
     *  @returns 成功返回 true；失败写入 error。 */
    static bool importModelFile (const juce::File& source, juce::String& error);

    /** 判断一个 id 是否是用户导入的。 */
    static bool isUserModel (const juce::String& modelId);

    /** 用户模型 id -> 文件路径。 */
    static juce::File resolveModelFile (const juce::String& modelId);

    //==========================================================================
    /** 开始下载模型（后台线程）。
     *  已存在则直接回调 onFinished(true)。 */
    void downloadModel (const juce::String& modelId);

    /** 取消正在进行的下载。 */
    void cancelDownload();

    /** 删除已下载的模型（释放空间）。 */
    static bool deleteModel (const juce::String& modelId);

    //==========================================================================
    /** 下载进度 0..1（UI 用）。 */
    float getDownloadProgress() const noexcept { return downloadProgress.load(); }

    /** 是否正在下载。 */
    bool isDownloading() const noexcept { return isThreadRunning(); }

    /** 当前下载的模型 id。 */
    juce::String getDownloadingModelId() const { return downloadingId; }

    /** 状态文字（"正在下载…" / 错误信息）。 */
    juce::String getStatusText() const { return statusText; }

    /** 进度回调（从后台线程调用，UI 需自行 post 到消息线程）。 */
    std::function<void()> onProgress;
    std::function<void (bool success, const juce::String& message)> onFinished;

private:
    void run() override;

    juce::String downloadingId;
    juce::String statusText;
    std::atomic<float> downloadProgress { 0.0f };
    std::atomic<bool> shouldStop { false };
    int httpStatus = 0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModelManager)
};

} // namespace kf
