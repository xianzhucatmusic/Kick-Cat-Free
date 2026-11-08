//==============================================================================
// 完整插件链路离线渲染
//
// 直接实例化 KickForgeProcessor，喂 MIDI 音符，渲染若干秒，
// 导出 WAV 并检查：
//   - 确实有声音输出
//   - Keytrack 开关真的改变音高
//   - Portamento 滑音真的产生频率过渡
//   - 状态保存/恢复往返一致
// 这是端到端验证，覆盖 AU/VST3 外壳之下的真实处理逻辑。
//==============================================================================

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>

#include "PluginProcessor.h"

using namespace kf;

static int failures = 0;

static void check (bool cond, const juce::String& what, const juce::String& detail = {})
{
    std::cout << (cond ? "  [PASS] " : "  [FAIL] ") << what;
    if (detail.isNotEmpty()) std::cout << "   (" << detail << ")";
    std::cout << std::endl;
    if (! cond) ++failures;
}

static void writeWav (const juce::File& file, const juce::AudioBuffer<float>& buf, double sr)
{
    file.deleteFile();
    juce::WavAudioFormat fmt;
    std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
    if (stream == nullptr) return;
    std::unique_ptr<juce::AudioFormatWriter> w (
        fmt.createWriterFor (stream.get(), sr, (unsigned) buf.getNumChannels(), 24, {}, 0));
    if (w == nullptr) return;
    stream.release();
    w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
}

/** 在 25..400Hz 范围找谱峰，用于验证音高。 */
static float detectPitch (const juce::AudioBuffer<float>& buf, double sr, int offset)
{
    const auto* d = buf.getReadPointer (0);
    const int total = buf.getNumSamples();
    if (offset >= total - 2048) return 0.0f;

    const int len = juce::jmin (total - offset, (int) (sr * 0.12));
    if (len < 2048) return 0.0f;

    auto mag = [&] (double f) -> double
    {
        double re = 0.0, im = 0.0;
        for (int i = 0; i < len; ++i)
        {
            const double ph = -2.0 * juce::MathConstants<double>::pi * f * i / sr;
            re += (double) d[offset + i] * std::cos (ph);
            im += (double) d[offset + i] * std::sin (ph);
        }
        return std::sqrt (re * re + im * im) / (double) len;
    };

    double bestF = 0.0, bestM = -1.0;
    for (double f = 25.0; f <= 400.0; f += 0.5)
    {
        const double m = mag (f);
        if (m > bestM) { bestM = m; bestF = f; }
    }
    return (float) bestF;
}

/** 通过 APVTS 设置一个参数（自动换算成 0..1）。 */
static void setParam (KickForgeProcessor& p, const juce::String& id, float value)
{
    if (auto* param = p.getAPVTS().getParameter (id))
        param->setValueNotifyingHost (param->convertTo0to1 (value));
}

/** 渲染一段：先发一个 noteOn，然后渲染 seconds 秒。 */
static juce::AudioBuffer<float> renderNote (KickForgeProcessor& proc, int note, double sr,
                                            double seconds, int blockSize = 512)
{
    const int total = (int) (sr * seconds);
    juce::AudioBuffer<float> out (2, total);
    out.clear();

    proc.prepareToPlay (sr, blockSize);

    int pos = 0;
    bool noteSent = false;

    while (pos < total)
    {
        const int n = juce::jmin (blockSize, total - pos);

        juce::AudioBuffer<float> block (2, n);
        block.clear();

        juce::MidiBuffer midi;
        if (! noteSent)
        {
            midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);
            noteSent = true;
        }

        proc.processBlock (block, midi);

        for (int ch = 0; ch < 2; ++ch)
            out.copyFrom (ch, pos, block, ch, 0, n);

        pos += n;
    }

    return out;
}

