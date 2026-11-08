#include "MasterChain.h"

namespace kf
{

void MasterChain::prepare (double newSampleRate, int /*maxBlockSize*/)
{
    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    for (auto& d : dcBlockers)
        d.prepare (sampleRate);
    reset();
}

void MasterChain::reset()
{
    envelopeDb = 0.0f;
    limiterEnvDb = 0.0f;
    scHpState.fill (0.0f);
    gainReductionDb = 0.0f;
    outputPeak = 0.0f;
    for (auto& d : dcBlockers)
        d.reset();
}

void MasterChain::process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                           bool  compEnabled,
                           float thresholdDb,
                           float ratio,
                           float attackMs,
                           float releaseMs,
                           float makeupDb,
                           bool  limiterEnabled,
                           float limiterCeilingDb,
                           float sidechainHpHz,
                           float lookaheadMs)
{
    if (numSamples <= 0)
        return;

    const int numChannels = juce::jmin (buffer.getNumChannels(), 2);

    // --- 侧链高通：底鼓能量极大，若侧链全频，每个低频周期都会触发压缩，
    //     产生严重的「呼吸」。截止频率可调：
    //     调低 -> 低频也参与压缩（更有整体感，但容易抽气）
    //     调高 -> 只有中高频触发（保留低频冲击力）
    const float scHpCut = juce::jlimit (20.0f, 2000.0f, sidechainHpHz);
    const float scCoeff = juce::jlimit (0.0f, 1.0f,
        1.0f - std::exp (-juce::MathConstants<float>::twoPi * scHpCut / (float) sampleRate));

    // --- 前瞻：给信号加一点延迟，让检测器"提前"看到瞬态 ---
    // 这样压缩器可以在瞬态到达之前就开始动作，避免削掉音头。
    const int lookSamples = juce::jlimit (0, 4096,
        (int) (juce::jmax (0.0f, lookaheadMs) * 0.001f * (float) sampleRate));

    if (lookSamples > 0
        && (lookaheadBuffer.getNumSamples() < lookSamples
            || lookaheadBuffer.getNumChannels() < numChannels))
    {
        lookaheadBuffer.setSize (numChannels, lookSamples, false, true, false);
        lookaheadBuffer.clear();
        lookaheadWritePos = 0;
    }

    // --- 平滑系数（dB 域一阶平滑）
    auto msToCoeff = [this] (float ms)
    {
        if (ms <= 0.0f) return 1.0f;
        const double n = juce::jmax (1.0, (double) ms * 0.001 * sampleRate);
        return (float) (1.0 - std::exp (-1.0 / n));
    };

    const float aCoeff = msToCoeff (attackMs);
    const float rCoeff = msToCoeff (releaseMs);

    const float ratioSafe = juce::jmax (1.0f, ratio);
    const float slope = 1.0f - 1.0f / ratioSafe;
    const float kneeDb = 6.0f;   // soft knee 宽度

    const float ceiling = juce::Decibels::decibelsToGain<float> (limiterCeilingDb);
    const float makeupGain = juce::Decibels::decibelsToGain<float> (makeupDb);

    float blockPeak = 0.0f;
    float maxGr = 0.0f;

    for (int i = 0; i < numSamples; ++i)
    {
        // --- 侧链检测信号：两声道取最大，再经高通
        float sc = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            sc = juce::jmax (sc, std::abs (buffer.getSample (ch, startSample + i)));

        scHpState[0] += scCoeff * (sc - scHpState[0]);
        sc -= scHpState[0];
        sc = std::abs (sc);

        float gain = 1.0f;

        // --- 压缩级
        if (compEnabled && sc > 1.0e-6f)
        {
            const float inDb = juce::Decibels::gainToDecibels (sc);

            // soft knee：阈值上下 kneeDb/2 范围内渐进
            float targetGr = 0.0f;
            const float over = inDb - thresholdDb;
            if (over > kneeDb * 0.5f)
            {
                targetGr = over * slope;
            }
            else if (over > -kneeDb * 0.5f)
            {
                // 二次插值，保证拐点处一阶连续
                const float x = over + kneeDb * 0.5f;
                targetGr = slope * x * x / (2.0f * kneeDb);
            }

            // 目标增益衰减（dB 域）
            const float targetEnv = -targetGr;

            // 起音要快（抓住瞬态）、释放要慢（避免抽吸）
            const float coeff = (targetEnv < envelopeDb) ? aCoeff : rCoeff;
            envelopeDb += coeff * (targetEnv - envelopeDb);

            gain = juce::Decibels::decibelsToGain<float> (envelopeDb);
            maxGr = juce::jmin (maxGr, envelopeDb);
        }
        else
        {
            envelopeDb += rCoeff * (0.0f - envelopeDb);
        }

        // --- 限幅级（砖墙，起音几乎瞬时）
        for (int ch = 0; ch < numChannels; ++ch)
        {
            // 前瞻：先把信号写进环形缓冲，再读出"lookSamples 之前"的样本。
            // 这样上面的检测器看到的是**未来**的信号，压缩可以在瞬态
            // 到达之前就建立好增益衰减，避免削掉音头。
            float delayed;
            if (lookSamples > 0)
            {
                auto* lb = lookaheadBuffer.getWritePointer (ch);
                delayed = lb[lookaheadWritePos];
                lb[lookaheadWritePos] = buffer.getSample (ch, startSample + i);
            }
            else
            {
                delayed = buffer.getSample (ch, startSample + i);
            }

            float s = delayed * gain * makeupGain;

            // 直流阻断：压缩器的 makeup 增益会把输入里的微小直流放大成巨大的偏置
            // （实测 makeup 拉满时直流可达 4000+）。这里在信号链末端彻底清掉直流，
            // 保护后级与音箱。dcBlockers 之前只声明未使用，是个遗漏。
            s = dcBlockers[(size_t) ch].process (s);

            if (limiterEnabled)
            {
                const float a = std::abs (s);
                if (a > 1.0e-6f)
                {
                    const float inDb = juce::Decibels::gainToDecibels (a);
                    const float overDb = inDb - limiterCeilingDb;

                    // 限幅器用极快起音、中等释放
                    if (overDb > 0.0f)
                    {
                        const float targetEnv = -overDb;
                        limiterEnvDb += 0.5f * (targetEnv - limiterEnvDb);   // 快
                    }
                    else
                    {
                        limiterEnvDb += rCoeff * (0.0f - limiterEnvDb);      // 慢
                    }

                    s *= juce::Decibels::decibelsToGain<float> (limiterEnvDb);

                    // 最后再硬钳一次，保证绝对不越界
                    s = juce::jlimit (-ceiling, ceiling, s);
                }
            }

            buffer.setSample (ch, startSample + i, s);
            blockPeak = juce::jmax (blockPeak, std::abs (s));
        }
    }

    gainReductionDb = maxGr;
    outputPeak = blockPeak;
}

} // namespace kf
