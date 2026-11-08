#pragma once

#include <juce_dsp/juce_dsp.h>

namespace kf
{

//==============================================================================
    /** 8 段参数均衡，频段针对底鼓/贝斯优化。
     *
     *  默认中心频率（Hz）与用途：
     *    1)   20  Sub rumble    —— 清理次低频 / 增强极低频体感
     *    2)   40  Sub body      —— 底鼓"下潜"的核心区
     *    3)   70  Punch         —— 胸口冲击力
     *    4)  120  Fundamental   —— 基频区，决定音高感
     *    5)  250  Mud           —— 浑浊区，通常要减
     *    6)  600  Box           —— 纸盒声区
     *    7) 2500  Attack        —— 点击/击打感
     *    8) 8000  Click air     —— 采样 Click 的清脆度
     */
class Equalizer
{
public:
    Equalizer() = default;

    static constexpr int kNumBands = 8;

    /** 单个双二阶最多级联的级数（4 级 = 48 dB/oct）。 */
    static constexpr int kMaxStages = 4;

    /** 每段的默认中心频率。 */
    static const std::array<float, kNumBands>& defaultFrequencies() noexcept;

    void prepare (const juce::dsp::ProcessSpec& spec);
    void reset();

    /** 滤波器类型。 */
    enum class BandType { Peak = 0, LowShelf, HighShelf, HighPass, LowPass };

    /** 更新某一段的参数。
     *  @param slopeDbPerOct  仅 HighPass/LowPass 有效：12/24/36/48 dB/oct。 */
    void setBandParams (int band, float freq, float gainDb, float q, BandType type,
                        bool enabled, int slopeDbPerOct = 12);

    void process (juce::AudioBuffer<float>& buffer, int startSample, int numSamples);

    /** 计算某频点的总增益（dB），供 UI 画曲线。 */
    float getResponseDb (float frequency) const;

    struct BandSettings
    {
        float freq = 100.0f;
        float gainDb = 0.0f;
        float q = 1.0f;
        BandType type = BandType::Peak;
        bool enabled = false;

        /** 高切/低切的斜率，单位 dB/oct。
         *  只有 HighPass / LowPass 用得到：12 = 2 阶（1 级），24 = 4 阶（2 级），
         *  36 = 6 阶（3 级），48 = 8 阶（4 级）。
         *  Peak / Shelf 类型忽略此项（它们本身不是"切"）。 */
        int slopeDbPerOct = 12;
    };

    /** 斜率允许值（dB/oct）。 */
    static juce::StringArray slopeChoices()
    {
        return { "12 dB/oct", "24 dB/oct", "36 dB/oct", "48 dB/oct" };
    }

    /** 由斜率求需要的级联级数（每级 2 阶 = 12dB/oct）。 */
    static int stagesForSlope (int slopeDbPerOct) noexcept
    {
        return juce::jlimit (1, kMaxStages, slopeDbPerOct / 12);
    }

    const BandSettings& getBandSettings (int band) const noexcept
    {
        return settings[(size_t) juce::jlimit (0, kNumBands - 1, band)];
    }

private:
    using Filter = juce::dsp::IIR::Filter<float>;
    using Coeffs = juce::dsp::IIR::Coefficients<float>;

    std::array<BandSettings, kNumBands> settings;

    /** 每段用 kMaxStages 个双二阶串联。
     *  高切/低切靠级联获得更陡的斜率（单级只有 12dB/oct，Q 改不了斜率）。 */
    std::array<std::array<juce::dsp::ProcessorDuplicator<Filter, Coeffs>, kMaxStages>,
               kNumBands> filters;

    juce::dsp::ProcessSpec processSpec { 44100.0, 512, 2 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Equalizer)
};

} // namespace kf
