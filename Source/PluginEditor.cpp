#include "PluginEditor.h"
#include <aether/Modular.h>
#include <aether/FmSynth.h>
#include "BinaryData.h"
#include <cmath>
#include <map>

namespace {
// Atlas lays out on a 1440x900 logical canvas that is scaled to fit the editor window.
constexpr float baseWidth=1440.f,baseHeight=900.f;
// Below this fit scale (the 50% preset and similarly small windows) Atlas switches to a compact 1000x625 layout so text stays >=10 effective px without truncation.
// Default-layout FM algorithm picker: wide enough for the longest "NN  Name / Subtitle" entry at the largest text floor.
constexpr int fmAlgorithmWidth=320;
// Default-layout FM: y of the rule above the operator columns, below the matrix, hint and live-voice texts (stage.y + 268..292).
constexpr int fmOperatorTop=296;
// Compact Motion rows: width of the destination box, sized so the longest destination name (Source / FM groups included) fits at the largest compact text floor.
constexpr int motionDestWidth=212;
constexpr float compactWidth=1000.f,compactHeight=625.f,compactBelow=.72f;
// Text is never allowed below 11 logical px or 10 effective (on-screen) px, whichever is larger. Capped so very small windows stay laid out.
float legibleFloor(float scale){return juce::jlimit(11.f,16.f,10.f/juce::jmax(.25f,scale));}
// Scale of the Atlas content transform for the component currently being drawn (set from Impl::paint, message thread only).
float uiTextScale=1.f;
// Plugin edition: AETHER Micro Synth (JucePlugin_IsSynth) offers only Synth mode; AETHER Micro (effect) offers only FX and Resynth. Chosen at compile time;
// the override exists so tests and snapshot tools can exercise both editions from one binary (-1 = compile-time edition).
int editionOverride=-1;
// Randomize backend factory (set by tests and snapshot tools); without one the editor has no Randomize controls.
AetherAudioProcessorEditor::RandomizeBackendFactory randomizeFactory;
bool synthEdition(){if(editionOverride>=0)return editionOverride>0;
#if JucePlugin_IsSynth
    return true;
#else
    return false;
#endif
}
// global.mode keeps its parameter choices (0 FX, 1 Resynth, 2 Synth); the UI only offers the applicable ones and maps combo item ids to the real index (id = index + 1).
bool modeApplicable(int index){return synthEdition()?index==2:(index==0||index==1);}
// What the panel shows for a stored mode value: an inapplicable value (e.g. Synth restored into the effect, or FX into the Synth edition) falls back to the edition's own mode.
int modeShown(int raw){raw=juce::jlimit(0,2,raw);return modeApplicable(raw)?raw:(synthEdition()?2:0);}
int modeSounding(){return synthEdition()?2:1;}
// Modulation destinations are grouped by the label before " / " ("Node 3 / Mix" -> "Node 3"; the new Source and FM destinations will carry "Source" / "FM"); items without one (None) stay ungrouped.
juce::String targetGroupOf(const juce::String& item){const int cut=item.indexOf(" / ");return cut>0?item.substring(0,cut):juce::String();}
// Fill a destination box with its items (ids = real index + 1, exactly what ComboBoxAttachment expects) and a non-selectable heading at the start of every group; headings are not items, so indices never shift.
void fillTargetCombo(juce::ComboBox& combo,const juce::StringArray& items){
    juce::String group;
    for(int i=0;i<items.size();++i){const auto g=targetGroupOf(items[i]);if(g.isNotEmpty()&&g!=group)combo.addSectionHeading(g);group=g;combo.addItem(items[i],i+1);}
}
float scaleOf(const juce::Component& c){float s=1.f;for(const auto* q=&c;q!=nullptr;q=q->getParentComponent())s*=q->getTransform().getScaleFactor();return s;}
const juce::Colour paper{0xffeee9dc}, light{0xfff7f3e9}, ink{0xff252b2b}, muted{0xff575a51}, earthText{0xff963c24}, line{0xffcfcbbd}, earth{0xffb65337}, blue{0xff264fba}, pale{0xffded9ca}, inactive{0xff6f6c62};
juce::Font type(float size, bool display=false) {
    static auto serif=juce::Typeface::createSystemTypefaceFor(BinaryData::InstrumentSerifRegular_ttf,BinaryData::InstrumentSerifRegular_ttfSize);
    static auto mono=juce::Typeface::createSystemTypefaceFor(BinaryData::DMMonoRegular_ttf,BinaryData::DMMonoRegular_ttfSize);
    return juce::Font(juce::FontOptions(display?serif:mono).withHeight(size));
}
void text(juce::Graphics& g,const juce::String& s,juce::Rectangle<int> r,float size=11,juce::Colour colour=ink,int justification=juce::Justification::centredLeft,bool display=false) {
    // earth is a graphic colour (4.05:1 on paper); text uses the darker earthText (5.84:1).
    if(colour==earth)colour=earthText;
    g.setColour(colour); g.setFont(type(display?size:juce::jmax(legibleFloor(uiTextScale),size),display)); g.drawText(s,r,justification,true);
}
juce::String nid(int n,const juce::String& suffix) {return "node."+juce::String(n+1)+"."+suffix;}
juce::String oid(int n,const juce::String& suffix) {return "fm.op."+juce::String(n+1)+"."+suffix;}
juce::String rid(int s,int d) {return "route."+juce::String(s+1)+"."+juce::String(d+1);}
juce::String eid(int s,int d) {return "fm.edge."+juce::String(s+1)+"."+juce::String(d+1);}
const aether::ParameterSpec* specFor(const juce::String& id) { for(const auto& s:aether::parameterSpecs()) if(juce::String(s.id)==id)return &s; return nullptr; }
void arch(juce::Graphics& g,juce::Rectangle<float> r,float alpha=1) {
    auto outer=r; juce::Path p; p.startNewSubPath(outer.getX(),outer.getBottom()); p.lineTo(outer.getX(),outer.getY()+outer.getWidth()/2);
    p.cubicTo(outer.getX(),outer.getY()-outer.getWidth()/6,outer.getRight(),outer.getY()-outer.getWidth()/6,outer.getRight(),outer.getY()+outer.getWidth()/2);
    p.lineTo(outer.getRight(),outer.getBottom()); p.closeSubPath();
    g.setColour(pale.withAlpha(alpha));g.fillPath(p);
    auto inner=r.reduced(r.getWidth()*.24f,0).withTrimmedTop(r.getWidth()*.24f); juce::Path hole;
    hole.startNewSubPath(inner.getX(),inner.getBottom());hole.lineTo(inner.getX(),inner.getY()+inner.getWidth()/2);
    hole.cubicTo(inner.getX(),inner.getY()-inner.getWidth()/6,inner.getRight(),inner.getY()-inner.getWidth()/6,inner.getRight(),inner.getY()+inner.getWidth()/2);
    hole.lineTo(inner.getRight(),inner.getBottom());hole.closeSubPath();g.setColour(paper.withAlpha(alpha));g.fillPath(hole);
}
class AtlasTheme final:public juce::LookAndFeel_V4 {
public:
    AtlasTheme(){
        setColour(juce::ComboBox::backgroundColourId,light);setColour(juce::ComboBox::outlineColourId,line);setColour(juce::ComboBox::textColourId,ink);setColour(juce::ComboBox::arrowColourId,earth);
        setColour(juce::PopupMenu::backgroundColourId,light);setColour(juce::PopupMenu::textColourId,ink);setColour(juce::PopupMenu::highlightedBackgroundColourId,blue);setColour(juce::PopupMenu::highlightedTextColourId,light);
        setColour(juce::TextEditor::backgroundColourId,light);setColour(juce::TextEditor::textColourId,ink);setColour(juce::TextEditor::outlineColourId,line);
        setColour(juce::Slider::textBoxTextColourId,ink);setColour(juce::Slider::textBoxOutlineColourId,line);setColour(juce::Slider::textBoxBackgroundColourId,light);setColour(juce::Label::textColourId,ink);setColour(juce::Label::backgroundColourId,juce::Colours::transparentBlack);
        setColour(juce::TooltipWindow::backgroundColourId,ink);setColour(juce::TooltipWindow::textColourId,light);setColour(juce::TooltipWindow::outlineColourId,ink);
        setColour(juce::ScrollBar::thumbColourId,line);
    }
    juce::Font getComboBoxFont(juce::ComboBox& c) override{return type(juce::jmax(12.f,legibleFloor(scaleOf(c))));}
    juce::Font getPopupMenuFont() override{return type(12);}
    juce::Font getTextButtonFont(juce::TextButton& b,int) override{return type(legibleFloor(scaleOf(b)));}
    juce::Font getLabelFont(juce::Label& l) override{return type(juce::jmax(12.f,legibleFloor(scaleOf(l))));}
    juce::Label* createSliderTextBox(juce::Slider& slider) override {auto* label=juce::LookAndFeel_V4::createSliderTextBox(slider);label->setFont(type(12));label->setColour(juce::Label::textColourId,ink);label->setColour(juce::Label::backgroundColourId,slider.findColour(juce::Slider::textBoxBackgroundColourId));label->setColour(juce::Label::outlineColourId,line);label->setColour(juce::TextEditor::textColourId,ink);label->setColour(juce::TextEditor::backgroundColourId,slider.findColour(juce::Slider::textBoxBackgroundColourId));return label;}
    void drawButtonBackground(juce::Graphics& g,juce::Button& b,const juce::Colour&,bool hover,bool down) override{
        g.setColour(b.getToggleState()?ink:(down?line:(hover?light:paper)));g.fillRect(b.getLocalBounds());
        g.setColour(b.getToggleState()?ink:line);g.drawRect(b.getLocalBounds(),1);
    }
    void drawButtonText(juce::Graphics& g,juce::TextButton& b,bool,bool) override{text(g,b.getButtonText(),b.getLocalBounds().reduced(5,0),11,b.getToggleState()?light:ink,juce::Justification::centred);}
    void drawToggleButton(juce::Graphics& g,juce::ToggleButton& b,bool,bool) override{
        auto r=b.getLocalBounds();const auto id=b.getComponentID();const bool on=b.getToggleState();
        const bool bypass=id=="global.bypass",enable=id.endsWith(".on")||id.endsWith(".enabled");
        // Bypass engaged: solid earthText chip with light text (6.38:1) instead of a faint dot.
        if(bypass&&on){g.setColour(earthText);g.fillRect(r.reduced(0,2));text(g,"BYPASS ON",r.reduced(8,0),11,light,juce::Justification::centred);return;}
        // Disabled (on/enable switches off): hollow earthText ring and earthText label, clearly different from the blue active dot.
        if(enable&&!on){g.setColour(earthText);g.drawEllipse(3.5f,float(r.getCentreY())-3.5f,7.f,7.f,1.5f);text(g,b.getButtonText(),r.withTrimmedLeft(17),10,earthText);return;}
        g.setColour(on?blue:muted);if(on)g.fillEllipse(3.f,float(r.getCentreY()-4),8,8);else g.drawEllipse(3.5f,float(r.getCentreY())-3.5f,7.f,7.f,1.5f);text(g,b.getButtonText(),r.withTrimmedLeft(17),10,on?ink:muted);
    }
    void drawLinearSlider(juce::Graphics& g,int x,int y,int w,int h,float pos,float,float,juce::Slider::SliderStyle,juce::Slider& s) override {
        const float cy=float(y)+float(h)*.5f;g.setColour(line);g.drawLine(float(x),cy,float(x+w),cy,1);g.setColour(s.findColour(juce::Slider::rotarySliderFillColourId));g.drawLine(float(x),cy,pos,cy,2);g.fillEllipse(pos-3,cy-3,6,6);
    }
    void drawRotarySlider(juce::Graphics& g,int x,int y,int w,int h,float v,float start,float end,juce::Slider& s) override{
        const auto r=juce::Rectangle<float>(float(x),float(y),float(w),float(h)).reduced(8);auto c=r.getCentre();const float radius=juce::jmin(r.getWidth(),r.getHeight())/2;
        g.setColour(ink.withAlpha(.08f));g.fillEllipse(c.x-radius+5,c.y-radius+7,radius*2,radius*2);g.setColour(light);g.fillEllipse(c.x-radius,c.y-radius,radius*2,radius*2);g.setColour(line);g.drawEllipse(c.x-radius,c.y-radius,radius*2,radius*2,1);
        juce::Path a;a.addCentredArc(c.x,c.y,radius+3,radius+3,0,start,start+(end-start)*v,true);g.setColour(s.findColour(juce::Slider::rotarySliderFillColourId));g.strokePath(a,juce::PathStrokeType(2));
        const float angle=start+(end-start)*v-juce::MathConstants<float>::halfPi;g.drawLine(c.x+std::cos(angle)*radius*.4f,c.y+std::sin(angle)*radius*.4f,c.x+std::cos(angle)*radius*.8f,c.y+std::sin(angle)*radius*.8f,2);
    }
};
// JUCE arrow-key edits call setValue directly; provide the same gesture boundary as mouse, wheel and text edits.
class InstrumentSlider final:public juce::Slider {
public:
    // Snapshot effective routing before JUCE attachment listeners begin the edited parameter gesture.
    std::function<void()> beforeGesture;
    void startedDragging() override {if(beforeGesture)beforeGesture();juce::Slider::startedDragging();}
    bool keyPressed(const juce::KeyPress& key) override {
        const bool arrow=key==juce::KeyPress::leftKey||key==juce::KeyPress::rightKey||key==juce::KeyPress::upKey||key==juce::KeyPress::downKey;
        if(isEnabled()&&arrow&&!key.getModifiers().isAnyModifierKeyDown()){juce::Slider::ScopedDragNotification gesture(*this);return juce::Slider::keyPressed(key);}
        return juce::Slider::keyPressed(key);
    }
};
class ParamControl final:public juce::Component {
public:
    ParamControl(AetherAudioProcessor& p,const juce::String& id,const juce::String& caption={},bool rotary=false):parameterId(id){
        rotaryStyle=rotary;setName(id);setComponentID(id);const auto* spec=specFor(id);if(!spec)return;
        title=caption.isEmpty()?juce::String(spec->name):caption;
        if(spec->boolean){toggle.setButtonText(title);addAndMakeVisible(toggle);toggle.setComponentID(id);toggle.setName(id);buttonAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(p.state,id,toggle);}
        else if(id=="global.mode"&&p.state.getParameter(id)!=nullptr){
            // ComboBoxAttachment maps the selected item *index* onto the parameter, which would misalign once inapplicable modes are hidden; this keeps item ids = real indices and
            // binds through juce::ParameterAttachment (same host gesture / automation path).
            for(int i=0;i<spec->choices.size();++i)if(modeApplicable(i))combo.addItem(spec->choices[i],i+1);
            addAndMakeVisible(combo);combo.setComponentID(id);combo.setName(id);
            modeAttachment=std::make_unique<juce::ParameterAttachment>(*p.state.getParameter(id),[this](float v){combo.setSelectedId(modeShown(juce::roundToInt(v))+1,juce::dontSendNotification);});
            combo.onChange=[this]{const int index=combo.getSelectedId()-1;if(index>=0&&modeAttachment)modeAttachment->setValueAsCompleteGesture(float(index));};
            modeAttachment->sendInitialUpdate();
        }
        else if(!spec->choices.isEmpty()){if(id.startsWith("modulation.")&&id.endsWith(".target"))fillTargetCombo(combo,spec->choices);else combo.addItemList(spec->choices,1);addAndMakeVisible(combo);combo.setComponentID(id);combo.setName(id);choiceAttachment=std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(p.state,id,combo);}
        else {addAndMakeVisible(slider);slider.setComponentID(id);slider.setName(id);slider.setSliderStyle(rotary?juce::Slider::RotaryHorizontalVerticalDrag:juce::Slider::LinearHorizontal);slider.setTextBoxStyle(rotary?juce::Slider::TextBoxBelow:juce::Slider::TextBoxRight,false,rotary?90:74,18);slider.setColour(juce::Slider::rotarySliderFillColourId,earth);slider.setColour(juce::Slider::textBoxTextColourId,ink);slider.setColour(juce::Slider::textBoxBackgroundColourId,light);slider.setColour(juce::Slider::textBoxOutlineColourId,line);slider.setDoubleClickReturnValue(true,spec->defaultValue);sliderAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.state,id,slider);if(id.startsWith("route.")||(id.startsWith("node.")&&(id.endsWith(".input")||id.endsWith(".output"))))slider.beforeGesture=[&p]{p.prepareGraphEditing();};else if(id.startsWith("fm.edge."))slider.beforeGesture=[&p]{p.prepareFmEditing();};if(spec->step>=1.f){slider.textFromValueFunction=[](double v){return juce::String(juce::roundToInt(v));};slider.updateText();}}
        setTooltipText(juce::String(spec->group)+" / "+title+". Double click a slider to restore its default.");
    }
    void setTooltipText(const juce::String& t){slider.setTooltip(t);combo.setTooltip(t);toggle.setTooltip(t);}
    // Inactive style: text keeps its full contrast (ink / muted on paper); only the graphic parts go grey (track fill, thumb) and the value box / combo use the pale ground.
    void setDimmed(bool d){if(d==dimmed)return;dimmed=d;slider.setColour(juce::Slider::rotarySliderFillColourId,d?inactive:earth);slider.setColour(juce::Slider::textBoxBackgroundColourId,d?pale:light);combo.setColour(juce::ComboBox::backgroundColourId,d?pale:light);repaint();}
    void macro(const aether::modular::MacroInfo& info){title=info.name;const auto lo=info.minimum,hi=info.maximum;const juce::String unit=info.unit;slider.textFromValueFunction=[lo,hi,unit](double v){return juce::String(lo+float(v)*(hi-lo),hi-lo>100?0:2)+(unit.isEmpty()?"":" "+unit);};slider.valueFromTextFunction=[lo,hi](const juce::String& v){return (v.getDoubleValue()-lo)/juce::jmax(.00001f,hi-lo);};slider.updateText();resized();repaint();}
    void paint(juce::Graphics& g)override{if(!buttonAttachment&&captionShown)text(g,title,getLocalBounds().removeFromTop(17),10,muted);}
    // Compact layout: sliders become linear with a wider value box (knobs do not fit), optionally without the caption row.
    void setCompact(bool c,bool caption=true){compact=c;captionShown=caption;if(sliderAttachment){const bool rot=rotaryStyle&&!c;slider.setSliderStyle(rot?juce::Slider::RotaryHorizontalVerticalDrag:juce::Slider::LinearHorizontal);boxWidth=0;slider.setTextBoxStyle(rot?juce::Slider::TextBoxBelow:juce::Slider::TextBoxRight,false,rot?90:(c?78:74),c?20:18);}resized();repaint();}
    void resized()override{auto r=getLocalBounds();if(buttonAttachment){toggle.setBounds(r);return;}r.removeFromTop(captionShown?17:0);if(choiceAttachment||modeAttachment)combo.setBounds(r.reduced(0,1));else{fitValueBox(r.getWidth());slider.setBounds(r);}}
    // Widen the value box to the longest of min / max / current text at the real font so extremes such as "-100.00 ct" are never truncated.
    void fitValueBox(int total){if(rotaryStyle&&!compact)return;const auto font=type(juce::jmax(12.f,legibleFloor(scaleOf(*this))));int need=0;for(double v:{slider.getMinimum(),slider.getMaximum(),slider.getValue()})need=juce::jmax(need,juce::GlyphArrangement::getStringWidthInt(font,slider.getTextFromValue(v)));const int base=compact?78:74,w=juce::jmax(base,juce::jmin(need+12,total*5/8));if(w!=boxWidth){boxWidth=w;slider.setTextBoxStyle(juce::Slider::TextBoxRight,false,w,compact?20:18);}}
    juce::String parameterId,title;InstrumentSlider slider;juce::ComboBox combo;juce::ToggleButton toggle;bool rotaryStyle=false,compact=false,captionShown=true,dimmed=false;int boxWidth=0;
private:
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> choiceAttachment;
    std::unique_ptr<juce::ParameterAttachment> modeAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;
};
class Surface:public juce::Component {public:std::function<void(juce::Graphics&)> draw;std::function<void(const juce::MouseEvent&)> down,drag,up;void paint(juce::Graphics& g)override{if(draw)draw(g);}void mouseDown(const juce::MouseEvent& e)override{if(down)down(e);}void mouseDrag(const juce::MouseEvent& e)override{if(drag)drag(e);}void mouseUp(const juce::MouseEvent& e)override{if(up)up(e);}};
}

