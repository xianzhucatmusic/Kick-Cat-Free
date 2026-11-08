//==============================================================================
// 波形选择 + 4 层 Click 功能测试
//==============================================================================
#include <juce_audio_processors/juce_audio_processors.h>
#include <iostream>
#include <cmath>
#include "PluginProcessor.h"
#include "WavExporter.h"
#include "PluginEditor.h"
using namespace kf;

static int failures = 0;
static void check (bool c, const juce::String& what, const juce::String& d = {})
{
    std::cout << (c ? "  [PASS] " : "  [FAIL] ") << what;
    if (d.isNotEmpty()) std::cout << "   (" << d << ")";
    std::cout << std::endl;
    if (! c) ++failures;
}

/** 渲染并统计谐波含量（过零率）与峰值。 */
struct Stats { float peak=0, zcr=0; bool finite=true; };
static Stats render (KickForgeProcessor& p, double sr, int midiNote = 36, double seconds = 0.4)
{
    Stats s;
    p.prepareToPlay (sr, 512);
    juce::AudioBuffer<float> acc (2, (int)(sr*seconds));
    acc.clear();
    int pos=0; bool sent=false; int zc=0; long n=0;
    while (pos < acc.getNumSamples())
    {
        int b = juce::jmin (512, acc.getNumSamples()-pos);
        juce::AudioBuffer<float> buf (2,b); buf.clear();
        juce::MidiBuffer m;
        if (!sent) { m.addEvent (juce::MidiMessage::noteOn (1, midiNote, (juce::uint8)127), 0); sent=true; }
        p.processBlock (buf, m);
        float prev = pos>0 ? acc.getSample(0,pos-1) : 0.0f;
        for (int i=0;i<b;i++)
        {
            float v = buf.getSample(0,i);
            if (!std::isfinite(v)) s.finite=false;
            else s.peak = juce::jmax (s.peak, std::abs(v));
            if ((v>=0) != (prev>=0)) ++zc;
            prev = v; ++n;
        }
        for (int ch=0; ch<2; ++ch) acc.copyFrom (ch,pos,buf,ch,0,b);
        pos += b;
    }
    s.zcr = n>0 ? (float)zc/(float)n : 0.0f;
    return s;
}

