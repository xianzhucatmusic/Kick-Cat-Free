#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "WaveDisplay.h"

namespace kf
{

//==============================================================================
// 参数布局
//==============================================================================

juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout()
{
    juce::AudioProcessorValueTreeState::ParameterLayout layout;

    using FloatParam = juce::AudioParameterFloat;
    using BoolParam  = juce::AudioParameterBool;
    using ChoiceParam = juce::AudioParameterChoice;
    using Range = juce::NormalisableRange<float>;

    auto addFloat = [&layout] (const juce::String& id, const juce::String& name,
                               Range range, float def, const juce::String& suffix = {})
    {
        layout.add (std::make_unique<FloatParam> (
            juce::ParameterID { id, 1 }, name, range, def,
            juce::AudioParameterFloatAttributes().withLabel (suffix)));
    };

    auto addBool = [&layout] (const juce::String& id, const juce::String& name, bool def)
    {
        layout.add (std::make_unique<BoolParam> (juce::ParameterID { id, 1 }, name, def));
    };

    // --- 分组：让宿主里的参数列表也有条理
    const juce::String globalGroup = "Global";

    //=== 全局 / 音高 ==========================================================
    addBool (pid::keytrack, "Keytrack (follow MIDI note)", true);

    // Tune：以半音为单位，-24..+24。keytrack 关闭时它是绝对音高偏移；
    // 打开时它是相对 MIDI 音符的偏移。
    addFloat (pid::tune, "Tune", Range (-24.0f, 24.0f, 0.01f), 0.0f, " st");

    // Portamento 滑音时间
    addFloat (pid::glide, "Portamento", Range (0.0f, 500.0f, 0.1f, 0.4f), 0.0f, " ms");

    layout.add (std::make_unique<ChoiceParam> (
        juce::ParameterID { pid::glideMode, 1 }, "Glide Mode",
        juce::StringArray { "Off", "Always", "Legato" }, 0));

    //=== Sub =================================================================
    addBool (pid::subOn, "Sub On", true);
    // 默认电平整体调低，留出余量。
    // 原来默认输出峰值已达 -0.42 dBFS（紧贴限幅天花板），
    // 用户一进来就没有叠加/处理的余地。
    // 现在约 -6 dBFS，是更舒服的工作电平。
    addFloat (pid::subLevel, "Sub Level", Range (0.0f, 1.0f, 0.001f), 0.45f);
    addFloat (pid::subDecay, "Sub Decay", Range (20.0f, 2000.0f, 1.0f, 0.35f), 420.0f, " ms");
    addFloat (pid::subPitchAmt, "Pitch Env Amount", Range (-48.0f, 48.0f, 0.1f), 24.0f, " st");

    // 泛音数量 0..8
    layout.add (std::make_unique<juce::AudioParameterInt> (
        juce::ParameterID { pid::subHarmonics, 1 }, "Harmonics", 0, 8, 0));

    addFloat (pid::subSubHarm, "Sub Harmonics", Range (0.0f, 1.0f, 0.001f), 0.0f);
    addBool (pid::subSubHarmOn, "Sub Harmonics On", false);
    addFloat (pid::subPhase, "Start Phase", Range (0.0f, 360.0f, 1.0f), 0.0f, " deg");

    // Sub 振荡器波形：不同波形给底鼓完全不同的性格
    layout.add (std::make_unique<ChoiceParam> (
        juce::ParameterID { pid::subWave, 1 }, "Sub Waveform",
        wave::allNames(), 0));   // 默认 Sine
    addFloat (pid::subShape, "Sub Wave Shape", Range (0.0f, 1.0f, 0.001f), 0.5f);

    // 泛音衰减速度：0 = 圆润，1 = 明亮。0.5 对应原来的固定衰减。
    addFloat (pid::subHarmTilt, "Harmonic Tilt", Range (0.0f, 1.0f, 0.001f), 0.5f);

    // 音高包络的独立时长：0 = 跟随 Sub Decay（默认），>0 = 单独指定。
    // 让"音高先收紧、音量再收尾"这种经典底鼓做法成为可能。
    addFloat (pid::subPitchDecay, "Pitch Decay", Range (0.0f, 2000.0f, 1.0f, 0.35f), 0.0f, " ms");

    // 音量包络整形：控制起音的软硬
    addFloat (pid::subAmpPunch, "Amp Punch", Range (0.0f, 1.0f, 0.001f), 0.5f);

    // --- 包络跟随滤波：截止频率被音量包络驱动，做出"起音亮、尾巴暗"
    addFloat (pid::subFilterHz, "Sub Filter", Range (30.0f, 20000.0f, 1.0f, 0.25f),
              20000.0f, " Hz");
    addFloat (pid::subFilterQ, "Filter Reso", Range (0.5f, 8.0f, 0.01f), 0.707f);
    addFloat (pid::subFilterEnv, "Filter Env", Range (-1.0f, 1.0f, 0.001f), 0.0f);

    // --- MIDI 力度映射
    // 力度对音色的影响量：0 = 力度不影响（全部一样响），1 = 完全跟随力度
    addFloat (pid::velAmount, "Vel Amount", Range (0.0f, 1.0f, 0.001f), 0.6f);
    layout.add (std::make_unique<ChoiceParam> (
        juce::ParameterID { pid::velTarget, 1 }, "Vel Target",
        juce::StringArray { "Level", "Decay", "Pitch", "Level+Decay" }, 0));

    // --- Sub 音色结构
    // 奇次/偶次泛音平衡：改变泛音"结构"而非只是"多少"
    addFloat (pid::subOddEven, "Odd/Even", Range (-1.0f, 1.0f, 0.001f), 0.0f);

    // --- Sub 噪声瞬态层
    // 底鼓的"啪"感通常来自音头的一小段噪声。
    // 独立成层可以单独控制它的音量/衰减/高通，不污染 Sub 的干净低频。
    addFloat (pid::subNoiseAmt, "Noise Attack", Range (0.0f, 1.0f, 0.001f), 0.0f);
    addFloat (pid::subNoiseDecay, "Noise Decay", Range (1.0f, 200.0f, 0.1f, 0.3f), 12.0f, " ms");
    addFloat (pid::subNoiseHp, "Noise HP", Range (20.0f, 8000.0f, 1.0f, 0.25f), 400.0f, " Hz");

    // --- Sub 音高包络曲线与保持段
    addFloat (pid::subPitchCurve, "Pitch Curve", Range (-1.0f, 1.0f, 0.001f), 0.0f);
    addFloat (pid::subHold, "Hold", Range (0.0f, 200.0f, 0.1f), 0.0f, " ms");

    // --- Sub 立体声宽度与力度敏感度
    addFloat (pid::subWidth, "Width", Range (0.0f, 2.0f, 0.001f), 0.0f);

    // --- SUB 起音时间：做出"软起音"的长 808 ---
    addFloat (pid::subAttack, "Attack", Range (0.0f, 200.0f, 0.1f), 0.0f, " ms");

    // --- 滤波类型：TPT SVF 同时给出三种输出，切换零成本 ---
    layout.add (std::make_unique<ChoiceParam> (
        juce::ParameterID { pid::subFilterType, 1 }, "Filter Type",
        juce::StringArray { "Low Pass", "Band Pass", "High Pass" }, 0));

    // --- 泛音失谐：增加厚度（模拟合成器的常见手法）---
    addFloat (pid::subDetune, "Detune", Range (0.0f, 50.0f, 0.01f), 0.0f, " cent");

    // --- 相位随机化：多次触发时打散泛音相位，避免梳状叠加 ---
    addFloat (pid::subPhaseRand, "Phase Rand", Range (0.0f, 1.0f, 0.001f), 0.0f);

    // --- 音高包络延迟：起音后先保持最高音，延迟一段再开始下滑 ---
    // 做出"先顶住再掉"的形态，比立刻下滑更硬朗。
    addFloat (pid::subPitchDelay, "Pitch Delay", Range (0.0f, 200.0f, 0.1f), 0.0f, " ms");

    // --- 第二段下坠：主包络走完之后再补一段额外的下滑 ---
    // 单段衰减做不出"两级下坠"，这是长 808 常用的形态。
    addFloat (pid::subStage2Amt, "Stage2 Amt", Range (0.0f, 24.0f, 0.01f), 0.0f, " st");
    addFloat (pid::subStage2Time, "Stage2 Time", Range (0.0f, 100.0f, 0.1f), 0.0f, " %");

    // --- 滑音曲线：0 = 线性，1 = 指数（更接近模拟合成器）---
    addFloat (pid::subGlideCurve, "Glide Curve", Range (0.0f, 1.0f, 0.001f), 1.0f);

    // --- 力度对 SUB 时长的影响：重按 -> 更长 ---
    addFloat (pid::subVelToDecay, "Vel>Decay", Range (0.0f, 1.0f, 0.001f), 0.0f);

    // --- 瞬态整形：单独控制起音与延音的突出程度
    addBool (pid::transOn, "Transient On", false);
    // --- 侧链高通：低频不触发压缩 ---
    // 底鼓的低频能量很大，会让压缩器一直压。加一个侧链高通后，
    // 只有中高频触发压缩，低频的冲击力得以保留。
    addFloat (pid::compScHp, "SC High Pass", Range (20.0f, 2000.0f, 1.0f, 0.3f), 20.0f, " Hz");

    // --- 前瞻：提前看到瞬态，压缩更干净 ---
    addFloat (pid::compLookahead, "Lookahead", Range (0.0f, 10.0f, 0.01f), 0.0f, " ms");

    addFloat (pid::transAttack, "Attack", Range (-1.0f, 1.0f, 0.001f), 0.0f);
    addFloat (pid::transSustain, "Sustain", Range (-1.0f, 1.0f, 0.001f), 0.0f);

    //=== Click 层 ============================================================
    for (int i = 0; i < kNumClickLayers; ++i)
    {
        const juce::String n = juce::String (i + 1);

        addBool (pid::click (i, "on"), "Click " + n + " On", i == 0);
        // 默认电平对所有层都给一个可用值（第 1 层略高），
        // 整体比早期版本低约 5.6dB，避免默认就顶到限幅器。
        // 采样切片：起止点（相对于采样长度的比例）
        addFloat (pid::click (i, "start"), "Click " + n + " Start",
                  Range (0.0f, 1.0f, 0.001f), 0.0f);
        addFloat (pid::click (i, "end"), "Click " + n + " End",
                  Range (0.0f, 1.0f, 0.001f), 1.0f);
        addFloat (pid::click (i, "level"), "Click " + n + " Level",
                  Range (0.0f, 1.0f, 0.001f), i == 0 ? 0.30f : 0.25f);
        addFloat (pid::click (i, "pitch"), "Click " + n + " Pitch", Range (-24.0f, 24.0f, 0.01f), 0.0f, " st");
        addFloat (pid::click (i, "decay"), "Click " + n + " Decay", Range (5.0f, 500.0f, 0.5f, 0.4f), 60.0f, " ms");

        // 采样起音淡化：从采样中间切进去（Start 不为 0）时会有咔哒声，
        // 加一个极短的渐入能消除它。
        addFloat (pid::click (i, "fadein"), "Click " + n + " Fade In",
                  Range (0.0f, 20.0f, 0.01f), 0.0f, " ms");
        addFloat (pid::click (i, "lp"), "Click " + n + " Low Pass", Range (200.0f, 20000.0f, 1.0f, 0.25f), 20000.0f, " Hz");
        addFloat (pid::click (i, "hp"), "Click " + n + " High Pass", Range (10.0f, 5000.0f, 1.0f, 0.3f), 10.0f, " Hz");
        addBool (pid::click (i, "keytrack"), "Click " + n + " Keytrack", false);

        // 波形声源：index 0 = "Sample"（用载入的采样），1..6 = 内置波形
        juce::StringArray waveChoices;
        waveChoices.add ("Sample");
        waveChoices.addArray (wave::allNames());

        layout.add (std::make_unique<ChoiceParam> (
            juce::ParameterID { pid::click (i, "wave"), 1 },
            "Click " + n + " Waveform", waveChoices, 0));

        addFloat (pid::click (i, "shape"), "Click " + n + " Wave Shape",
                  Range (0.0f, 1.0f, 0.001f), 0.5f);
        // 波形基频（仅波形模式有效）
        addFloat (pid::click (i, "wavetune"), "Click " + n + " Wave Tune",
                  Range (40.0f, 4000.0f, 1.0f, 0.3f), 400.0f, " Hz");
    }

    //=== 失真 ================================================================
    addBool (pid::distOn, "Distortion On", false);
    // 染色模型：从"失真类型"扩展为"硬件染色模拟"。
    // 每个模型不只是不同的波形整形曲线，还带**自己的频率响应** ——
    // 真实硬件的染色有一半来自频响（变压器低频饱和、磁带高频损失、
    // 无线电带通……），只改波形会听起来都像同一个失真。
    layout.add (std::make_unique<ChoiceParam> (
        juce::ParameterID { pid::distType, 1 }, "Colour Model",
        juce::StringArray {
            "Tube",         // 电子管：非对称软饱和，暖厚
            "Tape",         // 磁带：高频损失 + 磁头隆起
            "Hard Clip",    // 硬削波：干脆方波化
            "Wave Fold",    // 波折叠：金属谐波
            "Transformer",  // 变压器：低频磁饱和
            "Console",      // 调音台通道：极轻饱和，只加密度
            "Germanium",    // 锗二极管：粗糙复古脏感
            "Fuzz",         // 锗管法兹：非对称 + 门限
            "Radio",        // 无线电窄带：电话音
            "Exciter"       // 谐波激励器：只加高频
        }, 0));
    addFloat (pid::distDrive, "Dist Drive", Range (1.0f, 20.0f, 0.01f), 3.0f);
    addFloat (pid::distMix, "Dist Mix", Range (0.0f, 1.0f, 0.001f), 1.0f);

    // 偏置：给波形加直流再进非线性，正负半周不对称 -> 偶次谐波。
    // 这是"暖"与"脏"之间最重要的一个旋钮。
    addFloat (pid::distBias, "Bias", Range (-0.5f, 0.5f, 0.001f), 0.0f);

    // 音色明暗：对各模型自带的输出低通做 ±1 八度调整
    addFloat (pid::distTone, "Tone", Range (0.0f, 1.0f, 0.001f), 0.5f);

    // 自动增益：按模型微调补偿量，切换染色类型时音量不会跳
    addBool (pid::distAutoGain, "Auto Gain", true);

    //=== 压缩 / 限幅 =========================================================
    addBool (pid::compOn, "Compressor On", false);
    addFloat (pid::compThresh, "Comp Threshold", Range (-40.0f, 0.0f, 0.1f), -12.0f, " dB");
    addFloat (pid::compRatio, "Comp Ratio", Range (1.0f, 20.0f, 0.01f), 4.0f, ":1");
    addFloat (pid::compAttack, "Comp Attack", Range (0.05f, 100.0f, 0.01f, 0.4f), 3.0f, " ms");
    addFloat (pid::compRelease, "Comp Release", Range (5.0f, 1000.0f, 0.1f, 0.4f), 120.0f, " ms");
    addFloat (pid::compMakeup, "Comp Makeup", Range (0.0f, 24.0f, 0.01f), 0.0f, " dB");

    addBool (pid::limOn, "Limiter On", true);
    addFloat (pid::limCeiling, "Limiter Ceiling", Range (-12.0f, 0.0f, 0.01f), -0.3f, " dB");

    //=== EQ ==================================================================
    {
        const auto& defs = Equalizer::defaultFrequencies();
        const char* names[kNumEqBands] = { "Sub Rumble", "Sub Body", "Punch", "Fundamental",
                                           "Mud", "Box", "Attack", "Air" };
        for (int b = 0; b < kNumEqBands; ++b)
        {
            const juce::String n = juce::String (b + 1);

            addBool (pid::eq (b, "on"), "EQ " + n + " " + names[b] + " On", false);
            addFloat (pid::eq (b, "freq"), "EQ " + n + " Freq",
                      Range (10.0f, 20000.0f, 1.0f, 0.25f), defs[(size_t) b], " Hz");
            addFloat (pid::eq (b, "gain"), "EQ " + n + " Gain", Range (-18.0f, 18.0f, 0.01f), 0.0f, " dB");
            addFloat (pid::eq (b, "q"), "EQ " + n + " Q", Range (0.1f, 12.0f, 0.01f, 0.4f), 0.9f);

            // 高切/低切的斜率（12/24/36/48 dB/oct）。
            // 单级双二阶斜率固定为 12dB/oct，Q 只能改谐振峰、改不了斜率；
            // 要更陡的切必须级联多级，所以这里单独给一个参数。
            layout.add (std::make_unique<ChoiceParam> (
                juce::ParameterID { pid::eq (b, "slope"), 1 },
                "EQ " + n + " Slope", Equalizer::slopeChoices(), 0));
        }
    }

    //=== 总输出 ==============================================================
    addFloat (pid::masterGain, "Master Gain", Range (-24.0f, 12.0f, 0.01f), 0.0f, " dB");

    //=== 质量选项 ============================================================
    // 过采样：失真会产生大量超高频谐波，2x/4x 过采样能显著减少混叠。
    addBool (pid::oversample, "Oversampling", false);
    addBool (pid::phaseReset, "Phase Reset on Trigger", false);

    juce::ignoreUnused (globalGroup);
    return layout;
}

//==============================================================================
// Processor
//==============================================================================

KickForgeProcessor::KickForgeProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true))
    , apvts (*this, nullptr, "PARAMETERS", createParameterLayout())
{
    // 建立默认包络形状
    // Sub Pitch：从高音快速下滑到基频，这是底鼓「啪」的起点
    {
        auto& e = envelopes[(size_t) EnvelopeTarget::SubPitch];
        e.resetToFlat (0.0f);

        // 重新构造一条经典的下滑曲线：起始 +1（满深度）-> 归零
        std::vector<EnvNode> nodes;
        EnvNode a; a.time = 0.0f; a.value = 1.0f;
        a.outTanX = 0.10f; a.outTanY = 0.55f; a.autoOut = false; a.autoIn = false;
        nodes.push_back (a);

        EnvNode b; b.time = 0.10f; b.value = 0.42f;
        b.inTanX = 0.5f; b.inTanY = 0.30f;
        b.outTanX = 0.35f; b.outTanY = 0.32f;
        b.autoIn = false; b.autoOut = false;
        nodes.push_back (b);

        EnvNode c; c.time = 0.45f; c.value = 0.0f;
        c.inTanX = 0.45f; c.inTanY = 0.28f;
        c.outTanX = 0.4f; c.outTanY = 0.0f;
        c.autoIn = false; c.autoOut = false;
        nodes.push_back (c);

        EnvNode d; d.time = 1.0f; d.value = 0.0f;
        d.inTanX = 0.5f; d.inTanY = 0.0f; d.autoIn = false; d.autoOut = false;
        nodes.push_back (d);

        juce::String s = "v1;";
        for (size_t i = 0; i < nodes.size(); ++i)
        {
            const auto& n = nodes[i];
            if (i > 0) s << "|";
            s << juce::String (n.time, 5) << "," << juce::String (n.value, 5) << ","
              << juce::String (n.outTanX, 5) << "," << juce::String (n.outTanY, 5) << ","
              << juce::String (n.inTanX, 5) << "," << juce::String (n.inTanY, 5) << "," << "00";
        }
        e.fromString (s);
    }

    // Sub Amp：极快起音 + 指数下落
    envelopes[(size_t) EnvelopeTarget::SubAmp].resetToDefault();

    // Click 层包络：更短、更陡
    for (int i = 0; i < kNumClickLayers; ++i)
    {
        auto& e = envelopes[(size_t) EnvelopeTarget::Click1Amp + i];
        e.resetToFlat (0.0f);

        std::vector<EnvNode> nodes;
        EnvNode a; a.time = 0.0f; a.value = 0.0f; nodes.push_back (a);
        EnvNode b; b.time = 0.005f; b.value = 1.0f;
        b.inTanX = 0.5f; b.outTanX = 0.25f; b.outTanY = 0.6f;
        b.autoIn = false; b.autoOut = false;
        nodes.push_back (b);
        EnvNode c; c.time = 1.0f; c.value = 0.0f;
        c.inTanX = 0.5f; c.inTanY = 0.45f; c.autoIn = false; c.autoOut = false;
        nodes.push_back (c);

        juce::String s = "v1;";
        for (size_t i2 = 0; i2 < nodes.size(); ++i2)
        {
            const auto& n = nodes[i2];
            if (i2 > 0) s << "|";
            s << juce::String (n.time, 5) << "," << juce::String (n.value, 5) << ","
              << juce::String (n.outTanX, 5) << "," << juce::String (n.outTanY, 5) << ","
              << juce::String (n.inTanX, 5) << "," << juce::String (n.inTanY, 5) << "," << "00";
        }
        e.fromString (s);
    }

    // 烘焙所有包络
    for (auto& e : envelopes)
        e.rebuildTable (2048);

    // 预设管理器（会扫描用户预设目录）
    presetManager = std::make_unique<PresetManager> (*this);

    // AI 参数应用器：放在 processor 上而不是编辑器上 ——
    // 编辑器可能被宿主反复创建销毁，过渡状态不应该跟着丢。
    aiApplier = std::make_unique<AiApplier> (*this);

}

