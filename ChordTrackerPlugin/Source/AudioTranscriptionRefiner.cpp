#include "AudioTranscriptionRefiner.h"
#include "ChordEngine.h"
#include "BasicPitch.h"
#include <atomic>
#include <cmath>
#include <numeric>

namespace
{
constexpr double modelSampleRate=22050.0;
constexpr size_t ringCapacity=1u<<18;
constexpr size_t ringMask=ringCapacity-1;
constexpr size_t chunkSamples=(size_t)(modelSampleRate*8.0);
constexpr size_t hopSamples=(size_t)(modelSampleRate*6.0);
constexpr size_t boundaryChunkSamples=(size_t)(modelSampleRate*6.0);
constexpr size_t boundaryHopSamples=(size_t)(modelSampleRate*4.0);
constexpr size_t minimumFlushSamples=(size_t)(modelSampleRate*1.0);

struct CapturedSample
{
    float audio=0.0f;
    double ppq=0.0;
    bool segmentBoundary=false;
};
}

class AudioTranscriptionRefiner::Impl final : private juce::Thread
{
public:
    Impl():Thread("Chordizer neural transcription")
    {
        ring=std::make_unique<CapturedSample[]>(ringCapacity);
        captured.reserve(chunkSamples+hopSamples);
    }

    ~Impl() override { release(); }

    void prepare(double rate)
    {
        release();
        sourceRate=rate>0.0?rate:44100.0;
        const auto cutoff=juce::jmin(9000.0,juce::jmin(sourceRate,modelSampleRate)*0.45);
        lowPassCoefficient=(float)(1.0-std::exp(-2.0*juce::MathConstants<double>::pi*cutoff/sourceRate));
        inputPosition=0;nextOutputPosition=0.0;lowPass=previousLowPass=0.0f;previousPpq=0.0;
        lastRefinedEndPpq=-1000.0;
        hasPreviousInput=false;readPosition.store(0);writePosition.store(0);captured.clear();
        transportPlaying.store(false);
        currentStatus.store(Status::loading);
        startThread(juce::Thread::Priority::low);
    }

    void release()
    {
        signalThreadShouldExit();
        stopThread(10000);
        currentStatus.store(Status::stopped);
    }

    void setPlaying(bool playing) noexcept
    {
        const auto wasPlaying=transportPlaying.exchange(playing,std::memory_order_acq_rel);
        if(playing&&!wasPlaying)currentStatus.store(Status::collecting);
        if(!playing&&wasPlaying)
        {
            auto write=writePosition.load(std::memory_order_relaxed);
            const auto read=readPosition.load(std::memory_order_acquire);
            if(write-read<ringCapacity)
            {
                ring[(size_t)write&ringMask]={0.0f,0.0,true};
                writePosition.store(++write,std::memory_order_release);
                currentStatus.store(Status::collecting);
                notify();
            }
            else droppedSamples.fetch_add(1,std::memory_order_relaxed);
        }
    }

    void push(const juce::AudioBuffer<float>& buffer,double blockStartPpq,double bpm) noexcept
    {
        if(!isThreadRunning()||sourceRate<=0.0||buffer.getNumSamples()<=0)return;
        auto write=writePosition.load(std::memory_order_relaxed);
        const auto read=readPosition.load(std::memory_order_acquire);
        const auto downmixPlan=createPhaseSafeDownmixPlan(buffer);
        const auto ppqPerSourceSample=bpm/(60.0*sourceRate);
        const auto sourceSamplesPerOutput=sourceRate/modelSampleRate;
        for(int sample=0;sample<buffer.getNumSamples();++sample)
        {
            const auto mixed=phaseSafeDownmixSample(buffer,sample,downmixPlan);
            lowPass+=lowPassCoefficient*(mixed-lowPass);
            const auto currentPpq=blockStartPpq+(double)sample*ppqPerSourceSample;
            if(!hasPreviousInput)
            {
                previousLowPass=lowPass;previousPpq=currentPpq;hasPreviousInput=true;
            }
            const auto currentPosition=(double)inputPosition;
            while(nextOutputPosition<=currentPosition+0.0000001)
            {
                const auto alpha=inputPosition==0?1.0:juce::jlimit(0.0,1.0,nextOutputPosition-(currentPosition-1.0));
                const auto output=previousLowPass+(lowPass-previousLowPass)*(float)alpha;
                const auto outputPpq=previousPpq+(currentPpq-previousPpq)*alpha;
                if(write-read<ringCapacity)
                {
                    ring[(size_t)write&ringMask]={output,outputPpq,false};++write;
                }
                else droppedSamples.fetch_add(1,std::memory_order_relaxed);
                nextOutputPosition+=sourceSamplesPerOutput;
            }
            previousLowPass=lowPass;previousPpq=currentPpq;++inputPosition;
        }
        writePosition.store(write,std::memory_order_release);
    }

