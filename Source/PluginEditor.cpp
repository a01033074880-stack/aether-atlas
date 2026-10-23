#include "PluginEditor.h"
#include <cmath>

namespace {
const juce::Colour background{0xff101518}, panel{0xff192125}, edge{0xff2b363b},
    text{0xffe0e9e7}, muted{0xff839795}, mint{0xff8ae5c7}, amber{0xffe8ba7e};
const std::array<juce::Colour, 5> accents{{mint, juce::Colour{0xff8dc9e6}, amber,
                                       juce::Colour{0xffb3a2df}, juce::Colour{0xffd6dfb0}}};
juce::Font font(float size, bool bold = false) {
    return juce::Font{juce::FontOptions{size, bold ? juce::Font::bold : juce::Font::plain}};
}
void write(juce::Graphics& g, juce::String value, juce::Rectangle<int> r,
           float size, juce::Colour colour, int justify = juce::Justification::centredLeft) {
    g.setColour(colour); g.setFont(font(size));
    g.drawText(value, r, justify, true);
}
juce::String format(float value, const juce::String& unit) {
    if (unit == "%") return juce::String(juce::roundToInt(value * 100.0f)) + "%";
    if (unit == "Hz" && value >= 1000) return juce::String(value / 1000.0f, 2) + " kHz";
    if (unit == "Hz") return (value<20 ? juce::String(value,2) : juce::String(juce::roundToInt(value))) + " Hz";
    if (unit == "ms") return juce::String(juce::roundToInt(value)) + " ms";
    return juce::String(value, std::abs(value) < 10.0f ? 2 : 1)
           + (unit.isEmpty() ? "" : " " + unit);
}
}

