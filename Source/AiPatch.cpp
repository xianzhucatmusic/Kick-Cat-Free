#include "AiPatch.h"
#include <cmath>

namespace kf
{

//==============================================================================
// JSON 解析
//
// 容错优先：模型（或本地引擎）可能少给字段、给字符串、给越界值。
// 任何一种都不应该让整个生成失败 —— 能用的部分就用，不能用的退回默认。
//==============================================================================

namespace
{
/** 从 var 里安全取浮点：支持数字与数字字符串。 */
float getFloat (const juce::var& obj, const char* key, float fallback)
{
    if (auto* o = obj.getDynamicObject())
    {
        const auto v = o->getProperty (juce::Identifier (key));
        if (v.isVoid() || v.isUndefined())
            return fallback;
        if (v.isDouble() || v.isInt() || v.isInt64() || v.isBool())
            return (float) (double) v;
        if (v.isString())
            return v.toString().getFloatValue();
    }
    return fallback;
}

bool hasKey (const juce::var& obj, const char* key)
{
    if (auto* o = obj.getDynamicObject())
        return o->hasProperty (juce::Identifier (key));
    return false;
}
} // anonymous namespace

AiPatch AiPatch::fromJson (const juce::String& json, bool* ok)
{
    AiPatch p;

    juce::var parsed;
    const auto result = juce::JSON::parse (json, parsed);

    if (result.failed() || ! parsed.isObject())
    {
        if (ok) *ok = false;
        return p;
    }

    // 逐字段读取；缺什么就用默认值
    p.startPitch  = getFloat (parsed, "start_pitch",  p.startPitch);
    p.endPitch    = getFloat (parsed, "end_pitch",    p.endPitch);
    p.pitchDecay  = getFloat (parsed, "pitch_decay",  p.pitchDecay);
    p.pitchCurve  = getFloat (parsed, "pitch_curve",  p.pitchCurve);
    p.ampDecay    = getFloat (parsed, "amp_decay",    p.ampDecay);
    p.ampCurve    = getFloat (parsed, "amp_curve",    p.ampCurve);
    p.subLevel    = getFloat (parsed, "sub_level",    p.subLevel);
    p.clickLevel  = getFloat (parsed, "click_level",  p.clickLevel);
    p.noiseLevel  = getFloat (parsed, "noise_level",  p.noiseLevel);
    p.clickPitch  = getFloat (parsed, "click_pitch",  p.clickPitch);
    p.saturation  = getFloat (parsed, "saturation",   p.saturation);
    p.tone        = getFloat (parsed, "tone",         p.tone);
    p.bias        = getFloat (parsed, "bias",         p.bias);
    p.filterHz    = getFloat (parsed, "filter_hz",    p.filterHz);
    p.width       = getFloat (parsed, "width",        p.width);
    p.punch       = getFloat (parsed, "punch",        p.punch);

    if (hasKey (parsed, "colour_model"))
        p.colourModel = (int) getFloat (parsed, "colour_model", (float) p.colourModel);

    p.sanitise();

    if (ok) *ok = true;
    return p;
}

juce::String AiPatch::toJson() const
{
    auto* o = new juce::DynamicObject();

    o->setProperty ("start_pitch",  startPitch);
    o->setProperty ("end_pitch",    endPitch);
    o->setProperty ("pitch_decay",  pitchDecay);
    o->setProperty ("pitch_curve",  pitchCurve);
    o->setProperty ("amp_decay",    ampDecay);
    o->setProperty ("amp_curve",    ampCurve);
    o->setProperty ("sub_level",    subLevel);
    o->setProperty ("click_level",  clickLevel);
    o->setProperty ("noise_level",  noiseLevel);
    o->setProperty ("click_pitch",  clickPitch);
    o->setProperty ("saturation",   saturation);
    o->setProperty ("colour_model", colourModel);
    o->setProperty ("tone",         tone);
    o->setProperty ("bias",         bias);
    o->setProperty ("filter_hz",    filterHz);
    o->setProperty ("width",        width);
    o->setProperty ("punch",        punch);

    return juce::JSON::toString (juce::var (o), true);
}

bool AiPatch::isSane() const
{
    auto fin = [] (float v) { return std::isfinite (v); };

    if (! fin (startPitch) || ! fin (endPitch) || ! fin (pitchDecay)
        || ! fin (ampDecay) || ! fin (subLevel) || ! fin (clickLevel))
        return false;

    if (ampDecay <= 0.0f || pitchDecay < 0.0f)
        return false;

    return true;
}

void AiPatch::sanitise()
{
    // NaN / Inf 先清掉 —— 它们会污染整个参数树
    auto fix = [] (float& v, float lo, float hi, float fallback)
    {
        if (! std::isfinite (v)) { v = fallback; return; }
        v = juce::jlimit (lo, hi, v);
    };

    // 音高用 MIDI 音符，限制在钢琴范围内（超出没有音乐意义）
    fix (startPitch, 12.0f, 96.0f, 48.0f);
    fix (endPitch,   12.0f, 96.0f, 33.0f);

    // 时间：最短 1ms（再短没意义），最长 4 秒
    fix (pitchDecay, 0.0f, 2.0f,   0.05f);
    fix (ampDecay,   0.01f, 4.0f,  0.5f);

    fix (pitchCurve, 0.0f, 1.0f, 0.5f);
    fix (ampCurve,   0.0f, 1.0f, 0.5f);

    fix (subLevel,   0.0f, 1.0f, 0.45f);
    fix (clickLevel, 0.0f, 1.0f, 0.30f);
    fix (noiseLevel, 0.0f, 1.0f, 0.0f);

    fix (clickPitch, 200.0f, 8000.0f, 1500.0f);
    fix (saturation, 0.0f, 1.0f, 0.0f);
    fix (tone,       0.0f, 1.0f, 0.5f);
    fix (bias,      -0.5f, 0.5f, 0.0f);
    fix (filterHz,   200.0f, 20000.0f, 20000.0f);
    fix (width,      0.0f, 1.0f, 0.0f);
    fix (punch,      0.0f, 1.0f, 0.5f);

    colourModel = juce::jlimit (0, 9, colourModel);

    // 起始音高必须不低于结束音高 + 半音，否则没有"下滑"的听感
    // （除非用户就是要往上扫 —— 这里只保证不会两者完全相同）
    if (std::abs (startPitch - endPitch) < 0.5f)
        endPitch = startPitch - 6.0f;
}

} // namespace kf
