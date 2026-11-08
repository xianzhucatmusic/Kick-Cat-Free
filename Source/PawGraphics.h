#pragma once

#include <juce_graphics/juce_graphics.h>
#include "Theme.h"

namespace kf
{

//==============================================================================
/** 猫爪图形绘制工具。
 *
 *  一个猫爪 = 1 个大肉垫（掌心）+ 4 个小脚趾，全部用椭圆画。
 *  这些函数是纯绘制、无状态，可以画在任意位置和尺寸：
 *   - drawPaw       完整猫爪（标题、背景装饰、开关）
 *   - drawPawPrints 一串爪印（背景装饰，斜向排布）
 *   - drawPawIcon   极简版（用在很小的按钮上）
 */
namespace paw
{

/** 画一个猫爪。
 *  @param bounds  爪子占据的正方形区域
 *  @param colour  肉垫颜色
 *  @param outlineColour 轮廓色（透明则不画轮廓）
 *  @param detail  是否画高光细节（小尺寸时关掉，避免糊成一团） */
inline void drawPaw (juce::Graphics& g,
                     juce::Rectangle<float> bounds,
                     juce::Colour colour,
                     juce::Colour outlineColour = juce::Colours::transparentBlack,
                     bool detail = true)
{
    const float w = bounds.getWidth();
    const float h = bounds.getHeight();
    const float s = juce::jmin (w, h);
    if (s < 3.0f)
        return;

    const float cx = bounds.getCentreX();
    const float cy = bounds.getCentreY();

    // 手掌肉垫：偏下的一个大椭圆
    const float padW = s * 0.58f;
    const float padH = s * 0.50f;
    const juce::Rectangle<float> pad (cx - padW * 0.5f,
                                      cy + s * 0.06f,
                                      padW, padH);

    // 4 个脚趾：沿上方弧线排布，中间两个略大
    struct Toe { float dx, dy, r; };
    const Toe toes[4]
    {
        { -0.30f, -0.26f, 0.115f },
        { -0.11f, -0.36f, 0.130f },
        {  0.11f, -0.36f, 0.130f },
        {  0.30f, -0.26f, 0.115f }
    };

    auto drawAll = [&] (float grow)
    {
        // 脚趾
        for (const auto& t : toes)
        {
            const float r = s * t.r + grow;
            g.fillEllipse (cx + s * t.dx - r, cy + s * t.dy - r, r * 2.0f, r * 2.0f);
        }
        // 手掌
        g.fillEllipse (pad.expanded (grow));
    };

    // 轮廓：先画放大的深色形状，再画正常大小的亮色形状，形成描边效果
    if (! outlineColour.isTransparent())
    {
        g.setColour (outlineColour);
        drawAll (juce::jmax (0.8f, s * 0.045f));
    }

    g.setColour (colour);
    drawAll (0.0f);

    // 高光：让爪子显得饱满有光泽
    if (detail && s > 14.0f)
    {
        g.setColour (juce::Colours::white.withAlpha (0.22f));
        g.fillEllipse (pad.getX() + pad.getWidth() * 0.18f,
                       pad.getY() + pad.getHeight() * 0.14f,
                       pad.getWidth() * 0.34f,
                       pad.getHeight() * 0.28f);
    }
}

/** 画一串猫爪印（背景装饰）。斜向从左上到右下排布。 */
inline void drawPawPrints (juce::Graphics& g,
                           juce::Rectangle<float> area,
                           int count,
                           float pawSize,
                           juce::Colour colour)
{
    if (count <= 0 || pawSize < 2.0f)
        return;

    for (int i = 0; i < count; ++i)
    {
        // 沿对角线均匀分布
        const float t = (count > 1) ? (float) i / (float) (count - 1) : 0.0f;
        const float x = area.getX() + area.getWidth() * (0.08f + t * 0.84f);
        const float y = area.getY() + area.getHeight() * (0.18f + t * 0.64f);

        // 爪印略微旋转，显得自然
        juce::Graphics::ScopedSaveState save (g);
        g.addTransform (juce::AffineTransform::rotation (
            juce::MathConstants<float>::pi * 0.22f, x, y));

        drawPaw (g,
                 juce::Rectangle<float> (x - pawSize * 0.5f, y - pawSize * 0.5f, pawSize, pawSize),
                 colour,
                 juce::Colours::transparentBlack,
                 false);
    }
}

/** 极简猫爪图标：只画轮廓，用于小按钮。 */
inline void drawPawIcon (juce::Graphics& g,
                         juce::Rectangle<float> bounds,
                         juce::Colour colour)
{
    const float s = juce::jmin (bounds.getWidth(), bounds.getHeight());
    if (s < 4.0f)
        return;

    const float cx = bounds.getCentreX();
    const float cy = bounds.getCentreY();

    g.setColour (colour);

    // 手掌
    g.fillEllipse (cx - s * 0.26f, cy - s * 0.04f, s * 0.52f, s * 0.44f);

    // 4 个脚趾
    const float ry = s * 0.10f;
    g.fillEllipse (cx - s * 0.40f, cy - s * 0.32f, ry * 2.1f, ry * 2.1f);
    g.fillEllipse (cx - s * 0.16f, cy - s * 0.44f, ry * 2.4f, ry * 2.4f);
    g.fillEllipse (cx + s * 0.16f - ry * 2.4f, cy - s * 0.44f, ry * 2.4f, ry * 2.4f);
    g.fillEllipse (cx + s * 0.40f - ry * 2.1f, cy - s * 0.32f, ry * 2.1f, ry * 2.1f);
}

/** 猫耳朵形状（用于装饰标题两侧）。 */
inline juce::Path makeEar (float cx, float cy, float size, bool flip)
{
    juce::Path p;
    const float dir = flip ? -1.0f : 1.0f;

    p.startNewSubPath (cx, cy);
    p.lineTo (cx + dir * size * 0.42f, cy - size * 0.72f);
    p.quadraticTo (cx + dir * size * 0.55f, cy - size * 0.30f,
                   cx + dir * size * 0.62f, cy + size * 0.10f);
    p.closeSubPath();

    return p;
}

} // namespace paw
} // namespace kf
