#include <algorithm>
#include "PresetManager.h"
#include "KfPaths.h"
#include "PluginProcessor.h"
#include "FactoryPresets.h"

namespace kf
{

namespace
{
    constexpr const char* kPresetExtension = ".kfpreset";
    /** 预设文件里记录格式版本，将来改结构时可以做兼容。 */
    constexpr const char* kFormatVersion = "1";

    /** 把预设名变成安全的文件名。 */
    juce::String sanitiseFileName (const juce::String& name)
    {
        auto s = name.trim();
        if (s.isEmpty())
            s = "Untitled";

        // 去掉路径分隔符与非法字符
        s = s.retainCharacters ("abcdefghijklmnopqrstuvwxyz"
                                "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                                "0123456789 _-()[]#+.");

        s = s.trim();
        if (s.isEmpty())
            s = "Untitled";

        return s.substring (0, 64);
    }
}

//==============================================================================

PresetManager::PresetManager (KickForgeProcessor& p)
    : processor (p)
{
    loadFavourites();
    refresh();
}


//==============================================================================
// 收藏
//==============================================================================

juce::File PresetManager::getFavouritesFile()
{
    return paths::getFavouritesFile();
}

void PresetManager::loadFavourites()
{
    favourites.clear();

    const auto f = getFavouritesFile();
    if (! f.existsAsFile())
        return;

    juce::StringArray lines;
    f.readLines (lines);

    for (const auto& line : lines)
    {
        const auto t = line.trim();
        if (t.isNotEmpty())
            favourites.push_back (t);
    }
}

void PresetManager::saveFavourites() const
{
    juce::StringArray lines;
    for (const auto& n : favourites)
        lines.add (n);

    getFavouritesFile().replaceWithText (lines.joinIntoString ("\n"));
}

bool PresetManager::isFavourite (const juce::String& presetName) const
{
    return std::find (favourites.begin(), favourites.end(), presetName) != favourites.end();
}

void PresetManager::setFavourite (const juce::String& presetName, bool shouldBeFavourite)
{
    if (presetName.isEmpty())
        return;

    const bool currently = isFavourite (presetName);
    if (currently == shouldBeFavourite)
        return;

    if (shouldBeFavourite)
        favourites.push_back (presetName);
    else
        favourites.erase (std::remove (favourites.begin(), favourites.end(), presetName),
                          favourites.end());

    saveFavourites();
}

void PresetManager::toggleFavourite (const juce::String& presetName)
{
    setFavourite (presetName, ! isFavourite (presetName));
}

juce::File PresetManager::getUserPresetDirectory()
{
    return paths::getPresetDirectory();
}

juce::StringArray PresetManager::getFactoryPresetNames()
{
    // 预设由 FactoryPresets 程序化生成：
    // 12 种风格 × 10 个子类 × 4 个变体 = 480 个。
    //
    // 手写 480 份参数表无法维护，也无法保证它们真的互不相同；
    // 生成器用"风格 DNA + 子类偏移 + 确定性随机"来保证：
    //   - 同一预设名每次生成结果一致（可复现）
    //   - 不同预设之间有可闻差异
    //   - 所有取值都经过音乐性约束（不会出现没法用的组合）
    juce::StringArray names;
    names.ensureStorageAllocated ((int) FactoryPresets::getAll().size());

    for (const auto& e : FactoryPresets::getAll())
        names.add (e.name);

    return names;
}

//==============================================================================
// 工厂预设的构造
//
// 用「基准 + 风格覆盖」的方式组织：先给一个通用的底鼓起点，
// 再由每个预设覆盖它关心的那几个参数。这样新增预设只需写差异，
// 不必把 30 多个参数重复抄一遍。
//==============================================================================

namespace
{
    /** 往 ValueTree 里写一个参数值。 */
    inline void put (juce::ValueTree& tree, const juce::String& id, float value)
    {
        juce::ValueTree p ("PARAM");
        p.setProperty ("id", id, nullptr);
        p.setProperty ("value", value, nullptr);
        tree.appendChild (p, nullptr);
    }

