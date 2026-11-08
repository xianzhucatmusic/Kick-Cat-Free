#pragma once

#include <juce_core/juce_core.h>
#include <vector>

namespace kf
{

//==============================================================================
/** 全局配置（与预设无关，跨工程保留）。
 *
 *  存在 ~/Library/Application Support/XianzhuCatMusic/Kick Cat/ai_config.json
 *  而不是工程里 —— 因为这些是"用户偏好"，不是音色的一部分。
 */
class AiConfig
{
public:
    AiConfig();

    /** 最近用过的描述词（最多 10 条，最新的在前）。 */
    std::vector<juce::String> promptHistory;

    /** 使用的引擎：0 = 本地（当前唯一选项）。
     *  保留这个字段是为了将来接入云端模型时不必改存档格式。 */
    int modelIndex = 0;

    /** 是否自动在生成后触发试听。 */
    bool autoAudition = true;

    /** 添加一条描述到历史（自动去重、限长）。 */
    void addPrompt (const juce::String& prompt);

    /** 读取 / 保存。 */
    void load();
    void save() const;

    static juce::File getConfigFile();

    static constexpr int kMaxHistory = 10;
};

} // namespace kf
