// Renders Atlas editor PNGs with Component::createComponentSnapshot at several window sizes and render scales.
// Usage: AetherUiSnapshots <output-directory>
#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <juce_gui_extra/juce_gui_extra.h>
#include <iostream>

namespace {
bool writePng(const juce::File& file,const juce::Image& image) {
    juce::PNGImageFormat png;
    juce::FileOutputStream stream(file);
    if(!stream.openedOk()) return false;
    stream.setPosition(0); stream.truncate();
    const bool ok=png.writeImageToStream(image,stream);
    std::cout<<(ok?"WROTE ":"FAILED ")<<file.getFullPathName()<<' '<<image.getWidth()<<'x'<<image.getHeight()<<'\n';
    return ok;
}
void setParam(AetherAudioProcessor& p,const char* id,float value) {
    if(auto* param=p.state.getParameter(id)) param->setValueNotifyingHost(param->convertTo0to1(value));
}
}

int main(int argc,char** argv) {
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File dir(juce::String(argc>1?argv[1]:"ui-snapshots"));
    if(!dir.createDirectory().wasOk()) return 2;
    AetherAudioProcessor p; p.prepareToPlay(48000,512);
    juce::AudioBuffer<float> audio(2,512); juce::MidiBuffer midi;
    for(int block=0;block<32;++block) {
        for(int c=0;c<2;++c) for(int i=0;i<512;++i) audio.setSample(c,i,0.22f*std::sin(0.06f*float(i+block*512)));
        p.processBlock(audio,midi);
    }
    std::unique_ptr<juce::AudioProcessorEditor> base(p.createEditor());
    auto* editor=dynamic_cast<AetherAudioProcessorEditor*>(base.get());
    if(editor==nullptr) return 3;
    struct Size { const char* name; int w,h; };
    const Size sizes[]={{"min-1180x780",1180,780},{"default-1440x900",1440,900},{"max-2200x1500",2200,1500}};
    const char* workspaces[]={"patch","fm","motion","all"};
    bool ok=true;
    for(int ws=0;ws<4;++ws) for(const auto& size:sizes) for(float scale:{1.f,2.f}) {
        editor->setMinimalMode(ws==3); if(ws<3) editor->setWorkspace(ws);
        editor->setSize(size.w,size.h);
        const auto image=editor->createComponentSnapshot(editor->getLocalBounds(),true,scale);
        ok=writePng(dir.getChildFile(juce::String("Atlas-")+workspaces[ws]+"-"+size.name+"-"+juce::String(int(scale))+"x.png"),image)&&ok;
    }
    // Zoom presets and extreme aspect ratios.
    editor->setMinimalMode(false); editor->setWorkspace(0);
    for(const char* zoom:{"zoom.50","zoom.75"}) {
        editor->performUiAction(zoom);
        ok=writePng(dir.getChildFile(juce::String("Atlas-patch-")+zoom+"-1x.png"),editor->createComponentSnapshot(editor->getLocalBounds(),true,1.f))&&ok;
        ok=writePng(dir.getChildFile(juce::String("Atlas-patch-")+zoom+"-2x.png"),editor->createComponentSnapshot(editor->getLocalBounds(),true,2.f))&&ok;
    }
    editor->performUiAction("zoom.100");
    // Engaged bypass and a disabled module slot, at the minimum window size.
    setParam(p,"global.bypass",1); setParam(p,"node.3.on",0);
    editor->setSize(1180,780);
    ok=writePng(dir.getChildFile("Atlas-patch-min-1180x780-bypass-1x.png"),editor->createComponentSnapshot(editor->getLocalBounds(),true,1.f))&&ok;
    ok=writePng(dir.getChildFile("Atlas-patch-min-1180x780-bypass-2x.png"),editor->createComponentSnapshot(editor->getLocalBounds(),true,2.f))&&ok;
    setParam(p,"global.bypass",0); setParam(p,"node.3.on",1);
    for(const auto& odd:{std::pair<int,int>{1180,1500},std::pair<int,int>{2200,780}}) {
        editor->setSize(odd.first,odd.second);
        ok=writePng(dir.getChildFile("Atlas-patch-odd-"+juce::String(odd.first)+"x"+juce::String(odd.second)+"-1x.png"),editor->createComponentSnapshot(editor->getLocalBounds(),true,1.f))&&ok;
    }
    return ok?0:1;
}