class AetherAudioProcessorEditor::Theme final : public juce::LookAndFeel_V4 {
public:
    Theme() {
        setColour(juce::Slider::textBoxTextColourId, text);
        setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        setColour(juce::ComboBox::backgroundColourId, juce::Colour{0xff111a1d});
        setColour(juce::ComboBox::outlineColourId, edge);
        setColour(juce::ComboBox::textColourId, text);
        setColour(juce::ComboBox::arrowColourId, muted);
        setColour(juce::PopupMenu::backgroundColourId, panel);
        setColour(juce::PopupMenu::textColourId, text);
        setColour(juce::PopupMenu::highlightedBackgroundColourId, mint.withAlpha(0.15f));
        setColour(juce::PopupMenu::highlightedTextColourId, mint);
        setColour(juce::TooltipWindow::backgroundColourId, panel);
        setColour(juce::TooltipWindow::textColourId, text);
        setColour(juce::TooltipWindow::outlineColourId, edge);
    }
    juce::Font getComboBoxFont(juce::ComboBox&) override { return font(12.0f); }
    juce::Font getTextButtonFont(juce::TextButton&, int) override { return font(12.0f); }
    juce::Font getLabelFont(juce::Label&) override { return font(12.0f); }
    void drawButtonBackground(juce::Graphics& g, juce::Button& button, const juce::Colour&,
                              bool hover, bool down) override {
        const bool on = button.getToggleState();
        auto bounds = button.getLocalBounds().toFloat().reduced(0.5f);
        g.setColour(on ? mint.withAlpha(0.16f) : (hover || down ? edge : panel));
        g.fillRoundedRectangle(bounds, 5.0f);
        g.setColour(on ? mint.withAlpha(0.55f) : edge);
        g.drawRoundedRectangle(bounds, 5.0f, 1.0f);
    }
    void drawButtonText(juce::Graphics& g, juce::TextButton& b, bool, bool) override {
        write(g, b.getButtonText(), b.getLocalBounds().reduced(5, 0), 12.0f,
              b.getToggleState() ? mint : text, juce::Justification::centred);
        if (b.hasKeyboardFocus(true)) { g.setColour(mint); g.drawRect(b.getLocalBounds().reduced(2)); }
    }
    void drawToggleButton(juce::Graphics& g, juce::ToggleButton& b, bool hover, bool) override {
        auto r = b.getLocalBounds();
        const auto colour = b.getToggleState() ? mint : muted;
        g.setColour(colour.withAlpha(hover ? 0.25f : 0.12f));
        g.fillRoundedRectangle(r.toFloat().reduced(1), 4.0f);
        g.setColour(colour); g.fillEllipse(8.0f, static_cast<float>(r.getCentreY()) - 3, 6, 6);
        const auto caption = b.getButtonText()=="ON" ? (b.getToggleState()?"ON":"OFF") : b.getButtonText();
        write(g, caption, r.withTrimmedLeft(21), 11.0f, colour);
        if (b.hasKeyboardFocus(true)) { g.setColour(mint); g.drawRect(r.reduced(1)); }
    }
    void drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                          float position, float, float, juce::Slider::SliderStyle,
                          juce::Slider& slider) override {
        const float centreY = static_cast<float>(y) + static_cast<float>(height) * 0.5f;
        g.setColour(edge); g.fillRoundedRectangle(static_cast<float>(x), centreY - 2,
                                                  static_cast<float>(width), 4, 2);
        const auto colour = slider.findColour(juce::Slider::rotarySliderFillColourId);
        g.setColour(colour.withAlpha(0.65f));
        g.fillRoundedRectangle(static_cast<float>(x), centreY - 2,
                                juce::jmax(1.0f, position - static_cast<float>(x)), 4, 2);
        g.setColour(colour); g.fillEllipse(position - 3.5f, centreY - 3.5f, 7, 7);
        if (slider.hasKeyboardFocus(true)) { g.setColour(mint); g.drawRect(slider.getLocalBounds().reduced(1)); }
    }
    void drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                          float position, float start, float end, juce::Slider& slider) override {
        const auto bounds = juce::Rectangle<float>{static_cast<float>(x), static_cast<float>(y),
            static_cast<float>(width), static_cast<float>(height)}.reduced(5);
        const float radius = juce::jmin(bounds.getWidth(), bounds.getHeight()) * 0.5f;
        const auto centre = bounds.getCentre();
        const auto colour = slider.findColour(juce::Slider::rotarySliderFillColourId);
        juce::Path track, value;
        track.addCentredArc(centre.x, centre.y, radius, radius, 0, start, end, true);
        value.addCentredArc(centre.x, centre.y, radius, radius, 0, start, start + position * (end-start), true);
        g.setColour(edge); g.strokePath(track, juce::PathStrokeType(3.0f));
        g.setColour(colour); g.strokePath(value, juce::PathStrokeType(3.0f));
        g.setColour(juce::Colour{0xff11191d});
        g.fillEllipse(centre.x-radius+7, centre.y-radius+7, 2*radius-14, 2*radius-14);
        const float angle = start + position * (end-start) - juce::MathConstants<float>::halfPi;
        g.setColour(colour);
        g.drawLine(centre.x + std::cos(angle)*(radius-16), centre.y+std::sin(angle)*(radius-16),
                   centre.x+std::cos(angle)*(radius-9), centre.y+std::sin(angle)*(radius-9), 2.0f);
        if (slider.hasKeyboardFocus(true)) { g.setColour(mint); g.drawRect(slider.getLocalBounds().reduced(1)); }
    }
};

