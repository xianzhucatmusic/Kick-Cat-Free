#include "WavExporter.h"
#include "PluginProcessor.h"

namespace kf
{

WavExporter::WavExporter (KickForgeProcessor& p)
    : processor (p)
{
}

//==============================================================================
// 离线渲染
//
// 关键设计：直接用处理器真实的 processBlock 渲染。
// 这样导出结果 = 实时听到的声音（失真、EQ、压缩、限幅全部包含）。
// 如果为了"简单"另写一条精简信号链，导出就必然和试听不一致。
//==============================================================================

juce::AudioBuffer<float> WavExporter::renderToBuffer (juce::AudioProcessor& proc,
                                                      const Options& opts,
                                                      double& outSeconds,
                                                      float& outPeak)
{
    const double sr = opts.sampleRate > 0.0 ? opts.sampleRate : 44100.0;
    const int blockSize = 512;
    const int totalSamples = (int) (sr * juce::jlimit (0.1, 60.0, opts.maxSeconds));

    juce::AudioBuffer<float> out (2, totalSamples);
    out.clear();

    proc.prepareToPlay (sr, blockSize);

    // --- 触发一个音符，然后一直渲染到尾音消失
    int pos = 0;
    bool noteSent = false;
    int silenceBlocks = 0;
    const int silenceBlocksToStop = (int) (sr / blockSize * 0.25);   // 静音 250ms 即认为结束
    int lastLoudSample = 0;

    while (pos < totalSamples)
    {
        const int n = juce::jmin (blockSize, totalSamples - pos);

        juce::AudioBuffer<float> block (2, n);
        block.clear();

        juce::MidiBuffer midi;
        if (! noteSent)
        {
            midi.addEvent (juce::MidiMessage::noteOn (1, opts.midiNote,
                                                      (juce::uint8) juce::jlimit (1, 127,
                                                          (int) std::round (opts.velocity * 127.0f))),
                           0);
            noteSent = true;
        }

        proc.processBlock (block, midi);

        float blockPeak = 0.0f;
        for (int ch = 0; ch < 2; ++ch)
        {
            out.copyFrom (ch, pos, block, ch, 0, n);
            blockPeak = juce::jmax (blockPeak, block.getMagnitude (ch, 0, n));
        }

        if (blockPeak > 1.0e-4f)
        {
            silenceBlocks = 0;
            lastLoudSample = pos + n;
        }
        else
        {
            ++silenceBlocks;
        }

        pos += n;

        // 已经响过、且连续静音 -> 收尾
        if (noteSent && silenceBlocks >= silenceBlocksToStop && lastLoudSample > 0)
            break;
    }

    // --- 裁剪到实际长度（末尾留 10ms 淡出，避免突然截断产生咔哒）
    int usedSamples = juce::jlimit (512, totalSamples, lastLoudSample + (int) (sr * 0.02));
    out.setSize (2, usedSamples, true, true, true);

    {
        const int fade = juce::jmin ((int) (sr * 0.01), usedSamples / 4);
        for (int i = 0; i < fade; ++i)
        {
            const float g = 1.0f - (float) i / (float) fade;
            for (int ch = 0; ch < 2; ++ch)
                out.setSample (ch, usedSamples - 1 - i,
                               out.getSample (ch, usedSamples - 1 - i) * g);
        }
    }

    // --- 可选归一化
    float peak = 0.0f;
    for (int ch = 0; ch < 2; ++ch)
        peak = juce::jmax (peak, out.getMagnitude (ch, 0, usedSamples));

    if (opts.normalizeToDb != 0.0f && peak > 1.0e-6f)
    {
        const float target = juce::Decibels::decibelsToGain (opts.normalizeToDb);
        out.applyGain (target / peak);
        peak = target;
    }

    outSeconds = (double) usedSamples / sr;
    outPeak = peak;

    return out;
}

//==============================================================================

bool WavExporter::writeWavFile (const juce::File& file,
                                const juce::AudioBuffer<float>& buffer,
                                double sampleRate,
                                int bitDepth)
{
    file.deleteFile();

    juce::WavAudioFormat fmt;

    std::unique_ptr<juce::FileOutputStream> stream (file.createOutputStream());
    if (stream == nullptr)
        return false;

    std::unique_ptr<juce::AudioFormatWriter> writer (
        fmt.createWriterFor (stream.get(),
                             sampleRate,
                             (unsigned int) buffer.getNumChannels(),
                             bitDepth,
                             {},
                             0));

    if (writer == nullptr)
        return false;

    stream.release();   // writer 接管 stream 的所有权

    return writer->writeFromAudioSampleBuffer (buffer, 0, buffer.getNumSamples());
}

void WavExporter::exportAsync (const Options& opts,
                               std::function<void (const Result&)> onFinished)
{
    // 用一个模态进度窗跑后台线程：渲染几秒音频很快，
    // 但万一用户设了很长的 maxSeconds，进度窗能给出反馈。
    class ExportThread : public juce::ThreadWithProgressWindow
    {
    public:
        ExportThread (juce::AudioProcessor& p, const Options& o, const juce::File& f)
            : ThreadWithProgressWindow ("Exporting WAV...", true, true),
              proc (p), opts (o), targetFile (f)
        {
        }

        void run() override
        {
            setProgress (0.15);
            setStatusMessage ("Rendering...");

            double seconds = 0.0;
            float peak = 0.0f;
            auto buffer = WavExporter::renderToBuffer (proc, opts, seconds, peak);

            if (threadShouldExit())
                return;

            setProgress (0.75);
            setStatusMessage ("Writing file...");

            resultOk = WavExporter::writeWavFile (targetFile, buffer, opts.sampleRate, opts.bitDepth);
            resultSeconds = seconds;
            resultPeak = peak;

            setProgress (1.0);
        }

        juce::AudioProcessor& proc;
        Options opts;
        juce::File targetFile;
        std::atomic<bool> resultOk { false };
        double resultSeconds = 0.0;
        float resultPeak = 0.0f;
    };

    // 文件名：音色名 + 音高 + 采样率
    const auto presetName = processor.getPresetManager().getCurrentPresetName();
    juce::String suggested = presetName.isNotEmpty() ? presetName : "Kick Cat";
    suggested = suggested.retainCharacters ("abcdefghijklmnopqrstuvwxyz"
                                            "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                            "0123456789 _-");

    const auto defaultFile = juce::File::getSpecialLocation (juce::File::userMusicDirectory)
                                 .getChildFile (suggested + ".wav");

    chooser = std::make_unique<juce::FileChooser> ("Export WAV", defaultFile, "*.wav",
                                                   true, false, nullptr);

    chooser->launchAsync (juce::FileBrowserComponent::saveMode
                        | juce::FileBrowserComponent::canSelectFiles
                        | juce::FileBrowserComponent::warnAboutOverwriting,
        [this, opts, onFinished] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();

            if (file == juce::File())
                return;   // 用户取消

            if (! file.hasFileExtension ("wav"))
                file = file.withFileExtension ("wav");

            auto* t = new ExportThread (processor, opts, file);

            t->launchThread();

            // 等待线程结束（runThread 内部是模态进度窗，会阻塞到完成）
            while (t->isThreadRunning())
                juce::Thread::sleep (20);

            Result r;
            r.ok = t->resultOk.load();
            r.file = file;
            r.seconds = t->resultSeconds;
            r.peak = t->resultPeak;

            if (r.ok)
            {
                r.message = "Exported " + juce::String (r.seconds, 2) + "s  ·  peak "
                          + juce::String (juce::Decibels::gainToDecibels (r.peak), 1) + " dBFS";
            }
            else
            {
                r.message = "Export failed (could not write file)";
            }

            delete t;

            if (onFinished)
                onFinished (r);
        });
}

void WavExporter::exportWithDialog (juce::Component* parent, const Options& opts)
{
    juce::ignoreUnused (parent);
    exportAsync (opts, nullptr);
}

} // namespace kf
