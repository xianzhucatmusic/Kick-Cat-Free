#include "LocalPatchEngine.h"
#include <cmath>

namespace kf
{

//==============================================================================
// 听感维度
//
// 描述词不直接对应参数 —— "冲击力"至少同时影响 punch / click_level /
// amp_curve / saturation。所以先归一到维度，再由维度映射到参数。
//==============================================================================
namespace
{
struct Dimensions
{
    float lowLength   = 0.0f;   // -1 短促 .. +1 绵长（808 那种长尾）
    float punch       = 0.0f;   // -1 柔和 .. +1 冲击力强
    float brightness  = 0.0f;   // -1 暗   .. +1 明亮
    float dirt        = 0.0f;   // -1 干净 .. +1 脏/失真
    float vintage     = 0.0f;   // -1 现代 .. +1 复古/模拟
    float pitchHigh   = 0.0f;   // -1 低沉 .. +1 高亢
    float wideness    = 0.0f;   // -1 单声道 .. +1 宽广
    float softer      = 0.0f;   // -1 硬   .. +1 柔软
    float noise       = 0.0f;   // -1 纯净 .. +1 噪/气声
};

/** 一条关键词规则。 */
struct Keyword
{
    const char* text;        // 匹配词（小写）
    float Dimensions::*field; // 作用在哪个维度
    float amount;            // 加权量
    const char* tag;         // 显示用的标签（nullptr = 不显示）
};

// 规则表：中英文都覆盖。
// 同一个词可以出现在多条规则里（作用于不同维度）。
const Keyword kKeywords[]
{
    //--- 低频长度
    { "long",       &Dimensions::lowLength,  0.9f,  "长尾" },
    { "长",         &Dimensions::lowLength,  0.9f,  "长尾" },
    { "绵长",       &Dimensions::lowLength,  1.0f,  "长尾" },
    { "808",        &Dimensions::lowLength,  1.0f,  "808" },
    { "sustain",    &Dimensions::lowLength,  0.7f,  "长尾" },
    { "sustained",  &Dimensions::lowLength,  0.7f,  "长尾" },
    { "deep",       &Dimensions::lowLength,  0.6f,  "深沉" },
    { "深沉",       &Dimensions::lowLength,  0.6f,  "深沉" },
    { "sub",        &Dimensions::lowLength,  0.7f,  "深沉" },
    { "滚动",       &Dimensions::lowLength,  0.5f,  "长尾" },
    { "rolling",    &Dimensions::lowLength,  0.5f,  "长尾" },

    { "short",      &Dimensions::lowLength, -0.9f,  "短促" },
    { "短",         &Dimensions::lowLength, -0.9f,  "短促" },
    { "tight",      &Dimensions::lowLength, -0.7f,  "紧凑" },
    { "紧凑",       &Dimensions::lowLength, -0.7f,  "紧凑" },
    { "punchy",     &Dimensions::lowLength, -0.4f,  "紧凑" },

    //--- 冲击力
    { "punchy",     &Dimensions::punch,  0.9f,  "冲击力" },
    { "冲击",       &Dimensions::punch,  1.0f,  "冲击力" },
    { "冲击力",     &Dimensions::punch,  1.0f,  "冲击力" },
    { "punch",      &Dimensions::punch,  0.8f,  "冲击力" },
    { "hard",       &Dimensions::punch,  0.7f,  "硬" },
    { "硬",         &Dimensions::punch,  0.7f,  "硬" },
    { "aggressive", &Dimensions::punch,  0.8f,  "冲击力" },
    { "激烈",       &Dimensions::punch,  0.8f,  "冲击力" },
    { "attack",     &Dimensions::punch,  0.5f,  "冲击力" },
    { "音头",       &Dimensions::punch,  0.6f,  "冲击力" },

    { "soft",       &Dimensions::punch, -0.8f,  "柔和" },
    { "柔和",       &Dimensions::punch, -0.8f,  "柔和" },
    { "柔",         &Dimensions::punch, -0.7f,  "柔和" },
    { "gentle",     &Dimensions::punch, -0.7f,  "柔和" },
    { "mellow",     &Dimensions::punch, -0.6f,  "柔和" },

    //--- 明亮度
    { "bright",     &Dimensions::brightness,  0.9f,  "明亮" },
    { "明亮",       &Dimensions::brightness,  0.9f,  "明亮" },
    { "亮",         &Dimensions::brightness,  0.7f,  "明亮" },
    { "crisp",      &Dimensions::brightness,  0.7f,  "明亮" },
    { "清晰",       &Dimensions::brightness,  0.6f,  "明亮" },
    { "clicky",     &Dimensions::brightness,  0.7f,  "明亮" },
    { "snappy",     &Dimensions::brightness,  0.6f,  "明亮" },

    { "dark",       &Dimensions::brightness, -0.9f,  "暗" },
    { "暗",         &Dimensions::brightness, -0.9f,  "暗" },
    { "muffled",    &Dimensions::brightness, -0.8f,  "暗" },
    { "闷",         &Dimensions::brightness, -0.8f,  "暗" },
    { "warm",       &Dimensions::brightness, -0.4f,  "温暖" },
    { "温暖",       &Dimensions::brightness, -0.4f,  "温暖" },
    { "暖",         &Dimensions::brightness, -0.4f,  "温暖" },

    //--- 脏度 / 失真
    { "distorted",  &Dimensions::dirt,  1.0f,  "失真" },
    { "失真",       &Dimensions::dirt,  1.0f,  "失真" },
    { "dirty",      &Dimensions::dirt,  0.8f,  "脏" },
    { "脏",         &Dimensions::dirt,  0.8f,  "脏" },
    { "industrial", &Dimensions::dirt,  0.8f,  "工业" },
    { "工业",       &Dimensions::dirt,  0.8f,  "工业" },
    { "harsh",      &Dimensions::dirt,  0.8f,  "刺" },
    { "刺",         &Dimensions::dirt,  0.7f,  "刺" },
    { "gritty",     &Dimensions::dirt,  0.7f,  "脏" },
    { "aggressive", &Dimensions::dirt,  0.5f,  nullptr },
    { "hardcore",   &Dimensions::dirt,  0.9f,  "工业" },
    { "gabber",     &Dimensions::dirt,  1.0f,  "工业" },
    { "saturat",    &Dimensions::dirt,  0.5f,  "饱和" },
    { "饱和",       &Dimensions::dirt,  0.5f,  "饱和" },
    { "overdrive",  &Dimensions::dirt,  0.6f,  "失真" },

    { "clean",      &Dimensions::dirt, -0.9f,  "干净" },
    { "干净",       &Dimensions::dirt, -0.9f,  "干净" },
    { "pure",       &Dimensions::dirt, -0.8f,  "干净" },
    { "纯净",       &Dimensions::dirt, -0.8f,  "干净" },

    //--- 复古 / 模拟
    { "vintage",    &Dimensions::vintage,  1.0f,  "复古" },
    { "复古",       &Dimensions::vintage,  1.0f,  "复古" },
    { "analog",     &Dimensions::vintage,  1.0f,  "模拟" },
    { "模拟",       &Dimensions::vintage,  1.0f,  "模拟" },
    { "tape",       &Dimensions::vintage,  1.0f,  "磁带" },
    { "磁带",       &Dimensions::vintage,  1.0f,  "磁带" },
    { "lofi",       &Dimensions::vintage,  0.9f,  "Lo-Fi" },
    { "lo-fi",      &Dimensions::vintage,  0.9f,  "Lo-Fi" },
    { "lo fi",      &Dimensions::vintage,  0.9f,  "Lo-Fi" },
    { "retro",      &Dimensions::vintage,  0.9f,  "复古" },
    { "oldschool",  &Dimensions::vintage,  0.8f,  "复古" },
    { "old school", &Dimensions::vintage,  0.8f,  "复古" },
    { "发霉",       &Dimensions::vintage,  0.9f,  "Lo-Fi" },
    { "颗粒",       &Dimensions::vintage,  0.6f,  "Lo-Fi" },
    { "dusty",      &Dimensions::vintage,  0.8f,  "Lo-Fi" },
    { "vinyl",      &Dimensions::vintage,  0.8f,  "Lo-Fi" },
    { "黑胶",       &Dimensions::vintage,  0.8f,  "Lo-Fi" },
    { "tube",       &Dimensions::vintage,  0.8f,  "电子管" },
    { "电子管",     &Dimensions::vintage,  0.8f,  "电子管" },
    { "valve",      &Dimensions::vintage,  0.8f,  "电子管" },

    { "modern",     &Dimensions::vintage, -0.8f,  "现代" },
    { "现代",       &Dimensions::vintage, -0.8f,  "现代" },
    { "digital",    &Dimensions::vintage, -0.7f,  "数字" },

    //--- 音高
    { "high",       &Dimensions::pitchHigh,  0.7f,  "高音" },
    { "高",         &Dimensions::pitchHigh,  0.6f,  "高音" },
    { "screech",    &Dimensions::pitchHigh,  0.9f,  "高音" },
    { "tonal",      &Dimensions::pitchHigh,  0.4f,  "有调性" },
    { "有调",       &Dimensions::pitchHigh,  0.4f,  "有调性" },
    { "melodic",    &Dimensions::pitchHigh,  0.4f,  "有调性" },

    { "low",        &Dimensions::pitchHigh, -0.7f,  "深沉" },
    { "低",         &Dimensions::pitchHigh, -0.7f,  "深沉" },
    { "sub",        &Dimensions::pitchHigh, -0.5f,  nullptr },
    { "deep",       &Dimensions::pitchHigh, -0.6f,  nullptr },
    { "深沉",       &Dimensions::pitchHigh, -0.6f,  nullptr },

    //--- 宽度
    { "wide",       &Dimensions::wideness,  1.0f,  "宽广" },
    { "宽",         &Dimensions::wideness,  1.0f,  "宽广" },
    { "宽广",       &Dimensions::wideness,  1.0f,  "宽广" },
    { "stereo",     &Dimensions::wideness,  0.8f,  "宽广" },
    { "立体",       &Dimensions::wideness,  0.8f,  "宽广" },
    { "mono",       &Dimensions::wideness, -0.8f,  "单声道" },
    { "单声道",     &Dimensions::wideness, -0.8f,  "单声道" },

    //--- 柔软度
    { "soft",       &Dimensions::softer,  0.8f,  nullptr },
    { "柔和",       &Dimensions::softer,  0.8f,  nullptr },
    { "round",      &Dimensions::softer,  0.7f,  "圆润" },
    { "圆润",       &Dimensions::softer,  0.7f,  "圆润" },
    { "smooth",     &Dimensions::softer,  0.6f,  "圆润" },

    //--- 噪声 / 气声
    { "noise",      &Dimensions::noise,  1.0f,  "噪" },
    { "噪",         &Dimensions::noise,  0.8f,  "噪" },
    { "noisy",      &Dimensions::noise,  1.0f,  "噪" },
    { "气声",       &Dimensions::noise,  0.7f,  "噪" },
    { "snap",       &Dimensions::noise,  0.5f,  nullptr },
};

/** 风格基线：识别到风格词时先套用，再用维度做偏移。 */
struct StyleBase
{
    const char* name;
    float lowLength, punch, brightness, dirt, vintage, pitchHigh;
};

const StyleBase kStyles[]
{
    { "trap",      0.8f,  0.3f, -0.3f,  0.0f,  0.0f, -0.3f },
    { "808",       0.9f,  0.2f, -0.4f,  0.0f,  0.0f, -0.4f },
    { "techno",   -0.2f,  0.7f,  0.2f,  0.3f, -0.1f,  0.0f },
    { "house",     0.0f,  0.5f,  0.1f,  0.0f,  0.1f,  0.0f },
    { "hardstyle",-0.4f,  0.9f,  0.4f,  0.8f,  0.0f,  0.3f },
    { "hardcore", -0.5f,  1.0f,  0.4f,  0.9f,  0.0f,  0.3f },
    { "gabber",   -0.5f,  1.0f,  0.3f,  1.0f,  0.0f,  0.2f },
    { "dnb",       0.2f,  0.8f,  0.2f,  0.3f, -0.1f,  0.0f },
    { "drum",      0.2f,  0.8f,  0.2f,  0.3f, -0.1f,  0.0f },
    { "hiphop",    0.3f,  0.4f, -0.3f,  0.1f,  0.5f, -0.2f },
    { "hip hop",   0.3f,  0.4f, -0.3f,  0.1f,  0.5f, -0.2f },
    { "boom bap",  0.2f,  0.4f, -0.4f,  0.2f,  0.7f, -0.2f },
    { "lofi",      0.1f,  0.2f, -0.6f,  0.2f,  1.0f, -0.1f },
    { "lo-fi",     0.1f,  0.2f, -0.6f,  0.2f,  1.0f, -0.1f },
    { "edm",      -0.1f,  0.8f,  0.4f,  0.2f, -0.3f,  0.1f },
    { "dubstep",   0.4f,  0.7f,  0.0f,  0.6f, -0.1f, -0.1f },
    { "ambient",   0.7f, -0.6f, -0.4f,  0.0f,  0.3f, -0.5f },
    { "jazz",      0.0f,  0.3f, -0.2f,  0.0f,  0.6f, -0.1f },
    { "funk",     -0.1f,  0.6f,  0.2f,  0.1f,  0.5f,  0.0f },
    { "rock",     -0.2f,  0.7f,  0.2f,  0.4f,  0.3f,  0.1f },
    { "metal",    -0.3f,  0.9f,  0.3f,  0.7f,  0.1f,  0.2f },
};

/** 确定性伪随机（同 AiPatch 的做法，保证可复现）。 */
struct Rng
{
    explicit Rng (juce::uint32 s) : state (s | 1u) {}
    juce::uint32 next()
    {
        state ^= state << 13; state ^= state >> 17; state ^= state << 5;
        return state;
    }
    float bipolar() { return (float) (next() >> 8) / (float) (1 << 23) - 1.0f; }
    juce::uint32 state;
};
} // anonymous namespace

//==============================================================================
juce::StringArray LocalPatchEngine::getQuickTags()
{
    // UI 上的快捷标签。点一下会把描述填进输入框。
    //
    // 注意必须用 fromUTF8 —— juce::StringArray 的 initializer_list<const char*>
    // 构造会把中文字节当 Latin-1 解释，界面上显示成乱码。
    return juce::StringArray {
        juce::String::fromUTF8 ("深沉"),
        juce::String::fromUTF8 ("明亮"),
        juce::String::fromUTF8 ("复古"),
        juce::String::fromUTF8 ("工业"),
        juce::String::fromUTF8 ("Lo-Fi"),
        juce::String::fromUTF8 ("有冲击力"),
        juce::String::fromUTF8 ("长尾 808"),
        juce::String::fromUTF8 ("短促紧凑"),
        juce::String::fromUTF8 ("模拟磁带"),
        juce::String::fromUTF8 ("宽广")
    };
}

//==============================================================================
bool LocalPatchEngine::generate (const juce::String& prompt,
                                 int seed,
                                 AiPatch& out,
                                 juce::String& error)
{
    detectedStyle.clear();
    detectedTags.clear();

    const auto text = prompt.trim();
    if (text.isEmpty())
    {
        error = "描述不能为空";
        return false;
    }

    // 防御：如果调用方用 `const char*` 隐式构造 juce::String，
    // 中文字节会被当成 Latin-1 解释 —— 长度膨胀、关键词全部匹配不上，
    // 而且**不报错**（只是"识别不出特征"），非常难查。
    // 这里检测到疑似 mojibake 就按 UTF-8 重新解释一次。
    auto looksLikeMojibake = [] (const juce::String& t)
    {
        // UTF-8 被按 Latin-1 读时，每个字节变成一个 U+0080..U+00FF 的字符；
        // 而真正的中文都在 U+4E00 以上。
        //
        // 注意要统计**全部** >= 0x80 的字符，不能只数 0xC0..0xFF ——
        // 实测坏字符串里 0xC0-0xFF 有 13 个、0x80-0xBF 有 26 个，
        // 只数前者会漏判。
        int high = 0;
        int cjk  = 0;

        for (auto c : t)
        {
            if (c >= 0x80 && c <= 0xFF)  ++high;
            if (c >= 0x3000)             ++cjk;    // CJK / 全角标点
        }

        // 有大量 Latin-1 扩展、且几乎没有真正的 CJK -> 判定为 mojibake
        return high > 2 && cjk == 0;
    };

    // 检测到 mojibake 时**无法可靠还原** —— 字节已经在这一层丢失了。
    // 与其静默地"识别不出特征"，不如明确报错，让调用方去修。
    // （正确做法是调用方用 juce::String::fromUTF8 构造字符串。）
    if (looksLikeMojibake (text))
    {
        error = "描述文本编码异常（应按 UTF-8 传入）";
        return false;
    }

    const auto lower = text.toLowerCase();

    //--- 1. 风格先验
    Dimensions dim;

    for (const auto& s : kStyles)
    {
        if (lower.contains (s.name))
        {
            dim.lowLength  += s.lowLength;
            dim.punch      += s.punch;
            dim.brightness += s.brightness;
            dim.dirt       += s.dirt;
            dim.vintage    += s.vintage;
            dim.pitchHigh  += s.pitchHigh;
            detectedStyle = juce::String (s.name).toUpperCase();
            break;   // 只取第一个匹配到的风格，避免叠加过头
        }
    }

    //--- 2. 关键词加权
    float tagStrength = 0.0f;   // 用于判断"用户到底说了多少"

    for (const auto& k : kKeywords)
    {
        if (! lower.contains (k.text))
            continue;

        dim.*(k.field) += k.amount;
        tagStrength += std::abs (k.amount);

        if (k.tag != nullptr)
        {
            const juce::String t (k.tag);
            if (! detectedTags.contains (t))
                detectedTags.add (t);
        }
    }

    // 没有任何可识别信息时不要瞎猜 —— 明确告诉用户
    if (tagStrength < 0.3f && detectedStyle.isEmpty())
    {
        error = "没能从描述里识别出音色特征，试试「有冲击力的长尾 808」这类说法";
        return false;
    }

    // 维度归一到 -1..1（叠加后可能超过，用 tanh 软限制）
    auto squash = [] (float v) { return std::tanh (v * 0.8f); };
    dim.lowLength  = squash (dim.lowLength);
    dim.punch      = squash (dim.punch);
    dim.brightness = squash (dim.brightness);
    dim.dirt       = squash (dim.dirt);
    dim.vintage    = squash (dim.vintage);
    dim.pitchHigh  = squash (dim.pitchHigh);
    dim.wideness   = squash (dim.wideness);
    dim.softer     = squash (dim.softer);
    dim.noise      = squash (dim.noise);

    //--- 3. 维度 -> 参数
    AiPatch p;

    // 从"中性底鼓"出发：中等的音高下滑、中等的衰减
    const float baseStartPitch = 48.0f;   // C3
    const float baseEndPitch   = 33.0f;   // A1

    // 音高
    p.startPitch = baseStartPitch + dim.pitchHigh * 12.0f;
    p.endPitch   = baseEndPitch   + dim.pitchHigh * 8.0f;

    // 音高下滑时间：越"冲击"越短促
    p.pitchDecay = juce::jlimit (0.008f, 0.25f,
        0.05f * (1.0f - dim.punch * 0.5f) * (1.0f + dim.lowLength * 0.3f));

    // 幅度衰减 —— 这是"长尾/短促"最主要的落点
    p.ampDecay = juce::jlimit (0.06f, 3.5f,
        0.55f * std::pow (2.0f, dim.lowLength * 2.0f));

    // 曲线：长尾用更"持"的曲线，短促用更"点"的
    p.ampCurve   = juce::jlimit (0.05f, 0.95f, 0.5f + dim.lowLength * 0.35f);
    p.pitchCurve = juce::jlimit (0.05f, 0.95f, 0.5f - dim.punch * 0.3f);

    // 电平
    p.subLevel   = juce::jlimit (0.15f, 0.85f, 0.48f + dim.lowLength * 0.15f);
    p.clickLevel = juce::jlimit (0.0f,  0.75f, 0.26f + dim.punch * 0.28f + dim.brightness * 0.12f);
    p.noiseLevel = juce::jlimit (0.0f,  0.6f,  dim.noise * 0.35f + dim.vintage * 0.08f);

    // 音头音高：明亮的音头更靠上
    p.clickPitch = juce::jlimit (300.0f, 8000.0f,
        1400.0f * std::pow (2.0f, dim.brightness * 1.4f));

    // 饱和
    p.saturation = juce::jlimit (0.0f, 1.0f,
        dim.dirt * 0.55f + dim.vintage * 0.20f + dim.punch * 0.08f);

    // 明暗：复古通常偏暗
    p.tone = juce::jlimit (0.05f, 0.95f,
        0.5f + dim.brightness * 0.35f - dim.vintage * 0.12f);

    // 偶次谐波：复古/温暖偏向正偏置
    p.bias = juce::jlimit (-0.3f, 0.3f, dim.vintage * 0.10f + dim.softer * 0.05f);

    // 低通：暗的时候收
    p.filterHz = juce::jlimit (400.0f, 20000.0f,
        20000.0f * std::pow (0.05f, juce::jmax (0.0f, -dim.brightness) * 1.2f));

    p.width = juce::jlimit (0.0f, 1.0f, dim.wideness * 0.7f);
    p.punch = juce::jlimit (0.0f, 1.0f, 0.5f + dim.punch * 0.4f);

    //--- 4. 选择染色模型
    // 按"脏度 + 复古度"挑一个合适的，而不是随便选
    if (dim.vintage > 0.35f && dim.dirt < 0.45f)
        p.colourModel = 1;                       // Tape
    else if (dim.vintage > 0.35f)
        p.colourModel = 6;                       // Germanium
    else if (dim.dirt > 0.5f)
        p.colourModel = (dim.punch > 0.4f) ? 2 : 7;   // Hard Clip / Fuzz
    else if (dim.brightness > 0.4f)
        p.colourModel = 9;                       // Exciter
    else if (dim.dirt > 0.2f)
        p.colourModel = 0;                       // Tube
    else
        p.colourModel = 5;                       // Console（几乎不失真）

    //--- 5. 种子扰动
    // 同一个描述配不同种子给出"微妙变化"。
    // 扰动幅度很小且集中在非关键参数上，避免语义被破坏。
    if (seed != 0)
    {
        Rng rng ((juce::uint32) seed);

        p.startPitch += rng.bipolar() * 0.6f;
        p.endPitch   += rng.bipolar() * 0.4f;

        // 时间类参数按百分比扰动（±4%），听感上"同一个音色的另一种做法"
        p.ampDecay   *= (1.0f + rng.bipolar() * 0.04f);
        p.pitchDecay *= (1.0f + rng.bipolar() * 0.06f);

        p.clickLevel += rng.bipolar() * 0.02f;
        p.subLevel   += rng.bipolar() * 0.015f;
        p.clickPitch *= (1.0f + rng.bipolar() * 0.05f);
        p.tone       += rng.bipolar() * 0.03f;
        p.punch      += rng.bipolar() * 0.02f;

        // 曲线扰动：这是"手感"差异的主要来源，但幅度要小
        p.ampCurve   += rng.bipolar() * 0.03f;
        p.pitchCurve += rng.bipolar() * 0.03f;
    }

    p.sanitise();

    if (! p.isSane())
    {
        error = "生成的参数不合理，请换个说法再试";
        return false;
    }

    out = p;
    return true;
}

} // namespace kf