KickForgeProcessor::~KickForgeProcessor() = default;

//==============================================================================

void KickForgeProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    currentSampleRate = sampleRate;
    currentBlockSize  = samplesPerBlock;

    subSynth.prepare (sampleRate, samplesPerBlock);
    clicks.prepare (sampleRate, samplesPerBlock);

    // 瞬态整形的包络跟随器必须清零，否则残留状态会影响下一段渲染
    transFastEnv = 0.0f;
    transSlowEnv = 0.0f;

    // 波形捕获用的独立引擎
    captureSub.prepare (sampleRate, samplesPerBlock);
    captureClicks.prepare (sampleRate, samplesPerBlock);

    distortion.prepare (sampleRate, samplesPerBlock);

    // 过采样器：按最大 4x 预分配，运行时按参数切换因子（避免音频线程分配）
    {
        const int numCh = juce::jmax (1, getTotalNumOutputChannels());
        oversampler = std::make_unique<juce::dsp::Oversampling<float>> (
            (size_t) numCh, 2,   // 2 级 = 最多 4x
            juce::dsp::Oversampling<float>::filterHalfBandPolyphaseIIR,
            true, false);
        oversampler->initProcessing ((size_t) samplesPerBlock);
        oversampler->reset();

        oversampleBuffer.setSize (numCh, samplesPerBlock, false, true, true);
    }
    masterChain.prepare (sampleRate, samplesPerBlock);

    juce::dsp::ProcessSpec spec { sampleRate, (juce::uint32) samplesPerBlock, 2 };
    eq.prepare (spec);

    for (auto& d : postEqDcBlockers)
        d.prepare (sampleRate, 20.0);

    // EQ 增益平滑：约 30ms 收敛。块大小不同时系数会略有差异，
    // 但听感上都是"平滑过渡"，足够了。
    {
        const double blocksPerSecond = sampleRate / juce::jmax (1, samplesPerBlock);
        eqGainSmoothCoeff = (float) juce::jlimit (0.02, 1.0,
            1.0 - std::exp (-1.0 / (0.030 * blocksPerSecond)));
    }

    for (int b = 0; b < kNumEqBands; ++b)
        eqSmoothedGain[b] = rawGainFor (b);

    library.initialise (sampleRate);

    glideFreq.reset (sampleRate, 0.05);

    for (auto& r : envReaders)
        r.prepare (2048);

    updateEnvelopeReaders();

    for (auto& w : waveCapture)
        w.fill (0.0f);
    waveCaptureFill.fill (0);
}

