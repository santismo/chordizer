# Chordizer

Chordizer is an open-source pair of Audio Unit plug-ins for Logic Pro that
turn MIDI or audio into a synchronized, editable chord track.

## Components

- **Chordizer** is a MIDI FX plug-in that groups incoming notes into
  PPQ-aligned chord regions.
- **Chordizer Audio** combines live CQT/HPCP analysis with a delayed Basic
  Pitch transcription pass for audio-only chord detection.
- Both components share transport, chord regions, edits, alternate candidates,
  view settings, and lead-sheet data across open instances.

The interface includes timeline and lead-sheet views, host-following playheads,
pinch zoom and two-finger scrolling, chord range selection, chord-name copying,
editable region boundaries, undo/redo, and MIDI drag export.

Chordizer is under active development. Audio transcription is designed for
harmonic material and can still require correction on dense mixes, unusual
voicings, distortion, or weak bass notes.

## Requirements

- macOS 13 or later
- Logic Pro or another host that supports Audio Unit MIDI effects and effects
- Xcode Command Line Tools
- CMake 3.22 or later
- Git

JUCE 8.0.7 is downloaded automatically during configuration. Set `JUCE_DIR`
to use an existing JUCE checkout instead.

## Build

Build universal `arm64` and `x86_64` Audio Units:

```sh
./script/build_chordizer.sh
```

Build and install both components for the current user:

```sh
./script/build_chordizer.sh --install
```

The install target is:

```text
~/Library/Audio/Plug-Ins/Components
```

Restart the host after installation if it does not rescan the components.

## Tests

After configuring or building the project:

```sh
cmake --build ChordTrackerPlugin/build --target ChordizerEngineTests
ChordTrackerPlugin/build/ChordizerEngineTests_artefacts/Release/ChordizerEngineTests

cmake --build ChordTrackerPlugin/build --target ChordizerNeuralSmokeTest
ChordTrackerPlugin/build/ChordizerNeuralSmokeTest_artefacts/Release/ChordizerNeuralSmokeTest
```

Recorded guitar fixtures cover:

- `Ebmaj7, Fm7, Bb7, Ebmaj7`
- `Bm7, F#7, G, A13`

## Repository Layout

- `ChordTrackerPlugin/Source`: plug-in, UI, chord engine, audio analysis, and
  MIDI export source
- `ChordTrackerPlugin/Tests`: engine and embedded-model regression tests
- `ChordTrackerPlugin/ThirdParty`: vendored model/runtime code and licenses
- `script/build_chordizer.sh`: universal build and local install script

## Contributing

See [CONTRIBUTING.md](CONTRIBUTING.md). Bug reports should include the host,
macOS version, component type, project tempo, and a minimal audio or MIDI
example when possible.

## License

Chordizer source is available under the [MIT License](LICENSE). Vendored
dependencies and model assets retain their own licenses; see
[THIRD_PARTY_NOTICES.md](ChordTrackerPlugin/ThirdParty/THIRD_PARTY_NOTICES.md).
