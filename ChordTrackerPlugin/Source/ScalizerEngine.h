#pragma once

#include <JuceHeader.h>
#include "ChordEngine.h"
#include <array>
#include <vector>

enum class ScalizerLockMode { chordTones, scale };

struct ScalizerHarmonyVoice
{
    int degree = 0; // 0 = off, 2..8 = diatonic interval
    bool above = true;
};

struct ScalizerScaleChoice
{
    int rootPitchClass = 0;
    juce::String name;
    std::array<bool, 12> scalePitchClasses {};
    std::array<bool, 12> chordPitchClasses {};
    std::array<int, 8> orderedScalePitchClasses {};
    int scaleSize = 0;
    bool valid = false;
};

ScalizerScaleChoice inferScalizerScale(const std::vector<ChordRegionData>& regions, size_t regionIndex);
std::vector<ScalizerScaleChoice> inferScalizerScales(const std::vector<ChordRegionData>& regions);
juce::StringArray scalizerScaleTypeNames();
juce::String makeScalizerScaleName(int rootPitchClass, int scaleTypeIndex, bool preferSharps);

class ScalizerEngine
{
public:
    void reset() noexcept;
    void releaseAll(juce::MidiBuffer& output, int samplePosition) noexcept;
    void process(juce::MidiBuffer& midi,
                 const std::vector<ChordRegionData>& regions,
                 double blockStartPpq,
                 double bpm,
                 double sampleRate,
                 ScalizerLockMode lockMode,
                 const std::array<ScalizerHarmonyVoice, 3>& voices);

private:
    struct ActiveMapping
    {
        std::array<int, 4> outputNotes {};
        int count = 0;
    };

    static int nearestAllowedNote(int note, const std::array<bool, 12>& allowed) noexcept;
    static int degreeMappedNote(int inputNote, const ScalizerScaleChoice& choice,
                                const std::array<bool, 12>& allowed) noexcept;
    static int diatonicHarmonyNote(int note, int degree, bool above,
                                   const ScalizerScaleChoice& choice,
                                   const std::array<bool, 12>& allowed) noexcept;
    void addOutputNote(juce::MidiBuffer& output, int channel, int note, float velocity,
                       int samplePosition, ActiveMapping& mapping) noexcept;
    void releaseMapping(juce::MidiBuffer& output, int channel, ActiveMapping& mapping,
                        float velocity, int samplePosition) noexcept;

    std::array<std::array<ActiveMapping, 128>, 16> activeMappings {};
    std::array<std::array<unsigned short, 128>, 16> outputRefCounts {};
};
