#include "FactoryPresets.h"
#include "Params.h"

namespace kf
{
namespace FactoryPresets
{

//==============================================================================
// 工厂预设生成器
//
// 每个风格定义：
//   - 子类（二级菜单）若干
//   - 每个子类下 4 个变体
// 风格数 ≥ 10，子类数 × 变体数 保证每风格 ≥ 40
//
// 所有取值都经过音乐性约束：不会有"20ms 衰减配 48 半音"这种没法用的组合。
//==============================================================================

namespace
{
//------------------------------------------------------------------------------
// 确定性伪随机：同一个预设名每次生成结果一致（可复现），
// 不同预设之间有可闻差异。
//------------------------------------------------------------------------------
struct Rng
{
    explicit Rng (const juce::String& seedText)
    {
        // FNV-1a：把预设名散列成种子
        juce::uint32 h = 2166136261u;
        const char* p = seedText.toRawUTF8();
        for (; *p != 0; ++p)
        {
            h ^= (juce::uint32) (juce::uint8) *p;
            h *= 16777619u;
        }
        state = h | 1u;   // 保证非零
    }

    juce::uint32 next()
    {
        // xorshift32
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }

    /** 0..1 */
    float nextFloat() { return (float) (next() >> 8) / (float) (1 << 24); }

    /** [lo, hi] */
    float range (float lo, float hi) { return lo + nextFloat() * (hi - lo); }

    /** 以 p 的概率返回 true */
    bool chance (float p) { return nextFloat() < p; }

    /** [0, maxExclusive) 的整数 */
    int nextInt (int maxExclusive)
    {
        if (maxExclusive <= 1) return 0;
        return (int) (next() % (juce::uint32) maxExclusive);
    }

