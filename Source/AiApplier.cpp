#include "AiApplier.h"
#include "PluginProcessor.h"
#include "Params.h"
#include <cmath>

namespace kf
{

//==============================================================================
// 参数映射
//
// AiPatch 用的是"音乐单位"（MIDI 音符、秒、0..1），
// 插件参数用的是各自的范围。这里集中做一次转换，
// 避免换算逻辑散落在各处。
//==============================================================================

namespace
{
/** MIDI 音符 -> Hz（A4 = 440Hz = MIDI 69）。 */
inline float midiToHz (float note) noexcept
{
    return 440.0f * std::pow (2.0f, (note - 69.0f) / 12.0f);
}

/** 把参数设为"原始值"（非归一化），内部处理归一化转换。 */
void setParamRaw (juce::AudioProcessorValueTreeState& apvts,
                  const juce::String& id,
                  float rawValue)
{
    if (auto* p = apvts.getParameter (id))
    {
        // 对 AudioParameterChoice，raw 值是选项**索引**，
        // 要先归一化再 setValueNotifyingHost（这是之前踩过的坑）。
        p->setValueNotifyingHost (p->convertTo0to1 (rawValue));
    }
}

/** 读取参数的归一化值（0..1）。 */
float getNormalised (juce::AudioProcessorValueTreeState& apvts, const juce::String& id)
{
    if (auto* p = apvts.getParameter (id))
        return p->getValue();
    return 0.0f;
}

/** 把包络重塑成"起点高、按 curve 下落"的形状。 */
void shapeEnvelope (Envelope& env, float decayShape, float curve, bool pitchEnv)
{
    // decayShape 0..1：0 = 极快落定，1 = 缓慢下落
    // curve       0..1：0 = 先陡后缓（指数），1 = 先缓后陡（对数）

    const float settleT = juce::jlimit (0.05f, 0.85f, 0.12f + decayShape * 0.6f);

    // 出切线：curve 越低，下落越"陡"
    const float outY = juce::jlimit (0.05f, 0.75f, 0.55f - curve * 0.5f);

    // 先用 resetToDefault 得到一个已知的 4 节点形状，
    // 再逐点移动到目标位置（Envelope 没有 clear/setNodes 接口）。
    env.resetToDefault();

    // 节点 0：起点
    env.moveNode (0, 0.0f, pitchEnv ? 1.0f : 0.0f);
    env.setNodeTangent (0, true, 0.12f, pitchEnv ? 0.55f : 0.75f);

    // 节点 1：起音峰值
    env.moveNode (1, pitchEnv ? 0.10f : 0.02f, 1.0f);
    env.setNodeTangent (1, false, 0.5f, 0.0f);
    env.setNodeTangent (1, true, 0.30f, outY);

    // 节点 2：中段（决定"落定"的快慢）
    env.moveNode (2, settleT, pitchEnv ? 0.18f : 0.30f);
    env.setNodeTangent (2, false, 0.33f, 0.28f);
    env.setNodeTangent (2, true, 0.33f, 0.18f);

    // 节点 3：终点
    env.moveNode (3, 1.0f, 0.0f);
    env.setNodeTangent (3, false, 0.45f, 0.35f);

    env.rebuildTable();
}
} // anonymous namespace

//==============================================================================
AiApplier::AiApplier (KickForgeProcessor& p)
    : proc (p)
{
}

AiApplier::~AiApplier()
{
    stopTimer();
}

float AiApplier::getProgress() const noexcept
{
    if (! animating)
        return 1.0f;
    return juce::jlimit (0.0f, 1.0f, (float) frame / (float) kFrames);
}

void AiApplier::cancel()
{
    stopTimer();
    animating = false;
    targets.clear();
}

void AiApplier::finishImmediately()
{
    if (animating)
    {
        writeFrame (1.0f);
        stopTimer();
        animating = false;
        targets.clear();
    }
}

//==============================================================================
void AiApplier::apply (const AiPatch& patch, bool animate)
{
    cancel();
    changedParams.clear();

    auto& apvts = proc.getAPVTS();

    //----------------------------------------------------------------------
    // 1. 算出所有目标"原始值"
    //    每一项都是 (参数ID, 目标原始值)
    //----------------------------------------------------------------------
    struct Entry { juce::String id; float raw; };
    std::vector<Entry> entries;

    auto add = [&entries] (const juce::String& id, float raw)
    {
        entries.push_back ({ id, raw });
    };

    // --- 音高：把 MIDI 音符换算成插件的 Hz 参数范围
    //
    // sub 的基频参数是固定 20..200Hz？实际是 tune + keytrack 决定基频，
    // 这里用 pitchAmt 表达"起始音高比基频高多少半音"，
    // pitchDecay 表达下落时间。所以：
    //   起始音高 -> pitchAmt（半音数）
    //   结束音高 -> 由 pitchAmt 与基频共同决定，这里只用它算差值
    const float baseNote = patch.endPitch;                 // 结束音高当作基频
    const float startSemis = juce::jlimit (0.0f, 48.0f,
                                           patch.startPitch - baseNote);

    add (pid::subPitchAmt, startSemis);

    // 基频：用 tune 参数近似（-12..+12 半音相对默认）
    // 默认基频约 40Hz（E1），把 endPitch 相对 33 (A1) 的偏移映射过去
    add (pid::tune, juce::jlimit (-12.0f, 12.0f, patch.endPitch - 33.0f));

    // --- 时间：秒 -> 毫秒
    add (pid::subPitchDecay, juce::jlimit (0.0f, 1000.0f, patch.pitchDecay * 1000.0f));
    add (pid::subDecay,      juce::jlimit (20.0f, 4000.0f, patch.ampDecay * 1000.0f));

    // --- 电平
    add (pid::subOn,    1.0f);
    add (pid::subLevel, juce::jlimit (0.0f, 1.0f, patch.subLevel));

    // Click 第 1 层承载"音头"
    add (pid::click (0, "on"),    patch.clickLevel > 0.01f ? 1.0f : 0.0f);
    add (pid::click (0, "level"), juce::jlimit (0.0f, 1.0f, patch.clickLevel));
    add (pid::click (0, "lp"),    juce::jlimit (500.0f, 20000.0f, patch.clickPitch * 3.0f));
    add (pid::click (0, "hp"),    juce::jlimit (20.0f, 2000.0f, patch.clickPitch * 0.25f));

    // --- 噪声层（用 sub 的噪声瞬态）
    add (pid::subNoiseAmt,   juce::jlimit (0.0f, 1.0f, patch.noiseLevel));
    add (pid::subNoiseDecay, juce::jlimit (1.0f, 200.0f, patch.ampDecay * 60.0f));

    // --- 音色
    add (pid::distOn,       patch.saturation > 0.01f ? 1.0f : 0.0f);
    add (pid::distType,     (float) patch.colourModel);
    add (pid::distDrive,    juce::jlimit (1.0f, 20.0f, 1.0f + patch.saturation * 12.0f));
    add (pid::distMix,      juce::jlimit (0.0f, 1.0f, patch.saturation));
    add (pid::distTone,     juce::jlimit (0.0f, 1.0f, patch.tone));
    add (pid::distBias,     juce::jlimit (-0.5f, 0.5f, patch.bias));
    add (pid::distAutoGain, 1.0f);

    add (pid::subFilterHz, juce::jlimit (20.0f, 20000.0f, patch.filterHz));
    add (pid::subWidth,    juce::jlimit (0.0f, 1.0f, patch.width));
    add (pid::subAmpPunch, juce::jlimit (0.0f, 1.0f, patch.punch));

    //----------------------------------------------------------------------
    // 2. 建立插值目标
    //----------------------------------------------------------------------
    targets.clear();

    for (const auto& e : entries)
    {
        auto* p = apvts.getParameter (e.id);
        if (p == nullptr)
            continue;   // 参数不存在就跳过（防御：ID 写错时不会崩）

        const float to = juce::jlimit (0.0f, 1.0f, p->convertTo0to1 (e.raw));
        const float from = p->getValue();

        // 只对真正变化的参数做过渡，并记录下来供 UI 高亮
        if (std::abs (to - from) > 1.0e-5f)
        {
            targets.push_back ({ e.id, from, to });
            changedParams.add (e.id);
        }
    }

    //----------------------------------------------------------------------
    // 3. 包络形状（不走参数插值，直接重塑）
    //----------------------------------------------------------------------
    // 包络的重塑是"形状"层面的，无法逐帧插值，
    // 但因为它只影响下一次触发，不会造成当前音的咔嗒，所以直接应用。
    shapeEnvelope (proc.getEnvelope (EnvelopeTarget::SubPitch),
                   juce::jlimit (0.0f, 1.0f, patch.pitchDecay * 4.0f),
                   patch.pitchCurve, true);

    shapeEnvelope (proc.getEnvelope (EnvelopeTarget::SubAmp),
                   juce::jlimit (0.0f, 1.0f, patch.ampDecay / 2.0f),
                   patch.ampCurve, false);

    proc.commitEnvelope (EnvelopeTarget::SubPitch);
    proc.commitEnvelope (EnvelopeTarget::SubAmp);

    if (targets.empty())
    {
        // 参数没变也要让包络生效
        if (onFinished) onFinished();
        return;
    }

    //----------------------------------------------------------------------
    // 4. 开始过渡
    //----------------------------------------------------------------------
    frame = 0;

    if (animate)
    {
        animating = true;
        startTimerHz (60);      // 12 帧 / 60Hz ≈ 200ms
    }
    else
    {
        writeFrame (1.0f);
        animating = false;
        if (onFinished) onFinished();
    }
}

void AiApplier::writeFrame (float t)
{
    auto& apvts = proc.getAPVTS();

    // 用 smoothstep 而不是线性 —— 两端更平缓，听感上更自然
    const float s = t * t * (3.0f - 2.0f * t);

    for (const auto& tg : targets)
    {
        if (auto* p = apvts.getParameter (tg.paramID))
        {
            const float v = tg.fromNormalised + (tg.toNormalised - tg.fromNormalised) * s;
            p->setValueNotifyingHost (juce::jlimit (0.0f, 1.0f, v));
        }
    }
}

void AiApplier::timerCallback()
{
    if (! animating)
    {
        stopTimer();
        return;
    }

    ++frame;
    const float t = juce::jlimit (0.0f, 1.0f, (float) frame / (float) kFrames);

    writeFrame (t);

    // 过渡期间每帧都刷新 UI（让滑块看得见地移动）
    if (onFinished) onFinished();

    if (frame >= kFrames)
    {
        stopTimer();
        animating = false;
        targets.clear();
        if (onFinished) onFinished();
    }
}

} // namespace kf
