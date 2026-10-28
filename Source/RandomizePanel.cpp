#include "RandomizePanel.h"
#include <algorithm>
#include <cmath>
#include <aether/Rand.h>
#include <aether/RandState.h>
#include "RandomizeController.h"
#include <cstring>
#include <tuple>
#include <array>
#include <bitset>
#include <deque>
#include <thread>

namespace ar=aether::rand;
namespace aether::ui {
namespace {
constexpr const char* seedAlphabet="0123456789ABCDEFGHJKMNPQRSTVWXYZ";
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
// EngineBackend: the popover's backend, a thin adapter over aether::rand::RandomizeController (Source/RandomizeController.h,
// owned by AetherAudioProcessor, reached through AetherAudioProcessor::randomizer()). The controller owns the 16-step undo ring,
// the LockSet, the recipe, the A/B face handling and the preset name; this class only maps UI requests to it.
//   - randomize(cfg, locks()): one call = capture the target face, generate() (which validates), commit. A patch that fails
//     validation is re-rolled with the next Config::attempt (0..5, spec 6.1); nothing is written, pushed or renamed unless one passes.
//   - LIVE FACE WRITE = ONE ATOMIC REPLACESTATE (project decision, spec P.5 recommended a gesture loop instead): the controller copies
//     the state, sets the changed values and calls APVTS::replaceState once, on the message thread only. By the JUCE sources the host
//     gets one setValueNotifyingHost per changed parameter and NO begin/endChangeGesture; automation recording of that is unverified.
//   - UNDO RECORDS THE PHYSICAL A/B FACE (not "live/alternate"): an entry taken from face A is restored into A even if the user has
//     pressed A/B since. undoRandom() therefore never toggles A/B. Whole-state undo: manual edits after the roll are undone too.
//   - Randomize into B writes only the snapshot tree of face B (cfg.intoB): no host notification, no panic, the user stays on A.
//   - Preset name = randomPresetName(source, seed) = "<source> (Random) <seed>"; set by the controller on live-face rolls only.
//   - "Modified" = hashValues(current values of the rolled face) != recipe.resultHash.
//   - generate() is not realtime safe (Patch holds a std::vector): calls are refused off the thread that made the backend (the
//     editor's message thread), and the controller refuses them as well. The UI does not measure loudness, so graph.trim
//     (trimDbForLoudness) is not written: that is the sound engineer's P2 item.
// ------------------------------------------------------------------------------------------------------------------------
juce::StringArray randomSpecGroups() { return {"Chain","ModuleParams","FM","Source","Motion","MixGlobal","System"}; }
// RandSpec 1.2 group of a parameter id, read from the engine's table (a single source of truth with the locks).
juce::String randomGroupOf(const juce::String& id) {
    const int i=ar::paramIndex(id.toStdString());
    return i<0?juce::String("System"):randomSpecGroups()[int(ar::paramTable()[size_t(i)].group)];
}

namespace {
struct EngineLocks { std::uint32_t scope=ar::ScopeAll; ar::LockSet set; };
// UI lock chips -> engine: a locked group leaves the scope (when it has a scope bit) AND locks each of its parameters by index,
// so nothing of it can be written even if a rule looks across groups. MixGlobal / System are Never in the engine (no scope bit).
EngineLocks engineLocksFor(const juce::StringArray& locked) {
    EngineLocks l; const auto table=ar::paramTable(); const auto groups=randomSpecGroups();
    for(int g=0;g<groups.size();++g) {
        if(!locked.contains(groups[g])) continue;
        for(int i=0;i<ar::kParamCount;++i) if(int(table[size_t(i)].group)==g) l.set.params.set(size_t(i));
        switch(static_cast<ar::Group>(g)) {
            case ar::Group::Chain: l.scope&=~std::uint32_t(ar::ScopeChain); break;
            case ar::Group::ModuleParams: l.scope&=~std::uint32_t(ar::ScopeModuleParams); break;
            case ar::Group::FM: l.scope&=~std::uint32_t(ar::ScopeFM); l.set.sections|=ar::kLockFm; break;
            case ar::Group::Source: l.scope&=~std::uint32_t(ar::ScopeSource); l.set.sections|=ar::kLockSource; break;
            case ar::Group::Motion: l.scope&=~std::uint32_t(ar::ScopeMotion); l.set.sections|=ar::kLockMod|(0xFu*ar::kLockLfo0); break;
            case ar::Group::MixGlobal: case ar::Group::System: break;
        }
    }
    return l;
}
// LockSet (as persisted by the controller) -> UI chips: a group counts as locked when every parameter of it is locked.
juce::StringArray groupsFromLocks(const ar::LockSet& set) {
    const auto table=ar::paramTable(); const auto groups=randomSpecGroups();
    std::array<int,7> total{},held{};
    for(int i=0;i<ar::kParamCount;++i) { const int g=int(table[size_t(i)].group); ++total[size_t(g)]; if(set.params.test(size_t(i))) ++held[size_t(g)]; }
    juce::StringArray out; for(int g=0;g<groups.size();++g) if(total[size_t(g)]>0&&held[size_t(g)]==total[size_t(g)]) out.add(groups[g]);
    return out;
}
bool lockSetEmpty(const ar::LockSet& l) { return l.params.none()&&l.sections==0&&l.modRows==0; }

class EngineBackend final:public RandomizeBackend {
public:
    explicit EngineBackend(AetherAudioProcessor& proc):p(proc),ctl(proc.randomizer()),owner(std::this_thread::get_id()) {}
    // UI section chips: the seven RandSpec 1.2 groups. MixGlobal and System hold only Never ids plus graph.trim (2.3), so they
    // start locked: the engine would not move them anyway.
    juce::StringArray lockGroups() const override { return randomSpecGroups(); }
    juce::StringArray defaultLocked() const override { return {"MixGlobal","System"}; }
    // The chips follow the controller's LockSet (restored with the plugin state). Until a lock was ever written (an empty LockSet
    // and no chip touched in this session) the default applies; an all-unlocked choice that is saved and reloaded reads as the default again.
    juce::StringArray lockedGroups() const override {
        const auto set=ctl.locks();
        if(lockSetEmpty(set)&&!locksWritten) return defaultLocked();
        return groupsFromLocks(set);
    }
    void setLockedGroups(const juce::StringArray& groups) override {
        ctl.setLocks(engineLocksFor(groups).set); locksWritten=true;
    }
    RandomizeRecipe randomize(const RandomizeRequest& r) override { return run(r,false); }
    RandomizeRecipe randomizeIntoB(const RandomizeRequest& r) override { return run(r,true); }
    bool undo() override {
        if(!onOwnerThread()||!ctl.canUndoRandom()) return false;
        const auto out=ctl.undoRandom();
        if(out.ok) dismissed.reset();
        return out.ok;
    }
    int undoDepth() const override { return ctl.undoDepth(); }
    std::optional<RandomizeRecipe> recipe() const override {
        const auto r=ctl.recipe();
        if(!r.valid||(dismissed&&*dismissed==keyOf(r))) return std::nullopt;
        RandomizeRecipe out; out.seed=r.seed; out.mode=r.mode==ar::Mode::Full?RandomizeMode::full:RandomizeMode::mutate; out.amount=r.amount;
        out.lockedGroups=groupsFromLocks(ctl.locks()); out.sourcePreset=juce::String(juce::CharPointer_UTF8(r.sourcePreset.c_str()));
        out.intoB=r.intoB; out.algoVersion=r.algoVersion; out.attempt=r.attempt;
        // The face the roll sits on: remembered from the roll made in this session; a recipe restored from a saved state has no
        // record, so intoB means B and a live roll means the face that is live now.
        const auto info=infoFor(r); const auto liveFace=ctl.liveFace();
        const auto face=info?info->face:(r.intoB?ar::Face::B:liveFace);
        out.onLiveFace=face==liveFace;
        if(info) { out.changedValues=info->changedValues; out.touchedGroups=info->touchedGroups; out.note=info->note; } else out.changedValues=-1;  // -1: not known (state was reloaded)
        out.modified=ar::hashValues(ctl.captureSnapshot(face).value)!=r.resultHash;
        return out;
    }
    void forgetRecipe() override { const auto r=ctl.recipe(); if(r.valid) dismissed=keyOf(r); }
private:
    using Key=std::tuple<std::uint64_t,std::int64_t,std::uint64_t>;   // seed, time, result hash
    static Key keyOf(const ar::RandomRecipe& r) { return {r.seed,r.timeUtcMs,r.resultHash}; }
    struct Info { Key key; ar::Face face; int changedValues; juce::StringArray touchedGroups; juce::String note; };
    AetherAudioProcessor& p; ar::RandomizeController& ctl; std::thread::id owner;
    std::deque<Info> infos; std::optional<Key> dismissed; bool locksWritten=false;
    bool onOwnerThread() const { return std::this_thread::get_id()==owner; }
    const Info* infoFor(const ar::RandomRecipe& r) const { for(auto it=infos.rbegin();it!=infos.rend();++it) if(it->key==keyOf(r)) return &*it; return nullptr; }
    RandomizeRecipe run(const RandomizeRequest& r,bool intoB) {
        RandomizeRecipe rec; rec.seed=r.seed&randomSeedMask; rec.mode=r.mode; rec.amount=r.amount; rec.lockedGroups=r.lockedGroups; rec.intoB=intoB;
        { const auto cur=ctl.recipe(); rec.sourcePreset=cur.valid?juce::String(juce::CharPointer_UTF8(cur.sourcePreset.c_str())):p.currentPresetName(); }
        rec.algoVersion=ar::kRandAlgoVersion;
        if(!onOwnerThread()) { rec.rejected=true; rec.note="wrong thread"; return rec; }
        ar::Config cfg; cfg.seed=rec.seed; cfg.mode=r.mode==RandomizeMode::full?ar::Mode::Full:ar::Mode::Mutate;
        cfg.amount=juce::jlimit(0.f,1.f,r.amount); cfg.safety=ar::Safety::Safe; cfg.intoB=intoB;
        const auto locks=engineLocksFor(r.lockedGroups); cfg.scopeMask=locks.scope;
        setLockedGroups(r.lockedGroups);                       // the controller's LockSet is the one that gets saved with the state
        const auto face=intoB?ar::Face::B:ctl.liveFace();
        const auto before=ctl.captureSnapshot(face);
        ar::Outcome out; juce::String why;
        for(int attempt=0;attempt<=5;++attempt) {
            cfg.attempt=attempt; out=ctl.randomize(cfg,ctl.locks());
            if(out.ok) break;
            why=out.reason&&*out.reason?out.reason:"generate";
            if(why=="not-message-thread") break;
        }
        if(!out.ok) { rec.rejected=true; rec.note=why=="not-message-thread"?juce::String("wrong thread"):"no safe patch: "+why; return rec; }   // nothing written, ring and recipe untouched
        rec.attempt=cfg.attempt;
        const auto after=ctl.captureSnapshot(face); const auto table=ar::paramTable();
        for(int i=0;i<ar::kParamCount;++i) if(std::memcmp(&before.value[size_t(i)],&after.value[size_t(i)],sizeof(float))!=0) rec.touchedGroups.addIfNotAlreadyThere(randomSpecGroups()[int(table[size_t(i)].group)]);
        rec.changedValues=out.changedParams;
        if(rec.changedValues==0) rec.note=r.lockedGroups.size()>=7?"all locked":(cfg.mode==ar::Mode::Mutate&&cfg.amount<=0.f?"amount 0":"no change");
        rec.onLiveFace=out.wroteLive; dismissed.reset();
        if(rec.changedValues>0) {      // an unchanged roll leaves the controller's recipe and ring alone
            const auto cr=ctl.recipe();
            if(cr.valid) { infos.push_back({keyOf(cr),face,rec.changedValues,rec.touchedGroups,rec.note}); while(int(infos.size())>randomUndoSteps+1) infos.pop_front(); }
            rec.modified=false;
        }
        return rec;
    }
};
}

std::unique_ptr<RandomizeBackend> makeEngineBackend(AetherAudioProcessor& p) { return std::make_unique<EngineBackend>(p); }

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
    seed=seedSource(); locked=backend.lockedGroups();
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
        chip->onClick=[this,c=chip.get()] { const auto name=c->getButtonText(); if(c->getToggleState()) locked.addIfNotAlreadyThere(name); else locked.removeString(name); backend.setLockedGroups(locked); repaint(); };
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
    backend.setLockedGroups(locked); refreshWidgets();
}
void RandomizePanel::resetLocks() { locked=backend.defaultLocked(); backend.setLockedGroups(locked); refreshWidgets(); }
void RandomizePanel::clearRecipe() { noChangeNote={}; if(backend.recipe()) { backend.forgetRecipe(); refreshWidgets(); if(changed) changed(); } }

void RandomizePanel::setBusy(bool b) {
    if(busy==b) return; busy=b; refreshWidgets(); if(changed) changed();
}
RandomizeRecipe RandomizePanel::roll(bool intoB) {
    if(busy) { RandomizeRecipe rec; rec.rejected=true; rec.note="re-rolling"; return rec; }   // reserved for the audition stage (graph.trim compensation, sound engineer P2)
    commitSeedText();
    RandomizeRequest r; r.mode=requestMode; r.amount=requestAmount; r.lockedGroups=locked;
    if(!seedPinned) seed=seedSource()&randomSeedMask;
    r.seed=seed; seedEdit.setText(formatRandomSeed(seed),juce::dontSendNotification);
    const auto rec=intoB?backend.randomizeIntoB(r):backend.randomize(r);
    noChangeNote=rec.rejected?juce::String("Nothing changed (no safe patch)"):(rec.changedValues==0?"Nothing changed ("+(rec.note.isEmpty()?juce::String("no change"):rec.note)+")":juce::String());
    refreshWidgets(); if(changed) changed();
    return rec;
}
bool RandomizePanel::undo() {
    if(busy) return false;
    const bool ok=backend.undo();
    if(ok) { noChangeNote={}; refreshWidgets(); if(changed) changed(); }
    return ok;
}
juce::String RandomizePanel::presetLabel() const {
    const auto rec=backend.recipe();
    return rec&&!rec->intoB&&rec->onLiveFace?randomPresetLabel(rec->seed):juce::String();
}
juce::String RandomizePanel::sourcePreset() const { const auto rec=backend.recipe(); return rec?rec->sourcePreset:juce::String(); }
// Called by the editor's timer. The processor's preset name is the truth: a roll on the live face renames it to
// randomPresetName(source, seed), so the recipe stays on screen while the name is that one. Any other change of the name
// (a preset was loaded) ends the roll on screen. Also follows the controller's LockSet (a state load restores the locks)
// and the "modified" mark of the roll (values edited since).
void RandomizePanel::notePreset(const juce::String& name,bool) {
    const bool nameChanged=havePresetName&&name!=presetName; presetName=name; havePresetName=true;
    bool refresh=false;
    if(const auto rec=backend.recipe()) {
        const auto expected=juce::String(juce::CharPointer_UTF8(ar::randomPresetName(std::string(rec->sourcePreset.toRawUTF8()),rec->seed).c_str()));
        if(nameChanged&&name!=expected) { noChangeNote={}; backend.forgetRecipe(); if(changed) changed(); refresh=true; }
        else if(rec->modified!=shownModified) refresh=true;
        shownModified=backend.recipe()?backend.recipe()->modified:false;
    } else shownModified=false;
    if(const auto groups=backend.lockedGroups(); groups!=locked) { locked=groups; refresh=true; }
    if((refresh||nameChanged)&&open) refreshWidgets();
}
void RandomizePanel::setHint(const juce::String& h) { if(h!=hint) { hint=h; repaint(); } }

juce::String RandomizePanel::undoText() const { return "Undo Random  "+juce::String(backend.undoDepth())+"/"+juce::String(randomUndoSteps); }
juce::String RandomizePanel::lockCountText() const { return juce::String(locked.size())+" locked"; }
juce::String RandomizePanel::sourceText() const {
    const auto rec=backend.recipe(); if(!rec) return "No random roll yet.";
    return "from "+rec->sourcePreset+(rec->intoB?"  (in B)":"");
}
juce::String RandomizePanel::lastRollText() const {
    if(busy) return "Re-rolling...";
    if(noChangeNote.isNotEmpty()) return noChangeNote;
    const auto rec=backend.recipe(); if(!rec) return {};
    if(rec->changedValues<0) return rec->modified?"Edited since the roll":juce::String();   // restored from a saved state: the counts were not saved
    return juce::String(rec->changedValues)+(rec->changedValues==1?" value":" values")+" changed in "+juce::String(rec->touchedGroups.size())+(rec->touchedGroups.size()==1?" group":" groups")+(rec->modified?", edited":"");
}
void RandomizePanel::refreshWidgets() {
    mutateButton->setToggleState(requestMode==RandomizeMode::mutate,juce::dontSendNotification); fullButton->setToggleState(requestMode==RandomizeMode::full,juce::dontSendNotification);
    pinButton->setToggleState(seedPinned,juce::dontSendNotification);
    for(auto& c:chips) c->setToggleState(locked.contains(c->getButtonText()),juce::dontSendNotification);
    undoButton->setButtonText(undoText()); undoButton->setEnabled(!busy&&backend.undoDepth()>0);
    rollButton->setEnabled(!busy); rollBButton->setEnabled(!busy);
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
    // The seed text is inserted in the constructor, before the editor sees the look-and-feel colours; repaint it in ink or it stays near-invisible until the first roll.
    seedEdit.setColour(juce::TextEditor::textColourId,style.ink); seedEdit.applyColourToAllText(style.ink,true);
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
