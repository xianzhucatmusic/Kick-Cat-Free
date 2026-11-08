//==============================================================================
// UI 截图工具
//
// 创建编辑器，渲染到图像并保存 PNG。
// 用来验证界面真实可绘制（而不是只编译通过）。
//==============================================================================

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <iostream>

#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace kf;

int main (int argc, char** argv)
{

    juce::ScopedJuceInitialiser_GUI juceInit;

    juce::File outDir ("/Users/serenity/Documents/deepseek-harness/default-workspace/KickForge/selftest_output");
    outDir.createDirectory();

    const int width  = (argc > 1) ? juce::String (argv[1]).getIntValue() : 1180;
    const int height = (argc > 2) ? juce::String (argv[2]).getIntValue() : 760;

    std::cout << "渲染编辑器 " << width << "x" << height << " ..." << std::endl;

    KickForgeProcessor proc;
    proc.prepareToPlay (44100.0, 512);
    proc.getSampleLibrary().initialise (44100.0);
    proc.getSampleLibrary().loadIntoLayer (0, proc.getClickLayerRef (0));

    // 若给了第三个参数 "eqdemo"，设置一条有起伏的 EQ 曲线，
    // 这样截图能验证曲线的绘制效果（默认全 0dB 看不出问题）。
    if (argc > 3 && juce::String (argv[3]) == "slopedemo")
    {
        auto setP = [&proc] (const juce::String& id, float v)
        {
            if (auto* p = proc.getAPVTS().getParameter (id))
                p->setValueNotifyingHost (v);
        };
        auto setIdx = [&proc] (const juce::String& id, float idx)
        {
            if (auto* p = proc.getAPVTS().getParameter (id))
                p->setValueNotifyingHost (p->convertTo0to1 (idx));
        };

        auto setHz = [&proc] (const juce::String& id, float hz)
        {
            if (auto* p = proc.getAPVTS().getParameter (id))
                p->setValueNotifyingHost (p->convertTo0to1 (hz));
        };

        // Band 1 = 高通 40Hz, 48dB/oct
        setP ("eq1_on", 1.0f);
        setHz ("eq1_freq", 40.0f);
        setIdx ("eq1_slope", 3.0f);

        // Band 8 = 低通 12kHz, 48dB/oct
        setP ("eq8_on", 1.0f);
        setHz ("eq8_freq", 12000.0f);
        setIdx ("eq8_slope", 3.0f);

        // 中间来点音乐性调整
        setP ("eq3_on", 1.0f); setHz ("eq3_freq", 70.0f);  setP ("eq3_gain", 5.0f);
        setP ("eq5_on", 1.0f); setHz ("eq5_freq", 300.0f); setP ("eq5_gain", -4.0f);
    }

    if (argc > 3 && juce::String (argv[3]) == "eqdemo")
    {
        auto setP = [&proc] (const juce::String& id, float v)
        {
            if (auto* p = proc.getAPVTS().getParameter (id))
                p->setValueNotifyingHost (p->convertTo0to1 (v));
        };

        setP ("eq1_on", 1.0f);  setP ("eq1_freq", 32.0f);   setP ("eq1_gain", -6.0f); setP ("eq1_q", 0.7f);
        setP ("eq2_on", 1.0f);  setP ("eq2_freq", 70.0f);   setP ("eq2_gain", 6.5f);  setP ("eq2_q", 1.0f);
        setP ("eq3_on", 1.0f);  setP ("eq3_freq", 250.0f);  setP ("eq3_gain", -4.5f); setP ("eq3_q", 1.2f);
        setP ("eq4_on", 1.0f);  setP ("eq4_freq", 900.0f);  setP ("eq4_gain", 2.5f);  setP ("eq4_q", 1.0f);
        setP ("eq5_on", 1.0f);  setP ("eq5_freq", 2800.0f); setP ("eq5_gain", -3.0f); setP ("eq5_q", 1.4f);
        setP ("eq6_on", 1.0f);  setP ("eq6_freq", 6500.0f); setP ("eq6_gain", 3.5f);  setP ("eq6_q", 0.9f);
        setP ("eq7_on", 1.0f);  setP ("eq7_freq", 12000.0f);setP ("eq7_gain", 2.0f);  setP ("eq7_q", 0.9f);
    }

    std::unique_ptr<juce::AudioProcessorEditor> editor (proc.createEditor());
    if (editor == nullptr)
    {
        std::cout << "  [FAIL] createEditor() 返回空" << std::endl;
        return 1;
    }

    editor->setSize (width, height);
    editor->setVisible (true);

    // 可选第 4 个参数：切换到指定功能页（sub/click/eq/dyn）
    juce::String pageArg = (argc > 3) ? juce::String (argv[3]) : juce::String();
    juce::ignoreUnused (pageArg);

    // 先触发一次试听并把音频渲染出来，让波形捕获有数据。
    proc.triggerAudition (1.0f);
    {
        juce::AudioBuffer<float> b (2, 512);
        juce::MidiBuffer m;
        for (int i = 0; i < 40; ++i)
        {
            b.clear();
            m.clear();
            proc.processBlock (b, m);
        }
    }

    // 驱动消息循环，让 Timer 回调（波形刷新、采样名更新）执行。
    // runDispatchLoopUntil 需要 JUCE_MODAL_LOOPS_PERMITTED（本工具已开启）。
    juce::MessageManager::getInstance()->runDispatchLoopUntil (400);

    // 切页（仅用于截图验证）：通过模拟点击页签实现
    if (pageArg == "click" || pageArg == "eq" || pageArg == "dyn" || pageArg == "mod")
    {
        juce::Array<juce::TextButton*> tabs;
        std::function<void(juce::Component*)> walk = [&] (juce::Component* c)
        {
            for (int i = 0; i < c->getNumChildComponents(); ++i)
            {
                auto* ch = c->getChildComponent (i);
                if (auto* tb = dynamic_cast<juce::TextButton*> (ch)) tabs.add (tb);
                walk (ch);
            }
        };
        walk (editor.get());

        const juce::String want = (pageArg == "click") ? "CLICK"
                                : (pageArg == "eq")    ? "EQ"
                                : (pageArg == "mod")   ? "MOD" : "DYNAMICS";
        // 按尺寸区分功能页签与波形源按钮（两者可能同名）
        for (auto* tb : tabs)
            if (tb->getWidth() >= 150 && tb->getButtonText() == want)
                { tb->triggerClick(); break; }

        for (int k = 0; k < 8; ++k)
            juce::MessageManager::getInstance()->runDispatchLoopUntil (40);
    }

    // 渲染到图像
    juce::Image img (juce::Image::ARGB, width, height, true);
    {
        juce::Graphics g (img);
        editor->paintEntireComponent (g, true);
    }

    // 检查不是纯色（说明真的画了东西）
    juce::Image::BitmapData bd (img, juce::Image::BitmapData::readOnly);
    int distinctColours = 0;
    juce::SortedSet<juce::uint32> seen;
    for (int y = 0; y < height; y += 4)
        for (int x = 0; x < width; x += 4)
            seen.add (bd.getPixelColour (x, y).getARGB());

    distinctColours = seen.size();

    const juce::String pageTag = pageArg.isNotEmpty() ? ("_" + pageArg) : juce::String();
    const auto outFile = outDir.getChildFile ("ui_" + juce::String (width) + "x"
                                              + juce::String (height) + pageTag + ".png");
    outFile.deleteFile();

    juce::PNGImageFormat png;
    std::unique_ptr<juce::FileOutputStream> fs (outFile.createOutputStream());
    if (fs != nullptr)
        png.writeImageToStream (img, *fs);

    std::cout << "  截图: " << outFile.getFullPathName() << std::endl;
    std::cout << "  不同颜色数: " << distinctColours << std::endl;

    bool ok = true;
    if (distinctColours < 50)
    {
        std::cout << "  [FAIL] 界面几乎是纯色，可能没有正确绘制" << std::endl;
        ok = false;
    }
    else
    {
        std::cout << "  [PASS] 界面已绘制（颜色丰富）" << std::endl;
    }

    // 顺便验证编辑器尺寸约束
    std::cout << "  编辑器尺寸: " << editor->getWidth() << "x" << editor->getHeight() << std::endl;

    editor->setVisible (false);
    editor.reset();

    return ok ? 0 : 1;
}
