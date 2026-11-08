//==============================================================================
// KickForge 离线自检工具
//
// 目的：不依赖任何宿主，直接驱动 DSP 渲染并把结果写成 WAV，
// 同时打印客观指标（峰值、RMS、频谱重心、包络跟随），
// 用来证明每一项功能是真的生效，而不是只编译通过。
//==============================================================================

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <iostream>
#include <cmath>

#include "Envelope.h"
#include "SubSynth.h"
#include "ClickLayer.h"
#include "Distortion.h"
#include "MasterChain.h"
#include "Equalizer.h"
#include "SampleLibrary.h"

using namespace kf;

static int failures = 0;

static void check (bool cond, const juce::String& what, const juce::String& detail = {})
{
    std::cout << (cond ? "  [PASS] " : "  [FAIL] ") << what;
    if (detail.isNotEmpty())
        std::cout << "   (" << detail << ")";
    std::cout << std::endl;
    if (! cond)
        ++failures;
}

//==============================================================================
// 把 buffer 存成 WAV，方便人耳复核
static void writeWav (const juce::File& file, const juce::AudioBuffer<float>& buf, double sr)
{
    file.deleteFile();
    juce::WavAudioFormat fmt;
    std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
    if (stream == nullptr) return;

    std::unique_ptr<juce::AudioFormatWriter> w (
        fmt.createWriterFor (stream.get(), sr, (unsigned) buf.getNumChannels(), 24, {}, 0));
    if (w == nullptr) return;

    stream.release();   // writer 接管
    w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
}

//==============================================================================
// 分析工具
struct Stats
{
    float peak = 0.0f;
    float rms = 0.0f;
    float dcOffset = 0.0f;
    /** 频谱重心（Hz），用 FFT 算，反映"亮/暗" */
    float centroid = 0.0f;
    /** 过零率，粗略反映高频含量 */
    float zcr = 0.0f;
};

static Stats analyse (const juce::AudioBuffer<float>& buf, double sr)
{
    Stats s;
    const int n = buf.getNumSamples();
    if (n <= 0) return s;

    const auto* d = buf.getReadPointer (0);

    double sumSq = 0.0, sum = 0.0;
    int zc = 0;
    for (int i = 0; i < n; ++i)
    {
        s.peak = juce::jmax (s.peak, std::abs (d[i]));
        sumSq += (double) d[i] * d[i];
        sum += d[i];
        if (i > 0 && ((d[i] >= 0.0f) != (d[i-1] >= 0.0f)))
            ++zc;
    }
    s.rms = (float) std::sqrt (sumSq / n);
    s.dcOffset = (float) (sum / n);
    s.zcr = (float) zc / (float) n;

    // 频谱重心：短采样用 Goertzel 直接扫频段，长信号用 FFT。
    // Click 采样只有几毫秒（几百个采样点），不够一个 8192 点 FFT，
    // 直接 FFT 会因为补零/长度不足而算出 0。
    if (n >= 4096)
    {
        const int order = 12;
        const int fftSize = 1 << order;
        juce::dsp::FFT fft (order);
        std::vector<float> data ((size_t) fftSize * 2, 0.0f);
        for (int i = 0; i < fftSize; ++i)
            data[(size_t) i] = d[i] * (0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * i / (fftSize - 1)));

        fft.performFrequencyOnlyForwardTransform (data.data());

        double num = 0.0, den = 0.0;
        for (int k = 1; k < fftSize / 2; ++k)
        {
            const double freq = (double) k * sr / fftSize;
            const double mag = data[(size_t) k];
            num += freq * mag;
            den += mag;
        }
        s.centroid = den > 0.0 ? (float) (num / den) : 0.0f;
    }
    else
    {
        // 短信号：在 1/3 八度带宽的频点上做 DFT，加权求重心
        double num = 0.0, den = 0.0;
        const int steps = 60;
        for (int k = 0; k < steps; ++k)
        {
            // 100Hz .. 16kHz 对数分布
            const double freq = 100.0 * std::pow (16000.0 / 100.0, (double) k / (steps - 1));
            double re = 0.0, im = 0.0;
            for (int i = 0; i < n; ++i)
            {
                const double ph = -2.0 * juce::MathConstants<double>::pi * freq * i / sr;
                re += (double) d[i] * std::cos (ph);
                im += (double) d[i] * std::sin (ph);
            }
            const double mag = std::sqrt (re * re + im * im) / (double) n;
            num += freq * mag;
            den += mag;
        }
        s.centroid = den > 0.0 ? (float) (num / den) : 0.0f;
    }

    return s;
}

