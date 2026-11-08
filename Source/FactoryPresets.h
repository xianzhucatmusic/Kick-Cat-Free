#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace kf
{

//==============================================================================
/** 工厂预设的程序化生成器。
 *
 *  为什么不用手写：
 *  需求是 10+ 种风格 × 每种 40+ 个预设 = 400+ 个。
 *  手写 400 份参数表既不可能维护，也没法保证它们**真的互不相同**。
 *
 *  做法：
 *   - 每种风格定义一组"音色轴"（sub 时长 / 音高量 / 泛音结构 / 滤波 / click …）
 *   - 在每个轴上取若干档位，做**系统化的笛卡尔组合**
 *   - 用确定性伪随机（固定种子）在组合上再叠加细微偏移，
 *     保证同名预设每次都生成一样的结果（可复现），
 *     但不同的预设之间有可闻的差异
 *
 *  这样 400+ 个预设是"设计出来的变化"，而不是"随机噪声"。
 */
namespace FactoryPresets
{

/** 一个预设的完整描述（供菜单分组用）。 */
struct Entry
{
    juce::String style;      // 风格（一级菜单），如 "Techno"
    juce::String category;   // 子类（二级菜单），如 "Rolling"
    juce::String name;       // 完整名 "Techno - Rolling 03"
};

/** 全部工厂预设的清单（含分组信息）。 */
const std::vector<Entry>& getAll();

/** 生成某个预设的参数树。名字必须是 getAll() 里出现过的。 */
juce::ValueTree create (const juce::String& fullName);

/** 按风格聚合，供层级菜单使用：style -> category -> [索引] */
struct MenuTree
{
    // 保序：用 vector 而不是 map，保证菜单顺序稳定
    struct Category { juce::String name; std::vector<int> presetIndices; };
    struct Style    { juce::String name; std::vector<Category> categories; };

    std::vector<Style> styles;

    int totalPresets() const
    {
        int n = 0;
        for (auto& s : styles)
            for (auto& c : s.categories)
                n += (int) c.presetIndices.size();
        return n;
    }
};

/** 构建层级菜单树（索引对应 getAll() 的下标）。 */
MenuTree buildMenuTree();

} // namespace FactoryPresets
} // namespace kf
