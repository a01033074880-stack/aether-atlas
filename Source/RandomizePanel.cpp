#include "RandomizePanel.h"
#include <algorithm>
#include <cmath>
#include <deque>

namespace aether::ui {
namespace {
constexpr const char* seedAlphabet="0123456789ABCDEFGHJKMNPQRSTVWXYZ";
std::uint64_t mix64(std::uint64_t x) {
    x+=0x9e3779b97f4a7c15ULL; x=(x^(x>>30))*0xbf58476d1ce4e5b9ULL; x=(x^(x>>27))*0x94d049bb133111ebULL; return x^(x>>31);
}
float unitFor(std::uint64_t seed,const juce::String& id) {
    std::uint64_t h=1469598103934665603ULL;
    for(const char c:id.toStdString()) h=(h^static_cast<unsigned char>(c))*1099511628211ULL;
    return float(mix64(seed^h)>>40)/16777216.f;
}
std::uint64_t systemSeed() {
    auto& r=juce::Random::getSystemRandom();
    return ((std::uint64_t(std::uint32_t(r.nextInt()))<<8)^std::uint64_t(std::uint32_t(r.nextInt())))&randomSeedMask;
}
}

juce::String formatRandomSeed(std::uint64_t seed) {
    seed&=randomSeedMask; juce::String s;
    for(int i=7;i>=0;--i) s<<juce::String::charToString(static_cast<juce::juce_wchar>(seedAlphabet[(seed>>(5*i))&31]));
    return s.substring(0,4)+"-"+s.substring(4);
}
bool parseRandomSeed(const juce::String& text,std::uint64_t& seed) {
    juce::String t=text.toUpperCase().removeCharacters("- \t");
    if(t.isEmpty()||t.length()>8) return false;
    std::uint64_t v=0;
    for(auto c:t) {
        if(c=='O') c='0'; else if(c=='I'||c=='L') c='1';
        int digit=-1; for(int i=0;i<32;++i) if(juce::juce_wchar(seedAlphabet[i])==c) {digit=i;break;}
        if(digit<0) return false;
        v=(v<<5)|std::uint64_t(digit);
    }
    seed=v&randomSeedMask; return true;
}
juce::String randomPresetLabel(std::uint64_t seed) { return "(Random) "+formatRandomSeed(seed); }

// ------------------------------------------------------------------------------------------------------------------------
// MockBackend — stand-in for aether::rand::generate + SnapshotRing (§P.1 / §P.2) until the real engine lands.
// Live writes use begin/set/end per changed id (§P.5). Undo restores via replaceState (mock shortcut; real path also uses
// begin/set/end). into-B has no public AlternateSnapshot writer yet, so the mock approximates it with toggleAB (leaves the
// user on B); the real path writes the B ValueTree only and keeps A live (§P.2 Target::AlternateSnapshot).
// ------------------------------------------------------------------------------------------------------------------------
namespace {
class MockBackend final:public RandomizeBackend {
public:
    explicit MockBackend(AetherAudioProcessor& proc):p(proc) {}
    // UI section chips. Spec §1.2 groups are Chain/ModuleParams/FM/Source/Motion/MixGlobal/System; this mock keeps the
    // six research-era labels (Global≈MixGlobal+System Never, Nodes≈Chain+ModuleParams, Texture≈remainder) until LockSet
    // section bits land.
    juce::StringArray lockGroups() const override { return {"Global","Nodes","Motion","FM","Source","Texture"}; }
    juce::StringArray defaultLocked() const override { return {"Global"}; }
    RandomizeRecipe randomize(const RandomizeRequest& r) override { return run(r,false); }
    RandomizeRecipe randomizeIntoB(const RandomizeRequest& r) override { return run(r,true); }
    bool undo() override {
        if(ring.empty()) return false;
        auto e=std::move(ring.back()); ring.pop_back();
        // SnapshotRing restore of Target::Live (and the mock's into-B which left the user on B).
        p.state.replaceState(e.before);
        current=std::move(e.recipeBefore);
        if(e.switchedToB&&p.isSnapshotB()) p.toggleAB();
        return true;
    }
    int undoDepth() const override { return int(ring.size()); }
    std::optional<RandomizeRecipe> recipe() const override { return current; }
    void forgetRecipe() override { current.reset(); }
private:
    // RandomSnapshot stand-in (§P.2): whole APVTS tree + prior recipe + whether into-B flipped the live slot.
    struct Entry { juce::ValueTree before; std::optional<RandomizeRecipe> recipeBefore; bool switchedToB=false; };
    AetherAudioProcessor& p; std::deque<Entry> ring; std::optional<RandomizeRecipe> current;
    static juce::String groupOf(const juce::String& id) {
        if(id.startsWith("global.")) return "Global";
        if(id.startsWith("node.")||id.startsWith("route.")||id.startsWith("routing.")||id.startsWith("graph.")) return "Nodes";
        if(id.startsWith("lfo.")||id.startsWith("modulation.")) return "Motion";
        if(id.startsWith("fm.")) return "FM";
        if(id.startsWith("source.")) return "Source";
        return "Texture";
    }
    RandomizeRecipe run(const RandomizeRequest& r,bool intoB) {
        Entry e{p.state.copyState(),current,false};
        // into-B mock: copy A onto B and leave B live. Real path: write AlternateSnapshot only, A stays (§P.2).
        if(intoB&&!p.isSnapshotB()) { auto a=p.state.copyState(); p.toggleAB(); p.state.replaceState(a); e.switchedToB=true; }
        RandomizeRecipe rec; rec.seed=r.seed&randomSeedMask; rec.mode=r.mode; rec.amount=r.amount; rec.lockedGroups=r.lockedGroups; rec.intoB=intoB;
        rec.sourcePreset=current?current->sourcePreset:p.currentPresetName();
        // Mutate + amount==0 => empty patch (§6.2); no parameter writes, still push the ring so Undo is consistent.
        const float amount=juce::jlimit(0.f,1.f,r.amount);
        const bool emptyMutate=r.mode==RandomizeMode::mutate&&amount<=0.f;
        if(!emptyMutate) for(const auto& spec:aether::parameterSpecs()) {
            if(spec.boolean||spec.choices.size()>0) continue; // mock: continuous / integer only (Never/Structural not modelled)
            const auto group=groupOf(spec.id); if(r.lockedGroups.contains(group)) continue;
            auto* prm=p.state.getParameter(spec.id); if(prm==nullptr) continue;
            const float before=prm->getValue(),u=unitFor(rec.seed,spec.id);
            const float target=r.mode==RandomizeMode::mutate?before+(u*2.f-1.f)*amount*.5f:before+(u-before)*(.5f+.5f*amount);
            // Live commit path recommended by §P.5 (same as applyValues / loadFactoryPreset).
            prm->beginChangeGesture(); prm->setValueNotifyingHost(juce::jlimit(0.f,1.f,target)); prm->endChangeGesture();
            if(std::abs(prm->getValue()-before)>1.0e-6f) { ++rec.changedValues; rec.touchedGroups.addIfNotAlreadyThere(group); }
        }
        rec.onSlotB=p.isSnapshotB();
        ring.push_back(std::move(e)); while(int(ring.size())>randomUndoSteps) ring.pop_front();
        current=rec; return rec;
    }
};
}

std::unique_ptr<RandomizeBackend> makeMockBackend(AetherAudioProcessor& p) { return std::make_unique<MockBackend>(p); }

// ------------------------------------------------------------------------------------------------------------------------
// Widgets
// ------------------------------------------------------------------------------------------------------------------------
GlyphButton::GlyphButton(const RandomizeStyle& s,Kind k):juce::Button(k==Kind::caret?"Randomize settings":"Close"),style(s),kind(k) {}
void GlyphButton::paintButton(juce::Graphics& g,bool hover,bool down) {
    auto r=getLocalBounds();
    g.setColour(down?style.line:(hover||opened?style.light:style.paper)); g.fillRect(r); // same states as the header's text buttons
    g.setColour(style.line); g.drawRect(r,1);
    const auto c=r.toFloat().getCentre(); g.setColour(style.ink);
    juce::Path path;
    if(kind==Kind::caret) {
        const float dy=opened?-1.f:1.f; path.addTriangle(c.x-4.f,c.y-2.f*dy,c.x+4.f,c.y-2.f*dy,c.x,c.y+2.f*dy); g.fillPath(path);
    } else {
        path.startNewSubPath(c.x-4.f,c.y-4.f); path.lineTo(c.x+4.f,c.y+4.f); path.startNewSubPath(c.x+4.f,c.y-4.f); path.lineTo(c.x-4.f,c.y+4.f);
        g.strokePath(path,juce::PathStrokeType(1.5f));
    }
}

class RandomizePanel::PanelButton final:public juce::Button {
public:
    enum class Variant { normal, primary, toggle };
    PanelButton(const RandomizeStyle& s,Variant v,const juce::String& name):juce::Button(name),style(s),variant(v) { if(v==Variant::toggle) setClickingTogglesState(true); setButtonText(name); }
    bool filled() const { return isEnabled()&&(variant==Variant::primary||(variant==Variant::toggle&&getToggleState())); }
    juce::Colour foreground() const { return !isEnabled()?style.muted:(filled()?style.light:style.ink); }
    juce::Colour background() const { return !isEnabled()?style.paper:(filled()?style.ink:style.light); }
    juce::Rectangle<int> textArea() const { return getLocalBounds().reduced(6,0); }
    void paintButton(juce::Graphics& g,bool hover,bool down) override {
        auto bg=background(); if(isEnabled()&&!filled()) bg=down?style.line:(hover?style.paper:style.light);
        g.setColour(bg); g.fillRect(getLocalBounds());
        g.setColour(!isEnabled()?style.line:(filled()?style.ink:style.muted)); g.drawRect(getLocalBounds(),1);
        style.drawText(g,getButtonText(),textArea(),11,foreground(),juce::Justification::centred);
    }
private:
    RandomizeStyle style; Variant variant;
};

class RandomizePanel::LockChip final:public juce::Button {
public:
    LockChip(const RandomizeStyle& s,const juce::String& group):juce::Button(group),style(s) { setClickingTogglesState(true); setButtonText(group); setTooltip("Lock "+group+": its values stay as they are when you randomize."); }
    bool locked() const { return getToggleState(); }
    juce::Colour foreground() const { return locked()?style.light:style.ink; }
    juce::Colour background() const { return locked()?style.ink:style.light; }
    juce::Rectangle<int> textArea() const { return getLocalBounds().withTrimmedLeft(28).withTrimmedRight(6); }
    void paintButton(juce::Graphics& g,bool hover,bool down) override {
        auto bg=background(); if(!locked()) bg=down?style.line:(hover?style.paper:style.light);
        g.setColour(bg); g.fillRect(getLocalBounds()); g.setColour(locked()?style.ink:style.muted); g.drawRect(getLocalBounds(),1);
        // 12 px padlock drawn from paths: body + shackle (closed when locked, lifted on one side when open)
        const auto o=juce::Point<float>(9.f,float(getHeight())*.5f-6.f); g.setColour(foreground());
        g.fillRect(juce::Rectangle<float>(o.x,o.y+5.f,12.f,8.f));
        juce::Path shackle; shackle.startNewSubPath(o.x+3.f,o.y+5.f);
        if(locked()) { shackle.lineTo(o.x+3.f,o.y+3.f); shackle.addArc(o.x+3.f,o.y,6.f,6.f,-juce::MathConstants<float>::halfPi,juce::MathConstants<float>::halfPi,false); shackle.lineTo(o.x+9.f,o.y+5.f); }
        else { shackle.lineTo(o.x+3.f,o.y+2.f); shackle.addArc(o.x+3.f,o.y-2.f,6.f,6.f,-juce::MathConstants<float>::halfPi,juce::MathConstants<float>::halfPi,true); shackle.lineTo(o.x+9.f,o.y+2.f); }
        g.strokePath(shackle,juce::PathStrokeType(1.5f));
        style.drawText(g,getButtonText(),textArea(),11,foreground(),juce::Justification::centredLeft);
    }
private:
    RandomizeStyle style;
};

struct RandomizePanel::OutsideClicks final:juce::MouseListener {
    explicit OutsideClicks(RandomizePanel& o):panel(o) {}
    void mouseDown(const juce::MouseEvent& e) override {
        if(!panel.open) return;
        auto* c=e.eventComponent; if(c==nullptr||c==&panel||panel.isParentOf(c)) return;
        for(auto* a:panel.anchors) if(c==a||a->isParentOf(c)) return;
        panel.setOpen(false);
    }
    RandomizePanel& panel;
};

// ------------------------------------------------------------------------------------------------------------------------
// Panel
// ------------------------------------------------------------------------------------------------------------------------
namespace { constexpr int pad=16,chipTop=184,chipH=28,chipStep=36,buttonH=32; }
int RandomizePanel::heightFor(int groups) {
    const int rowsOfChips=juce::jmax(1,(groups+1)/2),chipsBottom=chipTop+chipStep*rowsOfChips-(chipStep-chipH);
    return chipsBottom+16+3*buttonH+2*8+16+24+20+20+20+16;
}

RandomizePanel::RandomizePanel(RandomizeBackend& b,RandomizeStyle s,std::function<void()> onChanged):backend(b),style(std::move(s)),changed(std::move(onChanged)) {
    seedSource=[] { return systemSeed(); };
    seed=seedSource(); locked=backend.defaultLocked();
    outside=std::make_unique<OutsideClicks>(*this);
    setComponentID("random.panel"); setName("Randomize"); setOpaque(true); setWantsKeyboardFocus(true); setVisible(false);
    closeButton=std::make_unique<GlyphButton>(style,GlyphButton::Kind::close); closeButton->setComponentID("random.close"); closeButton->setTooltip("Close"); closeButton->onClick=[this] { setOpen(false); };
    using V=PanelButton::Variant;
    mutateButton=std::make_unique<PanelButton>(style,V::toggle,"Mutate"); fullButton=std::make_unique<PanelButton>(style,V::toggle,"Full");
    for(auto* b2:{mutateButton.get(),fullButton.get()}) b2->setRadioGroupId(7301);
    mutateButton->setComponentID("random.mode.mutate"); fullButton->setComponentID("random.mode.full");
    mutateButton->setTooltip("Mutate: nudge the current patch by the amount."); fullButton->setTooltip("Full: draw new values for every unlocked group.");
    mutateButton->onClick=[this] { if(mutateButton->getToggleState()) requestMode=RandomizeMode::mutate; };
    fullButton->onClick=[this] { if(fullButton->getToggleState()) requestMode=RandomizeMode::full; };
    pinButton=std::make_unique<PanelButton>(style,V::toggle,"Pin"); pinButton->setComponentID("random.pin"); pinButton->setTooltip("Pin the seed: every roll reuses it instead of drawing a new one.");
    pinButton->onClick=[this] { seedPinned=pinButton->getToggleState(); };
    newButton=std::make_unique<PanelButton>(style,V::normal,"New"); newButton->setComponentID("random.newseed"); newButton->setTooltip("Draw a new seed without rolling."); newButton->onClick=[this] { newSeed(); };
    rollButton=std::make_unique<PanelButton>(style,V::primary,"Randomize"); rollButton->setComponentID("random.roll"); rollButton->setTooltip("Randomize with this amount, seed and locks."); rollButton->onClick=[this] { roll(false); };
    rollBButton=std::make_unique<PanelButton>(style,V::normal,"Randomize into B"); rollBButton->setComponentID("random.rollb"); rollBButton->setTooltip("Keep this patch in A and write the result into B."); rollBButton->onClick=[this] { roll(true); };
    undoButton=std::make_unique<PanelButton>(style,V::normal,"Undo Random"); undoButton->setComponentID("random.undo"); undoButton->setTooltip("Step back one random roll (up to 16)."); undoButton->onClick=[this] { undo(); };
    for(auto* c:std::initializer_list<juce::Component*>{closeButton.get(),mutateButton.get(),fullButton.get(),pinButton.get(),newButton.get(),rollButton.get(),rollBButton.get(),undoButton.get()}) addAndMakeVisible(*c);
    for(const auto& group:backend.lockGroups()) {
        auto chip=std::make_unique<LockChip>(style,group); chip->setComponentID("random.lock."+group); chip->setToggleState(locked.contains(group),juce::dontSendNotification);
        chip->onClick=[this,c=chip.get()] { const auto name=c->getButtonText(); if(c->getToggleState()) locked.addIfNotAlreadyThere(name); else locked.removeString(name); repaint(); };
        addAndMakeVisible(*chip); chips.push_back(std::move(chip));
    }
    amountSlider.setComponentID("random.amount"); amountSlider.setName("Randomize amount"); amountSlider.setSliderStyle(juce::Slider::LinearHorizontal); amountSlider.setTextBoxStyle(juce::Slider::NoTextBox,true,0,0);
    amountSlider.setRange(0.0,100.0,1.0); amountSlider.setValue(double(requestAmount)*100.0,juce::dontSendNotification); amountSlider.setColour(juce::Slider::rotarySliderFillColourId,style.blue);
    amountSlider.setTooltip("How far the roll moves values: 0 keeps the patch, 100 moves everything unlocked.");
    amountSlider.onValueChange=[this] { requestAmount=float(amountSlider.getValue())/100.f; repaint(); };
    addAndMakeVisible(amountSlider);
    seedEdit.setComponentID("random.seed"); seedEdit.setName("Randomize seed"); seedEdit.setMultiLine(false); seedEdit.setReturnKeyStartsNewLine(false); seedEdit.setInputRestrictions(12);
    seedEdit.setTooltip("Seed of the roll: 8 characters, e.g. A7F3-92C1. Type one to repeat a roll."); seedEdit.setText(formatRandomSeed(seed),juce::dontSendNotification);
    seedEdit.onReturnKey=[this] { commitSeedText(); }; seedEdit.onFocusLost=[this] { commitSeedText(); };
    addAndMakeVisible(seedEdit);
    setSize(panelWidth,heightFor(int(chips.size())));
    refreshWidgets();
}
RandomizePanel::~RandomizePanel() { juce::Desktop::getInstance().removeGlobalMouseListener(outside.get()); }

float RandomizePanel::floorSize() const { return style.textFloor?style.textFloor(*this):11.f; }
void RandomizePanel::setOpen(bool o) {
    if(open==o) return; open=o; setVisible(o);
    if(o) { juce::Desktop::getInstance().addGlobalMouseListener(outside.get()); toFront(false); grabKeyboardFocus(); refreshWidgets(); }
    else juce::Desktop::getInstance().removeGlobalMouseListener(outside.get());
    for(auto* a:anchors) if(auto* g=dynamic_cast<GlyphButton*>(a)) g->setOpenState(o);
}
void RandomizePanel::setMode(RandomizeMode m) { requestMode=m; refreshWidgets(); }
void RandomizePanel::setAmount(float a) { requestAmount=juce::jlimit(0.f,1.f,a); amountSlider.setValue(double(requestAmount)*100.0,juce::dontSendNotification); repaint(); }
void RandomizePanel::setPinned(bool b) { seedPinned=b; refreshWidgets(); }
juce::String RandomizePanel::seedText() const { return seedEdit.getText(); }
bool RandomizePanel::setSeedText(const juce::String& t) {
    std::uint64_t v=0; if(!parseRandomSeed(t,v)) { seedEdit.setText(formatRandomSeed(seed),juce::dontSendNotification); return false; }
    seed=v; seedEdit.setText(formatRandomSeed(seed),juce::dontSendNotification); return true;
}
void RandomizePanel::commitSeedText() { (void)setSeedText(seedEdit.getText()); }
void RandomizePanel::newSeed() { seed=seedSource()&randomSeedMask; seedEdit.setText(formatRandomSeed(seed),juce::dontSendNotification); }
void RandomizePanel::setSeedSource(std::function<std::uint64_t()> f) { if(f) seedSource=std::move(f); }
juce::StringArray RandomizePanel::lockedGroups() const { return locked; }
bool RandomizePanel::isLocked(const juce::String& g) const { return locked.contains(g); }
void RandomizePanel::setLocked(const juce::String& g,bool on) {
    if(!backend.lockGroups().contains(g)) return;
    if(on) locked.addIfNotAlreadyThere(g); else locked.removeString(g);
    refreshWidgets();
}
void RandomizePanel::resetLocks() { locked=backend.defaultLocked(); refreshWidgets(); }
void RandomizePanel::clearRecipe() { if(backend.recipe()) { backend.forgetRecipe(); refreshWidgets(); if(changed) changed(); } }

RandomizeRecipe RandomizePanel::roll(bool intoB) {
    commitSeedText();
    RandomizeRequest r; r.mode=requestMode; r.amount=requestAmount; r.lockedGroups=locked;
    if(!seedPinned) seed=seedSource()&randomSeedMask;
    r.seed=seed; seedEdit.setText(formatRandomSeed(seed),juce::dontSendNotification);
    const auto rec=intoB?backend.randomizeIntoB(r):backend.randomize(r);
    currentSlotB=rec.onSlotB; refreshWidgets(); if(changed) changed();
    return rec;
}
bool RandomizePanel::undo() {
    const bool ok=backend.undo();
    if(ok) { if(auto rec=backend.recipe()) currentSlotB=rec->onSlotB; refreshWidgets(); if(changed) changed(); }
    return ok;
}
juce::String RandomizePanel::presetLabel() const {
    const auto rec=backend.recipe();
    return rec&&rec->onSlotB==currentSlotB?randomPresetLabel(rec->seed):juce::String();
}
juce::String RandomizePanel::sourcePreset() const { const auto rec=backend.recipe(); return rec?rec->sourcePreset:juce::String(); }
void RandomizePanel::notePreset(const juce::String& name,bool slotB) {
    const bool differs=name!=presetName||slotB!=currentSlotB; presetName=name; currentSlotB=slotB;
    if(const auto rec=backend.recipe(); rec&&rec->sourcePreset!=name) { backend.forgetRecipe(); if(changed) changed(); }
    if(differs&&open) refreshWidgets();
}
void RandomizePanel::setHint(const juce::String& h) { if(h!=hint) { hint=h; repaint(); } }

juce::String RandomizePanel::undoText() const { return "Undo Random  "+juce::String(backend.undoDepth())+"/"+juce::String(randomUndoSteps); }
juce::String RandomizePanel::lockCountText() const { return juce::String(locked.size())+" locked"; }
juce::String RandomizePanel::sourceText() const {
    const auto rec=backend.recipe(); if(!rec) return "No random roll yet.";
    return "from "+rec->sourcePreset+(rec->intoB?"  (B slot)":"");
}
juce::String RandomizePanel::lastRollText() const {
    const auto rec=backend.recipe(); if(!rec) return {};
    if(rec->changedValues==0) return "Nothing changed (all locked)";
    return juce::String(rec->changedValues)+(rec->changedValues==1?" value":" values")+" changed in "+juce::String(rec->touchedGroups.size())+(rec->touchedGroups.size()==1?" group":" groups");
}
void RandomizePanel::refreshWidgets() {
    mutateButton->setToggleState(requestMode==RandomizeMode::mutate,juce::dontSendNotification); fullButton->setToggleState(requestMode==RandomizeMode::full,juce::dontSendNotification);
    pinButton->setToggleState(seedPinned,juce::dontSendNotification);
    for(auto& c:chips) c->setToggleState(locked.contains(c->getButtonText()),juce::dontSendNotification);
    undoButton->setButtonText(undoText()); undoButton->setEnabled(backend.undoDepth()>0);
    repaint(); for(auto* c:{static_cast<juce::Component*>(mutateButton.get()),static_cast<juce::Component*>(fullButton.get()),static_cast<juce::Component*>(pinButton.get()),static_cast<juce::Component*>(undoButton.get())}) c->repaint();
}

void RandomizePanel::resized() {
    const int w=getWidth(),inner=w-2*pad;
    rows.title={pad,16,inner-32,24}; closeButton->setBounds(w-pad-24,16,24,24);
    mutateButton->setBounds(pad,48,104,28); fullButton->setBounds(pad+104,48,104,28);
    rows.amountLabel={pad,88,64,24}; amountSlider.setBounds(88,88,168,24); rows.amountValue={264,88,w-pad-264,24};
    rows.seedLabel={pad,120,40,28}; seedEdit.setBounds(60,120,116,28); pinButton->setBounds(184,120,64,28); newButton->setBounds(256,120,w-pad-256,28);
    rows.lockHeading={pad,160,inner-96,20}; rows.lockCount={w-pad-96,160,96,20};
    const int colW=(inner-8)/2;
    for(size_t i=0;i<chips.size();++i) chips[i]->setBounds(pad+int(i%2)*(colW+8),chipTop+int(i/2)*chipStep,colW,chipH);
    const int rowsOfChips=juce::jmax(1,int(chips.size()+1)/2),chipsBottom=chipTop+chipStep*rowsOfChips-(chipStep-chipH);
    int y=chipsBottom+16;
    rollButton->setBounds(pad,y,inner,buttonH); rollBButton->setBounds(pad,y+buttonH+8,inner,buttonH); undoButton->setBounds(pad,y+2*(buttonH+8),inner,buttonH);
    y+=3*buttonH+2*8+16;
    rows.presetLabel={pad,y,56,24}; rows.presetValue={pad+64,y,inner-64,24}; rows.presetSource={pad,y+24,inner,20}; rows.hintLine={pad,y+44,inner,20}; rows.lastLine={pad,y+64,inner,20};
    seedEdit.applyFontToAllText(style.font?style.font(juce::jmax(12.f,floorSize())):juce::Font(juce::FontOptions(12.f)),true);
    seedEdit.setJustification(juce::Justification::centredLeft);
}
bool RandomizePanel::keyPressed(const juce::KeyPress& k) { if(k==juce::KeyPress::escapeKey) { setOpen(false); return true; } return false; }

std::vector<RandomizeTextItem> RandomizePanel::textItems() const {
    std::vector<RandomizeTextItem> v; using J=juce::Justification;
    auto add=[&](const char* id,const juce::String& text,juce::Rectangle<int> area,float size,juce::Colour fg,juce::Colour bg,int just=J::centredLeft,bool ellipsis=false,bool widget=false) {
        v.push_back({id,text,area,size,fg,bg,just,ellipsis,widget});
    };
    add("title","RANDOMIZE",rows.title,11,style.earthText,style.light);
    add("amount.label","Amount",rows.amountLabel,11,style.ink,style.light);
    add("amount.value",juce::String(juce::roundToInt(requestAmount*100.f))+" %",rows.amountValue,11,style.ink,style.light,J::centredRight);
    add("seed.label","Seed",rows.seedLabel,11,style.ink,style.light);
    add("lock.heading","LOCK GROUPS",rows.lockHeading,11,style.earthText,style.light);
    add("lock.count",lockCountText(),rows.lockCount,11,style.muted,style.light,J::centredRight);
    add("preset.label","Preset",rows.presetLabel,11,style.muted,style.light);
    const auto label=presetLabel();
    add("preset.value",label.isNotEmpty()?label:presetName,rows.presetValue,11,style.ink,style.light,J::centredLeft,label.isEmpty());
    add("preset.source",sourceText(),rows.presetSource,11,style.muted,style.light,J::centredLeft,true);
    add("hint",hint,rows.hintLine,11,style.muted,style.light);
    add("last",lastRollText(),rows.lastLine,11,style.ink,style.light);
    auto widgetText=[&](const juce::Component& c,const juce::String& id,const juce::String& text,juce::Rectangle<int> area,juce::Colour fg,juce::Colour bg) { (void)c; add(id.toRawUTF8(),text,area.translated(c.getX(),c.getY()),11,fg,bg,J::centred,false,true); };
    for(auto* b:{mutateButton.get(),fullButton.get(),pinButton.get(),newButton.get(),rollButton.get(),rollBButton.get(),undoButton.get()})
        widgetText(*b,b->getComponentID(),b->getButtonText(),b->textArea(),b->foreground(),b->background());
    for(auto& c:chips) { auto id=c->getComponentID(); v.push_back({id,c->getButtonText(),c->textArea().translated(c->getX(),c->getY()),11,c->foreground(),c->background(),J::centredLeft,false,true}); }
    v.push_back({"random.seed",seedEdit.getText(),seedEdit.getBounds().reduced(10,0),juce::jmax(12.f,floorSize()),style.ink,style.light,J::centredLeft,false,true});
    return v;
}

void RandomizePanel::paint(juce::Graphics& g) {
    g.fillAll(style.light); g.setColour(style.ink); g.drawRect(getLocalBounds(),1);
    const auto chipsBottom=chips.empty()?chipTop:chips.back()->getBottom();
    g.setColour(style.line); g.drawHorizontalLine(150+4,float(pad),float(getWidth()-pad)); g.drawHorizontalLine(chipsBottom+8,float(pad),float(getWidth()-pad)); g.drawHorizontalLine(undoButton->getBottom()+8,float(pad),float(getWidth()-pad));
    for(const auto& t:textItems()) if(!t.widget) style.drawText(g,t.text,t.area,t.size,t.colour,t.justification);
}
}