    Status status() const noexcept { return currentStatus.load(); }

private:
    void run() override
    {
        try
        {
            auto model=std::make_unique<BasicPitch>();
            currentStatus.store(Status::collecting);
            while(!threadShouldExit())
            {
                const auto reachedBoundary=drainRing();
                while(captured.size()>=chunkSamples)
                {
                    currentStatus.store(Status::analyzing);
                    transcribe(*model,chunkSamples,false);
                    if(captured.size()>=hopSamples)
                        captured.erase(captured.begin(),captured.begin()+(std::ptrdiff_t)hopSamples);
                    currentStatus.store(Status::refined);
                }
                if(reachedBoundary)
                {
                    while(captured.size()>=boundaryChunkSamples)
                    {
                        currentStatus.store(Status::analyzing);
                        transcribe(*model,boundaryChunkSamples,false);
                        captured.erase(captured.begin(),captured.begin()+(std::ptrdiff_t)boundaryHopSamples);
                    }
                    if(captured.size()>=minimumFlushSamples)
                    {
                        currentStatus.store(Status::analyzing);
                        transcribe(*model,captured.size(),true);
                    }
                    captured.clear();
                    lastRefinedEndPpq=-1000.0;
                    currentStatus.store(Status::refined);
                }
                wait(20);
            }
        }
        catch(const std::exception&)
        {
            currentStatus.store(Status::error);
        }
        catch(...)
        {
            currentStatus.store(Status::error);
        }
    }

    bool drainRing()
    {
        auto read=readPosition.load(std::memory_order_relaxed);
        const auto write=writePosition.load(std::memory_order_acquire);
        bool reachedBoundary=false;
        while(read<write)
        {
            auto sample=ring[(size_t)read&ringMask];
            if(sample.segmentBoundary)
            {
                ++read;
                reachedBoundary=true;
                break;
            }
            if(!captured.empty())
            {
                const auto delta=sample.ppq-captured.back().ppq;
                if(delta< -0.05||delta>0.05)
                {
                    reachedBoundary=true;
                    break;
                }
                else if(delta<0.0)sample.ppq=captured.back().ppq;
            }
            captured.push_back(sample);
            ++read;
        }
        readPosition.store(read,std::memory_order_release);
        return reachedBoundary;
    }