    /** 所有预设共用的基准：Sub 打开、Click 1 打开、限幅打开。 */
    inline void applyBaseline (juce::ValueTree& t)
    {
        put (t, pid::subOn, 1.0f);
        // 默认电平与插件默认值保持一致（整体比早期版本低约 5.6dB），
        // 让预设也留出余量，而不是一上来就顶到限幅器。
        put (t, pid::subLevel, 0.45f);
        put (t, pid::subDecay, 420.0f);
        put (t, pid::subPitchAmt, 24.0f);
        put (t, pid::subHarmonics, 1.0f);
        put (t, pid::subPhase, 0.0f);
        put (t, pid::keytrack, 1.0f);
        put (t, pid::tune, 0.0f);
        put (t, pid::glide, 0.0f);
        put (t, pid::glideMode, 0.0f);

        put (t, pid::click (0, "on"), 1.0f);
        put (t, pid::click (0, "level"), 0.30f);
        put (t, pid::click (0, "pitch"), 0.0f);
        put (t, pid::click (0, "decay"), 45.0f);
        put (t, pid::click (0, "lp"), 20000.0f);
        put (t, pid::click (0, "hp"), 10.0f);
        put (t, pid::click (1, "on"), 0.0f);
        put (t, pid::click (2, "on"), 0.0f);

        put (t, pid::distOn, 0.0f);
        put (t, pid::distType, 0.0f);
        put (t, pid::distDrive, 3.0f);
        put (t, pid::distMix, 1.0f);

        put (t, pid::compOn, 0.0f);
        put (t, pid::compThresh, -12.0f);
        put (t, pid::compRatio, 4.0f);
        put (t, pid::compAttack, 3.0f);
        put (t, pid::compRelease, 120.0f);
        put (t, pid::compMakeup, 0.0f);

        put (t, pid::limOn, 1.0f);
        put (t, pid::limCeiling, -0.3f);
        put (t, pid::masterGain, 0.0f);

        put (t, pid::oversample, 0.0f);
        put (t, pid::phaseReset, 0.0f);

        // EQ 默认全关
        for (int b = 0; b < 8; ++b)
            put (t, pid::eq (b, "on"), 0.0f);
    }

    /** 打开某个 EQ 段并设值。 */
    inline void eqBand (juce::ValueTree& t, int band, float freq, float gain, float q)
    {
        put (t, pid::eq (band, "on"), 1.0f);
        put (t, pid::eq (band, "freq"), freq);
        put (t, pid::eq (band, "gain"), gain);
        put (t, pid::eq (band, "q"), q);
    }