class AetherAudioProcessorEditor::Control final : public juce::Component {
public:
    Control(AetherAudioProcessor& processor, const aether::ParameterSpec& spec, juce::Colour colour)
        : specification(spec) {
        setName(juce::String(spec.group) + " " + spec.name);
        label.setText(spec.name, juce::dontSendNotification);
        label.setColour(juce::Label::textColourId, muted);
        label.setFont(font(11)); label.setBorderSize({0,0,0,0});
        addAndMakeVisible(label);
        juce::String tip = juce::String(spec.group) + ": " + spec.name;
        if (spec.boolean) {
            toggle.setButtonText(spec.name); toggle.setTooltip(tip);
            toggle.setName(getName()); addAndMakeVisible(toggle);
            buttonAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment>(processor.state, spec.id, toggle);
            label.setVisible(false);
        } else if (!spec.choices.isEmpty()) {
            combo.addItemList(spec.choices, 1); combo.setTooltip(tip); combo.setName(getName());
            addAndMakeVisible(combo);
            comboAttachment = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment>(processor.state,spec.id,combo);
        } else {
            slider.setSliderStyle(juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 65, 20);
            slider.setColour(juce::Slider::rotarySliderFillColourId, colour);
            slider.setDoubleClickReturnValue(true, spec.defaultValue);
            slider.setTooltip(tip + ". Double-click to reset; drag or use arrow keys to adjust.");
            slider.setName(getName()); slider.setWantsKeyboardFocus(true);
            addAndMakeVisible(slider);
            sliderAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(processor.state,spec.id,slider);
            const juce::String unit = spec.unit;
            slider.textFromValueFunction = [unit](double value) { return format(static_cast<float>(value), unit); };
            slider.valueFromTextFunction = [unit](const juce::String& input) {
                auto v = input.getDoubleValue();
                if (unit == "%") v *= 0.01;
                if (unit == "Hz" && input.containsIgnoreCase("k")) v *= 1000.0;
                return v;
            };
            slider.updateText();
        }
    }
    void layout(bool isPrimary, bool isCompact = false) {
        primary = isPrimary; compact = isCompact;
        label.setText(compact ? juce::String(specification.group) + " · " + specification.name : specification.name,
                      juce::dontSendNotification);
        if (buttonAttachment) {
            const juce::String id{specification.id};
            toggle.setButtonText(!compact && id.endsWith(".enabled") ? "ON" : specification.name);
        }
        if (sliderAttachment) {
            slider.setSliderStyle(primary ? juce::Slider::RotaryHorizontalVerticalDrag : juce::Slider::LinearHorizontal);
            slider.setTextBoxStyle(primary ? juce::Slider::TextBoxBelow : juce::Slider::TextBoxRight,
                                   false, primary ? 80 : 65, primary ? 20 : 18);
        }
        resized();
    }
    void stageToggle() { toggle.setButtonText("ON"); }
    void resized() override {
        auto r = getLocalBounds();
        if (buttonAttachment) { toggle.setBounds(r); return; }
        if (compact) {
            label.setJustificationType(juce::Justification::centredLeft);
            label.setBounds(r.removeFromTop(17));
            if (comboAttachment) combo.setBounds(r.reduced(0,2));
            else slider.setBounds(r);
        } else if (primary) {
            label.setJustificationType(juce::Justification::centred);
            label.setBounds(r.removeFromTop(17)); slider.setBounds(r);
        } else {
            label.setJustificationType(juce::Justification::centredLeft);
            label.setBounds(r.removeFromTop(11));
            if (comboAttachment) combo.setBounds(r.reduced(0,1));
            else slider.setBounds(r);
        }
    }
    const aether::ParameterSpec& specification;
private:
    juce::Label label;
    juce::Slider slider;
    juce::ComboBox combo;
    juce::ToggleButton toggle;
    bool primary = false, compact = false;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> sliderAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> comboAttachment;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> buttonAttachment;
};

