#include "ModulationMatrix.h"

namespace kf
{

//==============================================================================
juce::StringArray ModulationMatrix::getSourceNames()
{
    return juce::StringArray {
        "Off",
        "ENV 1", "ENV 2", "ENV 3", "ENV 4",
        "LFO 1", "LFO 2",
        "Velocity", "Keytrack", "Random"
    };
}

juce::StringArray ModulationMatrix::getTargetNames()
{
    // ⚠️ 顺序必须与 Target 枚举**严格一致** ——
    // 下拉框用下标映射目标，错位会导致"选 A 实际调制 B"。
    return juce::StringArray {
        "Off", "EQ 1 Gain", "EQ 2 Gain", "EQ 3 Gain",
        "EQ 4 Gain", "EQ 5 Gain", "EQ 6 Gain", "EQ 7 Gain",
        "EQ 8 Gain", "Dist Drive", "Dist Mix", "Dist Tone",
        "Dist Bias", "Sub Pitch", "Sub Level", "Sub Filter Freq",
        "Sub Filter Q", "Sub Filter Env", "Sub Harmonics", "Sub Harm Tilt",
        "Sub Noise", "Sub Noise HP", "Sub Detune", "Sub Width",
        "Sub Punch", "Glide", "Sub Decay", "Sub Pitch Decay",
        "Sub Pitch Curve", "Sub Attack", "Sub Hold", "Sub Vel To Decay",
        "Sub Shape", "Sub Odd/Even", "Sub Sub Harm", "Sub Phase Rand",
        "Sub Pitch Delay", "Sub Stage 2", "Sub Stage 2 Time", "Sub Glide Curve",
        "Sub Noise Decay", "Click Level", "Click Decay", "Click HP",
        "Click LP", "Click Start", "Click End", "Click Fade In",
        "Click Shape", "Click Wave Tune", "EQ 1 Freq", "EQ 2 Freq",
        "EQ 3 Freq", "EQ 4 Freq", "EQ 5 Freq", "EQ 6 Freq",
        "EQ 7 Freq", "EQ 8 Freq", "EQ 1 Q", "EQ 2 Q",
        "EQ 3 Q", "EQ 4 Q", "EQ 5 Q", "EQ 6 Q",
        "EQ 7 Q", "EQ 8 Q", "Comp Threshold", "Comp Ratio",
        "Comp Attack", "Comp Release", "Comp Makeup", "Comp Sidechain HP",
        "Comp Lookahead", "Limiter Ceiling", "Transient Attack", "Transient Sustain",
        "Master Gain"
    };
}

//==============================================================================
ModulationMatrix::ModulationMatrix()
{
    // 默认给每条包络一个**有用的形状**，而不是平直线。
    //
    // 平直的 ENV 调制任何参数都等于"加一个常量偏移"，
    // 听感上是音色变了但没有动态 —— 用户会以为功能坏了。
    // 给一个"快起快落"的音头形状，一选就有明显效果。
    for (int i = 0; i < kNumEnvs; ++i)
    {
        auto& e = envs[(size_t) i];

        // ⚠️ 不能先 resetToFlat 再 addNode ——
        // resetToFlat 已经在 t=0 和 t=1 放了节点，
        // 再 addNode 同时间点会产生**重复节点**，
        // 求值行为变得不确定（实测节点表里出现两个 t=0）。
        // 从空表开始，自己铺全部节点。
        e.clearNodes();
        e.addNode (0.0f, 0.0f);

        // 音头型：0 -> 1（快速）-> 0（回落）。
        // 每条包络的**时长**不同（见 envLengthSeconds），
        // 所以这里曲线的归一化形状可以相似，实际快慢由时长决定。
        const float peakT = 0.10f + 0.08f * (float) i;
        const float fallT = 0.45f + 0.12f * (float) i;

        e.addNode (peakT, 1.0f);
        e.addNode (fallT, 0.0f);
        e.addNode (1.0f, 0.0f);
    }

    for (auto& r : envReaders)
        r.prepare (2048);

    for (auto& r : routes)
    {
        r.source.store (0);
        r.target.store (0);
        r.depth.store (0.0f);
    }

    // 默认给一条示例路由：ENV 1 -> EQ 2 Gain（音头抬中低频、然后回落）。
    //
    // 为什么给默认值：矩阵全空时用户看不出这个功能怎么用。
    // 给一条能听出效果的示例，比一篇说明文档更直观。
    //
    // ENV 1 默认形状 = 快起快落（音头型），配 EQ 2（40Hz 低频段）
    // 就得到经典的"音头有低频冲击、尾巴干净"。
    {
        Route r;
        r.source = Source::Env1;
        r.target = Target::Eq2Gain;
        r.depth  = 0.5f;
        setRoute (0, r);
    }

    // 4 条都要交给音频线程 —— 之前只 publish 了第 0 条，
    // 结果 ENV2/3/4 在音频线程读到的是空表（恒为 0），
    // 表现是"选了 ENV2 但没有任何效果"。
    for (int i = 0; i < kNumEnvs; ++i)
        publishEnvelope (i);
}

//==============================================================================
void ModulationMatrix::noteOn (float velocity, int midiNote) noexcept
{
    currentVelocity = juce::jlimit (0.0f, 1.0f, velocity);

    // 相对 C2（MIDI 36）的半音偏移，归一化到 -1..1（±24 半音）
    currentKeytrack = juce::jlimit (-1.0f, 1.0f,
                                    (float) (midiNote - 36) / 24.0f);

    currentRandom = juce::Random::getSystemRandom().nextFloat() * 2.0f - 1.0f;

    // 包络与 LFO 重新开始
    envTime = 0.0;

    for (int i = 0; i < kNumLfos; ++i)
    {
        const auto& st = lfoSettings[(size_t) i];

        if (st.resetOnTrigger)
        {
            // 回到**周期起点**。
            //
            // ⚠️ 这里不能设成 phaseOffset —— 读取时已经会加一次偏移，
            // 两处都加就变成 2×offset（实测 180° 偏移不反相，变成 360°=同相）。
            // 偏移只在一个地方应用：advance() 的读取位置。
            lfoPhase[(size_t) i] = 0.0;
        }
        // 否则保持当前相位自由运行 —— 不做任何修改
    }

    for (auto& v : lfoRandomValue)
        v = juce::Random::getSystemRandom().nextFloat() * 2.0f - 1.0f;

    // 块内插值的起点
    sourceStart.fill (0.0f);
    sourceEnd.fill (0.0f);
}

//==============================================================================
void ModulationMatrix::advance (int numSamples, double sampleRate) noexcept
{
    if (sampleRate <= 0.0 || numSamples <= 0)
        return;

    const double blockSeconds = (double) numSamples / sampleRate;

    //--- 1. 计算各源在**块末**的值
    std::array<float, (size_t) Source::kNumSources> now {};
    now.fill (0.0f);

    // 通用 ADSR：把包络当成"以秒为单位的时间轴"
    //
    // 现有 Envelope 是按归一化时间 0..1 求值的，这里把包络的时长
    // 定义为 envLengthSeconds，让用户能控制快慢。
    envTime += blockSeconds;

    // 每条包络用自己的时长换算归一化位置 ——
    // 这样"60ms 的音头 + 1s 的尾巴"可以同时存在。
    for (int i = 0; i < kNumEnvs; ++i)
    {
        const float len = envLengthSeconds[(size_t) i];
        const float norm = len > 1.0e-4 ? (float) (envTime / (double) len) : 1.0f;
        now[(size_t) Source::Env1 + (size_t) i] =
            envReaders[(size_t) i].read (juce::jlimit (0.0f, 1.0f, norm));
    }

    // LFO
    //
    // 宿主停止时**冻结**相位：否则停止播放后 LFO 继续走，
    // 参数会一直在飘（听起来像插件坏了，而且导出结果不确定）。
    if (hostPlaying)
    for (int i = 0; i < kNumLfos; ++i)
    {
        const auto& st = lfoSettings[(size_t) i];

        // 同步模式下由 BPM 和分度算出等效频率：
        //   division 是以全音符为单位的周期长度
        //   例：division=0.25（每拍一次），BPM=120 -> 每秒 2 拍 -> 2 Hz
        double effHz = (double) st.rateHz;
        if (st.sync)
        {
            const double beatsPerSecond = hostBpm / 60.0;
            const double wholeNotesPerSecond = beatsPerSecond / 4.0;   // 4 拍 = 1 全音符
            const double period = juce::jmax (1.0e-4, (double) st.division);
            effHz = wholeNotesPerSecond / period;
        }

        lfoPhase[(size_t) i] += effHz * blockSeconds;
        lfoPhase[(size_t) i] -= std::floor (lfoPhase[(size_t) i]);   // 0..1

        // 相位偏移只影响**读取位置**，不影响内部相位累加 ——
        // 这样调偏移时不会打断 LFO 的连续性。
        float ph = (float) lfoPhase[(size_t) i] + st.phaseOffset;
        ph -= std::floor (ph);
        float v = 0.0f;

        switch (st.shape)
        {
            case LfoSettings::Shape::Sine:
                v = std::sin (ph * juce::MathConstants<float>::twoPi);
                break;
            case LfoSettings::Shape::Triangle:
                v = 4.0f * std::abs (ph - 0.5f) - 1.0f;
                break;
            case LfoSettings::Shape::Saw:
                v = 2.0f * ph - 1.0f;
                break;
            case LfoSettings::Shape::Square:
                v = ph < 0.5f ? 1.0f : -1.0f;
                break;
            case LfoSettings::Shape::Random:
                // 每个周期取一个新的随机值（样本保持）
                if (ph < lastLfoPhase[(size_t) i])
                    lfoRandomValue[(size_t) i] =
                        juce::Random::getSystemRandom().nextFloat() * 2.0f - 1.0f;
                v = lfoRandomValue[(size_t) i];
                break;
            default: break;
        }

        lastLfoPhase[(size_t) i] = ph;
        now[(size_t) Source::Lfo1 + (size_t) i] = v;
    }

    now[(size_t) Source::Velocity]  = currentVelocity;
    now[(size_t) Source::Keytrack]  = currentKeytrack;
    now[(size_t) Source::Random]    = currentRandom;

    //--- 2. 记下块起始值（上一块的结束值），供块内插值
    sourceStart = sourceEnd;
    sourceEnd = now;

    //--- 3. 把各源的值按路由累加到目标
    targetValues.fill (0.0f);

    for (int i = 0; i < kMaxRoutes; ++i)
    {
        const auto s = (Source) routes[(size_t) i].source.load (std::memory_order_relaxed);
        const auto t = (Target) routes[(size_t) i].target.load (std::memory_order_relaxed);
        const float d = routes[(size_t) i].depth.load (std::memory_order_relaxed);

        if (s == Source::None || t == Target::None || std::abs (d) < 1.0e-4f)
            continue;

        const size_t si = (size_t) s;
        if (si >= (size_t) Source::kNumSources)
            continue;

        // 用块末值计算（块内插值由调用方按需处理）
        targetValues[(size_t) t] += now[si] * d;
    }

    // 钳制到 -1..1 —— 多条路由叠加可能超出，钳制避免目标被推到荒谬的值
    for (auto& v : targetValues)
        v = juce::jlimit (-1.0f, 1.0f, v);

    // 发布实时值供 UI 显示。
    // 注意 Keytrack/Random 是双极性(-1..1)，Velocity 是单极性(0..1) ——
    // UI 画指示条时会按源的极性决定基线位置。
    for (size_t i = 0; i < meterValues.size(); ++i)
        meterValues[i].store (now[i], std::memory_order_relaxed);
}

//==============================================================================
float ModulationMatrix::getValue (Target t) const noexcept
{
    const size_t i = (size_t) t;
    return i < targetValues.size() ? targetValues[i] : 0.0f;
}

float ModulationMatrix::getSourceValue (Source s) const noexcept
{
    const size_t i = (size_t) s;
    if (i >= sourceEnd.size())
        return 0.0f;
    return sourceEnd[i];
}

//==============================================================================
ModulationMatrix::Target ModulationMatrix::targetForParamID (const juce::String& id)
{
    // --- EQ：eq1_gain / eq3_freq / eq5_q ...
    if (id.startsWith ("eq"))
    {
        const int us = id.indexOfChar ('_');
        if (us > 2)
        {
            const int band = id.substring (2, us).getIntValue();      // 1..8
            const auto kind = id.substring (us + 1);

            if (band >= 1 && band <= 8)
            {
                if (kind == "gain") return (Target) ((int) Target::Eq1Gain + band - 1);
                if (kind == "freq") return (Target) ((int) Target::Eq1Freq + band - 1);
                if (kind == "q")    return (Target) ((int) Target::Eq1Q    + band - 1);
            }
        }
        return Target::None;
    }

    // --- 失真
    if (id == "dist_drive") return Target::DistDrive;
    if (id == "dist_mix")   return Target::DistMix;
    if (id == "dist_tone")  return Target::DistTone;
    if (id == "dist_bias")  return Target::DistBias;

    // --- 演奏（Tune / VelAmount 在触发时读取，不适合调制，见头文件说明）
    if (id == "glide")      return Target::Glide;

    // --- Sub（逐一对应，漏掉一个就会变成"旋钮上没有环"）
    if (id == "sub_pitchamt")   return Target::SubPitch;
    if (id == "sub_level")      return Target::SubLevel;
    if (id == "sub_filterhz")   return Target::SubFilterFreq;
    if (id == "sub_filterq")    return Target::SubFilterQ;
    if (id == "sub_filterenv")  return Target::SubFilterEnv;
    if (id == "sub_harmonics")  return Target::SubHarmonics;
    if (id == "sub_harmtilt")   return Target::SubHarmTilt;
    if (id == "sub_noiseamt")   return Target::SubNoiseAmt;
    if (id == "sub_noisehp")    return Target::SubNoiseHp;
    if (id == "sub_detune")     return Target::SubDetune;
    if (id == "sub_width")      return Target::SubWidth;
    if (id == "sub_amppunch")   return Target::SubAmpPunch;
    if (id == "sub_decay")      return Target::SubDecay;
    if (id == "sub_pitchdecay") return Target::SubPitchDecay;
    if (id == "sub_pitchcurve") return Target::SubPitchCurve;
    if (id == "sub_attack")     return Target::SubAttack;
    if (id == "sub_hold")       return Target::SubHold;
    if (id == "sub_veltodecay") return Target::SubVelToDecay;
    if (id == "sub_shape")      return Target::SubShape;
    if (id == "sub_oddeven")    return Target::SubOddEven;
    if (id == "sub_subharm")    return Target::SubSubHarm;
    if (id == "sub_phaserand")  return Target::SubPhaseRand;
    if (id == "sub_pitchdelay") return Target::SubPitchDelay;
    if (id == "sub_stage2amt")  return Target::SubStage2Amt;
    if (id == "sub_stage2time") return Target::SubStage2Time;
    if (id == "sub_glidecurve") return Target::SubGlideCurve;
    if (id == "sub_noisedecay") return Target::SubNoiseDecay;

    // --- Click：4 层共用目标（旋钮上只看后缀）
    //     注意 pitch / keytrack 不在此列 —— 它们在触发瞬间读取，调制无效。
    if (id.startsWith ("click"))
    {
        if (id.endsWith ("_level"))    return Target::ClickLevel;
        if (id.endsWith ("_decay"))    return Target::ClickDecay;
        if (id.endsWith ("_hp"))       return Target::ClickHp;
        if (id.endsWith ("_lp"))       return Target::ClickLp;
        if (id.endsWith ("_start"))    return Target::ClickStart;
        if (id.endsWith ("_end"))      return Target::ClickEnd;
        if (id.endsWith ("_fadein"))   return Target::ClickFadeIn;
        if (id.endsWith ("_shape"))    return Target::ClickShape;
        if (id.endsWith ("_wavetune")) return Target::ClickWaveTune;
        return Target::None;
    }

    // --- 动态
    if (id == "comp_thresh")     return Target::CompThresh;
    if (id == "comp_ratio")      return Target::CompRatio;
    if (id == "comp_attack")     return Target::CompAttack;
    if (id == "comp_release")    return Target::CompRelease;
    if (id == "comp_makeup")     return Target::CompMakeup;
    if (id == "comp_schp")       return Target::CompScHp;
    if (id == "comp_lookahead")  return Target::CompLookahead;
    if (id == "limiter_ceiling") return Target::LimCeiling;

    // --- 瞬态
    if (id == "trans_attack")  return Target::TransAttack;
    if (id == "trans_sustain") return Target::TransSustain;

    if (id == "master_gain") return Target::MasterGain;

    return Target::None;
}

float ModulationMatrix::modulationRangeForTarget (Target t)
{
    // 这些数字必须与 PluginProcessor 里实际用的量程映射一致，
    // 否则画出来的"可达范围"环会与实际听感对不上。
    switch (t)
    {
        // EQ 增益：实际 ±24dB，参数量程 -18..+18（共 36dB）
        case Target::Eq1Gain: case Target::Eq2Gain:
        case Target::Eq3Gain: case Target::Eq4Gain:
        case Target::Eq5Gain: case Target::Eq6Gain:
        case Target::Eq7Gain: case Target::Eq8Gain:
            return 24.0f / 36.0f;

        // EQ 频率与 Q 用倍频程/比例缩放，没有固定归一化幅度
        case Target::Eq1Freq: case Target::Eq2Freq: case Target::Eq3Freq:
        case Target::Eq4Freq: case Target::Eq5Freq: case Target::Eq6Freq:
        case Target::Eq7Freq: case Target::Eq8Freq:
            return 0.35f;

        case Target::Eq1Q: case Target::Eq2Q: case Target::Eq3Q: case Target::Eq4Q:
        case Target::Eq5Q: case Target::Eq6Q: case Target::Eq7Q: case Target::Eq8Q:
            return 3.0f / 12.0f;                // ±3，量程 0.1..12

        case Target::DistDrive:   return 10.0f / 19.0f;
        case Target::DistMix:     return 1.0f;
        case Target::DistTone:    return 1.0f;
        case Target::DistBias:    return 0.5f;

        case Target::Glide:       return 200.0f / 2000.0f;

        case Target::SubPitch:     return 24.0f / 96.0f;
        case Target::SubLevel:     return 1.0f;
        case Target::SubFilterQ:   return 4.0f / 7.5f;
        case Target::SubFilterEnv: return 1.0f;
        case Target::SubHarmonics: return 8.0f / 8.0f;
        case Target::SubHarmTilt:  return 1.0f;
        case Target::SubNoiseAmt:  return 1.0f;
        case Target::SubDetune:    return 25.0f / 50.0f;
        case Target::SubWidth:     return 1.0f / 2.0f;
        case Target::SubAmpPunch:  return 1.0f;
        case Target::SubDecay:     return 0.3f;   // ±300ms
        case Target::SubPitchDecay:return 0.3f;
        case Target::SubPitchCurve:return 1.0f;
        case Target::SubAttack:    return 0.05f;  // ±50ms
        case Target::SubHold:      return 0.05f;
        case Target::SubVelToDecay:return 1.0f;
        case Target::SubShape:     return 1.0f;
        case Target::SubOddEven:   return 1.0f;
        case Target::SubSubHarm:   return 1.0f;
        case Target::SubPhaseRand: return 1.0f;
        case Target::SubPitchDelay:return 0.05f;
        case Target::SubStage2Amt: return 1.0f;
        case Target::SubStage2Time:return 0.3f;
        case Target::SubGlideCurve:return 1.0f;
        case Target::SubNoiseDecay:return 0.3f;

        case Target::ClickLevel:  return 1.0f;
        case Target::ClickDecay:  return 0.15f / 2.0f;
        case Target::ClickFadeIn: return 5.0f / 20.0f;   // ms，量程 0..20
        case Target::ClickShape:  return 1.0f;
        case Target::ClickWaveTune:return 0.35f;   // 倍频程缩放
        // 采样区间是 0..1 的归一化位置
        case Target::ClickStart:
        case Target::ClickEnd:    return 0.5f;
        // 频率类用倍频程缩放
        case Target::ClickHp:
        case Target::ClickLp:
        case Target::SubFilterFreq:
        case Target::SubNoiseHp:  return 0.35f;

        case Target::CompThresh:  return 12.0f / 40.0f;
        case Target::CompRatio:   return 4.0f / 20.0f;   // 1..20
        case Target::CompAttack:  return 0.05f;          // 量程 0..200ms
        case Target::CompRelease: return 0.2f;           // 量程 0..2s
        case Target::CompMakeup:  return 12.0f / 24.0f;
        case Target::CompScHp:    return 0.35f;
        case Target::CompLookahead:return 0.25f;
        case Target::LimCeiling:  return 6.0f / 12.0f;

        case Target::TransAttack:  return 1.0f;
        case Target::TransSustain: return 1.0f;

        case Target::MasterGain:  return 12.0f / 36.0f;

        default: return 0.0f;
    }
}

//==============================================================================
ModulationMatrix::Route ModulationMatrix::getRoute (int index) const noexcept
{
    Route r;

    if (index < 0 || index >= kMaxRoutes)
        return r;

    r.source = (Source) routes[(size_t) index].source.load (std::memory_order_relaxed);
    r.target = (Target) routes[(size_t) index].target.load (std::memory_order_relaxed);
    r.depth  = routes[(size_t) index].depth.load (std::memory_order_relaxed);

    return r;
}

void ModulationMatrix::setRoute (int index, const Route& r)
{
    if (index < 0 || index >= kMaxRoutes)
        return;

    routes[(size_t) index].source.store ((int) r.source, std::memory_order_relaxed);
    routes[(size_t) index].target.store ((int) r.target, std::memory_order_relaxed);
    routes[(size_t) index].depth.store (r.depth, std::memory_order_relaxed);

    recomputeActiveFlag();
}

void ModulationMatrix::recomputeActiveFlag() noexcept
{
    bool any = false;
    for (int i = 0; i < kMaxRoutes && ! any; ++i)
        if (getRoute (i).isActive())
            any = true;
    anyActive.store (any, std::memory_order_relaxed);
}

void ModulationMatrix::clearAllRoutes()
{
    for (int i = 0; i < kMaxRoutes; ++i)
        setRoute (i, Route {});
}

//==============================================================================
void ModulationMatrix::publishEnvelope (int index)
{
    const int i = juce::jlimit (0, kNumEnvs - 1, index);

    envs[(size_t) i].rebuildTable (2048);

    std::vector<float> table (2048);
    for (int k = 0; k < 2048; ++k)
        table[(size_t) k] = envs[(size_t) i].getValueAt ((float) k / 2047.0f);

    envReaders[(size_t) i].swapTable (std::move (table));
}

//==============================================================================
ModulationMatrix::LfoSettings ModulationMatrix::getLfo (int index) const noexcept
{
    if (index < 0 || index >= kNumLfos)
        return {};
    return lfoSettings[(size_t) index];
}

void ModulationMatrix::setLfo (int index, const LfoSettings& s)
{
    if (index < 0 || index >= kNumLfos)
        return;
    lfoSettings[(size_t) index] = s;
}

//==============================================================================
juce::String ModulationMatrix::toString() const
{
    juce::DynamicObject::Ptr o = new juce::DynamicObject();

    juce::Array<juce::var> routeArr;
    for (int i = 0; i < kMaxRoutes; ++i)
    {
        const auto r = getRoute (i);
        if (! r.isActive())
            continue;

        juce::DynamicObject::Ptr ro = new juce::DynamicObject();
        // ⚠️ 必须记录**原始槽位**。
        // 只存 (源,目标,深度) 的话，恢复时会按顺序压紧到 0,1,2...，
        // 槽位 3 的路由会跑到槽位 1 —— 用户看到的顺序全乱了
        // （实测：route 3 恢复成了 route 1）。
        ro->setProperty ("i", i);
        ro->setProperty ("s", (int) r.source);
        ro->setProperty ("t", (int) r.target);
        ro->setProperty ("d", r.depth);
        routeArr.add (juce::var (ro.get()));
    }
    o->setProperty ("routes", routeArr);

    juce::Array<juce::var> envArr;
    for (const auto& e : envs)
        envArr.add (e.toString());
    o->setProperty ("envs", envArr);

    juce::Array<juce::var> lfoArr;
    for (const auto& l : lfoSettings)
    {
        juce::DynamicObject::Ptr lo = new juce::DynamicObject();
        lo->setProperty ("shape", (int) l.shape);
        lo->setProperty ("rate", l.rateHz);
        lo->setProperty ("sync", l.sync);
        lo->setProperty ("div", l.division);
        lo->setProperty ("rst", l.resetOnTrigger);
        lo->setProperty ("poff", l.phaseOffset);
        lfoArr.add (juce::var (lo.get()));
    }
    o->setProperty ("lfos", lfoArr);
    juce::Array<juce::var> lenArr;
    for (float l : envLengthSeconds)
        lenArr.add (l);
    o->setProperty ("envLens", lenArr);

    // 兼容旧存档：早期版本只有一个全局 "envLen"
    o->setProperty ("envLen", envLengthSeconds[0]);

    return juce::JSON::toString (juce::var (o.get()), false);
}

bool ModulationMatrix::fromString (const juce::String& s)
{
    if (s.isEmpty())
        return false;

    const auto v = juce::JSON::parse (s);
    auto* o = v.getDynamicObject();
    if (o == nullptr)
        return false;

    clearAllRoutes();

    if (auto* arr = o->getProperty ("routes").getArray())
    {
        int fallback = 0;
        for (const auto& rv : *arr)
        {
            auto* ro = rv.getDynamicObject();
            if (ro == nullptr)
                continue;

            Route r;
            r.source = (Source) (int) ro->getProperty ("s");
            r.target = (Target) (int) ro->getProperty ("t");
            r.depth  = (float) (double) ro->getProperty ("d");

            if (r.source >= Source::kNumSources) r.source = Source::None;
            if (r.target >= Target::kNumTargets) r.target = Target::None;

            // 优先用存档里的原始槽位；老版本没有 "i" 字段时退化为顺序填充
            int slot = ro->hasProperty ("i") ? (int) ro->getProperty ("i")
                                             : fallback++;
            if (slot < 0 || slot >= kMaxRoutes)
                continue;

            setRoute (slot, r);
        }
    }

    if (auto* arr = o->getProperty ("envs").getArray())
        for (int i = 0; i < juce::jmin (kNumEnvs, arr->size()); ++i)
        {
            envs[(size_t) i].fromString (arr->getReference (i).toString());
            publishEnvelope (i);
        }

    if (auto* arr = o->getProperty ("lfos").getArray())
        for (int i = 0; i < juce::jmin (kNumLfos, arr->size()); ++i)
        {
            auto* lo = arr->getReference (i).getDynamicObject();
            if (lo == nullptr) continue;
            LfoSettings st;
            st.shape  = (LfoSettings::Shape) (int) lo->getProperty ("shape");
            st.rateHz = (float) (double) lo->getProperty ("rate");
            if (lo->hasProperty ("sync")) st.sync = (bool) lo->getProperty ("sync");
            if (lo->hasProperty ("div"))  st.division = (float) (double) lo->getProperty ("div");
            if (lo->hasProperty ("rst"))  st.resetOnTrigger = (bool) lo->getProperty ("rst");
            if (lo->hasProperty ("poff")) st.phaseOffset = (float) (double) lo->getProperty ("poff");
            setLfo (i, st);
        }

    if (auto* arr = o->getProperty ("envLens").getArray())
    {
        for (int i = 0; i < juce::jmin (kNumEnvs, arr->size()); ++i)
            setEnvelopeLengthSeconds (i, (float) (double) arr->getReference (i));
    }
    else if (o->hasProperty ("envLen"))
    {
        // 旧存档只有全局时长 —— 全部包络沿用同一个值，
        // 保持载入后的听感与存档时一致。
        const float legacy = (float) (double) o->getProperty ("envLen");
        for (int i = 0; i < kNumEnvs; ++i)
            setEnvelopeLengthSeconds (i, legacy);
    }

    recomputeActiveFlag();
    return true;
}

} // namespace kf
