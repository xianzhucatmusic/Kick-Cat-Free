#pragma once

#include <juce_graphics/juce_graphics.h>

namespace kf
{

//==============================================================================
/** Kick Cat 主题：灰色扁平 + 青/橙双强调（Alchemy 风格）。
 *
 *  设计语言（对照参考图提炼，不照抄）：
 *
 *  1. **两级灰面板**：窗口底最深，每个功能模块是一块略亮的浅灰面板，
 *     靠 1px 边框分隔。层次全部由亮度差表达，不用阴影、不用渐变。
 *
 *  2. **青色主强调 (#4FC3E0)**：用在"已经激活"和"主要交互"上 ——
 *     开启的开关、当前页签、包络曲线、EQ 曲线。满屏灰里只有它是亮的。
 *
 *  3. **橙色次强调 (#E8A33D)**：专用于**频率类**参数（Cutoff / Freq / Tune），
 *     这样一眼能区分"这是调频率的"还是"这是调音量的"。
 *     这是参考图里最值得学的细节：颜色承载语义，而不只是装饰。
 *
 *  4. **旋钮**：深灰圆底 + 细环轨道 + 彩色值弧 + 白色指针线。
 *     值弧从左下(7点)扫到右下(5点)，符合硬件旋钮的直觉。
 *
 *  5. **控件**：小圆角(3px)胶囊或方角，细边框，无渐变、无阴影。
 */
namespace theme
{
    //==========================================================================
    // 灰阶：直接从参考图采样得到的值
    //
    // 关键特征（与我上一版凭感觉调的区别）：
    //   - 灰是**冷灰**（带蓝），不是中性灰。这ne是"现代感"的主要来源。
    //   - 层次是「深面板 + 亮控件」：面板底 #202A32，旋钮盘反而更亮 #333F47。
    //   - 工具栏/页签条明显更亮 #585B61，作为区段分隔。

    /** L0 窗口最外底（参考图接近纯黑） */
    const juce::Colour g0 { 0xff15181c };
    /** L1 模块面板底 */
    const juce::Colour g1 { 0xff202a32 };
    /** L2 面板内嵌区 / 标题条 */
    const juce::Colour g2 { 0xff262f38 };
    /** L3 控件底（按钮、下拉、旋钮盘） */
    const juce::Colour g3 { 0xff333f47 };
    /** L4 控件悬停 / 亮的旋钮盘 */
    const juce::Colour g4 { 0xff4a555d };
    /** L5 工具栏 / 页签条（明显亮一档，用于区段分隔） */
    const juce::Colour g5 { 0xff585b61 };
    /** L6 次级文字 */
    const juce::Colour g6 { 0xff8a949c };
    /** L7 主文字 */
    const juce::Colour g7 { 0xffc8cfd6 };
    /** L8 高亮文字 */
    const juce::Colour g8 { 0xffeef2f5 };

    //==========================================================================
    // 语义别名

    const juce::Colour bg          = g0;   // 窗口底
    const juce::Colour panel       = g1;   // 模块面板
    const juce::Colour panelDeep   = g2;   // 面板内嵌区
    const juce::Colour control     = g3;   // 控件底
    const juce::Colour controlHover= g4;   // 控件悬停
    const juce::Colour outline     = g5;   // 描边
    const juce::Colour outlineSoft = g4;   // 弱描边
    const juce::Colour grid        = g3;   // 网格线

    const juce::Colour text        = g7;   // 主文字
    const juce::Colour textDim     = g6;   // 次级文字
    const juce::Colour textFaint   = g5;   // 刻度、提示
    const juce::Colour white       = g8;   // 高亮

    const juce::Colour disabled    = g4;   // 禁用态
    /** 旋钮轨道底（比面板深，形成凹陷） */
    const juce::Colour knobTrack   = juce::Colour (0xff18222a);

    //==========================================================================
    // 强调色

    /** 主强调：青。采样自参考图的选中页签/激活开关。 */
    const juce::Colour cyan      { 0xff0b9bc1 };
    /** 青色亮版：悬停、高光。 */
    const juce::Colour cyanBright{ 0xff29b0d4 };
    /** 青色暗版：描边、次级。 */
    const juce::Colour cyanDeep  { 0xff0a7d9c };

    /** 次强调：橙。专用于**频率类**参数（参考图里 Cutoff 用橙）。 */
    // 警示色：只在错误提示里用（AI 面板的失败消息）。
    // 用偏暖的红而不是纯红 —— 与整体的灰蓝调性不冲突。
    const juce::Colour error { 0xffd4574e };

    const juce::Colour orange    { 0xffe09a30 };
    /** 橙色亮版。 */
    const juce::Colour orangeBright { 0xfff0b050 };
    /** 橙色暗版。 */
    const juce::Colour orangeDeep { 0xffb07820 };

    /** 主强调色（= 青）。 */
    const juce::Colour accent    { 0xff0b9bc1 };
    const juce::Colour accentCool = cyan;

    //==========================================================================
    // 功能分区色
    //
    // 参考图里每个大模块用不同颜色区分。这里保持青/橙为主，
    // 只给"切"类功能（EQ）和动态处理各一个颜色，避免满屏花。

    inline juce::Colour sectionSub()   { return cyan; }      // Sub 振荡器
    inline juce::Colour sectionClick() { return orange; }    // Click 层
    inline juce::Colour sectionEq()    { return cyan; }      // EQ
    inline juce::Colour sectionDyn()   { return orange; }    // 动态
    inline juce::Colour sectionEnv()   { return cyan; }      // 包络
    inline juce::Colour sectionWave()  { return cyan; }      // 波形

    /** 4 个 Click 层的颜色：在青/橙之间插值，既区分又统一。 */
    inline juce::Colour clickColour (int layer)
    {
        static const juce::Colour cols[4]
        {
            juce::Colour (0xffe09a30),   // 橙
            juce::Colour (0xffd8c04a),   // 黄
            juce::Colour (0xff5fc0a8),   // 青绿
            juce::Colour (0xff0b9bc1)    // 青
        };
        return cols[juce::jlimit (0, 3, layer)];
    }

    /** EQ 8 段：青色系内微调，保持整体不花。 */
    inline juce::Colour eqBand (int index)
    {
        static const juce::Colour cols[8]
        {
            juce::Colour (0xff0b9bc1), juce::Colour (0xff12a3c8),
            juce::Colour (0xff19abcf), juce::Colour (0xff20b3d6),
            juce::Colour (0xff27bbdd), juce::Colour (0xff2ec3e4),
            juce::Colour (0xff35cbeb), juce::Colour (0xff3cd3f2)
        };
        return cols[juce::jlimit (0, 7, index)];
    }

    /** 波形显示的 5 条曲线（Sub + 4 个 Click 层）。 */
    inline juce::Colour waveSource (int index)
    {
        if (index == 0) return cyan;
        return clickColour (index - 1);
    }

    /** 电平表（功能性渐变，灰底上唯一允许的例外）。 */
    const juce::Colour meterLow  { 0xff5fb87a };
    const juce::Colour meterMid  { 0xffd4c04a };
    const juce::Colour meterHigh { 0xffe8684a };

    //==========================================================================
    // 圆角

    /** 面板圆角 */
    constexpr float panelRadius   = 4.0f;
    /** 控件圆角 */
    constexpr float controlRadius = 4.0f;
}

} // namespace kf
