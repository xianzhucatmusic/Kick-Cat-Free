#pragma once

#include <juce_core/juce_core.h>

namespace kf
{

//==============================================================================
/** 插件的数据目录（唯一入口）。
 *
 *  为什么需要这个文件：
 *  juce::File::userApplicationDataDirectory 在 macOS 上返回的是 `~/Library`，
 *  **不是** `~/Library/Application Support`。直接用它会让所有数据散落在
 *  ~/Library 根目录下，既不符合 macOS 惯例，也和其他 App 混在一起。
 *
 *  正确位置：~/Library/Application Support/XianzhuCatMusic/Kick Cat
 *
 *  所有需要持久化的东西（预设 / 收藏 / 配置 / 模型）都必须走这里，
 *  避免各处各写一份路径逻辑。
 */
namespace paths
{
    /** 插件数据根目录（会自动创建，并迁移旧版本遗留的数据）。 */
    inline juce::File getAppDataDirectory()
    {
        auto dir = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                       .getChildFile ("Application Support")
                       .getChildFile ("XianzhuCatMusic")
                       .getChildFile ("Kick Cat");

        if (! dir.exists())
            dir.createDirectory();

        // --- 一次性迁移 ---------------------------------------------------
        // 早期版本误用了 juce::File::userApplicationDataDirectory
        // （在 macOS 上它是 ~/Library，不是 Application Support），
        // 导致数据落在 ~/Library/XianzhuCatMusic。这里把旧数据搬过来，
        // 避免用户丢失已有预设与收藏。
        static bool migrationChecked = false;

        if (! migrationChecked)
        {
            migrationChecked = true;

            const auto legacy = juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
                                    .getChildFile ("XianzhuCatMusic")
                                    .getChildFile ("Kick Cat");

            if (legacy.isDirectory() && legacy != dir)
            {
                // 逐个搬，已存在的不覆盖（新位置优先）
                for (const auto& entry : juce::RangedDirectoryIterator (legacy, true, "*",
                                                                       juce::File::findFiles))
                {
                    const auto src = entry.getFile();
                    const auto rel = src.getRelativePathFrom (legacy);
                    const auto dst = dir.getChildFile (rel);

                    if (dst.existsAsFile())
                        continue;

                    dst.getParentDirectory().createDirectory();
                    src.copyFileTo (dst);
                }
            }
        }

        return dir;
    }

    inline juce::File getPresetDirectory()
    {
        auto dir = getAppDataDirectory().getChildFile ("Presets");
        if (! dir.exists())
            dir.createDirectory();
        return dir;
    }

    inline juce::File getModelDirectory()
    {
        auto dir = getAppDataDirectory().getChildFile ("Models");
        if (! dir.exists())
            dir.createDirectory();
        return dir;
    }

    inline juce::File getConfigFile()
    {
        return getAppDataDirectory().getChildFile ("ai_config.json");
    }

    inline juce::File getFavouritesFile()
    {
        return getAppDataDirectory().getChildFile ("Favourites.txt");
    }
} // namespace paths

} // namespace kf
