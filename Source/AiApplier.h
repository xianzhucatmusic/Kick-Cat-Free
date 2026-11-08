#pragma once

#include "AiPatch.h"
#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>

namespace kf
{

class KickForgeProcessor;

//==============================================================================
/** 把 AiPatch 平滑地应用到插件参数上。
 *
 *  为什么要"平滑"而不是直接跳变：
 *  参数直接跳变会让滤波器系数、包络、失真工作点瞬间改变，
 *  产生咔嗒声（尤其是 filter / gain / mix 这类）。
 *  这里的做法是在**消息线程定时器**里分若干帧插值到目标值，
 *  每帧间隔 ~16ms，整体 ~200ms 完成。
 *
 *  注意：这里改的是"用户看到的参数值"，不是音频线程的系数平滑。
 *  两者配合：参数渐变 + 各 DSP 自身的平滑共同避免咔嗒。
 */
class AiApplier : private juce::Timer
{
public:
    explicit AiApplier (KickForgeProcessor& p);
    ~AiApplier() override;

    /** 把补丁应用到参数（带平滑过渡）。
     *  @param patch  目标参数
     *  @param animate  true = 分帧过渡；false = 立即生效（用于测试/无 UI 场景） */
    void apply (const AiPatch& patch, bool animate = true);

    /** 是否正在过渡中。 */
    bool isAnimating() const noexcept { return animating; }

    /** 过渡进度 0..1（UI 进度条用）。 */
    float getProgress() const noexcept;

    /** 本次应用改动的参数 ID 列表（UI 用来高亮）。 */
    const juce::StringArray& getChangedParams() const noexcept { return changedParams; }

    /** 取消正在进行的过渡（例如用户手动拧了旋钮）。 */
    void cancel();

    /** 过渡结束回调（UI 用来刷新）。 */
    std::function<void()> onFinished;

    /** 供测试：立即把所有待应用值写完（不等定时器）。 */
    void finishImmediately();

private:
    void timerCallback() override;

    struct Target
    {
        juce::String paramID;
        float fromNormalised = 0.0f;
        float toNormalised   = 0.0f;
    };

    void writeFrame (float t);

    KickForgeProcessor& proc;

    std::vector<Target> targets;
    juce::StringArray changedParams;

    bool animating = false;
    int  frame = 0;
    static constexpr int kFrames = 12;      // 12 帧 x 16ms ≈ 200ms

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (AiApplier)
};

} // namespace kf