struct AetherAudioProcessorEditor::Impl final:juce::Component,private juce::Timer {
    AetherAudioProcessorEditor& owner;AetherAudioProcessor& p;AtlasTheme theme;
    juce::TooltipWindow tooltips{this,550};
    juce::ComboBox presets,algorithm,fmAlgorithm,zoom;juce::TextButton load{"Open"},save{"Save"},ab{"A / B"},panic{"Panic"},help{"?"},minimalButton{"All params"},patchTab{"01  Patch"},fmTab{"02  FM"},motionTab{"03  Motion"},sourceTab{"04  Source"};juce::Label statusLabel,noticeLabel;
    juce::TextEditor search;juce::Viewport browserViewport,allViewport;Surface browser,canvas,gallery,fmDiagram,fmMatrix,motionScope,allBody,sourceEnvelope;
    std::vector<std::unique_ptr<ParamControl>> globals,inspector,fmControls,motionControls,allControls,sourceControls;
    InstrumentSlider cableGain;juce::TextButton removeCable{"Remove cable"};
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> cableAttachment;
    std::unique_ptr<juce::FileChooser> chooser;std::vector<int> browserItems;
    std::unique_ptr<aether::ui::RandomizeBackend> randomBackend;std::unique_ptr<aether::ui::RandomizePanel> randomPanel;juce::TextButton randomRoll{"Randomize"};std::unique_ptr<aether::ui::GlyphButton> randomCaret;
    juce::Rectangle<int> content,left,right,stage,footer;std::array<juce::Point<float>,12> nodePoints{};
    int workspace=0,slot=0,lastType=-1,lastEngine=-1,source=-1,selectedSource=-1,selectedDestination=-1;bool minimal=false,compact=false;float zoomFactor=1.f;juce::Point<float> cableEnd;juce::String currentName;
    Impl(AetherAudioProcessorEditor& o,AetherAudioProcessor& proc):owner(o),p(proc){
        setLookAndFeel(&theme);setOpaque(true);cableGain.setColour(juce::Slider::textBoxTextColourId,ink);cableGain.setColour(juce::Slider::textBoxBackgroundColourId,light);cableGain.setColour(juce::Slider::textBoxOutlineColourId,line);
        for(auto* b:{&load,&save,&ab,&panic,&help,&minimalButton,&patchTab,&fmTab,&motionTab,&sourceTab}){addAndMakeVisible(*b);b->setName(b->getButtonText());}
        help.setComponentID("help");help.setTooltip("Patching, feedback and intrinsic texture delay");help.onClick=[this]{juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::InfoIcon,"Atlas / field notes","PATCH: Select a numbered slot, then choose a material. Drag an earth-coloured OUT port to a blue IN port, or click the two ports. Click a cable to adjust signed gain; right-click removes it. Self and backward cables use one sample of delay and the bounded Feedback return control.\n\nFM: Choose an architecture or click the matrix to cycle 0, 0.5 and 1. Right-click clears. Editing switches to the stored custom matrix.\n\nMOTION: Assign a source and destination, then set signed depth. Beat sync rates are cycles per beat.\n\nTEXTURE DELAY: Spectral pitch, granular and freeze materials have intrinsic processing delay. The live TEXTURE figure reports the longest active material delay; host PDC remains zero.\n\nAll params opens the complete scrollable parameter atlas, including the original v1 controls.","Close",this);};load.setComponentID("preset.open");save.setComponentID("preset.save");ab.setComponentID("preset.ab");panic.setComponentID("panic");minimalButton.setComponentID("workspace.minimal");patchTab.setComponentID("workspace.patch");fmTab.setComponentID("workspace.fm");motionTab.setComponentID("workspace.motion");sourceTab.setComponentID("workspace.source");
        patchTab.onClick=[this]{setWorkspace(0);};fmTab.onClick=[this]{setWorkspace(1);};motionTab.onClick=[this]{setWorkspace(2);};sourceTab.onClick=[this]{setWorkspace(3);};minimalButton.onClick=[this]{setMinimal(!minimal);};panic.onClick=[this]{p.requestPanic();};ab.onClick=[this]{p.toggleAB();};load.onClick=[this]{choose(false);};save.onClick=[this]{choose(true);};
        presets.setText(p.currentPresetName(),juce::dontSendNotification);currentName=p.currentPresetName();addAndMakeVisible(presets);presets.setName("Factory preset");presets.setComponentID("preset.factory");presets.addItemList(AetherAudioProcessor::factoryPresetNames(),1);presets.onChange=[this]{if(presets.getSelectedId()>0){p.loadFactoryPreset(presets.getSelectedId()-1);if(randomPanel)randomPanel->clearRecipe();}};
        addAndMakeVisible(zoom);zoom.setName("Interface zoom");zoom.setComponentID("ui.zoom");zoom.setTooltip("Scale Atlas to 50, 75 or 100 percent; audio and parameters are unchanged.");zoom.addItem("50 %",1);zoom.addItem("75 %",2);zoom.addItem("100 %",3);zoom.setSelectedId(3,juce::dontSendNotification);zoom.onChange=[this]{const auto id=zoom.getSelectedId();if(id>=1&&id<=3)setZoom(id==1?.5f:(id==2?.75f:1.f));};
        for(auto id:{"global.input","global.output","global.mix","global.mode","global.bypass","graph.enabled"})addControl(globals,id);
        if(synthEdition())globals[0]->setTooltipText("Global / Input. The Synth edition has no audio input: this gain scales the Source and FM voices.");
        addAndMakeVisible(search);search.setName("Search module library");search.setComponentID("library.search");search.setTextToShowWhenEmpty("Search modules...",muted);search.onTextChange=[this]{filterBrowser();};
        addAndMakeVisible(browserViewport);browserViewport.setViewedComponent(&browser,false);browserViewport.setScrollBarsShown(true,false);browser.draw=[this](juce::Graphics& g){paintBrowser(g);};browser.down=[this](const juce::MouseEvent& e){int index=e.y/48;if(index>=0&&index<int(browserItems.size()))assign(browserItems[size_t(index)]);};filterBrowser();
        addAndMakeVisible(canvas);canvas.setName("Twelve slot patch canvas");canvas.setComponentID("patch.canvas");canvas.draw=[this](juce::Graphics& g){paintCanvas(g);};canvas.down=[this](const juce::MouseEvent& e){canvasDown(e);};canvas.drag=[this](const juce::MouseEvent& e){if(source>=0){cableEnd=e.position;canvas.repaint();}};canvas.up=[this](const juce::MouseEvent& e){canvasUp(e);};
        addAndMakeVisible(gallery);gallery.draw=[this](juce::Graphics& g){paintGallery(g);};gallery.down=[this](const juce::MouseEvent& e){const int cellW=gallery.getWidth()/8,index=(e.y/36)*8+e.x/juce::jmax(1,cellW);if(index<32)p.applyRoutingAlgorithm(index);};gallery.setComponentID("routing.gallery");
        addAndMakeVisible(algorithm);for(int i=0;i<32;++i)algorithm.addItem(juce::String(i+1).paddedLeft('0',2)+"  "+aether::modular::algorithmName(i),i+1);algorithm.addItem("33  Custom constellation",33);algorithm.setSelectedId(int(p.parameterValue("routing.algorithm"))+1,juce::dontSendNotification);algorithm.setComponentID("routing.algorithm");algorithm.onChange=[this]{if(algorithm.getSelectedId()>0&&algorithm.getSelectedId()<=32)p.applyRoutingAlgorithm(algorithm.getSelectedId()-1);else if(algorithm.getSelectedId()==33)p.setParameterValue("routing.algorithm",32);};
        addAndMakeVisible(cableGain);cableGain.setName("Selected cable gain");cableGain.setComponentID("routing.cable.gain");cableGain.setSliderStyle(juce::Slider::LinearHorizontal);cableGain.setTextBoxStyle(juce::Slider::TextBoxRight,false,60,18);cableGain.setColour(juce::Slider::rotarySliderFillColourId,blue);addAndMakeVisible(removeCable);removeCable.onClick=[this]{if(selectedSource>=0){p.setGraphEdge(selectedSource,selectedDestination,0);selectCable(-1,-1);}};
        for(auto id:{"graph.feedback","graph.trim"})addControl(globals,id);
        addAndMakeVisible(fmAlgorithm);for(int i=0;i<32;++i)fmAlgorithm.addItem(juce::String(i+1).paddedLeft('0',2)+"  "+aether::fm::algorithm(i).name,i+1);fmAlgorithm.setSelectedId(int(p.parameterValue("fm.algorithm"))+1,juce::dontSendNotification);fmAlgorithm.setComponentID("fm.algorithm");fmAlgorithm.onChange=[this]{if(fmAlgorithm.getSelectedId()>0)p.applyFmAlgorithm(fmAlgorithm.getSelectedId()-1);};
        addAndMakeVisible(fmDiagram);fmDiagram.draw=[this](juce::Graphics& g){paintFmDiagram(g);};addAndMakeVisible(fmMatrix);fmMatrix.setComponentID("fm.matrix");fmMatrix.draw=[this](juce::Graphics& g){paintFmMatrix(g);};fmMatrix.down=[this](const juce::MouseEvent& e){int s=(e.x-38)/30,d=(e.y-30)/30;if(e.x>=38&&e.y>=30&&s<6&&d<6){const float v=fmEdge(s,d);p.setFmEdge(s,d,e.mods.isRightButtonDown()?0:(v<.1f?.5f:(v<.9f?1:0)));fmMatrix.repaint();}};
        for(int op=0;op<6;++op)for(auto id:{"ratio","level","carrier","detune","attack","release"})addControl(fmControls,oid(op,id));
        for(auto id:{"fm.enabled","fm.custom","fm.index","fm.morph","fm.feedback","fm.attack","fm.decay","fm.sustain","fm.release","fm.gain","fm.spread"})addControl(fmControls,id);
        for(int i=0;i<4;++i)for(auto id:{"rate","phase","shape","sync"})addControl(motionControls,"lfo."+juce::String(i+1)+"."+id);
        for(int i=0;i<16;++i)for(auto id:{"source","target","depth"})addControl(motionControls,"modulation."+juce::String(i+1)+"."+id);
        if(synthEdition())for(int i=0;i<16;++i)motionControls[size_t(16+i*3)]->setTooltipText("Motion source. The Synth edition has no audio input: Envelope follows the Source and FM voices, Pitch the last MIDI note.");
        addAndMakeVisible(motionScope);motionScope.draw=[this](juce::Graphics& g){paintMotion(g);};
        for(auto id:{"source.engine","source.enabled","source.attack","source.decay","source.sustain","source.release","source.gain","source.spread","source.transpose","source.antialias","source.seed","source.pg.sequency","source.pg.walshmix","source.pg.czshape","source.pg.czdepth","source.pg.czenvelope","source.pg.czvelocity","source.pg.vpsd","source.pg.vpsv","source.pg.vpsratio","source.pg.phaserandom","source.chaos.segments","source.chaos.distribution","source.chaos.ampstep","source.chaos.durstep","source.chaos.durrange","source.chaos.momentum","source.chaos.ampbarrier"})addControl(sourceControls,id,juce::String(id)=="source.enabled"?"Source on":"");
        addAndMakeVisible(statusLabel);statusLabel.setComponentID("source.status");statusLabel.setName("Source status");statusLabel.setMinimumHorizontalScale(1.f);statusLabel.setBorderSize(juce::BorderSize<int>(0));statusLabel.setInterceptsMouseClicks(false,false);
        addAndMakeVisible(noticeLabel);noticeLabel.setComponentID("page.notice");noticeLabel.setName("Page notice");noticeLabel.setMinimumHorizontalScale(1.f);noticeLabel.setBorderSize(juce::BorderSize<int>(0));noticeLabel.setInterceptsMouseClicks(false,false);
        addAndMakeVisible(sourceEnvelope);sourceEnvelope.setComponentID("source.envelope");sourceEnvelope.setInterceptsMouseClicks(false,false);sourceEnvelope.draw=[this](juce::Graphics& g){paintSourceEnvelope(g);};
        sourceControls[0]->combo.onChange=[this]{syncSourceEngine();refreshSource();};
        addAndMakeVisible(allViewport);allViewport.setViewedComponent(&allBody,false);allViewport.setScrollBarsShown(true,false);
        for(const auto& spec:aether::parameterSpecs()){auto c=std::make_unique<ParamControl>(p,spec.id,juce::String(spec.group)+" / "+juce::String(spec.name));allBody.addAndMakeVisible(*c);allControls.push_back(std::move(c));}
        setupRandomize();rebuildInspector();setWorkspace(0);p.analysisEnabled.store(false);startTimerHz(24);
    }
    ~Impl()override{stopTimer();chooser.reset();cableAttachment.reset();setLookAndFeel(nullptr);}
    ParamControl& addControl(std::vector<std::unique_ptr<ParamControl>>& v,const juce::String& id,const juce::String& name={},bool rotary=false){auto c=std::make_unique<ParamControl>(p,id,name,rotary);c->setCompact(compact);addAndMakeVisible(*c);v.push_back(std::move(c));return *v.back();}
    // Switch between the default and compact layouts; returns true when the mode changed.
    bool applyCompact(bool c){if(compact==c)return false;compact=c;for(auto* group:{&globals,&inspector,&fmControls,&motionControls,&allControls,&sourceControls})for(size_t i=0;i<group->size();++i)(*group)[i]->setCompact(c,!(c&&((group==&motionControls&&i>=16)||(group==&sourceControls&&i==0))));return true;}
    void setWorkspace(int w){workspace=juce::jlimit(0,3,w);minimal=false;resized();repaint();}
    void setMinimal(bool b){minimal=b;resized();repaint();}
    // ---- Source workspace (engine select, shared voice controls, engine-specific block) ----
    int sourceEngine()const{return juce::jlimit(0,2,int(p.parameterValue("source.engine")+.5f));}
    int shownMode()const{return modeShown(int(p.parameterValue("global.mode")+.5f));}
    bool sourceSounding()const{return shownMode()==modeSounding()&&sourceEngine()>0&&p.parameterValue("source.enabled")>.5f;}
    juce::String sourceStatusText()const{
        const int e=sourceEngine(),mode=shownMode();const auto* engines=specFor("source.engine");const auto* modes=specFor("global.mode");
        const auto engine=engines?engines->choices[e]:juce::String();const auto modeName=modes&&mode>=0&&mode<modes->choices.size()?modes->choices[mode]:juce::String(mode);
        if(e==0)return synthEdition()?"Source layer is off. Choose an engine to use it (plays on MIDI notes).":"Source layer is off. Choose an engine to use it (Resynth mode).";
        if(mode!=modeSounding())return "Idle: Mode is "+modeName+". Switch Mode to Resynth in the footer to run this layer.";
        if(p.parameterValue("source.enabled")<=.5f)return "Switched off. Turn Source on to hear "+engine+".";
        return synthEdition()?"Active: "+engine+" sounds on MIDI notes in Synth mode.":"Active: "+engine+" follows the input audio in Resynth mode.";
    }
    // ---- Header notice (right of the tabs) for Patch / FM / Motion: what the page does in this edition and Mode. Warnings use the Source panel's earth-red (earthText, 5.84:1 on paper). ----
    struct Notice{juce::String text;bool warn=false;};
    bool fmSilent()const{return p.parameterValue("fm.enabled")<=.5f||shownMode()==0;}
    // A Motion row that has a depth and targets a Source / FM destination that currently makes no sound (groups come from the destination names, see targetGroupOf).
    juce::String motionTargetWarning()const{
        for(int n=1;n<=16;++n){
            const auto base="modulation."+juce::String(n)+".";const auto* spec=specFor(base+"target");if(!spec||std::abs(p.parameterValue(base+"depth"))<.001f)continue;
            const int index=juce::jlimit(0,spec->choices.size()-1,int(p.parameterValue(base+"target")+.5f));const auto group=targetGroupOf(spec->choices[index]);
            if(group=="FM"&&fmSilent())return "A row targets FM, but FM is silent now.";
            if(group=="Source"&&!sourceSounding())return "A row targets Source, but Source is silent now.";
        }
        return {};
    }
    Notice pageNoticeFor(int ws)const{
        const bool synth=synthEdition();
        if(ws==1){
            if(p.parameterValue("fm.enabled")<=.5f)return {"Six operator engine is off. Turn it on to hear FM.",true};
            if(shownMode()==0)return {"Idle: Mode is FX. Switch Mode to Resynth for FM.",true};
            return {synth?"FM sounds on MIDI notes in Synth mode.":"FM follows the input audio in Resynth mode.",false};
        }
        if(ws==2){
            const auto warning=motionTargetWarning();if(warning.isNotEmpty())return {warning,true};
            if(synth)return {"Envelope follows the voices; Pitch the last MIDI note.",false};
        }
        if(ws==0&&synth)return {"No audio input: IN carries the Source and FM voices.",false};
        return {};
    }
    void refreshNotice(){
        const auto n=(minimal||workspace==3)?Notice{}:pageNoticeFor(workspace);
        noticeLabel.setText(n.text,juce::dontSendNotification);noticeLabel.setColour(juce::Label::textColourId,n.warn?earthText:ink);noticeLabel.setVisible(n.text.isNotEmpty());
    }
    // Status text, text colour and the inactive (grey graphics, full-contrast text) style of the controls that do not sound right now.
    void refreshSource(){
        const bool on=sourceSounding();statusLabel.setText(sourceStatusText(),juce::dontSendNotification);statusLabel.setColour(juce::Label::textColourId,on?ink:earthText);
        for(size_t i=2;i<sourceControls.size();++i)sourceControls[i]->setDimmed(!on);
        sourceEnvelope.repaint();
    }
    // The engine decides which block of controls is visible; relayout when it changed (menu, host automation, preset load).
    bool syncSourceEngine(){const int e=sourceEngine();if(e==lastEngine)return false;lastEngine=e;resized();repaint();return true;}
    std::vector<AetherAudioProcessorEditor::SourcePaintText> sourceTexts()const{
        using T=AetherAudioProcessorEditor::SourcePaintText;std::vector<T> v;
        const int x0=content.getX(),y0=content.getY(),cw=content.getWidth(),e=sourceEngine();const auto engine=specFor("source.engine")->choices[e];
        static const char* pgHeads[3]={"WALSH / PHASE","CASIO CZ","VARIABLE PHASE SHAPING"};static const char* chaosHeads[3]={"SEGMENTS","STEP SIZES","MEMORY / LIMITS"};
        if(!compact){
            const int rx=x0+488,rw=cw-488,colW=rw/3;
            v.push_back({{x0,y0,600,38},"Sound sources",32.f,true,ink});
            v.push_back({{x0,y0+104,440,20},"VOICE / ENVELOPE",10.f,false,earthText});
            v.push_back({{x0,y0+472,440,20},"SECONDARY / PROCESSING",10.f,false,muted});
            if(e==0){
                v.push_back({{rx,y0+96,rw,38},"No engine selected",28.f,true,ink});
                v.push_back({{rx,y0+138,rw,20},"Choose Phase Geometry or Chaos in the Engine menu to show its parameters here.",10.f,false,muted});
                v.push_back({{rx,y0+162,rw,20},synthEdition()?"The source layer plays on MIDI notes in Synth mode with the engine switched on.":"The source layer follows the input audio in Resynth mode with the engine switched on.",10.f,false,muted});
                v.push_back({{rx,y0+186,rw,20},"Envelope, level, spread and transpose stay editable on the left.",10.f,false,muted});
            }else{
                v.push_back({{rx,y0+96,rw,38},engine,28.f,true,ink});
                v.push_back({{rx,y0+138,rw,20},e==1?"Walsh sequency, Casio CZ phase distortion and variable phase shaping.":"Stochastic waveform: random-walk breakpoints with chosen distribution and barriers.",10.f,false,muted});
                for(int c=0;c<3;++c)v.push_back({{rx+c*colW,y0+176,colW-18,20},e==1?pgHeads[c]:chaosHeads[c],10.f,false,earthText});
            }
        }else{
            const int rx=x0+480;
            v.push_back({{x0,y0+36,444,18},"VOICE / ENVELOPE",10.f,false,earthText});
            v.push_back({{x0,y0+228,444,18},"SECONDARY",10.f,false,muted});
            if(e==0){
                v.push_back({{rx,y0+36,496,18},"NO ENGINE SELECTED",10.f,false,earthText});
                v.push_back({{rx,y0+58,496,20},"Choose Phase Geometry or Chaos in the Engine menu.",10.f,false,muted});
                v.push_back({{rx,y0+82,496,20},synthEdition()?"The source layer plays on MIDI notes in Synth mode.":"The source layer follows the input audio in Resynth mode.",10.f,false,muted});
                v.push_back({{rx,y0+106,496,20},"Common parameters stay editable on the left.",10.f,false,muted});
            }else v.push_back({{rx,y0+36,496,18},engine.toUpperCase()+" / "+juce::String(e==1?10:7)+" PARAMETERS",10.f,false,earthText});
        }
        return v;
    }
    void layoutSource(){
        stage=content;const int x0=content.getX(),y0=content.getY(),cw=content.getWidth();auto& C=sourceControls;
        static const int pgCol[10]={0,0,1,1,1,1,2,2,2,0},pgRow[10]={0,1,0,1,2,3,0,1,2,2},chCol[7]={0,0,1,1,1,2,2},chRow[7]={0,1,0,1,2,0,1};
        if(!compact){
            C[0]->setBounds(x0,y0+44,320,46);C[1]->setBounds(x0+344,y0+61,150,28);statusLabel.setBounds(x0+510,y0+61,cw-510,28);sourceEnvelope.setBounds(x0,y0+128,440,88);
            for(int r=0;r<4;++r){C[size_t(2+r)]->setBounds(x0,y0+232+r*56,202,48);if(r<3)C[size_t(6+r)]->setBounds(x0+220,y0+232+r*56,202,48);}
            C[9]->setBounds(x0,y0+509,202,28);C[10]->setBounds(x0+220,y0+492,202,48);
            const int rx=x0+488,colW=(cw-488)/3;
            for(int i=0;i<10;++i)C[size_t(11+i)]->setBounds(rx+pgCol[i]*colW,y0+200+pgRow[i]*64,colW-18,48);
            for(int i=0;i<7;++i)C[size_t(21+i)]->setBounds(rx+chCol[i]*colW,y0+200+chRow[i]*64,colW-18,48);
        }else{
            C[0]->setBounds(x0,y0,260,28);C[1]->setBounds(x0+272,y0,120,28);statusLabel.setBounds(x0+404,y0,cw-404,28);sourceEnvelope.setBounds(0,0,0,0);
            for(int r=0;r<4;++r){C[size_t(2+r)]->setBounds(x0,y0+58+r*40,216,38);if(r<3)C[size_t(6+r)]->setBounds(x0+228,y0+58+r*40,216,38);}
            C[9]->setBounds(x0,y0+250,216,38);C[10]->setBounds(x0+228,y0+250,216,38);
            const int rx=x0+480;
            for(int i=0;i<17;++i)C[size_t(11+i)]->setBounds(rx+(i%(i<10?2:2))*248,y0+58+((i<10?i:i-10)/2)*40,236,38);
        }
    }
    void paintSource(juce::Graphics& g){
        for(const auto& t:sourceTexts())text(g,t.text,t.area,t.size,t.colour,juce::Justification::centredLeft,t.display);
        const int x0=content.getX(),y0=content.getY();g.setColour(line);
        if(!compact)g.drawVerticalLine(x0+464,float(y0+104),float(y0+540));else g.drawVerticalLine(x0+462,float(y0+36),float(y0+290));
    }
    void paintSourceEnvelope(juce::Graphics& g){
        auto r=sourceEnvelope.getLocalBounds().toFloat();g.setColour(light);g.fillRect(r);g.setColour(line);g.drawRect(r,1.f);
        const bool on=sourceSounding();auto plot=r.reduced(12,8);plot.removeFromBottom(22);
        auto w=[](float t){return std::sqrt(juce::jmax(.001f,t));};
        const float a=w(p.parameterValue("source.attack")),d=w(p.parameterValue("source.decay")),rel=w(p.parameterValue("source.release")),s=juce::jlimit(0.f,1.f,p.parameterValue("source.sustain"));
        const float hold=(a+d+rel)*.4f,total=a+d+hold+rel,unit=plot.getWidth()/total;
        const float x1=plot.getX()+a*unit,x2=x1+d*unit,x3=x2+hold*unit,x4=x3+rel*unit,top=plot.getY(),bottom=plot.getBottom(),sy=bottom-s*plot.getHeight();
        juce::Path env;env.startNewSubPath(plot.getX(),bottom);env.lineTo(x1,top);env.lineTo(x2,sy);env.lineTo(x3,sy);env.lineTo(x4,bottom);
        auto fill=env;fill.closeSubPath();g.setColour((on?blue:inactive).withAlpha(.12f));g.fillPath(fill);
        g.setColour(line);g.drawHorizontalLine(int(bottom),plot.getX(),plot.getRight());g.setColour(on?blue:inactive);g.strokePath(env,juce::PathStrokeType(2.f));
        const float mids[4]={(plot.getX()+x1)*.5f,(x1+x2)*.5f,(x2+x3)*.5f,(x3+x4)*.5f};const char* names[4]={"A","D","S","R"};
        for(int i=0;i<4;++i)text(g,names[i],{int(mids[i])-10,int(r.getBottom())-22,20,18},10,muted,juce::Justification::centred);
    }
    void setZoom(float value){zoomFactor=value;const int id=value<.625f?1:(value<.875f?2:3);zoom.setSelectedId(id,juce::dontSendNotification);owner.setResizeLimits(juce::jmax(juce::roundToInt(1180*value),value<.625f?720:0),juce::jmax(juce::roundToInt(780*value),value<.625f?450:0),juce::roundToInt(2200*value),juce::roundToInt(1500*value));owner.setSize(juce::roundToInt(1440*value),juce::roundToInt(900*value));owner.resized();repaint();}
    float getZoom()const noexcept{return zoomFactor;}
    void select(int s){slot=juce::jlimit(0,11,s);rebuildInspector();resized();repaint();}
    void assign(int type){p.assignModule(slot,type);rebuildInspector();resized();repaint();}
    void rebuildInspector(){inspector.clear();lastType=int(p.parameterValue(nid(slot,"type")));lastType=juce::jlimit(0,20,lastType);const auto& info=aether::modular::moduleInfo(aether::modular::ModuleType(lastType));for(auto id:{"type","on","mix","level","pan"})addControl(inspector,nid(slot,id));for(int k=0;k<4;++k){auto& c=addControl(inspector,nid(slot,juce::String::charToString(char('a'+k))),{},true);c.macro(info.macros[size_t(k)]);c.setTooltipText(juce::String(info.description)+"\n"+info.macros[size_t(k)].name+" / "+info.macros[size_t(k)].unit);}for(auto id:{"input","output"}){auto& c=addControl(inspector,nid(slot,id));c.slider.beforeGesture=[this]{p.prepareGraphEditing();};if(synthEdition()&&juce::String(id)=="input")c.setTooltipText("Input send. The Synth edition has no audio input: this is how much of the Source and FM voices enters this module.");}}
    void filterBrowser(){browserItems.clear();const auto query=search.getText().trim();for(int i=0;i<21;++i){const auto& info=aether::modular::moduleInfo(aether::modular::ModuleType(i));if(query.isEmpty()||juce::String(info.name).containsIgnoreCase(query)||juce::String(info.category).containsIgnoreCase(query))browserItems.push_back(i);}browser.setSize(juce::jmax(170,browserViewport.getWidth()-10),int(browserItems.size())*48);browser.repaint();}
    void choose(bool saving){load.setEnabled(false);save.setEnabled(false);chooser=std::make_unique<juce::FileChooser>(saving?"Save Atlas sound":"Open Atlas sound",juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),"*.aether");juce::Component::SafePointer<Impl> safe(this);chooser->launchAsync((saving?juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::warnAboutOverwriting:juce::FileBrowserComponent::openMode)|juce::FileBrowserComponent::canSelectFiles,[safe,saving](const juce::FileChooser& fc){if(!safe)return;safe->load.setEnabled(true);safe->save.setEnabled(true);auto f=fc.getResult();if(f==juce::File{})return;if(saving)f=f.withFileExtension(".aether");auto result=saving?safe->p.savePreset(f):safe->p.loadPreset(f);if(result.failed())juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,"Preset",result.getErrorMessage());});}
    void resized()override{resizedLayout();positionRandomPanel();}
    void resizedLayout(){
        if(compact){resizedCompact();return;}
        auto r=getLocalBounds().reduced(26,0);auto top=r.removeFromTop(104);auto tabs=top.withY(60).withHeight(31);tabs.removeFromLeft(215);for(auto* b:{&patchTab,&fmTab,&motionTab,&sourceTab})b->setBounds(tabs.removeFromLeft(119).reduced(3,0));zoom.setBounds(top.getRight()-208,20,92,28);minimalButton.setBounds(top.getRight()-103,20,103,28);help.setBounds(590,20,28,28);if(randomCaret){randomRoll.setButtonText("Randomize");randomRoll.setBounds(446,20,96,28);randomCaret->setBounds(542,20,24,28);}noticeLabel.setBounds(733,60,juce::jmax(0,top.getRight()-733),31);
        auto tools=top.withY(20).withHeight(28);tools.removeFromLeft(625);presets.setBounds(tools.removeFromLeft(juce::jmax(140,tools.getWidth()-402)));tools.removeFromLeft(8);for(auto* b:{&load,&save,&ab})b->setBounds(tools.removeFromLeft(59).reduced(3,0));
        footer={26,getHeight()-96,getWidth()-52,76};content={26,114,getWidth()-52,getHeight()-222};
        auto fr=footer.withY(footer.getY()+22).withHeight(48);for(int i=0;i<3;++i)globals[size_t(i)]->setBounds(fr.removeFromLeft(juce::jmax(120,footer.getWidth()/7)).reduced(0,1));fr.removeFromLeft(18);globals[3]->setBounds(fr.removeFromLeft(126));globals[4]->setBounds(fr.removeFromLeft(100));panic.setBounds(footer.getRight()-70,footer.getY()+27,70,29);
        updateVisibility();
        if(minimal){allViewport.setBounds(content);const int cw=(content.getWidth()-22)/4;for(size_t i=0;i<allControls.size();++i)allControls[i]->setBounds(int(i%4)*cw,int(i/4)*57,cw-18,51);allBody.setSize(content.getWidth()-18,int((allControls.size()+3)/4)*57);return;}
        if(workspace==0){
            left=content.removeFromLeft(194);content.removeFromLeft(24);right=content.removeFromRight(270);content.removeFromRight(24);stage=content;
            search.setBounds(left.withY(left.getY()+50).withHeight(30));browserViewport.setBounds(left.withTrimmedTop(95));filterBrowser();
            auto st=stage;st.removeFromTop(51);algorithm.setBounds(st.removeFromTop(29).withTrimmedRight(100));globals[5]->setBounds(stage.getRight()-88,stage.getY()+51,88,29);st.removeFromTop(12);gallery.setBounds(st.removeFromTop(144));st.removeFromTop(10);canvas.setBounds(st.withTrimmedBottom(40));
            auto end=stage.withY(stage.getBottom()-34).withHeight(32);globals[6]->setBounds(end.removeFromLeft(125));globals[7]->setBounds(end.removeFromLeft(125));end.removeFromLeft(10);cableGain.setBounds(end.removeFromLeft(juce::jmax(50,end.getWidth()-100)));removeCable.setBounds(end.removeFromRight(94).reduced(0,3));cableGain.setVisible(selectedSource>=0);removeCable.setVisible(selectedSource>=0);
            auto ins=right;ins.removeFromTop(100);auto typeRow=ins.removeFromTop(45);inspector[0]->setBounds(typeRow.withTrimmedRight(86));inspector[0]->combo.setTooltip("Choose a module type for the selected slot. Its four macros update to the material metadata.");inspector[1]->setBounds(typeRow.removeFromRight(82).withTrimmedTop(17));auto levels=ins.removeFromTop(40);inspector[2]->setBounds(levels.removeFromLeft(132));inspector[3]->setBounds(levels.withTrimmedLeft(6));inspector[4]->setBounds(ins.removeFromTop(35));ins.removeFromTop(8);const int knobH=juce::jlimit(70,120,(right.getHeight()-398)/2);for(int i=0;i<4;++i)inspector[size_t(i+5)]->setBounds(right.getX()+(i%2)*137,ins.getY()+(i/2)*knobH,130,knobH);ins.removeFromTop(knobH*2);for(int i=9;i<11;++i)inspector[size_t(i)]->setBounds(ins.removeFromTop(35));
        }else if(workspace==1){
            stage=content;fmAlgorithm.setBounds(content.getX(),content.getY()+45,fmAlgorithmWidth,28);fmDiagram.setBounds(content.getX(),content.getY()+90,260,190);fmMatrix.setBounds(content.getX()+fmAlgorithmWidth+18,content.getY()+48,225,218);
            const int gx=content.getX()+fmAlgorithmWidth+275,gw=content.getWidth()-(fmAlgorithmWidth+275);for(int i=0;i<11;++i){const int col=i%3,row=i/3;fmControls[size_t(36+i)]->setBounds(gx+col*(gw/3),content.getY()+44+row*54,gw/3-18,48);}
            const int y=content.getY()+fmOperatorTop,cw=content.getWidth()/6;for(int op=0;op<6;++op)for(int k=0;k<6;++k)fmControls[size_t(op*6+k)]->setBounds(content.getX()+op*cw+10,y+32+k*39,cw-25,36);
        }else if(workspace==3){
            layoutSource();
        }else{
            stage=content;const int cw=content.getWidth()/4;for(int n=0;n<4;++n){for(int k=0;k<4;++k)motionControls[size_t(n*4+k)]->setBounds(content.getX()+n*cw+12+(k%2)*((cw-24)/2),content.getY()+41+(k/2)*49,(cw-24)/2-12,43);}motionScope.setBounds(content.withY(content.getY()+147).withHeight(47));
            const int rowHeight=juce::jmax(36,(content.getHeight()-236)/8),half=content.getWidth()/2;for(int n=0;n<16;++n){const int x=content.getX()+(n/8)*half+32,y=content.getY()+235+(n%8)*rowHeight;motionControls[size_t(16+n*3)]->setBounds(x,y,136,rowHeight-5);motionControls[size_t(17+n*3)]->setBounds(x+146,y,half-310,rowHeight-5);motionControls[size_t(18+n*3)]->setBounds(x+half-151,y,112,rowHeight-5);}
        }
    }
    void updateVisibility(){
        for(auto* c:std::initializer_list<juce::Component*>{&search,&browserViewport,&canvas,&gallery,&algorithm,&cableGain,&removeCable})c->setVisible(!minimal&&workspace==0);
        for(auto* c:std::initializer_list<juce::Component*>{&fmAlgorithm,&fmDiagram,&fmMatrix})c->setVisible(!minimal&&workspace==1);
        motionScope.setVisible(!minimal&&workspace==2);lastEngine=sourceEngine();for(size_t i=0;i<sourceControls.size();++i){const bool group=i<11||(i<21?lastEngine==1:lastEngine==2);sourceControls[i]->setVisible(!minimal&&workspace==3&&group);}statusLabel.setVisible(!minimal&&workspace==3);sourceEnvelope.setVisible(!minimal&&workspace==3&&!compact);refreshSource();refreshNotice();for(auto& c:inspector)c->setVisible(!minimal&&workspace==0);for(auto& c:fmControls)c->setVisible(!minimal&&workspace==1);for(auto& c:motionControls)c->setVisible(!minimal&&workspace==2);allViewport.setVisible(minimal);
        globals[5]->setVisible(!minimal&&workspace==0);globals[6]->setVisible(!minimal&&workspace==0);globals[7]->setVisible(!minimal&&workspace==0);
        patchTab.setToggleState(!minimal&&workspace==0,juce::dontSendNotification);fmTab.setToggleState(!minimal&&workspace==1,juce::dontSendNotification);motionTab.setToggleState(!minimal&&workspace==2,juce::dontSendNotification);sourceTab.setToggleState(!minimal&&workspace==3,juce::dontSendNotification);minimalButton.setToggleState(minimal,juce::dontSendNotification);
    }
    void resizedCompact(){
        const int W=getWidth(),H=getHeight(),m=12,headerH=72,footerH=70;
        auto row=juce::Rectangle<int>(m,8,W-2*m,28);
        minimalButton.setBounds(row.removeFromRight(100));row.removeFromRight(6);zoom.setBounds(row.removeFromRight(84));row.removeFromRight(6);
        for(auto* b:{&ab,&save,&load}){b->setBounds(row.removeFromRight(60));row.removeFromRight(4);}
        row.removeFromLeft(100);help.setBounds(row.removeFromLeft(28));row.removeFromLeft(8);if(randomCaret){randomRoll.setButtonText("Random");randomCaret->setBounds(row.removeFromRight(24));randomRoll.setBounds(row.removeFromRight(80));row.removeFromRight(4);}presets.setBounds(row);
        auto tabs=juce::Rectangle<int>(m,40,W-2*m,28);for(auto* b:{&patchTab,&fmTab,&motionTab,&sourceTab})b->setBounds(tabs.removeFromLeft(120).withTrimmedRight(6));noticeLabel.setBounds(500,40,juce::jmax(0,W-m-500),28);
        footer={m,H-footerH,W-2*m,footerH-6};content={m,headerH+4,W-2*m,H-footerH-headerH-10};
        const int fy=footer.getY();auto fr=juce::Rectangle<int>(m,fy+22,W-2*m,44);
        for(int i=0;i<3;++i){globals[size_t(i)]->setBounds(fr.removeFromLeft(160).reduced(0,1));fr.removeFromLeft(8);}
        globals[3]->setBounds(fr.removeFromLeft(112));fr.removeFromLeft(10);globals[4]->setBounds(fr.removeFromLeft(104).withTrimmedTop(14));panic.setBounds(W-m-64,fy+30,64,30);
        updateVisibility();gallery.setVisible(false);fmDiagram.setVisible(false);
        const int x0=content.getX(),y0=content.getY();
        if(minimal){allViewport.setBounds(content);const int cw=(content.getWidth()-22)/3;for(size_t i=0;i<allControls.size();++i)allControls[i]->setBounds(int(i%3)*cw,int(i/3)*57,cw-18,51);allBody.setSize(content.getWidth()-18,int((allControls.size()+2)/3)*57);return;}
        if(workspace==0){
            auto c=content;left=c.removeFromLeft(176);c.removeFromLeft(10);right=c.removeFromRight(244);c.removeFromRight(10);stage=c;
            search.setBounds(left.withHeight(28));browserViewport.setBounds(left.withTrimmedTop(34));filterBrowser();
            algorithm.setBounds(stage.getX(),stage.getY(),stage.getWidth()-132,28);globals[5]->setBounds(stage.getRight()-124,stage.getY(),124,28);
            canvas.setBounds(stage.getX(),stage.getY()+36,stage.getWidth(),stage.getHeight()-76);
            auto end=stage.withY(stage.getBottom()-36).withHeight(36);globals[6]->setBounds(end.removeFromLeft(150));globals[7]->setBounds(end.removeFromLeft(130));end.removeFromLeft(8);removeCable.setBounds(end.removeFromRight(124).reduced(0,4));end.removeFromRight(6);cableGain.setBounds(end);cableGain.setVisible(selectedSource>=0);removeCable.setVisible(selectedSource>=0);
            auto ins=right;ins.removeFromTop(56);inspector[0]->setBounds(ins.removeFromTop(42));inspector[0]->combo.setTooltip("Choose a module type for the selected slot. Its four macros update to the material metadata.");inspector[1]->setBounds(ins.removeFromTop(26));for(int i=2;i<11;++i)inspector[size_t(i)]->setBounds(ins.removeFromTop(38));
        }else if(workspace==1){
            stage=content;const int gx=x0+505,gw=content.getWidth()-505;fmAlgorithm.setBounds(gx,y0,gw,28);fmControls[36]->setBounds(x0,y0,252,26);fmControls[37]->setBounds(x0,y0+34,252,26);fmMatrix.setBounds(x0+264,y0,225,218);
            for(int i=2;i<11;++i){const int col=(i-2)%3,r=(i-2)/3;fmControls[size_t(36+i)]->setBounds(gx+col*(gw/3),y0+36+r*62,gw/3-12,44);}
            const int oy=y0+222,cw=content.getWidth()/6;for(int op=0;op<6;++op)for(int k=0;k<6;++k)fmControls[size_t(op*6+k)]->setBounds(x0+op*cw+8,oy+28+k*37,cw-20,37);
        }else if(workspace==3){
            layoutSource();
        }else{
            stage=content;const int cw=content.getWidth()/4;for(int n=0;n<4;++n)for(int k=0;k<4;++k)motionControls[size_t(n*4+k)]->setBounds(x0+n*cw+12,y0+28+k*38,cw-24,38);
            motionScope.setBounds(content.withY(y0+182).withHeight(26));
            const int rowH=30,half=content.getWidth()/2;for(int n=0;n<16;++n){const int hx=x0+(n/8)*half,y=y0+230+(n%8)*rowH;motionControls[size_t(16+n*3)]->setBounds(hx+26,y,112,rowH-4);motionControls[size_t(17+n*3)]->setBounds(hx+142,y,motionDestWidth,rowH-4);motionControls[size_t(18+n*3)]->setBounds(hx+142+motionDestWidth+4,y,half-(142+motionDestWidth+4)-6,rowH-4);}
        }
    }
    void paintCompact(juce::Graphics& g){
        const int W=getWidth(),H=getHeight(),m=12;
        g.fillAll(paper);g.setColour(line.withAlpha(.07f));for(int y=0;y<H;y+=11)g.drawHorizontalLine(y,0,float(W));
        text(g,"Atlas",{m,4,96,36},30,ink,juce::Justification::centredLeft,true);
        g.setColour(line);g.drawHorizontalLine(71,float(m),float(W-m));g.drawHorizontalLine(H-73,float(m),float(W-m));
        const int fy=footer.getY();
        text(g,"SIGNAL / GLOBAL",{m,fy,160,20},11,earth);
        const auto rate=p.getSampleRate()>0?p.getSampleRate():48000.;const float delayMs=float(p.textureLatencySamples.load()*1000./rate);juce::String telemetry="TEXTURE "+juce::String(delayMs,1)+" ms / PDC 0";const float cpu=p.cpuPercent.load();if(cpu>=0.0f)telemetry+=" / CPU "+juce::String(cpu,1)+"%";
        text(g,telemetry,{m+170,fy,W-2*m-170-270,20},11,muted,juce::Justification::centredRight);
        text(g,juce::String(JucePlugin_VersionString)+"  /  "+juce::String(JucePlugin_Name),{W-m-260,fy,260,20},11,muted,juce::Justification::centredRight);
        const int mx=W-m-64-10-150;meter(g,"IN",p.inputMeterDb.load(),{mx,fy+24,150,20},50);meter(g,"OUT",p.outputMeterDb.load(),{mx,fy+46,150,20},50);
        if(minimal)return;
        const int x0=content.getX(),y0=content.getY();
        if(workspace==0){
            text(g,"INSPECT / "+juce::String(slot+1).paddedLeft('0',2),right.withHeight(20),10,earth);const auto& info=aether::modular::moduleInfo(aether::modular::ModuleType(lastType));text(g,info.name,right.withY(right.getY()+20).withHeight(34),28,ink,juce::Justification::centredLeft,true);
        }else if(workspace==1){
            text(g,"LIVE VOICES  "+juce::String(p.fmVoiceCount.load())+" / 16",fmPaintGeometry().liveVoices,10,earth);
            const int oy=y0+222,cw=content.getWidth()/6;for(int op=0;op<6;++op){auto r=juce::Rectangle<int>(x0+op*cw,oy,cw-8,26);g.setColour(line);g.drawLine(float(r.getX()),float(r.getY()),float(r.getRight()),float(r.getY()),1);text(g,"OP "+juce::String(op+1),{r.getX()+8,r.getY()+2,100,24},17,op%2?earth:blue,juce::Justification::centredLeft,true);}
        }else if(workspace==3){
            paintSource(g);
        }else{
            const int cw=content.getWidth()/4;for(int n=0;n<4;++n){text(g,"0"+juce::String(n+1)+" / Low frequency",{x0+n*cw+12,y0,cw-20,26},20,n%2?earth:blue,juce::Justification::centredLeft,true);g.setColour(line);g.drawVerticalLine(x0+n*cw,float(y0),float(y0+176));}
            const int half=content.getWidth()/2;for(int h=0;h<2;++h){const int hx=x0+h*half;text(g,"Source",{hx+26,y0+210,112,18},10,muted);text(g,"Destination",{hx+142,y0+210,motionDestWidth,18},10,muted);text(g,"Depth",{hx+142+motionDestWidth+4,y0+210,half-(142+motionDestWidth+4)-6,18},10,muted);}
            for(int n=0;n<16;++n)text(g,juce::String(n+1).paddedLeft('0',2),{x0+(n/8)*half,y0+230+(n%8)*30+3,24,20},10,earth);
        }
    }
    void paint(juce::Graphics& g)override{
        uiTextScale=getTransform().getScaleFactor();
        refreshRandomHint();
        if(compact){paintCompact(g);return;}
        g.fillAll(paper);g.setColour(line.withAlpha(.07f));for(int y=0;y<getHeight();y+=11)g.drawHorizontalLine(y,0,float(getWidth()));
        text(g,"Atlas",{26,4,162,57},51,ink,juce::Justification::centredLeft,true);text(g,"AETHER",{29,66,172,17},10,earth);text(g,"THE IMPOSSIBLE INSTRUMENT",{204,17,390,20},10,muted);
        g.setColour(line);g.drawHorizontalLine(103,26,float(getWidth()-26));g.drawHorizontalLine(getHeight()-105,26,float(getWidth()-26));
        text(g,"SIGNAL / GLOBAL",footer.withHeight(17),11,earth);const auto rate=p.getSampleRate()>0?p.getSampleRate():48000.;const float delayMs=float(p.textureLatencySamples.load()*1000./rate);juce::String telemetry="TEXTURE "+juce::String(delayMs,1)+" ms / PDC 0";const float cpu=p.cpuPercent.load();if(cpu>=0.0f)telemetry+=" / CPU "+juce::String(cpu,1)+"%";text(g,telemetry,footer.withHeight(17).withTrimmedLeft(330).withTrimmedRight(100),11,muted,juce::Justification::centredRight);const int mx=footer.getRight()-275;meter(g,"IN",p.inputMeterDb.load(),{mx,footer.getY()+28,170,15});meter(g,"OUT",p.outputMeterDb.load(),{mx,footer.getY()+48,170,15});text(g,juce::String(JucePlugin_VersionString)+"  /  "+juce::String(JucePlugin_Name),{26,getHeight()-18,400,13},9,muted);text(g,"DESIGNED FOR THE UNEXPECTED",{getWidth()-350,getHeight()-18,324,13},9,muted,juce::Justification::centredRight);
        if(minimal){text(g,"Complete parameter atlas",content.withY(110).withHeight(0));return;}
        if(workspace==0){
            text(g,"Materials",left.withHeight(42),31,ink,juce::Justification::centredLeft,true);text(g,"21 MODULES / CLICK TO ASSIGN",left.withY(left.getY()+80).withHeight(14),8,muted);
            text(g,"A constellation of sound",stage.withHeight(42),32,ink,juce::Justification::centredLeft,true);
            text(g,"INSPECT / "+juce::String(slot+1).paddedLeft('0',2),right.withHeight(22),10,earth);const auto& info=aether::modular::moduleInfo(aether::modular::ModuleType(lastType));text(g,info.name,right.withY(right.getY()+27).withHeight(40),31,ink,juce::Justification::centredLeft,true);
            g.setColour(muted);g.setFont(type(juce::jmax(10.f,legibleFloor(uiTextScale))));g.drawFittedText(info.description,right.withY(right.getY()+67).withHeight(33),juce::Justification::topLeft,3);
            text(g,"Drag OUT to IN / click two ports to connect",stage.withY(stage.getBottom()-54).withHeight(15),9,muted);
        }else if(workspace==1){
            text(g,"Phase architectures",stage.withHeight(38),32,ink,juce::Justification::centredLeft,true);text(g,"SOURCE > DESTINATION",{stage.getX()+fmAlgorithmWidth+56,stage.getY()+24,190,20},9,muted);
            text(g,"Click matrix: 0 / 0.5 / 1  -  right-click: clear",fmPaintGeometry().matrixHint,9,muted);text(g,"LIVE VOICES  "+juce::String(p.fmVoiceCount.load())+" / 16",fmPaintGeometry().liveVoices,10,earth,juce::Justification::centredRight);
            const int cw=stage.getWidth()/6;for(int op=0;op<6;++op){auto r=juce::Rectangle<int>(stage.getX()+op*cw,stage.getY()+fmOperatorTop,cw-8,stage.getHeight()-fmOperatorTop);g.setColour(line);g.drawLine(float(r.getX()),float(r.getY()),float(r.getRight()),float(r.getY()),1);text(g,"OP "+juce::String(op+1),r.withY(r.getY()+5).withHeight(25).reduced(10,0),19,op%2?earth:blue,juce::Justification::centredLeft,true);}
        }else if(workspace==3){
            paintSource(g);
        }else{
            const int cw=stage.getWidth()/4;for(int n=0;n<4;++n){text(g,"0"+juce::String(n+1)+" / Low frequency",{stage.getX()+n*cw+12,stage.getY(),cw-20,32},22,n%2?earth:blue,juce::Justification::centredLeft,true);g.setColour(line);g.drawVerticalLine(stage.getX()+n*cw,float(stage.getY()),float(stage.getY()+134));}
            text(g,"Movements & destinations",{stage.getX(),stage.getY()+195,600,36},28,ink,juce::Justification::centredLeft,true);text(g,"BIPOLAR SOURCES / SIGNED DEPTH",{stage.getRight()-330,stage.getY()+206,330,20},9,muted,juce::Justification::centredRight);
            const int rh=juce::jmax(36,(stage.getHeight()-236)/8),half=stage.getWidth()/2;for(int n=0;n<16;++n)text(g,juce::String(n+1).paddedLeft('0',2),{stage.getX()+(n/8)*half,stage.getY()+255+(n%8)*rh,27,20},10,earth);
        }
    }
    void meter(juce::Graphics& g,const juce::String& label,float db,juce::Rectangle<int> r,int valueWidth=42){text(g,label,r.removeFromLeft(30),9,muted);text(g,db<=-99?"-inf":juce::String(db,1),r.removeFromRight(valueWidth),9,ink,juce::Justification::centredRight);auto bar=r.reduced(0,6).toFloat();g.setColour(line);g.fillRect(bar);g.setColour(db>-.5f?earth:blue);g.fillRect(bar.withWidth(bar.getWidth()*juce::jlimit(0.f,1.f,(db+60)/60)));}
    AetherAudioProcessorEditor::FmPaintGeometry fmPaintGeometry()const{
        AetherAudioProcessorEditor::FmPaintGeometry g;
        if(compact){g.liveVoices={content.getX(),content.getY()+72,252,22};g.dividerY=content.getY()+222;}
        else{g.matrixHint={stage.getX()+fmAlgorithmWidth+18,stage.getY()+268,420,24};g.liveVoices={stage.getRight()-220,stage.getY()+268,220,24};g.dividerY=stage.getY()+fmOperatorTop;}
        g.dividerX0=content.getX();g.dividerX1=content.getRight();return g;
    }
    void paintBrowser(juce::Graphics& g){for(size_t n=0;n<browserItems.size();++n){int i=browserItems[n];const auto& info=aether::modular::moduleInfo(aether::modular::ModuleType(i));auto r=juce::Rectangle<int>(0,int(n)*48,browser.getWidth(),48);if(i==lastType){g.setColour(ink);g.fillRect(r);}text(g,juce::String(i).paddedLeft('0',2),r.withWidth(28).reduced(3,0),9,i==lastType?light:earth);text(g,info.name,r.withTrimmedLeft(33).withHeight(26),12,i==lastType?light:ink);text(g,juce::String(info.category).toUpperCase(),r.withTrimmedLeft(33).withY(r.getY()+27).withHeight(16),8,i==lastType?line:muted);g.setColour(line);g.drawHorizontalLine(r.getBottom()-1,0,float(r.getRight()));}}
    void miniature(juce::Graphics& g,juce::Rectangle<float> r,int index,bool fm=false){
        const int count=fm?6:12;std::array<juce::Point<float>,12> pts{};for(int i=0;i<count;++i)pts[size_t(i)]={r.getX()+r.getWidth()*(.1f+.8f*float(i%4)/3),r.getY()+r.getHeight()*(.15f+.7f*float(i/4)/float(fm?1:2))};
        aether::modular::Parameters routing;if(!fm)aether::modular::applyAlgorithm(index,routing);for(int d=0;d<count;++d)for(int s=0;s<count;++s){const float v=fm?aether::fm::algorithm(index).matrix[size_t(d)][size_t(s)]:routing.connection[size_t(d)][size_t(s)];if(std::abs(v)>.001f){g.setColour(s>=d?earth.withAlpha(.6f):muted.withAlpha(.65f));g.drawLine({pts[size_t(s)],pts[size_t(d)]},.7f);}}
        for(int i=0;i<count;++i){g.setColour(index==int(p.parameterValue("routing.algorithm"))?earth:ink);g.fillEllipse(pts[size_t(i)].x-1.3f,pts[size_t(i)].y-1.3f,2.6f,2.6f);}
    }
    void paintGallery(juce::Graphics& g){const int cw=gallery.getWidth()/8,selected=int(p.parameterValue("routing.algorithm"));for(int i=0;i<32;++i){auto r=juce::Rectangle<int>((i%8)*cw,(i/8)*36,cw-4,32);if(i==selected){g.setColour(light);g.fillRect(r);g.setColour(earth);g.drawRect(r);}text(g,juce::String(i+1).paddedLeft('0',2),r.withWidth(21).reduced(3,0),8,i==selected?earth:muted);miniature(g,r.withTrimmedLeft(24).reduced(2,3).toFloat(),i);}}
    void positionNodes(){const float w=float(canvas.getWidth()),h=float(canvas.getHeight());for(int i=0;i<12;++i){int row=i/4,col=i%4;nodePoints[size_t(i)]={w*(.11f+.26f*float(col)),36.f+juce::jmax(60.f,h-104.f)*float(row)/2.f+(col%2?5.f:-5.f)};}}
    juce::Point<float> port(int n,bool out)const{return nodePoints[size_t(n)]+juce::Point<float>(out?28.f:-28.f,0);}
    juce::Path connectionPath(int s,int d)const{auto a=port(s,true),b=port(d,false);juce::Path path;path.startNewSubPath(a);if(s==d)path.cubicTo(a.x+48,a.y-70,b.x-48,b.y-70,b.x,b.y);else{const float bend=s<d?30.f:-55.f;path.cubicTo(a.x+bend,a.y+float(s>=d?38:0),b.x-bend,b.y+float(s>=d?38:0),b.x,b.y);}return path;}
    float graphEdge(int s,int d)const{const int a=int(p.parameterValue("routing.algorithm"));if(a==32)return p.parameterValue(rid(s,d));aether::modular::Parameters routing;aether::modular::applyAlgorithm(a,routing);return routing.connection[size_t(d)][size_t(s)];}
    void paintCanvas(juce::Graphics& g){
        positionNodes();const float w=float(canvas.getWidth()),h=float(canvas.getHeight());arch(g,{w*.36f,h*.10f,w*.26f,h*.78f},.8f);
        juce::Path shadow;shadow.startNewSubPath(w*.36f,h*.88f);shadow.lineTo(w*.62f,h*.88f);shadow.lineTo(w*.81f,h*.98f);shadow.lineTo(w*.55f,h*.98f);shadow.closeSubPath();g.setColour(ink.withAlpha(.04f));g.fillPath(shadow);
        g.setGradientFill(juce::ColourGradient(light,w*.79f,h*.02f,earth,w*.87f,h*.24f,true));g.fillEllipse(w*.80f,h*.02f,46,46);g.setColour(ink.withAlpha(.07f));g.fillEllipse(w*.86f,h*.26f,70,11);
        aether::modular::Parameters visualRouting;const int visualAlgorithm=int(p.parameterValue("routing.algorithm"));if(visualAlgorithm!=32)aether::modular::applyAlgorithm(visualAlgorithm,visualRouting);for(int i=0;i<12;++i){const float input=visualAlgorithm==32?p.parameterValue(nid(i,"input")):visualRouting.input[size_t(i)],output=visualAlgorithm==32?p.parameterValue(nid(i,"output")):visualRouting.output[size_t(i)];g.setColour(blue.withAlpha(.22f));if(std::abs(input)>.001f)g.drawLine(7,h*.5f,port(i,false).x,port(i,false).y,.8f);g.setColour(earth.withAlpha(.22f));if(std::abs(output)>.001f)g.drawLine(port(i,true).x,port(i,true).y,w-7,h*.5f,.8f);}
        for(int d=0;d<12;++d)for(int s=0;s<12;++s){const float v=visualAlgorithm==32?p.parameterValue(rid(s,d)):visualRouting.connection[size_t(d)][size_t(s)];if(std::abs(v)<.001f)continue;const bool selected=s==selectedSource&&d==selectedDestination;g.setColour((selected?blue:(s>=d?earth:ink)).withAlpha(selected?.95f:.4f));g.strokePath(connectionPath(s,d),juce::PathStrokeType(selected?2.8f:1.25f));if(s>=d){auto mid=connectionPath(s,d).getPointAlongPath(connectionPath(s,d).getLength()*.5f);text(g,"z-1",{int(mid.x)-(compact?16:12),int(mid.y)-8,compact?32:25,15},8,earth);}}
        for(int i=0;i<12;++i){auto c=nodePoints[size_t(i)];const int t=juce::jlimit(0,20,int(p.parameterValue(nid(i,"type"))));const bool on=p.parameterValue(nid(i,"on"))>.5f;auto colour=i==slot?blue:ink;g.setColour(ink.withAlpha(.07f));g.fillEllipse(c.x-24+8,c.y-24+11,48,48);g.setColour(i==slot?blue:(on?light:pale));g.fillEllipse(c.x-24,c.y-24,48,48);g.setColour(i==slot?blue:line);g.drawEllipse(c.x-24,c.y-24,48,48,1);if(!on){juce::Path ring,dashed;ring.addEllipse(c.x-24,c.y-24,48,48);const float dashes[]{4.f,3.f};juce::PathStrokeType(1.5f).createDashedStroke(dashed,ring,dashes,2);g.setColour(earthText);g.fillPath(dashed);}text(g,juce::String(i+1).paddedLeft('0',2),{int(c.x)-22,int(c.y)-17,44,34},20,i==slot?light:ink,juce::Justification::centred,true);text(g,aether::modular::moduleInfo(aether::modular::ModuleType(t)).name,{int(c.x)-(compact?68:58),int(c.y)+29,compact?136:116,18},9,on?colour:earthText,juce::Justification::centred);const float meterLevel=juce::jlimit(0.f,1.f,p.nodeMeters[size_t(i)].load());g.setColour(line);g.fillRect(c.x-21,c.y+52,42.f,2.f);g.setColour(earth);g.fillRect(c.x-21,c.y+52,42.f*meterLevel,2.f);
            for(bool out:{false,true}){auto q=port(i,out);g.setColour(out?earth:blue);g.fillEllipse(q.x-3.3f,q.y-3.3f,6.6f,6.6f);}if(!on){g.setColour(earthText);g.drawLine(c.x-15,c.y+15,c.x+15,c.y-15,2);}}
        if(source>=0){g.setColour(blue);juce::Path dragPath;auto a=port(source,true);dragPath.startNewSubPath(a);dragPath.cubicTo(a.x+50,a.y,cableEnd.x-50,cableEnd.y,cableEnd.x,cableEnd.y);g.strokePath(dragPath,juce::PathStrokeType(2));}
        text(g,"IN",{0,int(h/2)-10,23,20},8,muted);text(g,"OUT",{int(w)-28,int(h/2)-10,28,20},8,muted);
    }
    int hitNode(juce::Point<float> q)const{for(int i=0;i<12;++i)if(q.getDistanceFrom(nodePoints[size_t(i)])<26)return i;return -1;}
    int hitPort(juce::Point<float> q,bool out)const{for(int i=0;i<12;++i)if(q.getDistanceFrom(port(i,out))<12)return i;return -1;}
    void canvasDown(const juce::MouseEvent& e){positionNodes();int out=hitPort(e.position,true),in=hitPort(e.position,false);if(in>=0&&source>=0){p.setGraphEdge(source,in,1);selectCable(source,in);source=-1;canvas.repaint();return;}if(out>=0){source=out;cableEnd=e.position;canvas.repaint();return;}int n=hitNode(e.position);if(n>=0){source=-1;select(n);return;}for(int d=0;d<12;++d)for(int s=0;s<12;++s)if(std::abs(graphEdge(s,d))>.001f){juce::PathStrokeType stroke(13);juce::Path hit;stroke.createStrokedPath(hit,connectionPath(s,d));if(hit.contains(e.position)){if(e.mods.isRightButtonDown()){p.setGraphEdge(s,d,0);selectCable(-1,-1);}else selectCable(s,d);return;}}source=-1;selectCable(-1,-1);}
    void canvasUp(const juce::MouseEvent& e){if(source<0)return;int d=hitPort(e.position,false);if(d>=0){p.setGraphEdge(source,d,1);selectCable(source,d);source=-1;}else if(e.getDistanceFromDragStart()>5)source=-1;canvas.repaint();}
    void selectCable(int s,int d){cableAttachment.reset();selectedSource=s;selectedDestination=d;if(s>=0){cableAttachment=std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(p.state,rid(s,d),cableGain);cableGain.setTooltip("Signed source-to-destination gain. Self and backward arcs use one sample of delay.");cableGain.setValue(graphEdge(s,d),juce::dontSendNotification);cableGain.beforeGesture=[this]{p.prepareGraphEditing();};}resized();canvas.repaint();}
    float fmEdge(int s,int d)const{const int ai=juce::jlimit(0,31,int(p.parameterValue("fm.algorithm")));const auto& a=aether::fm::algorithm(ai);const auto& next=aether::fm::algorithm((ai+1)%32);const float morph=juce::jlimit(0.f,1.f,p.parameterValue("fm.morph"));if(p.parameterValue("fm.custom")>.5f){const float custom=p.parameterValue(eid(s,d));return custom+(a.matrix[size_t(d)][size_t(s)]-custom)*morph;}const float from=a.matrix[size_t(d)][size_t(s)],to=next.matrix[size_t(d)][size_t(s)];return from+(to-from)*morph;}
    void paintFmDiagram(juce::Graphics& g){
        const float w=float(fmDiagram.getWidth()),h=float(fmDiagram.getHeight());
        arch(g,{w*.26f,12,w*.48f,h-25},.8f);
        std::array<juce::Point<float>,6> points;
        for(int i=0;i<6;++i)points[size_t(i)]={35.f+float(i%3)*(w-70)/2,30.f+float(i/3)*70.f};
        for(int d=0;d<6;++d)for(int s=0;s<6;++s)if(fmEdge(s,d)>.001f){
            g.setColour(s>=d?earth:blue);auto a=points[size_t(s)],b=points[size_t(d)];
            if(s==d)g.drawEllipse(a.x-20,a.y-30,40,25,1.3f);else g.drawArrow({a,b},1,5,5);
        }
        const int ai=juce::jlimit(0,31,int(p.parameterValue("fm.algorithm")));
        const float morph=juce::jlimit(0.f,1.f,p.parameterValue("fm.morph"));
        const auto& cur=aether::fm::algorithm(ai);const auto& nxt=aether::fm::algorithm((ai+1)%32);
        std::array<float,6> carriers{};int activeCount=0;
        for(int i=0;i<6;++i){
            float carrier=cur.carriers[size_t(i)]+(nxt.carriers[size_t(i)]-cur.carriers[size_t(i)])*morph;
            if(p.parameterValue("fm.custom")>.5f)carrier=1.f+(cur.carriers[size_t(i)]-1.f)*morph;
            carriers[size_t(i)]=carrier*p.parameterValue(oid(i,"carrier"));
            if(carriers[size_t(i)]>.001f)++activeCount;
        }
        const float busY=h-54.f;const float portWidth=(w-48.f)/float(juce::jmax(1,activeCount));int active=0;
        if(activeCount>1){g.setColour(earth.withAlpha(.5f));g.drawLine(24.f+portWidth*.5f,busY,w-24.f-portWidth*.5f,busY,1.f);}
        for(int i=0;i<6;++i)if(carriers[size_t(i)]>.001f){
            const float outX=24.f+(float(active++)+.5f)*portWidth;const auto c=points[size_t(i)];
            g.setColour(earth.withAlpha(.8f));g.drawArrow({c.x,c.y+18.f,outX,busY},1.2f,4.f,4.f);g.fillEllipse(outX-2.5f,busY-2.5f,5,5);
            text(g,juce::String(carriers[size_t(i)],1),{int(outX-portWidth*.5f),int(busY)+4,int(portWidth),14},9,earth,juce::Justification::centred);
        }
        for(int i=0;i<6;++i){auto c=points[size_t(i)];g.setColour(light);g.fillEllipse(c.x-16,c.y-16,32,32);g.setColour(ink);g.drawEllipse(c.x-16,c.y-16,32,32,1);text(g,juce::String(i+1),{int(c.x)-14,int(c.y)-14,28,28},15,ink,juce::Justification::centred,true);}
        text(g,activeCount>0?"OUTPUT":"OUTPUT / SILENT",{0,int(h)-35,int(w),15},9,activeCount>0?earth:muted,juce::Justification::centred);
        text(g,p.parameterValue("fm.custom")>.5f?"CUSTOM / STORED MATRIX":"ALGORITHM / MORPH TO NEXT",{0,int(h)-16,int(w),16},9,muted,juce::Justification::centred);
    }
    void paintFmMatrix(juce::Graphics& g){for(int i=0;i<6;++i){text(g,juce::String(i+1),{38+i*30,5,27,20},10,blue,juce::Justification::centred);text(g,juce::String(i+1),{4,30+i*30,27,27},10,earth,juce::Justification::centred);}for(int d=0;d<6;++d)for(int s=0;s<6;++s){const float v=fmEdge(s,d);auto r=juce::Rectangle<int>(38+s*30,30+d*30,27,27);g.setColour(light);g.fillRect(r);if(v>.001f){const auto tone=s>=d?earth:blue;g.setColour(tone.withAlpha(.16f+.26f*v));g.fillRect(r);g.setColour(tone);g.fillRect(r.getX(),r.getBottom()-int(2+4*v),r.getWidth(),int(2+4*v));}g.setColour(line);g.drawRect(r);text(g,v>.001f?juce::String(v,1):"-",r.withTrimmedBottom(4),11,v>.001f?ink:muted,juce::Justification::centred);}}
    void paintMotion(juce::Graphics& g){const int cw=motionScope.getWidth()/4;for(int n=0;n<4;++n){const float scopeH=compact?20.f:35.f;auto r=juce::Rectangle<float>(float(n*cw+12),compact?3.f:4.f,float(cw-25),scopeH);g.setColour(line);g.drawLine(r.getX(),r.getCentreY(),r.getRight(),r.getCentreY());juce::Path waveform;const int shape=int(p.parameterValue("lfo."+juce::String(n+1)+".shape"));const float phase=p.parameterValue("lfo."+juce::String(n+1)+".phase");for(int i=0;i<100;++i){float t=float(i)/99+phase,v=0;t-=std::floor(t);switch(shape){case 0:v=std::sin(t*juce::MathConstants<float>::twoPi);break;case 1:v=1-4*std::abs(t-.5f);break;case 2:v=2*t-1;break;case 3:v=t<.5f?1.f:-1.f;break;default:v=std::sin(std::floor(t*7)*27.4f);break;}float x=r.getX()+r.getWidth()*float(i)/99,y=r.getCentreY()-v*r.getHeight()*.4f;if(i==0)waveform.startNewSubPath(x,y);else waveform.lineTo(x,y);}g.setColour(n%2?earth:blue);g.strokePath(waveform,juce::PathStrokeType(1.2f));const float live=p.modulationMeters[size_t(n+1)].load();g.fillEllipse(r.getRight()-5,r.getCentreY()-live*(scopeH*.4f)-3,6,6);}}
    // ---- Randomize (stage B): header split button + popover. Exists only when a backend factory is installed (see setRandomizeBackendFactory); the engine side stays behind aether::ui::RandomizeBackend. ----
    aether::ui::RandomizeStyle randomStyle()const{
        aether::ui::RandomizeStyle s;s.paper=paper;s.light=light;s.ink=ink;s.muted=muted;s.earthText=earthText;s.line=line;s.blue=blue;
        s.drawText=[](juce::Graphics& g,const juce::String& t,juce::Rectangle<int> r,float size,juce::Colour c,int j){text(g,t,r,size,c,j);};
        s.font=[](float size){return type(size);};
        s.textFloor=[](const juce::Component& c){return legibleFloor(scaleOf(c));};
        return s;
    }
    void setupRandomize(){
        if(!randomizeFactory)return;randomBackend=randomizeFactory(p);if(!randomBackend)return;
        randomCaret=std::make_unique<aether::ui::GlyphButton>(randomStyle(),aether::ui::GlyphButton::Kind::caret);
        randomRoll.setComponentID("random.header.roll");randomRoll.setName("Randomize");randomRoll.setTooltip("Randomize with the amount, seed and locks set in the Randomize panel.");
        randomCaret->setComponentID("random.header.menu");randomCaret->setTooltip("Randomize settings: amount, seed, locks, undo.");
        addAndMakeVisible(randomRoll);addAndMakeVisible(*randomCaret);
        randomPanel=std::make_unique<aether::ui::RandomizePanel>(*randomBackend,randomStyle(),[this]{randomChanged();});
        addChildComponent(*randomPanel);randomPanel->addAnchor(*randomCaret);randomPanel->addAnchor(randomRoll);
        randomRoll.onClick=[this]{randomPanel->roll(false);};
        randomCaret->onClick=[this]{randomPanel->setOpen(!randomPanel->isOpen());positionRandomPanel();};
        randomPanel->notePreset(p.currentPresetName(),p.isSnapshotB());refreshRandomHint();
    }
    // What a roll does in this edition and Mode (one line in the popover).
    juce::String randomHint()const{
        if(synthEdition())return "The next notes use the new patch.";
        return shownMode()==0?"FX mode: FM and Source stay idle.":"Resynth: Source follows the input.";
    }
    void refreshRandomHint(){if(randomPanel)randomPanel->setHint(randomHint());}
    // The popover hangs below the caret, right-aligned to it, and stays inside the canvas.
    void positionRandomPanel(){
        if(!randomPanel||!randomCaret)return;const int w=randomPanel->getWidth(),h=randomPanel->getHeight();
        const int x=juce::jlimit(8,juce::jmax(8,getWidth()-w-8),randomCaret->getRight()-w),y=juce::jmin(randomCaret->getBottom()+4,juce::jmax(4,getHeight()-h-4));
        randomPanel->setTopLeftPosition(x,y);
    }
    // "(Random) <seed>" while a roll is on screen, otherwise the processor's preset name.
    juce::String shownPresetName()const{if(randomPanel){const auto label=randomPanel->presetLabel();if(label.isNotEmpty())return label;}return p.currentPresetName();}
    void updateRandomTooltip(){if(!randomPanel)return;const auto label=randomPanel->presetLabel();presets.setTooltip(label.isNotEmpty()?"Random roll of "+randomPanel->sourcePreset()+". Choose a preset to leave it.":juce::String());}
    void randomChanged(){const auto shown=shownPresetName();if(shown!=currentName){currentName=shown;presets.setText(shown,juce::dontSendNotification);}updateRandomTooltip();repaint();}
    bool randomAction(const juce::String& a){
        if(!randomPanel)return false;
        if(a=="random.open"){randomPanel->setOpen(true);positionRandomPanel();return true;}
        if(a=="random.close"){randomPanel->setOpen(false);return true;}
        if(a=="random.roll"){randomPanel->roll(false);return true;}
        if(a=="random.rollb"){randomPanel->roll(true);return true;}
        if(a=="random.undo")return randomPanel->undo();
        return false;
    }
    void timerCallback()override{if(randomPanel){randomPanel->notePreset(p.currentPresetName(),p.isSnapshotB());refreshRandomHint();}const auto name=shownPresetName();if(name!=currentName){currentName=name;presets.setText(name,juce::dontSendNotification);updateRandomTooltip();}ab.setButtonText(p.isSnapshotB()?"B / A":"A / B");if(lastType!=int(p.parameterValue(nid(slot,"type")))){rebuildInspector();resized();}algorithm.setSelectedId(int(p.parameterValue("routing.algorithm"))+1,juce::dontSendNotification);fmAlgorithm.setSelectedId(int(p.parameterValue("fm.algorithm"))+1,juce::dontSendNotification);for(int i=0;i<4;++i){auto& rateControl=*motionControls[size_t(i*4)];const bool synced=p.parameterValue("lfo."+juce::String(i+1)+".sync")>.5f;const juce::String label=synced?"Rate / cyc per beat":"Rate / Hz";if(rateControl.title!=label){rateControl.title=label;rateControl.slider.textFromValueFunction=[synced](double v){return juce::String(v,v<1?2:1)+(synced?" cpb":" Hz");};rateControl.slider.updateText();rateControl.repaint();}}if(workspace==3&&!minimal){syncSourceEngine();refreshSource();}if(!minimal){refreshNotice();canvas.repaint();gallery.repaint();fmDiagram.repaint();fmMatrix.repaint();motionScope.repaint();repaint();}}
    bool action(const juce::String& a){if(a.startsWith("random."))return randomAction(a);auto parts=juce::StringArray::fromTokens(a,".","");if(parts.size()==2&&parts[0]=="zoom"){const int value=parts[1].getIntValue();if(value==50||value==75||value==100){setZoom(value==50?.5f:(value==75?.75f:1.f));return true;}return false;}if(a=="workspace.patch"){patchTab.triggerClick();return true;}if(a=="workspace.fm"){fmTab.triggerClick();return true;}if(a=="workspace.motion"){motionTab.triggerClick();return true;}if(a=="workspace.source"){sourceTab.triggerClick();return true;}if(a=="workspace.minimal"){minimalButton.triggerClick();return true;}if(parts.size()==2&&parts[0]=="slot"){select(parts[1].getIntValue()-1);return true;}if(parts.size()==2&&parts[0]=="module"){assign(parts[1].getIntValue());return true;}if(parts.size()==2&&parts[0]=="routing"){algorithm.setSelectedId(parts[1].getIntValue()+1,juce::sendNotificationSync);return true;}if(parts.size()==3&&parts[0]=="fm"&&parts[1]=="algorithm"){fmAlgorithm.setSelectedId(parts[2].getIntValue()+1,juce::sendNotificationSync);return true;}if(parts.size()==3&&parts[0]=="route"){int s=parts[1].getIntValue()-1,d=parts[2].getIntValue()-1;if(s<0||s>11||d<0||d>11)return false;positionNodes();juce::MouseEvent e(juce::Desktop::getInstance().getMainMouseSource(),port(s,true),juce::ModifierKeys(),1.f,0.f,0.f,0.f,0.f,&canvas,&canvas,juce::Time(),port(s,true),juce::Time(),1,false);canvasDown(e);juce::MouseEvent end=e.withNewPosition(port(d,false));canvasDown(end);return true;}if(parts.size()==4&&parts[0]=="fm"&&parts[1]=="edge"){int s=parts[2].getIntValue()-1,d=parts[3].getIntValue()-1;if(s<0||s>5||d<0||d>5)return false;juce::MouseEvent e(juce::Desktop::getInstance().getMainMouseSource(),{float(38+s*30+13),float(30+d*30+13)},juce::ModifierKeys(),1.f,0.f,0.f,0.f,0.f,&fmMatrix,&fmMatrix,juce::Time(),{},juce::Time(),1,false);fmMatrix.mouseDown(e);return true;}return false;}
};