/** 主频估计。
 *
 *  先用自相关找候选周期，再在其中挑出真正的基频。
 *  自相关容易锁到「次谐波」（例如把 200Hz 误判成 100Hz 或 66Hz），
 *  所以拿到候选后要检查它的 2x/3x 倍数上是否存在更强的谱峰。
 */
static float estimateFundamental (const juce::AudioBuffer<float>& buf, double sr)
{
    const auto* d = buf.getReadPointer (0);
    const int total = buf.getNumSamples();

    // 跳过起音瞬态
    const int start = juce::jmin (total / 4, (int) (sr * 0.01));
    const int len = juce::jmin (total - start, (int) (sr * 0.15));
    if (len < 2048) return 0.0f;

    // 直接在 25..400Hz 范围做对数细扫，找谱峰。
    // 这比「自相关 + 次谐波修正」稳健得多：后者的 lag 量化误差
    // 会让 200Hz 落到 150Hz 之类的非整数倍上。
    auto magnitudeAt = [&] (double freq) -> double
    {
        double re = 0.0, im = 0.0;
        for (int i = 0; i < len; ++i)
        {
            const double ph = -2.0 * juce::MathConstants<double>::pi * freq * i / sr;
            re += (double) d[start + i] * std::cos (ph);
            im += (double) d[start + i] * std::sin (ph);
        }
        return std::sqrt (re * re + im * im) / (double) len;
    };

    // 粗扫：0.5Hz 步长
    double bestF = 0.0, bestMag = -1.0;
    for (double f = 25.0; f <= 400.0; f += 0.5)
    {
        const double m = magnitudeAt (f);
        if (m > bestMag) { bestMag = m; bestF = f; }
    }

    if (bestF <= 0.0) return 0.0f;

    // 细扫：±1Hz，步长 0.02Hz
    double refined = bestF, refinedMag = bestMag;
    for (double f = bestF - 1.0; f <= bestF + 1.0; f += 0.02)
    {
        if (f <= 20.0 || f >= 400.0) continue;
        const double m = magnitudeAt (f);
        if (m > refinedMag) { refinedMag = m; refined = f; }
    }

    // --- 次谐波校验：若 2 倍频处的谱峰显著更强，说明找到的是次谐波。
    //     真正基频的能量应远大于其 2 倍频（除非是纯方波）。
    const double h2 = magnitudeAt (refined * 2.0);
    if (h2 > refinedMag * 2.5 && refined * 2.0 < 400.0)
        refined *= 2.0;

    return (float) refined;
}

