#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

namespace kf
{

class KickForgeProcessor;

//==============================================================================
/** 预设管理器。
 *
 *  JUCE 8.0.4 **没有** 提供预设类（AudioProcessorValueTreeState::PresetManager
 *  是社区代码，不在 JUCE 里），所以这里自己实现。
 *
 *  设计：
 *   - 预设就是一份完整的 ValueTree（参数 + 包络 + 采样名），存成 .kfpreset
 *   - 用户目录：~/Library/Application Support/XianzhuCatMusic/Kick Cat/Presets
 *   - 内置工厂预设写在代码里，首次运行会铺到用户目录，用户可以随意改
 *   - 序列化直接复用处理器的 getStateAsValueTree / setStateFromValueTree，
 *     不重复实现一遍（否则日后加参数容易漏）
 */
class PresetManager
{
public:
    explicit PresetManager (KickForgeProcessor& p);

    struct PresetInfo
    {
        juce::String name;
        juce::File file;        // 内置预设为 invalid file
        bool isFactory = false;
    };

    /** 用户预设目录。 */
    static juce::File getUserPresetDirectory();

    /** 重新扫描磁盘上的预设列表（工厂预设始终在最前）。 */
    void refresh();

    const std::vector<PresetInfo>& getPresets() const noexcept { return presets; }

    /** 载入某个预设。 */
    bool loadPreset (int index);

    /** 按名字载入（状态恢复时用）。 */
    bool loadPresetByName (const juce::String& name);

    /** 保存当前状态为新预设。
     *  @param name  预设名（会做文件名安全化）
     *  @returns 是否成功 */
    bool savePreset (const juce::String& name);

    /** 覆盖保存到已有索引。 */
    bool overwritePreset (int index);

    /** 删除某个用户预设（工厂预设不可删）。 */
    bool deletePreset (int index);

    //==========================================================================
    // 收藏
    //
    // 480 个预设靠滚动很难找。收藏让用户把常用的几个标出来，
    // 菜单里可以只看收藏。收藏状态按**预设名**存（不是索引），
    // 这样即使预设顺序变化也不会错位。
    //==========================================================================

    bool isFavourite (const juce::String& presetName) const;
    void setFavourite (const juce::String& presetName, bool shouldBeFavourite);
    void toggleFavourite (const juce::String& presetName);

    /** 收藏的预设名（保序）。 */
    const std::vector<juce::String>& getFavourites() const noexcept { return favourites; }

    /** 收藏文件的路径（收藏存在这里，跟预设目录同级）。 */
    static juce::File getFavouritesFile();

private:
    /** 从磁盘读收藏列表。 */
    void loadFavourites();
    /** 写回磁盘。 */
    void saveFavourites() const;

    std::vector<juce::String> favourites;

public:

    /** 当前选中的预设名（用于在 UI 上显示）。 */
    const juce::String& getCurrentPresetName() const noexcept { return currentPresetName; }

    /** 取得上一个 / 下一个预设的索引（循环）。 */
    int getNextIndex (int delta) const;

    /** 让用户选目录导出/导入（异步）。
     *  @param forSave  true = 导出到文件，false = 从文件导入 */
    void exportToFile (juce::Component* parent, const juce::String& suggestedName);
    void importFromFile (juce::Component* parent);

    /** 载入完成后回调（UI 需要刷新所有控件）。 */
    std::function<void()> onPresetLoaded;

    /** 内置工厂预设的名字列表。 */
    static juce::StringArray getFactoryPresetNames();

private:
    /** 生成内置工厂预设的状态树。 */
    static juce::ValueTree makeFactoryPreset (const juce::String& name);

    KickForgeProcessor& processor;
    std::vector<PresetInfo> presets;
    juce::String currentPresetName { "Init" };

    /** FileChooser 必须保活，否则对话框会被取消。 */
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PresetManager)
};

} // namespace kf
