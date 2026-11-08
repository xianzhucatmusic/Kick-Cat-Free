#include "AiConfig.h"
#include "KfPaths.h"
#include <algorithm>

namespace kf
{

AiConfig::AiConfig()
{
    load();
}

juce::File AiConfig::getConfigFile()
{
    return paths::getConfigFile();
}

void AiConfig::load()
{
    const auto f = getConfigFile();
    if (! f.existsAsFile())
        return;

    juce::var parsed;
    if (juce::JSON::parse (f.loadFileAsString(), parsed).failed())
        return;

    auto* o = parsed.getDynamicObject();
    if (o == nullptr)
        return;

    modelIndex   = (int) o->getProperty ("ai_model");
    autoAudition = (bool) o->getProperty ("auto_audition");

    if (auto* arr = o->getProperty ("ai_prompt_history").getArray())
    {
        promptHistory.clear();
        for (const auto& v : *arr)
        {
            const auto t = v.toString().trim();
            if (t.isNotEmpty())
                promptHistory.push_back (t);
            if ((int) promptHistory.size() >= kMaxHistory)
                break;
        }
    }

    modelIndex = juce::jlimit (0, 1, modelIndex);
}

void AiConfig::save() const
{
    auto* o = new juce::DynamicObject();
    o->setProperty ("ai_model", modelIndex);
    o->setProperty ("auto_audition", autoAudition);

    juce::Array<juce::var> arr;
    for (const auto& p : promptHistory)
        arr.add (p);
    o->setProperty ("ai_prompt_history", arr);

    const auto f = getConfigFile();
    f.getParentDirectory().createDirectory();
    f.replaceWithText (juce::JSON::toString (juce::var (o), true));
}

void AiConfig::addPrompt (const juce::String& prompt)
{
    const auto t = prompt.trim();
    if (t.isEmpty())
        return;

    // 已存在就提到最前（而不是留下两条重复）
    promptHistory.erase (std::remove (promptHistory.begin(), promptHistory.end(), t),
                         promptHistory.end());

    promptHistory.insert (promptHistory.begin(), t);

    if ((int) promptHistory.size() > kMaxHistory)
        promptHistory.resize ((size_t) kMaxHistory);

    save();
}

} // namespace kf
