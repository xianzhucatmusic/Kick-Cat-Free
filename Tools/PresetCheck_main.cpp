#include <juce_gui_basics/juce_gui_basics.h>
#include <iostream>
#include "PluginProcessor.h"
using namespace kf;
int main(){

    juce::ScopedJuceInitialiser_GUI init;
    KickForgeProcessor proc;
    auto& pm = proc.getPresetManager();

    auto setP=[&](const juce::String& id,float v){
        if(auto* p=proc.getAPVTS().getParameter(id)) p->setValueNotifyingHost(p->convertTo0to1(v));
    };
    auto getP=[&](const juce::String& id){ return proc.getAPVTS().getRawParameterValue(id)->load(); };

    // 1. set a distinctive value
    setP(pid::subDecay, 777.0f);
    std::cout << "set decay=777, read=" << getP(pid::subDecay) << std::endl;

    // 2. inspect what getStateAsValueTree actually captured
    auto tree = proc.getStateAsValueTree();
    auto pv = tree.getChildWithName("PARAMETERS");
    std::cout << "PARAMETERS child valid: " << (pv.isValid()?"yes":"no") << std::endl;
    if(!pv.isValid()) { std::cout << "  -> top type: " << tree.getType().toString() << std::endl; }
    // look for our param
    int found=-1;
    std::function<void(juce::ValueTree)> walk=[&](juce::ValueTree t){
        for(int i=0;i<t.getNumChildren();++i){
            auto c=t.getChild(i);
            if(c.getProperty("id").toString()=="sub_decay") found=(int)(float)c.getProperty("value");
            walk(c);
        }
    };
    walk(tree);
    std::cout << "sub_decay in tree = " << found << std::endl;

    // 3. save
    bool saved = pm.savePreset("__T1__");
    std::cout << "saved=" << (saved?"yes":"no") << std::endl;

    auto f = PresetManager::getUserPresetDirectory().getChildFile("__T1__.kfpreset");
    std::cout << "file exists: " << (f.existsAsFile()?"yes":"no") << std::endl;
    if(f.existsAsFile()){
        auto txt = f.loadFileAsString();
        std::cout << "file contains 777: " << (txt.contains("777")?"YES":"NO") << std::endl;
        std::cout << "file size: " << txt.length() << std::endl;
    }

    // 4. change value, then reload
    setP(pid::subDecay, 111.0f);
    std::cout << "changed to " << getP(pid::subDecay) << std::endl;
    pm.loadPresetByName("__T1__");
    std::cout << "after reload = " << getP(pid::subDecay) << "  (expect 777)" << std::endl;

    pm.deletePreset((int)pm.getPresets().size()-1);
    f.deleteFile();
    return 0;
}
