# -*- coding: utf-8 -*-
from pathlib import Path
p = Path(r"C:\Users\24168\Documents\Projects\wt-integration-210\tests\FixtureTests.cpp")
text = p.read_text(encoding="utf-8")

def must_replace(old, new, label):
    global text
    if old not in text:
        raise SystemExit(f"MISSING: {label}")
    text = text.replace(old, new, 1)
    print("replaced:", label)

must_replace(
    "// Parameter values the sample file itself contains (preset: 39 legacy ids; state: all ids).",
    "// Parameter values the sample file itself contains (preset: 39 legacy ids; state: legacyParameterCount ids from schema 2).",
    "comment expectedValues",
)
must_replace(
    'if(!s.isPreset) require(compared==static_cast<size_t>(aether::totalParameterCount),"state fixture does not carry every parameter: "+s.id.toStdString());',
    'if(!s.isPreset) require(compared==static_cast<size_t>(aether::legacyParameterCount),"state fixture does not carry every legacy parameter: "+s.id.toStdString());',
    "compared==legacyParameterCount",
)
must_replace(
    'require(xml!=nullptr && xml->getIntAttribute("schema")==2,"re-saved state is not schema 2: "+s.id.toStdString());',
    'require(xml!=nullptr && xml->getIntAttribute("schema")==aether::stateSchema,"re-saved state is not schema "+std::to_string(aether::stateSchema)+": "+s.id.toStdString());',
    "resave schema==stateSchema",
)
must_replace(
    "for(const int schema:{999,0,3,-1}) {",
    "for(const int schema:{999,0,4,-1}) {",
    "reject schema 4 not 3",
)
hdr_old = "// summary. Purpose: a future schema 3 migration must not change old sounds."
hdr_new = "// summary. Purpose: schema 1/2 fixtures must load, upgrade to schema 3, and keep legacy sound path."
if hdr_old in text:
    text = text.replace(hdr_old, hdr_new, 1)
    print("replaced: header purpose")

marker = '        test("fixture renders are finite and audible",[&] {'
if "archived fixtures upgrade to schema 3" in text:
    raise SystemExit("upgrade test already present")
if marker not in text:
    raise SystemExit("render marker missing")