    juce::uint32 state = 1u;
};

//------------------------------------------------------------------------------
// 参数写入辅助
//
// 关键：AudioProcessorValueTreeState 的状态格式是**每个参数一个 PARAM 子节点**：
//     <PARAMETERS>
//       <PARAM id="sub_decay" value="419.99"/>
//       ...
//     </PARAMETERS>
//
// 而不是把参数名当属性平铺在根节点上。
// 之前写成平铺属性，replaceState 匹配不到任何 PARAM 子节点，
// 结果 480 个预设载入后**全部等于默认值**（而且不报错，非常隐蔽）。
//------------------------------------------------------------------------------
inline void put (juce::ValueTree& t, const juce::String& id, float v)
{
    juce::ValueTree p ("PARAM");
    p.setProperty ("id", id, nullptr);
    p.setProperty ("value", v, nullptr);
    t.appendChild (p, nullptr);
}

inline void put (juce::ValueTree& t, const char* id, float v)
{
    put (t, juce::String (id), v);
}

//------------------------------------------------------------------------------
// 一个风格的"音色基因"：描述这个风格大致的取向
//------------------------------------------------------------------------------
struct StyleDNA
{
    // 时长范围（ms）
    float decayLo = 200.0f, decayHi = 600.0f;
    // 音高包络量（半音）
    float pitchLo = 12.0f, pitchHi = 36.0f;
    // 基础电平
    float levelLo = 0.42f, levelHi = 0.58f;
    // 泛音数上限
    int   harmMax = 6;
    // 是否常用失真
    float distChance = 0.15f;
    // 是否常用长音（808 类）
    float longChance = 0.0f;
    // click 层的典型衰减（ms）
    float clickDecayLo = 15.0f, clickDecayHi = 60.0f;
    // 是否常用低通（做暗色音色）
    float darkChance = 0.3f;
};

// 每个风格一份 DNA
inline StyleDNA dnaFor (const juce::String& style)
{
    StyleDNA d;

    if (style == "Techno")        { d.decayLo = 180; d.decayHi = 520;  d.pitchLo = 18; d.pitchHi = 40;
                                    d.harmMax = 6; d.distChance = 0.25f; d.clickDecayLo = 10; d.clickDecayHi = 40; }
    else if (style == "House")    { d.decayLo = 220; d.decayHi = 560;  d.pitchLo = 14; d.pitchHi = 32;
                                    d.harmMax = 4; d.distChance = 0.10f; d.clickDecayLo = 20; d.clickDecayHi = 70; }
    else if (style == "Trap")     { d.decayLo = 400; d.decayHi = 1400; d.pitchLo = 8;  d.pitchHi = 28;
                                    d.harmMax = 3; d.distChance = 0.10f; d.longChance = 0.5f;
                                    d.clickDecayLo = 8; d.clickDecayHi = 30; }
    else if (style == "808")      { d.decayLo = 700; d.decayHi = 1800; d.pitchLo = 6;  d.pitchHi = 22;
                                    d.harmMax = 2; d.distChance = 0.05f; d.longChance = 0.8f;
                                    d.clickDecayLo = 6; d.clickDecayHi = 24; }
    else if (style == "Hardstyle"){ d.decayLo = 150; d.decayHi = 420;  d.pitchLo = 24; d.pitchHi = 48;
                                    d.harmMax = 8; d.distChance = 0.55f; d.clickDecayLo = 8; d.clickDecayHi = 30; }
    else if (style == "Hardcore") { d.decayLo = 120; d.decayHi = 380;  d.pitchLo = 28; d.pitchHi = 48;
                                    d.harmMax = 8; d.distChance = 0.75f; d.clickDecayLo = 6; d.clickDecayHi = 24; }
    else if (style == "DnB")      { d.decayLo = 200; d.decayHi = 700;  d.pitchLo = 16; d.pitchHi = 38;
                                    d.harmMax = 6; d.distChance = 0.30f; d.clickDecayLo = 10; d.clickDecayHi = 40; }
    else if (style == "HipHop")   { d.decayLo = 300; d.decayHi = 900;  d.pitchLo = 10; d.pitchHi = 28;
                                    d.harmMax = 4; d.distChance = 0.15f; d.longChance = 0.4f;
                                    d.clickDecayLo = 12; d.clickDecayHi = 50; }
    else if (style == "LoFi")     { d.decayLo = 260; d.decayHi = 700;  d.pitchLo = 10; d.pitchHi = 26;
                                    d.harmMax = 3; d.distChance = 0.10f; d.darkChance = 0.75f;
                                    d.clickDecayLo = 20; d.clickDecayHi = 80; }
    else if (style == "EDM")      { d.decayLo = 200; d.decayHi = 560;  d.pitchLo = 20; d.pitchHi = 44;
                                    d.harmMax = 6; d.distChance = 0.30f; d.clickDecayLo = 10; d.clickDecayHi = 36; }
    else if (style == "Dubstep")  { d.decayLo = 300; d.decayHi = 900;  d.pitchLo = 14; d.pitchHi = 36;
                                    d.harmMax = 6; d.distChance = 0.45f; d.longChance = 0.3f;
                                    d.clickDecayLo = 10; d.clickDecayHi = 40; }
    else if (style == "Ambient")  { d.decayLo = 600; d.decayHi = 1600; d.pitchLo = 4;  d.pitchHi = 16;
                                    d.harmMax = 3; d.distChance = 0.03f; d.longChance = 0.7f;
                                    d.darkChance = 0.5f; d.clickDecayLo = 20; d.clickDecayHi = 90; }

    return d;
}

} // anonymous namespace

//==============================================================================
// 风格与子类定义
//
// 每种风格 10 个子类 × 每个子类 4 个变体 = 40 个预设。
// 12 种风格 => 480 个预设。
//==============================================================================

struct StyleDef
{
    const char* name;
    const char* categories[10];
};

static const StyleDef kStyles[]
{
    { "Techno",   { "Rolling", "Peak Time", "Deep", "Warehouse", "Industrial",
                    "Minimal", "Dub", "Hard", "Hypnotic", "Raw" } },
    { "House",    { "Classic", "Deep", "Jackin", "Garage", "Tech",
                    "Soulful", "Tribal", "Piano", "Bassline", "Chunky" } },
    { "Trap",     { "Boom", "Sub", "Punchy", "Dark", "Bouncy",
                    "Distorted", "Clean", "Wide", "Snappy", "Heavy" } },
    { "808",      { "Long", "Short", "Glide", "Distorted", "Clean",
                    "Thump", "Sub", "Punchy", "Warm", "Saturated" } },
    { "Hardstyle",{ "Reverse Bass", "Screech", "Punch", "Kick Only", "Raw",
                    "Euphoric", "Hard", "Festival", "Kickroll", "Distorted" } },
    { "Hardcore", { "Gabber", "Industrial", "Speedcore", "Terror", "Uptempo",
                    "Raw", "Distorted", "Punch", "Metal", "Extreme" } },
    { "DnB",      { "Rolling", "Jump Up", "Liquid", "Neuro", "Jungle",
                    "Halftime", "Dark", "Minimal", "Amen", "Deep" } },
    { "HipHop",   { "Boom Bap", "Trap Soul", "LoFi", "Dusty", "Modern",
                    "West Coast", "East Coast", "Boom", "Tight", "Deep" } },
    { "LoFi",     { "Dusty", "Warm Tape", "Soft", "Vinyl", "Mellow",
                    "Hazy", "Filtered", "Vintage", "Cassette", "Sleepy" } },
    { "EDM",      { "Festival", "Big Room", "Future Bass", "Pop", "Progressive",
                    "Electro", "Tropical", "Mainstage", "Anthem", "Bright" } },
    { "Dubstep",  { "Classic", "Riddim", "Deep", "Brostep", "Melodic",
                    "Heavy", "Wobble", "Dark", "Sub", "Festival" } },
    { "Ambient",  { "Soft", "Long", "Pad", "Drone", "Meditative",
                    "Textural", "Warm", "Cold", "Evolving", "Minimal" } }
};

static constexpr int kNumVariants = 4;

//==============================================================================
const std::vector<Entry>& getAll()
{
    static std::vector<Entry> cache;
    static bool built = false;

    if (! built)
    {
        for (const auto& st : kStyles)
        {
            for (const char* cat : st.categories)
            {
                for (int v = 1; v <= kNumVariants; ++v)
                {
                    Entry e;
                    e.style = st.name;
                    e.category = cat;

                    // 名字形如 "Techno - Rolling 03"
                    e.name = juce::String (st.name) + " - " + cat + " "
                           + juce::String (v).paddedLeft ('0', 2);

                    cache.push_back (e);
                }
            }
        }
        built = true;
    }

    return cache;
}

//==============================================================================
juce::ValueTree create (const juce::String& fullName)
{
    juce::ValueTree t ("PARAMETERS");

    // 找到这个预设的风格/子类
    juce::String style, category;
    for (const auto& e : getAll())
    {
        if (e.name == fullName) { style = e.style; category = e.category; break; }
    }

    // 未知名字退回到一个安全的默认音色（而不是空树，否则参数全是 0）
    if (style.isEmpty())
    {
        put (t, pid::subOn, 1.0f);
        put (t, pid::subLevel, 0.45f);
        put (t, pid::subDecay, 420.0f);
        put (t, pid::subPitchAmt, 24.0f);
        put (t, pid::subHarmonics, 1.0f);
        put (t, pid::tune, 0.0f);
        put (t, pid::keytrack, 1.0f);
        return t;
    }

    const auto dna = dnaFor (style);
    Rng rng (fullName);

    //--- 子类的"性格"偏移 -----------------------------------------------
    // 让同一风格下的不同子类有明确区分，而不是只有随机差异。
    float decayBias = 1.0f, pitchBias = 1.0f, levelBias = 1.0f, darkBias = 0.0f;

    const juce::String cat = category.toLowerCase();

    if (cat.contains ("long") || cat.contains ("deep") || cat.contains ("sub")
        || cat.contains ("drone") || cat.contains ("meditat"))
        { decayBias = 1.6f; pitchBias = 0.7f; }
    if (cat.contains ("short") || cat.contains ("tight") || cat.contains ("snappy")
        || cat.contains ("punch"))
        { decayBias = 0.6f; pitchBias = 1.3f; }
    if (cat.contains ("dark") || cat.contains ("industrial") || cat.contains ("metal")
        || cat.contains ("terror") || cat.contains ("hazy") || cat.contains ("cold"))
        { darkBias = 0.5f; }
    if (cat.contains ("hard") || cat.contains ("heavy") || cat.contains ("extreme")
        || cat.contains ("brostep") || cat.contains ("distort") || cat.contains ("raw"))
        { levelBias = 1.1f; pitchBias = 1.15f; }
    if (cat.contains ("soft") || cat.contains ("mellow") || cat.contains ("sleepy")
        || cat.contains ("warm") || cat.contains ("clean"))
        { levelBias = 0.9f; darkBias = 0.3f; }
    if (cat.contains ("wide"))  { /* 稍后单独处理 width */ }
    if (cat.contains ("glide")) { pitchBias = 1.2f; }

    //--- Sub ---------------------------------------------------------------
    const float decay = juce::jlimit (60.0f, 2000.0f,
        rng.range (dna.decayLo, dna.decayHi) * decayBias);

    put (t, pid::subOn, 1.0f);
    put (t, pid::subLevel, juce::jlimit (0.25f, 0.75f, rng.range (dna.levelLo, dna.levelHi) * levelBias));
    put (t, pid::subDecay, decay);
    put (t, pid::subPitchAmt, juce::jlimit (0.0f, 48.0f,
        rng.range (dna.pitchLo, dna.pitchHi) * pitchBias));
    put (t, pid::subHarmonics, (float) rng.nextInt (dna.harmMax + 1));
    put (t, pid::subHarmTilt, rng.range (0.35f, 0.72f));
    put (t, pid::subAmpPunch, rng.range (0.35f, 0.7f));
    put (t, pid::subPhase, 0.0f);
    put (t, pid::tune, (float) (rng.nextInt (5) - 2));      // ±2 半音
    put (t, pid::keytrack, 1.0f);
    put (t, pid::glide, 0.0f);
    put (t, pid::glideMode, 0.0f);

    // 音高包络独立时长：长音色常常需要音高早点落定
    put (t, pid::subPitchDecay, rng.chance (0.45f)
        ? juce::jlimit (20.0f, 800.0f, decay * rng.range (0.2f, 0.6f)) : 0.0f);

    // 泛音结构
    put (t, pid::subOddEven, rng.range (-0.5f, 0.5f));
    put (t, pid::subDetune, rng.chance (0.25f) ? rng.range (4.0f, 20.0f) : 0.0f);
    put (t, pid::subShape, rng.range (0.35f, 0.65f));
    put (t, pid::subPhaseRand, rng.chance (0.3f) ? rng.range (0.2f, 0.8f) : 0.0f);

    // 立体声宽度：多数保持单声道（底鼓更实）
    put (t, pid::subWidth, rng.chance (0.15f) ? rng.range (0.2f, 0.8f) : 0.0f);

    // 起音：多数立刻起音，少数软起音
    put (t, pid::subAttack, rng.chance (0.18f) ? rng.range (3.0f, 25.0f) : 0.0f);

    // 保持段
    put (t, pid::subHold, rng.chance (0.2f) ? rng.range (5.0f, 40.0f) : 0.0f);

    // 两级下坠：硬派与长音色偶尔用
    if (rng.chance (0.2f))
    {
        put (t, pid::subStage2Amt, rng.range (4.0f, 16.0f));
        put (t, pid::subStage2Time, rng.range (30.0f, 60.0f));
    }
    else
    {
        put (t, pid::subStage2Amt, 0.0f);
        put (t, pid::subStage2Time, 45.0f);
    }

    //--- 滤波 -------------------------------------------------------------
    const bool dark = rng.chance (dna.darkChance + darkBias);
    if (dark)
    {
        put (t, pid::subFilterHz, rng.range (250.0f, 2200.0f));
        put (t, pid::subFilterEnv, rng.range (-0.5f, 0.6f));
        put (t, pid::subFilterQ, rng.range (0.7f, 1.8f));
        put (t, pid::subFilterType, 0.0f);
    }
    else
    {
        put (t, pid::subFilterHz, 20000.0f);
        put (t, pid::subFilterEnv, 0.0f);
        put (t, pid::subFilterQ, 0.71f);
        put (t, pid::subFilterType, 0.0f);
    }

    // 带通 / 高通：少数音色（做"鼻音"或纯音头）
    if (rng.chance (0.12f))
    {
        put (t, pid::subFilterType, rng.chance (0.5f) ? 1.0f : 2.0f);
        put (t, pid::subFilterHz, rng.range (120.0f, 900.0f));
        put (t, pid::subFilterQ, rng.range (1.5f, 4.0f));
    }

    //--- 噪声瞬态层 -------------------------------------------------------
    if (rng.chance (0.45f))
    {
        put (t, pid::subNoiseAmt, rng.range (0.05f, 0.4f));
        put (t, pid::subNoiseDecay, rng.range (3.0f, 25.0f));
        put (t, pid::subNoiseHp, rng.range (250.0f, 2500.0f));
    }
    else
    {
        put (t, pid::subNoiseAmt, 0.0f);
        put (t, pid::subNoiseDecay, 12.0f);
        put (t, pid::subNoiseHp, 400.0f);
    }

    //--- Click 层 ---------------------------------------------------------
    // 第 1 层几乎总是开（提供音头），其余层按概率
    for (int i = 0; i < kNumClickLayers; ++i)
    {
        const juce::String pre = "click" + juce::String (i + 1) + "_";
        const bool on = (i == 0) || rng.chance (0.3f);

        put (t, pre + "on", on ? 1.0f : 0.0f);
        put (t, pre + "level", rng.range (0.15f, 0.45f));
        put (t, pre + "pitch", rng.range (-5.0f, 7.0f));
        put (t, pre + "decay", rng.range (dna.clickDecayLo, dna.clickDecayHi));
        put (t, pre + "lp", dark ? rng.range (4000.0f, 12000.0f) : 20000.0f);
        put (t, pre + "hp", rng.range (40.0f, 400.0f));
        put (t, pre + "start", 0.0f);
        put (t, pre + "end", 1.0f);
        put (t, pre + "fadein", rng.chance (0.3f) ? rng.range (0.5f, 3.0f) : 0.0f);
        put (t, pre + "shape", rng.range (0.35f, 0.65f));
        put (t, pre + "wavetune", rng.range (300.0f, 1200.0f));
        put (t, pre + "keytrack", 0.0f);
        put (t, pre + "wave", 0.0f);        // 默认用采样
    }

    //--- 失真 -------------------------------------------------------------
    //--- 染色（10 种硬件模型）--------------------------------------------
    // 按风格选择"合适的"模型，而不是纯随机 —— 比如 Ambient 不该出现 Fuzz，
    // Hardcore 不该出现 Console 这种几乎不失真的。
    const bool useDist = rng.chance (dna.distChance);
    put (t, pid::distOn, useDist ? 1.0f : 0.0f);

    if (useDist)
    {
        // 每个风格一份候选模型表（用枚举序号）
        static const int tubeLike[]    { 0, 4, 5 };          // Tube / Transformer / Console
        static const int tapeLike[]    { 1, 5, 6 };          // Tape / Console / Germanium
        static const int dirtLike[]    { 2, 3, 6, 7 };       // Hard / Fold / Germanium / Fuzz
        static const int extremeLike[] { 2, 3, 7, 8 };       // Hard / Fold / Fuzz / Radio

        const int* pool = tubeLike;
        int poolSize = 3;

        if (style == "Hardcore" || style == "Hardstyle" || style == "Dubstep")
            { pool = extremeLike; poolSize = 4; }
        else if (style == "Techno" || style == "DnB" || style == "EDM")
            { pool = dirtLike; poolSize = 4; }
        else if (style == "LoFi" || style == "HipHop")
            { pool = tapeLike; poolSize = 3; }

        put (t, pid::distType, (float) pool[rng.nextInt (poolSize)]);
        put (t, pid::distDrive, rng.range (2.0f, 10.0f));
        put (t, pid::distMix, rng.range (0.15f, 0.6f));

        // 偏置：偶次谐波。磁带类不喜欢太脏，脏类喜欢。
        const bool dirty = (pool == dirtLike || pool == extremeLike);
        put (t, pid::distBias, dirty ? rng.range (-0.12f, 0.18f)
                                     : rng.range (-0.05f, 0.06f));

        // Tone：暗色风格偏暗
        put (t, pid::distTone, dark ? rng.range (0.25f, 0.45f)
                                    : rng.range (0.45f, 0.7f));
    }
    else
    {
        put (t, pid::distType, 0.0f);
        put (t, pid::distDrive, 3.0f);
        put (t, pid::distMix, 0.0f);
        put (t, pid::distBias, 0.0f);
        put (t, pid::distTone, 0.5f);
    }

    put (t, pid::distAutoGain, 1.0f);

    //--- 压缩 / 限幅 ------------------------------------------------------
    put (t, pid::compOn, rng.chance (0.5f) ? 1.0f : 0.0f);
    put (t, pid::compThresh, rng.range (-24.0f, -8.0f));
    put (t, pid::compRatio, rng.range (2.0f, 8.0f));
    put (t, pid::compAttack, rng.range (1.0f, 20.0f));
    put (t, pid::compRelease, rng.range (40.0f, 250.0f));
    put (t, pid::compMakeup, rng.range (0.0f, 6.0f));
    put (t, pid::compScHp, rng.range (20.0f, 200.0f));
    put (t, pid::compLookahead, 0.0f);
    put (t, pid::limOn, 1.0f);
    put (t, pid::limCeiling, -0.3f);

    //--- 瞬态整形 ---------------------------------------------------------
    put (t, pid::transOn, rng.chance (0.3f) ? 1.0f : 0.0f);
    put (t, pid::transAttack, rng.range (-0.2f, 0.5f));
    put (t, pid::transSustain, rng.range (-0.3f, 0.2f));

    //--- 力度 -------------------------------------------------------------
    put (t, pid::velAmount, rng.range (0.4f, 0.8f));
    put (t, pid::velTarget, 0.0f);

    //--- 输出 -------------------------------------------------------------
    // 保持余量，避免预设一上来就顶到限幅
    put (t, pid::masterGain, rng.range (-2.5f, 0.5f));

    //--- EQ：多数预设开 1~2 段做轻微塑形 ---------------------------------
    for (int b = 0; b < 8; ++b)
    {
        const juce::String pre = "eq" + juce::String (b + 1) + "_";
        put (t, pre + "on", 0.0f);
        put (t, pre + "gain", 0.0f);
        put (t, pre + "q", 0.9f);
        put (t, pre + "slope", 0.0f);
    }

    if (rng.chance (0.55f))
    {
        // 低频塑形（第 2/3 段：Sub Body / Punch）
        const int band = rng.chance (0.5f) ? 1 : 2;
        const juce::String pre = "eq" + juce::String (band + 1) + "_";
        put (t, pre + "on", 1.0f);
        put (t, pre + "gain", rng.range (1.5f, 4.5f));
        put (t, pre + "q", rng.range (0.7f, 1.4f));
    }

    if (dark && rng.chance (0.6f))
    {
        // 高频滚降（第 7/8 段：Attack / Air）
        const int band = rng.chance (0.5f) ? 6 : 7;
        const juce::String pre = "eq" + juce::String (band + 1) + "_";
        put (t, pre + "on", 1.0f);
        put (t, pre + "gain", rng.range (-4.0f, -1.5f));
        put (t, pre + "q", 0.9f);
    }

    return t;
}

//==============================================================================
MenuTree buildMenuTree()
{
    MenuTree tree;

    const auto& all = getAll();

    for (int i = 0; i < (int) all.size(); ++i)
    {
        const auto& e = all[(size_t) i];

        // 找（或新建）风格
        MenuTree::Style* style = nullptr;
        for (auto& s : tree.styles)
            if (s.name == e.style) { style = &s; break; }

        if (style == nullptr)
        {
            tree.styles.push_back ({ e.style, {} });
            style = &tree.styles.back();
        }

        // 找（或新建）子类
        MenuTree::Category* cat = nullptr;
        for (auto& c : style->categories)
            if (c.name == e.category) { cat = &c; break; }

        if (cat == nullptr)
        {
            style->categories.push_back ({ e.category, {} });
            cat = &style->categories.back();
        }

        cat->presetIndices.push_back (i);
    }

    return tree;
}

} // namespace FactoryPresets
} // namespace kf
