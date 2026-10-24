# Atlas v2 shared contracts

Workspace: the repository root (all paths in this document are repository-relative). Version 2.0.1. These interfaces permit independent work. Read publicheaders Modular.h/FmSynth.h for exact C++ fields; do not change them without coordinating controller.

## Host parameters

Legacy first39 IDs remain. NewIDs (1-based slot/operator/row indices):

```
graph.enabled bool; routing.algorithm choice0..31 plus32Custom; graph.feedback 0..0.95; graph.trim -24..12dB
node.1.type choice0..20 (ModuleType enum), node.1.on bool, node.1.mix0..1,
node.1.level -24..12dB, node.1.pan -1..1, node.1.a/b/c/d0..1,
node.1.input -1..1, node.1.output -1..1; repeat1..12
route.1.2 -1..1 means source1 -> destination2; all12x12 present including self
lfo.1.rate0.02..30, lfo.1.phase0..1, lfo.1.shape choiceSine/Triangle/Saw/Pulse/Random,
lfo.1.sync bool; repeat1..4
modulation.1.source choice0..9; .target0..84; .depth -1..1; repeat1..16
fm.enabled bool, fm.algorithm choice0..31, fm.custom bool,
fm.index0..12, fm.morph0..1, fm.feedback0..0.95,
fm.attack0.001..5s, fm.decay0.01..5s, fm.sustain0..1, fm.release0.01..12s,
fm.gain0..1, fm.spread0..1
fm.op.1.ratio0.125..16, .level0..1, .carrier0..1, .detune -100..100cents,
.attack0.1..4 multiplier, .release0.1..4 multiplier; repeat1..6
fm.edge.1.2 0..1 means sourceoperator1 modulates destination2; all6x6 present
```

Target0=None. Target1..84 maps `slot=(target-1)/7`, `attribute=(target-1)%7`: A/B/C/D/Mix/Level/Pan. Sources0None,1..4LFOs,5Envelope,6Velocity,7Pitch,8RandomHold,9Chaos. Sourcelevels use bipolar-1..1 except naturallyunipolar envelopes/performance. Module macro normalized fields map linearly to ModuleInfo physicalranges; defaultValue is NORMALIZED. Module metadatamust document musical ranges accurately. Editor shows macrophysicalunits and help.

Routing built-ins: host passes matrices as storedparameters; whenrouting.algorithm !=32 GraphEngine can apply selectedtemplate in host's conversion, while Custom32 usesstoredmatrix. UI calls applyRoutingAlgorithm to copytemplate intoallroute/input/outputparametersand selectalgorithm. On firstmanualedge/input/outputedit, switchrouting.algorithm32 toensurecustomvaluesareused. Slot typemetadata/defaults assigned via assignModule helper.

FM built-ins: Synth::setParameters uses algorithmdefinition whencustom=false, otherwise p.matrix plusoperatorcarrierfields. Operatorratio/level/detune/envelopescale always apply. Inbuiltmode algorithmcarriers are multipliedbyoperatorcarrier (defaultall1). Custom mode usesoperatorcarrier directly. Morph interpolates current/nextinbuiltalgorithm; custommode morph may blendcustomwith selectedtemplate, documentedexactly. Diagonalfeedback combinesmatrixselfconnection withglobalfeedback safely. UI editsmatrix byfirstcopyingactualcurrentdefinition intohostparameters thenfm.custom=true.

## Processor UI API (controller supplies)

```
existing state, inputMeterDb/outputMeterDb/gainReductionDb/detectedPitchHz/analysisEnabled/readAnalysis,
factoryPresetNames/loadFactoryPreset/currentPresetName/savePreset/loadPreset/toggleAB/isSnapshotB/requestPanic
float parameterValue(const juce::String& id) const;
void setParameterValue(const juce::String& id,float physicalValue);
void applyRoutingAlgorithm(int index);
void setGraphEdge(int sourceZeroBased,int destinationZeroBased,float gain);
void assignModule(int slotZeroBased,int moduleType);
void applyFmAlgorithm(int index);
void setFmEdge(int sourceZeroBased,int destinationZeroBased,float depth);
std::array<std::atomic<float>,12> nodeMeters;
std::array<std::atomic<float>,10> modulationMeters;
std::atomic<int> fmVoiceCount{0};
```

Existing AetherAudioProcessorEditor classname/constructor and publicsetMinimalMode(bool) remain. Add public `setWorkspace(int)` 0Patch1FM2Motion, `selectSlot(int)`, and `bool performUiAction(const juce::String&)` for actual interaction smoke tests. Actions should operate productioncontrols (e.g. browserselect/routeclick/FМedit), nottest-only DSPstate bypass. Controller testcantraversecomponents bynamesinstead; agreeexactcontract beforeaddingunnecessaryAPIs. Child components may liveSource/editor/, but controllermustaddnew.cpp sources.

UI usesParameterSpec withjuce::String id/name/unit/group andsamefloatrange/default/skew/choices/booleanfields; no long-livedconstchar* fromtemporarystrings. Allparams accessiblethroughscrollableMinimal view. Fetchedfonts assets/fonts/InstrumentSerif-Regular.ttf + DMMono-Regular.ttf compiled to BinaryData target AetherAssets; includeBinaryData.h anduseappropriatearray names (CMakejuce_add_binary_data). Sharedassetshandledcontroller; UIownsnewartboardassetonly.

## DSP tests/build

GraphEngine supports distinctL/Rpointers, nullablemono sidechain, arbitrarysamples,8..192kHz. Allbufferspreparedbeforeprocess. Sources/FM matricesandslots sanitized; resetdeterministic. Node/Meters getters areaudio-owned; wrapperpublishesatomiccopies. Onlyconstructor/prepareallocate. SignalsmithStretch requiresSignalsmithLinear headers; DaisySPselected.cpp sources linkedbycontroller. Include paths third_party/signalsmith-stretch, third_party/signalsmith-linear, third_party/DaisySP/Source, third_party/chowdsp_wdf/include. Do not enableDaisySP LGPLparts.

ControllerbuildsthroughMSVCvcvars64+Ninja. Availabledirectcompiler MinGW pathC:/rtools45/x86_64-w64-mingw32.static.posix/bin/g++.exe forpureC++tests. MSVCatC:/SDKs/AetherBuildTools. Do notwrite/reconfigurecontrollerCMakefromagents; reportdesiredadditionalsourcepaths. Noagentmaymodifyotheragentfilesorspawnnestedagents.
