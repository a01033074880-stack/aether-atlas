# Third-party software notices

Copyright 2026 Aether Micro contributors. The original standalone DSP code under `libs/aether-dsp` is MIT. The complete JUCE-linked application is distributed under AGPL-3.0-or-later; the full license is in `LICENSE`.

## JUCE

JUCE **8.0.12**, source revision `501c07674e1ad693085a7e7c398f205c2677f5da`, https://github.com/juce-framework/JUCE/tree/8.0.12.
Copyright Raw Material Software Limited. JUCE modules are dual licensed AGPLv3/commercial. This release uses the AGPLv3 option. No separate commercial JUCE license is conveyed. JUCE source and its original notices are included in the source dependency archive alongside the release. Do not use this project's MIT DSP license as a license for the JUCE application.

JUCE incorporates code under additional permissive licenses, including VST3 SDK (Steinberg), FreeType, HarfBuzz, libpng, zlib, SheenBidi, FLAC, Ogg/Vorbis and platform-specific components. The complete notices from the actual JUCE source tree are retained in `licenses/JUCE-LICENSE.md` and the included JUCE source archive. The VST3 SDK license from this checkout is included as `licenses/VST3-SDK-LICENSE.txt`. JUCE's disabled browser/cURL/ASIO modules are not enabled as optional runtime features.

## Development tools

MSVC, Windows SDK, CMake, Ninja and pluginval were used for build/validation. Their executables are not part of the plugin release. pluginval: https://github.com/Tracktion/pluginval. No source or assets from commercial audio plugins, NAM, DDSP, Airwindows, RTNeural, chowdsp_wdf or other algorithms discussed in the research report were copied into this project. The implemented DSP is original code based on general signal-processing techniques.