AetherAudioProcessorEditor::AetherAudioProcessorEditor(AetherAudioProcessor& p):AudioProcessorEditor(&p),ui(std::make_unique<Impl>(*this,p)){addAndMakeVisible(*ui);setResizable(true,true);setResizeLimits(1180,780,2200,1500);setSize(1440,900);}
AetherAudioProcessorEditor::~AetherAudioProcessorEditor()=default;
void AetherAudioProcessorEditor::paint(juce::Graphics& g){g.fillAll(paper);}
// Fit the 1440x900 logical canvas to the actual window: scale by the limiting axis and give the other axis extra logical room, so nothing is clipped at any size.
void AetherAudioProcessorEditor::resized(){
    if(!ui)return;
    const float w=float(juce::jmax(1,getWidth())),h=float(juce::jmax(1,getHeight()));
    // Small windows (50% preset) use the compact layout so fonts can be larger relative to the canvas.
    const bool compact=juce::jmin(w/baseWidth,h/baseHeight)<compactBelow;
    const float bw=compact?compactWidth:baseWidth,bh=compact?compactHeight:baseHeight;
    const float fit=juce::jmax(.25f,juce::jmin(w/bw,h/bh));
    const bool changed=ui->applyCompact(compact);
    ui->setBounds(0,0,juce::jmax(int(bw),int(std::ceil(w/fit-.001f))),juce::jmax(int(bh),int(std::ceil(h/fit-.001f))));
    ui->setTransform(juce::AffineTransform::scale(fit));
    if(changed){ui->resized();ui->repaint();}
}
void AetherAudioProcessorEditor::setMinimalMode(bool b){ui->setMinimal(b);}
void AetherAudioProcessorEditor::setWorkspace(int w){ui->setWorkspace(w);}
void AetherAudioProcessorEditor::selectSlot(int s){ui->select(s);}
bool AetherAudioProcessorEditor::performUiAction(const juce::String& s){return ui->action(s);}
AetherAudioProcessorEditor::FmPaintGeometry AetherAudioProcessorEditor::fmPaintGeometry() const{return ui->fmPaintGeometry();}
std::vector<AetherAudioProcessorEditor::SourcePaintText> AetherAudioProcessorEditor::sourcePaintTexts() const{return ui->sourceTexts();}
void AetherAudioProcessorEditor::setEditionForTesting(int edition){editionOverride=edition;}
void AetherAudioProcessorEditor::setRandomizeBackendFactory(RandomizeBackendFactory factory){randomizeFactory=std::move(factory);}
aether::ui::RandomizePanel* AetherAudioProcessorEditor::randomizePanel(){return ui->randomPanel.get();}
std::vector<aether::ui::RandomizeTextItem> AetherAudioProcessorEditor::randomizeTexts(){
    if(!ui->randomPanel)return {};
    ui->refreshRandomHint();
    auto items=ui->randomPanel->textItems();const float floorSize=legibleFloor(scaleOf(*ui->randomPanel));
    for(auto& item:items)item.size=juce::jmax(floorSize,item.size);
    return items;
}
juce::String AetherAudioProcessorEditor::pageNotice(){ui->refreshNotice();return ui->noticeLabel.getText();}
juce::String AetherAudioProcessorEditor::modulationTargetGroup(const juce::String& item){return targetGroupOf(item);}
void AetherAudioProcessorEditor::fillModulationTargets(juce::ComboBox& combo,const juce::StringArray& items){fillTargetCombo(combo,items);}
juce::String AetherAudioProcessorEditor::sourceStatus(){ui->syncSourceEngine();ui->refreshSource();return ui->statusLabel.getText();}
