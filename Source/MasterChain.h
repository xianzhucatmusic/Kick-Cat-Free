#pragma once

#include <juce_dsp/juce_dsp.h>
#include "DspUtils.h"

namespace kf
{

//==============================================================================
/** 专为鼓优化的压缩器 + 限幅器。
 *
 *  与通用压缩器不同，这里的关键设计：
 *   - 前馈 + 峰值检测，起音时间可以做到极快（底鼓瞬态最重要的 0-5ms）
 *   - Soft-knee 拐点，避免在阈值附近产生「抽吸」感
 *   - 独立的限幅级（砖墙）保证输出不会超过 0dBFS
 *   - 侧链带高通，避免低频把整个压缩器推得乱动（底鼓最大的问题）
 */
class MasterChain
{
public:
    MasterChain() = default;

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    void process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                  bool  compEnabled,
                  float thresholdDb,
                  float ratio,
                  float attackMs,
                  float releaseMs,
                  float makeupDb,
                  bool  limiterEnabled,
                  float limiterCeilingDb,
                  float sidechainHpHz = 120.0f,
                  float lookaheadMs = 0.0f);

    /** 当前增益衰减量（dB，负值），供 UI 表头显示。 */
    float getGainReductionDb() const noexcept { return gainReductionDb; }

    /** 峰值电平（线性），供 UI 表头显示。 */
    float getOutputPeak() const noexcept { return outputPeak; }

private:
    double sampleRate = 44100.0;

    // 包络跟随器的状态（用 dB 域做，控制更线性）
    float envelopeDb = 0.0f;
    float limiterEnvDb = 0.0f;

    // 侧链高通（一阶）状态，每声道
    std::array<float, 2> scHpState {};

    // 前瞻缓冲区：把信号延迟，检测器提前看到瞬态
    juce::AudioBuffer<float> lookaheadBuffer;
    int lookaheadWritePos = 0;
    std::array<dsp::DcBlocker, 2> dcBlockers;

    float gainReductionDb = 0.0f;
    float outputPeak = 0.0f;

    // 计算平滑系数
    float attackCoeff = 0.0f, releaseCoeff = 0.0f;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterChain)
};

} // namespace kf