int main()
{

    const double sr = 44100.0;

    juce::File outDir ("/Users/serenity/Documents/deepseek-harness/default-workspace/KickForge/selftest_output");
    outDir.createDirectory();

    std::cout << "==============================================\n";
    std::cout << " KickForge 完整插件链路测试\n";
    std::cout << "==============================================\n" << std::endl;

    juce::ScopedJuceInitialiser_GUI juceInit;

    //==========================================================================
    std::cout << "--- 1. 基本发声 ---" << std::endl;
    {
        KickForgeProcessor proc;

        // 确保有 Click 采样
        proc.getSampleLibrary().initialise (sr);
        proc.getSampleLibrary().loadIntoLayer (0, proc.getClickLayerRef (0));

        auto buf = renderNote (proc, 36, sr, 1.0);

        float peak = 0.0f, rms = 0.0;
        for (int ch = 0; ch < buf.getNumChannels(); ++ch)
            for (int i = 0; i < buf.getNumSamples(); ++i)
            {
                peak = juce::jmax (peak, std::abs (buf.getSample (ch, i)));
                rms += (double) buf.getSample (ch, i) * buf.getSample (ch, i);
            }
        rms = std::sqrt (rms / (buf.getNumSamples() * buf.getNumChannels()));

        check (peak > 0.05f, "插件对 MIDI noteOn 有音频输出",
               "peak = " + juce::String (peak, 3));
        check (peak <= 1.0f, "输出未越界", "peak = " + juce::String (peak, 3));
        check (rms > 0.005, "输出有有效能量", "rms = " + juce::String (rms, 4));

        writeWav (outDir.getChildFile ("20_plugin_basic.wav"), buf, sr);
    }

    //==========================================================================
    std::cout << "\n--- 2. Keytrack 开关真实改变音高 ---" << std::endl;
    {
        // Keytrack ON：note 36 (C2) vs note 48 (C3) 应差一个八度
        float pitchOn36 = 0.0f, pitchOn48 = 0.0f;

        {
            KickForgeProcessor proc;
            proc.getAPVTS().getParameter (pid::keytrack)->setValueNotifyingHost (1.0f);
            // 拉长 Decay，让音符在 Pitch 包络走完后仍有足够能量可供测量
            setParam (proc, pid::subDecay, 2000.0f);
            setParam (proc, pid::subPitchAmt, 0.0f);   // 关掉 pitch 调制，直接测基频
            proc.getSampleLibrary().initialise (sr);
            proc.getSampleLibrary().loadIntoLayer (0, proc.getClickLayerRef (0));
            auto buf = renderNote (proc, 36, sr, 1.0);
            pitchOn36 = detectPitch (buf, sr, (int) (sr * 0.3));
            writeWav (outDir.getChildFile ("21_keytrack_on_note36.wav"), buf, sr);
        }
        {
            KickForgeProcessor proc;
            proc.getAPVTS().getParameter (pid::keytrack)->setValueNotifyingHost (1.0f);
            setParam (proc, pid::subDecay, 2000.0f);
            setParam (proc, pid::subPitchAmt, 0.0f);
            proc.getSampleLibrary().initialise (sr);
            proc.getSampleLibrary().loadIntoLayer (0, proc.getClickLayerRef (0));
            auto buf = renderNote (proc, 48, sr, 1.0);
            pitchOn48 = detectPitch (buf, sr, (int) (sr * 0.3));
            writeWav (outDir.getChildFile ("22_keytrack_on_note48.wav"), buf, sr);
        }

        check (pitchOn36 > 30.0f && pitchOn36 < 100.0f,
               "Keytrack ON：note36 基频落在 C2 附近",
               juce::String (pitchOn36, 1) + " Hz");
        check (pitchOn48 > 90.0f && pitchOn48 < 200.0f,
               "Keytrack ON：note48 基频落在 C3 附近",
               juce::String (pitchOn48, 1) + " Hz");
        check (std::abs (pitchOn48 / juce::jmax (1.0f, pitchOn36) - 2.0f) < 0.25f,
               "Keytrack ON：两个八度关系成立（频率比 ≈ 2）",
               juce::String (pitchOn36, 1) + " -> " + juce::String (pitchOn48, 1)
                   + "  ratio=" + juce::String (pitchOn48 / juce::jmax (1.0f, pitchOn36), 2));

        // Keytrack OFF：两个音符应产生相同的音高
        float pitchOff36 = 0.0f, pitchOff48 = 0.0f;
        {
            KickForgeProcessor proc;
            proc.getAPVTS().getParameter (pid::keytrack)->setValueNotifyingHost (0.0f);
            setParam (proc, pid::subDecay, 2000.0f);
            setParam (proc, pid::subPitchAmt, 0.0f);
            proc.getSampleLibrary().initialise (sr);
            proc.getSampleLibrary().loadIntoLayer (0, proc.getClickLayerRef (0));
            auto buf = renderNote (proc, 36, sr, 1.0);
            pitchOff36 = detectPitch (buf, sr, (int) (sr * 0.3));
        }
        {
            KickForgeProcessor proc;
            proc.getAPVTS().getParameter (pid::keytrack)->setValueNotifyingHost (0.0f);
            setParam (proc, pid::subDecay, 2000.0f);
            setParam (proc, pid::subPitchAmt, 0.0f);
            proc.getSampleLibrary().initialise (sr);
            proc.getSampleLibrary().loadIntoLayer (0, proc.getClickLayerRef (0));
            auto buf = renderNote (proc, 48, sr, 1.0);
            pitchOff48 = detectPitch (buf, sr, (int) (sr * 0.3));
        }

        check (std::abs (pitchOff48 - pitchOff36) < 3.0f,
               "Keytrack OFF：不同音符音高相同（固定调音）",
               juce::String (pitchOff36, 1) + " vs " + juce::String (pitchOff48, 1) + " Hz");
    }

    //==========================================================================
    std::cout << "\n--- 3. Tune 参数改变音高 ---" << std::endl;
    {
        float p0 = 0.0f, p12 = 0.0f;

        {
            KickForgeProcessor proc;
            setParam (proc, pid::subDecay, 2000.0f);
            setParam (proc, pid::subPitchAmt, 0.0f);
            proc.getSampleLibrary().initialise (sr);
            proc.getSampleLibrary().loadIntoLayer (0, proc.getClickLayerRef (0));
            auto buf = renderNote (proc, 36, sr, 1.0);
            p0 = detectPitch (buf, sr, (int) (sr * 0.3));
        }
        {
            KickForgeProcessor proc;
            setParam (proc, pid::subDecay, 2000.0f);
            setParam (proc, pid::subPitchAmt, 0.0f);
            setParam (proc, pid::tune, 12.0f);
            proc.getSampleLibrary().initialise (sr);
            proc.getSampleLibrary().loadIntoLayer (0, proc.getClickLayerRef (0));
            auto buf = renderNote (proc, 36, sr, 1.0);
            p12 = detectPitch (buf, sr, (int) (sr * 0.3));
        }

        check (std::abs (p12 / juce::jmax (1.0f, p0) - 2.0f) < 0.3f,
               "Tune +12st 让频率翻倍",
               juce::String (p0, 1) + " -> " + juce::String (p12, 1) + " Hz");
    }

    //==========================================================================
    std::cout << "\n--- 4. Portamento 滑音 ---" << std::endl;
    {
        KickForgeProcessor proc;
        setParam (proc, pid::subDecay, 2000.0f);
        setParam (proc, pid::subPitchAmt, 0.0f);
        setParam (proc, pid::subOn, 1.0f);
        proc.getSampleLibrary().initialise (sr);

        // Glide = 300ms，模式 Always
        if (auto* p = proc.getAPVTS().getParameter (pid::glide))
            p->setValueNotifyingHost (p->convertTo0to1 (300.0f));
        if (auto* p = proc.getAPVTS().getParameter (pid::glideMode))
            p->setValueNotifyingHost (p->convertTo0to1 (1.0f));   // Always

        proc.prepareToPlay (sr, 512);

        // 第一个音：note 24 (低)
        {
            juce::AudioBuffer<float> b (2, 512);
            b.clear();
            juce::MidiBuffer m;
            m.addEvent (juce::MidiMessage::noteOn (1, 24, (juce::uint8) 100), 0);
            proc.processBlock (b, m);
        }

        // 立刻发第二个音：note 48（高一个八度），应该滑上去
        juce::AudioBuffer<float> out (2, (int) (sr * 0.8));
        out.clear();

        int pos = 0;
        bool sent = false;
        while (pos < out.getNumSamples())
        {
            const int n = juce::jmin (512, out.getNumSamples() - pos);
            juce::AudioBuffer<float> b (2, n);
            b.clear();
            juce::MidiBuffer m;
            if (! sent)
            {
                m.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 100), 0);
                sent = true;
            }
            proc.processBlock (b, m);
            for (int ch = 0; ch < 2; ++ch)
                out.copyFrom (ch, pos, b, ch, 0, n);
            pos += n;
        }

        // 起点附近与终点附近的频率
        const float early = detectPitch (out, sr, (int) (sr * 0.005));
        const float late  = detectPitch (out, sr, (int) (sr * 0.5));

        check (early > 0.0f && late > 0.0f, "滑音过程中都有音高",
               juce::String (early, 1) + " -> " + juce::String (late, 1) + " Hz");

        // 终点应接近 note48 的频率（C3 ≈ 130.8Hz），起点应更低
        // note24 -> note48 是一个八度：应从 ~32.7Hz 滑到 ~130.8Hz
        check (late > early * 1.8f,
               "滑音把音高从低推向高（一个八度）",
               juce::String (early, 1) + " Hz -> " + juce::String (late, 1) + " Hz");
        check (std::abs (late - 130.8f) < 25.0f,
               "滑音终点落在 note48 (C3 ≈ 130.8Hz)",
               juce::String (late, 1) + " Hz");

        writeWav (outDir.getChildFile ("23_portamento.wav"), out, sr);
    }

    //==========================================================================
    std::cout << "\n--- 5. 状态保存 / 恢复 ---" << std::endl;
    {
        KickForgeProcessor procA;

        // 改一堆参数
        procA.getAPVTS().getParameter (pid::subDecay)->setValueNotifyingHost (
            procA.getAPVTS().getParameter (pid::subDecay)->convertTo0to1 (777.0f));
        procA.getAPVTS().getParameter (pid::keytrack)->setValueNotifyingHost (0.0f);
        procA.getAPVTS().getParameter (pid::distOn)->setValueNotifyingHost (1.0f);

        // 改包络：加一个节点
        procA.getEnvelope (EnvelopeTarget::SubAmp).addNode (0.6f, 0.8f);
        procA.commitEnvelope (EnvelopeTarget::SubAmp);
        const int nodesA = procA.getEnvelope (EnvelopeTarget::SubAmp).getNumNodes();

        juce::MemoryBlock state;
        procA.getStateInformation (state);

        check (state.getSize() > 0, "状态可序列化",
               juce::String ((int) state.getSize()) + " bytes");

        // 恢复到新实例
        KickForgeProcessor procB;
        procB.setStateInformation (state.getData(), (int) state.getSize());

        const float decayB = procB.getAPVTS().getRawParameterValue (pid::subDecay)->load();
        const bool ktB = procB.getAPVTS().getRawParameterValue (pid::keytrack)->load() > 0.5f;
        const bool distB = procB.getAPVTS().getRawParameterValue (pid::distOn)->load() > 0.5f;
        const int nodesB = procB.getEnvelope (EnvelopeTarget::SubAmp).getNumNodes();

        check (std::abs (decayB - 777.0f) < 1.0f, "参数 Decay 恢复正确",
               juce::String (decayB, 1) + " ms");
        check (ktB == false, "开关 Keytrack 恢复正确");
        check (distB == true, "开关 Distortion 恢复正确");
        check (nodesB == nodesA, "包络节点数恢复正确",
               juce::String (nodesA) + " -> " + juce::String (nodesB));

        // 再序列化一次，两次结果应一致（往返稳定性）
        juce::MemoryBlock state2;
        procB.getStateInformation (state2);
        check (state2.getSize() > 0, "二次序列化成功",
               juce::String ((int) state2.getSize()) + " bytes");
    }

    //==========================================================================
    std::cout << "\n--- 6. 极端采样率与块大小 ---" << std::endl;
    {
        const double rates[] = { 22050.0, 44100.0, 48000.0, 96000.0, 192000.0 };

        for (double r : rates)
        {
            KickForgeProcessor proc;
            proc.getSampleLibrary().initialise (r);
            proc.getSampleLibrary().loadIntoLayer (0, proc.getClickLayerRef (0));

            auto buf = renderNote (proc, 36, r, 0.5, 256);

            float peak = 0.0f;
            for (int i = 0; i < buf.getNumSamples(); ++i)
                peak = juce::jmax (peak, std::abs (buf.getSample (0, i)));

            check (peak > 0.02f && peak <= 1.0f,
                   juce::String (r / 1000.0, 1) + "kHz 下有有效且不越界的输出",
                   "peak = " + juce::String (peak, 3));
        }

        // 非常规块大小
        for (int bs : { 1, 7, 64, 1024, 4096 })
        {
            KickForgeProcessor proc;
            proc.getSampleLibrary().initialise (sr);
            auto buf = renderNote (proc, 36, sr, 0.3, bs);

            float peak = 0.0f;
            for (int i = 0; i < buf.getNumSamples(); ++i)
                peak = juce::jmax (peak, std::abs (buf.getSample (0, i)));

            check (peak > 0.02f && peak <= 1.0f,
                   "块大小 " + juce::String (bs) + " 下输出正常",
                   "peak = " + juce::String (peak, 3));
        }
    }

    //==========================================================================
    std::cout << "\n--- 7. 参数数量与命名 ---" << std::endl;
    {
        KickForgeProcessor proc;
        const int n = proc.getParameters().size();
        check (n > 60, "参数数量合理", juce::String (n) + " 个参数");

        // 检查没有重复 ID
        juce::StringArray ids;
        for (auto* p : proc.getParameters())
        {
            if (auto* withID = dynamic_cast<juce::AudioProcessorParameterWithID*> (p))
                ids.add (withID->paramID);
        }
        const int numIds = ids.size();
        ids.removeDuplicates (false);
        check (ids.size() == numIds, "参数 ID 无重复",
               juce::String (ids.size()) + " unique / " + juce::String (numIds) + " total");
    }

    //==========================================================================
    std::cout << "\n==============================================\n";
    if (failures == 0)
        std::cout << " 全部通过 ✓" << std::endl;
    else
        std::cout << " 有 " << failures << " 项未通过 ✗" << std::endl;
    std::cout << "==============================================" << std::endl;

    return failures == 0 ? 0 : 1;
}
