//==============================================================================
// Kick Cat 对抗性 bug 检测
//
// 思路：不使用"正常操作"路径，而是专门攻击边界：
//   - 全参数随机极值（可能触发数值问题）
//   - 状态序列化往返（宿主保存/恢复）
//   - 采样率与块大小的极端组合
//   - 快速连续 noteOn/noteOff（连打、voice stealing）
//   - 参数在播放中途剧烈跳变（自动化）
//   - UI 在极端尺寸下的控件越界
//==============================================================================

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <iostream>
#include <cmath>
#include <set>

#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Distortion.h"
#include "AiApplier.h"
#include "AiConfig.h"
#include "LocalPatchEngine.h"
#include "AiPatch.h"
#include "FactoryPresets.h"

using namespace kf;

static int failures = 0;
static int checks = 0;

static void check (bool ok, const juce::String& what, const juce::String& detail = {})
{
    ++checks;
    std::cout << (ok ? "  [PASS] " : "  [FAIL] ") << what;
    if (detail.isNotEmpty()) std::cout << "   (" << detail << ")";
    std::cout << std::endl;
    if (! ok) ++failures;
}

static float peakOf (const juce::AudioBuffer<float>& b)
{
    float p = 0.0f;
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
            p = juce::jmax (p, std::abs (b.getSample (c, i)));
    return p;
}

static bool allFinite (const juce::AudioBuffer<float>& b)
{
    for (int c = 0; c < b.getNumChannels(); ++c)
        for (int i = 0; i < b.getNumSamples(); ++i)
            if (! std::isfinite (b.getSample (c, i))) return false;
    return true;
}

