#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>
#include <vector>

namespace kf
{

//==============================================================================
/** AI 生成音色的参数模板（JSON 契约）。
 *
 *  这是"描述词 -> 参数"的唯一中间格式：
 *     自然语言  ->  [ AiPatch ]  ->  插件参数
 *
 *  为什么要有这么一层：
 *   - 描述词与插件参数不是一一对应（"冲击力"同时影响 3~4 个参数），
 *     中间格式让映射逻辑集中在一处，而不是散落在 UI 里
 *   - 将来若要接云端模型，只要让它输出同样的 JSON 即可，
 *     下面的 apply 逻辑完全不用改
 *
 *  单位约定（重要）：
 *   - 音高用 **MIDI 音符号**（不是 Hz），因为用户与模型都更容易理解
 *     "60 是中央 C"；应用到 sub 的 Hz 参数时统一转换
 *   - 时间用 **秒**（不是毫秒）
 *   - 电平用 0..1
 */
struct AiPatch
{
    //--- 音高包络
    float startPitch = 48.0f;      // 起始音高（MIDI 音符）
    float endPitch   = 33.0f;      // 结束音高（MIDI 音符）
    float pitchDecay = 0.05f;      // 音高下滑时间（秒）
    float pitchCurve = 0.5f;       // 0 = 慢起快落，1 = 快起慢落

    //--- 幅度包络
    float ampDecay = 0.5f;         // 幅度衰减时间（秒）
    float ampCurve = 0.5f;         // 0 = 更"持"，1 = 更"点"

    //--- 各层电平 0..1
    float subLevel   = 0.45f;
    float clickLevel = 0.30f;
    float noiseLevel = 0.0f;

    //--- 音色
    float clickPitch  = 1500.0f;   // 音头中心频率（Hz）
    float saturation  = 0.0f;      // 饱和量 0..1
    int   colourModel = 0;         // 染色模型序号（见 DistortionType）
    float tone        = 0.5f;      // 明暗 0..1
    float bias        = 0.0f;      // 偶次谐波

    //--- 其他
    float filterHz  = 20000.0f;    // 低通
    float width     = 0.0f;        // 立体声宽度
    float punch     = 0.5f;        // 音头冲击力 0..1

    /** 解析来自模型/引擎的 JSON。
     *  缺失字段保持默认值（而不是清零）—— 这样模型少给几个字段也能用。 */
    static AiPatch fromJson (const juce::String& json, bool* ok = nullptr);

    /** 序列化为 JSON（用于日志、调试、历史记录）。 */
    juce::String toJson() const;

    /** 是否所有关键字段都在合理范围内。 */
    bool isSane() const;

    /** 把越界值夹回合法范围。 */
    void sanitise();
};

//==============================================================================
/** 把描述词解析成 AiPatch 的接口。
 *
 *  目前只有本地实现（LocalPatchEngine），但保留这个抽象是为了
 *  将来接入云端模型时不用改上层代码。
 */
class PatchEngine
{
public:
    virtual ~PatchEngine() = default;

    /** 引擎名称（显示在 UI 上，让用户知道用的是哪个）。 */
    virtual juce::String getName() const = 0;

    /** 是否可用（本地引擎恒为 true；云端引擎可能缺少 Key）。 */
    virtual bool isAvailable() const { return true; }

    /** 把自然语言解析成参数补丁。
     *  @param prompt  用户输入
     *  @param seed    随机种子 —— 同一个描述配不同种子会得到微妙不同的结果
     *  @param error   失败时写入原因
     *  @returns 是否成功 */
    virtual bool generate (const juce::String& prompt,
                           int seed,
                           AiPatch& out,
                           juce::String& error) = 0;
};

} // namespace kf