//==============================================================================
int main (int argc, char** argv)
{
    // 本工具直接驱动 SubSynth / ClickLayer 等组件，不经过 processBlock，

    const double sr = 44100.0;
    const int blockSize = 512;

    juce::File outDir ("/Users/serenity/Documents/deepseek-harness/default-workspace/KickForge/selftest_output");
    outDir.createDirectory();

    std::cout << "==============================================\n";
    std::cout << " KickForge 离线自检\n";
    std::cout << "==============================================\n" << std::endl;

    //==========================================================================
    std::cout << "--- 1. 节点式包络 (Envelope) ---" << std::endl;
    {
        Envelope env;
        env.resetToDefault();

        check (env.getNumNodes() == 4, "默认包络有 4 个节点",
               "got " + juce::String (env.getNumNodes()));

        // 基本单调性/端点
        check (std::abs (env.getValueAt (0.0f) - 0.0f) < 0.01f, "起点值 = 0");
        check (std::abs (env.getValueAt (1.0f) - 0.0f) < 0.01f, "终点值 = 0");

        // 峰值应出现在起音后
        float peakVal = 0.0f;
        for (int i = 0; i <= 100; ++i)
            peakVal = juce::jmax (peakVal, env.getValueAt ((float) i / 100.0f));
        check (peakVal > 0.95f, "峰值接近 1.0", juce::String (peakVal, 3));

        // 连续性：相邻采样点不应有跳变（证明 Bezier 求值平滑）
        float maxJump = 0.0f;
        float prev = env.getValueAt (0.0f);
        for (int i = 1; i <= 2000; ++i)
        {
            const float v = env.getValueAt ((float) i / 2000.0f);
            maxJump = juce::jmax (maxJump, std::abs (v - prev));
            prev = v;
        }
        check (maxJump < 0.05f, "曲线连续无跳变", "max jump = " + juce::String (maxJump, 4));

        // 增删节点
        const int added = env.addNode (0.7f, 0.5f);
        check (env.getNumNodes() == 5, "可新增节点");
        check (added >= 0 && added < env.getNumNodes(), "新增返回有效索引");
        env.removeNode (added);
        check (env.getNumNodes() == 4, "可删除节点");

        // 首尾节点不可删
        env.removeNode (0);
        check (env.getNumNodes() == 4, "首节点不可删除");

        // 序列化往返
        const auto str = env.toString();
        Envelope env2;
        const bool ok = env2.fromString (str);
        check (ok, "包络可反序列化");
        check (env2.getNumNodes() == env.getNumNodes(), "反序列化节点数一致");

        // 拖动节点会改变曲线（多点采样，避免单个时间点恰好重合）
        std::vector<float> beforeCurve;
        for (int i = 0; i <= 20; ++i) beforeCurve.push_back (env.getValueAt ((float) i / 20.0f));
        env.moveNode (1, 0.4f, 1.0f);
        std::vector<float> afterCurve;
        for (int i = 0; i <= 20; ++i) afterCurve.push_back (env.getValueAt ((float) i / 20.0f));

        float maxDiff = 0.0f;
        for (size_t i = 0; i < beforeCurve.size(); ++i)
            maxDiff = juce::jmax (maxDiff, std::abs (afterCurve[i] - beforeCurve[i]));

        const float before = beforeCurve[10];
        const float after = afterCurve[10];
        check (maxDiff > 1.0e-3f, "移动节点后整条曲线改变",
               "max diff = " + juce::String (maxDiff, 4));
    }

    //==========================================================================
    std::cout << "\n--- 2. 加法合成 Sub + 泛音 ---" << std::endl;
    {
        SubSynth sub;
        sub.prepare (sr, blockSize);

        Envelope ampEnv;
        ampEnv.resetToDefault();
        ampEnv.rebuildTable();

        EnvelopeReader ampReader;
        ampReader.prepare();
        {
            std::vector<float> t (2048);
            for (int i = 0; i < 2048; ++i) t[(size_t) i] = ampEnv.getValueAt ((float) i / 2047.0f);
            ampReader.swapTable (std::move (t));
        }

        Envelope pitchFlat;
        pitchFlat.resetToFlat (0.0f);
        EnvelopeReader pitchReader;
        pitchReader.prepare();
        {
            std::vector<float> t (2048);
            for (int i = 0; i < 2048; ++i) t[(size_t) i] = pitchFlat.getValueAt ((float) i / 2047.0f);
            pitchReader.swapTable (std::move (t));
        }

        auto renderSub = [&] (int harmonics, float subHarmLevel, bool subHarmOn)
        {
            juce::AudioBuffer<float> buf (1, (int) sr);
            buf.clear();
            sub.reset();
            sub.noteOn (50.0f, 1.0f);

            std::array<float, SubSynth::kMaxHarmonics> levels {};
            for (int h = 0; h < SubSynth::kMaxHarmonics; ++h)
                levels[(size_t) h] = 1.0f / (float) (h + 2);

            int pos = 0;
            while (pos < buf.getNumSamples())
            {
                const int n = juce::jmin (blockSize, buf.getNumSamples() - pos);
                // pitchDecay 传 0 = 跟随主 decay；punch 传 0.5 = 不整形（保持原行为）
                if (! sub.render (buf, pos, n, pitchReader, ampReader, 0.0f, 0.85f, 0.42f,
                                  0.0f, 0.5f,
                                  50.0f, harmonics, levels, subHarmLevel, subHarmOn,
                                  20000.0f, 0.707f, 0.0f,
                                  0.0f, 0.0f, 0.0f, 0.0f, 0.012f, 400.0f,
                                  0.0f, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f))
                    break;
                pos += n;
            }
            return buf;
        };

        // 纯基频
        auto pure = renderSub (0, 0.0f, false);
        const auto sPure = analyse (pure, sr);
        check (sPure.peak > 0.1f, "Sub 有输出", "peak = " + juce::String (sPure.peak, 3));

        const float f0 = estimateFundamental (pure, sr);
        check (std::abs (f0 - 50.0f) < 3.0f, "基频 = 50Hz", "measured " + juce::String (f0, 1) + " Hz");

        // 加 8 个泛音：频谱重心必须上升
        auto rich = renderSub (8, 0.0f, false);
        const auto sRich = analyse (rich, sr);
        check (sRich.centroid > sPure.centroid * 1.3f,
               "泛音使频谱重心上升",
               juce::String (sPure.centroid, 0) + " -> " + juce::String (sRich.centroid, 0) + " Hz");

        // Sub Harmonics：应引入低于基频的能量
        auto withSub = renderSub (0, 0.8f, true);
        const auto sWithSub = analyse (withSub, sr);
        check (sWithSub.rms > sPure.rms * 1.1f,
               "Sub Harmonics 增加低频能量",
               "rms " + juce::String (sPure.rms, 4) + " -> " + juce::String (sWithSub.rms, 4));

        writeWav (outDir.getChildFile ("01_sub_pure.wav"), pure, sr);
        writeWav (outDir.getChildFile ("02_sub_8harm.wav"), rich, sr);
        writeWav (outDir.getChildFile ("03_sub_subharm.wav"), withSub, sr);
    }

    //==========================================================================
    std::cout << "\n--- 3. Pitch 包络真实调制音高 ---" << std::endl;
    {
        SubSynth sub;
        sub.prepare (sr, blockSize);

        // 一条从 +24 半音滑到 0 的 pitch 包络
        Envelope pitchEnv;
        pitchEnv.resetToFlat (1.0f);   // 恒为 1.0 -> 满深度
        pitchEnv.rebuildTable();
        EnvelopeReader pitchReader;
        pitchReader.prepare();
        {
            std::vector<float> t (2048);
            for (int i = 0; i < 2048; ++i) t[(size_t) i] = pitchEnv.getValueAt ((float) i / 2047.0f);
            pitchReader.swapTable (std::move (t));
        }

        Envelope ampEnv;
        ampEnv.resetToFlat (1.0f);
        EnvelopeReader ampReader;
        ampReader.prepare();
        {
            std::vector<float> t (2048);
            for (int i = 0; i < 2048; ++i) t[(size_t) i] = ampEnv.getValueAt ((float) i / 2047.0f);
            ampReader.swapTable (std::move (t));
        }

        juce::AudioBuffer<float> buf (1, (int) (sr * 0.2));
        buf.clear();
        sub.reset();
        sub.noteOn (50.0f, 1.0f);

        std::array<float, SubSynth::kMaxHarmonics> levels {};
        int pos = 0;
        while (pos < buf.getNumSamples())
        {
            const int n = juce::jmin (blockSize, buf.getNumSamples() - pos);
            sub.render (buf, pos, n, pitchReader, ampReader, 24.0f, 1.0f, 10.0f,
                        0.0f, 0.5f,
                        50.0f, 0, levels, 0.0f, false,
                        20000.0f, 0.707f, 0.0f,
                        0.0f, 0.0f, 0.0f, 0.0f, 0.012f, 400.0f,
                        0.0f, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);
            pos += n;
        }

        const float f = estimateFundamental (buf, sr);
        // +24 半音 = 4 倍频 -> 200Hz
        check (std::abs (f - 200.0f) < 20.0f, "Pitch 包络 +24st 使 50Hz -> 200Hz",
               "measured " + juce::String (f, 1) + " Hz");

        writeWav (outDir.getChildFile ("04_pitch_env.wav"), buf, sr);
    }

    //==========================================================================
    std::cout << "\n--- 4. 失真 4 种类型 ---" << std::endl;
    {
        // 用 50Hz 正弦作为测试信号
        juce::AudioBuffer<float> dry (1, 8192);
        for (int i = 0; i < dry.getNumSamples(); ++i)
            dry.setSample (0, i, 0.5f * std::sin (juce::MathConstants<float>::twoPi * 50.0f * i / (float) sr));

        const auto dryStats = analyse (dry, sr);

        struct Case { DistortionType type; const char* name; };
        const Case cases[4]
        {
            { DistortionType::Tube, "Tube" },
            { DistortionType::Tape, "Tape" },
            { DistortionType::Hard, "Hard Clip" },
            { DistortionType::Fold, "Wave Fold" }
        };

        for (const auto& c : cases)
        {
            juce::AudioBuffer<float> wet (1, dry.getNumSamples());
            wet.copyFrom (0, 0, dry, 0, 0, dry.getNumSamples());

            Distortion dist;
            dist.prepare (sr, 8192);
            dist.process (wet, 0, wet.getNumSamples(), c.type, 6.0f, 1.0f);

            const auto ws = analyse (wet, sr);

            // 失真必然产生谐波 -> 频谱重心上升
            check (ws.centroid > dryStats.centroid * 1.5f,
                   juce::String (c.name) + " 产生谐波（频谱重心上升）",
                   juce::String (dryStats.centroid, 0) + " -> " + juce::String (ws.centroid, 0) + " Hz");

            check (ws.peak < 1.5f, juce::String (c.name) + " 输出未失控",
                   "peak = " + juce::String (ws.peak, 3));

            // 直流应被阻断
            check (std::abs (ws.dcOffset) < 0.02f,
                   juce::String (c.name) + " 无直流偏移",
                   "dc = " + juce::String (ws.dcOffset, 5));

            writeWav (outDir.getChildFile ("05_dist_" + juce::String (c.name).replace (" ", "_") + ".wav"),
                      wet, sr);
        }
    }

    //==========================================================================
    std::cout << "\n--- 5. 压缩 / 限幅 ---" << std::endl;
    {
        // 一个远超阈值的信号
        juce::AudioBuffer<float> loud (2, 8192);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < loud.getNumSamples(); ++i)
                loud.setSample (ch, i, 4.0f * std::sin (juce::MathConstants<float>::twoPi * 60.0f * i / (float) sr));

        MasterChain mc;
        mc.prepare (sr, 8192);

        juce::AudioBuffer<float> processed (2, loud.getNumSamples());
        processed.copyFrom (0, 0, loud, 0, 0, loud.getNumSamples());
        processed.copyFrom (1, 0, loud, 1, 0, loud.getNumSamples());

        mc.process (processed, 0, processed.getNumSamples(),
                    true, -12.0f, 4.0f, 3.0f, 120.0f, 0.0f,
                    true, -0.3f);

        const auto inStats = analyse (loud, sr);
        const auto outStats = analyse (processed, sr);

        check (outStats.peak <= inStats.peak * 0.5f,
               "限幅器把 4.0 峰值压到 1.0 以下",
               juce::String (inStats.peak, 2) + " -> " + juce::String (outStats.peak, 3));

        check (outStats.peak <= 0.985f, "输出不越界（<= -0.3dBFS 附近）",
               "peak = " + juce::String (outStats.peak, 4));

        check (mc.getGainReductionDb() < -0.5f, "压缩器产生增益衰减",
               juce::String (mc.getGainReductionDb(), 2) + " dB");

        writeWav (outDir.getChildFile ("06_limiter_in.wav"), loud, sr);
        writeWav (outDir.getChildFile ("07_limiter_out.wav"), processed, sr);
    }

    //==========================================================================
    std::cout << "\n--- 6. 8 段 EQ ---" << std::endl;
    {
        Equalizer eq;
        juce::dsp::ProcessSpec spec { sr, (juce::uint32) blockSize, 2 };
        eq.prepare (spec);

        // 只开第 3 段（70Hz）做 +12dB boost
        for (int b = 0; b < Equalizer::kNumBands; ++b)
        {
            eq.setBandParams (b, Equalizer::defaultFrequencies()[(size_t) b],
                              0.0f, 0.9f, Equalizer::BandType::Peak, false);
        }
        eq.setBandParams (2, 70.0f, 12.0f, 0.9f, Equalizer::BandType::Peak, true);

        // 70Hz 处的响应应为正
        const float resp70 = eq.getResponseDb (70.0f);
        const float resp1000 = eq.getResponseDb (1000.0f);

        check (resp70 > 8.0f, "70Hz 段 +12dB 在 70Hz 处响应 > 8dB",
               juce::String (resp70, 2) + " dB");
        check (std::abs (resp1000) < 1.0f, "1kHz 处几乎不受影响",
               juce::String (resp1000, 3) + " dB");

        // 实际渲染：70Hz 正弦应被放大
        juce::AudioBuffer<float> tone (2, 16384);
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < tone.getNumSamples(); ++i)
                tone.setSample (ch, i, 0.2f * std::sin (juce::MathConstants<float>::twoPi * 70.0f * i / (float) sr));

        const auto before = analyse (tone, sr);

        int pos = 0;
        while (pos < tone.getNumSamples())
        {
            const int n = juce::jmin (blockSize, tone.getNumSamples() - pos);
            eq.process (tone, pos, n);
            pos += n;
        }

        // 跳过滤波器建立时间，只看后段
        juce::AudioBuffer<float> tail (2, 8192);
        tail.copyFrom (0, 0, tone, 0, tone.getNumSamples() - 8192, 8192);
        tail.copyFrom (1, 0, tone, 1, tone.getNumSamples() - 8192, 8192);
        const auto after = analyse (tail, sr);

        check (after.rms > before.rms * 2.0f, "70Hz 正弦被实际放大",
               juce::String (before.rms, 4) + " -> " + juce::String (after.rms, 4));

        writeWav (outDir.getChildFile ("08_eq_boosted.wav"), tone, sr);
    }

    //==========================================================================
    std::cout << "\n--- 7. 内置采样库 180 个 ---" << std::endl;
    {
        SampleLibrary lib;
        lib.initialise (sr);

        check (lib.getNumSamples() == 180, "库内共 180 个采样",
               "got " + juce::String (lib.getNumSamples()));

        // 全部生成一遍，检查有效性与命名唯一性
        int generated = 0, silent = 0, clipped = 0;
        juce::StringArray names;
        std::vector<float> centroids;

        for (int i = 0; i < lib.getNumSamples(); ++i)
        {
            const auto* buf = lib.getSampleBuffer (i);
            if (buf == nullptr || buf->getNumSamples() <= 0)
                continue;
            ++generated;

            const auto st = analyse (*buf, sr);
            if (st.rms < 1.0e-4f) ++silent;
            if (st.peak > 1.0f)  ++clipped;

            names.add (lib.getEntry (i).name);
            centroids.push_back (st.centroid);
        }

        check (generated == 180, "180 个采样全部成功生成",
               "generated " + juce::String (generated));
        check (silent == 0, "没有静音采样", "silent = " + juce::String (silent));
        check (clipped == 0, "没有削顶采样", "clipped = " + juce::String (clipped));

        names.removeDuplicates (false);
        check (names.size() == 180, "采样名唯一", "unique = " + juce::String (names.size()));

        // 类别之间应有明显的音色差异（频谱重心不全部相同）
        float mn = 1.0e9f, mx = 0.0f;
        for (float c : centroids) { mn = juce::jmin (mn, c); mx = juce::jmax (mx, c); }
        check (mx > mn * 2.0f, "采样之间有音色差异（重心范围）",
               juce::String (mn, 0) + " - " + juce::String (mx, 0) + " Hz");

        // 载入到 ClickLayer 验证
        ClickLayer layer;
        layer.prepare (sr, blockSize);
        const bool loaded = lib.loadIntoLayer (0, layer);
        check (loaded, "可载入到 Click 层");
        check (layer.isLoaded(), "Click 层报告已载入");
        check (layer.getLoadedName() == lib.getEntry (0).name, "载入名称正确",
               layer.getLoadedName());

        // 导出前 3 个供试听
        for (int i = 0; i < 3; ++i)
        {
            if (const auto* b = lib.getSampleBuffer (i))
                writeWav (outDir.getChildFile ("09_lib_" + juce::String (i) + "_"
                                               + lib.getEntry (i).name.replace (" ", "_") + ".wav"),
                          *b, sr);
        }
    }

    //==========================================================================
    std::cout << "\n--- 8. Click 层包络 / 滤波 ---" << std::endl;
    {
        ClickLayer layer;
        layer.prepare (sr, blockSize);

        // 造一个宽带脉冲采样
        juce::AudioBuffer<float> src (1, 2048);
        for (int i = 0; i < src.getNumSamples(); ++i)
            src.setSample (0, i, (i < 64) ? 0.9f : 0.0f);
        layer.loadFromBuffer (src, sr);

        Envelope ampEnv;
        ampEnv.resetToFlat (1.0f);
        EnvelopeReader reader;
        reader.prepare();
        {
            std::vector<float> t (2048, 1.0f);
            reader.swapTable (std::move (t));
        }

        auto renderClick = [&] (float lp, float hp)
        {
            juce::AudioBuffer<float> out (1, 4096);
            out.clear();
            layer.noteOn (1.0f, 1.0f);
            layer.render (out, 0, out.getNumSamples(), reader, 1.0f, 0.2f, lp, hp, true);
            return out;
        };

        auto wide = renderClick (20000.0f, 10.0f);
        auto lowOnly = renderClick (400.0f, 10.0f);
        auto highOnly = renderClick (20000.0f, 3000.0f);

        const auto sWide = analyse (wide, sr);
        const auto sLow = analyse (lowOnly, sr);
        const auto sHigh = analyse (highOnly, sr);

        check (sWide.peak > 0.05f, "Click 层有输出", "peak = " + juce::String (sWide.peak, 3));

        check (sLow.centroid < sWide.centroid,
               "低通滤波降低频谱重心",
               juce::String (sWide.centroid, 0) + " -> " + juce::String (sLow.centroid, 0) + " Hz");

        check (sHigh.centroid > sWide.centroid,
               "高通滤波提高频谱重心",
               juce::String (sWide.centroid, 0) + " -> " + juce::String (sHigh.centroid, 0) + " Hz");

        writeWav (outDir.getChildFile ("10_click_wide.wav"), wide, sr);
        writeWav (outDir.getChildFile ("11_click_lowpassed.wav"), lowOnly, sr);
        writeWav (outDir.getChildFile ("12_click_highpassed.wav"), highOnly, sr);

        // --- 重采样路径：源采样率与工程不同（48k 源 -> 44.1k 工程）
        // 这条路径曾因「先 setSize 再读指针」导致 use-after-free，必须覆盖。
        {
            ClickLayer rs;
            rs.prepare (sr, blockSize);

            juce::AudioBuffer<float> src48 (1, 4800);
            for (int i = 0; i < src48.getNumSamples(); ++i)
                src48.setSample (0, i, (i < 200) ? 0.8f : 0.0f);

            const bool ok = rs.loadFromBuffer (src48, 48000.0);
            check (ok, "48kHz 采样可载入 44.1kHz 工程");

            juce::AudioBuffer<float> out (1, 8192);
            out.clear();
            rs.noteOn (1.0f, 1.0f);
            rs.render (out, 0, out.getNumSamples(), reader, 1.0f, 0.5f, 20000.0f, 10.0f, true);

            const auto st = analyse (out, sr);
            check (st.peak > 0.05f, "重采样后仍有有效输出（无 use-after-free）",
                   "peak = " + juce::String (st.peak, 3));

            writeWav (outDir.getChildFile ("13_click_resampled_48k.wav"), out, sr);
        }
    }

    //==========================================================================
    std::cout << "\n--- 14. Sub 音色参数（Shape / Harm Tilt / Punch）---" << std::endl;
    {
        // 回归测试：这几个参数都曾"存在但无反应"。
        //  - Harm Tilt：泛音数为 0 时天然无效，必须在有泛音时测
        //  - Wave Shape：原来 Square 被写死 50% 占空比，Shape 只在 Pulse 下生效
        auto renderSub = [&] (Waveform wf, float shapeV, int harm, float tilt, float punch)
        {
            SubSynth sub;
            sub.prepare (sr, blockSize);
            sub.setWaveform (wf);
            sub.setShape (shapeV);

            // EnvelopeReader 含 SpinLock，不可拷贝 —— 必须就地构造
            auto fillReader = [] (EnvelopeReader& r, Envelope& e)
            {
                e.rebuildTable();
                r.prepare();
                std::vector<float> t (2048);
                for (int i = 0; i < 2048; ++i)
                    t[(size_t) i] = e.getValueAt ((float) i / 2047.0f);
                r.swapTable (std::move (t));
            };

            Envelope pe, ae;
            pe.resetToDefault(); ae.resetToDefault();
            EnvelopeReader pr, ar;
            fillReader (pr, pe);
            fillReader (ar, ae);

            std::array<float, SubSynth::kMaxHarmonics> levels {};
            const float expo = juce::jmap (tilt, 1.6f, 0.35f);
            for (int h = 0; h < SubSynth::kMaxHarmonics; ++h)
                levels[(size_t) h] = std::pow ((float) (h + 2), -expo);

            juce::AudioBuffer<float> buf (1, blockSize * 60);
            buf.clear();
            sub.noteOn (50.0f, 1.0f);

            int pos = 0;
            while (pos < buf.getNumSamples())
            {
                const int n = juce::jmin (blockSize, buf.getNumSamples() - pos);
                if (! sub.render (buf, pos, n, pr, ar, 0.0f, 0.85f, 0.42f,
                                  0.0f, punch, 50.0f, harm, levels, 0.0f, false,
                                  20000.0f, 0.707f, 0.0f,
                                  0.0f, 0.0f, 0.0f, 0.0f, 0.012f, 400.0f,
                                  0.0f, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f))
                    break;
                pos += n;
            }
            return analyse (buf, sr).rms;
        };

        // Shape：Square 下改变占空比，能量应明显变化
        const float sqWide   = renderSub (Waveform::Square, 0.0f, 0, 0.5f, 0.5f);
        const float sqNarrow = renderSub (Waveform::Square, 1.0f, 0, 0.5f, 0.5f);
        check (std::abs (sqWide - sqNarrow) > sqWide * 0.3f,
               "Wave Shape 在 Square 波形下生效（占空比可变）",
               "shape0 rms=" + juce::String (sqWide, 5)
             + "  shape1 rms=" + juce::String (sqNarrow, 5));

        // Harm Tilt：有泛音时，衰减越慢高频越多、rms 越高
        const float tiltLo = renderSub (Waveform::Sine, 0.5f, 8, 0.0f, 0.5f);
        const float tiltHi = renderSub (Waveform::Sine, 0.5f, 8, 1.0f, 0.5f);
        check (tiltHi > tiltLo * 1.15f,
               "Harm Tilt 能改变泛音衰减（tilt 越大越亮）",
               "tilt0 rms=" + juce::String (tiltLo, 5)
             + "  tilt1 rms=" + juce::String (tiltHi, 5));

        // Amp Punch：整形指数应改变能量分布
        const float pSoft = renderSub (Waveform::Sine, 0.5f, 0, 0.5f, 0.0f);
        const float pHard = renderSub (Waveform::Sine, 0.5f, 0, 0.5f, 1.0f);
        check (std::abs (pSoft - pHard) > pSoft * 0.1f,
               "Amp Punch 能改变包络形状",
               "soft rms=" + juce::String (pSoft, 5)
             + "  hard rms=" + juce::String (pHard, 5));
    }

    //==========================================================================
    std::cout << "\n==============================================\n";
    if (failures == 0)
        std::cout << " 全部自检通过 ✓" << std::endl;
    else
        std::cout << " 有 " << failures << " 项未通过 ✗" << std::endl;
    std::cout << " WAV 输出目录: " << outDir.getFullPathName() << std::endl;
    std::cout << "==============================================" << std::endl;

    return failures == 0 ? 0 : 1;
}
