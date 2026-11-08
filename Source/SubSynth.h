#pragma once

#include <juce_dsp/juce_dsp.h>
#include <array>
#include "Envelope.h"
#include "Waveform.h"

namespace kf
{

//==============================================================================
/** 加法合成 Sub 引擎。
 *
 *  KICK 2 的 Sub 部分本质是「基频 + 最多 8 个泛音」的加法合成器：
 *  - 每个泛音有独立的幅度，可塑造复杂的低频谐波结构
 *  - Sub Harmonics 在基频下方加入 1/2、1/3 倍频的分量（Sub 感来源）
 *  - Pitch 包络驱动瞬时频率，Amp 包络驱动幅度
 *
 *  相位累加器按采样逐个推进，保证任何调制下都不产生咔哒声。
 */
class SubSynth
{
public:
    static constexpr int kMaxHarmonics = 8;
    static constexpr int kNumSubHarmonics = 3;   // 1/2, 1/3, 1/4

    SubSynth();

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    /** 触发一个新音符。
     *  @param frequency  目标基频（已包含 keytrack + tune 的结果）
     *  @param velocity   0..1
     *  @param startFromCurrentPhase  连奏时是否从当前相位继续（避免咔哒） */
    void noteOn (float frequency, float velocity);
    void noteOff();

    /** 设置振荡器波形（基频与泛音都用它）。 */
    void setWaveform (Waveform w) noexcept { waveform = w; }
    Waveform getWaveform() const noexcept { return waveform; }

    /** 波形形状微调 0..1：
     *  - Square/Pulse：改变占空比（0.5 -> 0.05）
     *  - Saw/Triangle：改变对称性（弯曲程度）
     *  - Sine/Noise：无效
     */
    void setShape (float s) noexcept { shape = juce::jlimit (0.0f, 1.0f, s); }

    /** 每次触发时按相位随机化量打散各泛音的起始相位。
     *  amount = 0 不改变；1 则每个泛音的起始相位完全随机。 */
    void randomizePhases (float amount);

    /** 设置起始相位（度）。
     *
     *  与 randomizePhases 的分工：
     *    - startPhase 决定**基准**起点（所有泛音从同一相位出发）
     *    - randomizePhases 在此基础上加**随机偏移**（让泛音之间错开）
     *
     *  这两者是独立的：KICK 2 里 Start Phase 用来对齐多次触发的相位，
     *  随机化用来避免泛音叠加时出现尖锐的瞬态。 */
    void setStartPhase (float degrees) noexcept;

    /** 音频线程逐块调用。返回是否仍在发声（用于 voice stealing / 电平表）。
     *
     *  包络时间基准：整条包络的横轴 0..1 映射到 decaySeconds 秒。
     *  因此第 n 个采样的归一化包络位置 = elapsedSeconds / decaySeconds。
     *  这样用户改 Decay 时曲线形状不变、只拉伸时间，与 KICK 2 一致。
     *
     *  @param liveFrequency  当前基频。每块都可以不同 —— Portamento 滑音
     *                        与 Keytrack 都靠它逐块更新（不能用 noteOn 时的
     *                        固定值，否则滑音和音高跟随都不会生效）。 */
    bool render (juce::AudioBuffer<float>& output,
                 int startSample, int numSamples,
                 const EnvelopeReader& pitchEnv,
                 const EnvelopeReader& ampEnv,
                 float pitchEnvDepthSemis,
                 float ampGain,
                 float decaySeconds,
                 float pitchDecaySeconds,
                 float ampPunch,
                 float liveFrequency,
                 int   numActiveHarmonics,
                 const std::array<float, kMaxHarmonics>& harmonicLevels,
                 float subHarmonicLevel,
                 bool  subHarmonicsEnabled,
                 float filterCutoffHz,
                 float filterResonance,
                 float filterEnvAmount,
                 float pitchCurve,
                 float holdSeconds,
                 float widthAmount,
                 float noiseAmount,
                 float noiseDecaySeconds,
                 float noiseHighPassHz,
                 float attackSeconds,
                 int   filterType,
                 float detuneCents,
                 float phaseRandomAmount,
                 float pitchDelaySeconds,
                 float stage2Semis,
                 float stage2StartPct);

    bool isActive() const noexcept { return active; }

    /** 已播放时长（秒），用于把包络挂到真实时间。 */
    float getElapsedSeconds() const noexcept { return (float) (elapsedSamples / sampleRate); }

    /** 让 UI 画波形用：算出当前音符在给定时刻的近似瞬时频率。 */
    static float semitonesToRatio (float semis) noexcept
    {
        return std::pow (2.0f, semis / 12.0f);
    }

private:
    /** 设置 Sub 的低通滤波（包络跟随滤波用）。
     *  截止频率按包络实时计算，所以只存基础值。 */
    void setFilter (float cutoffHz, float resonance, float envAmount) noexcept
    {
        filterBaseHz = juce::jlimit (20.0f, 20000.0f, cutoffHz);
        filterQ      = juce::jlimit (0.3f, 8.0f, resonance);
        filterEnvAmt = juce::jlimit (-1.0f, 1.0f, envAmount);
    }

    double sampleRate = 44100.0;
    bool   active = false;
    float  baseFrequency = 50.0f;
    float  velocityGain = 1.0f;
    double elapsedSamples = 0.0;

    // --- 噪声瞬态层 ---
    // 底鼓的"啪"感来自音头的一小段噪声。独立成层便于单独塑形。
    NoiseGen noiseGen;
    float noiseHpState = 0.0f;

    // 立体声宽度用的两路一阶高通状态
    float widthHp1 = 0.0f;
    float widthHp2 = 0.0f;

    // --- 包络跟随滤波 ---
    // 用状态变量滤波器（SVF）：比双二阶更容易在截止频率逐采样变化时保持稳定。
    float filterBaseHz = 20000.0f;
    float filterQ      = 0.707f;
    float filterEnvAmt = 0.0f;
    // TPT 状态变量滤波器的积分器状态
    float svfIc1 = 0.0f;
    float svfIc2 = 0.0f;

    // 每个泛音一个相位累加器（基频 + 8 泛音 + 3 个 sub harmonic）
    static constexpr int kNumOscillators = kMaxHarmonics + kNumSubHarmonics + 1;
    std::array<double, kNumOscillators> phases {};

    /** 触发时是否把相位归零。
     *
     *  默认 false —— 底鼓连击时相位归零会在低频产生突变咔哒声。
     *  开启后每次触发从同一相位开始，**波形可完全复现**，
     *  适合需要严格一致性的场合（比如导出、相位对齐）。 */
    bool resetPhaseOnTrigger = false;

    /** 起始相位（0..1 表示一个周期）。由 setStartPhase() 设置。 */
    double startPhase = 0.0;

public:
    void setPhaseResetOnTrigger (bool shouldReset) noexcept
    {
        resetPhaseOnTrigger = shouldReset;
    }

private:

    Waveform waveform = Waveform::Sine;
    float shape = 0.5f;

    // 噪声波形需要每根振荡器独立的随机源
    std::array<NoiseGen, kNumOscillators> noiseGens;

    /** 取某个振荡器当前的波形采样值。 */
    inline float oscValue (int index) noexcept;

    /** 该泛音相对基频的倍频系数。索引 0 是基频，1..8 是 2..9 倍频，
     *  之后是 sub harmonics 的 0.5 / 0.3333 / 0.25。 */
    static float ratioFor (int index) noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SubSynth)
};

} // namespace kf
