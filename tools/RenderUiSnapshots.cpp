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
    // "v2" mode: compact 50% layout set plus regression shots. Usage: AetherUiSnapshots <dir> v2
    if(argc>2&&juce::String(argv[2])=="v2") {
        bool good=true;
        auto shot=[&](const juce::String& name) {
            for(float scale:{1.f,2.f}) good=writePng(dir.getChildFile("Atlas-v2-"+name+"-"+juce::String(int(scale))+"x.png"),editor->createComponentSnapshot(editor->getLocalBounds(),true,scale))&&good;
        };
        const char* names[]={"patch","fm","motion","all"};
        editor->performUiAction("zoom.50");
        for(int ws=0;ws<4;++ws) { editor->setMinimalMode(ws==3); if(ws<3) editor->setWorkspace(ws); shot(juce::String(names[ws])+"-zoom.50-720x450"); }
        editor->setMinimalMode(false); editor->setWorkspace(0);
        setParam(p,"global.bypass",1); setParam(p,"node.3.on",0); shot("patch-zoom.50-bypass-disabled-node"); setParam(p,"global.bypass",0); setParam(p,"node.3.on",1);
        editor->setSize(1100,750); shot("patch-zoom.50-max-1100x750");
        editor->setSize(1000,625); shot("patch-compact-1000x625");
        editor->setSize(1030,640); shot("patch-compact-edge-1030x640");
        editor->setSize(1040,650); shot("patch-default-layout-edge-1040x650");
        editor->performUiAction("zoom.75"); shot("patch-zoom.75-1080x675");
        editor->performUiAction("zoom.100");
        for(const auto& size:{std::pair<int,int>{1180,780},{1440,900},{2200,1500}}) { editor->setSize(size.first,size.second); shot("patch-"+juce::String(size.first)+"x"+juce::String(size.second)); }
        editor->performUiAction("zoom.50"); editor->performUiAction("slot.6"); editor->performUiAction("module.5"); shot("patch-zoom.50-slot6-module5");
        editor->performUiAction("slot.1"); editor->performUiAction("route.1.2"); shot("patch-zoom.50-cable-selected");
        return good?0:1;
    }
    // "v3" mode: FM workspace with the longest algorithm name and detune at -100 ct, plus patch/motion/all regression shots. Usage: AetherUiSnapshots <dir> v3
    if(argc>2&&juce::String(argv[2])=="v3") {
        bool good=true;
        auto shot=[&](const juce::String& name) {
            for(float scale:{1.f,2.f}) good=writePng(dir.getChildFile("Atlas-v3-"+name+"-"+juce::String(int(scale))+"x.png"),editor->createComponentSnapshot(editor->getLocalBounds(),true,scale))&&good;
        };
        auto settle=[] {}; // parameter attachments update synchronously on the message thread
        editor->setMinimalMode(false); editor->setWorkspace(1);
        editor->performUiAction("fm.algorithm.24"); // item "25  Triangular Courtyard / Three Cycle"
        for(int op=1;op<=6;++op) setParam(p,("fm.op."+juce::String(op)+".detune").toRawUTF8(),-100.f);
        settle();
        editor->performUiAction("zoom.100"); editor->setSize(1440,900); shot("fm-default-1440x900");
        editor->setSize(1180,780); shot("fm-min-1180x780");
        editor->performUiAction("zoom.75"); shot("fm-zoom.75-1080x675");
        editor->performUiAction("zoom.50"); shot("fm-zoom.50-720x450");
        for(int op=1;op<=6;++op) setParam(p,("fm.op."+juce::String(op)+".detune").toRawUTF8(),100.f);
        settle(); editor->performUiAction("zoom.100"); editor->setSize(1440,900); shot("fm-default-1440x900-detune-plus100");
        const char* names[]={"patch","fm","motion","all"};
        for(int ws:{0,2,3}) {
            editor->performUiAction("zoom.50"); editor->setMinimalMode(ws==3); if(ws<3) editor->setWorkspace(ws); shot(juce::String(names[ws])+"-zoom.50-720x450");
            editor->performUiAction("zoom.100"); editor->setSize(1440,900); shot(juce::String(names[ws])+"-default-1440x900");
        }
        editor->setMinimalMode(false); editor->setWorkspace(2); editor->setSize(1100,750); shot("motion-1100x750");
        return good?0:1;
    }
    // "v4" mode: FM workspace hint row / operator divider spacing, plus one patch regression shot. Usage: AetherUiSnapshots <dir> v4
    if(argc>2&&juce::String(argv[2])=="v4") {
        bool good=true;
        auto shot=[&](const juce::String& name) {
            for(float scale:{1.f,2.f}) good=writePng(dir.getChildFile("Atlas-v4-"+name+"-"+juce::String(int(scale))+"x.png"),editor->createComponentSnapshot(editor->getLocalBounds(),true,scale))&&good;
        };
        editor->setMinimalMode(false); editor->setWorkspace(1);
        editor->performUiAction("fm.algorithm.24"); // item "25  Triangular Courtyard / Three Cycle"
        for(int op=1;op<=6;++op) setParam(p,("fm.op."+juce::String(op)+".detune").toRawUTF8(),-100.f);
        editor->performUiAction("zoom.100"); editor->setSize(1440,900); shot("fm-default-1440x900");
        editor->setSize(1180,780); shot("fm-min-1180x780");
        editor->performUiAction("zoom.75"); shot("fm-zoom.75-1080x675");
        editor->performUiAction("zoom.50"); shot("fm-zoom.50-720x450");
        editor->setWorkspace(0); editor->performUiAction("zoom.100"); editor->setSize(1440,900); shot("patch-default-1440x900");
        return good?0:1;
    }
    // "v5" mode: Source workspace (Off empty state, Phase Geometry, Chaos) at every size / zoom, Mode=FX notice, extreme values, All params and patch/fm/motion regression. Usage: AetherUiSnapshots <dir> v5
    if(argc>2&&juce::String(argv[2])=="v5") {
        bool good=true;
        auto shot=[&](const juce::String& name) {
            for(float scale:{1.f,2.f}) good=writePng(dir.getChildFile("Atlas-v5-"+name+"-"+juce::String(int(scale))+"x.png"),editor->createComponentSnapshot(editor->getLocalBounds(),true,scale))&&good;
        };
        struct Size { const char* name; int w,h; };
        const Size sizes[]={{"min-1180x780",1180,780},{"default-1440x900",1440,900},{"max-2200x1500",2200,1500}};
        auto allSizes=[&](const juce::String& prefix) {
            editor->performUiAction("zoom.100");
            for(const auto& size:sizes) { editor->setSize(size.w,size.h); shot(prefix+"-"+size.name); }
            editor->performUiAction("zoom.75"); shot(prefix+"-zoom.75-1080x675");
            editor->performUiAction("zoom.50"); shot(prefix+"-zoom.50-720x450");
            editor->performUiAction("zoom.100");
        };
        setParam(p,"global.mode",2);
        editor->setMinimalMode(false); editor->setWorkspace(3);
        setParam(p,"source.engine",0); (void)editor->sourceStatus(); allSizes("source-off");
        setParam(p,"source.engine",1); (void)editor->sourceStatus(); allSizes("source-pg");
        setParam(p,"source.engine",2); (void)editor->sourceStatus(); allSizes("source-chaos");
        // Extreme value texts: transpose -24 st, release 12 s, VPS ratio 16x, longest choice names, seed 65535.
        setParam(p,"source.transpose",-24.f); setParam(p,"source.release",12.f); setParam(p,"source.attack",5.f); setParam(p,"source.seed",65535.f); setParam(p,"source.spread",1.f); setParam(p,"source.gain",1.f);
        setParam(p,"source.pg.vpsratio",16.f); setParam(p,"source.pg.czshape",5.f); setParam(p,"source.pg.sequency",63.f); setParam(p,"source.pg.vpsd",.999f);
        setParam(p,"source.engine",1); (void)editor->sourceStatus(); allSizes("source-pg-extremes");
        setParam(p,"source.chaos.segments",32.f); setParam(p,"source.chaos.distribution",2.f); setParam(p,"source.chaos.momentum",.95f);
        setParam(p,"source.engine",2); (void)editor->sourceStatus(); allSizes("source-chaos-extremes");
        // Engine selected but Mode is not Synth (idle notice), and engine selected, Synth, but Source switched off.
        setParam(p,"global.mode",0); (void)editor->sourceStatus();
        editor->performUiAction("zoom.100"); editor->setSize(1180,780); shot("source-mode-fx-notice-min-1180x780");
        editor->performUiAction("zoom.100"); editor->setSize(1440,900); shot("source-mode-fx-notice-default-1440x900");
        editor->performUiAction("zoom.50"); shot("source-mode-fx-notice-zoom.50-720x450");
        setParam(p,"global.mode",2); setParam(p,"source.enabled",0); (void)editor->sourceStatus();
        editor->performUiAction("zoom.100"); editor->setSize(1180,780); shot("source-switched-off-min-1180x780");
        editor->performUiAction("zoom.50"); shot("source-switched-off-zoom.50-720x450");
        setParam(p,"source.enabled",1); setParam(p,"source.engine",0); (void)editor->sourceStatus();
        // All params page with the 28 new parameters (scroll to the end to show them).
        editor->performUiAction("zoom.100"); editor->setSize(1440,900); editor->setMinimalMode(true); shot("all-default-1440x900");
        editor->setSize(1180,780); shot("all-min-1180x780");
        editor->performUiAction("zoom.50"); shot("all-zoom.50-720x450");
        // Regression: the three existing workspaces at default size.
        editor->setMinimalMode(false);
        const char* names[]={"patch","fm","motion"};
        for(int ws=0;ws<3;++ws) { editor->setWorkspace(ws); editor->performUiAction("zoom.100"); editor->setSize(1440,900); shot(juce::String(names[ws])+"-default-1440x900"); editor->performUiAction("zoom.50"); shot(juce::String(names[ws])+"-zoom.50-720x450"); }
        return good?0:1;
    }
    struct Size { const char* name; int w,h; };
    const Size sizes[]={{"min-1180x780",1180,780},{"default-1440x900",1440,900},{"max-2200x1500",2200,1500}};
    const char* workspaces[]={"patch","fm","motion","source","all"};
    bool ok=true;
    for(int ws=0;ws<5;++ws) for(const auto& size:sizes) for(float scale:{1.f,2.f}) {
        editor->setMinimalMode(ws==4); if(ws<4) editor->setWorkspace(ws);
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
