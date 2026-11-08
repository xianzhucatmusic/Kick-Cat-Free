//==============================================================================
// EQ 曲线交互测试
//
// 直接构造 EqCurveEditor，模拟鼠标拖动，验证：
//   - 横向拖动真的改变频率
//   - 纵向拖动真的改变增益
//   - 滚轮真的改变 Q
//   - 曲线响应计算与滤波器一致
//   - 拖动未启用的段会自动启用
//
// 这类"交互式 UI"最容易只做到"看起来对"，所以用程序模拟真实鼠标事件来验证。
//==============================================================================

#include <juce_gui_basics/juce_gui_basics.h>
#include <iostream>
#include <cmath>

#include "EqCurveEditor.h"
#include "Equalizer.h"

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

/** 构造一个鼠标事件（按 JUCE 8.0.4 的 MouseEvent 构造签名）。 */
static juce::MouseEvent makeEvent (juce::Component& c, juce::Point<float> pos,
                                   juce::ModifierKeys mods = juce::ModifierKeys(),
                                   bool wasDragged = false)
{
    return juce::MouseEvent (juce::Desktop::getInstance().getMainMouseSource(),
                             pos,            // position
                             mods,           // modifiers
                             1.0f,           // pressure
                             0.0f, 0.0f,     // orientation, rotation
                             0.0f, 0.0f,     // tiltX, tiltY
                             &c,             // eventComponent
                             &c,             // originator
                             juce::Time::getCurrentTime(),   // eventTime
                             pos,            // mouseDownPos
                             juce::Time::getCurrentTime(),   // mouseDownTime
                             1,              // numberOfClicks
                             wasDragged);
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const int W = 800, H = 260;

    std::cout << "==============================================\n";
    std::cout << " EqCurveEditor 交互测试\n";
    std::cout << "==============================================\n" << std::endl;

    //==========================================================================
    std::cout << "--- 1. 基本几何 ---" << std::endl;
    {
        EqCurveEditor eq;
        eq.setSize (W, H);

        check (eq.getBand (0).freq > 0.0f, "默认有 8 段");
        check (EqCurveEditor::kNumBands == 8, "段数 = 8");

        // 全部关闭时，总响应应为 0dB
        float maxAbs = 0.0f;
        for (float f = 20.0f; f < 20000.0f; f *= 1.3f)
            maxAbs = juce::jmax (maxAbs, std::abs (eq.getTotalResponseDb (f)));

        check (maxAbs < 0.01f, "全部关闭时总响应为 0dB",
               "max = " + juce::String (maxAbs, 4) + " dB");
    }

    //==========================================================================
    std::cout << "\n--- 2. 启用一段后曲线真的有变化 ---" << std::endl;
    {
        EqCurveEditor eq;
        eq.setSize (W, H);

        // 设置第 3 段（70Hz）为 +12dB 并启用
        EqCurveEditor::BandState s;
        s.freq = 70.0f;
        s.gainDb = 12.0f;
        s.q = 0.9f;
        s.shape = EqCurveEditor::Shape::Peak;
        s.enabled = true;
        eq.setBand (2, s);

        const float at70 = eq.getTotalResponseDb (70.0f);
        const float at1k = eq.getTotalResponseDb (1000.0f);

        check (at70 > 8.0f, "70Hz 处响应 > +8dB",
               juce::String (at70, 2) + " dB");
        check (std::abs (at1k) < 1.5f, "1kHz 处几乎不受影响",
               juce::String (at1k, 2) + " dB");
    }

    //==========================================================================
    std::cout << "\n--- 3. 曲线计算与真实 DSP 一致 ---" << std::endl;
    {
        // EqCurveEditor 的响应计算必须与 Equalizer 的实际滤波一致，
        // 否则画面上看到的曲线就是在骗用户。
        EqCurveEditor eq;
        eq.setSize (W, H);

        Equalizer dsp;
        juce::dsp::ProcessSpec spec { 44100.0, 512, 2 };
        dsp.prepare (spec);

        const float testFreq = 120.0f;
        const float testGain = 9.0f;

        // UI 侧
        EqCurveEditor::BandState s;
        s.freq = testFreq;
        s.gainDb = testGain;
        s.q = 1.0f;
        s.shape = EqCurveEditor::Shape::Peak;
        s.enabled = true;
        eq.setBand (3, s);
        const float uiDb = eq.getTotalResponseDb (testFreq);

        // DSP 侧：只开这一段
        for (int b = 0; b < Equalizer::kNumBands; ++b)
            dsp.setBandParams (b, Equalizer::defaultFrequencies()[(size_t) b],
                               0.0f, 1.0f, Equalizer::BandType::Peak, false);

        dsp.setBandParams (3, testFreq, testGain, 1.0f, Equalizer::BandType::Peak, true);
        const float dspDb = dsp.getResponseDb (testFreq);

        check (std::abs (uiDb - dspDb) < 0.5f,
               "UI 曲线与 DSP 滤波器响应一致",
               "UI " + juce::String (uiDb, 2) + " dB  vs  DSP " + juce::String (dspDb, 2) + " dB");
    }

    //==========================================================================
    std::cout << "\n--- 4. 鼠标拖动改变频率与增益 ---" << std::endl;
    {
        EqCurveEditor eq;
        eq.setSize (W, H);

        // 先记录拖动回调是否被调用
        int callbackCount = 0;
        int lastBand = -1;
        EqCurveEditor::BandState lastState;
        eq.onBandChanged = [&] (int b, const EqCurveEditor::BandState& st)
        {
            ++callbackCount;
            lastBand = b;
            lastState = st;
        };

        bool enableCallbackFired = false;
        eq.onBandEnableChanged = [&] (int, bool) { enableCallbackFired = true; };

        // 命中第 4 段（默认 120Hz, 0dB）的屏幕位置
        auto state = eq.getBand (3);
        const float beforeFreq = state.freq;
        const float beforeGain = state.gainDb;

        // 计算该点的屏幕坐标（复刻 EqCurveEditor 的映射）
        auto plotArea = eq.getLocalBounds().toFloat().reduced (2.0f)
                          .withTrimmedLeft (26.0f).withTrimmedBottom (13.0f);
        auto freqToNorm = [] (float f)
        {
            const float lo = std::log10 (20.0f), hi = std::log10 (20000.0f);
            return (std::log10 (juce::jlimit (20.0f, 20000.0f, f)) - lo) / (hi - lo);
        };
        auto gainToNorm = [] (float g) { return 0.5f - juce::jlimit (-18.0f, 18.0f, g) / 36.0f; };

        const juce::Point<float> startPoint (
            plotArea.getX() + freqToNorm (beforeFreq) * plotArea.getWidth(),
            plotArea.getY() + gainToNorm (beforeGain) * plotArea.getHeight());

        eq.mouseDown (makeEvent (eq, startPoint));

        check (enableCallbackFired, "拖动未启用的段会自动启用它");

        // 往右上方拖：频率变高、增益变正
        const juce::Point<float> endPoint (
            plotArea.getX() + freqToNorm (800.0f) * plotArea.getWidth(),
            plotArea.getY() + gainToNorm (6.0f) * plotArea.getHeight());

        eq.mouseDrag (makeEvent (eq, endPoint, juce::ModifierKeys(), true));
        eq.mouseUp (makeEvent (eq, endPoint));

        check (callbackCount > 0, "拖动触发了参数回调",
               juce::String (callbackCount) + " 次");
        check (lastBand == 3, "回调报告的是被拖动的段",
               "band = " + juce::String (lastBand));

        const auto after = eq.getBand (3);
        check (after.freq > beforeFreq * 2.0f,
               "横向拖动提高了频率",
               juce::String (beforeFreq, 1) + " -> " + juce::String (after.freq, 1) + " Hz");
        check (after.gainDb > 3.0f,
               "纵向拖动提高了增益",
               juce::String (beforeGain, 1) + " -> " + juce::String (after.gainDb, 1) + " dB");

        // 频率应接近 800Hz
        check (std::abs (after.freq - 800.0f) < 120.0f, "频率落在目标附近",
               juce::String (after.freq, 1) + " Hz (目标 800)");
    }

    //==========================================================================
    std::cout << "\n--- 5. 滚轮改变 Q / Shift+滚轮改变增益 ---" << std::endl;
    {
        EqCurveEditor eq;
        eq.setSize (W, H);

        EqCurveEditor::BandState s;
        s.freq = 1000.0f; s.gainDb = 0.0f; s.q = 1.0f;
        s.shape = EqCurveEditor::Shape::Peak; s.enabled = true;
        eq.setBand (5, s);

        int cb = 0;
        eq.onBandChanged = [&] (int, const EqCurveEditor::BandState&) { ++cb; };

        auto plotArea = eq.getLocalBounds().toFloat().reduced (2.0f)
                          .withTrimmedLeft (26.0f).withTrimmedBottom (13.0f);
        auto freqToNorm = [] (float f)
        {
            const float lo = std::log10 (20.0f), hi = std::log10 (20000.0f);
            return (std::log10 (f) - lo) / (hi - lo);
        };
        const juce::Point<float> pt (plotArea.getX() + freqToNorm (1000.0f) * plotArea.getWidth(),
                                     plotArea.getCentreY());

        // 滚轮上滚 -> Q 变大
        const float qBefore = eq.getBand (5).q;
        juce::MouseWheelDetails wheel;
        wheel.deltaY = 0.5f;
        wheel.deltaX = 0.0f;
        wheel.isReversed = false;
        wheel.isSmooth = false;
        wheel.isInertial = false;

        eq.mouseWheelMove (makeEvent (eq, pt), wheel);
        const float qAfter = eq.getBand (5).q;

        check (qAfter > qBefore, "滚轮上滚提高 Q",
               juce::String (qBefore, 2) + " -> " + juce::String (qAfter, 2));

        // Shift + 滚轮 -> 改增益
        const float gBefore = eq.getBand (5).gainDb;
        eq.mouseWheelMove (makeEvent (eq, pt, juce::ModifierKeys (juce::ModifierKeys::shiftModifier)),
                           wheel);
        const float gAfter = eq.getBand (5).gainDb;

        check (std::abs (gAfter - gBefore) > 0.1f, "Shift+滚轮改变增益",
               juce::String (gBefore, 2) + " -> " + juce::String (gAfter, 2) + " dB");

        check (cb >= 2, "两种滚轮操作都触发了回调", juce::String (cb) + " 次");
    }

    //==========================================================================
    std::cout << "\n--- 6. 双击重置增益 ---" << std::endl;
    {
        EqCurveEditor eq;
        eq.setSize (W, H);

        EqCurveEditor::BandState s;
        s.freq = 250.0f; s.gainDb = -11.0f; s.q = 2.0f;
        s.shape = EqCurveEditor::Shape::Peak; s.enabled = true;
        eq.setBand (4, s);

        auto plotArea = eq.getLocalBounds().toFloat().reduced (2.0f)
                          .withTrimmedLeft (26.0f).withTrimmedBottom (13.0f);
        auto freqToNorm = [] (float f)
        {
            const float lo = std::log10 (20.0f), hi = std::log10 (20000.0f);
            return (std::log10 (f) - lo) / (hi - lo);
        };
        auto gainToNorm = [] (float g) { return 0.5f - juce::jlimit (-18.0f, 18.0f, g) / 36.0f; };

        const juce::Point<float> pt (
            plotArea.getX() + freqToNorm (250.0f) * plotArea.getWidth(),
            plotArea.getY() + gainToNorm (-11.0f) * plotArea.getHeight());

        eq.mouseDoubleClick (makeEvent (eq, pt));

        check (std::abs (eq.getBand (4).gainDb) < 0.01f, "双击把增益归零",
               juce::String (eq.getBand (4).gainDb, 3) + " dB");
    }

    //==========================================================================
    std::cout << "\n--- 7. HP / LP 类型不产生增益 ---" << std::endl;
    {
        EqCurveEditor eq;
        eq.setSize (W, H);

        EqCurveEditor::BandState s;
        s.freq = 100.0f; s.gainDb = 0.0f; s.q = 0.7f;
        s.shape = EqCurveEditor::Shape::HighPass; s.enabled = true;
        eq.setBand (0, s);

        // 高通应让低频衰减
        const float at20 = eq.getTotalResponseDb (20.0f);
        const float at5k = eq.getTotalResponseDb (5000.0f);

        check (at20 < -6.0f, "高通让 20Hz 大幅衰减",
               juce::String (at20, 1) + " dB");
        check (std::abs (at5k) < 1.0f, "高通对 5kHz 无影响",
               juce::String (at5k, 2) + " dB");
    }

    //==========================================================================
    std::cout << "\n--- 8. 拖动时数值被合理量化 ---" << std::endl;
    {
        EqCurveEditor eq;
        eq.setSize (W, H);

        EqCurveEditor::BandState s;
        s.freq = 500.0f; s.gainDb = 0.0f; s.q = 1.0f;
        s.shape = EqCurveEditor::Shape::Peak; s.enabled = true;
        eq.setBand (6, s);

        auto plotArea = eq.getLocalBounds().toFloat().reduced (2.0f)
                          .withTrimmedLeft (26.0f).withTrimmedBottom (13.0f);
        auto freqToNorm = [] (float f)
        {
            const float lo = std::log10 (20.0f), hi = std::log10 (20000.0f);
            return (std::log10 (f) - lo) / (hi - lo);
        };

        const juce::Point<float> start (plotArea.getX() + freqToNorm (500.0f) * plotArea.getWidth(),
                                        plotArea.getCentreY());
        eq.mouseDown (makeEvent (eq, start));

        // 拖到一个会落在 1kHz 以上的位置，检查量化后是可读整数
        const juce::Point<float> end (plotArea.getX() + freqToNorm (3456.7f) * plotArea.getWidth(),
                                      plotArea.getCentreY());
        eq.mouseDrag (makeEvent (eq, end, juce::ModifierKeys(), true));
        eq.mouseUp (makeEvent (eq, end));

        const float f = eq.getBand (6).freq;
        const bool readable = (std::abs (f - std::round (f / 10.0f) * 10.0f) < 0.01f);

        check (readable, "高频段频率按 10Hz 量化（数字可读）",
               juce::String (f, 2) + " Hz");
        check (f > 2000.0f && f < 5000.0f, "频率落在拖动目标附近",
               juce::String (f, 1) + " Hz");
    }

    //==========================================================================
    std::cout << "\n--- 9. 高切/低切的斜率真的会变 ---" << std::endl;
    {
        // 回归测试：单级双二阶斜率固定 12dB/oct，Q 改不了斜率。
        // 用户反馈"高切低切无法仅改变斜率"就是这个问题。
        // 现在斜率是独立参数，靠级联实现 12/24/36/48 dB/oct。

        // --- UI 曲线侧：不同斜率应给出不同的衰减
        float dbAt500[4] = {};
        float dbAtCut[4] = {};

        for (int si = 0; si < 4; ++si)
        {
            EqCurveEditor eq;
            eq.setSize (W, H);

            EqCurveEditor::BandState s;
            s.freq = 1000.0f;
            s.gainDb = 0.0f;
            s.q = 0.707f;
            s.shape = EqCurveEditor::Shape::HighPass;
            s.enabled = true;
            s.slopeDbPerOct = (si + 1) * 12;
            eq.setBand (0, s);

            dbAt500[si] = eq.getTotalResponseDb (500.0f);
            dbAtCut[si] = eq.getTotalResponseDb (1000.0f);
        }

        // 截止点必须始终约 -3dB（否则斜率一变滤波器就跑偏）
        bool cutoffOk = true;
        for (int si = 0; si < 4; ++si)
            if (std::abs (dbAtCut[si] + 3.0f) > 0.5f) cutoffOk = false;

        check (cutoffOk, "各斜率下截止点都保持在 -3dB",
               juce::String (dbAtCut[0], 2) + " / " + juce::String (dbAtCut[1], 2) + " / "
             + juce::String (dbAtCut[2], 2) + " / " + juce::String (dbAtCut[3], 2) + " dB");

        // 斜率越大，同一频点的衰减必须越多（严格单调）
        check (dbAt500[0] > dbAt500[1] && dbAt500[1] > dbAt500[2] && dbAt500[2] > dbAt500[3],
               "斜率越大、500Hz 处衰减越多（单调）",
               juce::String (dbAt500[0], 1) + " > " + juce::String (dbAt500[1], 1) + " > "
             + juce::String (dbAt500[2], 1) + " > " + juce::String (dbAt500[3], 1) + " dB");

        // --- 实测斜率：500Hz 与 250Hz 之间每八度衰减多少
        for (int si = 0; si < 4; ++si)
        {
            EqCurveEditor eq;
            eq.setSize (W, H);

            EqCurveEditor::BandState s;
            s.freq = 1000.0f; s.q = 0.707f;
            s.shape = EqCurveEditor::Shape::HighPass;
            s.enabled = true;
            s.slopeDbPerOct = (si + 1) * 12;
            eq.setBand (0, s);

            const float a = eq.getTotalResponseDb (500.0f);
            const float b2 = eq.getTotalResponseDb (250.0f);
            const float measured = b2 - a;   // 负值，一个八度的衰减量
            const int expect = (si + 1) * 12;

            check (std::abs (std::abs (measured) - (float) expect) < 4.0f,
                   juce::String (expect) + " dB/oct 设定与实测相符",
                   "实测 " + juce::String (std::abs (measured), 1) + " dB/oct");
        }

        // --- DSP 侧：级联级数正确
        for (int slope : { 12, 24, 36, 48 })
        {
            Equalizer dsp;
            juce::dsp::ProcessSpec spec { 44100.0, 512, 2 };
            dsp.prepare (spec);
            for (int b = 0; b < Equalizer::kNumBands; ++b)
                dsp.setBandParams (b, Equalizer::defaultFrequencies()[(size_t) b],
                                   0.0f, 0.9f, Equalizer::BandType::Peak, false, 12);
            dsp.setBandParams (0, 1000.0f, 0.0f, 0.707f,
                               Equalizer::BandType::HighPass, true, slope);

            const float atCut = dsp.getResponseDb (1000.0f);
            check (std::abs (atCut + 3.0f) < 1.5f,
                   "DSP 侧 " + juce::String (slope) + " dB/oct 截止点仍为 -3dB",
                   juce::String (atCut, 2) + " dB");
        }
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