AetherAudioProcessorEditor::AetherAudioProcessorEditor(AetherAudioProcessor& p)
    : AudioProcessorEditor(&p), processor(p), theme(std::make_unique<Theme>()) {
    setLookAndFeel(theme.get()); setOpaque(true);
    const auto& specs = aether::parameterSpecs();
    for (const auto& spec : specs) {
        auto colour = mint;
        const juce::String group{spec.group};
        if (group=="Modal") colour=accents[1];
        if (group=="Colour") colour=accents[2];
        if (group=="Space") colour=accents[3];
        if (group=="Master") colour=accents[4];
        auto c=std::make_unique<Control>(p,spec,colour); addAndMakeVisible(*c); controls.push_back(std::move(c));
    }
    for (auto i : {aether::spectralOn,aether::modalOn,aether::colourOn,aether::spaceOn,aether::masterOn}) control(i).stageToggle();
    presets.addItemList(AetherAudioProcessor::factoryPresetNames(),1);
    presets.setName("Factory preset"); presets.setTooltip("Choose a factory starting point. Load and Save use .aether preset files.");
    presets.onChange=[this] { if (presets.getSelectedId()>0) { processor.loadFactoryPreset(presets.getSelectedId()-1); displayedPreset.clear(); } };
    displayedPreset=processor.currentPresetName(); presets.setText(displayedPreset,juce::dontSendNotification);
    addAndMakeVisible(presets);
    for (auto* b : {&loadButton,&saveButton,&abButton,&minimalButton,&panicButton}) { addAndMakeVisible(*b); b->setName(b->getButtonText()); }
    loadButton.setTooltip("Open an AETHER .aether preset"); saveButton.setTooltip("Save the current sound as an .aether preset");
    abButton.setTooltip("Swap between the A and B parameter snapshots");
    minimalButton.setTooltip("Compact all-parameter view. Spectrum analysis pauses to save resources.");
    panicButton.setTooltip("Clear resonators, space tails and held notes");
    minimalButton.setClickingTogglesState(true);
    loadButton.onClick=[this]{choosePresetFile(false);}; saveButton.onClick=[this]{choosePresetFile(true);};
    abButton.onClick=[this]{processor.toggleAB(); abButton.setButtonText(processor.isSnapshotB()?"B  /  A":"A  /  B");
        displayedPreset=processor.currentPresetName(); presets.setText(displayedPreset,juce::dontSendNotification);};
    minimalButton.onClick=[this]{setMinimalMode(minimalButton.getToggleState());};
    panicButton.onClick=[this]{processor.requestPanic();};
    setResizable(true,true); setResizeLimits(900,680,1800,1300); setSize(1080,760);
    processor.analysisEnabled.store(true); spectrum.fill(-100.0f); startTimerHz(30);
}
AetherAudioProcessorEditor::~AetherAudioProcessorEditor() {
    stopTimer(); processor.analysisEnabled.store(false); chooser.reset(); setLookAndFeel(nullptr);
}
AetherAudioProcessorEditor::Control& AetherAudioProcessorEditor::control(int index) { return *controls[static_cast<size_t>(index)]; }
void AetherAudioProcessorEditor::setMinimalMode(bool shouldBeMinimal) {
    minimal=shouldBeMinimal; minimalButton.setToggleState(minimal,juce::dontSendNotification);
    processor.analysisEnabled.store(!minimal); fftWrite=0;
    if (minimal) { spectrum.fill(-100.0f); receivedAudio=false; }
    startTimerHz(minimal?8:30); resized(); repaint();
}
void AetherAudioProcessorEditor::choosePresetFile(bool save) {
    loadButton.setEnabled(false); saveButton.setEnabled(false);
    chooser=std::make_unique<juce::FileChooser>(save?"Save AETHER preset":"Load AETHER preset",
        juce::File::getSpecialLocation(juce::File::userDocumentsDirectory),"*.aether");
    juce::Component::SafePointer<AetherAudioProcessorEditor> safe{this};
    chooser->launchAsync((save?juce::FileBrowserComponent::saveMode|juce::FileBrowserComponent::warnAboutOverwriting:
                               juce::FileBrowserComponent::openMode)|juce::FileBrowserComponent::canSelectFiles,
        [safe,save](const juce::FileChooser& fileChooser) {
            if (!safe) return;
            safe->loadButton.setEnabled(true); safe->saveButton.setEnabled(true);
            auto file=fileChooser.getResult(); if (file==juce::File{}) return;
            if (save) file=file.withFileExtension(".aether");
            const auto result=save?safe->processor.savePreset(file):safe->processor.loadPreset(file);
            safe->displayedPreset.clear();
            if (result.failed()) juce::AlertWindow::showMessageBoxAsync(juce::MessageBoxIconType::WarningIcon,
                "Preset could not be opened",result.getErrorMessage(),"OK",safe.getComponent());
        });
}

