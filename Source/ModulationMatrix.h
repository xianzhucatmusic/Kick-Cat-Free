#pragma once

#include "Envelope.h"
#include <array>
#include <atomic>
#include <vector>

namespace kf
{

//==============================================================================
/** 调制矩阵：让任意调制源去控制任意目标参数。
 *
 *  ## 为什么要做这个
 *
 *  原先的调制是**硬接线**的：几条固定的线路写死在代码里
 *  （Pitch 包络 → Sub 音高、Amp 包络 → 音量、滤波包络 → 截止频率）。
 *  用户只能调"量"，不能改"连到哪"。
 *
 *  Vital / Serum 那种做法是**通用矩阵**：任何源可以连到任何目标，
 *  音头抬中频、音尾切低通这类操作就成了配置问题而不是代码问题。
 *
 *  ## 架构
 *
 *  ```
 *   调制源（Source）              调制矩阵            目标（Target）
 *   ────────────────             ────────            ──────────────
 *   ENV 1..4（通用 ADSR）  ──┐
 *   VELOCITY               ──┤    [源, 目标, 深度]     EQ1..8 Gain
 *   KEYTRACK               ──┼──> 最多 kMaxRoutes 条 ─> Dist Drive
 *   LFO 1..2               ──┤                         Filter Freq
 *   RANDOM                 ──┘                         Sub Pitch
 *                                                      Sub Level
 *  ```
 *
 *  ## 实现要点
 *
 *  1. **调制值每块算一次，块内线性插值**
 *     严格逐样本调制需要重写所有 DSP 的系数计算，代价太大。
 *     块级 + 线性插值在 512 样本块下已经听不出阶梯感
 *     （最大调制速率远低于块率）。
 *
 *  2. **目标参数在原有值上叠加偏移**
 *     调制不替换参数值，而是加一个偏移量。深度为 0 时完全等价于旧行为，
 *     所以**旧预设不需要迁移**。
 *
 *  3. **范围钳制在目标自身的合法区间内**
 *     调制不能把参数推出物理合理范围（比如 EQ 增益必须留在 ±24 dB）。
 */
class ModulationMatrix
{
public:
    //==========================================================================
    /** 通用 ADSR 包络的数量。
     *
     *  与原有的 Pitch/Amp 包络分开 —— 那两个是"结构性的"（决定底鼓基本形态），
     *  这里的 4 个是"表现性的"（做音色细节）。混在一起会让 UI 和语义都变乱。 */
    static constexpr int kNumEnvs = 4;

    /** LFO 数量。 */
    static constexpr int kNumLfos = 2;

    /** 最大路由条数。够用且不会让 UI 失控。 */
    static constexpr int kMaxRoutes = 12;

    //==========================================================================
    /** 调制源。 */
    enum class Source
    {
        None = 0,
        Env1, Env2, Env3, Env4,     // 4 个通用 ADSR
        Lfo1, Lfo2,                 // 2 个 LFO
        Velocity,                   // MIDI 力度
        Keytrack,                   // 音高相对 C2 的偏移
        Random,                     // 每次触发的随机值
        kNumSources
    };

    /** 调制目标。
     *
     *  只开放**听感明显**的目标。理论上可以开放全部 153 个参数，
     *  但那样 UI 会变得不可用，而且大部分组合没有意义。 */
    enum class Target
    {
        None = 0,

        //--- EQ（8 段增益）
        Eq1Gain, Eq2Gain, Eq3Gain, Eq4Gain,
        Eq5Gain, Eq6Gain, Eq7Gain, Eq8Gain,

        //--- 失真
        DistDrive,                  // 驱动量
        DistMix,                    // 干湿比
        DistTone,                   // 音色（高频）
        DistBias,                   // 偏置（不对称性）

        //--- Sub 振荡器
        SubPitch,                   // 音高包络深度（半音）
        SubLevel,                   // 音量
        SubFilterFreq,              // 低通截止
        SubFilterQ,                 // 滤波 Q
        SubFilterEnv,               // 滤波包络量
        SubHarmonics,               // 泛音数量
        SubHarmTilt,                // 泛音衰减速度（明亮度）
        SubNoiseAmt,                // 噪声成分
        SubNoiseHp,                 // 噪声高通
        SubDetune,                  // 失谐
        SubWidth,                   // 立体声宽度
        SubAmpPunch,                // 音量包络的"冲击"形状

