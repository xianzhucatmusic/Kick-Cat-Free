#pragma once

#include <juce_dsp/juce_dsp.h>
#include "DspUtils.h"

namespace kf
{

//==============================================================================
/** 失真类型。
 *  KICK 2 提供 4 种，其中 2 种是建模的模拟失真：
 *   - Tube    电子管：非对称软饱和，偶次谐波丰富，低频厚
 *   - Tape    磁带：轻微压缩 + 高频滚降 + 偶次谐波
 *   - Hard    硬削波：激进、方波化，适合做 dirt
 *   - Fold    波折叠：超过阈值后折返，产生金属质感的密集谐波
 */
enum class DistortionType
{
    Tube = 0,      // 电子管：非对称软饱和，偶次谐波丰富
    Tape,          // 磁带：软压缩 + 高频滚降 + 轻微抖动
    Hard,          // 硬削波：干脆的方波化
    Fold,          // 波折叠：密集金属谐波
    Transformer,   // 变压器：低频磁饱和 + 高频相位偏移
    Console,       // 调音台通道条：极轻的连续饱和，几乎不失真但"厚"
    Germanium,     // 锗二极管：低电压下的粗糙削波，复古脏感
    Fuzz,          // 锗管法兹：强非对称，近乎方波 + 门限
    Radio,         // 无线电/窄带：带通 + 削波，做"电话音"
    Exciter,       // 谐波激励器：只加高频谐波，不改变低频
    NumTypes
};

//==============================================================================
class Distortion
{
public:
    Distortion() = default;

    void prepare (double sampleRate, int maxBlockSize);
    void reset();

    /** 处理一整块（就地）。
     *  @param drive  1..10 左右，内部折算成增益
     *  @param mix    0..1 干湿比
     *  @param type   失真类型 */
    void process (juce::AudioBuffer<float>& buffer,
                  int startSample, int numSamples,
                  DistortionType type,
                  float drive,
                  float mix,
                  float bias = 0.0f,
                  float tone = 0.5f,
                  bool autoGain = true);

    /** 每个染色模型的频响特征（Hz，0 = 不启用）。 */
    struct ToneCurve
    {
        float preHpHz = 0.0f;
        float preLpHz = 0.0f;
        float postHpHz = 0.0f;
        float postLpHz = 0.0f;
        bool  lowSaturate = false;   // 低频额外饱和（变压器）
        bool  highOnly = false;      // 只处理高频（激励器）
    };

    static ToneCurve toneCurveFor (DistortionType type, float tone) noexcept;

private:
    double sampleRate = 44100.0;

    // 磁带模式的滚降滤波 + 直流阻断
    std::array<dsp::DcBlocker, 2> dcBlockers;
    struct TapeFilter
    {
        float lp = 0.0f;
        inline float process (float x, float coeff) noexcept
        {
            lp += coeff * (x - lp);
            return lp;
        }
    };
    std::array<TapeFilter, 2> tapeFilters;

    // --- 染色专用的频响滤波 ---------------------------------------------
    // 真实硬件的"染色"一半来自非线性，另一半来自频率响应
    // （变压器低频饱和、磁带高频损失、无线电带通……）。
    // 只做波形整形会失去这些特征，听起来都像同一个失真。
    std::array<TapeFilter, 2> preLp;      // 染色前低通（限制进入非线性的频段）
    std::array<TapeFilter, 2> preHp;      // 染色前高通
    std::array<TapeFilter, 2> postLp;     // 染色后低通（模拟输出变压器/磁头）
    std::array<TapeFilter, 2> postHp;     // 染色后高通

    // Exciter 用的高频通路分离
    std::array<TapeFilter, 2> exciterLp;
    std::array<dsp::DcBlocker, 2> biasBlockers;

    // 增益补偿的平滑值（避免 Auto Gain 开关时爆音）
    float smoothedComp = 1.0f;

    static float shape (DistortionType type, float x) noexcept;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (Distortion)
};

} // namespace kf
