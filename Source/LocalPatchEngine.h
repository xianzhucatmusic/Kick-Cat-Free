#pragma once

#include "AiPatch.h"

namespace kf
{

//==============================================================================
/** 本地语义引擎：把自然语言描述映射成参数。
 *
 *  完全离线，不联网、不需要 API Key。
 *
 *  工作方式（不是简单查表）：
 *   1. **分词**：中英文混合，按关键词表切出"语义标签"
 *   2. **维度评分**：每个标签对若干听感维度加权
 *      （低频长度 / 冲击力 / 明亮度 / 脏度 / 复古度 / 音高……）
 *   3. **风格先验**：识别到 "Trap"、"Techno" 等风格词时，
 *      先套用该风格的基线，再用维度分数做偏移
 *   4. **种子扰动**：同一个描述配不同种子产生微妙变化，
 *      但不会偏离语义太远（扰动幅度随维度置信度衰减）
 *
 *  为什么不做真正的 embedding 模型：
 *  一个轻量文本模型（哪怕 20MB）也要引入 ONNX Runtime，
 *  会让插件体积和构建复杂度大幅上升。关键词+维度的做法在本领域
 *  （底鼓描述）覆盖面已经足够，且完全可预测、可调试。
 */
class LocalPatchEngine : public PatchEngine
{
public:
    LocalPatchEngine() = default;

    juce::String getName() const override { return "Local (offline)"; }

    bool generate (const juce::String& prompt,
                   int seed,
                   AiPatch& out,
                   juce::String& error) override;

    /** 识别出的风格（供 UI 显示"我理解成了什么"）。 */
    const juce::String& getDetectedStyle() const noexcept { return detectedStyle; }

    /** 识别出的听感维度（供 UI 显示）。 */
    const juce::StringArray& getDetectedTags() const noexcept { return detectedTags; }

    /** 全部可用的快捷标签（UI 用）。 */
    static juce::StringArray getQuickTags();

private:
    juce::String detectedStyle;
    juce::StringArray detectedTags;
};

} // namespace kf
