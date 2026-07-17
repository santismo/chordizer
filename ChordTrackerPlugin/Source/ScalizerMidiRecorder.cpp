#include "ScalizerMidiRecorder.h"
#include <cstring>

void ScalizerMidiRecorder::start(double bpm,int numerator,int denominator) noexcept
{
    recording.store(false,std::memory_order_release);
    count.store(0,std::memory_order_release);
    overflowed.store(false,std::memory_order_release);
    firstPpq.store(-1.0,std::memory_order_release);
    lastPpq.store(-1.0,std::memory_order_release);
    takeEndPpq.store(-1.0,std::memory_order_release);
    takeBpm.store(juce::jlimit(1.0,999.0,bpm),std::memory_order_release);
    takeNumerator.store(juce::jmax(1,numerator),std::memory_order_release);
    takeDenominator.store(juce::jmax(1,denominator),std::memory_order_release);
    recording.store(true,std::memory_order_release);
}
void ScalizerMidiRecorder::stop(double endPpq) noexcept
{
    recording.store(false,std::memory_order_release);
    const auto last=lastPpq.load(std::memory_order_acquire);
    takeEndPpq.store(last<0.0?endPpq:juce::jmax(last,endPpq),std::memory_order_release);
}

void ScalizerMidiRecorder::clear() noexcept
{
    recording.store(false,std::memory_order_release);
    count.store(0,std::memory_order_release);
    overflowed.store(false,std::memory_order_release);
    firstPpq.store(-1.0,std::memory_order_release);
    lastPpq.store(-1.0,std::memory_order_release);
    takeEndPpq.store(-1.0,std::memory_order_release);
}

bool ScalizerMidiRecorder::shouldCapture(const juce::MidiMessage& message) noexcept
{
    return message.isNoteOnOrOff()||message.isController()||message.isPitchWheel()
        ||message.isAftertouch()||message.isChannelPressure()||message.isProgramChange();
}

void ScalizerMidiRecorder::process(const juce::MidiBuffer& processedMidi,double blockStartPpq,double bpm,
                                   double sampleRate,bool transportPlaying,int numerator,int denominator) noexcept
{
    if(!recording.load(std::memory_order_acquire)||!transportPlaying||sampleRate<=0.0)return;
    for(const auto metadata:processedMidi)
    {
        if(!recording.load(std::memory_order_relaxed))break;
        const auto message=metadata.getMessage();
        if(!shouldCapture(message)||message.getRawDataSize()<1||message.getRawDataSize()>3)continue;
        const auto eventPpq=blockStartPpq+metadata.samplePosition/sampleRate*bpm/60.0;
        const auto previous=lastPpq.load(std::memory_order_relaxed);
        if(previous>=0.0&&eventPpq<previous-0.0625)
        {
            // A seek/cycle jump ends the current take rather than folding later
            // events backward over material already captured.
            stop(previous);
            break;
        }
        const auto index=count.load(std::memory_order_relaxed);
        if(index>=maximumEvents)
        {
            overflowed.store(true,std::memory_order_release);
            stop(previous);
            break;
        }
        if(index==0)
        {
            firstPpq.store(eventPpq,std::memory_order_relaxed);
            takeBpm.store(juce::jlimit(1.0,999.0,bpm),std::memory_order_relaxed);
            takeNumerator.store(juce::jmax(1,numerator),std::memory_order_relaxed);
            takeDenominator.store(juce::jmax(1,denominator),std::memory_order_relaxed);
        }
        ScalizerRecordedMidiEvent event;
        event.ppq=eventPpq;event.size=(juce::uint8)message.getRawDataSize();
        std::memcpy(event.data.data(),message.getRawData(),event.size);
        events[index]=event;
        lastPpq.store(eventPpq,std::memory_order_relaxed);
        takeEndPpq.store(eventPpq,std::memory_order_relaxed);
        count.store(index+1,std::memory_order_release);
    }
}

ScalizerRecordingTake ScalizerMidiRecorder::snapshot() const
{
    ScalizerRecordingTake take;
    if(isRecording())return take;
    const auto available=juce::jmin(eventCount(),maximumEvents);
    take.events.assign(events.begin(),events.begin()+(std::ptrdiff_t)available);
    take.firstPpq=firstPpq.load(std::memory_order_acquire);
    take.endPpq=takeEndPpq.load(std::memory_order_acquire);
    take.bpm=takeBpm.load(std::memory_order_acquire);
    take.numerator=takeNumerator.load(std::memory_order_acquire);
    take.denominator=takeDenominator.load(std::memory_order_acquire);
    take.overflowed=overflowed.load(std::memory_order_acquire);
    return take;
}