void KickForgeProcessor::releaseResources()
{
}

bool KickForgeProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    // 只支持单输出（单声道或立体声）
    const auto& out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo();
}

//==============================================================================

void KickForgeProcessor::updateEnvelopeReaders()
{
    // 把 UI 线程编辑好的包络烘焙成表，交给音频线程的无锁读取器
    for (size_t i = 0; i < envelopes.size(); ++i)
    {
        envelopes[i].rebuildTable (2048);
        std::vector<float> table (2048);
        for (int k = 0; k < 2048; ++k)
            table[(size_t) k] = envelopes[i].getValueAt ((float) k / 2047.0f);

        envReaders[i].swapTable (std::move (table));
    }
}

void KickForgeProcessor::commitEnvelope (EnvelopeTarget t)
{
    const size_t i = (size_t) t;
    envelopes[i].rebuildTable (2048);

    std::vector<float> table (2048);
    for (int k = 0; k < 2048; ++k)
        table[(size_t) k] = envelopes[i].getValueAt ((float) k / 2047.0f);

    envReaders[i].swapTable (std::move (table));
}

float KickForgeProcessor::computeFrequency (int midiNote) const
{
    const bool kt = apvts.getRawParameterValue (pid::keytrack)->load() > 0.5f;
    const float tuneSemis = apvts.getRawParameterValue (pid::tune)->load();

    if (kt)
    {
        // 跟随 MIDI 音符：以 A4=440 为基准，加上 Tune 偏移
        return dsp::midiNoteToFrequency ((float) midiNote) * dsp::semitonesToRatio (tuneSemis);
    }

    // 不跟随：以 C1 (32.7Hz) 附近的固定音高为基准，只受 Tune 控制。
    // 用 60 (C4) 作为参考点，这样 Tune 的听感与 keytrack 打开时一致。
    constexpr int kRefNote = 36;   // C2 ≈ 65.4Hz，底鼓常用基频
    return dsp::midiNoteToFrequency ((float) kRefNote) * dsp::semitonesToRatio (tuneSemis);
}

void KickForgeProcessor::startNote (int midiNote, float velocity)
{
    const float targetFreq = computeFrequency (midiNote);

    const int glideMode = (int) apvts.getRawParameterValue (pid::glideMode)->load();
    // 非 const：下面会被调制值调整
    float glideMs = apvts.getRawParameterValue (pid::glide)->load();

    // 调制：滑音时间（±200ms）。
    // 在触发时读取 —— 滑音时长本来就在触发瞬间确定，
    // 所以这里用当前调制值是正确的（不像 Tune 那样存在时序问题，
    // 因为 glide 不影响音高本身，只影响滑到目标的过程）。
    glideMs = juce::jlimit (0.0f, 2000.0f,
        glideMs + modulation.getValue (ModulationMatrix::Target::Glide) * 200.0f);

    // 连奏判定：上一个音还在响（未走完包络）
    const bool isLegato = subSynth.isActive() || clicks.getLayer (0).isActive();

    bool doGlide = false;
    if (glideMs > 0.5f)
    {
        if (glideMode == 1)                                  // Always
            doGlide = (lastMidiNote >= 0);
        else if (glideMode == 2)                             // Legato
            doGlide = isLegato && (lastMidiNote >= 0);
    }

    float startFreq = targetFreq;
    if (doGlide && lastMidiNote >= 0)
        startFreq = currentFrequency.load();

    glideTargetFreq = targetFreq;
    glideStartFreq  = startFreq;
    glideTotalSamples = doGlide ? (double) glideMs * 0.001 * currentSampleRate : 0.0;
    glideSamples = 0.0;
    glidePending = doGlide;

    currentFrequency.store (startFreq);

    // --- Click 层的音高：层自身 pitch + 可选的 keytrack
    std::array<float, ClickSection::kNumLayers> clickPitch {};

    for (int i = 0; i < kNumClickLayers; ++i)
    {
        float semis = apvts.getRawParameterValue (pid::click (i, "pitch"))->load();

        // 若该层开启 keytrack，则把 MIDI 音符的音高差也算进去
        if (apvts.getRawParameterValue (pid::click (i, "keytrack"))->load() > 0.5f && midiNote >= 0)
            semis += (float) (midiNote - 36);

        clickPitch[(size_t) i] = dsp::semitonesToRatio (semis);
    }

    // --- MIDI 力度映射 -------------------------------------------------------
    //
    // 默认（Vel Amount = 0.6）时力度只影响音量，且范围收窄，
    // 避免"轻按几乎没声"这种在鼓机上很难用的行为。
    // 可以改映射目标：力度去调制 Decay 或 Pitch，做出"按得越重越短/越高"。
    const float velAmt  = apvts.getRawParameterValue (pid::velAmount)->load();
    // 直接用参数的选项索引（choiceIndex lambda 在下面才定义）
    const int   velTgt  = juce::jlimit (0, 3, (int) std::lround (
                              apvts.getRawParameterValue (pid::velTarget)->load()));

    // velocity 是 0..1；映射到"力度影响系数"
    //   0 -> 完全不受力度影响（固定 1.0）
    //   1 -> 完全跟随力度
    const float velScale = 1.0f - velAmt + velAmt * velocity;

    // 力度对 Decay 的影响：重按 -> 更长（最多 ±50%）
    float velDecayScale = 1.0f;
    if (velTgt == 1 || velTgt == 3)
        velDecayScale = 1.0f + (velocity - 0.5f) * velAmt;

    // 力度对音高的影响：重按 -> 更高（最多 ±12 半音）
    float velPitchSemis = 0.0f;
    if (velTgt == 2)
        velPitchSemis = (velocity - 0.5f) * 2.0f * velAmt * 12.0f;

    if (velTgt == 0 || velTgt == 3)
        velocity = juce::jlimit (0.0f, 1.0f, velScale);

    if (velPitchSemis != 0.0f)
    {
        startFreq *= dsp::semitonesToRatio (velPitchSemis);
        currentFrequency.store (startFreq);
    }

    // --- 触发声音
    const bool subOn = apvts.getRawParameterValue (pid::subOn)->load() > 0.5f;
    if (subOn)
    {
        // ⚠️ 顺序很重要：setStartPhase 必须在 noteOn **之前** ——
        // noteOn 里会用 startPhase 归零相位，放后面就晚了一步
        // （这一版之前就是把 setStartPhase 放在 noteOn 之后，导致 phase_reset 无效）。
        subSynth.setStartPhase (apvts.getRawParameterValue (pid::subPhase)->load());

        // 调制矩阵重新触发（通用 ADSR 与 LFO 从头开始）
        modulation.noteOn (velocity, midiNote);

        // Sub 用当前滑音起点频率触发；滑音过程在 processBlock 里逐块推进
        subSynth.noteOn (startFreq, velocity);

        // 相位随机化：在起始相位基础上加随机偏移，避免各泛音完全同相。
        // 必须放在 noteOn **之后** —— 否则会被 noteOn 里的归零覆盖掉。
        subSynth.randomizePhases (apvts.getRawParameterValue (pid::subPhaseRand)->load());
    }

    clicks.noteOn (velocity, clickPitch);

    lastMidiNote = midiNote;
    lastVelocity = velocity;
    noteActive = true;

    // 清空波形显示，准备捕获新的一次
    for (auto& w : waveCapture)
        w.fill (0.0f);
    waveCaptureFill.fill (0);
    needsWaveCapture = true;
}

