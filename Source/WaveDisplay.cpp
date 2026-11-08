#include "WaveDisplay.h"
#include "Theme.h"

namespace kf
{

WaveDisplay::WaveDisplay()
{
    // 每层一个区分度高的颜色：Sub 用暖橙，Click 三层用青/绿/紫
    // 4 条曲线各一个主题色（粉 / 蓝 / 紫 / 薄荷），见 Theme.h 的 waveSource()
    for (int i = 0; i < kNumSources; ++i)
        sources[(size_t) i].colour = theme::waveSource (i);

    for (auto& s : sources)
    {
        s.minValues.assign ((size_t) kResolution, 0.0f);
        s.maxValues.assign ((size_t) kResolution, 0.0f);
    }

    setInterceptsMouseClicks (false, false);   // 显示器不抢鼠标事件
}

WaveDisplay::~WaveDisplay() = default;

void WaveDisplay::captureSource (int sourceIndex, const float* samples, int numSamples)
{
    if (sourceIndex < 0 || sourceIndex >= kNumSources || samples == nullptr || numSamples <= 0)
        return;

    // 把 numSamples 个采样点压成 kResolution 个 (min,max) 对。
    // 这在音频线程调用，所以只用短锁 + 朴素的循环，不分配内存。
    const juce::SpinLock::ScopedLockType sl (lock);

    auto& s = sources[(size_t) sourceIndex];
    const float step = (float) numSamples / (float) kResolution;

    for (int i = 0; i < kResolution; ++i)
    {
        const int start = (int) ((float) i * step);
        const int end   = juce::jmin (numSamples, (int) ((float) (i + 1) * step));
        if (start >= end)
        {
            s.minValues[(size_t) i] = s.maxValues[(size_t) i] = 0.0f;
            continue;
        }

        float mn = samples[start], mx = samples[start];
        for (int k = start + 1; k < end; ++k)
        {
            mn = juce::jmin (mn, samples[k]);
            mx = juce::jmax (mx, samples[k]);
        }
        s.minValues[(size_t) i] = mn;
        s.maxValues[(size_t) i] = mx;
    }

    s.hasData = true;
}

void WaveDisplay::clearWaveforms()
{
    const juce::SpinLock::ScopedLockType sl (lock);
    for (auto& s : sources)
    {
        std::fill (s.minValues.begin(), s.minValues.end(), 0.0f);
        std::fill (s.maxValues.begin(), s.maxValues.end(), 0.0f);
        s.hasData = false;
    }
    repaint();
}

void WaveDisplay::setSourceVisible (int sourceIndex, bool visible)
{
    if (sourceIndex < 0 || sourceIndex >= kNumSources)
        return;
    sources[(size_t) sourceIndex].visible = visible;
    repaint();
}

bool WaveDisplay::isSourceVisible (int sourceIndex) const
{
    if (sourceIndex < 0 || sourceIndex >= kNumSources)
        return false;
    return sources[(size_t) sourceIndex].visible;
}

void WaveDisplay::setSourceColour (int sourceIndex, juce::Colour c)
{
    if (sourceIndex < 0 || sourceIndex >= kNumSources)
        return;
    sources[(size_t) sourceIndex].colour = c;
    repaint();
}

void WaveDisplay::resized()
{
}

void WaveDisplay::drawGrid (juce::Graphics& g, juce::Rectangle<float> area)
{
    g.setColour (theme::outline);
    g.drawRect (area, 1.0f);

    // 中线（零轴）
    g.setColour (theme::outlineSoft);
    g.drawHorizontalLine ((int) area.getCentreY(), area.getX(), area.getRight());

    // 时间网格：4 等分
    for (int i = 1; i < 4; ++i)
    {
        const float x = area.getX() + area.getWidth() * (float) i / 4.0f;
        g.drawVerticalLine ((int) x, area.getY(), area.getBottom());
    }

    // 纵向 4 等分
    for (int i = 1; i < 4; ++i)
    {
        const float y = area.getY() + area.getHeight() * (float) i / 4.0f;
        g.drawHorizontalLine ((int) y, area.getX(), area.getRight());
    }
}

void WaveDisplay::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // 背景
    g.setColour (theme::panelDeep);
    g.fillRoundedRectangle (bounds, 4.0f);

    auto area = bounds.reduced (2.0f);
    drawGrid (g, area);

    const juce::SpinLock::ScopedLockType sl (lock);

    // 每层都归一化到同样的纵向尺度，这样叠加时能直观看出各层的相对幅度与相位
    float globalPeak = 1.0e-6f;
    for (const auto& s : sources)
    {
        if (! s.visible || ! s.hasData)
            continue;
        for (int i = 0; i < kResolution; ++i)
        {
            globalPeak = juce::jmax (globalPeak, std::abs (s.minValues[(size_t) i]));
            globalPeak = juce::jmax (globalPeak, std::abs (s.maxValues[(size_t) i]));
        }
    }

    const float centreY = area.getCentreY();
    const float halfH   = area.getHeight() * 0.46f;
    const float scale   = halfH / globalPeak;

    for (int si = 0; si < kNumSources; ++si)
    {
        const auto& s = sources[(size_t) si];
        if (! s.visible || ! s.hasData)
            continue;

        juce::Path path;

        // 上包络
        for (int i = 0; i < kResolution; ++i)
        {
            const float x = area.getX() + area.getWidth() * (float) i / (float) (kResolution - 1);
            const float y = centreY - s.maxValues[(size_t) i] * scale;
            if (i == 0) path.startNewSubPath (x, y);
            else        path.lineTo (x, y);
        }

        // 下包络回描，形成封闭轮廓
        for (int i = kResolution - 1; i >= 0; --i)
        {
            const float x = area.getX() + area.getWidth() * (float) i / (float) (kResolution - 1);
            const float y = centreY - s.minValues[(size_t) i] * scale;
            path.lineTo (x, y);
        }
        path.closeSubPath();

        // 填充半透明 + 描边，叠加时层次清楚
        g.setColour (s.colour.withAlpha (0.28f));
        g.fillPath (path);

        g.setColour (s.colour.withAlpha (0.95f));
        g.strokePath (path, juce::PathStrokeType (1.0f));
    }

    // 图例
    g.setFont (juce::Font (juce::FontOptions (10.0f)));
    const char* labels[kNumSources] = { "SUB", "C1", "C2", "C3" };
    float lx = area.getX() + 6.0f;
    for (int si = 0; si < kNumSources; ++si)
    {
        const auto& s = sources[(size_t) si];
        g.setColour (s.visible ? s.colour : s.colour.withAlpha (0.25f));
        g.drawText (labels[si], juce::Rectangle<float> (lx, area.getY() + 4.0f, 26.0f, 12.0f),
                    juce::Justification::centredLeft, false);
        lx += 28.0f;
    }

    if (globalPeak <= 1.0001e-6f)
    {
        g.setColour (theme::textFaint);
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawText ("play a note to capture waveforms",
                    area, juce::Justification::centred, false);
    }
}

} // namespace kf
