#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <functional>

namespace kf
{

class KickForgeProcessor;

//==============================================================================
/** 把当前音色离线渲染成 WAV。
 *
 *  实现要点：
 *   - 复制一份当前参数与包络状态，喂给**处理器本身的** processBlock，
 *     这样导出的声音与实时听到的完全一致（包括失真/EQ/压缩/限幅）。
 *     如果另写一套简化渲染，导出结果就一定和试听对不上。
 *   - 渲染在后台线程，避免大缓冲区阻塞 UI。
 *   - 自动检测尾音结束，不写多余的静音。
 */
class WavExporter
{
public:
    explicit WavExporter (KickForgeProcessor& p);

    struct Options
    {
        int    midiNote   = 36;      // C2，底鼓常用
        float  velocity   = 1.0f;
        double sampleRate = 44100.0;
        int    bitDepth   = 24;
        /** 最长渲染时长（秒）；实际会更早结束，取决于尾音。 */
        double maxSeconds = 8.0;
        /** 归一化峰值到该 dBFS；设为 0 表示不归一化。 */
        float  normalizeToDb = 0.0f;
    };

    struct Result
    {
        bool ok = false;
        juce::String message;
        juce::File file;
        double seconds = 0.0;
        float  peak = 0.0f;
    };

    /** 同步渲染到内存（供测试使用）。 */
    static juce::AudioBuffer<float> renderToBuffer (juce::AudioProcessor& proc,
                                                    const Options& opts,
                                                    double& outSeconds,
                                                    float& outPeak);

    /** 异步导出到文件（供 UI 使用）。渲染在后台线程完成。 */
    void exportAsync (const Options& opts, std::function<void (const Result&)> onFinished);

    /** 弹出保存对话框并导出。 */
    void exportWithDialog (juce::Component* parent, const Options& opts);

private:
    static bool writeWavFile (const juce::File& file,
                              const juce::AudioBuffer<float>& buffer,
                              double sampleRate,
                              int bitDepth);

    KickForgeProcessor& processor;

    /** FileChooser 必须保活，否则对话框会被取消。 */
    std::unique_ptr<juce::FileChooser> chooser;

    /** 后台渲染线程。 */
    std::unique_ptr<juce::ThreadWithProgressWindow> progressWindow;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (WavExporter)
};

} // namespace kf