void KickForgeProcessor::triggerAudition (float velocity)
{
    // 用一个默认音符试听（C2 = 36）
    startNote (36, velocity);
}

void KickForgeProcessor::handleMidi (const juce::MidiBuffer& midi)
{
    for (const auto meta : midi)
    {
        const auto msg = meta.getMessage();

        if (msg.isNoteOn())
        {
            startNote (msg.getNoteNumber(), msg.getFloatVelocity());
        }
        else if (msg.isNoteOff())
        {
            // 底鼓是 one-shot：noteOff 不停声，让包络自然收尾
            noteActive = false;
        }
        else if (msg.isAllNotesOff() || msg.isAllSoundOff())
        {
            subSynth.reset();
            clicks.reset();
            noteActive = false;
        }
    }
}

//==============================================================================

void KickForgeProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;

    const int numSamples = buffer.getNumSamples();
    const int numChannels = buffer.getNumChannels();

    // 清空输出（我们只做加法渲染）
    for (int ch = 0; ch < numChannels; ++ch)
        buffer.clear (ch, 0, numSamples);

    if (numSamples <= 0)
        return;

    //=== 调制矩阵推进 ========================================================
    // 每块算一次，结果在下面各处按目标查询。
    // 没有任何激活路由时跳过（避免无谓开销）。
    if (modulation.hasAnyActiveRoute())
    {
        // 把宿主速度传给调制矩阵（供 LFO 同步模式使用）
        double bpm = 0.0;
        bool playing = true;
        if (auto* ph = getPlayHead())
        {
            if (auto pos = ph->getPosition())
            {
                // getBpm() 返回 Optional<double>，getIsPlaying() 返回 bool
                if (auto b = pos->getBpm())  bpm = *b;
                playing = pos->getIsPlaying();
            }
        }
        modulation.setHostTempo (bpm, playing);
        modulation.advance (numSamples, currentSampleRate);
    }


    //=== 波形配置（必须在 handleMidi 之前）=====================================
    // 关键顺序：Click 层的 noteOn() 会检查"是否有声源"。
    // 若先处理 MIDI（触发 noteOn）再配置波形，层会因为还处于"无采样"状态
    // 而直接返回、不发声 —— 波形模式就完全失效。
    // 所以波形模式必须在触发音符之前设定好。
    //=== Sub 振荡器波形 ======================================================
    // AudioParameterChoice 的读取方式（实测确认）：
    //   getRawParameterValue(id)->load()  返回的就是 **选项索引**（如 0..6），
    //   不是 0..1 归一化值 —— 归一化值只在 getValue()/setValueNotifyingHost()
    //   这条宿主 API 上用。
    //   所以这里直接四舍五入取整即可。反之若要"按索引写入"，必须用
    //   p->setValueNotifyingHost(p->convertTo0to1(index))。
    auto choiceIndex = [this] (const juce::String& id) -> int
    {
        if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (apvts.getParameter (id)))
        {
            const float raw = apvts.getRawParameterValue (id)->load();
            const int n = juce::jmax (1, p->choices.size());
            return juce::jlimit (0, n - 1, (int) std::lround (raw));
        }
        return 0;
    };

    // Sub 的开关在下面才读取，这里就地取一次
    if (apvts.getRawParameterValue (pid::subOn)->load() > 0.5f)
    {
        const int wf = choiceIndex (pid::subWave);
        subSynth.setWaveform ((Waveform) juce::jlimit (0, (int) Waveform::NumWaveforms - 1, wf));
        // 调制：波形形状。Square/Saw/Triangle/Pulse 会响应它
        // （Sine/Noise 不响应 —— 正弦没有可调形状，这是设计如此）。
        subSynth.setShape (juce::jlimit (0.0f, 1.0f,
            apvts.getRawParameterValue (pid::subShape)->load()
                + modulation.getValue (ModulationMatrix::Target::SubShape)));

    // 相位归零开关（原来声明了参数但 DSP 从未读取 —— 实测开关无效）
    subSynth.setPhaseResetOnTrigger (
        apvts.getRawParameterValue (pid::phaseReset)->load() > 0.5f);
    }

    //=== Click 层波形声源 ====================================================
    for (int i = 0; i < kNumClickLayers; ++i)
    {
        // 0 = Sample，1.. = 内置波形
        const int wfIdx = choiceIndex (pid::click (i, "wave"));

        if (wfIdx > 0)
        {
            const auto w = (Waveform) juce::jlimit (0, (int) Waveform::NumWaveforms - 1, wfIdx - 1);
            // 调制：波形调音（±50 半音）与波形形状。
            // 注意：这两个只对**波形模式**有效（wfIdx > 0）；
            // 采样模式下不读它们。
            clicks.getLayer (i).setWaveformSource (
                true, w,
                // ⚠️ 参数范围是 **40..4000 Hz**，不是半音。
                // 我第一次写成 jlimit(-24, 24) 把它钳成了 24 —— 
                // 实测 wavetune 因此完全失效（200 和 1200 都被压成 24）。
                // 用**倍频程**缩放，与其它频率参数保持一致。
                juce::jlimit (40.0f, 4000.0f,
                    apvts.getRawParameterValue (pid::click (i, "wavetune"))->load()
                        * std::pow (2.0f, modulation.getValue (
                              ModulationMatrix::Target::ClickWaveTune) * 1.0f)),
                juce::jlimit (0.0f, 1.0f,
                    apvts.getRawParameterValue (pid::click (i, "shape"))->load()
                        + modulation.getValue (ModulationMatrix::Target::ClickShape)));
        }
        else
        {
            clicks.getLayer (i).setWaveformSource (false, Waveform::Sine, 400.0f, 0.5f);
        }
    }

    handleMidi (midi);

    //=== 读取参数 =============================================================
    const bool  subOn       = apvts.getRawParameterValue (pid::subOn)->load() > 0.5f;
    // 调制：Sub 音量（0..1）。按 ±1 映射后钳制。
    const float subLevel    = juce::jlimit (0.0f, 1.0f,
        apvts.getRawParameterValue (pid::subLevel)->load()
            + modulation.getValue (ModulationMatrix::Target::SubLevel));
    // 力度 -> 时长：重按让 Sub 更长（最多 ±60%）。
    // 与全局的 vel_target 不同，这个只作用于 Sub，不影响 Click。
    const float velToDecay = apvts.getRawParameterValue (pid::subVelToDecay)->load();
    const float velDecayMul = 1.0f + (lastVelocity - 0.5f) * velToDecay * 1.2f;

    // 调制：音量包络时长（±300ms）。乘在力度/速度缩放之后，
    // 这样"重按更长"与"调制更长"能叠加而不是互相覆盖。
    const float subDecay    = juce::jlimit (0.001f, 8.0f,
        apvts.getRawParameterValue (pid::subDecay)->load() * 0.001f
            * velocityDecayScale * velDecayMul
            + modulation.getValue (ModulationMatrix::Target::SubDecay) * 0.3f);
    // 调制：音高包络时长（±300ms）
    const float subPitchDec = juce::jlimit (0.001f, 4.0f,
        apvts.getRawParameterValue (pid::subPitchDecay)->load() * 0.001f
            + modulation.getValue (ModulationMatrix::Target::SubPitchDecay) * 0.3f);
    // 调制：音量包络的"冲击"形状（0..1）。
    const float subPunch    = juce::jlimit (0.0f, 1.0f,
        apvts.getRawParameterValue (pid::subAmpPunch)->load()
            + modulation.getValue (ModulationMatrix::Target::SubAmpPunch));
    // 调制：Sub 音高。pitchAmt 是"音高包络的深度"（半音），
    // 调制直接叠加在它上面 —— 所以调制 Sub Pitch 实际改变的是
    // "音头下滑的幅度"，这正是底鼓最需要的效果。
    // 量程映射：归一化调制(-1..1) × 24 半音。
    const float pitchAmt    = apvts.getRawParameterValue (pid::subPitchAmt)->load()
                              + modulation.getValue (ModulationMatrix::Target::SubPitch) * 24.0f;

    // 调制：泛音量。按 ±8 映射（参数本身是 0..8）。
    const int   harmonics   = juce::jlimit (0, 8,
        (int) std::lround (apvts.getRawParameterValue (pid::subHarmonics)->load()
                           + modulation.getValue (ModulationMatrix::Target::SubHarmonics) * 8.0f));
    // 调制：次谐波量（"下潜感"）
    const float subHarm     = juce::jlimit (0.0f, 1.0f,
        apvts.getRawParameterValue (pid::subSubHarm)->load()
            + modulation.getValue (ModulationMatrix::Target::SubSubHarm));
    const bool  subHarmOn   = apvts.getRawParameterValue (pid::subSubHarmOn)->load() > 0.5f;

    // 泛音幅度：加法合成。
    //
    // 原来是写死的 1/(h+2) 衰减，用户无法调整"泛音多少"的音色倾向。
    // 现在由 Harmonic Tilt 控制衰减速度：
    //   tilt = 0    -> 快速衰减（只有基频附近，音色圆润）
    //   tilt = 0.5  -> 原来的 1/(h+2)
    //   tilt = 1    -> 缓慢衰减（泛音丰富，音色明亮/硬）
    // 调制：泛音衰减速度（明亮度）。这是"音头亮、尾巴暗"的关键参数。
    const float harmonicTilt = juce::jlimit (0.0f, 1.0f,
                                  apvts.getRawParameterValue (pid::subHarmTilt)->load()
                                      + modulation.getValue (ModulationMatrix::Target::SubHarmTilt));

    // 指数从 1.6（快衰减）到 0.35（慢衰减）
    const float falloffExp = juce::jmap (harmonicTilt, 1.6f, 0.35f);

    // 奇次/偶次平衡的调制值（-1..1），下面与参数值合成
    const float oddEvenMod = modulation.getValue (ModulationMatrix::Target::SubOddEven);

    // 奇次 / 偶次泛音平衡：
    //   -1 = 全是偶次（空心、类似方波的"鼻音"）
    //    0 = 原样（自然的 1/n 衰减）
    //   +1 = 全是奇次（厚实、类似单簧管）
    // 这是改变泛音"结构"而不只是"多少"的手段。
    const float oddEven = juce::jlimit (-1.0f, 1.0f,
                            apvts.getRawParameterValue (pid::subOddEven)->load()
                                + oddEvenMod);

    std::array<float, SubSynth::kMaxHarmonics> harmonicLevels {};
    for (int h = 0; h < SubSynth::kMaxHarmonics; ++h)
    {
        const float idx = (float) (h + 2);
        float lvl = std::pow (idx, -falloffExp);

        // h 索引 0 对应第 2 泛音（偶次），索引 1 对应第 3 泛音（奇次）
        const bool isOdd = ((h + 2) % 2) != 0;

        // 只衰减"不受欢迎"的那一类，不做提升，避免整体变响
        if (oddEven > 0.0f && ! isOdd)
            lvl *= (1.0f - oddEven);
        else if (oddEven < 0.0f && isOdd)
            lvl *= (1.0f + oddEven);

        harmonicLevels[(size_t) h] = lvl;
    }

    std::array<float, ClickSection::kNumLayers> clickLevels {};
    std::array<float, ClickSection::kNumLayers> clickDecays {};
    std::array<float, ClickSection::kNumLayers> clickLp {};
    std::array<float, ClickSection::kNumLayers> clickHp {};
    std::array<bool,  ClickSection::kNumLayers> clickOn {};
    std::array<float, ClickSection::kNumLayers> clickStart {};
    std::array<float, ClickSection::kNumLayers> clickEnd {};
    std::array<float, ClickSection::kNumLayers> clickFadeIn {};
    std::array<const EnvelopeReader*, ClickSection::kNumLayers> clickEnvs {};

    for (int i = 0; i < kNumClickLayers; ++i)
    {
        clickOn[(size_t) i]     = apvts.getRawParameterValue (pid::click (i, "on"))->load() > 0.5f;
        // 调制：Click 层音量。一条路由同时推动 4 层，
        // 这样"整体音头强度"可以跟着包络走（做音头突出/收敛）。
        clickLevels[(size_t) i] = juce::jlimit (0.0f, 2.0f,
            apvts.getRawParameterValue (pid::click (i, "level"))->load()
                + modulation.getValue (ModulationMatrix::Target::ClickLevel));
        // 调制：Click 衰减。按 ±150ms 映射，一条路由推动全部 4 层。
        clickDecays[(size_t) i] = juce::jlimit (0.001f, 2.0f,
            apvts.getRawParameterValue (pid::click (i, "decay"))->load() * 0.001f
                + modulation.getValue (ModulationMatrix::Target::ClickDecay) * 0.15f);
        // 调制：Click 低通（倍频程）
        clickLp[(size_t) i]     = juce::jlimit (20.0f, 20000.0f,
            apvts.getRawParameterValue (pid::click (i, "lp"))->load()
                * std::pow (2.0f, modulation.getValue (
                      ModulationMatrix::Target::ClickLp) * 2.0f));
        // 调制：Click 高通。倍频程缩放（同 Sub 滤波的理由）。
        clickHp[(size_t) i]     = juce::jlimit (20.0f, 20000.0f,
            apvts.getRawParameterValue (pid::click (i, "hp"))->load()
                * std::pow (2.0f, modulation.getValue (
                      ModulationMatrix::Target::ClickHp) * 2.0f));
        // 调制：采样区间（归一化 0..1）与渐入
        clickStart[(size_t) i]  = juce::jlimit (0.0f, 1.0f,
            apvts.getRawParameterValue (pid::click (i, "start"))->load()
                + modulation.getValue (ModulationMatrix::Target::ClickStart) * 0.5f);
        clickEnd[(size_t) i]    = juce::jlimit (0.0f, 1.0f,
            apvts.getRawParameterValue (pid::click (i, "end"))->load()
                + modulation.getValue (ModulationMatrix::Target::ClickEnd) * 0.5f);
        // ⚠️ 单位：参数是 **ms**（0..20），这里**保持 ms 传下去**。
        //
        // ClickSection::render 内部会 *0.001 转成秒（见 ClickLayer.cpp:325）。
        // 我一开始在这里也乘了 0.001，结果转了两次 ——
        // 10ms 变成 0.01ms，淡入完全消失（实测 0 和 10ms 输出一模一样）。
        //
        // 调制幅度按 ±5ms 给：ms 量程只有 0..20，
        // 按比例算 ±5ms 已经能明显改变音头软化程度。
        clickFadeIn[(size_t) i] = juce::jlimit (0.0f, 20.0f,
            apvts.getRawParameterValue (pid::click (i, "fadein"))->load()
                + modulation.getValue (ModulationMatrix::Target::ClickFadeIn) * 5.0f);
        clickEnvs[(size_t) i]   = &envReaders[(size_t) EnvelopeTarget::Click1Amp + i];
    }

    //=== 滑音推进（逐采样把 Sub 的频率滑向目标）===============================
    // 由于 SubSynth 内部按块取一个频率，这里在块开始时把滑音推进到块末的值，
    // 并在块内做线性插值 —— 实际上 SubSynth 只接受一个固定频率，
    // 所以滑音以「块」为粒度更新，块长通常很小（<10ms），听感足够平滑。
    if (glidePending && glideTotalSamples > 0.0)
    {
        glideSamples += numSamples;
        const float t = (float) juce::jlimit (0.0, 1.0, glideSamples / glideTotalSamples);

        // 滑音曲线：
        //   curve = 0 -> 线性（等音程匀速）
        //   curve = 1 -> 指数（先快后慢，更接近模拟合成器的滑音手感）
        // 之前 Glide Curve 参数只声明没实现，是个死参数。
        const float curve = juce::jlimit (0.0f, 1.0f,
                              apvts.getRawParameterValue (pid::subGlideCurve)->load());
        const float shaped = t * ((1.0f - curve) + curve * t);   // 线性 -> 二次

        // 用等音程插值（对数域），听感上是匀速滑音
        const float logStart = std::log2 (juce::jmax (1.0f, glideStartFreq));
        const float logEnd   = std::log2 (juce::jmax (1.0f, glideTargetFreq));
        const float freq = std::pow (2.0f, logStart + (logEnd - logStart) * shaped);

        currentFrequency.store (freq);

        if (t >= 1.0f)
        {
            glidePending = false;
            currentFrequency.store (glideTargetFreq);
        }
    }
    else
    {
        currentFrequency.store (glideTargetFreq);
    }

    //=== 渲染 Sub =============================================================
    if (subOn)
    {
        subSynth.render (buffer, 0, numSamples,
                         envReaders[(size_t) EnvelopeTarget::SubPitch],
                         envReaders[(size_t) EnvelopeTarget::SubAmp],
                         pitchAmt,
                         subLevel,
                         subDecay,
                         subPitchDec,
                         subPunch,
                         currentFrequency.load(),
                         harmonics,
                         harmonicLevels,
                         subHarm,
                         subHarmOn,
                         // 调制：滤波截止频率。
                         // 用**倍频程**缩放而不是线性加减 ——
                         // 听感上频率是对数的，线性加法在低频几乎听不出、
                         // 在高频又过于剧烈。±2 个八度是明显的效果范围。
                         juce::jlimit (30.0f, 20000.0f,
                             apvts.getRawParameterValue (pid::subFilterHz)->load()
                                 * std::pow (2.0f, modulation.getValue (
                                       ModulationMatrix::Target::SubFilterFreq) * 2.0f)),
                         // 调制：滤波 Q（0.5..8）与滤波包络量（-1..1）
                         juce::jlimit (0.5f, 8.0f,
                             apvts.getRawParameterValue (pid::subFilterQ)->load()
                                 + modulation.getValue (ModulationMatrix::Target::SubFilterQ) * 4.0f),
                         juce::jlimit (-1.0f, 1.0f,
                             apvts.getRawParameterValue (pid::subFilterEnv)->load()
                                 + modulation.getValue (ModulationMatrix::Target::SubFilterEnv)),
                         // 调制：音高曲线 / 保持段
                         juce::jlimit (0.0f, 1.0f,
                             apvts.getRawParameterValue (pid::subPitchCurve)->load()
                                 + modulation.getValue (ModulationMatrix::Target::SubPitchCurve)),
                         juce::jlimit (0.0f, 2.0f,
                             apvts.getRawParameterValue (pid::subHold)->load() * 0.001f
                                 + modulation.getValue (ModulationMatrix::Target::SubHold) * 0.05f),
                         // 调制：宽度 / 噪声量 / 噪声高通
                         juce::jlimit (0.0f, 2.0f,
                             apvts.getRawParameterValue (pid::subWidth)->load()
                                 + modulation.getValue (ModulationMatrix::Target::SubWidth)),
                         juce::jlimit (0.0f, 1.0f,
                             apvts.getRawParameterValue (pid::subNoiseAmt)->load()
                                 + modulation.getValue (ModulationMatrix::Target::SubNoiseAmt)),
                         // 调制：噪声衰减（±300ms）
                         juce::jlimit (0.001f, 4.0f,
                             apvts.getRawParameterValue (pid::subNoiseDecay)->load() * 0.001f
                                 + modulation.getValue (ModulationMatrix::Target::SubNoiseDecay) * 0.3f),
                         // 噪声高通也用倍频程缩放（同滤波频率的理由）
                         juce::jlimit (20.0f, 8000.0f,
                             apvts.getRawParameterValue (pid::subNoiseHp)->load()
                                 * std::pow (2.0f, modulation.getValue (
                                       ModulationMatrix::Target::SubNoiseHp) * 2.0f)),
                         // 调制：起始渐入（±50ms）
                         juce::jlimit (0.0f, 1.0f,
                             apvts.getRawParameterValue (pid::subAttack)->load() * 0.001f
                                 + modulation.getValue (ModulationMatrix::Target::SubAttack) * 0.05f),
                         (int) std::lround (apvts.getRawParameterValue (pid::subFilterType)->load()),
                         // 调制：失谐（0..50 cent）
                         juce::jlimit (0.0f, 50.0f,
                             apvts.getRawParameterValue (pid::subDetune)->load()
                                 + modulation.getValue (ModulationMatrix::Target::SubDetune) * 25.0f),
                         // 调制：相位随机 / 音高延迟 / 第二阶段
                         juce::jlimit (0.0f, 1.0f,
                             apvts.getRawParameterValue (pid::subPhaseRand)->load()
                                 + modulation.getValue (ModulationMatrix::Target::SubPhaseRand)),
                         juce::jlimit (0.0f, 1.0f,
                             apvts.getRawParameterValue (pid::subPitchDelay)->load() * 0.001f
                                 + modulation.getValue (ModulationMatrix::Target::SubPitchDelay) * 0.05f),
                         juce::jlimit (0.0f, 1.0f,
                             apvts.getRawParameterValue (pid::subStage2Amt)->load()
                                 + modulation.getValue (ModulationMatrix::Target::SubStage2Amt)),
                         juce::jlimit (0.0f, 4.0f,
                             apvts.getRawParameterValue (pid::subStage2Time)->load()
                                 + modulation.getValue (ModulationMatrix::Target::SubStage2Time) * 0.3f));
    }

    //=== 渲染 Click 层 ========================================================
    clicks.render (buffer, 0, numSamples,
                   clickEnvs, clickLevels, clickDecays, clickLp, clickHp, clickOn,
                   clickStart, clickEnd, clickFadeIn);



    //=== 瞬态整形 =============================================================
    //
    // 原理：用两条快/慢包络跟随器分别追踪"起音"和"延音"，
    // 两个增益相减得到瞬态量，再按 Attack / Sustain 旋钮分别加权。
    //   Attack  > 0 -> 起音更突出（punch 更强）
    //   Attack  < 0 -> 起音变柔
    //   Sustain > 0 -> 延音更饱满（尾巴更长）
    //   Sustain < 0 -> 延音更干
    if (apvts.getRawParameterValue (pid::transOn)->load() > 0.5f)
    {
        // 调制：瞬态整形的起音/延音强度
        const float attAmt = juce::jlimit (-1.0f, 1.0f,
            apvts.getRawParameterValue (pid::transAttack)->load()
                + modulation.getValue (ModulationMatrix::Target::TransAttack));
        const float susAmt = juce::jlimit (-1.0f, 1.0f,
            apvts.getRawParameterValue (pid::transSustain)->load()
                + modulation.getValue (ModulationMatrix::Target::TransSustain));


        // 时间常数针对底鼓尺度选取：
        //   快包络 0.5ms  —— 追踪起音的尖锐程度
        //   慢包络 12ms   —— 代表"身体"部分的能量
        // 原来用 1ms/60ms：60ms 对底鼓太长，两条包络几乎同步，
        // 差值恒为 0，整形完全不起作用。
        constexpr float kFastMs = 0.5f;
        constexpr float kSlowMs = 12.0f;
        // 必须用 currentSampleRate 而不是 getSampleRate()：
        // 在离线/无宿主的渲染路径里 getSampleRate() 会返回 0，
        // 导致两个系数都变成 0、包络退化成"直接跟随输入"，
        // 瞬态检测彻底失效（这个 bug 让整形看起来完全没反应）。
        const float srF = (float) (currentSampleRate > 0.0 ? currentSampleRate : 44100.0);
        const float fastCoeff = std::exp (-1.0f / (kFastMs * 0.001f * srF));
        const float slowCoeff = std::exp (-1.0f / (kSlowMs * 0.001f * srF));


        // 两个关键点，缺一个都会导致数值爆炸：
        //
        //  1) 检测必须在**施加增益之前**取值。
        //     如果先放大再检测，下一拍的包络会看到被放大的信号，
        //     形成正反馈，几百个采样内就冲到 1e38。
        //
        //  2) 检测信号必须与输出解耦 —— 这里先把整块的原始检测值
        //     取出来，再统一施加增益。
        for (int i = 0; i < numSamples; ++i)
        {
            // 用左右声道的较大值作为检测信号（原始、未加增益）
            float detect = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
                detect = juce::jmax (detect, std::abs (buffer.getSample (ch, i)));

            transFastEnv = fastCoeff * transFastEnv + (1.0f - fastCoeff) * detect;
            transSlowEnv = slowCoeff * transSlowEnv + (1.0f - slowCoeff) * detect;

            // 瞬态量：快包络高于慢包络说明是"起音"，反之为"延音"。
            // 用慢包络归一化，这样增益与绝对音量无关（轻弹重弹效果一致）。
            const float diff = transFastEnv - transSlowEnv;
            const float transient = juce::jlimit (-1.0f, 1.0f,
                                                  diff / juce::jmax (1.0e-4f, transSlowEnv));

            // Attack 作用在正向瞬态（起音），Sustain 作用在负向（延音）
            float gain = 1.0f;
            if (transient > 0.0f)
                gain += transient * attAmt;
            else
                gain += transient * susAmt;


            // 增益上限压到 1.8（约 +5dB），并且不允许为负
            gain = juce::jlimit (0.0f, 1.8f, gain);

            for (int ch = 0; ch < numChannels; ++ch)
                buffer.getWritePointer (ch)[i] *= gain;
        }

    }

    //=== 失真 =================================================================
    const bool distOn = apvts.getRawParameterValue (pid::distOn)->load() > 0.5f;
    if (distOn)
    {
        const int typeIdx = (int) apvts.getRawParameterValue (pid::distType)->load();
        const auto distType = (DistortionType) juce::jlimit (0, (int) DistortionType::NumTypes - 1, typeIdx);
        // 调制：失真驱动量。量程 1..20，归一化调制按 ±10 映射。
        const float drive = juce::jlimit (
            1.0f, 20.0f,
            apvts.getRawParameterValue (pid::distDrive)->load()
                + modulation.getValue (ModulationMatrix::Target::DistDrive) * 10.0f);
        const float mix   = juce::jlimit (0.0f, 1.0f,
            apvts.getRawParameterValue (pid::distMix)->load()
                + modulation.getValue (ModulationMatrix::Target::DistMix));
        const float bias  = juce::jlimit (-0.5f, 0.5f,
            apvts.getRawParameterValue (pid::distBias)->load()
                + modulation.getValue (ModulationMatrix::Target::DistBias) * 0.5f);
        const float tone  = juce::jlimit (0.0f, 1.0f,
            apvts.getRawParameterValue (pid::distTone)->load()
                + modulation.getValue (ModulationMatrix::Target::DistTone));
        const bool  autoG = apvts.getRawParameterValue (pid::distAutoGain)->load() > 0.5f;

        // 过采样因子：1（关）/ 2 / 4
        const bool osOn = apvts.getRawParameterValue (pid::oversample)->load() > 0.5f;
        const int wantFactor = osOn ? 2 : 1;

        bool didOversample = false;

        // ⚠️ 这里原来写的是 numSamples <= oversampler->getOversamplingFactor()，
        // 把**块大小**（512）和**倍率**（2 或 4）比较 —— 永远为 false，
        // 导致过采样**从未真正执行**（开关形同虚设，实测无任何差异）。
        //
        // 正确的判断是"块大小不超过预分配的容量"。
        if (osOn && oversampler != nullptr
            && numSamples <= oversampleBuffer.getNumSamples())
        {
            juce::ignoreUnused (wantFactor);
            // 失真产生的高次谐波会折返成混叠，先升采样再失真能显著改善。
            juce::dsp::AudioBlock<float> block (buffer);
            auto sub = block.getSubBlock (0, (size_t) numSamples);

            // 升采样：返回的块指向 oversampler 的内部缓冲，可直接就地处理
            auto osBlock = oversampler->processSamplesUp (sub);

            const int osNumSamples = (int) osBlock.getNumSamples();
            const int osNumCh = (int) osBlock.getNumChannels();

            if (osNumSamples > 0 && osNumCh > 0)
            {
                // 用 AudioBuffer 视图包装这块内存，交给失真处理（零拷贝）
                float* channelPtrs[2] = { nullptr, nullptr };
                const int chansToUse = juce::jmin (osNumCh, 2);
                for (int ch = 0; ch < chansToUse; ++ch)
                    channelPtrs[ch] = osBlock.getChannelPointer ((size_t) ch);

                juce::AudioBuffer<float> osView (channelPtrs, chansToUse, osNumSamples);
                distortion.process (osView, 0, osNumSamples, distType, drive, mix);
            }

            oversampler->processSamplesDown (sub);
            didOversample = true;
        }

        if (! didOversample)
        {
            distortion.process (buffer, 0, numSamples, distType, drive, mix, bias, tone, autoG);
        }
    }

    //=== 电平表 / 波形显示数据 ================================================
    {
        float peak = 0.0f;
        for (int ch = 0; ch < numChannels; ++ch)
            peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, numSamples));

        meterPeak.store (peak);
        meterGr.store (masterChain.getGainReductionDb());
    }

    //=== EQ ===================================================================
    {
        static const Equalizer::BandType bandTypes[kNumEqBands]
        {
            Equalizer::BandType::HighPass,   // 20Hz   —— 清次低频
            Equalizer::BandType::Peak,       // 40Hz
            Equalizer::BandType::Peak,       // 70Hz
            Equalizer::BandType::Peak,       // 120Hz
            Equalizer::BandType::Peak,       // 250Hz
            Equalizer::BandType::Peak,       // 600Hz
            Equalizer::BandType::Peak,       // 2.5kHz
            Equalizer::BandType::HighShelf   // 8kHz
        };

        bool anyEq = false;
        for (int b = 0; b < kNumEqBands; ++b)
        {
            const bool on = apvts.getRawParameterValue (pid::eq (b, "on"))->load() > 0.5f;
            anyEq = anyEq || on;

            // --- 增益平滑 -----------------------------------------------------
            // 系数突变会让双二阶滤波器产生阶跃暂态。单段 +18dB 突变的瞬态
            // 已达稳态的 1.7 倍；8 段同时突变会叠加成十几倍的尖峰（实测 10.5），
            // 听感上就是"砰"的一声。
            // 这里把增益按块做一阶平滑，突变被摊到多块里，暂态就消失了。
            // 频率与 Q 不参与平滑：它们的突变只改变音色、不产生能量跳变。
            float targetGain = apvts.getRawParameterValue (pid::eq (b, "gain"))->load();

            // 调制：EQ 各段的增益可以被任意调制源推动。
            //
            // 量程映射：归一化调制值(-1..1) × 24dB。
            // 用 24 而不是 18（参数本身的量程）是刻意的 ——
            // 调制应该能推到参数手调达不到的极端，否则"音头猛抬一下"
            // 这类效果做不出来。最终由 Equalizer 内部钳制到 ±24dB。
            const auto eqTarget = (ModulationMatrix::Target) ((int) ModulationMatrix::Target::Eq1Gain + b);
            targetGain += modulation.getValue (eqTarget) * 24.0f;

            eqSmoothedGain[b] += eqGainSmoothCoeff * (targetGain - eqSmoothedGain[b]);
            const float gain = eqSmoothedGain[b];

            // slope 参数存的是索引（0..3），换算成 dB/oct
            const int slopeIdx = (int) std::lround (
                apvts.getRawParameterValue (pid::eq (b, "slope"))->load());
            const int slopeDbPerOct = (juce::jlimit (0, 3, slopeIdx) + 1) * 12;

            // 调制：EQ 频率（倍频程缩放）与 Q（±3）
            const auto freqTarget = (ModulationMatrix::Target)
                ((int) ModulationMatrix::Target::Eq1Freq + b);
            const auto qTarget = (ModulationMatrix::Target)
                ((int) ModulationMatrix::Target::Eq1Q + b);

            const float bandFreq = juce::jlimit (10.0f, 24000.0f,
                apvts.getRawParameterValue (pid::eq (b, "freq"))->load()
                    * std::pow (2.0f, modulation.getValue (freqTarget) * 2.0f));

            const float bandQ = juce::jlimit (0.1f, 12.0f,
                apvts.getRawParameterValue (pid::eq (b, "q"))->load()
                    + modulation.getValue (qTarget) * 3.0f);

            eq.setBandParams (b, bandFreq, gain, bandQ,
                              bandTypes[b],
                              on,
                              slopeDbPerOct);
        }

        if (anyEq)
            eq.process (buffer, 0, numSamples);
    }

    //=== EQ 之后清除直流与滤波器暂态 ==========================================
    // 这里必须放在 EQ **之后**。原因：
    //  dc 有两个来源 —— (1) 输入信号里的直流会被 shelf 滤波器的标称增益放大；
    //  (2) 更主要的是，参数每块都在变时，双二阶滤波器的系数突变会激发
    //      巨大的阶跃暂态（实测限幅关闭时可达 9 万）。
    //  这两者都产生在 EQ 内部，所以只有在 EQ 之后才拦得住。
    //  截止频率取 20Hz：底鼓最低基频约 30-50Hz，20Hz 高通不会伤到有用低频，
    //  但能在几个毫秒内把直流压下去。
    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* d = buffer.getWritePointer (ch, 0);
        for (int i = 0; i < numSamples; ++i)
            d[i] = postEqDcBlockers[(size_t) juce::jmin (ch, 1)].process (d[i]);
    }

    // [TEMP DEBUG] 打印各阶段峰值
    //=== 总输出增益 ===========================================================
    // 注意顺序：Master Gain 必须在压缩/限幅**之前**。
    // 早期版本把它放在限幅器之后，结果用户把 Master 推到 +12dB 时输出峰值达到 3.85
    // —— 限幅器完全失效，直接爆掉宿主的输出。
    // 现在限幅器是信号链的最后一环，真正做到"输出不越界"。
    // 调制：总输出。按 ±12dB 映射。
    const float masterDb = juce::jlimit (-24.0f, 12.0f,
        apvts.getRawParameterValue (pid::masterGain)->load()
            + modulation.getValue (ModulationMatrix::Target::MasterGain) * 12.0f);
    if (std::abs (masterDb) > 1.0e-4f)
        buffer.applyGain (juce::Decibels::decibelsToGain (masterDb));

    //=== 压缩 / 限幅（信号链最后一环）==========================================
    // 调制：压缩阈值 / 补偿增益 / 限幅上限。
    // 量程映射按各自参数的物理范围（12dB / 12dB / 6dB）。
    masterChain.process (buffer, 0, numSamples,
                         apvts.getRawParameterValue (pid::compOn)->load() > 0.5f,
                         juce::jlimit (-40.0f, 0.0f,
                             apvts.getRawParameterValue (pid::compThresh)->load()
                                 + modulation.getValue (ModulationMatrix::Target::CompThresh) * 12.0f),
                         // 调制：压缩比 / 起音 / 释放
                         juce::jlimit (1.0f, 20.0f,
                             apvts.getRawParameterValue (pid::compRatio)->load()
                                 + modulation.getValue (ModulationMatrix::Target::CompRatio) * 4.0f),
                         juce::jlimit (0.0f, 200.0f,
                             apvts.getRawParameterValue (pid::compAttack)->load()
                                 + modulation.getValue (ModulationMatrix::Target::CompAttack) * 50.0f),
                         juce::jlimit (1.0f, 2000.0f,
                             apvts.getRawParameterValue (pid::compRelease)->load()
                                 + modulation.getValue (ModulationMatrix::Target::CompRelease) * 200.0f),
                         juce::jlimit (0.0f, 24.0f,
                             apvts.getRawParameterValue (pid::compMakeup)->load()
                                 + modulation.getValue (ModulationMatrix::Target::CompMakeup) * 12.0f),
                         apvts.getRawParameterValue (pid::limOn)->load() > 0.5f,
                         juce::jlimit (-12.0f, 0.0f,
                             apvts.getRawParameterValue (pid::limCeiling)->load()
                                 + modulation.getValue (ModulationMatrix::Target::LimCeiling) * 6.0f),
                         // 调制：侧链高通（倍频程）/ 前瞻
                         juce::jlimit (20.0f, 2000.0f,
                             apvts.getRawParameterValue (pid::compScHp)->load()
                                 * std::pow (2.0f, modulation.getValue (
                                       ModulationMatrix::Target::CompScHp) * 2.0f)),
                         juce::jlimit (0.0f, 20.0f,
                             apvts.getRawParameterValue (pid::compLookahead)->load()
                                 + modulation.getValue (ModulationMatrix::Target::CompLookahead) * 5.0f));

    //=== 波形捕获（供波形显示器用）===========================================
    // 捕获策略：一次触发后，把头 kWaveCaptureSize 个输出采样存下来。
    // 为了让 Sub 与各 Click 层能「分开显示」，这里额外渲染每个层的独立贡献：
    // 第一次触发时（且尚未开始捕获）用一份临时 buffer 分层渲染一遍。
    if (needsWaveCapture)
    {
        captureRender (numSamples,
                       subOn, subLevel, subDecay, pitchAmt, subPitchDec, subPunch, harmonics,
                       subHarm, subHarmOn, harmonicLevels,
                       clickEnvs, clickLevels, clickDecays, clickLp, clickHp, clickOn);

        if (waveCaptureFill[0] >= kWaveCaptureSize)
            needsWaveCapture = false;
    }
}