void AetherAudioProcessorEditor::resized() {
    const int w=getWidth(), h=getHeight(), margin=20, gap=10;
    auto toolbar=juce::Rectangle<int>{margin,54,w-2*margin,39};
    const int presetWidth=juce::jlimit(175,280,w/4);
    presets.setBounds(toolbar.removeFromLeft(presetWidth).reduced(0,4)); toolbar.removeFromLeft(8);
    for (auto* b : {&loadButton,&saveButton,&abButton}) { b->setBounds(toolbar.removeFromLeft(53).reduced(2,4)); }
    minimalButton.setBounds(toolbar.removeFromRight(78).reduced(2,4));
    if (!minimal) {
        control(aether::bypass).setBounds(toolbar.removeFromRight(83).reduced(2,4));
        control(aether::bypass).layout(false,true);
        control(aether::mode).setBounds(toolbar.removeFromRight(112).reduced(4,0)); control(aether::mode).layout(false,true);
    }
    for (auto& c:controls) c->setVisible(true);
    if (minimal) {
        spectrumBounds={}; footerBounds={}; orderBounds={}; cards.fill({});
        const int columns=4, rows=10, cellWidth=(w-2*margin-3*gap)/columns;
        const int cellHeight=(h-129)/rows;
        for (int i=0;i<aether::parameterCount;++i) {
            auto& c=control(i); c.setBounds(margin+(i/rows)*(cellWidth+gap),112+(i%rows)*cellHeight,cellWidth,cellHeight-5);
            c.layout(false,true);
        }
        panicButton.setBounds(w-95,18,75,27);
        return;
    }
    const int footerHeight=140;
    const int cardsHeight=juce::jlimit(280,320,(h-100-footerHeight-36)*57/100);
    const int spectrumHeight=h-100-footerHeight-cardsHeight-55;
    spectrumBounds={margin,103,w-2*margin,spectrumHeight};
    orderBounds={margin,spectrumBounds.getBottom()+5,w-2*margin,31};
    control(aether::order).setBounds(orderBounds.withTrimmedLeft(100).withWidth(juce::jmin(470,w-320)));
    control(aether::order).layout(false);
    const int cardY=orderBounds.getBottom()+8, cardWidth=(w-2*margin-4*gap)/5;
    const std::array<int,5> first{{aether::spectralOn,aether::modalOn,aether::colourOn,aether::spaceOn,aether::masterOn}};
    const std::array<int,5> last{{aether::transientProtect,aether::modalMaterial,aether::driveModel,aether::freeze,aether::dither}};
    for (int stage=0;stage<5;++stage) {
        auto& card=cards[static_cast<size_t>(stage)]; card={margin+stage*(cardWidth+gap),cardY,cardWidth,cardsHeight};
        auto body=card.reduced(13,10); auto header=body.removeFromTop(25);
        control(first[static_cast<size_t>(stage)]).setBounds(header.removeFromRight(48));
        control(first[static_cast<size_t>(stage)]).layout(false);
        const int primaryIndex=first[static_cast<size_t>(stage)]+1;
        const int secondaryCount=last[static_cast<size_t>(stage)]-primaryIndex;
        const int primaryHeight=92;
        control(primaryIndex).setBounds(body.removeFromTop(primaryHeight)); control(primaryIndex).layout(true);
        const int rowHeight=body.getHeight()/juce::jmax(1,secondaryCount);
        for (int i=primaryIndex+1;i<=last[static_cast<size_t>(stage)];++i) {
            auto& c=control(i); c.setBounds(body.removeFromTop(rowHeight).reduced(0,1)); c.layout(false);
        }
    }
    footerBounds={margin,cardY+cardsHeight+10,w-2*margin,footerHeight-12};
    auto inner=footerBounds.reduced(13,10); inner.removeFromTop(17);
    const int telemetryWidth=145, globalWidth=juce::jmax(180,w/5);
    auto telemetry=inner.removeFromRight(telemetryWidth); inner.removeFromRight(16);
    auto globals=inner.removeFromRight(globalWidth); inner.removeFromRight(16);
    const int modWidth=inner.getWidth();
    auto modTop=inner.removeFromTop(38);
    const std::array<int,3> modRow{{aether::modRate,aether::modDepth,aether::modSync}};
    for (int k=0;k<3;++k) { auto& c=control(modRow[static_cast<size_t>(k)]); c.setBounds(modTop.removeFromLeft(modWidth/3).reduced(0,1)); c.layout(false); }
    auto& target=control(aether::modTarget); target.setBounds(inner.removeFromLeft(modWidth/2).reduced(0,1)); target.layout(false);
    auto& shape=control(aether::modShape); shape.setBounds(inner.reduced(2,1)); shape.layout(false);
    for (int i=aether::input;i<=aether::mix;++i) { auto& c=control(i); c.setBounds(globals.removeFromTop(30)); c.layout(false); }
    panicButton.setBounds(telemetry.getRight()-58,footerBounds.getY()+7,58,20);
}

