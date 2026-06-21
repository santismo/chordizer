#!/bin/zsh
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
PROJECT="$ROOT/ChordTrackerPlugin"
BUILD="$PROJECT/build"
INSTALL=0
[[ "${1:-}" == "--install" ]] && INSTALL=1

JUCEAIDE="$BUILD/JUCE-build/tools/extras/Build/juceaide/juceaide_artefacts/Custom/juceaide"
[[ -x "$JUCEAIDE" ]] && codesign --force --sign - "$JUCEAIDE" 2>/dev/null || true

JUCE_ARGS=()
for candidate in "${JUCE_DIR:-}" "$ROOT/JUCE" "$ROOT/../JUCE" "$HOME/JUCE" "$HOME/Desktop/JUCE"; do
  if [[ -n "$candidate" && -f "$candidate/CMakeLists.txt" ]]; then
    JUCE_ARGS=(-DJUCE_DIR="$candidate")
    break
  fi
done

cmake -S "$PROJECT" -B "$BUILD" "${JUCE_ARGS[@]}" -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_OSX_ARCHITECTURES="arm64;x86_64"
cmake --build "$BUILD" --config Release --parallel 4

MIDI="$BUILD/ChordizerMidi_artefacts/Release/AU/Chordizer.component"
AUDIO="$BUILD/ChordizerAudio_artefacts/Release/AU/Chordizer Audio.component"
[[ -d "$MIDI" && -d "$AUDIO" ]] || { echo "Built Chordizer components not found" >&2; exit 1; }

if (( INSTALL )); then
  DEST="$HOME/Library/Audio/Plug-Ins/Components"
  mkdir -p "$DEST"
  rm -rf "$DEST/Chord Tracker.component" "$DEST/Chord Tracker Audio.component" \
         "$DEST/Chordizer.component" "$DEST/Chordizer Audio.component"
  ditto "$MIDI" "$DEST/Chordizer.component"
  ditto "$AUDIO" "$DEST/Chordizer Audio.component"
  codesign --force --deep --sign - "$DEST/Chordizer.component"
  codesign --force --deep --sign - "$DEST/Chordizer Audio.component"
  killall -9 AudioComponentRegistrar 2>/dev/null || true
  echo "Installed Chordizer components to $DEST"
fi