//==============================================================================
// 分层波形捕获
//
// 只在插件编辑器打开时才会被调用：把 Sub 和三个 Click 层分别渲染到独立缓冲，
// 这样波形显示器可以把它们叠加显示并单独开关 —— 这是对齐相位的关键。
void KickForgeProcessor::captureRender (int numSamples,
                                        bool subOn, float subLevel, float subDecay, float pitchAmt,
                                        float subPitchDec, float subPunch,
                                        int harmonics, float subHarm, bool subHarmOn,
                                        const std::array<float, SubSynth::kMaxHarmonics>& harmonicLevels,
                                        const std::array<const EnvelopeReader*, ClickSection::kNumLayers>& clickEnvs,
                                        const std::array<float, ClickSection::kNumLayers>& clickLevels,
                                        const std::array<float, ClickSection::kNumLayers>& clickDecays,
                                        const std::array<float, ClickSection::kNumLayers>& clickLp,
                                        const std::array<float, ClickSection::kNumLayers>& clickHp,
                                        const std::array<bool, ClickSection::kNumLayers>& clickOn)
{
    // 每层用一份干净的临时缓冲
    juce::AudioBuffer<float> tmp (1, numSamples);

    // --- Sub 层
    if (subOn)
    {
        tmp.clear();
        captureSub.noteOn (currentFrequency.load(), lastVelocity);
        captureSub.render (tmp, 0, numSamples,
                           envReaders[(size_t) EnvelopeTarget::SubPitch],
                           envReaders[(size_t) EnvelopeTarget::SubAmp],
                           pitchAmt, subLevel, subDecay, subPitchDec, subPunch,
                           currentFrequency.load(),
                           harmonics, harmonicLevels, subHarm, subHarmOn,
                           20000.0f, 0.707f, 0.0f,
                           0.0f, 0.0f, 0.0f, 0.0f, 0.012f, 400.0f,
                           0.0f, 0, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f);  // 捕获用干声
        appendCapture (0, tmp.getReadPointer (0), numSamples);
    }

    // --- Click 层：逐层单独渲染
    for (int i = 0; i < kNumClickLayers; ++i)
    {
        if (! clickOn[(size_t) i] || clickEnvs[(size_t) i] == nullptr)
            continue;

        tmp.clear();
        auto& cl = captureClicks.getLayer (i);
        if (! cl.isLoaded())
            continue;

        cl.noteOn (lastVelocity, 1.0f);
        cl.render (tmp, 0, numSamples, *clickEnvs[(size_t) i],
                   clickLevels[(size_t) i], clickDecays[(size_t) i],
                   clickLp[(size_t) i], clickHp[(size_t) i], true);
        appendCapture (i + 1, tmp.getReadPointer (0), numSamples);
    }
}

