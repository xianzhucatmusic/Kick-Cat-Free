#include "EqCurveEditor.h"
#include "Theme.h"
#include <juce_dsp/juce_dsp.h>

namespace kf
{

//==============================================================================
// 频率轴范围：20Hz .. 20kHz（对数）
// 增益轴范围：±18dB（与参数范围一致）
//==============================================================================
namespace
{
    constexpr float kMinFreq = 20.0f;
    constexpr float kMaxFreq = 20000.0f;
    constexpr float kMaxGain = 18.0f;

    /** 复数幅度 -> dB。 */
    inline float magToDb (double mag) noexcept
    {
        return (float) juce::Decibels::gainToDecibels (mag, -120.0);
    }
}

//==============================================================================

EqCurveEditor::EqCurveEditor()
{
    setWantsKeyboardFocus (false);
    setMouseCursor (juce::MouseCursor::PointingHandCursor);

    // 默认 8 段的中心频率，与 DSP 侧的 Equalizer::defaultFrequencies() 一致
    static const float defFreqs[kNumBands]
        { 20.0f, 40.0f, 70.0f, 120.0f, 250.0f, 600.0f, 2500.0f, 8000.0f };
    static const char* defNames[kNumBands]
        { "Sub Rumble", "Sub Body", "Punch", "Fundamental", "Mud", "Box", "Attack", "Air" };

    // 首尾两段默认用 HP / LS 更符合底鼓处理习惯，但保持 disabled
    for (int i = 0; i < kNumBands; ++i)
    {
        bands[(size_t) i].freq = defFreqs[i];
        bands[(size_t) i].gainDb = 0.0f;
        bands[(size_t) i].q = 0.9f;
        bands[(size_t) i].enabled = false;
        bands[(size_t) i].name = defNames[i];
        bands[(size_t) i].shape = (i == 0) ? Shape::HighPass
                                : (i == kNumBands - 1) ? Shape::HighShelf
                                                       : Shape::Peak;
    }
}

juce::Colour EqCurveEditor::colourForBand (int index)
{
    // 低频暖（橙/红）-> 高频冷（青/蓝），视觉上能一眼对应到频段
    static const juce::Colour cols[kNumBands]
    {
        theme::meterHigh, theme::orange,
        theme::cyan, theme::orange,
        theme::orangeBright, theme::orangeBright,
        theme::cyan, theme::cyan
    };
    return cols[juce::jlimit (0, kNumBands - 1, index)];
}

void EqCurveEditor::setBand (int index, const BandState& state)
{
    if (index < 0 || index >= kNumBands)
        return;

    auto s = state;
    // name 由 UI 维护，不被外部覆盖（外部不关心显示名）
    if (s.name.isEmpty())
        s.name = bands[(size_t) index].name;

    bands[(size_t) index] = s;
    repaint();
}

const EqCurveEditor::BandState& EqCurveEditor::getBand (int index) const
{
    static BandState dummy;
    if (index < 0 || index >= kNumBands)
        return dummy;
    return bands[(size_t) index];
}

//==============================================================================
// 坐标换算
//==============================================================================

float EqCurveEditor::freqToNorm (float freq) noexcept
{
    freq = juce::jlimit (kMinFreq, kMaxFreq, freq);
    return (std::log10 (freq) - std::log10 (kMinFreq))
         / (std::log10 (kMaxFreq) - std::log10 (kMinFreq));
}

float EqCurveEditor::normToFreq (float norm) noexcept
{
    norm = juce::jlimit (0.0f, 1.0f, norm);
    const float lo = std::log10 (kMinFreq);
    const float hi = std::log10 (kMaxFreq);
    return std::pow (10.0f, lo + norm * (hi - lo));
}

float EqCurveEditor::gainToNorm (float gainDb) noexcept
{
    // 0dB 在中间；上下各 kMaxGain
    return 0.5f - juce::jlimit (-kMaxGain, kMaxGain, gainDb) / (2.0f * kMaxGain);
}

float EqCurveEditor::normToGain (float norm) noexcept
{
    return (0.5f - juce::jlimit (0.0f, 1.0f, norm)) * 2.0f * kMaxGain;
}

juce::Rectangle<float> EqCurveEditor::getPlotArea() const
{
    // 左侧留频率标签、底部留频率刻度
    return getLocalBounds().toFloat().reduced (2.0f).withTrimmedLeft (26.0f).withTrimmedBottom (13.0f);
}

juce::Point<float> EqCurveEditor::toScreen (float freq, float gainDb) const
{
    const auto a = getPlotArea();
    return { a.getX() + freqToNorm (freq) * a.getWidth(),
             a.getY() + gainToNorm (gainDb) * a.getHeight() };
}

//==============================================================================
// 滤波器响应
//
// 用与 DSP 侧相同的双二阶系数来算幅度响应，保证画面与听感一致。
// （借 juce::dsp::IIR::Coefficients 的 make* 与 getMagnitudeForFrequency）
//==============================================================================

float EqCurveEditor::bandResponseDb (const BandState& b, float frequency) const
{
    using Coeffs = juce::dsp::IIR::Coefficients<float>;

    constexpr double sr = 44100.0;   // 显示用；采样率对归一化曲线形状影响很小
    const double f = (double) juce::jlimit (10.0f, 20000.0f, b.freq);
    const float Q = juce::jlimit (0.1f, 12.0f, b.q);
    const float g = juce::Decibels::decibelsToGain (b.gainDb);

    Coeffs::Ptr c;

    switch (b.shape)
    {
        case Shape::LowShelf:  c = Coeffs::makeLowShelf  (sr, f, Q, g); break;
        case Shape::HighShelf: c = Coeffs::makeHighShelf (sr, f, Q, g); break;
        case Shape::HighPass:
        case Shape::LowPass:
        {
            // 高切/低切是多级级联：总响应 = 各级相乘。
            // 这里必须和 Equalizer 用同一套 Q 分配，否则画出来的曲线
            // 与实际听感对不上（这正是"可视化 EQ"最容易骗人的地方）。
            const int stages = juce::jlimit (1, 4, b.slopeDbPerOct / 12);
            const double stageQ = (stages <= 1)
                                    ? 0.70710678
                                    : std::pow (10.0, -3.0 / (20.0 * (double) stages));

            const bool hp = (b.shape == Shape::HighPass);
            double totalMag = 1.0;

            for (int st = 0; st < stages; ++st)
            {
                auto cs = hp ? Coeffs::makeHighPass (sr, f, (float) stageQ)
                             : Coeffs::makeLowPass  (sr, f, (float) stageQ);
                totalMag *= cs->getMagnitudeForFrequency ((double) frequency, sr);
            }

            return magToDb (totalMag);
        }
        case Shape::Peak:
        default:               c = Coeffs::makePeakFilter (sr, f, Q, g); break;
    }

    if (c == nullptr)
        return 0.0f;

    return magToDb (c->getMagnitudeForFrequency ((double) frequency, sr));
}

float EqCurveEditor::getTotalResponseDb (float frequency) const
{
    float total = 0.0f;

    for (const auto& b : bands)
    {
        if (! b.enabled)
            continue;
        total += bandResponseDb (b, frequency);
    }

    return total;
}

//==============================================================================
// 绘制
//==============================================================================

void EqCurveEditor::drawGrid (juce::Graphics& g, juce::Rectangle<float> area)
{
    // 背景
    g.setColour (theme::panelDeep);
    g.fillRoundedRectangle (area, 4.0f);

    // --- 纵向频率网格线（对数，标出 50/100/500/1k/5k/10k）
    static const float gridFreqs[] { 30.0f, 50.0f, 100.0f, 200.0f, 500.0f,
                                     1000.0f, 2000.0f, 5000.0f, 10000.0f };
    g.setColour (theme::outline);
    for (float f : gridFreqs)
    {
        const float x = area.getX() + freqToNorm (f) * area.getWidth();
        g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
    }

    // --- 横向增益网格线：+12 / +6 / 0 / -6 / -12
    static const float gridGains[] { 12.0f, 6.0f, 0.0f, -6.0f, -12.0f };
    for (float gn : gridGains)
    {
        const float y = area.getY() + gainToNorm (gn) * area.getHeight();

        // 0dB 线更亮
        g.setColour (gn == 0.0f ? theme::outlineSoft : theme::outline);
        g.drawHorizontalLine ((int) y, area.getX(), area.getRight());
    }

    // 外框
    g.setColour (theme::outline);
    g.drawRoundedRectangle (area, 4.0f, 1.0f);

    // --- 坐标标签
    g.setFont (juce::Font (juce::FontOptions (8.5f)));

    // 频率刻度（底部）
    g.setColour (theme::textFaint);
    struct FreqLabel { float freq; const char* text; };
    static const FreqLabel freqLabels[]
    {
        { 50.0f, "50" }, { 100.0f, "100" }, { 500.0f, "500" },
        { 1000.0f, "1k" }, { 5000.0f, "5k" }, { 10000.0f, "10k" }
    };
    for (const auto& fl : freqLabels)
    {
        const float x = area.getX() + freqToNorm (fl.freq) * area.getWidth();
        g.drawText (fl.text,
                    juce::Rectangle<float> (x - 16.0f, area.getBottom() + 1.0f, 32.0f, 11.0f),
                    juce::Justification::centred, false);
    }

    // 增益刻度（左侧）
    for (float gn : gridGains)
    {
        const float y = area.getY() + gainToNorm (gn) * area.getHeight();
        g.setColour (theme::textFaint);
        g.drawText (juce::String ((int) gn),
                    juce::Rectangle<float> (area.getX() - 25.0f, y - 5.5f, 22.0f, 11.0f),
                    juce::Justification::centredRight, false);
    }
}

void EqCurveEditor::drawCurve (juce::Graphics& g, juce::Rectangle<float> area)
{
    // --- 合成总响应曲线（对数频率上均匀取样）
    constexpr int kSteps = 220;
    juce::Path curve;
    juce::Path fill;

    bool first = true;

    for (int i = 0; i <= kSteps; ++i)
    {
        const float norm = (float) i / (float) kSteps;
        const float freq = normToFreq (norm);
        const float db = getTotalResponseDb (freq);

        const float x = area.getX() + norm * area.getWidth();
        const float y = area.getY() + gainToNorm (db) * area.getHeight();

        if (first)
        {
            curve.startNewSubPath (x, y);
            fill.startNewSubPath (x, area.getY() + gainToNorm (0.0f) * area.getHeight());
            fill.lineTo (x, y);
            first = false;
        }
        else
        {
            curve.lineTo (x, y);
            fill.lineTo (x, y);
        }
    }

    // 曲线回落到 0dB 基准线，形成填充区域
    fill.lineTo (area.getRight(), area.getY() + gainToNorm (0.0f) * area.getHeight());
    fill.closeSubPath();

    // 填充：粉蓝渐变，淡淡的，不抢主曲线
    juce::ColourGradient grad (theme::cyan.withAlpha (0.22f), area.getX(), area.getY(),
                               theme::cyan.withAlpha (0.16f), area.getRight(), area.getY(), false);
    g.setGradientFill (grad);
    g.fillPath (fill);

    // 主曲线：横向粉->蓝渐变，呼应主题
    juce::ColourGradient lineGrad (theme::cyan, area.getX(), area.getCentreY(),
                                   theme::cyan, area.getRight(), area.getCentreY(), false);
    g.setGradientFill (lineGrad);
    g.strokePath (curve, juce::PathStrokeType (2.0f,
                                               juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));
}

void EqCurveEditor::drawHandles (juce::Graphics& g, juce::Rectangle<float> area)
{
    juce::ignoreUnused (area);

    for (int i = 0; i < kNumBands; ++i)
    {
        const auto& b = bands[(size_t) i];
        const auto p = toScreen (b.freq, b.gainDb);
        const auto col = colourForBand (i);

        const bool hovered = (i == hoverBand);
        const bool active = (i == dragBand);

        // 未启用的段用淡色小点，表示"存在但不生效"
        const float r = (hovered || active) ? 6.5f : 5.0f;
        const float alpha = b.enabled ? 1.0f : 0.35f;

        // 外圈光晕（悬停/拖动时）
        if (hovered || active)
        {
            g.setColour (col.withAlpha (0.25f));
            g.fillEllipse (p.x - r - 4.0f, p.y - r - 4.0f, (r + 4.0f) * 2.0f, (r + 4.0f) * 2.0f);
        }

        // 手柄圆环
        g.setColour (theme::bg.withAlpha (alpha));
        g.fillEllipse (p.x - r, p.y - r, r * 2.0f, r * 2.0f);

        g.setColour (col.withAlpha (alpha));
        g.drawEllipse (p.x - r, p.y - r, r * 2.0f, r * 2.0f, 2.0f);

        // 中心实心点
        g.fillEllipse (p.x - r * 0.35f, p.y - r * 0.35f, r * 0.7f, r * 0.7f);

        // 段号
        g.setColour (theme::bg.withAlpha (b.enabled ? 0.9f : 0.5f));
        g.setFont (juce::Font (juce::FontOptions (8.0f).withStyle ("Bold")));
        g.drawText (juce::String (i + 1),
                    juce::Rectangle<float> (p.x - r, p.y - r * 0.75f, r * 2.0f, r * 1.5f),
                    juce::Justification::centred, false);
    }

    // --- 悬停/拖动时显示数值气泡
    const int infoBand = (dragBand >= 0) ? dragBand : hoverBand;
    if (infoBand >= 0 && infoBand < kNumBands)
    {
        const auto& b = bands[(size_t) infoBand];
        const auto p = toScreen (b.freq, b.gainDb);

        juce::String shapeName;
        switch (b.shape)
        {
            case Shape::LowShelf:  shapeName = "LOW SHELF";  break;
            case Shape::HighShelf: shapeName = "HIGH SHELF"; break;
            case Shape::HighPass:  shapeName = "HP";         break;
            case Shape::LowPass:   shapeName = "LP";         break;
            case Shape::Peak:
            default:               shapeName = "BELL";       break;
        }

        juce::String txt;
        txt << shapeName << "   " << juce::String (b.name) << "\n"
            << juce::String (b.freq, b.freq < 1000.0f ? 1 : 0) << " Hz";

        // 高切/低切显示斜率而不是增益（它们没有增益概念），Q 也不参与听感
        if (b.shape == Shape::HighPass || b.shape == Shape::LowPass)
            txt << "   " << b.slopeDbPerOct << " dB/oct";
        else
            txt << "   " << (b.gainDb >= 0.0f ? "+" : "") << juce::String (b.gainDb, 1) << " dB"
                << "   Q " << juce::String (b.q, 2);

        if (! b.enabled)
            txt << "   (off)";

        g.setFont (juce::Font (juce::FontOptions (10.0f)));

        const int textW = 210;
        const int textH = 30;

        // 气泡尽量不贴边
        float bx = juce::jlimit (4.0f, (float) getWidth() - textW - 4.0f, p.x + 12.0f);
        float by = juce::jlimit (4.0f, (float) getHeight() - textH - 4.0f, p.y - textH - 10.0f);

        juce::Rectangle<float> box (bx, by, (float) textW, (float) textH);

        g.setColour (theme::panel.withAlpha(0.94f));
        g.fillRoundedRectangle (box, 4.0f);
        g.setColour (colourForBand (infoBand).withAlpha (0.7f));
        g.drawRoundedRectangle (box, 4.0f, 1.0f);

        g.setColour (theme::text);
        g.drawFittedText (txt, box.toNearestInt().reduced (6, 3),
                          juce::Justification::centredLeft, 2);
    }
}


//==============================================================================
// 频谱：把时域样本做 FFT，转成 dB 并做时间平滑
//==============================================================================
void EqCurveEditor::pushSpectrum (const float* samples, int numSamples)
{
    if (samples == nullptr || numSamples <= 0)
        return;

    // 只取最新的 kFftSize 个样本
    const int n = juce::jmin (numSamples, kFftSize);
    const int offset = numSamples - n;

    std::fill (fftData.begin(), fftData.end(), 0.0f);
    for (int i = 0; i < n; ++i)
        fftData[(size_t) i] = samples[offset + i];

    window.multiplyWithWindowingTable (fftData.data(), (size_t) kFftSize);
    fft.performFrequencyOnlyForwardTransform (fftData.data());

    // 转 dB 并做指数平滑（避免每帧跳动）
    const float smooth = juce::jlimit (0.0f, 0.95f, spectrumSmoothing);

    for (int b = 0; b < kNumBins; ++b)
    {
        const float mag = fftData[(size_t) b];
        const float db = juce::Decibels::gainToDecibels (juce::jmax (1.0e-7f, mag));

        if (! spectrumPrimed)
            spectrumDb[(size_t) b] = db;
        else
            spectrumDb[(size_t) b] = spectrumDb[(size_t) b] * smooth + db * (1.0f - smooth);
    }

    spectrumPrimed = true;
    ++spectrumAge;
}


//==============================================================================
// 绘制实时频谱（填充在 EQ 曲线下面一层）
//==============================================================================
void EqCurveEditor::drawSpectrum (juce::Graphics& g, juce::Rectangle<float> area)
{
    if (! spectrumVisible || ! spectrumPrimed || spectrumAge < 2)
        return;

    // 频率 -> FFT bin 的换算
    const double sr = sampleRateForSpectrum > 0.0 ? sampleRateForSpectrum : 44100.0;
    const double binHz = sr / (double) kFftSize;

    // 频谱的 dB 显示范围：-84 .. 0
    constexpr float kMinDb = -84.0f;
    constexpr float kMaxDb = 0.0f;

    constexpr int kSteps = 180;
    juce::Path spec;

    for (int i = 0; i <= kSteps; ++i)
    {
        const float norm = (float) i / (float) kSteps;
        const float freq = normToFreq (norm);

        const int bin = juce::jlimit (1, kNumBins - 1, (int) std::round (freq / binHz));
        const float db = spectrumDb[(size_t) bin];

        const float t = juce::jlimit (0.0f, 1.0f, (db - kMinDb) / (kMaxDb - kMinDb));
        const float x = area.getX() + norm * area.getWidth();
        const float y = area.getBottom() - t * area.getHeight();

        if (i == 0) spec.startNewSubPath (x, y);
        else        spec.lineTo (x, y);
    }

    // 填充 + 描边：用低饱和度的颜色，避免盖过 EQ 曲线
    {
        juce::Path fill (spec);
        fill.lineTo (area.getRight(), area.getBottom());
        fill.lineTo (area.getX(),     area.getBottom());
        fill.closeSubPath();

        g.setColour (theme::cyan.withAlpha (0.10f));
        g.fillPath (fill);
    }

    g.setColour (theme::cyan.withAlpha (0.38f));
    g.strokePath (spec, juce::PathStrokeType (1.0f));
}

void EqCurveEditor::paint (juce::Graphics& g)
{
    const auto area = getPlotArea();

    drawGrid (g, area);
    drawSpectrum (g, area);   // 频谱在下层，不遮挡 EQ 曲线
    drawCurve (g, area);
    drawHandles (g, area);
}

void EqCurveEditor::resized()
{
}

//==============================================================================
// 交互
//==============================================================================

int EqCurveEditor::hitTestBand (juce::Point<float> pos) const
{
    int best = -1;
    float bestDist = 1.0e9f;

    for (int i = 0; i < kNumBands; ++i)
    {
        const auto& b = bands[(size_t) i];
        const auto p = toScreen (b.freq, b.gainDb);
        const float d = p.getDistanceFrom (pos);

        // 命中半径适当放宽，鼠标不必非常精确
        if (d < 14.0f && d < bestDist)
        {
            bestDist = d;
            best = i;
        }
    }

    return best;
}

void EqCurveEditor::setSelectedBand (int index, bool notify)
{
    const int clamped = (index >= 0 && index < kNumBands) ? index : -1;
    if (clamped == selectedBand)
        return;

    selectedBand = clamped;
    repaint();

    if (notify && onSelectionChanged)
        onSelectionChanged (selectedBand);
}

void EqCurveEditor::mouseDown (const juce::MouseEvent& e)
{
    const int hit = hitTestBand (e.position);

    // 右键：弹出该段的功能菜单
    if (e.mods.isPopupMenu())
    {
        if (hit < 0)
            return;

        const auto& b = bands[(size_t) hit];
        const bool isOn = b.enabled;

        juce::PopupMenu m;
        m.addSectionHeader ("EQ Band " + juce::String (hit + 1) + "  ·  " + b.name);

        m.addItem (1, "Enabled", true, isOn);

        juce::PopupMenu shapes;
        shapes.addItem (10, "Bell (Peak)", true, b.shape == Shape::Peak);
        shapes.addItem (11, "Low Shelf", true, b.shape == Shape::LowShelf);
        shapes.addItem (12, "High Shelf", true, b.shape == Shape::HighShelf);
        shapes.addItem (13, "High Pass", true, b.shape == Shape::HighPass);
        shapes.addItem (14, "Low Pass", true, b.shape == Shape::LowPass);
        m.addSubMenu ("Filter Type", shapes);

        // 斜率只对高切/低切有意义（Peak/Shelf 的形状不靠级联改变）
        if (b.shape == Shape::HighPass || b.shape == Shape::LowPass)
        {
            juce::PopupMenu slopes;
            slopes.addItem (40, "12 dB/oct  (2nd order)", true, b.slopeDbPerOct == 12);
            slopes.addItem (41, "24 dB/oct  (4th order)", true, b.slopeDbPerOct == 24);
            slopes.addItem (42, "36 dB/oct  (6th order)", true, b.slopeDbPerOct == 36);
            slopes.addItem (43, "48 dB/oct  (8th order)", true, b.slopeDbPerOct == 48);
            m.addSubMenu ("Slope", slopes);
        }

        m.addSeparator();
        m.addItem (20, "Reset This Band");
        m.addItem (21, "Reset All Bands");
        m.addSeparator();
        m.addItem (30, "Flatten Gains (keep freq/Q)");

        const int bandIndex = hit;

        m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
            [this, bandIndex] (int result)
            {
                if (result == 0 || bandIndex < 0 || bandIndex >= kNumBands)
                    return;

                auto& bd = bands[(size_t) bandIndex];

                if (result == 1)
                {
                    bd.enabled = ! bd.enabled;
                    if (onBandEnableChanged) onBandEnableChanged (bandIndex, bd.enabled);
                }
                else if (result >= 10 && result <= 14)
                {
                    static const Shape shapes[] { Shape::Peak, Shape::LowShelf, Shape::HighShelf,
                                                  Shape::HighPass, Shape::LowPass };
                    bd.shape = shapes[result - 10];
                    // 改滤波器类型时自动启用，符合直觉
                    if (! bd.enabled)
                    {
                        bd.enabled = true;
                        if (onBandEnableChanged) onBandEnableChanged (bandIndex, true);
                    }
                    if (onBandChanged) onBandChanged (bandIndex, bd);
                }
                else if (result >= 40 && result <= 43)
                {
                    bd.slopeDbPerOct = (result - 40 + 1) * 12;
                    if (onBandChanged) onBandChanged (bandIndex, bd);
                }
                else if (result == 20)
                {
                    EqCurveEditor::BandState fresh;
                    fresh.freq = bd.freq;
                    fresh.name = bd.name;
                    fresh.shape = bd.shape;
                    fresh.enabled = bd.enabled;
                    bd.gainDb = 0.0f;
                    bd.q = 0.9f;
                    if (onBandChanged) onBandChanged (bandIndex, bd);
                }
                else if (result == 21)
                {
                    for (int i = 0; i < kNumBands; ++i)
                    {
                        bands[(size_t) i].gainDb = 0.0f;
                        bands[(size_t) i].q = 0.9f;
                        if (onBandChanged) onBandChanged (i, bands[(size_t) i]);
                    }
                }
                else if (result == 30)
                {
                    for (int i = 0; i < kNumBands; ++i)
                    {
                        bands[(size_t) i].gainDb = 0.0f;
                        if (onBandChanged) onBandChanged (i, bands[(size_t) i]);
                    }
                }

                repaint();
            });

        return;
    }

