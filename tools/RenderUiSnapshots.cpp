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
    // "v6" mode: Source tab of both plugin editions (AETHER Micro FX + Resynth, AETHER Micro Synth) at the minimum size and 50% compact, 1x and 2x. Usage: AetherUiSnapshots <dir> v6
    if(argc>2&&juce::String(argv[2])=="v6") {
        base.reset(); editor=nullptr;
        bool good=true;
        struct Case { const char* name; int edition; float mode; int engine; };
        const Case cases[]={
            {"micro-fx-notice",0,0,1},{"micro-fx-restored-synth-value",0,2,2},{"micro-resynth-off",0,1,0},{"micro-resynth-pg",0,1,1},{"micro-resynth-chaos",0,1,2},
            {"synth-off",1,2,0},{"synth-pg",1,2,1},{"synth-chaos",1,2,2},{"synth-stored-fx-value",1,0,1}};
        for(int edition=0;edition<2;++edition) {
            AetherAudioProcessorEditor::setEditionForTesting(edition);
            AetherAudioProcessor q; q.prepareToPlay(48000,512);
            std::unique_ptr<juce::AudioProcessorEditor> eBase(q.createEditor());
            auto* ed=dynamic_cast<AetherAudioProcessorEditor*>(eBase.get());
            if(ed==nullptr) return 3;
            auto shot=[&](const juce::String& name) {
                for(float scale:{1.f,2.f}) good=writePng(dir.getChildFile("Atlas-v6-"+name+"-"+juce::String(int(scale))+"x.png"),ed->createComponentSnapshot(ed->getLocalBounds(),true,scale))&&good;
            };
            for(const auto& c:cases) {
                if(c.edition!=edition) continue;
                setParam(q,"global.mode",c.mode); setParam(q,"source.engine",float(c.engine)); setParam(q,"source.enabled",1);
                ed->setMinimalMode(false); ed->setWorkspace(3); (void)ed->sourceStatus();
                ed->performUiAction("zoom.100"); ed->setSize(1180,780); shot(juce::String(c.name)+"-min-1180x780");
                ed->performUiAction("zoom.50"); shot(juce::String(c.name)+"-zoom.50-720x450");
            }
            setParam(q,"global.mode",edition==1?2.f:1.f);
            ed->performUiAction("zoom.100"); ed->setSize(1180,780); ed->setMinimalMode(true); shot(juce::String(edition==1?"synth":"micro")+"-all-min-1180x780");
            ed->performUiAction("zoom.50"); shot(juce::String(edition==1?"synth":"micro")+"-all-zoom.50-720x450");
            ed->setMinimalMode(false); ed->performUiAction("zoom.100");
        }
        AetherAudioProcessorEditor::setEditionForTesting(-1);
        return good?0:1;
    }
    // "v7" mode: 2.3.0 page notices. FM hints (effect edition with Mode=FX, Six operator engine off, active), Motion page of both editions, Patch page of both editions; minimum size and 50% compact, 1x and 2x. Usage: AetherUiSnapshots <dir> v7
    if(argc>2&&juce::String(argv[2])=="v7") {
        base.reset(); editor=nullptr;
        bool good=true;
        struct Case { const char* name; int edition; int workspace; float mode; float fmEnabled; };
        const Case cases[]={
            {"fm-micro-fx-notice",0,1,0,1},{"fm-micro-six-operator-off",0,1,1,0},{"fm-micro-resynth-active",0,1,1,1},
            {"fm-synth-active",1,1,2,1},{"fm-synth-six-operator-off",1,1,2,0},
            {"motion-micro",0,2,1,1},{"motion-synth",1,2,2,1},
            {"patch-micro",0,0,1,1},{"patch-synth",1,0,2,1}};
        for(int edition=0;edition<2;++edition) {
            AetherAudioProcessorEditor::setEditionForTesting(edition);
            AetherAudioProcessor q; q.prepareToPlay(48000,512);
            std::unique_ptr<juce::AudioProcessorEditor> eBase(q.createEditor());
            auto* ed=dynamic_cast<AetherAudioProcessorEditor*>(eBase.get());
            if(ed==nullptr) return 3;
            auto shot=[&](const juce::String& name) {
                for(float scale:{1.f,2.f}) good=writePng(dir.getChildFile("Atlas-v7-"+name+"-"+juce::String(int(scale))+"x.png"),ed->createComponentSnapshot(ed->getLocalBounds(),true,scale))&&good;
            };
            for(const auto& c:cases) {
                if(c.edition!=edition) continue;
                setParam(q,"global.mode",c.mode); setParam(q,"fm.enabled",c.fmEnabled);
                ed->setMinimalMode(false); ed->setWorkspace(c.workspace);
                ed->performUiAction("zoom.100"); ed->setSize(1180,780); shot(juce::String(c.name)+"-min-1180x780");
                ed->performUiAction("zoom.50"); shot(juce::String(c.name)+"-zoom.50-720x450");
            }
            ed->performUiAction("zoom.100");
        }
        AetherAudioProcessorEditor::setEditionForTesting(-1);
        return good?0:1;
    }
    // "wire" mode: 2.3.0 real Source / FM modulation destinations (85..112) on the Motion page, with the per-engine notices. Both editions, minimum size and 50% compact, 1x and 2x. Usage: AetherUiSnapshots <dir> wire
    if(argc>2&&juce::String(argv[2])=="wire") {
        base.reset(); editor=nullptr;
        bool good=true;
        struct Case { const char* name; int edition; float mode; float engine; float fmEnabled; int targets[6]; };
        // Rows 1..6 target the listed destination option (0 = None). 88 Source / Walsh seq, 90 CZ depth, 94 VPS V (Phase Geometry); 97 Amp step, 99 Dur range, 101 Amp barrier (Chaos); 102 FM / Mod index, 108 FM / Op 2 level; 85 Source / Level; 5 Node 1 / Mix.
        const Case cases[]={
            {"pg-ok",-1,-1,1,1,{88,90,94,85,107,5}},
            {"pg-needs-phase-geometry-engine-is-chaos",-1,-1,2,1,{88,90,94,85,107,5}},
            {"chaos-ok",-1,-1,2,1,{97,99,101,86,108,5}},
            {"chaos-needs-chaos-engine-is-phase-geometry",-1,-1,1,1,{97,99,101,86,108,5}},
            {"fm-ok",-1,-1,1,1,{102,104,106,110,112,5}},
            {"fm-six-operator-off",-1,-1,1,0,{102,104,106,110,112,5}},
            {"source-engine-off",-1,-1,0,1,{85,86,87,5,0,0}},
            {"fm-micro-fx-mode",0,0,1,1,{102,104,106,110,112,5}}};
        for(int edition=0;edition<2;++edition) {
            AetherAudioProcessorEditor::setEditionForTesting(edition);
            AetherAudioProcessor q; q.prepareToPlay(48000,512);
            std::unique_ptr<juce::AudioProcessorEditor> eBase(q.createEditor());
            auto* ed=dynamic_cast<AetherAudioProcessorEditor*>(eBase.get());
            if(ed==nullptr) return 3;
            auto shot=[&](const juce::String& name) {
                for(float scale:{1.f,2.f}) good=writePng(dir.getChildFile("Atlas-wire-"+name+"-"+juce::String(int(scale))+"x.png"),ed->createComponentSnapshot(ed->getLocalBounds(),true,scale))&&good;
            };
            for(const auto& c:cases) {
                if(c.edition>=0&&c.edition!=edition) continue;
                setParam(q,"global.mode",c.mode>=0?c.mode:(edition==1?2.f:1.f)); setParam(q,"fm.enabled",c.fmEnabled); setParam(q,"source.enabled",1); setParam(q,"source.engine",c.engine);
                for(int row=1;row<=6;++row) {
                    const auto base2="modulation."+juce::String(row)+".";
                    setParam(q,(base2+"source").toRawUTF8(),c.targets[row-1]>0?float(row):0.f); setParam(q,(base2+"depth").toRawUTF8(),c.targets[row-1]>0?.5f:0.f); setParam(q,(base2+"target").toRawUTF8(),float(c.targets[row-1]));
                }
                ed->setMinimalMode(false); ed->setWorkspace(2);
                const juce::String prefix=juce::String(edition==1?"synth-":"micro-")+c.name;
                ed->performUiAction("zoom.100"); ed->setSize(1180,780); shot(prefix+"-min-1180x780");
                ed->performUiAction("zoom.50"); shot(prefix+"-zoom.50-720x450");
            }
            ed->performUiAction("zoom.100");
        }
        AetherAudioProcessorEditor::setEditionForTesting(-1);
        return good?0:1;
    }
    // "rand" mode: 2.3.0 stage B Randomize popover on the aether::rand engine backend. Closed header, popover open, after a roll, lock mode, full undo history (16/16), Randomize into B and the FX hint,
    // for both plugin editions at the minimum size, the default size and the 50% compact layout, 1x and 2x. Usage: AetherUiSnapshots <dir> rand
    if(argc>2&&juce::String(argv[2])=="rand") {
        base.reset(); editor=nullptr;
        bool good=true;
        AetherAudioProcessorEditor::setRandomizeBackendFactory([](AetherAudioProcessor& proc) { return aether::ui::makeEngineBackend(proc); });
        for(int edition=0;edition<2;++edition) {
            AetherAudioProcessorEditor::setEditionForTesting(edition);
            AetherAudioProcessor q; q.prepareToPlay(48000,512);
            std::unique_ptr<juce::AudioProcessorEditor> eBase(q.createEditor());
            auto* ed=dynamic_cast<AetherAudioProcessorEditor*>(eBase.get());
            if(ed==nullptr||ed->randomizePanel()==nullptr) return 3;
            auto* panel=ed->randomizePanel();
            const juce::String tag=edition==1?"synth":"micro";
            auto shot=[&](const juce::String& name) {
                for(float scale:{1.f,2.f}) good=writePng(dir.getChildFile("Atlas-rand-"+tag+"-"+name+"-"+juce::String(int(scale))+"x.png"),ed->createComponentSnapshot(ed->getLocalBounds(),true,scale))&&good;
            };
            auto sizes=[&](const juce::String& name,bool withDefault) {
                ed->performUiAction("zoom.100"); ed->setSize(1180,780); shot(name+"-min-1180x780");
                if(withDefault) { ed->setSize(1440,900); shot(name+"-default-1440x900"); }
                ed->performUiAction("zoom.50"); shot(name+"-zoom.50-720x450");
                ed->performUiAction("zoom.100"); ed->setSize(1180,780);
            };
            setParam(q,"global.mode",edition==1?2.f:1.f);
            ed->setMinimalMode(false); ed->setWorkspace(0);
            sizes("closed",false);
            ed->performUiAction("random.open"); sizes("open",true);
            // A roll with a pinned seed: the preset box reads "(Random) <seed>".
            panel->setPinned(true); panel->setSeedText("A7F3-92C1"); panel->setAmount(.6f); panel->roll(false); sizes("rolled",true);
            ed->performUiAction("random.close"); sizes("rolled-closed",false);
            ed->performUiAction("random.open");
            // Lock mode: Chain, Motion and FM locked next to the default MixGlobal / System locks.
            panel->setLocked("Chain",true); panel->setLocked("Motion",true); panel->setLocked("FM",true); sizes("locks",true);
            // Full undo ring: twenty rolls leave 16 steps.
            panel->setPinned(false); for(int i=0;i<20;++i) panel->roll(false); sizes("history-full",false);
            panel->setLocked("Chain",false); panel->setLocked("Motion",false); panel->setLocked("FM",false);
            // Randomize into B: the result sits in the B slot, A keeps the patch.
            panel->setPinned(true); panel->setSeedText("0Z9K-4Q7M"); panel->roll(true); sizes("into-b",false);
            while(panel->undo()) {}
            if(edition==0) { setParam(q,"global.mode",0.f); ed->performUiAction("random.open"); sizes("fx-hint",false); }
            ed->performUiAction("random.close");
        }
        AetherAudioProcessorEditor::setEditionForTesting(-1);
        AetherAudioProcessorEditor::setRandomizeBackendFactory({});
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