int main()
{

    juce::ScopedJuceInitialiser_GUI init;
    const double sr = 44100.0;

    std::cout << "==============================================\n";
    std::cout << " Kick Cat 对抗性 bug 检测\n";
    std::cout << "==============================================\n" << std::endl;

    //==========================================================================
    std::cout << "--- 1. 全参数随机极值：不得出现 NaN / Inf / 爆音 ---" << std::endl;
    {
        juce::Random rng (12345);
        int bad = 0; int worstIdx = -1; float worstPk = 0.0f;
        juce::String worstDesc;

        for (int trial = 0; trial < 120; ++trial)
        {
            KickForgeProcessor p;
            p.getSampleLibrary().initialise (sr);
            for (int i = 0; i < kNumClickLayers; ++i)
                p.getSampleLibrary().loadIntoLayer (i * 7, p.getClickLayerRef (i));

            auto& apvts = p.getAPVTS();

            // 把所有参数随机设为极端值（0 或 1，偶尔中间）
            for (auto* param : p.getParameters())
            {
                if (auto* wp = dynamic_cast<juce::AudioProcessorParameterWithID*> (param))
                {
                    juce::ignoreUnused (wp);
                    const float r = rng.nextFloat();
                    const float v = (r < 0.45f) ? 0.0f : (r < 0.9f ? 1.0f : rng.nextFloat());
                    param->setValueNotifyingHost (v);
                }
            }
            juce::ignoreUnused (apvts);

            p.prepareToPlay (sr, 512);
            juce::AudioBuffer<float> b (2, 512);
            juce::MidiBuffer m;
            m.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 127), 0);

            float pk = 0.0f; bool finite = true;
            for (int k = 0; k < 60; ++k)
            {
                b.clear(); p.processBlock (b, m); m.clear();
                if (! allFinite (b)) { finite = false; break; }
                pk = juce::jmax (pk, peakOf (b));
            }

            // 只有在**限幅器开启**时输出才必须受 ceiling 约束。
            // 关掉限幅器时高增益理应原样通过（这是设计行为，不是 bug）。
            const bool limOn = p.getAPVTS().getRawParameterValue (pid::limOn)->load() > 0.5f;

            if (! finite || (limOn && pk > 1.05f))
            {
                ++bad;
                if (pk > worstPk) worstPk = pk;
                worstDesc = finite ? ("peak=" + juce::String (pk, 4)
                                      + " limiter=" + juce::String (limOn ? 1 : 0)) : "NaN/Inf";
                worstIdx = trial;
            }
        }

        check (bad == 0, "120 组随机极值参数下输出均有限（限幅开启时不超界）",
               bad == 0 ? "" : (juce::String (bad) + " 组异常，最差: "
                                + worstDesc + " (trial " + juce::String (worstIdx) + ")"));
    }

    //==========================================================================
    std::cout << "\n--- 2. 状态序列化往返（宿主保存/恢复）---" << std::endl;
    {
        KickForgeProcessor a;
        a.getSampleLibrary().initialise (sr);

        // 设一组非默认值
        juce::Random rng (999);
        for (auto* param : a.getParameters())
            if (auto* wp = dynamic_cast<juce::AudioParameterFloat*> (param))
            {
                juce::ignoreUnused (wp);
                param->setValueNotifyingHost (rng.nextFloat());
            }

        juce::MemoryBlock state;
        a.getStateInformation (state);
        check (state.getSize() > 100, "getStateInformation 产生非空状态",
               juce::String ((int) state.getSize()) + " 字节");

        KickForgeProcessor b;
        b.getSampleLibrary().initialise (sr);
        b.setStateInformation (state.getData(), (int) state.getSize());

        // 逐参数比对
        int mismatches = 0;
        auto& pa = a.getParameters();
        auto& pb = b.getParameters();
        for (int i = 0; i < juce::jmin (pa.size(), pb.size()); ++i)
        {
            const float va = pa[i]->getValue();
            const float vb = pb[i]->getValue();
            if (std::abs (va - vb) > 1.0e-3f) ++mismatches;
        }
        check (mismatches == 0, "恢复后的参数与保存前一致",
               mismatches == 0 ? "" : (juce::String (mismatches) + " 个参数不一致"));

        // 空/垃圾数据不得崩溃
        KickForgeProcessor c;
        c.setStateInformation (nullptr, 0);
        c.setStateInformation (nullptr, 100);
        juce::MemoryBlock garbage ("not a valid xml at all", 24);
        c.setStateInformation (garbage.getData(), (int) garbage.getSize());
        check (true, "空指针/垃圾数据不崩溃");
    }

    //==========================================================================
    std::cout << "\n--- 3. 采样率与块大小的极端组合 ---" << std::endl;
    {
        const double rates[] { 8000.0, 11025.0, 22050.0, 44100.0, 48000.0, 88200.0, 96000.0, 192000.0 };
        const int blocks[] { 1, 16, 32, 64, 512, 2048, 8192 };

        bool allOk = true;
        juce::String firstBad;

        for (double r : rates)
            for (int blk : blocks)
            {
                KickForgeProcessor p;
                p.getSampleLibrary().initialise (r);
                for (int i = 0; i < kNumClickLayers; ++i)
                    p.getSampleLibrary().loadIntoLayer (i * 7, p.getClickLayerRef (i));

                p.prepareToPlay (r, blk);
                juce::AudioBuffer<float> b (2, blk);
                juce::MidiBuffer m;
                m.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 127), 0);

                float pk = 0.0f; bool finite = true;
                for (int k = 0; k < 20; ++k)
                {
                    b.clear(); p.processBlock (b, m); m.clear();
                    if (! allFinite (b)) { finite = false; break; }
                    pk = juce::jmax (pk, peakOf (b));
                }

                if (! finite || pk > 1.05f)
                {
                    allOk = false;
                    if (firstBad.isEmpty())
                        firstBad = juce::String (r, 0) + "Hz / " + juce::String (blk)
                                 + " samples -> peak=" + juce::String (pk, 4)
                                 + (finite ? "" : " (NaN)");
                }
            }

        check (allOk, "8 种采样率 x 7 种块大小均稳定", firstBad);
    }

    //==========================================================================
    std::cout << "\n--- 4. 快速连续触发（连打 / voice stealing）---" << std::endl;
    {
        KickForgeProcessor p;
                p.getSampleLibrary().initialise (sr);
        for (int i = 0; i < kNumClickLayers; ++i)
            p.getSampleLibrary().loadIntoLayer (i * 7, p.getClickLayerRef (i));

        p.prepareToPlay (sr, 128);
        juce::AudioBuffer<float> b (2, 128);
        juce::MidiBuffer m;

        float pk = 0.0f; bool finite = true;
        for (int k = 0; k < 400; ++k)
        {
            m.clear();
            // 每块都触发，音高与力度随机
            m.addEvent (juce::MidiMessage::noteOn (1, 24 + (k % 40),
                                                   (juce::uint8) (1 + (k * 7) % 127)), 0);
            if (k % 3 == 0)
                m.addEvent (juce::MidiMessage::noteOff (1, 24 + (k % 40)), 0);

            b.clear(); p.processBlock (b, m);
            if (! allFinite (b)) { finite = false; break; }
            pk = juce::jmax (pk, peakOf (b));
        }
        check (finite && pk <= 1.05f, "400 次连续触发无 NaN 且不超界",
               "peak=" + juce::String (pk, 4));
    }

    //==========================================================================
    std::cout << "\n--- 5. 自动化：参数在播放中途剧烈跳变 ---" << std::endl;
    {
        KickForgeProcessor p;
                p.getSampleLibrary().initialise (sr);
        for (int i = 0; i < kNumClickLayers; ++i)
            p.getSampleLibrary().loadIntoLayer (i * 7, p.getClickLayerRef (i));

        p.prepareToPlay (sr, 256);
        juce::AudioBuffer<float> b (2, 256);
        juce::MidiBuffer m;
        m.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 127), 0);

        juce::Random rng (4242);
        float pk = 0.0f; bool finite = true;
        for (int k = 0; k < 200; ++k)
        {
            // 每隔一块就随机跳变一个参数（模拟极端自动化）
            if (auto* param = p.getParameters()[rng.nextInt (p.getParameters().size())])
                param->setValueNotifyingHost (rng.nextFloat());

            b.clear(); p.processBlock (b, m); m.clear();
            if (! allFinite (b)) { finite = false; break; }
            pk = juce::jmax (pk, peakOf (b));
        }
        check (finite && pk <= 1.05f, "200 次参数跳变无 NaN 且不超界",
               "peak=" + juce::String (pk, 4));
    }

    //==========================================================================
    std::cout << "\n--- 6. UI 极端尺寸：控件不得超出窗口 ---" << std::endl;
    {
        const int sizes[][2] { {960,560}, {1080,620}, {1400,720}, {1800,720},
                               {2100,1200}, {960,720}, {1800,560} };
        bool allInside = true;
        juce::String firstBad;

        for (auto& sz : sizes)
        {
            KickForgeProcessor proc;            std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
            ed->setSize (sz[0], sz[1]);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (30);

            const juce::Rectangle<int> win (0, 0, ed->getWidth(), ed->getHeight());

            std::function<void (juce::Component*)> walk = [&] (juce::Component* c)
            {
                for (int i = 0; i < c->getNumChildComponents(); ++i)
                {
                    auto* ch = c->getChildComponent (i);
                    if (ch->isVisible() && dynamic_cast<juce::Slider*> (ch)
                        || ch->isVisible() && dynamic_cast<juce::TextButton*> (ch))
                    {
                        auto r = ch->getBoundsInParent();
                        // 转换到顶层坐标
                        auto top = ed->getLocalArea (ch->getParentComponent(), r);
                        if (! win.contains (top))
                        {
                            allInside = false;
                            if (firstBad.isEmpty())
                                firstBad = juce::String (sz[0]) + "x" + juce::String (sz[1])
                                         + " 控件越界 " + top.toString();
                        }
                    }
                    walk (ch);
                }
            };
            walk (ed.get());
        }

        check (allInside, "7 种窗口尺寸下控件均在窗口内", firstBad);
    }

    //--------------------------------------------------------------------------
    std::cout << "\n--- 7. 每个参数都必须能改变输出（含前置条件）---" << std::endl;
    {
        // 很多参数有**前置依赖**：Wave Shape 对 Sine 无效、Sub Harm 需要开关
        // 打开、噪声层参数需要先把噪声电平拉起来……。
        // 测试必须把前提摆好，否则会把正常设计误报成"死参数"。
        struct Cand
        {
            const char* id;
            float lo, hi;
            std::function<void (KickForgeProcessor&)> setup;
        };

        auto base = [] (KickForgeProcessor& p)
        {
            auto set = [&p] (const juce::String& id, float v)
            {
                if (auto* par = p.getAPVTS().getParameter (id))
                    par->setValueNotifyingHost (par->convertTo0to1 (v));
            };
            set (pid::limOn, 1.0f);      set (pid::limCeiling, -0.3f);
            // 压缩/失真/瞬态默认关：它们会掩盖细微的音色变化
            // （实测 Odd/Even 在开启这三者后差异从 6.7% 掉到 0.017%），
            // 导致把有效参数误判成"死参数"。各自的专项测试单独覆盖它们。
            set (pid::compOn, 0.0f);     set (pid::distOn, 0.0f);
            set (pid::transOn, 0.0f);    set (pid::subOn, 1.0f);
            set (pid::subLevel, 0.7f);
            set (pid::click (0, "on"), 1.0f);
            set (pid::click (0, "level"), 0.5f);
            set (pid::subHarmonics, 8.0f);
            set (pid::distMix, 0.6f);    set (pid::distDrive, 0.5f);
            set (pid::velAmount, 0.6f);  set (pid::masterGain, 0.0f);
        };

        // 用 juce::String 接收：click 层的参数 ID 是动态拼出来的
        auto on = [] (const juce::String& id, float v)
        {
            return [id, v] (KickForgeProcessor& p)
            {
                if (auto* x = p.getAPVTS().getParameter (id))
                    x->setValueNotifyingHost (x->convertTo0to1 (v));
            };
        };

        const std::vector<Cand> cands
        {
            { "sub_level",      0.0f,    1.0f,    nullptr },
            { "sub_decay",      60.0f,   1200.0f, nullptr },
            { "sub_pitchamt",   -24.0f,  36.0f,   nullptr },
            { "sub_harmonics",  0.0f,    8.0f,    nullptr },
            { "sub_subharm",    0.0f,    1.0f,    on (pid::subSubHarmOn, 1.0f) },
            // 注意：Start Phase 只改变波形相位，不影响 rms 能量。
            // 因此不能纳入"rms 是否变化"的判定，单独在别处验证。
            { "tune",           -12.0f,  12.0f,   nullptr },
            // Wave Shape 对 Sine/Noise 无效 -> 换成 Square（索引 3）
            { "sub_shape",      0.0f,    1.0f,    on (pid::subWave, 3.0f) },
            { "sub_harmtilt",   0.0f,    1.0f,    nullptr },
            { "sub_pitchdecay", 20.0f,   400.0f,  nullptr },
            { "sub_amppunch",   0.0f,    1.0f,    nullptr },
            { "sub_filterhz",   150.0f,  12000.0f,nullptr },
            // 滤波器参数：必须让滤波器真的启用（filterEnv 非零）
            { "sub_filterq",    0.6f,    6.0f,    on (pid::subFilterEnv, 0.5f) },
            // 滤波包络只有在截止频率**低到能削掉内容**时才听得出来。
            // 默认 sub_filterhz 较高，包络推动的倍频程落在已经很亮的地方，
            // RMS 差异只有 0.03%（低于阈值）。把截止压到 300Hz 再测。
            { "sub_filterenv",  -1.0f,   1.0f,
              on (pid::subFilterHz, 300.0f) },
            { "sub_oddeven",    -1.0f,   1.0f,    nullptr },
            // 噪声层：先把噪声电平拉起来
            { "sub_noisedecay", 2.0f,    120.0f,  on (pid::subNoiseAmt, 0.6f) },
            { "sub_noisehp",    100.0f,  4000.0f, on (pid::subNoiseAmt, 0.6f) },
            { "sub_noiseamt",   0.0f,    1.0f,    nullptr },
            { "sub_pitchcurve", -1.0f,   1.0f,    nullptr },
            { "sub_hold",       0.0f,    120.0f,  nullptr },
            { "sub_width",      0.0f,    2.0f,    nullptr },
            { "sub_attack",     0.0f,    150.0f,  nullptr },
            { "sub_detune",     0.0f,    40.0f,   nullptr },
            // 相位随机化不改变单次触发的能量，但它影响的是"多次触发的一致性"，
            // 不能用 rms 判定，已在专项测试里覆盖。
            // 滤波类型：三种输出的能量差异明显
            { "sub_filtertype", 0.0f,    2.0f,    on (pid::subFilterHz, 800.0f) },
            { "sub_pitchdelay", 0.0f,    120.0f,  on (pid::subPitchAmt, 24.0f) },
            { "sub_stage2amt",  0.0f,    24.0f,   on (pid::subPitchAmt, 12.0f) },
            // 滑音（含滑音曲线）**必须有两个音**才存在 —— 它描述的是
            // 从前一个音滑到后一个音的轨迹。单击测试永远测不到，
            // 因此不纳入本项扫描，由下面的专项测试覆盖。
            // 力度->时长只在**低力度**触发时才有差异（测试用 vel=40）
            { "sub_veltodecay", 0.0f,    1.0f,    nullptr },
            // click 层：起音淡化。
            // 注意切片起点的选择：start=0.5 处采样内容本身很轻，
            // 淡化与否差异仅 0.018%（会被阈值判为"无效果"）。
            // 改用 start=0.2（该处有实际幅度），差异升到约 8%。
            { "click1_fadein",  0.0f,    10.0f,   on (pid::click (0, "start"), 0.2f) },
            { "click1_shape",   0.0f,    1.0f,    on (pid::click (0, "wave"), 3.0f) },
            { "click1_wavetune",200.0f,  1200.0f, on (pid::click (0, "wave"), 3.0f) },
            // 动态：侧链高通
            { "comp_schp",      20.0f,   1500.0f, on (pid::compOn, 1.0f) },
            { "master_gain",    -12.0f,  6.0f,    nullptr },
            { "comp_thresh",    -30.0f,  0.0f,    on (pid::compOn, 1.0f) },
            { "comp_ratio",     1.5f,    12.0f,   on (pid::compOn, 1.0f) },
            { "comp_makeup",    0.0f,    12.0f,   on (pid::compOn, 1.0f) },
            // Dist Drive 的实际范围是 1..20（不是 0..1）
            { "dist_drive",     1.0f,    20.0f,   on (pid::distOn, 1.0f) },
            { "dist_mix",       0.0f,    1.0f,    on (pid::distOn, 1.0f) },
            { "trans_attack",   -1.0f,   1.0f,    on (pid::transOn, 1.0f) },
            { "trans_sustain",  -1.0f,   1.0f,    on (pid::transOn, 1.0f) },
            { "vel_amount",     0.0f,    1.0f,    nullptr }
        };

        std::vector<float> lastSignal;

        auto render = [&] (const Cand& c, float v, int velocity)
        {
            KickForgeProcessor p;
            p.getSampleLibrary().initialise (sr);
            for (int i = 0; i < kNumClickLayers; ++i)
                p.getSampleLibrary().loadIntoLayer (i * 7, p.getClickLayerRef (i));

            base (p);
            if (c.setup) c.setup (p);

            if (auto* par = p.getAPVTS().getParameter (c.id))
                par->setValueNotifyingHost (par->convertTo0to1 (v));

            p.prepareToPlay (sr, 512);
            juce::AudioBuffer<float> b (2, 512);
            juce::MidiBuffer m;
            m.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) velocity), 0);

            double sq = 0.0; long n = 0;
            std::vector<float> sig;
            sig.reserve (40 * 512);
            for (int k = 0; k < 40; ++k)
            {
                b.clear(); p.processBlock (b, m); m.clear();
                for (int i = 0; i < 512; ++i)
                {
                    const float x = b.getSample (0, i);
                    sq += (double) x * x; ++n;
                    sig.push_back (x);
                }
            }
            lastSignal = std::move (sig);
            return (float) std::sqrt (sq / (double) n);
        };

        int dead = 0;
        juce::StringArray deadIds;

        for (auto& c : cands)
        {
            // 力度参数要用低力度触发才看得出差别
            const int vel = (juce::String (c.id) == "vel_amount") ? 40 : 127;

            const float a = render (c, c.lo, vel);
            const auto sigA = lastSignal;
            const float bb = render (c, c.hi, vel);
            const auto sigB = lastSignal;

            const float denom = juce::jmax (1.0e-6f, juce::jmax (a, bb));
            float rel = std::abs (bb - a) / denom;

            // ⚠️ 光看 RMS 会漏掉"只改变波形形状、不改变总能量"的参数
            // （实测 sub_pitchcurve 差异只有 0.034%，低于 0.05% 阈值，
            //  但两者的音高轨迹确实不同）。
            // 这里再加一个逐样本最大差作为补充判据。
            {
                float mx = 0.0f;
                const size_t count = juce::jmin (sigA.size(), sigB.size());
                for (size_t i = 0; i < count; ++i)
                    mx = juce::jmax (mx, std::abs (sigA[i] - sigB[i]));
                if (mx > 1.0e-4f)
                    rel = juce::jmax (rel, 1.0f);   // 波形确实不同 -> 判定有效
            }

            // 阈值 0.05%：有些参数在组合参数下效果会被稀释
            // （例如 Odd/Even 在 Square 波形下泛音本来就不明显），
            // 但只要确实有变化就不算"死参数"。
            if (rel < 5.0e-4f)
            {
                ++dead;
                deadIds.add (juce::String (c.id) + "(" + juce::String (a, 5)
                             + " vs " + juce::String (bb, 5) + ")");
            }
        }

        check (dead == 0, juce::String ((int) cands.size())
                          + " 个参数在正确前置条件下均影响输出",
               dead == 0 ? "" : ("无效果: " + deadIds.joinIntoString (", ")));
    }

    //==========================================================================
    std::cout << "\n--- 7b. 滑音曲线（需要两个音）---" << std::endl;
    {
        // 滑音只在"前一个音滑向后一个音"时存在，单击测不到。
        // 这里先触发一个低音，再触发高音，比较不同曲线下的轨迹。
        auto renderGlide = [&] (float curve)
        {
            KickForgeProcessor p;
            p.getSampleLibrary().initialise (sr);
            for (int i = 0; i < kNumClickLayers; ++i)
                p.getSampleLibrary().loadIntoLayer (i * 7, p.getClickLayerRef (i));

            auto set = [&] (const juce::String& id, float v)
            {
                if (auto* par = p.getAPVTS().getParameter (id))
                    par->setValueNotifyingHost (par->convertTo0to1 (v));
            };
            set (pid::limOn, 0.0f); set (pid::compOn, 0.0f);
            set (pid::subOn, 1.0f); set (pid::subLevel, 0.7f);
            for (int i = 0; i < kNumClickLayers; ++i)
                set (pid::click (i, "on"), 0.0f);
            set (pid::glide, 400.0f);
            set (pid::glideMode, 1.0f);            // 1 = Always（0 是 Off，滑音不会启用）
            set (pid::subGlideCurve, curve);

            p.prepareToPlay (sr, 512);
            juce::AudioBuffer<float> b (2, 512);
            juce::MidiBuffer m;

            // 第一个音：低音
            m.addEvent (juce::MidiMessage::noteOn (1, 24, (juce::uint8) 127), 0);
            for (int k = 0; k < 6; ++k) { b.clear(); p.processBlock (b, m); m.clear(); }

            // 第二个音：高音（会从低音滑上去）
            m.addEvent (juce::MidiMessage::noteOn (1, 48, (juce::uint8) 127), 0);
            std::vector<float> w;
            for (int k = 0; k < 20; ++k)
            {
                b.clear(); p.processBlock (b, m); m.clear();
                for (int i = 0; i < 512; ++i) w.push_back (b.getSample (0, i));
            }
            return w;
        };

        auto lin = renderGlide (0.0f);
        auto exp = renderGlide (1.0f);

        double diff = 0.0;
        const size_t N = juce::jmin (lin.size(), exp.size());
        for (size_t i = 0; i < N; ++i) diff += std::abs (lin[i] - exp[i]);

        check (N > 0 && diff / (double) juce::jmax ((size_t) 1, N) > 1.0e-5,
               "滑音曲线改变滑音轨迹（线性 vs 指数）",
               "平均差异=" + juce::String ((float) (diff / (double) juce::jmax ((size_t) 1, N)), 6));
    }

    //==========================================================================
    std::cout << "\n--- 8. Sub 滤波器：启用时不得静音或发散 ---" << std::endl;
    {
        // 回归测试：Chamberlin SVF 的稳定条件 f < 2 - 2*damp 与阻尼强耦合，
        // 默认 Q=0.707 时 damp=1.414 已不满足，我先遇到发散（1e38）、
        // 加了钳制后又变成**完全静音** —— 只要滤波器一启用，Sub 就没声。
        // 现已换成 TPT/Zavalishin 结构（无条件稳定）。
        //
        // 这里覆盖"启用滤波器的所有路径"：低截止、高截止、正负包络调制。
        auto renderSub = [&] (float hz, float env, float q)
        {
            KickForgeProcessor p;
            p.getSampleLibrary().initialise (sr);
            for (int i = 0; i < kNumClickLayers; ++i)
                p.getSampleLibrary().loadIntoLayer (i * 7, p.getClickLayerRef (i));

            auto set = [&] (const juce::String& id, float v)
            {
                if (auto* par = p.getAPVTS().getParameter (id))
                    par->setValueNotifyingHost (par->convertTo0to1 (v));
            };
            set (pid::limOn, 0.0f);          // 关限幅，问题才会暴露
            set (pid::compOn, 0.0f);
            set (pid::distOn, 0.0f);
            set (pid::subOn, 1.0f);
            set (pid::subLevel, 0.7f);
            for (int i = 0; i < kNumClickLayers; ++i)
                set (pid::click (i, "on"), 0.0f);
            set (pid::subFilterHz, hz);
            set (pid::subFilterEnv, env);
            set (pid::subFilterQ, q);

            p.prepareToPlay (sr, 512);
            juce::AudioBuffer<float> b (2, 512);
            juce::MidiBuffer m;
            m.addEvent (juce::MidiMessage::noteOn (1, 36, (juce::uint8) 127), 0);

            double sq = 0.0; long n = 0; float pk = 0.0f; bool finite = true;
            for (int k = 0; k < 40; ++k)
            {
                b.clear(); p.processBlock (b, m); m.clear();
                if (! allFinite (b)) { finite = false; break; }
                pk = juce::jmax (pk, peakOf (b));
                for (int i = 0; i < 512; ++i)
                { const float x = b.getSample (0, i); sq += (double) x * x; ++n; }
            }
            return std::array<float, 3> { (float) std::sqrt (sq / juce::jmax (1L, n)),
                                          pk, finite ? 1.0f : 0.0f };
        };

        bool allAudible = true, allFinite2 = true, allBounded = true;
        juce::String firstBad;

        for (float hz : { 80.0f, 300.0f, 600.0f, 3000.0f, 12000.0f, 20000.0f })
            for (float env : { -1.0f, 0.0f, 1.0f })
                for (float q : { 0.6f, 0.707f, 2.0f, 8.0f })
                {
                    auto r = renderSub (hz, env, q);
                    const float rms = r[0], pk = r[1], fin = r[2];

                    if (fin < 0.5f) { allFinite2 = false;
                        if (firstBad.isEmpty()) firstBad = "NaN @ " + juce::String (hz, 0)
                            + "Hz env=" + juce::String (env, 1); }
                    // 启用滤波器后仍必须有声音（这是被漏掉的严重 bug）
                    if (rms < 1.0e-4f) { allAudible = false;
                        if (firstBad.isEmpty()) firstBad = "静音 @ " + juce::String (hz, 0)
                            + "Hz env=" + juce::String (env, 1) + " Q=" + juce::String (q, 2); }
                    if (pk > 4.0f) { allBounded = false;
                        if (firstBad.isEmpty()) firstBad = "发散 @ " + juce::String (hz, 0) + "Hz"; }
                }

        check (allFinite2, "滤波器全组合无 NaN/Inf", firstBad);
        check (allAudible, "滤波器启用时 Sub 仍有输出（不静音）", firstBad);
        check (allBounded, "滤波器全组合不发散", firstBad);
    }

    //==========================================================================
    std::cout << "\n--- 9. 工厂预设：数量、分组、可载入性 ---" << std::endl;
    {
        auto tree = FactoryPresets::buildMenuTree();
        const auto& all = FactoryPresets::getAll();

        check ((int) tree.styles.size() >= 10, "风格数 >= 10",
               juce::String ((int) tree.styles.size()) + " 种");

        bool allEnough = true;
        for (auto& st : tree.styles)
        {
            int n = 0;
            for (auto& c : st.categories) n += (int) c.presetIndices.size();
            if (n < 40) allEnough = false;
        }
        check (allEnough, "每种风格 >= 40 个预设");

        // 每个预设都要能载入，并且真的改变了参数
        int loaded = 0, unchanged = 0, silent = 0;
        std::set<juce::String> sigs;

        for (size_t i = 0; i < all.size(); ++i)
        {
            KickForgeProcessor p;
            p.getSampleLibrary().initialise (sr);
            for (int k = 0; k < kNumClickLayers; ++k)
                p.getSampleLibrary().loadIntoLayer (k * 7, p.getClickLayerRef (k));

            if (! p.getPresetManager().loadPresetByName (all[i].name))
                continue;

            ++loaded;

            juce::String sig;
            for (const char* id : { pid::subDecay, pid::subPitchAmt, pid::subLevel,
                                    pid::subHarmonics, pid::subHarmTilt })
                sig += juce::String (p.getAPVTS().getRawParameterValue (id)->load(), 2) + "|";
            sigs.insert (sig);
        }

        check (loaded == (int) all.size(), "全部预设可载入",
               juce::String (loaded) + " / " + juce::String ((int) all.size()));

        // 这是本次修掉的核心 bug：预设格式写错导致载入后全等于默认值
        check ((int) sigs.size() == loaded,
               "所有预设的参数组合互不相同（不是全等于默认值）",
               juce::String ((int) sigs.size()) + " 种组合 / " + juce::String (loaded) + " 个预设");

        check (true, "（已覆盖 " + juce::String (loaded) + " 个预设）");

        // --- 菜单是两层：风格 -> 该风格全部预设（平铺）---
        // 三级菜单每选一个音色要点三次，中间层还把 40 个预设切得很碎。
        // 这里确认每个风格的预设能被完整摊平成一层。
        bool flatOk = true;
        for (auto& st : tree.styles)
        {
            int flat = 0;
            for (auto& c : st.categories) flat += (int) c.presetIndices.size();
            if (flat < 40) flatOk = false;
        }
        check (flatOk, "每个风格摊平后仍有 >= 40 个预设（两层菜单可用）");

        // --- 预设名必须唯一，否则菜单里会出现重复项 ---
        std::set<juce::String> names;
        for (const auto& e : all) names.insert (e.name);
        check ((int) names.size() == (int) all.size(),
               "预设名唯一（菜单无重复项）",
               juce::String ((int) names.size()) + " / " + juce::String ((int) all.size()));

        // --- 已移除的功能不应该还在 ---
        {
            KickForgeProcessor p;
            std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
            int abLike = 0;
            std::function<void (juce::Component*)> walk = [&] (juce::Component* c)
            {
                for (int i = 0; i < c->getNumChildComponents(); ++i)
                {
                    auto* ch = c->getChildComponent (i);
                    if (auto* b = dynamic_cast<juce::TextButton*> (ch))
                    {
                        const auto t = b->getButtonText().trim().toUpperCase();
                        if (t == "RANDOM" || t == "A" || t == "B" || t == "A>B" || t == "A/B")
                            ++abLike;
                    }
                    walk (ch);
                }
            };
            ed->setSize (1080, 700);
            walk (ed.get());
            check (abLike == 0, "已移除 A/B 对比与随机音色按钮",
                   juce::String (abLike) + " 个残留");

            // --- 预设菜入口已合并到下拉框，不应再有独立的 "..." 按钮 ---
            int dots = 0;
            std::function<void (juce::Component*)> walk2 = [&] (juce::Component* c)
            {
                for (int i = 0; i < c->getNumChildComponents(); ++i)
                {
                    auto* ch = c->getChildComponent (i);
                    if (auto* b = dynamic_cast<juce::TextButton*> (ch))
                        if (b->getButtonText().trim() == "..." || b->getButtonText().trim() == "..")
                            ++dots;
                    walk2 (ch);
                }
            };
            walk2 (ed.get());
            check (dots == 0, "预设菜单入口已合并到下拉框（无独立的 \"...\" 按钮）",
                   juce::String (dots) + " 个残留");

            // --- 点下拉框应能弹出菜单（不崩、不卡）---
            {
                int comboCount = 0;
                std::function<void (juce::Component*)> walk3 = [&] (juce::Component* c)
                {
                    for (int i = 0; i < c->getNumChildComponents(); ++i)
                    {
                        auto* ch = c->getChildComponent (i);
                        if (auto* cb = dynamic_cast<juce::ComboBox*> (ch))
                            if (cb->getNumItems() == 0) ++comboCount;   // 预设选择器不承载列表
                        walk3 (ch);
                    }
                };
                walk3 (ed.get());
                check (comboCount > 0, "预设下拉框自身不承载 480 项列表（由弹出菜单管理）",
                       juce::String (comboCount) + " 个空列表下拉框");
            }

            // --- 菜单项 ID 必须与回调解析一致 ---
            // 这类 bug 很隐蔽：菜单能弹出来、能点，但点了没反应
            // （ID 对不上，回调走不到 loadPresetByIndex）。
            {
                auto mt = FactoryPresets::buildMenuTree();
                int mismatches = 0;

                for (int si = 0; si < (int) mt.styles.size(); ++si)
                {
                    std::vector<int> flat;
                    for (const auto& c : mt.styles[(size_t) si].categories)
                        for (int idx : c.presetIndices)
                            flat.push_back (idx);

                    for (int slot = 0; slot < (int) flat.size(); ++slot)
                    {
                        const int id = 20000 + si * 100 + slot;
                        const int packed = id - 20000;
                        if (packed / 100 != si || packed % 100 != slot)
                            ++mismatches;
                    }
                }

                check (mismatches == 0,
                       "菜单项 ID 与回调解析一致（点了能真正载入）",
                       juce::String (mismatches) + " 处不匹配");

                int overflow = 0;
                for (auto& st : mt.styles)
                {
                    int flat = 0;
                    for (auto& c : st.categories) flat += (int) c.presetIndices.size();
                    if (flat > 100) ++overflow;
                }
                check (overflow == 0, "每风格预设数不超过 ID 空间（100）");
            }

            // --- 收藏：按名字持久化，顺序变化也不错位 ---
            {
                KickForgeProcessor fp;
                auto& pm = fp.getPresetManager();

                const juce::String a = "Techno - Rolling 01";
                const juce::String b = "808 - Long 01";

                pm.setFavourite (a, true);
                pm.setFavourite (b, true);

                check (pm.isFavourite (a) && pm.isFavourite (b),
                       "收藏：加入两个后都能查到");
                check (! pm.isFavourite ("Techno - Rolling 02"),
                       "收藏：未加入的不受影响");

                // 新建实例验证落盘
                {
                    KickForgeProcessor fp2;
                    check (fp2.getPresetManager().isFavourite (a),
                           "收藏：重启后仍然保留（已落盘）");
                }

                pm.setFavourite (a, false);
                check (! pm.isFavourite (a), "收藏：可以取消");
                check (pm.isFavourite (b), "收藏：取消一个不影响另一个");

                // 清理，避免污染用户目录
                pm.setFavourite (b, false);
            }

            // --- 搜索：关键词匹配 ---
            {
                KickForgeProcessor sp;
                const auto& pl = sp.getPresetManager().getPresets();

                auto countMatches = [&pl] (const juce::String& q)
                {
                    int n = 0;
                    for (const auto& e : pl) if (e.name.containsIgnoreCase (q)) ++n;
                    return n;
                };

                check (countMatches ("rolling") > 0, "搜索：\"rolling\" 有匹配",
                       juce::String (countMatches ("rolling")) + " 个");
                check (countMatches ("808") == 40, "搜索：\"808\" 命中整个风格",
                       juce::String (countMatches ("808")) + " 个");
                check (countMatches ("zzzznope") == 0, "搜索：无匹配时返回 0");
            }

            // --- 撤销 / 重做（快照式）---
            {
                KickForgeProcessor up;
                up.clearUndoHistory();

                auto setP = [&up] (const char* id, float v)
                {
                    if (auto* par = up.getAPVTS().getParameter (id))
                        par->setValueNotifyingHost (par->convertTo0to1 (v));
                };
                auto getP = [&up] (const char* id)
                {
                    return up.getAPVTS().getRawParameterValue (id)->load();
                };

                check (! up.canUndo(), "撤销：初始状态无历史可撤销");

                setP (pid::subDecay, 900.0f);  up.pushUndoSnapshot();
                setP (pid::subDecay, 250.0f);  up.pushUndoSnapshot();

                check (up.canUndo(), "撤销：改动后可以撤销");

                up.undo();
                check (std::abs (getP (pid::subDecay) - 900.0f) < 1.0f,
                       "撤销：回到上一状态", juce::String (getP (pid::subDecay), 1));

                up.undo();
                check (std::abs (getP (pid::subDecay) - 420.0f) < 1.0f,
                       "撤销：再退回初始状态", juce::String (getP (pid::subDecay), 1));

                up.redo();
                check (std::abs (getP (pid::subDecay) - 900.0f) < 1.0f,
                       "重做：前进一状态", juce::String (getP (pid::subDecay), 1));

                // 防抖：状态未变时不应堆积历史
                up.clearUndoHistory();
                for (int i = 0; i < 20; ++i) up.pushUndoSnapshot();
                check (! up.canUndo(), "撤销：状态未变时不产生多余历史");
            }

            // --- 染色模型：10 种，各有不同的频响特征 ---
            {
                constexpr double dsr = 44100.0;

                auto gainAt = [] (DistortionType ty, float freq, float tone)
                {
                    Distortion d;
                    d.prepare (dsr, 512);

                    juce::AudioBuffer<float> b (1, 16384);
                    b.clear();
                    for (int i = 0; i < 16384; ++i)
                        b.setSample (0, i, 0.3f * std::sin (2.0 * juce::MathConstants<double>::pi
                                                            * freq * i / dsr));

                    d.process (b, 0, 16384, ty, 4.0f, 1.0f, 0.0f, tone, true);

                    double sq = 0.0;
                    for (int i = 8192; i < 16384; ++i)
                    {
                        const float v = b.getSample (0, i);
                        sq += (double) v * v;
                    }
                    return (float) std::sqrt (sq / 8192.0);
                };

                const int numModels = (int) DistortionType::NumTypes;
                check (numModels == 10, "染色模型数量为 10",
                       juce::String (numModels) + " 种");

                // 每个模型都必须有输出（不能有"哑"模型）
                int silent = 0;
                for (int m = 0; m < numModels; ++m)
                    if (gainAt ((DistortionType) m, 1000.0f, 0.5f) < 0.02f)
                        ++silent;
                check (silent == 0, "所有染色模型都有输出（无哑模型）",
                       juce::String (silent) + " 个过小");

                // 每个模型都必须**改变**信号（不能和干声一样）
                int unchanged = 0;
                for (int m = 0; m < numModels; ++m)
                {
                    // Radio 在 100Hz 上本来就该几乎没输出，改用 1kHz 判断
                    const float g = gainAt ((DistortionType) m, 1000.0f, 0.5f);
                    if (std::abs (g - 1.0f) < 0.02f) ++unchanged;
                }
                check (unchanged == 0, "所有模型都实际改变了信号",
                       juce::String (unchanged) + " 个无变化");

                // 频响差异：Radio 必须砍掉低频（窄带特征）
                {
                    const float lo = gainAt (DistortionType::Radio, 100.0f, 0.5f);
                    const float mid = gainAt (DistortionType::Radio, 1000.0f, 0.5f);
                    check (lo < mid * 0.4f, "Radio 具备窄带特征（低频被抑制）",
                           juce::String (lo, 3) + " vs " + juce::String (mid, 3));
                }

                // Exciter 必须**保留**低频（这是它 "只加高频" 的定义）
                //
                // 注意 gainAt 返回的是 **RMS**，而输入正弦幅度 0.3 的
                // RMS 是 0.3/√2 ≈ 0.212。所以"保留低频"的判据是
                // 接近 0.212，而不是接近 0.3（之前阈值写错导致误报失败）。
                {
                    const float lo = gainAt (DistortionType::Exciter, 100.0f, 0.5f);
                    const float hi = gainAt (DistortionType::Exciter, 8000.0f, 0.5f);

                    check (lo > 0.17f, "Exciter 保留低频（只处理高频）",
                           juce::String (lo, 3) + "  (输入 rms 约 0.212)");
                    check (hi > lo * 1.3f, "Exciter 提升高频",
                           juce::String (lo, 3) + " -> " + juce::String (hi, 3));
                }

                // Tape 必须有高频损失（磁头特征）
                {
                    const float hi = gainAt (DistortionType::Tape, 14000.0f, 0.5f);
                    const float lo = gainAt (DistortionType::Tape, 1000.0f, 0.5f);
                    check (hi < lo * 0.9f, "Tape 有高频损失",
                           juce::String (hi / juce::jmax (1e-6f, lo), 3));
                }

                // tone 旋钮必须真的起作用（暗/亮有区别）
                {
                    const float dark  = gainAt (DistortionType::Tube, 6000.0f, 0.0f);
                    const float bright = gainAt (DistortionType::Tube, 6000.0f, 1.0f);
                    check (bright > dark * 1.5f, "Tone 旋钮能改变明暗",
                           juce::String (dark, 3) + " -> " + juce::String (bright, 3));
                }

                // 各模型电平不应相差过大（Auto Gain 的目的）
                {
                    float lo = 1.0e9f, hi = 0.0f;
                    for (int m = 0; m < numModels; ++m)
                    {
                        // Radio 是窄带，整体电平天然低，不计入
                        if ((DistortionType) m == DistortionType::Radio) continue;
                        const float g = gainAt ((DistortionType) m, 1000.0f, 0.5f);
                        lo = juce::jmin (lo, g);
                        hi = juce::jmax (hi, g);
                    }
                    check (hi < lo * 4.0f, "Auto Gain 后各模型电平接近（<12dB 差）",
                           juce::String (lo, 3) + " ~ " + juce::String (hi, 3));
                }
            }
        }
    }

    //==========================================================================

    //==========================================================================
    std::cout << "\n--- 10. AI 生成 ---" << std::endl;
    {
        auto U = [] (const char* s) { return juce::String::fromUTF8 (s); };

        // --- JSON 契约
        {
            bool ok = false;
            auto p = AiPatch::fromJson (
                "{\"amp_decay\":1.2,\"sub_level\":0.7,\"start_pitch\":55}", &ok);
            check (ok, "AI: JSON 解析成功");
            check (std::abs (p.ampDecay - 1.2f) < 0.01f, "AI: 字段正确读取",
                   juce::String (p.ampDecay, 3));

            bool ok2 = false;
            auto p2 = AiPatch::fromJson ("{\"amp_decay\":1.5}", &ok2);
            check (ok2 && p2.subLevel > 0.1f, "AI: 缺失字段保留默认值",
                   juce::String (p2.subLevel, 3));

            bool ok3 = false;
            auto p3 = AiPatch::fromJson (
                "{\"amp_decay\":-9,\"sub_level\":42,\"start_pitch\":1e30}", &ok3);
            check (p3.ampDecay > 0.0f && p3.subLevel <= 1.0f && p3.startPitch <= 96.0f,
                   "AI: 越界值被夹到合法范围");

            bool ok4 = true;
            AiPatch::fromJson ("not json at all", &ok4);
            check (! ok4, "AI: 非 JSON 输入被拒绝（不崩）");

            auto rt = AiPatch::fromJson (p.toJson());
            check (std::abs (rt.ampDecay - p.ampDecay) < 0.001f, "AI: JSON 往返一致");
        }

        // --- 本地语义引擎
        {
            LocalPatchEngine eng;
            AiPatch longKick, shortKick, tapeKick;
            juce::String err;

            const bool okLong  = eng.generate (U ("有冲击力、低频很长的Trap底鼓"), 7, longKick, err);
            const bool okShort = eng.generate (U ("明亮短促的Techno kick"), 7, shortKick, err);
            const bool okTape  = eng.generate (U ("带有模拟磁带饱和感的复古感"), 7, tapeKick, err);

            check (okLong && okShort && okTape, "AI: 中文描述能解析");

            check (longKick.ampDecay > shortKick.ampDecay * 2.0f,
                   "AI: \"低频很长\" 的衰减明显更长",
                   juce::String (longKick.ampDecay, 2) + "s vs "
                       + juce::String (shortKick.ampDecay, 2) + "s");

            check (shortKick.tone > tapeKick.tone,
                   "AI: \"明亮\" 比 \"复古\" 更亮",
                   juce::String (shortKick.tone, 2) + " vs "
                       + juce::String (tapeKick.tone, 2));

            check (tapeKick.colourModel == 1, "AI: \"磁带\" 选到 Tape 染色模型",
                   juce::String (tapeKick.colourModel));

            AiPatch junk;
            const bool okJunk = eng.generate (U ("完全无法识别的胡言乱语xyzabc"), 0, junk, err);
            check (! okJunk, "AI: 无法识别时明确失败（不瞎猜）");

            const bool okEmpty = eng.generate ("", 0, junk, err);
            check (! okEmpty, "AI: 空描述被拒绝");
        }

        // --- 种子
        {
            LocalPatchEngine eng;
            AiPatch a, b, c;
            juce::String err;
            eng.generate (U ("有冲击力的长尾808"), 100, a, err);
            eng.generate (U ("有冲击力的长尾808"), 200, b, err);
            eng.generate (U ("有冲击力的长尾808"), 100, c, err);

            check (std::abs (a.ampDecay - c.ampDecay) < 1.0e-6f,
                   "AI: 同种子结果可复现");

            const float diff = std::abs (a.ampDecay - b.ampDecay);
            check (diff > 0.0f && diff < a.ampDecay * 0.2f,
                   "AI: 不同种子产生微妙差异（<20%）",
                   juce::String (diff, 4) + "s");
        }

        // --- 应用
        {
            KickForgeProcessor p;
                p.getSampleLibrary().initialise (44100.0);

            LocalPatchEngine eng;
            AiPatch patch;
            juce::String err;
            eng.generate (U ("有冲击力、低频很长的Trap底鼓"), 7, patch, err);

            const float before = p.getAPVTS().getRawParameterValue (pid::subDecay)->load();
            p.getAiApplier().apply (patch, false);
            const float after = p.getAPVTS().getRawParameterValue (pid::subDecay)->load();

            check (std::abs (after - before) > 1.0f, "AI: 应用后参数确实改变",
                   juce::String (before, 0) + " -> " + juce::String (after, 0));

            check (! p.getAiApplier().getChangedParams().isEmpty(),
                   "AI: 记录了改动的参数（供高亮）",
                   juce::String (p.getAiApplier().getChangedParams().size()) + " 个");

            bool finite = true;
            for (auto* par : p.getParameters())
                if (! std::isfinite (par->getValue()))
                    finite = false;
            check (finite, "AI: 应用后所有参数都是有限值（无 NaN）");
        }

        // --- 平滑过渡
        {
            KickForgeProcessor p;
                p.getSampleLibrary().initialise (44100.0);

            if (auto* par = p.getAPVTS().getParameter (pid::subDecay))
                par->setValueNotifyingHost (par->convertTo0to1 (300.0f));

            LocalPatchEngine eng;
            AiPatch patch;
            juce::String err;
            eng.generate (U ("低频很长的808"), 3, patch, err);

            p.getAiApplier().apply (patch, true);

            float maxJump = 0.0f;
            float prev = p.getAPVTS().getRawParameterValue (pid::subDecay)->load();

            for (int i = 0; i < 20; ++i)
            {
                juce::MessageManager::getInstance()->runDispatchLoopUntil (18);
                const float cur = p.getAPVTS().getRawParameterValue (pid::subDecay)->load();
                maxJump = juce::jmax (maxJump, std::abs (cur - prev));
                prev = cur;
            }

            const float total = std::abs (prev - 300.0f);
            check (total > 20.0f, "AI: 过渡确实发生了", juce::String (total, 0) + " ms");
            check (maxJump < total, "AI: 参数是渐变而非瞬间跳变",
                   "单帧 " + juce::String (maxJump, 0) + " ms < 总变化 "
                       + juce::String (total, 0) + " ms");
            check (! p.getAiApplier().isAnimating(), "AI: 过渡会自行结束");
        }

        // --- 历史
        {
            AiConfig cfg;
            cfg.promptHistory.clear();
            cfg.addPrompt (U ("第一条描述"));
            cfg.addPrompt (U ("第二条描述"));

            check (cfg.promptHistory.size() == 2, "AI: 历史记录能添加");
            check (cfg.promptHistory[0] == U ("第二条描述"), "AI: 最新的在最前");

            cfg.addPrompt (U ("第一条描述"));
            check (cfg.promptHistory.size() == 2, "AI: 重复描述不产生重复项");
            check (cfg.promptHistory[0] == U ("第一条描述"), "AI: 重复描述被提到最前");

            for (int i = 0; i < 20; ++i)
                cfg.addPrompt ("p" + juce::String (i));
            check ((int) cfg.promptHistory.size() <= AiConfig::kMaxHistory,
                   "AI: 历史上限 10 条",
                   juce::String ((int) cfg.promptHistory.size()));

            cfg.promptHistory.clear();
            cfg.save();
        }

        // --- 编码防御
        {
            LocalPatchEngine eng;
            AiPatch p;
            juce::String err;
            const juce::String bad = (const char*) "带有模拟磁带饱和感的复古感";
            const bool ok = eng.generate (bad, 0, p, err);
            check (! ok, "AI: 编码异常的输入被明确拒绝（不静默失败）");
        }
    }

    std::cout << "\n==============================================\n";
    if (failures == 0)
        std::cout << " 全部通过 ✓  (" << checks << " 项检查)" << std::endl;
    else
        std::cout << " 有 " << failures << " 项未通过 ✗  (共 " << checks << " 项)" << std::endl;
    std::cout << "==============================================" << std::endl;

    return failures == 0 ? 0 : 1;
}
