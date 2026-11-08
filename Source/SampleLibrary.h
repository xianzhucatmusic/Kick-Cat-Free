#pragma once

#include <juce_audio_formats/juce_audio_formats.h>
#include "ClickLayer.h"

namespace kf
{

//==============================================================================
/** 内置 Click 采样库（180 个，全部由 DSP 程序生成）。
 *
 *  为什么生成而不是打包 wav：
 *   - 无需外部素材、无版权风险，仓库体积小（生成代码 vs 数 MB 音频）
 *   - 完全可复现，调参即可重新生成整个库
 *
 *  6 个类别 × 30 个变体 = 180：
 *   - Click     : 极短的正弦/余弦爆发，干净的点击
 *   - Tick      : 更硬、更窄的脉冲，带一点噪声
 *   - Noise     : 带通噪声爆发，做"沙"感的瞬态
 *   - Transient : 打击感瞬态（噪声+正弦混合，极快衰减）
 *   - Beater    : 模拟鼓槌/踏板撞击，中频突出、有木质共鸣
 *   - Digital   : 数字感短音（FM / 位元噪声），现代电子风格
 */
class SampleLibrary
{
public:
    static constexpr int kNumCategories = 6;
    static constexpr int kVariantsPerCategory = 30;
    static constexpr int kTotalSamples = kNumCategories * kVariantsPerCategory;  // 180

    struct Entry
    {
        juce::String name;
        int category = 0;      // 0..kNumCategories-1
        int variant = 0;       // 0..kVariantsPerCategory-1
    };

    SampleLibrary();

    /** 生成整库到内存缓存。首次访问某个采样时才做渲染（懒加载）。 */
    void initialise (double sampleRate);

    int getNumSamples() const noexcept { return kTotalSamples; }

    const Entry& getEntry (int index) const noexcept
    {
        return entries[(size_t) juce::jlimit (0, kTotalSamples - 1, index)];
    }

    static juce::String getCategoryName (int category);

    /** 取得某个内置采样的音频（生成并缓存）。
     *  @returns 采样数据指针，生命周期由本库管理；失败返回 nullptr。 */
    const juce::AudioBuffer<float>* getSampleBuffer (int index);

    /** 把某个内置采样载入到 Click 层。 */
    bool loadIntoLayer (int index, ClickLayer& layer);

    /** 重新设置生成用的采样率（工程采样率变化时调用）。 */
    void setSampleRate (double sampleRate);

private:
    void buildEntries();
    juce::AudioBuffer<float> renderSample (int category, int variant) const;

    std::vector<Entry> entries;
    std::vector<std::unique_ptr<juce::AudioBuffer<float>>> cache;
    double sampleRate = 44100.0;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SampleLibrary)
};

} // namespace kf