void AetherAudioProcessorEditor::paint(juce::Graphics& g) {
    g.fillAll(background);
    g.setGradientFill(juce::ColourGradient(juce::Colour{0xff1d2b2c},0,0,background,
        static_cast<float>(getWidth()),static_cast<float>(getHeight())*0.7f,false));
    g.fillRect(getLocalBounds());
    g.setFont(font(25,true)); g.setColour(text); g.drawText("AETHER",20,14,125,30,juce::Justification::centredLeft);
    write(g,"MICRO",{150,19,65,25},12,mint);
    write(g,"1.0.0  /  " + juce::String(JucePlugin_Name),{219,20,360,23},11,muted);
    if (!minimal) write(g,"SOUND, IN ITS ELEMENT.",{getWidth()-275,20,255,23},10,muted,juce::Justification::centredRight);
    g.setColour(edge); g.drawHorizontalLine(99,20.0f,static_cast<float>(getWidth()-20));
    if (minimal) {
        const int cellWidth=(getWidth()-70)/4;
        for (int i=0;i<4;++i) {
            auto r=juce::Rectangle<int>{16+i*(cellWidth+10),107,cellWidth+8,getHeight()-120};
            g.setColour(panel.withAlpha(0.55f)); g.fillRoundedRectangle(r.toFloat(),7);
            g.setColour(edge); g.drawRoundedRectangle(r.toFloat(),7,1);
        }
        write(g,"ALL PARAMETERS  /  ANALYSER PAUSED",{20,getHeight()-15,getWidth()-40,13},9,muted);
        return;
    }
    paintSpectrum(g);
    write(g,"SIGNAL FLOW",orderBounds.withWidth(92),10,muted);
    write(g,"MASTER ALWAYS LAST",orderBounds.withTrimmedLeft(orderBounds.getWidth()-175),9,muted,juce::Justification::centredRight);
    const std::array<juce::String,5> names{{"SPECTRAL","MODAL","COLOUR","SPACE","MASTER"}};
    for (int i=0;i<5;++i) {
        const auto r=cards[static_cast<size_t>(i)]; const auto accent=accents[static_cast<size_t>(i)];
        g.setColour(panel); g.fillRoundedRectangle(r.toFloat(),8);
        g.setColour(edge); g.drawRoundedRectangle(r.toFloat().reduced(0.5f),8,1);
        g.setColour(accent.withAlpha(0.8f)); g.fillRoundedRectangle(static_cast<float>(r.getX()+13),static_cast<float>(r.getY()),34,2,1);
        write(g,names[static_cast<size_t>(i)],r.reduced(13,0).withY(r.getY()+10).withHeight(25).withTrimmedRight(50),12,accent);
    }
    g.setColour(panel); g.fillRoundedRectangle(footerBounds.toFloat(),8);
    g.setColour(edge); g.drawRoundedRectangle(footerBounds.toFloat().reduced(0.5f),8,1);
    const int globalWidth=juce::jmax(180,getWidth()/5);
    write(g,"MODULATION",footerBounds.reduced(13,0).withY(footerBounds.getY()+7).withHeight(17),10,amber);
    write(g,"GLOBAL",{footerBounds.getRight()-13-145-16-globalWidth,footerBounds.getY()+7,globalWidth,17},10,mint);
    paintMeters(g,{footerBounds.getRight()-158,footerBounds.getY()+30,145,footerBounds.getHeight()-35});
}

