#pragma once
#include <JuceHeader.h>
#include <array>
#include <mutex>
#include <vector>

struct ChordRegionData
{
    double startPpq = 0.0, endPpq = 0.0;
    juce::String name, source;
    float confidence = 1.0f;
    juce::StringArray alternatives;
    bool locked = false;
};

struct PitchedNoteRegion
{
    double startPpq = 0.0, endPpq = 0.0;
    int midiNote = 60;
    float confidence = 1.0f;
};

struct HarmonicFrameEvidence
{
    double startPpq = 0.0, endPpq = 0.0;
    std::array<float, 12> pitchWeights {};
    int bassPitchClass = -1;
    float confidence = 1.0f;
    float changeConfidence = 0.0f;
};

struct PhaseSafeDownmixPlan
{
    int channels = 1;
    int dominantChannel = 0;
    bool weightedStereo = false;
};

struct ChordSessionSnapshot
{
    std::vector<ChordRegionData> regions;
    double playheadPpq = 0.0, bpm = 120.0;
    int numerator = 4, denominator = 4, instanceCount = 0;
    bool playing = false;
    uint64_t revision = 0;
    float timelineTextScale = 1.0f, leadSheetTextScale = 1.0f;
};

enum class ChordUpdateKind { none, extend, refine, start };

struct MidiChordUpdate
{
    juce::String chord;
    ChordUpdateKind kind = ChordUpdateKind::none;
    float confidence = 0.0f;
    juce::StringArray alternatives;
    double regionStartPpq = -1.0;
};

class SharedChordSession
{
public:
    SharedChordSession();
    ~SharedChordSession();
    static SharedChordSession& instance();
    uint64_t registerInstance();
    void unregisterInstance(uint64_t id);
    void publishChord(double ppq, const juce::String& chord, const juce::String& source,
                      float confidence, ChordUpdateKind kind, const juce::StringArray& alternatives = {});
    void renameRegion(size_t index, const juce::String& name);
    void deleteRegion(size_t index);
    bool resizeRegion(size_t index, double startPpq, double endPpq);
    bool extendRegionToNext(size_t index);
    void replaceRegions(const std::vector<ChordRegionData>& replacement);
    void replaceAudioRegions(double startPpq, double endPpq,
                             const std::vector<ChordRegionData>& replacement);
    void setTextScale(bool leadSheet, float scale);
    void updateTransport(double ppq, double bpm, int numerator, int denominator, bool playing);
    ChordSessionSnapshot snapshot() const;
    void clear();
private:
    mutable std::mutex mutex;
    std::vector<ChordRegionData> regions;
    double playhead = 0.0, tempo = 120.0;
    int timeNumerator = 4, timeDenominator = 4, instances = 0;
    bool isPlaying = false;
    uint64_t nextID = 1, revision = 1;
    float timelineTextScale = 1.0f, leadTextScale = 1.0f;
    void* sharedMemory = nullptr;
    int sharedFile = -1;
};

class MidiChordDetector
{
public:
    void reset();
    MidiChordUpdate process(const juce::MidiBuffer& midi,double ppq=0.0);
private:
    std::array<bool, 128> heldNotes {}, sustainedNotes {};
    std::array<float, 12> keyWeights {};
    bool waitingForAttack = true;
    bool pendingRegionStart = false;
    bool provisionalAddition = false;
    bool sustainDown = false;
    double pendingStartPpq = -1.0, provisionalStartPpq = -1.0;
    juce::String currentChord;
    float currentConfidence = 0.0f;
    juce::StringArray currentAlternatives;
};

class AudioChordStabilizer
{
public:
    void reset();
    MidiChordUpdate process(const juce::String& chord,float confidence,const juce::StringArray& alternatives,
                            double ppq,bool onset=true);
private:
    juce::String currentChord,pendingChord;
    float currentConfidence=0.0f,pendingConfidence=0.0f;
    juce::StringArray currentAlternatives,pendingAlternatives;
    double pendingStartPpq=-1.0;
    int pendingObservations=0;
};

juce::String identifyChord(const std::array<float, 12>& pitchWeights, float& confidence,
                           int bassPitchClass = -1, juce::StringArray* alternatives = nullptr,
                           const std::array<float, 12>* keyContext = nullptr,
                           float additionalComplexityPenalty = 0.0f);
std::array<float,12> calculateConstantQHpcp(const float* samples,const float* window,int frameSize,double sampleRate,
                                            int* bassPitchClass = nullptr);
void stabilizeHarmonicFrames(std::vector<HarmonicFrameEvidence>& frames);
PhaseSafeDownmixPlan createPhaseSafeDownmixPlan(const juce::AudioBuffer<float>& buffer) noexcept;
float phaseSafeDownmixSample(const juce::AudioBuffer<float>& buffer,int sample,
                             const PhaseSafeDownmixPlan& plan) noexcept;
std::vector<ChordRegionData> createChordRegionsFromNotes(const std::vector<PitchedNoteRegion>& notes,
                                                         double startPpq, double endPpq,
                                                         double slicePpq = 0.125,
                                                         const std::vector<HarmonicFrameEvidence>* harmonicFrames = nullptr);
std::vector<PitchedNoteRegion> createConsensusNotes(const std::vector<PitchedNoteRegion>& sensitive,
                                                    const std::vector<PitchedNoteRegion>& strict);