void KickForgeProcessor::appendCapture (int channel, const float* data, int numSamples)
{
    if (channel < 0 || channel >= 4)
        return;

    auto& dst = waveCapture[(size_t) channel];
    int& fill = waveCaptureFill[channel];

    const int n = juce::jmin (numSamples, kWaveCaptureSize - fill);
    for (int i = 0; i < n; ++i)
        dst[(size_t) (fill + i)] = data[i];

    fill += n;
}

//==============================================================================

void KickForgeProcessor::notifySampleChanged()
{
    // 采样载入是在编辑器（消息）线程完成的，而 clicks 的渲染在音频线程。
    // ClickLayer::loadFromBuffer 会重建内部的 sampleData buffer，
    // 若此时音频线程正在读，就会读到已释放的内存。
    //
    // 这里用 suspendProcessing 把音频回调暂停一小段时间，等 in-flight 的
    // processBlock 走完再返回，从而保证载入期间没有并发访问。
    suspendProcessing (true);
    suspendProcessing (false);

    // 触发一次捕获，让波形显示立即反映新采样
    requestWaveCapture();
}

void KickForgeProcessor::copyWaveCapturesTo (WaveDisplay& display)
{
    // 把 4 个通道分别交给显示器（显示器内部做峰值压缩并叠加绘制）
    for (int i = 0; i < 4; ++i)
    {
        const int n = waveCaptureFill[(size_t) i];
        if (n > 0)
            display.captureSource (i, waveCapture[(size_t) i].data(), n);
    }
}