    std::vector<HarmonicFrameEvidence> analyzeHarmonicFrames(const std::vector<float>& audio,
                                                              size_t stableFirst,size_t stableLast) const
    {
        constexpr size_t frameSize=4096,hopSize=1024;
        constexpr size_t bassFrameSize=8192,bassHopSize=2048;
        std::vector<HarmonicFrameEvidence> frames;
        if(stableLast<=stableFirst||stableLast-stableFirst<frameSize)return frames;
        std::array<float,frameSize> window{};
        for(size_t sample=0;sample<frameSize;++sample)
            window[sample]=(float)(0.5-0.5*std::cos(2.0*juce::MathConstants<double>::pi
                                                   *(double)sample/(double)(frameSize-1)));
        struct TimedBass { size_t centre=0; int pitchClass=-1; };
        std::vector<TimedBass> bassFrames;
        if(stableLast-stableFirst>=bassFrameSize)
        {
            std::array<float,bassFrameSize> bassWindow{};
            for(size_t sample=0;sample<bassFrameSize;++sample)
                bassWindow[sample]=(float)(0.5-0.5*std::cos(2.0*juce::MathConstants<double>::pi
                    *(double)sample/(double)(bassFrameSize-1)));
            bassFrames.reserve((stableLast-stableFirst)/bassHopSize);
            for(auto first=stableFirst;first+bassFrameSize<=stableLast;first+=bassHopSize)
            {
                int bass=-1;
                calculateConstantQHpcp(audio.data()+first,bassWindow.data(),(int)bassFrameSize,
                                        modelSampleRate,&bass);
                bassFrames.push_back({first+bassFrameSize/2,bass});
            }
        }
        frames.reserve((stableLast-stableFirst)/hopSize);
        std::array<float,12> previousWeights{};
        auto hasPreviousWeights=false;
        for(auto first=stableFirst;first+frameSize<=stableLast;first+=hopSize)
        {
            double energy=0.0;
            for(size_t sample=0;sample<frameSize;++sample)
                energy+=(double)audio[first+sample]*audio[first+sample];
            const auto rms=std::sqrt(energy/(double)frameSize);
            if(rms<1.0e-4)continue;
            int shortBass=-1;
            const auto weights=calculateConstantQHpcp(audio.data()+first,window.data(),(int)frameSize,
                                                       modelSampleRate,&shortBass);
            const auto peak=*std::max_element(weights.begin(),weights.end());
            auto activePitchClasses=0;
            for(const auto weight:weights)if(weight>=peak*0.10f&&weight>0.06f)++activePitchClasses;
            if(peak<=0.0f||activePitchClasses<2||activePitchClasses>7)continue;
            auto changeConfidence=0.0f;
            if(hasPreviousWeights)
            {
                const auto previousTotal=std::accumulate(previousWeights.begin(),previousWeights.end(),0.0f);
                const auto currentTotal=std::accumulate(weights.begin(),weights.end(),0.0f);
                if(previousTotal>0.001f&&currentTotal>0.001f)
                {
                    auto distance=0.0f;
                    for(size_t pitchClass=0;pitchClass<weights.size();++pitchClass)
                        distance+=std::abs(previousWeights[pitchClass]/previousTotal
                                          -weights[pitchClass]/currentTotal);
                    changeConfidence=juce::jlimit(0.0f,1.0f,(distance*0.5f-0.10f)/0.32f);
                }
            }
            previousWeights=weights;hasPreviousWeights=true;
            const auto centre=first+frameSize/2;
            auto bass=shortBass;
            if(!bassFrames.empty())
            {
                const auto nearest=std::min_element(bassFrames.begin(),bassFrames.end(),[&](const auto& left,
                                                                                           const auto& right)
                {
                    return std::abs((int64_t)left.centre-(int64_t)centre)
                           <std::abs((int64_t)right.centre-(int64_t)centre);
                });
                if(nearest->pitchClass>=0)bass=nearest->pitchClass;
            }
            const auto supportFirst=centre>=hopSize/2?centre-hopSize/2:first;
            const auto supportLast=juce::jmin(stableLast-1,centre+hopSize/2);
            frames.push_back({captured[supportFirst].ppq,captured[supportLast].ppq,weights,bass,
                              juce::jlimit(0.15f,1.0f,(float)(rms/0.025)),changeConfidence});
        }
        stabilizeHarmonicFrames(frames);
        return frames;
    }