    if (hit < 0)
    {
        dragBand = -1;
        return;
    }

    dragBand = hit;
    hoverBand = hit;
    dragStartPos = e.position;
    dragStartState = bands[(size_t) hit];
    didDrag = false;

    // 点击节点即选中它 —— 下方旋钮会切到这一段
    setSelectedBand (hit);

    // 拖动一个未启用的段时自动启用它 —— 用户显然想用它
    if (! bands[(size_t) hit].enabled)
    {
        bands[(size_t) hit].enabled = true;
        if (onBandEnableChanged) onBandEnableChanged (hit, true);
    }

    repaint();
}

void EqCurveEditor::mouseDrag (const juce::MouseEvent& e)
{
    if (dragBand < 0 || dragBand >= kNumBands)
        return;

    const auto area = getPlotArea();
    if (area.getWidth() <= 1.0f || area.getHeight() <= 1.0f)
        return;

    auto& b = bands[(size_t) dragBand];

    // --- 频率：横向位置直接映射（绝对定位，符合"直接拖动"的直觉）
    const float nx = juce::jlimit (0.0f, 1.0f, (e.position.x - area.getX()) / area.getWidth());
    float newFreq = normToFreq (nx);

    // --- 增益：纵向位置
    const float ny = juce::jlimit (0.0f, 1.0f, (e.position.y - area.getY()) / area.getHeight());
    float newGain = normToGain (ny);

    // HP / LP 没有增益概念，拖动只改频率（Q 用滚轮）
    if (b.shape == Shape::HighPass || b.shape == Shape::LowPass)
        newGain = 0.0f;

    // 频率量化到易读值，避免出现 1234.7Hz 这种数字
    if (newFreq < 100.0f)
        newFreq = std::round (newFreq * 10.0f) / 10.0f;
    else if (newFreq < 1000.0f)
        newFreq = std::round (newFreq);
    else
        newFreq = std::round (newFreq / 10.0f) * 10.0f;

    // Alt 精细调节：增益按 0.5dB 步进
    if (e.mods.isAltDown())
        newGain = std::round (newGain * 2.0f) / 2.0f;

    b.freq = juce::jlimit (kMinFreq, kMaxFreq, newFreq);
    b.gainDb = juce::jlimit (-kMaxGain, kMaxGain, newGain);

    didDrag = true;

    if (onBandChanged) onBandChanged (dragBand, b);
    repaint();
}