//==============================================================================

//==============================================================================
// 状态序列化
//
// 提取成两个可复用的方法：getStateAsValueTree / setStateFromValueTree。
// get/setStateInformation（宿主保存工程用）与预设系统（PresetManager）共用这条路径，
// 避免两套序列化代码各写一遍、日后改一处漏一处。
//==============================================================================


//==============================================================================
// 撤销 / 重做
//==============================================================================

void KickForgeProcessor::clearUndoHistory()
{
    undoStack.clear();
    undoPos = -1;

    // 以当前状态作为基准（栈底）
    undoStack.push_back (getStateAsValueTree().createCopy());
    undoPos = 0;
}

void KickForgeProcessor::pushUndoSnapshot()
{
    auto snap = getStateAsValueTree().createCopy();
    if (! snap.isValid())
        return;

    // 栈还没初始化时先建立基准
    if (undoStack.empty())
    {
        undoStack.push_back (snap);
        undoPos = 0;
        return;
    }

    // 与当前项相同就不重复入栈（避免每帧都产生一个撤销点）
    if (undoPos >= 0 && undoPos < (int) undoStack.size())
    {
        // 直接比较序列化结果，简单可靠
        if (undoStack[(size_t) undoPos].isEquivalentTo (snap))
            return;
    }

    // 丢弃"重做"分支
    if (undoPos + 1 < (int) undoStack.size())
        undoStack.erase (undoStack.begin() + (undoPos + 1), undoStack.end());

    undoStack.push_back (snap);

    // 容量限制
    if ((int) undoStack.size() > kMaxUndoSteps)
        undoStack.erase (undoStack.begin());

    undoPos = (int) undoStack.size() - 1;
}

