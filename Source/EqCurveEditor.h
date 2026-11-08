#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <array>
#include <functional>

namespace kf
{

//==============================================================================
/** 可交互的 8 段 EQ 曲线编辑器。
 *
 *  视觉与交互参考专业插件（Pro-Q / 弦烛之声 Master EQ）：
 *   - 对数频率轴 20Hz..20kHz，线性增益轴 ±18dB
 *   - 实线绘制合成后的总频响曲线，曲线下方填充半透明渐变
 *   - 每段一个圆点手柄：拖动改「频率 + 增益」
 *   - 滚轮改 Q；Shift+滚轮改增益（精细）；右键菜单切滤波器类型 / 重置 / 开关
 *   - 悬停高亮 + 数值气泡，双击重置该段
 *
 *  组件本身不持有 DSP，只通过回调通知外部把改动写进参数。
 */
class EqCurveEditor : public juce::Component
{
public:
    static constexpr int kNumBands = 8;

    /** 滤波器类型（与 Equalizer::BandType 对应，但这里是 UI 层枚举，避免头文件互相依赖）。 */
    enum class Shape { Peak = 0, LowShelf, HighShelf, HighPass, LowPass };

    struct BandState
    {
        float freq = 100.0f;
        float gainDb = 0.0f;
        float q = 0.9f;
        Shape shape = Shape::Peak;
        bool enabled = false;
        /** 高切/低切的斜率（dB/oct）：12 / 24 / 36 / 48。其它类型忽略。 */
        int slopeDbPerOct = 12;
        /** 该段的显示名（如 "Punch"），仅用于 UI。 */
        juce::String name;
    };

    EqCurveEditor();

    /** 设置某段的显示状态（由外部在参数变化时调用）。 */
    void setBand (int index, const BandState& state);
    const BandState& getBand (int index) const;

    /** 用户拖动后触发：参数 index 有新值。 */
    std::function<void (int bandIndex, const BandState& newState)> onBandChanged;

    /** 选中的频点变化时回调（点击节点或双击）。
     *  编辑器用它切换下方显示哪一段的旋钮。 */
    std::function<void (int bandIndex)> onSelectionChanged;

    /** 当前选中的频点（-1 = 未选中）。 */
    int getSelectedBand() const noexcept { return selectedBand; }

    /** 以程序方式选中某段（不触发回调）。 */
    void setSelectedBand (int index, bool notify = true);

    /** 用户请求开关某段（右键菜单）。 */
    std::function<void (int bandIndex, bool enabled)> onBandEnableChanged;

    /** 取得某个频率处的总响应（dB），用于绘制曲线。 */
    float getTotalResponseDb (float frequency) const;

    void paint (juce::Graphics& g) override;

    /** 绘制实时频谱（内部调用）。 */
    void drawSpectrum (juce::Graphics& g, juce::Rectangle<float> area);

    //==========================================================================
    // 频谱显示
    //
    // 把实时频谱叠加在 EQ 曲线上 —— 这样调 EQ 时能直接看到"要处理的那块"
    // 在频谱上的位置，不用靠猜。
    //==========================================================================

    /** 处理器每次抓完波形后调用：传入单声道样本，内部做 FFT。 */
    void pushSpectrum (const float* samples, int numSamples);

    /** 频谱是否显示。 */
    bool spectrumVisible = true;

    /** 频谱平滑：新旧帧的混合比例（0.8 = 偏重历史，观感更稳）。 */
    float spectrumSmoothing = 0.75f;

    /** 频谱分析用的采样率（由编辑器在 prepareToPlay 后设置）。 */
    double sampleRateForSpectrum = 44100.0;

    void setSampleRateForSpectrum (double sr) { sampleRateForSpectrum = sr; }

    /** 是否已经收到过频谱数据（没收到就不画，避免空白区域）。 */
    bool hasSpectrum() const { return spectrumPrimed; }

    static constexpr int kFftOrder = 11;               // 2048 点
    static constexpr int kFftSize  = 1 << kFftOrder;
    static constexpr int kNumBins  = kFftSize / 2;

    juce::dsp::FFT fft { kFftOrder };
    juce::dsp::WindowingFunction<float> window {
        (size_t) kFftSize, juce::dsp::WindowingFunction<float>::hann };

    std::array<float, kFftSize * 2> fftData {};
    std::array<float, kNumBins> spectrumDb {};         // 平滑后的 dB 值
    bool spectrumPrimed = false;
    int  spectrumAge = 0;

    /** 当前选中的频点，用于高亮节点并驱动下方旋钮。 */
    int selectedBand = -1;

public:
    void resized() override;

    void mouseDown (const juce::MouseEvent& e) override;
    void mouseDrag (const juce::MouseEvent& e) override;
    void mouseUp (const juce::MouseEvent& e) override;
    void mouseMove (const juce::MouseEvent& e) override;
    void mouseExit (const juce::MouseEvent& e) override;
    void mouseDoubleClick (const juce::MouseEvent& e) override;
    void mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& wheel) override;

private:
    /** 频率 -> 横向比例（0..1），对数轴。 */
    static float freqToNorm (float freq) noexcept;
    static float normToFreq (float norm) noexcept;
    /** 增益 -> 纵向比例（0..1，0.5 为 0dB）。 */
    static float gainToNorm (float gainDb) noexcept;
    static float normToGain (float norm) noexcept;

    juce::Rectangle<float> getPlotArea() const;
    juce::Point<float> toScreen (float freq, float gainDb) const;

    /** 命中测试：返回最近的段索引，-1 表示没命中。 */
    int hitTestBand (juce::Point<float> pos) const;

    /** 单段的幅度响应（dB）。 */
    float bandResponseDb (const BandState& b, float frequency) const;

    void drawGrid (juce::Graphics& g, juce::Rectangle<float> area);
    void drawCurve (juce::Graphics& g, juce::Rectangle<float> area);
    void drawHandles (juce::Graphics& g, juce::Rectangle<float> area);

    std::array<BandState, kNumBands> bands;

    int dragBand = -1;
    int hoverBand = -1;
    juce::Point<float> dragStartPos;
    BandState dragStartState;
    bool didDrag = false;

    juce::Colour accent { 0xff4fc3d9 };

    /** 每段的配色：低频暖色 -> 高频冷色，便于快速区分。 */
    static juce::Colour colourForBand (int index);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (EqCurveEditor)
};

} // namespace kf
