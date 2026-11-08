#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_dsp/juce_dsp.h>
#include <array>

#include "Params.h"
#include "Envelope.h"
#include "ModulationMatrix.h"
#include "SubSynth.h"
#include "ClickLayer.h"
#include "Distortion.h"
#include "MasterChain.h"
#include "Equalizer.h"
#include "SampleLibrary.h"
#include "PresetManager.h"
#include "AiApplier.h"
#include "DspUtils.h"

namespace kf
{

//==============================================================================
/** 包络的两种角色（Pitch / Amp），每种在 Sub 和 3 个 Click 层上各有一份。 */
enum class EnvelopeTarget
{
    SubPitch = 0,
    SubAmp,
    Click1Amp,
    Click2Amp,
    Click3Amp,
    Click4Amp,
    NumTargets
};

/** Click 层的包络数量（与 kNumClickLayers 一致）。 */
static_assert ((int) EnvelopeTarget::Click4Amp - (int) EnvelopeTarget::Click1Amp + 1
               == kNumClickLayers,
               "EnvelopeTarget 的 Click 包络数量必须与 kNumClickLayers 一致，"
               "否则 clickEnvs[] 会越界读取");

//==============================================================================
class KickForgeProcessor : public juce::AudioProcessor
{
public:
    KickForgeProcessor();
    ~KickForgeProcessor() override;

    //==========================================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    //==========================================================================
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Kick Cat"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 2.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    //==========================================================================
    /** 编辑器访问点。 */
    juce::AudioProcessorValueTreeState& getAPVTS() noexcept { return apvts; }

    Envelope& getEnvelope (EnvelopeTarget t) { return envelopes[(size_t) t]; }

    /** 编辑器改完包络后调用，把新曲线提交给音频线程。 */
    void commitEnvelope (EnvelopeTarget t);

    SampleLibrary& getSampleLibrary() noexcept { return library; }

    /** 取得某个 Click 层（供编辑器载入采样）。 */
    ClickLayer& getClickLayerRef (int layer)
    {
        return clicks.getLayer (juce::jlimit (0, kNumClickLayers - 1, layer));
    }

    /** 采样载入后由编辑器调用：让音频线程的引擎同步到新采样。 */
    void notifySampleChanged();

    /** 当前采样率（编辑器初始化采样库时用）。 */
    double getCurrentSampleRate() const noexcept { return currentSampleRate; }

    //==========================================================================
    // 状态序列化（宿主保存工程 与 预设系统 共用同一条路径）
    //
    // 之所以提取成两个方法：预设保存/载入需要与工程保存完全一致的语义，
    // 若各写一份，日后新增参数很容易漏掉其中一处。
    juce::ValueTree getStateAsValueTree();
    void setStateFromValueTree (const juce::ValueTree& state);

    PresetManager& getPresetManager() noexcept { return *presetManager; }

    /** AI 参数应用器（由 processor 持有，这样编辑器重建后过渡不会丢）。 */
    AiApplier& getAiApplier() noexcept { return *aiApplier; }

    //==========================================================================
    // 撤销 / 重做（快照式）
    //
    // 为什么不用 juce::UndoManager：
    // APVTS 的撤销依赖"扁平属性"，只有当 state 树里**已存在**同名属性时
    // 才会记录事务（见 ValueTreeState 的 flushToTree）。而我们的参数是
    // JUCE 8 的 PARAM 子节点格式，走的是 um = nullptr 的分支，
    // 因此 UndoManager 永远收不到任何记录（实测 canUndo() 恒为 false）。
    //
    // 改为自己存状态快照：
    //   - 每次参数变化后（防抖）把整棵状态树压栈
    //   - 撤销/重做即弹栈 + replaceState
    //   - 好处是参数、包络、采样名一视同仁，也能覆盖预设载入
    //==========================================================================

    /** 记录一个撤销点（内部会做去重与容量限制）。 */
    void pushUndoSnapshot();

    bool canUndo() const noexcept { return undoPos > 0; }
    bool canRedo() const noexcept { return undoPos + 1 < (int) undoStack.size(); }

    /** 撤销 / 重做。返回是否真的执行了。 */
    bool undo();
    bool redo();

    /** 清空撤销历史（载入预设/工程时调用，避免跨状态撤销）。 */
    void clearUndoHistory();

    /** 当前音高（Hz），供 UI 显示。 */
    float getCurrentFrequency() const noexcept { return currentFrequency.load(); }

    MasterChain& getMasterChain() noexcept { return masterChain; }

    /** 电平表：当前输出峰值（线性）与增益衰减（dB）。 */
    float getMeterPeak() const noexcept { return meterPeak.load(); }
    float getMeterGainReductionDb() const noexcept { return meterGr.load(); }

    //==========================================================================
    /** 供 UI 画波形：最近一次渲染时捕获的各层波形。
     *  0 = Sub，1..3 = Click 层。缓冲由处理器持有，编辑器读取。 */
    static constexpr int kWaveCaptureSize = 4096;
    const float* getWaveCapture (int sourceIndex) const noexcept
    {
        const int i = juce::jlimit (0, 3, sourceIndex);
        return waveCapture[(size_t) i].data();
    }
    int getWaveCaptureSize (int sourceIndex) const noexcept
    {
        return waveCaptureFill[(size_t) juce::jlimit (0, 3, sourceIndex)];
    }
    /** 编辑器的波形显示器定时调用：把最新捕获的波形推进显示器。 */
    void copyWaveCapturesTo (class WaveDisplay& display);

    /** 请求下一次触发时重新捕获波形（例如编辑器刚打开）。 */
    void requestWaveCapture() noexcept { needsWaveCapture = true; }

