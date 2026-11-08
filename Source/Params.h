#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace kf
{

//==============================================================================
/** 所有参数的 ID 与创建函数集中在这里。
 *
 *  分层命名：
 *   sub_*     加法合成 Sub
 *   click{n}_* 三层 Click
 *   dist_*    失真
 *   comp_* 与 limiter_* 压缩与限幅
 *   eq{n}_*   8 段 EQ
 *   master_*  总输出
 */
namespace pid
{
    // --- 全局 / 音高
    constexpr const char* keytrack   = "keytrack";    // 音高跟随 MIDI 音符（开关）
    constexpr const char* tune       = "tune";        // 固定调音（半音）
    constexpr const char* glide      = "glide";       // Portamento 滑音时间（ms）
    constexpr const char* glideMode  = "glidemode";   // 滑音模式

    // --- Sub
    constexpr const char* subOn        = "sub_on";
    constexpr const char* subLevel     = "sub_level";
    constexpr const char* subDecay     = "sub_decay";      // Sub 包络总时长（ms）
    constexpr const char* subPitchAmt  = "sub_pitchamt";   // Pitch 包络深度（半音）
    constexpr const char* subHarmonics = "sub_harmonics";  // 泛音数量 0..8
    constexpr const char* subSubHarm   = "sub_subharm";    // Sub harmonics 量
    constexpr const char* subSubHarmOn = "sub_subharmon";
    constexpr const char* subPhase     = "sub_phase";      // 起始相位
    /** Sub 振荡器波形（Sine/Triangle/Saw/Square/Noise/Pulse）。 */
    constexpr const char* subWave     = "sub_wave";
    /** Sub 波形的 PWM / 形状微调（对 Saw/Square/Pulse 有效）。 */
    constexpr const char* subShape    = "sub_shape";
    constexpr const char* subHarmTilt = "sub_harmtilt";   // 泛音衰减速度
    constexpr const char* subPitchDecay = "sub_pitchdecay"; // 音高包络独立时长
    constexpr const char* subAmpPunch   = "sub_amppunch";   // 音量包络整形
    constexpr const char* subFilterHz   = "sub_filterhz";   // 包络跟随滤波截止
    constexpr const char* subFilterQ    = "sub_filterq";    // 滤波谐振
    constexpr const char* subFilterEnv  = "sub_filterenv";  // 包络跟随量
    constexpr const char* subOddEven    = "sub_oddeven";    // 奇次/偶次泛音平衡
    constexpr const char* subNoiseAmt   = "sub_noiseamt";   // 噪声瞬态层电平
    constexpr const char* subNoiseDecay = "sub_noisedecay"; // 噪声层衰减
    constexpr const char* subNoiseHp    = "sub_noisehp";    // 噪声层高通（去掉轰隆）
    constexpr const char* subPitchCurve = "sub_pitchcurve"; // 音高包络曲线弯曲
    constexpr const char* subHold       = "sub_hold";       // 保持时长
    constexpr const char* subWidth      = "sub_width";      // 立体声宽度
    constexpr const char* subAttack     = "sub_attack";     // 起音时间
    constexpr const char* subFilterType = "sub_filtertype"; // 滤波类型 LP/BP/HP
    constexpr const char* subDetune     = "sub_detune";     // 泛音失谐
    constexpr const char* subPhaseRand  = "sub_phaserand";  // 相位随机化
    constexpr const char* subPitchDelay = "sub_pitchdelay"; // 音高包络延迟启动
    constexpr const char* subStage2Amt  = "sub_stage2amt";  // 第二段下坠深度
    constexpr const char* subStage2Time = "sub_stage2time"; // 第二段起始时间
    constexpr const char* subGlideCurve = "sub_glidecurve"; // 滑音曲线（指数/线性）
    // click{n}_fadein —— 采样起音淡化（去咔哒）
    // click{n}_xfeed  —— 相邻层的交叉淡化
    constexpr const char* subVelToDecay = "sub_veltodecay"; // 力度->时长
    // comp_sc_hp   —— 侧链高通（让低频不触发压缩）
    // comp_lookahead —— 前瞻时间（更干净的瞬态控制）
    // eq_tilt      —— 全局倾斜（一次性改变整体明暗）
    constexpr const char* velAmount     = "vel_amount";     // MIDI 力度影响量
    constexpr const char* velTarget     = "vel_target";     // 力度映射目标
    constexpr const char* transOn       = "trans_on";       // 瞬态整形开关
    constexpr const char* transAttack   = "trans_attack";   // 瞬态 attack
    constexpr const char* transSustain  = "trans_sustain";  // 瞬态 sustain
    // click{n}_start / click{n}_end —— 采样切片起止点（0..1）

    // --- Click 层（3 层，动态编号）
    // click{n}_on / level / pitch / decay / lp / hp / keytrack / wave / shape
    // 每层除了采样，还能选一个内置波形作声源（wave != "Sample" 时用波形合成）
    inline juce::String click (int layer, const char* param)
    {
        return "click" + juce::String (layer + 1) + "_" + param;
    }

    // --- 失真
    constexpr const char* distOn    = "dist_on";
    constexpr const char* distType  = "dist_type";
    constexpr const char* distDrive = "dist_drive";
    constexpr const char* distMix   = "dist_mix";

    // --- 压缩 / 限幅
    constexpr const char* compOn       = "comp_on";
    constexpr const char* distBias     = "dist_bias";      // 染色偏置（偶次谐波）
    constexpr const char* distTone     = "dist_tone";      // 染色后音色明暗
    constexpr const char* distAutoGain = "dist_autogain";  // 自动增益补偿
    constexpr const char* compScHp     = "comp_schp";
    constexpr const char* compLookahead= "comp_lookahead";
    constexpr const char* compThresh  = "comp_thresh";
    constexpr const char* compRatio   = "comp_ratio";
    constexpr const char* compAttack  = "comp_attack";
    constexpr const char* compRelease = "comp_release";
    constexpr const char* compMakeup  = "comp_makeup";
    constexpr const char* limOn       = "limiter_on";
    constexpr const char* limCeiling  = "limiter_ceiling";

    // --- EQ（8 段，动态编号）
    inline juce::String eq (int band, const char* param)
    {
        return "eq" + juce::String (band + 1) + "_" + param;
    }

    // --- 总输出
    constexpr const char* masterGain = "master_gain";

    // --- 过采样（用于抑制失真产生的混叠）
    constexpr const char* oversample = "oversample";
    /** 是否开启相位重置（每次触发把振荡器相位归零，让底鼓每次都完全一致）。 */
    constexpr const char* phaseReset = "phase_reset";
}

//==============================================================================
/** 构造参数布局。处理器与编辑器共用同一份定义，避免两边不一致。 */
juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

/** 常量：Click 层数、EQ 段数。 */
constexpr int kNumClickLayers = 4;
constexpr int kNumEqBands = 8;

} // namespace kf