void AetherAudioProcessorEditor::paintSpectrum(juce::Graphics& g) {
    // A host may request its first editor snapshot before a timer has run.
    // Consume only genuine queued samples; an empty queue stays visibly empty.
    if (!receivedAudio) updateAnalysis();
    auto frame=spectrumBounds.toFloat();
    g.setColour(juce::Colour{0xff111b1e}); g.fillRoundedRectangle(frame,8);
    g.setColour(edge); g.drawRoundedRectangle(frame.reduced(0.5f),8,1);
    write(g,"SPECTRAL FIELD",spectrumBounds.reduced(14,0).withHeight(30),10,mint);
    const float pitch=processor.detectedPitchHz.load();
    write(g,pitch>20.0f?"PITCH  "+format(pitch,"Hz"):"PITCH  —",spectrumBounds.reduced(14,0).withHeight(30),10,muted,juce::Justification::centredRight);
    const auto plot=spectrumBounds.reduced(40,27).withTrimmedRight(-18).toFloat();
    for (int db=-72;db<=0;db+=24) {
        const float y=plot.getBottom()-plot.getHeight()*static_cast<float>(db+84)/84.0f;
        g.setColour(edge.withAlpha(0.5f)); g.drawHorizontalLine(juce::roundToInt(y),plot.getX(),plot.getRight());
        write(g,juce::String(db),{spectrumBounds.getX()+8,juce::roundToInt(y)-7,27,14},9,muted);
    }
    const double sampleRate=processor.getSampleRate()>0?processor.getSampleRate():48000.0;
    const float maxFrequency=static_cast<float>(juce::jmin(20000.0,sampleRate*0.49));
    for (float hz : {40.0f,100.0f,250.0f,1000.0f,4000.0f,10000.0f}) {
        if (hz>=maxFrequency) continue;
        const float x=plot.getX()+plot.getWidth()*std::log(hz/20.0f)/std::log(maxFrequency/20.0f);
        g.setColour(edge.withAlpha(0.5f)); g.drawVerticalLine(juce::roundToInt(x),plot.getY(),plot.getBottom());
        write(g,hz>=1000?juce::String(hz/1000,0)+"k":juce::String(hz,0),
            {juce::roundToInt(x)-17,spectrumBounds.getBottom()-20,35,14},9,muted,juce::Justification::centred);
    }
    if (!receivedAudio) {
        write(g,"AWAITING AUDIO",plot.toNearestInt(),10,muted,juce::Justification::centred); return;
    }
    juce::Path line;
    for (size_t i=0;i<spectrum.size();++i) {
        const float x=plot.getX()+plot.getWidth()*static_cast<float>(i)/static_cast<float>(spectrum.size()-1);
        const float y=plot.getBottom()-plot.getHeight()*juce::jlimit(0.0f,1.0f,(spectrum[i]+84.0f)/84.0f);
        if (i==0) line.startNewSubPath(x,y); else line.lineTo(x,y);
    }
    auto fill=line; fill.lineTo(plot.getRight(),plot.getBottom()); fill.lineTo(plot.getX(),plot.getBottom()); fill.closeSubPath();
    g.setGradientFill(juce::ColourGradient(mint.withAlpha(0.23f),0,plot.getY(),mint.withAlpha(0.01f),0,plot.getBottom(),false));
    g.fillPath(fill); g.setColour(mint.withAlpha(0.08f)); g.strokePath(line,juce::PathStrokeType(6));
    g.setColour(mint); g.strokePath(line,juce::PathStrokeType(1.4f));
}
void AetherAudioProcessorEditor::paintMeters(juce::Graphics& g,juce::Rectangle<int> area) {
    auto meter=[&](const juce::String& name,float value,juce::Colour colour) {
        auto row=area.removeFromTop(18);
        write(g,name,row.removeFromLeft(22),9,muted);
        auto number=row.removeFromRight(44); write(g,value<=-99?"−∞":juce::String(value,1),number,10,text,juce::Justification::centredRight);
        auto bar=row.reduced(3,6).toFloat(); g.setColour(edge); g.fillRoundedRectangle(bar,1);
        g.setColour(colour); g.fillRoundedRectangle(bar.withWidth(bar.getWidth()*juce::jlimit(0.0f,1.0f,(value+60)/60)),1);
    };
    meter("IN",processor.inputMeterDb.load(),mint); meter("OUT",processor.outputMeterDb.load(),mint);
    auto row=area.removeFromTop(17);
    write(g,"GR  "+juce::String(processor.gainReductionDb.load(),1)+" dB",row,9,amber);
    const float cpu=processor.cpuPercent.load();
    if (cpu>=0) write(g,"CPU  "+juce::String(cpu,1)+"%",area.removeFromTop(14),9,muted);
}
void AetherAudioProcessorEditor::timerCallback() {
    const auto name=processor.currentPresetName();
    if (name!=displayedPreset) { displayedPreset=name; presets.setText(name,juce::dontSendNotification); }
    abButton.setButtonText(processor.isSnapshotB()?"B  /  A":"A  /  B");
    if (!minimal) {
        updateAnalysis();
        repaint(spectrumBounds); repaint(footerBounds);
    }
}
void AetherAudioProcessorEditor::updateAnalysis() {
    if (!minimal) {
        // Drain a full FIFO even at 192 kHz; compute only the newest complete frame.
        std::array<float,16384> samples{};
        const int count=processor.readAnalysis(samples.data(),static_cast<int>(samples.size()));
        bool transformed=false;
        for (int i=0;i<count;++i) {
            fftInput[static_cast<size_t>(fftWrite++)]=samples[static_cast<size_t>(i)];
            if (fftWrite==2048) {
                std::copy(fftInput.begin(),fftInput.end(),fftData.begin());
                fftWrite=0; transformed=true; receivedAudio=true;
            }
        }
        if (transformed) {
            window.multiplyWithWindowingTable(fftData.data(),2048);
            fft.performFrequencyOnlyForwardTransform(fftData.data());
            const double sampleRate=processor.getSampleRate()>0?processor.getSampleRate():48000.0;
            const float maxFrequency=static_cast<float>(juce::jmin(20000.0,sampleRate*0.49));
            for (size_t i=0;i<spectrum.size();++i) {
                const float frequency=20*std::pow(maxFrequency/20.0f,static_cast<float>(i)/static_cast<float>(spectrum.size()-1));
                const float bin=frequency*2048.0f/static_cast<float>(sampleRate);
                const int index=juce::jlimit(0,1022,static_cast<int>(bin));
                const float fraction=bin-static_cast<float>(index);
                const float magnitude=juce::jmap(fraction,fftData[static_cast<size_t>(index)],fftData[static_cast<size_t>(index+1)]);
                const float db=juce::Decibels::gainToDecibels(magnitude/512.0f,-100.0f);
                spectrum[i]=juce::jmax(db,spectrum[i]-2.0f);
            }
        } else if (count==0) { for (auto& value:spectrum) value=juce::jmax(-100.0f,value-1.5f); }
    }
}