upgrade = r'''        test("archived fixtures upgrade to schema 3 with source-layer defaults",[&] {
            auto sourceAtDefaults=[](AetherAudioProcessor& p) {
                for(int i=aether::sourceOffset;i<aether::totalParameterCount;++i) {
                    const auto& spec=aether::parameterSpecs()[static_cast<size_t>(i)];
                    require(std::abs(p.parameterValue(spec.id)-spec.defaultValue)<1.0e-4f,
                            "source param not default after upgrade: "+spec.id.toStdString());
                }
            };
            auto savedSchema=[&](AetherAudioProcessor& p) {
                juce::MemoryBlock saved; p.getStateInformation(saved);
                const auto xml=juce::AudioProcessor::getXmlFromBinary(saved.getData(),static_cast<int>(saved.getSize()));
                require(xml!=nullptr,"upgrade save produced no XML");
                return xml->getIntAttribute("schema");
            };
            for(const auto& s:samples) if(s.isPreset) {
                AetherAudioProcessor p; p.prepareToPlay(kRate,kGoldenBlock);
                require(p.loadPreset(s.file).wasOk(),"preset upgrade load failed: "+s.id.toStdString());
                require(p.parameterValue("graph.enabled")==0,"schema1 upgrade left graph on: "+s.id.toStdString());
                require(p.parameterValue("fm.enabled")==0,"schema1 upgrade left FM on: "+s.id.toStdString());
                sourceAtDefaults(p);
                require(savedSchema(p)==aether::stateSchema,"preset did not re-save as schema 3: "+s.id.toStdString());
            }
            for(const auto& s:samples) if(!s.isPreset) {
                AetherAudioProcessor p; p.prepareToPlay(kRate,kGoldenBlock);
                // Dirty source params so a failed upgrade would leave them non-default.
                p.setParameterValue("source.engine",2); p.setParameterValue("source.enabled",1);
                p.setParameterValue("source.seed",1234); p.setParameterValue("source.pg.sequency",40);
                require(loadSample(p,s).isEmpty(),"state upgrade load failed: "+s.id.toStdString());
                sourceAtDefaults(p);
                require(savedSchema(p)==aether::stateSchema,"state did not re-save as schema 3: "+s.id.toStdString());
                // Legacy values from the fixture must still apply.
                const auto values=expectedValues(s);
                for(const auto& spec:aether::parameterSpecs()) {
                    if(spec.id.startsWith("source.")) continue;
                    const auto it=values.find(spec.id); if(it==values.end()) continue;
                    const double want=juce::jlimit(static_cast<double>(spec.minimum),static_cast<double>(spec.maximum),it->second);
                    require(std::abs(p.parameterValue(spec.id)-want)<1.0e-3*std::max(1.0,std::abs(want)),
                            "legacy param lost on schema2->3: "+s.id.toStdString()+" "+spec.id.toStdString());
                }
            }
        });
        test("schema 1->3 and 2->3 synthetic upgrades round-trip",[] {
            for(const int schema:{1,2}) {
                AetherAudioProcessor dirty; dirty.setParameterValue("source.engine",1); dirty.setParameterValue("source.enabled",1);
                dirty.setParameterValue("colour.drive",0.42f);
                juce::ValueTree old("AetherState"); old.setProperty("schema",schema,nullptr); old.setProperty("version","1.0.0",nullptr);
                const int count=schema==1?aether::parameterCount:aether::legacyParameterCount;
                for(int i=0;i<count;++i) {
                    const auto& spec=aether::parameterSpecs()[static_cast<size_t>(i)];
                    float value=spec.defaultValue;
                    if(spec.id=="colour.drive") value=0.42f;
                    juce::ValueTree item("PARAM"); item.setProperty("id",spec.id,nullptr); item.setProperty("value",value,nullptr); old.addChild(item,-1,nullptr);
                }
                juce::MemoryBlock block; auto xml=old.createXml(); juce::AudioProcessor::copyXmlToBinary(*xml,block);
                dirty.setStateInformation(block.getData(),static_cast<int>(block.getSize()));
                require(std::abs(dirty.parameterValue("colour.drive")-0.42f)<1.0e-4f,"drive lost on schema "+std::to_string(schema));
                if(schema==1) {
                    require(dirty.parameterValue("graph.enabled")==0,"graph not forced off on schema1");
                    require(dirty.parameterValue("fm.enabled")==0,"fm not forced off on schema1");
                }
                for(int i=aether::sourceOffset;i<aether::totalParameterCount;++i) {
                    const auto& spec=aether::parameterSpecs()[static_cast<size_t>(i)];
                    require(std::abs(dirty.parameterValue(spec.id)-spec.defaultValue)<1.0e-4f,
                            "source not default after schema "+std::to_string(schema)+" upgrade: "+spec.id.toStdString());
                }
                juce::MemoryBlock saved; dirty.getStateInformation(saved);
                const auto out=juce::AudioProcessor::getXmlFromBinary(saved.getData(),static_cast<int>(saved.getSize()));
                require(out!=nullptr && out->getIntAttribute("schema")==aether::stateSchema,"re-save schema after "+std::to_string(schema));
                AetherAudioProcessor again; again.setStateInformation(saved.getData(),static_cast<int>(saved.getSize()));
                require(std::abs(again.parameterValue("colour.drive")-0.42f)<1.0e-4f,"schema3 round-trip lost drive");
            }
        });
        test("schema 3 payloads are accepted",[] {
            AetherAudioProcessor p; p.setParameterValue("colour.drive",17);
            juce::ValueTree tree("AetherState"); tree.setProperty("schema",3,nullptr); tree.setProperty("version","2.1.0",nullptr);
            juce::ValueTree item("PARAM"); item.setProperty("id","colour.drive",nullptr); item.setProperty("value",3.0f,nullptr); tree.addChild(item,-1,nullptr);
            juce::MemoryBlock data; auto xml=tree.createXml(); juce::AudioProcessor::copyXmlToBinary(*xml,data);
            p.setStateInformation(data.getData(),static_cast<int>(data.getSize()));
            require(std::abs(p.parameterValue("colour.drive")-3.0f)<1.0e-4f,"schema 3 state was rejected");
        });
'''

text = text.replace(marker, upgrade + marker, 1)
p.write_text(text, encoding="utf-8", newline="\n")
print("OK bytes", p.stat().st_size)
print("has upgrade", "archived fixtures upgrade to schema 3" in text)
print("has synthetic", "schema 1->3 and 2->3 synthetic" in text)
print("has accept3", "schema 3 payloads are accepted" in text)
