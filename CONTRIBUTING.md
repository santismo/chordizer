# Contributing to Chordizer

Contributions and focused bug reports are welcome.

## Development Setup

1. Install Xcode Command Line Tools and CMake 3.22 or later.
2. Clone the repository.
3. Run `./script/build_chordizer.sh`.
4. Run the engine and neural smoke tests described in `README.md`.

Set `JUCE_DIR` before building to use a local JUCE 8.0.7 checkout. Otherwise,
CMake downloads the pinned JUCE release.

## Pull Requests

- Keep changes focused and explain user-visible behavior.
- Add regression coverage for chord naming, region timing, transport, or audio
  analysis changes.
- Run both test executables before opening a pull request.
- Do not commit build directories, installed components, debug recordings, or
  project-specific Logic files.

## Audio Detection Reports

Useful reports include:

- the expected chord sequence and approximate boundaries
- the detected sequence
- sample rate, tempo, and time signature
- instrument and playing style
- a short, redistributable WAV fixture when licensing permits

By contributing code, you agree that your contribution is licensed under the
repository's MIT License. Test audio remains subject to any license stated with
the fixture.
