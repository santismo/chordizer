#pragma once

#include <JuceHeader.h>
#include <array>
#include <atomic>
#include <vector>

struct ScalizerRecordedMidiEvent
{
    double ppq = 0.0;
    std::array<juce::uint8,3> data {};
    juce::uint8 size = 0;
};
struct ScalizerRecordingTake
{
    std::vector<ScalizerRecordedMidiEvent> events;
    double firstPpq = -1.0, endPpq = -1.0, bpm = 120.0;
    int numerator = 4, denominator = 4;
    bool overflowed = false;

    bool isEmpty() const noexcept { return events.empty(); }
};

class ScalizerMidiRecorder
{
public:
    static constexpr size_t maximumEvents = 65536;

    void start(double bpm,int numerator,int denominator) noexcept;
    void stop(double endPpq) noexcept;
    void clear() noexcept;
    void process(const juce::MidiBuffer& processedMidi,double blockStartPpq,double bpm,
                 double sampleRate,bool transportPlaying,int numerator,int denominator) noexcept;
    bool isRecording() const noexcept { return recording.load(std::memory_order_acquire); }
    size_t eventCount() const noexcept { return count.load(std::memory_order_acquire); }
    bool hasTake() const noexcept { return eventCount()>0; }
    bool didOverflow() const noexcept { return overflowed.load(std::memory_order_acquire); }
    ScalizerRecordingTake snapshot() const;

private:
    static bool shouldCapture(const juce::MidiMessage&) noexcept;

    std::array<ScalizerRecordedMidiEvent,maximumEvents> events {};
    std::atomic<size_t> count { 0 };
    std::atomic<bool> recording { false },overflowed { false };
    std::atomic<double> firstPpq { -1.0 },lastPpq { -1.0 },takeEndPpq { -1.0 },takeBpm { 120.0 };
    std::atomic<int> takeNumerator { 4 },takeDenominator { 4 };
};