        //--- 演奏
        Glide,                      // 滑音时间
        // 注意：没有 Tune / VelAmount。
        // 它们在**触发瞬间**读取（handleMidi 路径），而调制值在
        // processBlock 里算 —— 会延迟一个块。做进去就是"有效果但
        // 时序不对"的半成品，和 Click Pitch 同理，宁可不做。

        //--- Sub 包络
        SubDecay,                   // 音量包络时长
        SubPitchDecay,              // 音高包络时长
        SubPitchCurve,              // 音高下滑曲线
        SubAttack,                  // 起始渐入
        SubHold,                    // 保持段
        SubVelToDecay,              // 力度->时长

        //--- Sub 波形细节
        SubShape,                   // 波形形状
        SubOddEven,                 // 奇偶谐波平衡
        SubSubHarm,                 // 次谐波量
        SubPhaseRand,               // 相位随机量
        SubPitchDelay,              // 音高包络延迟
        SubStage2Amt,               // 第二阶段量
        SubStage2Time,              // 第二阶段时间
        SubGlideCurve,              // 滑音曲线

        //--- Sub 噪声
        SubNoiseDecay,              // 噪声衰减

        //--- Click（4 层共用）
        ClickLevel,                 // 音量
        ClickDecay,                 // 衰减
        ClickHp,                    // 高通
        ClickLp,                    // 低通
        ClickStart,                 // 采样起点
        ClickEnd,                   // 采样终点
        ClickFadeIn,                // 渐入
        ClickShape,                 // 波形形状
        ClickWaveTune,              // 波形调音
        // 注意：没有 "Click Pitch" / "Click Keytrack"。
        // 这两个在**触发瞬间**就烘焙进播放速率了，
        // 渲染阶段不读 —— 做成调制目标会是"看起来能调、实际没用"的旋钮。

        //--- EQ
        Eq1Freq, Eq2Freq, Eq3Freq, Eq4Freq,
        Eq5Freq, Eq6Freq, Eq7Freq, Eq8Freq,
        Eq1Q, Eq2Q, Eq3Q, Eq4Q,
        Eq5Q, Eq6Q, Eq7Q, Eq8Q,

        //--- 动态
        CompThresh,                 // 压缩阈值
        CompRatio,                  // 压缩比
        CompAttack,                 // 压缩起音
        CompRelease,                // 压缩释放
        CompMakeup,                 // 压缩补偿增益
        CompScHp,                   // 侧链高通
        CompLookahead,              // 前瞻
        LimCeiling,                 // 限幅上限

        //--- 瞬态整形
        TransAttack,                // 起音强度
        TransSustain,               // 延音强度

        //--- 全局
        MasterGain,                 // 总输出

        kNumTargets
    };

    static juce::StringArray getSourceNames();
    static juce::StringArray getTargetNames();

    /** 参数 ID -> 调制目标的映射。
     *
     *  UI 用它来决定"哪个旋钮该画调制环"。
     *  返回 Target::None 表示这个参数不可被调制。
     *
     *  ⚠️ Click 层的 4 个实例共用一个目标（比如 4 个 Level 都映射到
     *  ClickLevel），所以映射是"多对一"，不需要反向查询。 */
    static Target targetForParamID (const juce::String& paramID);

    /** 目标的最大调制幅度（归一化，用于画"可达范围"环）。
     *
     *  取值来自各处实际应用的量程映射 —— 比如 EQ 增益是 ±24dB，
     *  而 EQ 参数本身的量程是 ±18dB（归一化满量程 36dB），
     *  所以归一化幅度 = 24/36 = 0.667。 */
    static float modulationRangeForTarget (Target t);

    //==========================================================================
    /** 一条路由。 */
    struct Route
    {
        Source source = Source::None;
        Target target = Target::None;
        float  depth  = 0.0f;       // -1 .. +1，相对目标量程的比例

        bool isActive() const noexcept
        {
            return source != Source::None && target != Target::None
                   && std::abs (depth) > 1.0e-4f;
        }
    };

