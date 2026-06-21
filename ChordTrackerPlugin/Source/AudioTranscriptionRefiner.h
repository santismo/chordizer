#pragma once

#include <JuceHeader.h>
#include <memory>

class AudioTranscriptionRefiner
{
public:
    enum class Status { stopped, loading, collecting, analyzing, refined, error };

    AudioTranscriptionRefiner();
    ~AudioTranscriptionRefiner();

    void prepare(double sourceSampleRate);
    void release();
    void setTransportPlaying(bool playing) noexcept;
    void pushAudio(const juce::AudioBuffer<float>& buffer,double blockStartPpq,double bpm) noexcept;
    Status status() const noexcept;
    juce::String statusText() const;

private:
    class Impl;
    std::unique_ptr<Impl> impl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioTranscriptionRefiner)
};