    /** 设置一个 Click 层。 */
    inline void clickLayer (juce::ValueTree& t, int layer, bool on, float level,
                            float pitch, float decay, float lp, float hp)
    {
        put (t, pid::click (layer, "on"), on ? 1.0f : 0.0f);
        put (t, pid::click (layer, "level"), level);
        put (t, pid::click (layer, "pitch"), pitch);
        put (t, pid::click (layer, "decay"), decay);
        put (t, pid::click (layer, "lp"), lp);
        put (t, pid::click (layer, "hp"), hp);
    }
}

juce::ValueTree PresetManager::makeFactoryPreset (const juce::String& name)
{
    // 生成器已经保证了取值合理与可复现，这里直接转发。
    return FactoryPresets::create (name);
}

// 旧的手写预设实现已由 FactoryPresets::create() 取代
// （480 个预设手写无法维护，也无法保证互不相同）。

//==============================================================================

void PresetManager::refresh()
{
    presets.clear();

    // --- 工厂预设（始终可用，不可删）
    for (const auto& n : getFactoryPresetNames())
    {
        PresetInfo info;
        info.name = n;
        info.isFactory = true;
        presets.push_back (info);
    }

    // --- 用户预设（磁盘扫描）
    const auto dir = getUserPresetDirectory();
    if (dir.isDirectory())
    {
        juce::Array<juce::File> files;
        dir.findChildFiles (files, juce::File::findFiles, false, juce::String ("*") + kPresetExtension);

        // 按文件名排序，保证列表顺序稳定
        std::sort (files.begin(), files.end(),
                   [] (const juce::File& a, const juce::File& b)
                   { return a.getFileNameWithoutExtension()
                            .compareIgnoreCase (b.getFileNameWithoutExtension()) < 0; });

        for (const auto& f : files)
        {
            PresetInfo info;
            info.name = f.getFileNameWithoutExtension();
            info.file = f;
            info.isFactory = false;
            presets.push_back (info);
        }
    }
}

int PresetManager::getNextIndex (int delta) const
{
    if (presets.empty())
        return -1;

    // 找到当前预设的下标
    int cur = 0;
    for (int i = 0; i < (int) presets.size(); ++i)
    {
        if (presets[(size_t) i].name == currentPresetName)
        {
            cur = i;
            break;
        }
    }

    const int n = (int) presets.size();
    int next = ((cur + delta) % n + n) % n;
    return next;
}

bool PresetManager::loadPreset (int index)
{
    if (index < 0 || index >= (int) presets.size())
        return false;

    const auto& info = presets[(size_t) index];

    juce::ValueTree state;

    if (info.isFactory)
    {
        state = makeFactoryPreset (info.name);
    }
    else
    {
        if (! info.file.existsAsFile())
            return false;

        std::unique_ptr<juce::XmlElement> xml (juce::XmlDocument::parse (info.file));
        if (xml == nullptr)
            return false;

        state = juce::ValueTree::fromXml (*xml);
    }

    if (! state.isValid())
        return false;

    processor.setStateFromValueTree (state);
    currentPresetName = info.name;

    if (onPresetLoaded)
        onPresetLoaded();

    return true;
}

bool PresetManager::loadPresetByName (const juce::String& name)
{
    for (int i = 0; i < (int) presets.size(); ++i)
    {
        if (presets[(size_t) i].name == name)
            return loadPreset (i);
    }
    return false;
}

bool PresetManager::savePreset (const juce::String& name)
{
    const auto safe = sanitiseFileName (name);
    if (safe.isEmpty())
        return false;

    const auto file = getUserPresetDirectory()
                          .getChildFile (safe + kPresetExtension);

    auto state = processor.getStateAsValueTree();
    state.setProperty ("presetFormat", kFormatVersion, nullptr);
    state.setProperty ("presetName", safe, nullptr);

    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    if (xml == nullptr)
        return false;

    if (! xml->writeTo (file))
        return false;

    currentPresetName = safe;
    refresh();
    return true;
}

bool PresetManager::overwritePreset (int index)
{
    if (index < 0 || index >= (int) presets.size())
        return false;

    const auto& info = presets[(size_t) index];

    // 工厂预设不能覆盖 —— 改为"另存为同名用户预设"没有意义，直接拒绝
    if (info.isFactory)
        return false;

    auto state = processor.getStateAsValueTree();
    state.setProperty ("presetFormat", kFormatVersion, nullptr);
    state.setProperty ("presetName", info.name, nullptr);

    std::unique_ptr<juce::XmlElement> xml (state.createXml());
    if (xml == nullptr)
        return false;

    if (! xml->writeTo (info.file))
        return false;

    currentPresetName = info.name;
    return true;
}

bool PresetManager::deletePreset (int index)
{
    if (index < 0 || index >= (int) presets.size())
        return false;

    const auto& info = presets[(size_t) index];

    if (info.isFactory || ! info.file.existsAsFile())
        return false;

    // 移到废纸篓而不是直接删除，避免误操作无法恢复
    if (! info.file.moveToTrash())
        return false;

    refresh();
    return true;
}

//==============================================================================
// 文件导入 / 导出
//
// 注意：JUCE 8.0.4 里 **没有** canSelectFilesThatDon'tExist 这个 flag
// （那是 JUCE4 时代的 API，已被移除）。"允许输入不存在的文件名"由 saveMode 隐含提供。
//==============================================================================

void PresetManager::exportToFile (juce::Component* parent, const juce::String& suggestedName)
{
    const auto defaultDir = getUserPresetDirectory();

    chooser = std::make_unique<juce::FileChooser> (
        "Export Kick Cat Preset",
        defaultDir.getChildFile (sanitiseFileName (suggestedName) + kPresetExtension),
        juce::String ("*") + kPresetExtension,
        true, false, parent);

    chooser->launchAsync (juce::FileBrowserComponent::saveMode
                        | juce::FileBrowserComponent::canSelectFiles
                        | juce::FileBrowserComponent::warnAboutOverwriting,
        [this] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File())
                return;   // 用户取消

            if (! file.hasFileExtension (kPresetExtension))
                file = file.withFileExtension (kPresetExtension);

            auto state = processor.getStateAsValueTree();
            state.setProperty ("presetFormat", kFormatVersion, nullptr);
            state.setProperty ("presetName", file.getFileNameWithoutExtension(), nullptr);

            if (auto xml = std::unique_ptr<juce::XmlElement> (state.createXml()))
                xml->writeTo (file);
        });
}

void PresetManager::importFromFile (juce::Component* parent)
{
    chooser = std::make_unique<juce::FileChooser> (
        "Import Kick Cat Preset",
        getUserPresetDirectory(),
        juce::String ("*") + kPresetExtension,
        true, false, parent);

    chooser->launchAsync (juce::FileBrowserComponent::openMode
                        | juce::FileBrowserComponent::canSelectFiles,
        [this] (const juce::FileChooser& fc)
        {
            auto file = fc.getResult();
            if (file == juce::File() || ! file.existsAsFile())
                return;

            std::unique_ptr<juce::XmlElement> xml (juce::XmlDocument::parse (file));
            if (xml == nullptr)
                return;

            auto state = juce::ValueTree::fromXml (*xml);
            if (! state.isValid())
                return;

            processor.setStateFromValueTree (state);
            currentPresetName = file.getFileNameWithoutExtension();

            if (onPresetLoaded)
                onPresetLoaded();
        });
}

} // namespace kf
