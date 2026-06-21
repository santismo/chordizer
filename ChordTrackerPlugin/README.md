# Chordizer

Logic Pro Audio Unit plug-ins for synchronized chord analysis.

- **Chordizer**: transparent MIDI FX that listens to MIDI regions/live MIDI and records chord regions using host PPQ.
- **Chordizer Audio**: audio effect with immediate constant-Q/HPCP analysis plus a delayed Basic Pitch neural transcription refiner.
- Timeline and lead-sheet views are available in both components.
- MIDI and Audio component instances share transport, chord regions, edits, candidates, and typography through a cross-process mapped session.
- Live HPCP and neural capture use the same allocation-free, phase-safe stereo downmix, preserving chord energy when channels have opposite polarity. Audio is captured into a fixed-capacity real-time-safe ring with interpolating conversion from the host sample rate to 22.05 kHz. Eight-second overlapping neural windows run on a low-priority worker and backfill PPQ-aligned regions without blocking Logic's audio thread.
- Neural analysis copies receive bounded automatic gain for quiet recordings, with peak limiting confined to the analysis buffer; plug-in passthrough is never changed. True silence bypasses inference and clears stale unlocked Audio estimates in the analyzed span.
- Each neural window is decoded at sensitive and strict thresholds. Notes supported by both passes gain confidence; weak unmatched notes are down-weighted before chord naming to reduce false extensions.
- Neural note evidence is fused with sparse CQT/HPCP frames from the same audio window. The acoustic pass can recover missed chord tones and suppress weak chromatic hallucinations, while dense broadband/percussive profiles are rejected.
- Centered short-window CQT/HPCP frames carry spectral-change confidence into sequence decoding, while overlapping longer frames retain low-frequency bass and inversion resolution. When neural note attacks are weak or smeared, a strong harmonic change can still establish and PPQ-align the chord boundary.
- Transition-aware temporal chroma filtering suppresses isolated one-frame excursions that immediately return to the surrounding harmony, reducing drum and pitched-percussion contamination without smoothing sustained chord changes.
- Neural note slices are decoded as a sequence using note attacks and pitch-profile change. Octave doublings are compressed, implausibly weak bass notes are ignored, and isolated harmonic bursts are suppressed without losing short adjacent chords.
- Slash-chord labels are duration-voted across each decoded region, so one bass-tail or transition frame cannot relabel a full root-position chord while sustained inversions remain available.
- Short articulated notes feed an attack-aware decaying harmonic memory. Three-pitch bursts establish or reset the remembered voicing, repeated arpeggio cycles retain quiet roots, and decoded boundaries snap back to the first confirmed attack.
- A brief same-root triad nested between matching seventh/extension labels is folded back into that region; a trailing simplification is folded only when no new multi-pitch attack confirms it as a real change.
- CQT/HPCP fusion is articulation-aware: acoustic frames reinforce arpeggios without erasing chord tones that fall between individual plucks.
- Per-sample PPQ mapping follows tempo changes and tolerates small host corrections at tempo-marker block boundaries while still resetting on real seeks and cycle jumps.
- Transport stops and real PPQ seeks insert capture-segment boundaries. Neural refinement pads and analyzes takes as short as one second, closes final note events at the true PPQ endpoint, and keeps restarted playback in a separate segment.
- Queued stop/seek boundaries move neural status back to pending until the final overlap flush is committed, so hosts and tests cannot mistake an earlier chunk result for the completed take.
- Final partial windows reuse overlap for harmonic context but write only beyond the last refined PPQ, preventing a short tail pass from replacing stable earlier labels.
- Overlapping neural windows reconcile small onset jitter to one PPQ boundary and merge inversion-equivalent labels at analysis seams.
- Window-seam reconciliation also folds a brief same-root decay label into matching extended chords on either side, preventing stop-flush overlap from recreating a removed split.
- Neural backfill replaces only unlocked Audio estimates. MIDI regions and manually edited regions remain authoritative.
- The small header status light reports neural loading, collection, analysis, completion, or failure through the Listen button tooltip.
- Pinch over the slider-free timeline to zoom from `0.01` to `256` visible bars and use two-finger scrolling to move through PPQ time. Command-scroll is a zoom fallback.
- The compact header uses icon-only controls for view switching, full-width lead-sheet measures, edit mode, view-specific text size, listening, clearing, copying, undo, and redo. The one-measure control appears only in Lead Sheet view.
- In Lead Sheet view, pinch in for full-width measures or pinch out for the four-column overview.
- Timeline follows host playhead changes slightly left of center during playback and stopped scrubbing.
- Timeline and Lead Sheet automatically restore their own last window dimensions.
- Chord regions use a 20-color palette with guaranteed adjacent contrast, inset boundaries, and responsive chord-name typography.
- MIDI note additions refine the active chord until a note release arms the next chord region, with damper-pedal-aware extension.
- Brief legato overlaps remain provisional, allowing common-tone chord changes to start at the replacement-note attack even when old notes release slightly later.
- Harmonic context selects sharp or flat chord spelling where MIDI pitch classes allow it.
- In Edit mode, click a region to open its chord-anchored confidence/alternatives menu. Choose Edit name for inline renaming; clicking the selected region again closes editing.
- In Edit mode, drag either region edge to resize without overlapping adjacent chords. Right-click fills an existing blank gap to the next chord and does nothing on the final chord.
- In normal mode, select one chord or a range and drag it into Logic to create a named Standard MIDI File. Chord starts, gaps, durations, tempo, time signature, slash basses, and extended voicings are preserved at 960 PPQ.
- Timeline and Lead Sheet text scales persist separately and update across open instances.
- Timeline zoom, scroll position, view mode, and both view sizes are stored in plug-in state.

Build and install from the repository root with `./script/build_chordizer.sh --install`.
Run the focused engine checks with `cmake --build build --target ChordizerEngineTests && build/ChordizerEngineTests_artefacts/Release/ChordizerEngineTests`.
Run the embedded-model check with `cmake --build build --target ChordizerNeuralSmokeTest && build/ChordizerNeuralSmokeTest_artefacts/Release/ChordizerNeuralSmokeTest`.

Third-party model and runtime notices are in `ThirdParty/THIRD_PARTY_NOTICES.md`.