void EqCurveEditor::mouseUp (const juce::MouseEvent&)
{
    dragBand = -1;
    didDrag = false;
    repaint();
}

void EqCurveEditor::mouseMove (const juce::MouseEvent& e)
{
    const int hit = hitTestBand (e.position);
    if (hit != hoverBand)
    {
        hoverBand = hit;
        repaint();
    }
}

void EqCurveEditor::mouseExit (const juce::MouseEvent&)
{
    if (hoverBand >= 0)
    {
        hoverBand = -1;
        repaint();
    }
}

void EqCurveEditor::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int hit = hitTestBand (e.position);
    if (hit < 0)
        return;

    // 双击把该段增益归零（快速回到"不改变音色"）
    auto& b = bands[(size_t) hit];
    b.gainDb = 0.0f;

    if (onBandChanged) onBandChanged (hit, b);
    repaint();
}

void EqCurveEditor::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel)
{
    int target = dragBand;

    if (target < 0)
        target = hitTestBand (e.position);

    // 没命中任何手柄时：Shift 滚轮调整悬停不到的段就不处理
    if (target < 0 || target >= kNumBands)
        return;

    auto& b = bands[(size_t) target];

    const float delta = wheel.deltaY;

    if (e.mods.isShiftDown())
    {
        // Shift + 滚轮：微调增益
        b.gainDb = juce::jlimit (-kMaxGain, kMaxGain, b.gainDb + delta * 3.0f);
    }
    else
    {
        // 滚轮：调 Q（对数手感）
        const float factor = std::pow (1.35f, delta * 2.0f);
        b.q = juce::jlimit (0.1f, 12.0f, b.q * factor);
    }

    if (onBandChanged) onBandChanged (target, b);
    repaint();
}

} // namespace kf
