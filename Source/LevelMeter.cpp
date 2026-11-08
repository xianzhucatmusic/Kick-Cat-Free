#include "LevelMeter.h"
#include "Theme.h"
#include <juce_audio_basics/juce_audio_basics.h>

namespace kf
{

LevelMeter::LevelMeter()
{
    setInterceptsMouseClicks (true, false);
    startTimerHz (30);
}

LevelMeter::~LevelMeter()
{
    stopTimer();
}

void LevelMeter::pushPeak (float linearPeak) noexcept
{
    // 只保留最大值，UI 每帧取一次
    float cur = incomingPeak.load (std::memory_order_relaxed);
    while (linearPeak > cur
           && ! incomingPeak.compare_exchange_weak (cur, linearPeak,
                                                    std::memory_order_relaxed))
    {
    }
}

void LevelMeter::pushGainReduction (float grDb) noexcept
{
    // GR 取更负的值（衰减更多）
    float cur = incomingGr.load (std::memory_order_relaxed);
    while (grDb < cur
           && ! incomingGr.compare_exchange_weak (cur, grDb,
                                                  std::memory_order_relaxed))
    {
    }
}

void LevelMeter::resetPeakHold()
{
    peakHold = 0.0f;
    holdCounter = 0;
    repaint();
}

void LevelMeter::timerCallback()
{
    const float newPeak = incomingPeak.exchange (0.0f, std::memory_order_relaxed);
    const float newGr = incomingGr.exchange (0.0f, std::memory_order_relaxed);

    // 电平：上升快、下降慢（标准表头手感）
    if (newPeak > displayPeak)
        displayPeak = newPeak;
    else
        displayPeak *= 0.80f;

    // 峰值保持：保持约 1 秒后缓慢下落
    if (displayPeak >= peakHold)
    {
        peakHold = displayPeak;
        holdCounter = 30;   // 30 帧 @30fps ≈ 1 秒
    }
    else if (holdCounter > 0)
    {
        --holdCounter;
    }
    else
    {
        peakHold *= 0.94f;
        if (peakHold < 1.0e-5f)
            peakHold = 0.0f;
    }

    // GR：跟随较快
    displayGr = juce::jmin (displayGr * 0.7f, newGr);
    if (newGr < displayGr)
        displayGr = newGr;

    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();

    // 左侧留出刻度数字
    auto scaleArea = bounds.removeFromLeft (24.0f);

    // --- 背景
    g.setColour (theme::panelDeep);
    g.fillRoundedRectangle (bounds, 3.0f);
    g.setColour (theme::outline);
    g.drawRoundedRectangle (bounds, 3.0f, 1.0f);

    auto barArea = bounds.reduced (2.0f);

    // --- 电平条（从下往上）
    const float peakDb = juce::Decibels::gainToDecibels (displayPeak, kMinDb);
    const float holdDb = juce::Decibels::gainToDecibels (peakHold, kMinDb);

    const float norm = dbToNorm (peakDb);
    const float barH = barArea.getHeight() * norm;

    if (barH > 0.5f)
    {
        juce::Rectangle<float> bar (barArea.getX(), barArea.getBottom() - barH,
                                    barArea.getWidth(), barH);

        // 绿 -> 黄 -> 红 的渐变（标准电平表配色）
        juce::ColourGradient grad (theme::meterHigh, barArea.getX(), barArea.getY(),
                                   theme::orangeBright, barArea.getX(), barArea.getBottom(), false);
        grad.addColour (0.30, theme::orange);
        grad.addColour (0.62, theme::orangeBright);
        g.setGradientFill (grad);
        g.fillRect (bar);
    }

    // --- 峰值保持线
    if (holdDb > kMinDb)
    {
        const float y = barArea.getBottom() - barArea.getHeight() * dbToNorm (holdDb);
        g.setColour (theme::white);
        g.fillRect (barArea.getX(), y - 1.0f, barArea.getWidth(), 2.0f);
    }

    // --- 限幅天花板红线
    {
        const float y = barArea.getBottom() - barArea.getHeight() * dbToNorm (kCeilingDb);
        g.setColour (theme::meterHigh.withAlpha (0.85f));
        g.fillRect (barArea.getX(), y, barArea.getWidth(), 1.0f);
    }

    // --- 增益衰减条（从顶部往下，橙色）
    if (displayGr < -0.05f)
    {
        const float grNorm = juce::jlimit (0.0f, 1.0f, -displayGr / 24.0f);
        const float grH = barArea.getHeight() * grNorm;

        g.setColour (theme::cyan.withAlpha (0.75f));
        g.fillRect (barArea.getX(), barArea.getY(), barArea.getWidth() * 0.42f, grH);
    }

    // --- 刻度
    g.setFont (juce::Font (juce::FontOptions (8.0f)));
    static const float ticks[] { 0.0f, -6.0f, -12.0f, -24.0f, -48.0f };
    for (float t : ticks)
    {
        const float y = barArea.getBottom() - barArea.getHeight() * dbToNorm (t);
        g.setColour (theme::textFaint);
        g.drawText (juce::String ((int) t),
                    juce::Rectangle<float> (scaleArea.getX(), y - 5.0f, scaleArea.getWidth() - 3.0f, 10.0f),
                    juce::Justification::centredRight, false);
    }

    // --- 标题
    g.setColour (theme::textDim);
    g.setFont (juce::Font (juce::FontOptions (8.5f).withStyle ("Bold")));
    g.drawText ("OUT", scaleArea.getX(), scaleArea.getY() - 1.0f,
                scaleArea.getWidth(), 10.0f, juce::Justification::centredRight, false);
}

} // namespace kf