bool KickForgeProcessor::undo()
{
    if (! canUndo())
        return false;

    --undoPos;

    auto st = undoStack[(size_t) undoPos].createCopy();
    if (! st.isValid())
        return false;

    setStateFromValueTree (st);
    return true;
}

bool KickForgeProcessor::redo()
{
    if (! canRedo())
        return false;

    ++undoPos;

    auto st = undoStack[(size_t) undoPos].createCopy();
    if (! st.isValid())
        return false;

    setStateFromValueTree (st);
    return true;
}

juce::ValueTree KickForgeProcessor::getStateAsValueTree()
{
    juce::ValueTree state = apvts.copyState();

    // 把 5 条包络的节点数据一并存进状态树
    juce::ValueTree envTree ("ENVELOPES");
    const char* names[(size_t) EnvelopeTarget::NumTargets]
        {
            "subPitch", "subAmp", "click1Amp", "click2Amp", "click3Amp", "click4Amp" };

    for (size_t i = 0; i < envelopes.size(); ++i)
    {
        juce::ValueTree e ("ENV");
        e.setProperty ("target", names[i], nullptr);
        e.setProperty ("data", envelopes[i].toString(), nullptr);
        envTree.appendChild (e, nullptr);
    }
    state.appendChild (envTree, nullptr);

    // 采样：记录三层载入的采样名（内置库靠名字恢复）
    juce::ValueTree smpTree ("SAMPLES");
    for (int i = 0; i < kNumClickLayers; ++i)
    {
        juce::ValueTree s ("S");
        s.setProperty ("layer", i, nullptr);
        s.setProperty ("name", clicks.getLayer (i).getLoadedName(), nullptr);
        smpTree.appendChild (s, nullptr);
    }
    state.appendChild (smpTree, nullptr);

    // 调制矩阵：路由 + 通用包络 + LFO 设置
    //
    // 存成**一个字符串属性**而不是展开成子树 ——
    // ModulationMatrix::toString() 已经是完整的 JSON，
    // 展开成 ValueTree 只会增加两边的维护成本。
    {
        juce::ValueTree modTree ("MODULATION");
        modTree.setProperty ("data", modulation.toString(), nullptr);
        state.appendChild (modTree, nullptr);
    }

    return state;
}

void KickForgeProcessor::setStateFromValueTree (const juce::ValueTree& state)
{
    if (! state.isValid())
        return;

    apvts.replaceState (state);

    // --- 恢复包络
    auto envTree = state.getChildWithName ("ENVELOPES");
    if (envTree.isValid())
    {
        const char* names[(size_t) EnvelopeTarget::NumTargets]
            {
            "subPitch", "subAmp", "click1Amp", "click2Amp", "click3Amp", "click4Amp" };

        for (int i = 0; i < envTree.getNumChildren(); ++i)
        {
            auto e = envTree.getChild (i);
            const auto target = e.getProperty ("target").toString();
            const auto dataStr = e.getProperty ("data").toString();

            for (size_t t = 0; t < envelopes.size(); ++t)
            {
                if (target == names[t])
                {
                    envelopes[t].fromString (dataStr);
                    commitEnvelope ((EnvelopeTarget) t);
                    break;
                }
            }
        }
    }

    // --- 恢复调制矩阵
    //
    // 放在 EQ 平滑器对齐**之前**：调制会改变 EQ 目标值，
    // 顺序反了的话平滑器会对齐到未调制值，载入后有一小段过渡。
    auto modTree = state.getChildWithName ("MODULATION");
    if (modTree.isValid())
    {
        const auto data = modTree.getProperty ("data").toString();
        if (data.isNotEmpty())
            modulation.fromString (data);
    }

    // 参数已被整体替换，把 EQ 平滑器直接对齐到目标值，
    // 否则载入预设后 EQ 会从旧值"滑"过去，产生一段可听见的过渡。
    for (int b = 0; b < kNumEqBands; ++b)
        eqSmoothedGain[b] = rawGainFor (b);

    // --- 恢复 Click 层采样（按名字在内置库里找）
    auto smpTree = state.getChildWithName ("SAMPLES");
    if (smpTree.isValid())
    {
        for (int i = 0; i < smpTree.getNumChildren(); ++i)
        {
            auto s = smpTree.getChild (i);
            const int layer = (int) s.getProperty ("layer");
            const auto name = s.getProperty ("name").toString();

            if (layer < 0 || layer >= kNumClickLayers || name.isEmpty())
                continue;

            for (int k = 0; k < library.getNumSamples(); ++k)
            {
                if (library.getEntry (k).name == name)
                {
                    library.loadIntoLayer (k, clicks.getLayer (layer));
                    break;
                }
            }
        }
    }
}

void KickForgeProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = getStateAsValueTree();
    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    copyXmlToBinary (*xml, destData);
}

void KickForgeProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    // 必须先判空：JUCE 8.0.4 的 AudioProcessor::getXmlFromBinary 在
    // sizeInBytes > 8 时会直接解引用 data 而**不做 null 检查**
    // （见 juce_AudioProcessor.cpp:955  ByteOrder::littleEndianInt(data)）。
    // 宿主或损坏的工程文件传入 (nullptr, N>8) 会直接段错误。
    if (data == nullptr || sizeInBytes <= 0)
        return;

    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml == nullptr)
        return;

    auto state = juce::ValueTree::fromXml (*xml);
    if (! state.isValid())
        return;

    setStateFromValueTree (state);
}

} // namespace kf

//==============================================================================
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new kf::KickForgeProcessor();
}