    //==========================================================================
    /** 立即可试听：从编辑器触发一次点击。 */
    void triggerAudition (float velocity = 1.0f);

private:
    //==========================================================================
    void handleMidi (const juce::MidiBuffer& midi);
    void startNote (int midiNote, float velocity);
    void updateEnvelopeReaders();

    /** 分层渲染一遍，把 Sub 和每个 Click 层分别写进捕获缓冲。 */
    void captureRender (int numSamples,
                        bool subOn, float subLevel, float subDecay, float pitchAmt,
                        float subPitchDec, float subPunch,
                        int harmonics, float subHarm, bool subHarmOn,
                        const std::array<float, SubSynth::kMaxHarmonics>& harmonicLevels,
                        const std::array<const EnvelopeReader*, ClickSection::kNumLayers>& clickEnvs,
                        const std::array<float, ClickSection::kNumLayers>& clickLevels,
                        const std::array<float, ClickSection::kNumLayers>& clickDecays,
                        const std::array<float, ClickSection::kNumLayers>& clickLp,
                        const std::array<float, ClickSection::kNumLayers>& clickHp,
                        const std::array<bool, ClickSection::kNumLayers>& clickOn);

    void appendCapture (int channel, const float* data, int numSamples);

    // 由参数计算目标基频（keytrack + tune）
    float computeFrequency (int midiNote) const;

    juce::AudioProcessorValueTreeState apvts;

    SubSynth subSynth;
    ClickSection clicks;
    Distortion distortion;
    MasterChain masterChain;

    /** EQ 之后清除直流与滤波器暂态（左右各一）。 */
    std::array<dsp::DcBlocker, 2> postEqDcBlockers;

    /** EQ 增益平滑（避免系数突变产生阶跃暂态）。 */
    std::array<float, kNumEqBands> eqSmoothedGain {};
    float eqGainSmoothCoeff = 1.0f;

    /** 读取某段当前的目标增益（供平滑器初始化）。 */
    float rawGainFor (int band) const
    {
        return apvts.getRawParameterValue (pid::eq (band, "gain"))->load();
    }

    // 过采样：只包住失真级（混叠的源头）。
    // 用 juce::dsp::Oversampling 做 2x / 4x，在失真前后自动上/下采样。
    static constexpr int kMaxOversampleFactor = 4;
    std::unique_ptr<juce::dsp::Oversampling<float>> oversampler;
    int currentOversampleFactor = 1;
    /** 受限的过采样中间缓冲：blockSize 可能变化，这里按需扩容。 */
    juce::AudioBuffer<float> oversampleBuffer;
    Equalizer eq;
    SampleLibrary library;
    std::unique_ptr<PresetManager> presetManager;
    std::unique_ptr<AiApplier> aiApplier;



public:
    /** 供 UI 访问调制矩阵。 */
    ModulationMatrix& getModulation() noexcept { return modulation; }
    const ModulationMatrix& getModulation() const noexcept { return modulation; }

    std::array<Envelope, (size_t) EnvelopeTarget::NumTargets> envelopes;

    // 音频线程读取的烘焙表
    std::array<EnvelopeReader, (size_t) EnvelopeTarget::NumTargets> envReaders;

    /** 通用调制矩阵。
     *
     *  与上面的 envelopes 分工不同：
     *    - envelopes 是**结构性**的（Pitch / Amp / Click 音量），决定底鼓的基本形态
     *    - modulation 是**表现性**的（音头抬 EQ、音尾关失真），做音色细节
     *
     *  分开是因为语义不同：前者每个底鼓都必须有，后者是可选的修饰。
     *  混在一起会让 UI 和序列化都变乱。 */
    ModulationMatrix modulation;

    // 波形捕获缓冲（音频线程写，UI 线程读）
    /** 最近一次 noteOn 算出的力度->Decay 缩放，供 processBlock 使用。 */
    float velocityDecayScale = 1.0f;

    // Click Duck：侧链压制用的包络与增益曲线
    float duckEnv = 0.0f;

    std::array<std::array<float, 4096>, 2> duckScratch {};

    // 瞬态整形的双包络跟随器状态
    float transFastEnv = 0.0f;
    float transSlowEnv = 0.0f;

    // 撤销栈（保存完整状态树快照）
    std::vector<juce::ValueTree> undoStack;
    int undoPos = -1;                       // 当前位于栈中哪一项
    static constexpr int kMaxUndoSteps = 32;

    std::array<std::array<float, kWaveCaptureSize>, 4> waveCapture;
    std::array<int, 4> waveCaptureFill {};
    /** 是否需要（重新）捕获分层波形。编辑器打开或触发新音符时置位。 */
    bool needsWaveCapture = false;

    /** 专用于波形捕获的独立引擎实例，避免干扰正在发声的主引擎。 */
    SubSynth captureSub;
    ClickSection captureClicks;

    std::atomic<float> currentFrequency { 50.0f };

    // 电平表数据（音频线程写、UI 线程读）
    std::atomic<float> meterPeak { 0.0f };
    std::atomic<float> meterGr { 0.0f };

    // --- 音符状态
    int   lastMidiNote = -1;
    float lastVelocity = 1.0f;
    bool  noteActive = false;

    // --- Portamento：当前正在滑动的频率
    dsp::SmoothedValue glideFreq;
    bool  glidePending = false;
    float glideTargetFreq = 50.0f;
    float glideStartFreq = 50.0f;
    double glideSamples = 0.0;
    double glideTotalSamples = 0.0;

    double currentSampleRate = 44100.0;
    int    currentBlockSize = 512;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (KickForgeProcessor)
};

} // namespace kf