    //==========================================================================
    ModulationMatrix();

    /** 重新触发：所有包络与 LFO 从头开始。 */
    void noteOn (float velocity, int midiNote) noexcept;

    /** 推进一个块的调制计算。
     *  @param numSamples  块大小
     *  @param sampleRate  采样率
     *
     *  计算完结果存在内部，用 getValue() 查询。 */
    void advance (int numSamples, double sampleRate) noexcept;

    /** 由处理器每块调用，传入宿主当前速度。
     *
     *  @param bpm        宿主 BPM（<=0 表示宿主没提供，退回 120）
     *  @param isPlaying  宿主是否在播放 */
    void setHostTempo (double bpm, bool isPlaying) noexcept
    {
        hostBpm = bpm > 1.0 ? juce::jlimit (20.0, 400.0, bpm) : 120.0;
        hostPlaying = isPlaying;
    }

    /** 取某个目标在当前块的调制偏移（已加总所有连到它的路由）。
     *  @return 归一化偏移 -1..+1，调用方按目标量程换算 */
    float getValue (Target t) const noexcept;

    /** 取某个源当前的原始值（供 UI 显示）。 */
    float getSourceValue (Source s) const noexcept;

    /** 供 UI 读取的实时值快照（音频线程写，UI 线程读）。
     *
     *  用原子 float 而不是加锁 —— 可视化丢一两帧无所谓，
     *  但绝不能因为画图让音频线程等锁。 */
    float getMeterValue (Source s) const noexcept
    {
        const size_t i = (size_t) s;
        if (i >= meterValues.size())
            return 0.0f;
        return meterValues[i].load (std::memory_order_relaxed);
    }

    /** 当前是否有任何激活路由（供 UI 显示提示）。 */
    bool isIdle() const noexcept { return ! hasAnyActiveRoute(); }

    //==========================================================================
    // 路由配置（UI 线程读写，用原子指针交换避免音频线程竞争）

    int getNumRoutes() const noexcept { return kMaxRoutes; }
    Route getRoute (int index) const noexcept;
    void setRoute (int index, const Route& r);

    /** 是否有任何激活的路由（没有时可以整块跳过调制计算）。 */
    bool hasAnyActiveRoute() const noexcept { return anyActive.load (std::memory_order_relaxed); }

    //==========================================================================
    // 通用包络编辑（UI 线程）

    Envelope& getEnvelope (int index) noexcept
    {
        return envs[(size_t) juce::jlimit (0, kNumEnvs - 1, index)];
    }

    /** 把 UI 线程编辑过的包络交给音频线程（无锁交接）。 */
    void publishEnvelope (int index);

    /** LFO 设置。 */
    struct LfoSettings
    {
        enum class Shape { Sine = 0, Triangle, Saw, Square, Random, kNumShapes };
        Shape shape = Shape::Sine;

        /** 自由运行的频率（Hz）。sync 关闭时使用。 */
        float rateHz = 2.0f;

        /** 是否跟随宿主速度。
         *
         *  开启后 rateHz 被忽略，改用 division 决定"每小节几个周期"。 */
        bool sync = false;

        /** 同步分度：以**全音符**为单位。
         *
         *  1.0 = 每小节一次，0.5 = 每半小节，0.25 = 每拍，
         *  2.0 = 每两小节。底鼓常见的"每 4 拍扫一次"就是 0.25。 */
        float division = 0.25f;

        /** 触发时是否重置相位。
         *
         *  - false（默认）：LFO 自由运行，与触发时刻无关。
         *    适合"扫描感"的效果（比如缓慢开合滤波）。
         *
         *  - true：每次触发从相位 0 开始。
         *    适合要求**每次听感一致**的效果 ——
         *    比如"音头一定是 LFO 的上升段"。
         *    底鼓是 one-shot，这点很重要：自由运行的 LFO
         *    会让每次敲击的音色都不同。
         *
         *  另外还提供相位偏移，用来对齐"想让 LFO 从哪一段开始"。 */
        bool resetOnTrigger = false;

        /** 相位偏移（0..1 表示一个周期）。
         *
         *  只在 resetOnTrigger 开启时有意义 ——
         *  自由运行时相位是连续演进的，设"起点"无从谈起。 */
        float phaseOffset = 0.0f;