    void transcribe(BasicPitch& model,size_t sampleCount,bool finalWindow)
    {
        if(sampleCount==0||sampleCount>captured.size())return;
        constexpr size_t edgeSamples=(size_t)(modelSampleRate*0.40);
        const auto trailingPadding=finalWindow&&sampleCount<(size_t)(modelSampleRate*4.0)?edgeSamples:0;
        std::vector<float> audio(sampleCount+trailingPadding,0.0f);
        double energy=0.0;
        for(size_t index=0;index<sampleCount;++index)
        {
            audio[index]=captured[index].audio;
            energy+=(double)audio[index]*audio[index];
        }
        const auto stableFirst=juce::jmin(edgeSamples,sampleCount/4);
        const auto stableLast=finalWindow?sampleCount:sampleCount-edgeSamples;
        if(stableLast<=stableFirst)return;
        const auto stableStartPpq=captured[stableFirst].ppq;
        const auto stableEndPpq=captured[stableLast-1].ppq;
        if(stableEndPpq<=stableStartPpq)return;
        const auto replacementStart=finalWindow&&lastRefinedEndPpq>stableStartPpq
                                    &&lastRefinedEndPpq<stableEndPpq
                                        ?lastRefinedEndPpq:stableStartPpq;
        const auto rms=std::sqrt(energy/(double)sampleCount);
        if(rms<1.0e-4)
        {
            SharedChordSession::instance().replaceAudioRegions(replacementStart,stableEndPpq,{});
            lastRefinedEndPpq=stableEndPpq;
            return;
        }

        const auto analysisGain=rms<0.012?juce::jlimit(1.0,32.0,0.03/rms):1.0;
        if(analysisGain>1.0)
            for(size_t index=0;index<sampleCount;++index)
                audio[index]=juce::jlimit(-0.98f,0.98f,(float)(audio[index]*analysisGain));

        model.reset();
        model.setParameters(0.74f,0.67f,65.0f);
        model.transcribeToMIDI(audio.data(),(int)audio.size());

        const auto mapEvents=[&](const std::vector<Notes::Event>& events)
        {
            std::vector<PitchedNoteRegion> result;result.reserve(events.size());
            for(const auto& event:events)
            {
                const auto first=juce::jlimit<size_t>(stableFirst,stableLast-1,
                                                      (size_t)std::llround(event.startTime*modelSampleRate));
                const auto last=juce::jlimit<size_t>(first+1,stableLast,
                                                     (size_t)std::llround(event.endTime*modelSampleRate));
                if(last<=first||event.pitch<24||event.pitch>108)continue;
                result.push_back({captured[first].ppq,captured[last-1].ppq,event.pitch,
                                  juce::jlimit(0.05f,1.0f,(float)event.amplitude)});
            }
            return result;
        };
        const auto sensitive=mapEvents(model.getNoteEvents());
        model.setParameters(0.60f,0.56f,90.0f);
        model.updateMIDI();
        const auto strict=mapEvents(model.getNoteEvents());
        const auto notes=createConsensusNotes(sensitive,strict);
        const auto harmonicFrames=analyzeHarmonicFrames(audio,stableFirst,stableLast);
        auto regions=createChordRegionsFromNotes(notes,stableStartPpq,stableEndPpq,0.125,&harmonicFrames);
        double coverage=0.0;
        for(const auto& region:regions)coverage+=region.endPpq-region.startPpq;
        if(!regions.empty()&&coverage/(stableEndPpq-stableStartPpq)>=0.20)
            SharedChordSession::instance().replaceAudioRegions(replacementStart,stableEndPpq,regions);
        lastRefinedEndPpq=stableEndPpq;
    }

    std::unique_ptr<CapturedSample[]> ring;
    std::vector<CapturedSample> captured;
    std::atomic<uint64_t> readPosition{0},writePosition{0},droppedSamples{0};
    std::atomic<Status> currentStatus{Status::stopped};
    std::atomic<bool> transportPlaying{false};
    double sourceRate=44100.0,nextOutputPosition=0.0,previousPpq=0.0,lastRefinedEndPpq=-1000.0;
    uint64_t inputPosition=0;
    float lowPass=0.0f,previousLowPass=0.0f,lowPassCoefficient=1.0f;
    bool hasPreviousInput=false;
};

AudioTranscriptionRefiner::AudioTranscriptionRefiner():impl(std::make_unique<Impl>()) {}
AudioTranscriptionRefiner::~AudioTranscriptionRefiner()=default;
void AudioTranscriptionRefiner::prepare(double rate){impl->prepare(rate);}
void AudioTranscriptionRefiner::release(){impl->release();}
void AudioTranscriptionRefiner::setTransportPlaying(bool playing) noexcept{impl->setPlaying(playing);}
void AudioTranscriptionRefiner::pushAudio(const juce::AudioBuffer<float>& buffer,double ppq,double bpm) noexcept
{impl->push(buffer,ppq,bpm);}
AudioTranscriptionRefiner::Status AudioTranscriptionRefiner::status() const noexcept{return impl->status();}
juce::String AudioTranscriptionRefiner::statusText() const
{
    switch(status())
    {
        case Status::stopped:return "Neural refiner off";
        case Status::loading:return "Loading neural refiner";
        case Status::collecting:return "Neural refiner listening";
        case Status::analyzing:return "Refining audio chords";
        case Status::refined:return "Audio chords refined";
        case Status::error:return "Neural refiner unavailable";
    }
    return {};
}
