//==============================================================================
// WAV 导出 + 预设库 测试
//
// 验证：
//   - 导出的 WAV 内容与实时渲染一致（不是空文件、不是静音）
//   - 尾音自动裁剪，不写多余静音
//   - 不同预设导出的波形确实不同（证明预设真的生效）
//   - 68 个工厂预设都能正确载入
//==============================================================================

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>
#include <cmath>

#include "PluginProcessor.h"
#include "WavExporter.h"

using namespace kf;

static int failures = 0;

static void check (bool cond, const juce::String& what, const juce::String& detail = {})
{
    std::cout << (cond ? "  [PASS] " : "  [FAIL] ") << what;
    if (detail.isNotEmpty())
        std::cout << "   (" << detail << ")";
    std::cout << std::endl;
    if (! cond) ++failures;
}

int main()
{

    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::File outDir ("/Users/serenity/Documents/deepseek-harness/default-workspace/KickForge/selftest_output");
    outDir.createDirectory();

    std::cout << "==============================================\n";
    std::cout << " WAV 导出 + 预设库 测试\n";
    std::cout << "==============================================\n" << std::endl;

    //==========================================================================
    std::cout << "--- 1. 内置预设数量与载入 ---" << std::endl;
    {
        KickForgeProcessor proc;        auto& pm = proc.getPresetManager();

        int factory = 0;
        for (const auto& p : pm.getPresets())
            if (p.isFactory) ++factory;

        check (factory >= 64, "工厂预设 >= 64 个",
               juce::String (factory) + " 个");

        // 逐个载入，任何一个失败都算问题
        int loaded = 0, failed = 0;
        for (int i = 0; i < (int) pm.getPresets().size(); ++i)
        {
            if (! pm.getPresets()[(size_t) i].isFactory)
                continue;
            if (pm.loadPreset (i)) ++loaded; else ++failed;
        }

        check (failed == 0, "全部工厂预设可载入",
               juce::String (loaded) + " 成功 / " + juce::String (failed) + " 失败");

        // 预设名唯一
        juce::StringArray names;
        for (const auto& p : pm.getPresets())
            if (p.isFactory) names.add (p.name);
        const int before = names.size();
        names.removeDuplicates (false);
        check (names.size() == before, "预设名无重复",
               juce::String (names.size()) + " / " + juce::String (before));
    }

    //==========================================================================
    std::cout << "\n--- 2. 导出的 WAV 有真实内容 ---" << std::endl;
    {
        KickForgeProcessor proc;        proc.getSampleLibrary().initialise (44100.0);
        proc.getSampleLibrary().loadIntoLayer (0, proc.getClickLayerRef (0));

        WavExporter::Options opts;
        opts.midiNote = 36;
        opts.sampleRate = 44100.0;
        opts.bitDepth = 24;
        opts.maxSeconds = 8.0;

        double seconds = 0.0;
        float peak = 0.0f;
        auto buf = WavExporter::renderToBuffer (proc, opts, seconds, peak);

        check (buf.getNumSamples() > 0, "渲染出了采样",
               juce::String (buf.getNumSamples()) + " samples");

        // 应当明显短于 maxSeconds（说明尾音裁剪生效）
        check (seconds > 0.1 && seconds < 7.5, "尾音自动裁剪（不是固定 8 秒）",
               juce::String (seconds, 2) + " 秒");

        check (peak > 0.05f, "有实际电平（非静音）",
               juce::String (juce::Decibels::gainToDecibels (peak), 1) + " dBFS");
        check (peak <= 1.001f, "未削顶越界",
               juce::String (peak, 4));

        // 开头应当有声音（起音没被裁掉）
        const float headPeak = buf.getMagnitude (0, 0, juce::jmin (2048, buf.getNumSamples()));
        check (headPeak > 0.01f, "起音部分存在（没被裁掉）",
               juce::String (headPeak, 3));

        // 结尾应当接近静音（淡出生效）
        const int tailLen = juce::jmin (512, buf.getNumSamples());
        const float tailPeak = buf.getMagnitude (0, buf.getNumSamples() - tailLen, tailLen);
        check (tailPeak < headPeak * 0.5f, "结尾已淡出",
               juce::String (tailPeak, 4) + " vs 起始 " + juce::String (headPeak, 3));
    }

    //==========================================================================
    std::cout << "\n--- 3. 导出内容与实时渲染一致 ---" << std::endl;
    {
        // 用同一个处理器做"实时"渲染（分块喂 MIDI），
        // 再和导出结果对比前 N 个采样 —— 两者应当几乎完全一致。
        auto renderRealtime = [] (KickForgeProcessor& p, int totalSamples)
        {
            juce::AudioBuffer<float> out (2, totalSamples);
            out.clear();
            p.prepareToPlay (44100.0, 512);

            int pos = 0;
            bool sent = false;
            while (pos < totalSamples)
            {
                const int n = juce::jmin (512, totalSamples - pos);
                juce::AudioBuffer<float> b (2, n);
                b.clear();
                juce::MidiBuffer m;
                if (! sent)
                {
                    m.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 127), 0);
                    sent = true;
                }
                p.processBlock (b, m);
                for (int ch = 0; ch < 2; ++ch)
                    out.copyFrom (ch, pos, b, ch, 0, n);
                pos += n;
            }
            return out;
        };

        KickForgeProcessor procA, procB;
        procA.getSampleLibrary().initialise (44100.0);
        procA.getSampleLibrary().loadIntoLayer (0, procA.getClickLayerRef (0));
        procB.getSampleLibrary().initialise (44100.0);
        procB.getSampleLibrary().loadIntoLayer (0, procB.getClickLayerRef (0));

        WavExporter::Options opts;
        opts.sampleRate = 44100.0;
        opts.maxSeconds = 4.0;

        double sec = 0.0; float pk = 0.0f;
        auto exported = WavExporter::renderToBuffer (procA, opts, sec, pk);
        auto realtime = renderRealtime (procB, 8192);

        // 比较前 4096 个采样
        const int cmp = juce::jmin (4096, juce::jmin (exported.getNumSamples(),
                                                      realtime.getNumSamples()));
        double maxDiff = 0.0;
        for (int i = 0; i < cmp; ++i)
            maxDiff = juce::jmax (maxDiff,
                                  (double) std::abs (exported.getSample (0, i) - realtime.getSample (0, i)));

        check (maxDiff < 1.0e-5, "导出 == 实时渲染（前 4096 采样）",
               "max diff = " + juce::String (maxDiff, 8));
    }

    //==========================================================================
    std::cout << "\n--- 4. 不同预设导出不同波形 ---" << std::endl;
    {
        auto renderPreset = [&outDir] (const juce::String& presetName, const juce::String& fileTag)
        {
            KickForgeProcessor proc;            proc.getSampleLibrary().initialise (44100.0);
            proc.getSampleLibrary().loadIntoLayer (0, proc.getClickLayerRef (0));
            proc.getPresetManager().loadPresetByName (presetName);

            WavExporter::Options opts;
            opts.sampleRate = 44100.0;
            opts.maxSeconds = 6.0;

            double sec = 0.0; float pk = 0.0f;
            auto buf = WavExporter::renderToBuffer (proc, opts, sec, pk);

            // 顺手把几个预设导成文件，方便直接试听
            if (fileTag.isNotEmpty())
            {
                auto f = outDir.getChildFile ("30_preset_" + fileTag + ".wav");
                f.deleteFile();
                juce::WavAudioFormat fmt;
                if (auto stream = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream()))
                {
                    if (auto w = std::unique_ptr<juce::AudioFormatWriter> (
                            fmt.createWriterFor (stream.get(), 44100.0, 2, 24, {}, 0)))
                    {
                        stream.release();
                        w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
                    }
                }
            }

            return std::make_pair (sec, buf);
        };

        // 预设命名在 v2.7 改为程序化生成（风格 - 子类 NN），
        // 这里跟着换成存在的名字。选的是时长/性格差异明显的几组。
        auto [secInit, bufInit]   = renderPreset ("Basic - Init", "init");
        auto [sec808, buf808]     = renderPreset ("808 - Long 01", "808_long");
        auto [secHard, bufHard]   = renderPreset ("Hardcore - Distorted 01", "hardcore_dist");
        auto [secShort, bufShort] = renderPreset ("Techno - Minimal 01", "techno_min");
        auto [secSub, bufSub]     = renderPreset ("Ambient - Drone 01", "ambient_drone");

        check (sec808 > secInit * 1.5, "808 - Long 比 Init 明显更长",
               juce::String (secInit, 2) + "s vs " + juce::String (sec808, 2) + "s");

        check (secShort < secInit * 1.2, "Techno - Minimal 不比 Init 更长",
               juce::String (secShort, 3) + "s vs " + juce::String (secInit, 2) + "s");

        // 波形互相不同（逐点比较，取最大差）
        auto differs = [] (const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
        {
            const int n = juce::jmin (a.getNumSamples(), b.getNumSamples());
            if (n < 100) return 0.0;
            double d = 0.0;
            for (int i = 0; i < n; i += 7)
                d = juce::jmax (d, (double) std::abs (a.getSample (0, i) - b.getSample (0, i)));
            return d;
        };

        check (differs (buf808, bufInit) > 0.01, "808 - Long 与 Init 波形不同",
               "diff = " + juce::String (differs (buf808, bufInit), 3));
        check (differs (bufHard, bufInit) > 0.01, "Hardcore 与 Init 波形不同",
               "diff = " + juce::String (differs (bufHard, bufInit), 3));
        check (differs (bufShort, bufSub) > 0.01, "Techno - Minimal 与 Ambient - Drone 波形不同",
               "diff = " + juce::String (differs (bufShort, bufSub), 3));

        check (bufHard.getMagnitude (0, 0, bufHard.getNumSamples()) <= 1.001f,
               "Hardcore 预设导出未越界");

        juce::ignoreUnused (secHard, secSub);
    }

    //==========================================================================
    std::cout << "\n--- 5. 采样率 / 位深选项 ---" << std::endl;
    {
        for (double sr : { 44100.0, 48000.0, 96000.0 })
        {
            KickForgeProcessor proc;            proc.getSampleLibrary().initialise (sr);
            proc.getSampleLibrary().loadIntoLayer (0, proc.getClickLayerRef (0));

            WavExporter::Options opts;
            opts.sampleRate = sr;
            opts.maxSeconds = 4.0;

            double sec = 0.0; float pk = 0.0f;
            auto buf = WavExporter::renderToBuffer (proc, opts, sec, pk);

            check (pk > 0.05f && pk <= 1.001f,
                   juce::String (sr / 1000.0, 1) + "kHz 导出正常",
                   "peak " + juce::String (pk, 3) + " / " + juce::String (sec, 2) + "s");
        }

        // 写文件测试（16bit 与 24bit）
        for (int bits : { 16, 24 })
        {
            KickForgeProcessor proc;            proc.getSampleLibrary().initialise (44100.0);
            proc.getSampleLibrary().loadIntoLayer (0, proc.getClickLayerRef (0));

            WavExporter::Options opts;
            opts.sampleRate = 44100.0;
            opts.bitDepth = bits;
            opts.maxSeconds = 3.0;

            double sec = 0.0; float pk = 0.0f;
            auto buf = WavExporter::renderToBuffer (proc, opts, sec, pk);

            auto f = outDir.getChildFile ("31_bitdepth_" + juce::String (bits) + ".wav");
            f.deleteFile();

            juce::WavAudioFormat fmt;
            bool wrote = false;
            if (auto stream = std::unique_ptr<juce::FileOutputStream> (f.createOutputStream()))
            {
                if (auto w = std::unique_ptr<juce::AudioFormatWriter> (
                        fmt.createWriterFor (stream.get(), 44100.0, 2, bits, {}, 0)))
                {
                    stream.release();
                    wrote = w->writeFromAudioSampleBuffer (buf, 0, buf.getNumSamples());
                }
            }

            check (wrote && f.existsAsFile() && f.getSize() > 1000,
                   juce::String (bits) + "bit WAV 写入成功",
                   juce::String (f.getSize() / 1024) + " KB");
        }
    }

    //==========================================================================
    std::cout << "\n--- 6. 归一化选项 ---" << std::endl;
    {
        KickForgeProcessor proc;        proc.getSampleLibrary().initialise (44100.0);
        proc.getSampleLibrary().loadIntoLayer (0, proc.getClickLayerRef (0));

        WavExporter::Options opts;
        opts.sampleRate = 44100.0;
        opts.maxSeconds = 4.0;
        opts.normalizeToDb = -3.0f;

        double sec = 0.0; float pk = 0.0f;
        auto buf = WavExporter::renderToBuffer (proc, opts, sec, pk);

        const float target = juce::Decibels::decibelsToGain (-3.0f);
        check (std::abs (pk - target) < 0.01f, "归一化到 -3dBFS",
               juce::String (juce::Decibels::gainToDecibels (pk), 2) + " dBFS");

        juce::ignoreUnused (buf);
    }

    //==========================================================================
    std::cout << "\n==============================================\n";
    if (failures == 0)
        std::cout << " 全部通过 ✓" << std::endl;
    else
        std::cout << " 有 " << failures << " 项未通过 ✗" << std::endl;
    std::cout << " WAV 输出目录: " << outDir.getFullPathName() << std::endl;
    std::cout << "==============================================" << std::endl;

    return failures == 0 ? 0 : 1;
}