        /** 起始相位是否按 BPM 对齐到小节线。
         *
         *  开启后触发时不回到 0，而是对齐到"当前小节内的位置"，
         *  这样连续敲击之间 LFO 保持音乐上的连贯。
         *  只在 resetOnTrigger 关闭时有意义。 */
        bool syncPhaseToSong = false;
    };

    LfoSettings getLfo (int index) const noexcept;
    void setLfo (int index, const LfoSettings& s);

    /** 取某条通用包络的时长（秒）。 */
    float getEnvelopeLengthSeconds (int index) const noexcept
    {
        const int i = juce::jlimit (0, kNumEnvs - 1, index);
        return envLengthSeconds[(size_t) i];
    }

    void setEnvelopeLengthSeconds (int index, float sec) noexcept
    {
        const int i = juce::jlimit (0, kNumEnvs - 1, index);
        envLengthSeconds[(size_t) i] = juce::jlimit (0.005f, 5.0f, sec);
    }

    //==========================================================================
    // 序列化

    juce::String toString() const;
    bool fromString (const juce::String& s);

    /** 清空全部路由（恢复"无调制"状态，等价于旧行为）。 */
    void clearAllRoutes();

private:
    std::array<Envelope, kNumEnvs> envs;
    std::array<EnvelopeReader, kNumEnvs> envReaders;

    std::array<LfoSettings, kNumLfos> lfoSettings;
    std::array<double, kNumLfos> lfoPhase {};
    std::array<float, kNumLfos> lfoRandomValue {};

    struct AtomicRoute
    {
        std::atomic<int> source { 0 };
        std::atomic<int> target { 0 };
        std::atomic<float> depth { 0.0f };
    };
    std::array<AtomicRoute, kMaxRoutes> routes;

    std::atomic<bool> anyActive { false };

    /** 每个源的实时值，供 UI 画电平指示。 */
    std::array<std::atomic<float>, (size_t) Source::kNumSources> meterValues {};

    // 每个目标的当前调制值（-1..+1）
    std::array<float, (size_t) Target::kNumTargets> targetValues {};

    // 源值（供 UI 显示与矩阵计算）
    std::array<float, (size_t) Source::kNumSources> sourceValues {};

    /** 宿主当前的 BPM（由处理器每块更新）。 */
    double hostBpm = 120.0;

    float currentVelocity = 1.0f;
    float currentKeytrack = 0.0f;      // 相对 C2 的半音数 / 24，钳到 -1..1
    float currentRandom = 0.0f;

    /** 每条通用包络各自的时间轴长度（秒）。
     *
     *  现有 Envelope 是按归一化时间 0..1 求值的，这里给它一个真实时长，
     *  用户才能控制"音头抬起后多久回落"。
     *
     *  **每条独立**而不是全局共享：做"快速的音头 + 缓慢的尾巴"
     *  需要两条包络时长不同（比如 ENV1=60ms 抬 EQ，
     *  ENV2=800ms 慢慢关失真）。共享一个时长就做不出来。
     *
     *  默认值给 4 个不同长度，这样一打开就有区分度。 */
    std::array<float, kNumEnvs> envLengthSeconds { 0.06f, 0.15f, 0.4f, 1.0f };

    /** 包络已经走过了多少秒（每次触发归零，所有包络共用同一个时钟）。 */
    double envTime = 0.0;

    /** 上一块的 LFO 相位，用于检测"绕回"（Random 波形每个周期取新值）。 */
    std::array<float, kNumLfos> lastLfoPhase {};

    /** 宿主是否在播放（停止时 LFO 冻结，避免停止后参数还在飘）。 */
    bool hostPlaying = true;

    /** 每个源的"块起始值"与"块结束值"，用于块内线性插值。
     *
     *  严格逐样本调制需要重写所有 DSP 的系数计算，代价太大。
     *  块级计算 + 块内插值在 512 样本块下已经听不出阶梯感。 */
    std::array<float, (size_t) Source::kNumSources> sourceStart {};
    std::array<float, (size_t) Source::kNumSources> sourceEnd {};

    void recomputeActiveFlag() noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (ModulationMatrix)
};

} // namespace kf