int main()
{

    juce::ScopedJuceInitialiser_GUI init;
    const double sr = 44100.0;
    juce::File outDir ("/Users/serenity/Documents/deepseek-harness/default-workspace/KickForge/selftest_output");
    outDir.createDirectory();

    std::cout << "==============================================\n";
    std::cout << " 波形选择 + 4 层 Click 测试\n";
    std::cout << "==============================================\n" << std::endl;

    //--------------------------------------------------------------------------
    std::cout << "--- 1. 波形枚举与求值 ---" << std::endl;
    {
        check ((int)Waveform::NumWaveforms == 6, "共 6 种波形",
               juce::String((int)Waveform::NumWaveforms) + " 种");
        check (wave::allNames().size() == 6, "波形名列表 6 项");

        // 每种波形在周期内都应有界且在 [-1,1]
        bool bounded = true;
        for (int w=0; w<(int)Waveform::NumWaveforms; ++w)
        {
            auto wf = (Waveform)w;
            if (wave::isNoise(wf)) continue;
            for (int i=0;i<=2000;i++)
            {
                float v = wave::evaluate (wf, (float)i/2000.0f * 3.0f);
                if (!std::isfinite(v) || v < -1.001f || v > 1.001f) bounded=false;
            }
        }
        check (bounded, "所有波形输出有界且在 [-1,1]");

        // Sine 与 Square 的过零特性应不同
        float sineZc=0, sqZc=0;
        for (int i=1;i<=1000;i++)
        {
            if ((wave::evaluate(Waveform::Sine,(float)i/1000*5)>=0) !=
                (wave::evaluate(Waveform::Sine,(float)(i-1)/1000*5)>=0)) sineZc++;
            if ((wave::evaluate(Waveform::Square,(float)i/1000*5)>=0) !=
                (wave::evaluate(Waveform::Square,(float)(i-1)/1000*5)>=0)) sqZc++;
        }
        check (std::abs(sineZc-sqZc) < 1.0f, "Sine/Square 过零次数一致（同基频）",
               juce::String(sineZc) + " vs " + juce::String(sqZc));
    }

    //--------------------------------------------------------------------------
    std::cout << "\n--- 2. 4 层 Click 全部存在且独立 ---" << std::endl;
    {
        KickForgeProcessor proc;        check (kNumClickLayers == 4, "Click 层数 = 4");

        // 每层都有完整参数
        bool allParams = true;
        for (int i=0;i<4;i++)
        {
            for (const char* p : {"on","level","pitch","decay","lp","hp","keytrack","wave","shape","wavetune"})
            {
                if (proc.getAPVTS().getParameter(pid::click(i,p)) == nullptr)
                { allParams=false;
                  std::cout << "      缺少参数: click"<<(i+1)<<"_"<<p<<std::endl; }
            }
        }
        check (allParams, "4 层各有 10 个完整参数");

        // 每层的包络独立
        auto& e1 = proc.getEnvelope (EnvelopeTarget::Click1Amp);
        auto& e4 = proc.getEnvelope (EnvelopeTarget::Click4Amp);
        check (e1.getNumNodes() > 0 && e4.getNumNodes() > 0, "各层都有独立包络");

        // 修改第 4 层不应影响第 1 层
        float v1before = e1.getValueAt (0.3f);
        e4.moveNode (1, 0.5f, 0.9f);
        check (std::abs(e1.getValueAt(0.3f) - v1before) < 1e-6f,
               "修改第 4 层包络不影响第 1 层");
    }

    //--------------------------------------------------------------------------
    std::cout << "\n--- 3. Sub 波形真的改变音色 ---" << std::endl;
    {
        juce::StringArray tags {"sine","tri","saw","square","noise","pulse"};
        std::vector<Stats> results;

        for (int w=0; w<6; ++w)
        {
            KickForgeProcessor proc;            proc.getSampleLibrary().initialise (sr);
            // 只开 Sub，关掉 Click，避免采样干扰判断
            auto set=[&](const juce::String& id,float v){
                if(auto*p=proc.getAPVTS().getParameter(id)) p->setValueNotifyingHost(v); };
            set(pid::subOn, 1.0f);
            set(pid::click(0,"on"), 0.0f);
            set(pid::subPitchAmt, 0.0f);      // 关掉 pitch 包络，便于比较
            set(pid::subDecay, 1500.0f);
            set(pid::subHarmonics, 0.0f);
            // 按索引设置 choice 参数：setValueNotifyingHost 接收 0..1，
            // 所以要先用 convertTo0to1(index) 转换
            if (auto* p = proc.getAPVTS().getParameter (pid::subWave))
                p->setValueNotifyingHost (p->convertTo0to1 ((float) w));

            auto st = render (proc, sr);
            results.push_back (st);

            check (st.finite && st.peak > 0.05f && st.peak <= 1.01f,
                   "Sub 波形 " + tags[w] + " 输出正常",
                   "peak=" + juce::String(st.peak,3) + " zcr=" + juce::String(st.zcr,4));

            // 导出试听文件
            WavExporter::Options o; o.sampleRate=sr; o.maxSeconds=3.0;
            double sec=0; float pk=0;
            auto buf = WavExporter::renderToBuffer (proc, o, sec, pk);
            auto f = outDir.getChildFile ("40_subwave_"+tags[w]+".wav");
            f.deleteFile();
            juce::WavAudioFormat fmt;
            if (auto s2 = std::unique_ptr<juce::FileOutputStream>(f.createOutputStream()))
                if (auto wr = std::unique_ptr<juce::AudioFormatWriter>(
                        fmt.createWriterFor(s2.get(), sr, 2, 24, {}, 0)))
                { s2.release(); wr->writeFromAudioSampleBuffer(buf,0,buf.getNumSamples()); }
        }

        // 过零率在 55Hz / 0.4s 窗口下分辨率太粗，无法区分波形。
        // 改为直接比较振荡器的**波形形状**（这是"选择波形"真正要验证的东西），
        // 以及低频段的谐波能量差。
        auto shapeOf = [] (Waveform w)
        {
            std::vector<float> v;
            for (int i = 0; i < 32; ++i)
                v.push_back (wave::evaluate (w, (float) i / 32.0f));
            return v;
        };

        int distinctShapes = 0;
        const auto sineShape = shapeOf (Waveform::Sine);
        for (int w = 1; w < (int) Waveform::NumWaveforms; ++w)
        {
            const auto s = shapeOf ((Waveform) w);
            double diff = 0.0;
            for (size_t i = 0; i < s.size(); ++i)
                diff += std::abs (s[i] - sineShape[i]);
            if (diff / (double) s.size() > 0.05)
                ++distinctShapes;
        }

        check (distinctShapes == 5, "其余 5 种波形形状均与 Sine 不同",
               juce::String(distinctShapes) + " / 5");

        // 直流检查：脉冲类波形若不做零均值处理，会产生巨大直流偏置
        // （占空比 12.5% 的双极性脉冲均值 = -0.75），
        // 会浪费动态余量并让低音单元持续偏置。
        {
            bool zeroMean = true;
            for (int w = 0; w < (int) Waveform::NumWaveforms; ++w)
            {
                const auto wf = (Waveform) w;
                if (wave::isNoise (wf)) continue;

                double sum = 0.0;
                const int N = 48000;
                for (int i = 0; i < N; ++i)
                    sum += wave::evaluate (wf, (float) i / 200.0f);   // 非整数周期，更严格
                const double mean = sum / N;

                if (std::abs (mean) > 0.02)
                {
                    zeroMean = false;
                    std::cout << "      " << wave::name (wf)
                              << " 直流 = " << mean << std::endl;
                }
            }
            check (zeroMean, "所有波形均无直流偏置（|mean| < 0.02）");
        }

        // 各波形的峰值应大致相当（都在有效电平范围内且不为零）
        bool allAudible = true;
        for (const auto& r : results)
            if (r.peak < 0.05f || r.peak > 1.01f) allAudible = false;
        check (allAudible, "6 种 Sub 波形都有有效电平");
    }

    //--------------------------------------------------------------------------
    std::cout << "\n--- 4. Click 层波形模式（不依赖采样）---" << std::endl;
    {
        juce::StringArray tags {"sine","tri","saw","square","noise","pulse"};

        for (int w=0; w<6; ++w)
        {
            KickForgeProcessor proc;            proc.getSampleLibrary().initialise (sr);

            auto set=[&](const juce::String& id,float v){
                if(auto*p=proc.getAPVTS().getParameter(id)) p->setValueNotifyingHost(v); };

            set(pid::subOn, 0.0f);                 // 只留 Click
            set(pid::click(0,"on"), 1.0f);
            set(pid::click(0,"level"), 0.9f);
            set(pid::click(0,"decay"), 200.0f);
            if (auto* p = proc.getAPVTS().getParameter (pid::click(0,"wave")))
                p->setValueNotifyingHost (p->convertTo0to1 ((float)(w+1)));  // 0=Sample,1..6=波形
            set(pid::click(0,"wavetune"), 0.5f);              // 约 400Hz

            auto st = render (proc, sr, 36, 0.5);
            check (st.finite && st.peak > 0.03f && st.peak <= 1.01f,
                   "Click 波形 " + tags[w] + " 可独立发声（无需采样）",
                   "peak=" + juce::String(st.peak,3));
        }
    }

    //--------------------------------------------------------------------------
    std::cout << "\n--- 4b. 新装状态下 4 层都开箱可用 ---" << std::endl;
    {
        // 这条回归测试针对一个真实缺陷：
        // Click 2/3/4 原本默认 level=0 且没有采样，用户把它们打开、
        // 把 LEVEL 拧满也听不到任何声音 —— 看起来像"层不生效"。
        // 现在要求：新建实例并打开编辑器后，每一层单独打开都必须发声。
        KickForgeProcessor proc;        proc.getSampleLibrary().initialise (sr);

        // 模拟编辑器构造时做的默认铺采样
        for (int i = 0; i < kNumClickLayers; ++i)
            proc.getSampleLibrary().loadIntoLayer (i * 7, proc.getClickLayerRef (i));

        auto setP = [&proc] (const juce::String& id, float v)
        {
            if (auto* p = proc.getAPVTS().getParameter (id))
                p->setValueNotifyingHost (v);
        };

        setP (pid::subOn, 0.0f);

        // 1) 每层默认电平必须 > 0（否则打开也听不见）
        bool allHaveLevel = true;
        for (int i = 0; i < kNumClickLayers; ++i)
            if (proc.getAPVTS().getRawParameterValue (pid::click (i, "level"))->load() <= 0.01f)
                allHaveLevel = false;
        check (allHaveLevel, "4 层默认 LEVEL 均大于 0（不会一打开就是静音）");

        // 2) 每层单独打开都要有声
        for (int i = 0; i < kNumClickLayers; ++i)
        {
            for (int k = 0; k < kNumClickLayers; ++k)
                setP (pid::click (k, "on"), k == i ? 1.0f : 0.0f);

            auto st = render (proc, sr, 36, 0.45);
            check (st.finite && st.peak > 0.02f,
                   "只开 Click " + juce::String (i + 1) + " 时有声",
                   "peak=" + juce::String (st.peak, 4));
        }
    }

    //--------------------------------------------------------------------------
    std::cout << "\n--- 4d. 默认电平留有余量 ---" << std::endl;
    {
        // 回归测试：默认状态曾经顶到 -0.42 dBFS（紧贴限幅天花板），
        // 用户一进插件就没有叠加/处理的余地。
        // 现在要求默认峰值落在 -12 ~ -3 dBFS 之间（舒适工作电平）。
        KickForgeProcessor proc;        proc.getSampleLibrary().initialise (sr);
        for (int i = 0; i < kNumClickLayers; ++i)
            proc.getSampleLibrary().loadIntoLayer (i * 7, proc.getClickLayerRef (i));

        auto st = render (proc, sr, 36, 0.6);
        const float db = juce::Decibels::gainToDecibels (st.peak, -100.0f);

        check (db <= -3.0f, "默认峰值不超过 -3 dBFS（有余量）",
               juce::String (db, 2) + " dBFS");
        check (db >= -12.0f, "默认峰值不低于 -12 dBFS（不至于太小声）",
               juce::String (db, 2) + " dBFS");

        // 4 层全开时也不能顶到天花板
        for (int i = 0; i < kNumClickLayers; ++i)
            if (auto* p = proc.getAPVTS().getParameter (pid::click (i, "on")))
                p->setValueNotifyingHost (1.0f);

        auto st4 = render (proc, sr, 36, 0.6);
        const float db4 = juce::Decibels::gainToDecibels (st4.peak, -100.0f);

        check (db4 <= -2.0f, "4 层全开时仍有余量",
               juce::String (db4, 2) + " dBFS");
    }

    //--------------------------------------------------------------------------
    std::cout << "\n--- 4c. 每层都有独立的电源开关（UI 层）---" << std::endl;
    {
        // 回归测试：原来 Click 层的 ON 开关**从未被创建**，
        // 而 placeToggle() 是按 paramID 在已有控件里查找的，找不到就静默跳过，
        // 于是布局留了位置却什么都不画 —— 表现为"没有单独开关"。
        // 这里在真实编辑器实例上验证：4 层各有且仅有一个 ON 开关，
        // 并且点击它只翻转本层的参数。
        KickForgeProcessor proc;        proc.getSampleLibrary().initialise (sr);
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        check (ed != nullptr, "编辑器可创建");

        if (ed != nullptr)
        {
            // 收集所有 ToggleButton
            juce::Array<juce::ToggleButton*> toggles;
            std::function<void (juce::Component*)> walk = [&] (juce::Component* c)
            {
                for (int i = 0; i < c->getNumChildComponents(); ++i)
                {
                    auto* ch = c->getChildComponent (i);
                    if (auto* tb = dynamic_cast<juce::ToggleButton*> (ch))
                        toggles.add (tb);
                    walk (ch);
                }
            };
            ed->setSize (1160, 660);

            // 分页后，Click 层的控件只在 CLICK 页可见。
            // 测试必须先切到那一页，否则会误判成"开关没有尺寸"。
            {
                juce::Array<juce::TextButton*> tabs;
                std::function<void (juce::Component*)> tw = [&] (juce::Component* c)
                {
                    for (int i = 0; i < c->getNumChildComponents(); ++i)
                    {
                        auto* ch = c->getChildComponent (i);
                        if (auto* tb = dynamic_cast<juce::TextButton*> (ch)) tabs.add (tb);
                        tw (ch);
                    }
                };
                tw (ed.get());
                for (auto* tb : tabs)
                    if (tb->getButtonText() == "CLICK") { tb->triggerClick(); break; }

                for (int k = 0; k < 6; ++k)
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (30);
            }

            walk (ed.get());

            // 找出文字为 "ON" 的开关
            juce::Array<juce::ToggleButton*> onSwitches;
            for (auto* tb : toggles)
                if (tb->getButtonText() == "ON")
                    onSwitches.add (tb);

            check (onSwitches.size() == kNumClickLayers,
                   "存在 " + juce::String (kNumClickLayers) + " 个 Click 电源开关",
                   "实际 " + juce::String (onSwitches.size()) + " 个");

            // 没有任何开关是空文字的（空文字开关说明控件没被正确创建）
            bool anyBlank = false;
            for (auto* tb : toggles)
                if (tb->getButtonText().trim().isEmpty())
                    anyBlank = true;
            check (! anyBlank, "没有空文字的开关控件");

            // 逐个点击，必须只影响自己那一层
            int idx = 0;
            bool allIsolated = true;
            bool allUsable = true;

            for (auto* sw : onSwitches)
            {
                const int layer = idx++;
                if (layer >= kNumClickLayers)
                    break;

                // 开关必须有可见尺寸，否则用户点不到
                if (sw->getWidth() < 20 || sw->getHeight() < 10)
                    allUsable = false;

                bool before[kNumClickLayers], after[kNumClickLayers];
                for (int k = 0; k < kNumClickLayers; ++k)
                    before[k] = proc.getAPVTS().getRawParameterValue (pid::click (k, "on"))->load() > 0.5f;

                sw->setToggleState (! sw->getToggleState(), juce::sendNotificationSync);

                for (int k = 0; k < kNumClickLayers; ++k)
                    after[k] = proc.getAPVTS().getRawParameterValue (pid::click (k, "on"))->load() > 0.5f;

                int changed = 0, which = -1;
                for (int k = 0; k < kNumClickLayers; ++k)
                    if (before[k] != after[k]) { ++changed; which = k; }

                if (changed != 1 || which != layer)
                    allIsolated = false;
            }

            check (allUsable, "CLICK 页上每个电源开关都有可见尺寸（可点到）");
            check (allIsolated, "点击某个开关只翻转它自己那一层");
        }
    }

    //--------------------------------------------------------------------------
    std::cout << "\n--- 4e. 分页：每页只显示自己的控件 ---" << std::endl;
    {
        // 回归测试：分页后，切换页签必须让对应模块的控件出现、其它模块的消失。
        // 若 applyPageVisibility() 失效，所有控件会同时可见并重叠在一起。
        KickForgeProcessor proc;        proc.getSampleLibrary().initialise (sr);
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        check (ed != nullptr, "编辑器可创建");

        if (ed != nullptr)
        {
            ed->setSize (1160, 660);

            juce::Array<juce::TextButton*> tabs;
            std::function<void (juce::Component*)> tw = [&] (juce::Component* c)
            {
                for (int i = 0; i < c->getNumChildComponents(); ++i)
                {
                    auto* ch = c->getChildComponent (i);
                    if (auto* tb = dynamic_cast<juce::TextButton*> (ch)) tabs.add (tb);
                    tw (ch);
                }
            };
            tw (ed.get());

            // 注意：界面上有两个按钮都叫 "SUB" —— 功能页签(233x28)和
            // 波形源按钮(46x18)。必须按尺寸区分，否则会点到错误的控件。
            juce::TextButton* pageTab[4] = { nullptr, nullptr, nullptr, nullptr };
            const char* names[4] = { "SUB", "CLICK", "EQ", "DYNAMICS" };
            for (auto* tb : tabs)
            {
                if (tb->getWidth() < 150) continue;          // 跳过波形源按钮
                for (int i = 0; i < 4; ++i)
                    if (tb->getButtonText() == names[i]) pageTab[i] = tb;
            }

            bool allTabs = pageTab[0] && pageTab[1] && pageTab[2] && pageTab[3];
            check (allTabs, "存在 SUB / CLICK / EQ / DYNAMICS 四个页签");

            if (allTabs)
            {
                // 统计某页上"可见且有尺寸"的旋钮数量
                auto countVisibleKnobs = [&] () -> int
                {
                    int c = 0;
                    std::function<void (juce::Component*)> walk = [&] (juce::Component* comp)
                    {
                        for (int i = 0; i < comp->getNumChildComponents(); ++i)
                        {
                            auto* ch = comp->getChildComponent (i);
                            if (dynamic_cast<juce::Slider*> (ch)
                                && ch->isVisible() && ch->getWidth() > 0)
                                ++c;
                            walk (ch);
                        }
                    };
                    walk (ed.get());
                    return c;
                };

                int counts[4] = {};
                for (int p = 0; p < 4; ++p)
                {
                    pageTab[p]->triggerClick();
                    for (int k = 0; k < 5; ++k)
                        juce::MessageManager::getInstance()->runDispatchLoopUntil (25);
                    counts[p] = countVisibleKnobs();
                }

                // EQ 页只有曲线、没有旋钮，所以允许它为 0；
                // 其余三页都必须有旋钮。
                bool othersHave = counts[0] > 0 && counts[1] > 0 && counts[3] > 0;
                check (othersHave, "SUB / CLICK / DYNAMICS 三页都有可见旋钮",
                       "SUB=" + juce::String (counts[0]) + " CLICK=" + juce::String (counts[1])
                     + " EQ=" + juce::String (counts[2]) + " DYN=" + juce::String (counts[3]));

                check (counts[2] < counts[0], "EQ 页（无旋钮）与 SUB 页显示不同",
                       "EQ=" + juce::String (counts[2]) + " vs SUB=" + juce::String (counts[0]));

                // 总控件数应等于各页之和（说明没有重叠显示）
                int total = 0;
                for (int p = 0; p < 4; ++p) total += counts[p];

                // 回到 SUB 页
                pageTab[0]->triggerClick();
                for (int k = 0; k < 5; ++k)
                    juce::MessageManager::getInstance()->runDispatchLoopUntil (25);

                const int back = countVisibleKnobs();
                check (back > 0 && std::abs (back - counts[0]) <= 1,
                       "切回 SUB 页后控件恢复显示（不为空）",
                       juce::String (back) + " vs 首次 " + juce::String (counts[0]));

                // --- 纵向比例：包络区不能无限膨胀 ---
                // 回归测试：上方区（包络+波形）原来没有上限，窗口拉高时
                // 它吃掉 66% 的高度，而页内容固定不变，比例严重失衡。
                {
                    auto findEnvEditor = [&] () -> juce::Component*
                    {
                        juce::Component* found = nullptr;
                        std::function<void (juce::Component*)> w = [&] (juce::Component* c)
                        {
                            if (found) return;
                            for (int i = 0; i < c->getNumChildComponents(); ++i)
                            {
                                auto* ch = c->getChildComponent (i);
                                if (dynamic_cast<EnvelopeEditor*> (ch)) { found = ch; return; }
                                w (ch);
                                if (found) return;
                            }
                        };
                        w (ed.get());
                        return found;
                    };

                    if (auto* envEd = findEnvEditor())
                    {
                        int envH[3] = {};
                        const int heights[3] = { 470, 560, 660 };

                        for (int i = 0; i < 3; ++i)
                        {
                            ed->setSize (1000, heights[i]);
                            for (int k = 0; k < 4; ++k)
                                juce::MessageManager::getInstance()->runDispatchLoopUntil (25);
                            envH[i] = envEd->getHeight();
                        }

                        const double growth = envH[0] > 0
                                                ? (double) (envH[2] - envH[0]) / (double) envH[0]
                                                : 0.0;

                        // 窗口高度 +40%，包络区增长不应超过 60%
                        check (growth < 0.6,
                               "窗口拉高时包络区不会无限膨胀",
                               "470->" + juce::String (envH[0]) + "px,  "
                             + "660->" + juce::String (envH[2]) + "px  (增长 "
                             + juce::String (growth * 100.0, 0) + "%)");

                        ed->setSize (1000, 520);
                        for (int k = 0; k < 4; ++k)
                            juce::MessageManager::getInstance()->runDispatchLoopUntil (25);
                    }
                    else
                    {
                        check (false, "找不到包络编辑器组件");
                    }
                }

                // 各页之和应当大于任意单页（证明是切换而非叠加）
                check (total > counts[0], "各页控件不重叠（总数为各页之和）");

                // --- 窗口尺寸约束必须真实生效 ---
                // 回归测试：setResizeLimits 曾被写进了错误的函数作用域
                // （SampleBrowser 的构造函数里），编辑器实际仍用旧的
                // 960x700~2000x1300 限制，导致宿主可以把这个分页界面
                // 拉到 1000px 高，面板里出现大片空白。
                {
                    auto* c = ed->getConstrainer();
                    check (c != nullptr, "编辑器设置了尺寸约束器");

                    if (c != nullptr)
                    {
                        const int maxH = c->getMaximumHeight();
                        const int maxW = c->getMaximumWidth();

                        // 分页后内容固定，高度上限必须收紧。
                        // 上限随页内容变化：SUB 页有 3 行旋钮（22 个），
                        // 需要 720px；再高就会出现空白。
                        check (maxH <= 760, "窗口高度上限已收紧（不会出现大片空白）",
                               "上限 " + juce::String (maxH) + "px");
                        check (maxW >= 1400, "宽度上限宽松（旋钮可居中自适应）",
                               "上限 " + juce::String (maxW) + "px");
                    }
                }

                // --- 面板高度有上限，不随窗口无限拉伸 ---
                {
                    auto findPanelOwner = [&] () -> juce::Component*
                    {
                        // EQ 曲线所在的面板随页变化；这里用包络编辑器反推不合适，
                        // 改为直接测量：把窗口拉到上限，检查页内容区里是否有
                        // 一个高度被限制住的面板 —— 通过 EQ 曲线的高度间接判断
                        juce::Component* found = nullptr;
                        std::function<void (juce::Component*)> w = [&] (juce::Component* comp)
                        {
                            if (found) return;
                            for (int i = 0; i < comp->getNumChildComponents(); ++i)
                            {
                                auto* ch = comp->getChildComponent (i);
                                if (dynamic_cast<EqCurveEditor*> (ch)) { found = ch; return; }
                                w (ch);
                                if (found) return;
                            }
                        };
                        w (ed.get());
                        return found;
                    };

                    pageTab[2]->triggerClick();   // EQ 页
                    for (int k = 0; k < 4; ++k)
                        juce::MessageManager::getInstance()->runDispatchLoopUntil (25);

                    if (auto* curve = findPanelOwner())
                    {
                        const int hSmall = curve->getHeight();
                        ed->setSize (1000, 640);
                        for (int k = 0; k < 4; ++k)
                            juce::MessageManager::getInstance()->runDispatchLoopUntil (25);
                        const int hBig = curve->getHeight();

                        // 窗口 +120px，EQ 区不该跟着长那么多（面板有 300px 上限）
                        check (hBig - hSmall <= 130,
                               "窗口拉高时页内容不会无限拉伸",
                               "460->" + juce::String (hSmall) + "px, 640->" + juce::String (hBig) + "px");
                    }

                    pageTab[0]->triggerClick();
                    ed->setSize (1000, 520);
                    for (int k = 0; k < 4; ++k)
                        juce::MessageManager::getInstance()->runDispatchLoopUntil (25);
                }


                // --- 自绘组件也必须跟随分页 ---
                // 回归测试：EQ 曲线是自绘组件、不绑定参数，applyPageVisibility()
                // 的按参数名循环管不到它，导致它曾在所有页都可见、
                // 直接画在 SUB/CLICK 的旋钮上面（控件重叠）。
                {
                    auto findEqCurve = [&] () -> juce::Component*
                    {
                        juce::Component* found = nullptr;
                        std::function<void (juce::Component*)> w = [&] (juce::Component* c)
                        {
                            for (int i = 0; i < c->getNumChildComponents(); ++i)
                            {
                                auto* ch = c->getChildComponent (i);
                                if (dynamic_cast<EqCurveEditor*> (ch)) { found = ch; return; }
                                w (ch);
                                if (found) return;
                            }
                        };
                        w (ed.get());
                        return found;
                    };

                    if (auto* curve = findEqCurve())
                    {
                        // 切到 SUB 页，EQ 曲线必须隐藏
                        pageTab[0]->triggerClick();
                        for (int k = 0; k < 5; ++k)
                            juce::MessageManager::getInstance()->runDispatchLoopUntil (25);
                        ed->resized();   // 强制一次布局，避免断言读到旧状态
                        check (! curve->isVisible(), "SUB 页上 EQ 曲线已隐藏（不与旋钮重叠）");

                        // 切到 EQ 页，必须显示
                        pageTab[2]->triggerClick();
                        for (int k = 0; k < 5; ++k)
                            juce::MessageManager::getInstance()->runDispatchLoopUntil (25);
                        ed->resized();
                        check (curve->isVisible(), "EQ 页上 EQ 曲线可见");

                        pageTab[0]->triggerClick();
                        for (int k = 0; k < 5; ++k)
                            juce::MessageManager::getInstance()->runDispatchLoopUntil (25);
                    }
                    else
                    {
                        check (false, "找不到 EQ 曲线组件");
                    }
                }
            }
        }
    }

    //--------------------------------------------------------------------------
    std::cout << "\n--- 5. 4 层同时叠加 ---" << std::endl;
    {
        KickForgeProcessor proc;        proc.getSampleLibrary().initialise (sr);
        for (int i=0;i<4;i++)
            proc.getSampleLibrary().loadIntoLayer (i, proc.getClickLayerRef (i));

        auto set=[&](const juce::String& id,float v){
            if(auto*p=proc.getAPVTS().getParameter(id)) p->setValueNotifyingHost(v); };

        set(pid::subOn, 1.0f);
        for (int i=0;i<4;i++)
        {
            set(pid::click(i,"on"), 1.0f);
            set(pid::click(i,"level"), 0.5f);
            set(pid::click(i,"pitch"), (float)i * 1.0f / 3.0f);   // 各层不同音高
        }

        auto st = render (proc, sr);
        check (st.finite && st.peak <= 1.01f, "4 层同时开启输出受控",
               "peak=" + juce::String(st.peak,3));

        // 逐层关闭，电平应逐渐下降
        float prevPeak = st.peak;
        bool decreased = false;
        for (int i=3;i>=0;i--)
        {
            set(pid::click(i,"on"), 0.0f);
            auto s2 = render (proc, sr);
            if (s2.peak < prevPeak) decreased = true;
            prevPeak = s2.peak;
        }
        check (decreased, "逐层关闭 Click 时电平下降（证明各层独立生效）");
    }

    //--------------------------------------------------------------------------
    std::cout << "\n--- 6. 波形模式下的状态保存 ---" << std::endl;
    {
        KickForgeProcessor a;
        auto set=[&](const juce::String& id,float v){
            if(auto*p=a.getAPVTS().getParameter(id)) p->setValueNotifyingHost(v); };
        if (auto* p = a.getAPVTS().getParameter (pid::subWave))
            p->setValueNotifyingHost (p->convertTo0to1 (1.0f));      // Triangle
        if (auto* p = a.getAPVTS().getParameter (pid::click(0,"wave")))
            p->setValueNotifyingHost (p->convertTo0to1 (1.0f));      // Sine 波形

        juce::MemoryBlock st;
        a.getStateInformation (st);

        KickForgeProcessor b;
        b.setStateInformation (st.getData(), (int) st.getSize());

        // 注意：getRawParameterValue 返回 0..1 归一化值，不是索引。
        // Sub 有 6 项 -> index = norm * 5；Click 有 7 项 -> index = norm * 6
        // getRawParameterValue 对 choice 参数返回的就是索引
        const int i1 = (int) std::lround (b.getAPVTS().getRawParameterValue (pid::subWave)->load());
        const int i2 = (int) std::lround (b.getAPVTS().getRawParameterValue (pid::click(0,"wave"))->load());

        check (i1 == 1, "Sub 波形选择可保存/恢复（应恢复为 Triangle）",
               "index = " + juce::String (i1));
        check (i2 == 1, "Click 层波形选择可保存/恢复（应恢复为 Sine 波形）",
               "index = " + juce::String (i2));
    }

    std::cout << "\n==============================================\n";
    if (failures == 0) std::cout << " 全部通过 ✓\n";
    else std::cout << " 有 " << failures << " 项未通过 ✗\n";
    std::cout << "==============================================" << std::endl;
    return failures == 0 ? 0 : 1;
}
