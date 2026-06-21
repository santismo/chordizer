#include <JuceHeader.h>
#include "../Source/AudioTranscriptionRefiner.h"
#include "../Source/ChordEngine.h"
#include "BasicPitch.h"
#include <cmath>
#include <iostream>
#include <unistd.h>

namespace
{
constexpr double duration=6.0,chordDuration=1.5;
const std::array<std::array<int,4>,4> progression{{{{51,55,58,62}},{{53,56,60,63}},
                                                   {{46,50,53,56}},{{51,55,58,62}}}};

std::vector<float> createAudio(double sampleRate,bool textured=false)
{
    std::vector<float> audio((size_t)(sampleRate*duration),0.0f);
    for(size_t sample=0;sample<audio.size();++sample)
    {
        const auto time=(double)sample/sampleRate;
        double value=0.0;
        if(!textured)
        {
            const auto chordIndex=juce::jlimit(0,3,(int)(time/chordDuration));
            const auto localTime=std::fmod(time,chordDuration);
            const auto envelope=juce::jmin(1.0,localTime/0.025)*juce::jmin(1.0,(chordDuration-localTime)/0.06);
            for(size_t voice=0;voice<progression[(size_t)chordIndex].size();++voice)
            {
                const auto pitch=progression[(size_t)chordIndex][voice];
                const auto frequency=440.0*std::pow(2.0,(pitch-69.0)/12.0);
                const auto voiceGain=voice==3?0.66:1.0;
                for(int harmonic=1;harmonic<=5;++harmonic)
                    value+=envelope*voiceGain
                           *std::sin(2.0*juce::MathConstants<double>::pi*frequency*harmonic*time)
                           /(harmonic*harmonic);
            }
            audio[sample]=(float)(value*0.10);
            continue;
        }

        // A brighter, mildly detuned pad with staggered voice attacks and release
        // overlap exercises timbre and transition behavior beyond stationary sines.
        for(size_t chord=0;chord<progression.size();++chord)
        {
            const auto local=time-(double)chord*chordDuration;
            if(local<0.0||local>chordDuration+0.22)continue;
            for(size_t voice=0;voice<progression[chord].size();++voice)
            {
                const auto voiceTime=local-(double)voice*0.011;
                if(voiceTime<0.0)continue;
                const auto attack=juce::jmin(1.0,voiceTime/0.045);
                const auto release=voiceTime<=chordDuration?1.0:std::exp(-(voiceTime-chordDuration)*18.0);
                const auto detune=(voice%2==0?-0.035:0.028);
                const auto pitch=progression[chord][voice]+detune;
                const auto frequency=440.0*std::pow(2.0,(pitch-69.0)/12.0);
                const auto voiceGain=voice==3?0.56:1.0;
                for(int harmonic=1;harmonic<=8;++harmonic)
                {
                    const auto spectralGain=1.0/std::pow((double)harmonic,1.42);
                    value+=attack*release*voiceGain*spectralGain
                           *std::sin(2.0*juce::MathConstants<double>::pi*frequency*harmonic*time
                                    +(double)voice*0.37);
                }
            }
        }
        const auto roomTone=0.0025*std::sin(2.0*juce::MathConstants<double>::pi*173.0*time)
                            *std::sin(2.0*juce::MathConstants<double>::pi*0.41*time);
        audio[sample]=(float)(std::tanh(value*0.22)*0.18+roomTone);
    }
    return audio;
}

std::vector<float> createArpeggiatedAudio(double sampleRate)
{
    std::vector<float> audio((size_t)(sampleRate*duration),0.0f);
    for(size_t sample=0;sample<audio.size();++sample)
    {
        const auto time=(double)sample/sampleRate;
        double value=0.0;
        for(size_t chord=0;chord<progression.size();++chord)
            for(int cycle=0;cycle<3;++cycle)
                for(size_t voice=0;voice<progression[chord].size();++voice)
                {
                    const auto onset=(double)chord*chordDuration+(double)cycle*0.50+(double)voice*0.06;
                    const auto noteTime=time-onset;
                    if(noteTime<0.0||noteTime>0.18)continue;
                    const auto envelope=juce::jmin(1.0,noteTime/0.006)*std::exp(-noteTime*22.0);
                    const auto frequency=440.0*std::pow(2.0,(progression[chord][voice]-69.0)/12.0);
                    const auto voiceGain=voice==3?0.68:1.0;
                    for(int harmonic=1;harmonic<=6;++harmonic)
                        value+=envelope*voiceGain
                               *std::sin(2.0*juce::MathConstants<double>::pi*frequency*harmonic*time
                                        +(double)voice*0.21)
                               /std::pow((double)harmonic,1.65);
                }
        audio[sample]=(float)(std::tanh(value*0.38)*0.22);
    }
    return audio;
}

std::vector<float> createPluckedDecayAudio(double sampleRate)
{
    std::vector<float> audio((size_t)(sampleRate*duration),0.0f);
    for(size_t sample=0;sample<audio.size();++sample)
    {
        const auto time=(double)sample/sampleRate;
        double value=0.0;
        for(size_t chord=0;chord<progression.size();++chord)
            for(size_t voice=0;voice<progression[chord].size();++voice)
            {
                const auto onset=(double)chord*chordDuration+(double)voice*0.016;
                const auto noteTime=time-onset;
                if(noteTime<0.0||noteTime>chordDuration+0.20)continue;
                const auto attack=1.0-std::exp(-noteTime*360.0);
                const auto decay=std::exp(-noteTime*(1.18+(double)voice*0.09));
                const auto release=noteTime<=chordDuration?1.0
                    :std::exp(-(noteTime-chordDuration)*14.0);
                const auto frequency=440.0*std::pow(2.0,(progression[chord][voice]-69.0)/12.0);
                const auto voiceGain=voice==3?0.62:1.0;
                for(int harmonic=1;harmonic<=10;++harmonic)
                {
                    const auto stretchedFrequency=frequency*(double)harmonic
                        *std::sqrt(1.0+0.00018*(double)(harmonic*harmonic));
                    value+=attack*decay*release*voiceGain/std::pow((double)harmonic,1.28)
                           *std::sin(2.0*juce::MathConstants<double>::pi*stretchedFrequency*noteTime
                                     +(double)voice*0.31);
                }
            }
        audio[sample]=(float)(std::tanh(value*0.34)*0.24);
    }
    return audio;
}

std::vector<float> createRhythmSectionAudio(double sampleRate)
{
    auto audio=createPluckedDecayAudio(sampleRate);
    uint32_t randomState=0x6d2b79f5u;
    for(size_t sample=0;sample<audio.size();++sample)
    {
        randomState=randomState*1664525u+1013904223u;
        const auto noise=(double)(randomState>>8)/(double)0x00ffffff*2.0-1.0;
        const auto time=(double)sample/sampleRate;
        const auto beatTime=std::fmod(time,0.5);
        const auto eighthTime=std::fmod(time,0.25);
        const auto backbeatTime=std::fmod(time+0.5,1.0);
        auto drums=0.0;
        if(beatTime<0.18)
        {
            const auto phase=2.0*juce::MathConstants<double>::pi
                *(92.0*beatTime-76.0*beatTime*beatTime);
            drums+=std::sin(phase)*std::exp(-beatTime*20.0)*0.38;
        }
        if(backbeatTime<0.16)
            drums+=noise*std::exp(-backbeatTime*30.0)*0.32
                   +std::sin(2.0*juce::MathConstants<double>::pi*185.0*backbeatTime)
                    *std::exp(-backbeatTime*24.0)*0.10;
        if(eighthTime<0.055)
            drums+=noise*std::exp(-eighthTime*75.0)*0.13;
        audio[sample]=(float)std::tanh((double)audio[sample]*2.7+drums);
    }
    return audio;
}

std::vector<PitchedNoteRegion> mapEvents(const std::vector<Notes::Event>& events)
{
    std::vector<PitchedNoteRegion> result;
    for(const auto& event:events)
        result.push_back({event.startTime*2.0,event.endTime*2.0,event.pitch,(float)event.amplitude});
    return result;
}

bool expectedProgression(const std::vector<ChordRegionData>& regions)
{
    return regions.size()==4&&regions[0].name=="Ebmaj7"&&regions[1].name=="Fm7"
           &&regions[2].name=="Bb7"&&regions[3].name=="Ebmaj7";
}

bool runDirectInference(BasicPitch& model,bool textured)
{
    constexpr double modelRate=22050.0;
    auto audio=createAudio(modelRate,textured);
    model.reset();
    model.setParameters(0.74f,0.67f,65.0f);
    model.transcribeToMIDI(audio.data(),(int)audio.size());
    const auto sensitive=mapEvents(model.getNoteEvents());
    model.setParameters(0.60f,0.56f,90.0f);
    model.updateMIDI();
    const auto strict=mapEvents(model.getNoteEvents());
    const auto notes=createConsensusNotes(sensitive,strict);
    const auto regions=createChordRegionsFromNotes(notes,0.4*2.0,(duration-0.4)*2.0);
    std::cout<<(textured?"Textured":"Clean")<<" inference: sensitive "<<sensitive.size()
             <<", strict "<<strict.size()<<", consensus "<<notes.size()<<std::endl;
    for(const auto& region:regions)
        std::cout<<region.startPpq<<"-"<<region.endPpq<<" "<<region.name<<'\n';
    return expectedProgression(regions);
}

bool runDirectArpeggioInference(BasicPitch& model)
{
    constexpr double modelRate=22050.0;
    auto audio=createArpeggiatedAudio(modelRate);
    model.reset();
    model.setParameters(0.74f,0.67f,65.0f);
    model.transcribeToMIDI(audio.data(),(int)audio.size());
    const auto sensitive=mapEvents(model.getNoteEvents());
    model.setParameters(0.60f,0.56f,90.0f);
    model.updateMIDI();
    const auto strict=mapEvents(model.getNoteEvents());
    const auto notes=createConsensusNotes(sensitive,strict);
    const auto regions=createChordRegionsFromNotes(notes,0.4*2.0,(duration-0.4)*2.0);
    std::cout<<"Arpeggio inference: sensitive "<<sensitive.size()<<", strict "<<strict.size()
             <<", consensus "<<notes.size()<<std::endl;
    for(const auto& region:regions)
        std::cout<<region.startPpq<<"-"<<region.endPpq<<" "<<region.name<<'\n';
    return expectedProgression(regions);
}

bool runDirectPluckedInference(BasicPitch& model)
{
    constexpr double modelRate=22050.0;
    auto audio=createPluckedDecayAudio(modelRate);
    model.reset();
    model.setParameters(0.74f,0.67f,65.0f);
    model.transcribeToMIDI(audio.data(),(int)audio.size());
    const auto sensitive=mapEvents(model.getNoteEvents());
    model.setParameters(0.60f,0.56f,90.0f);
    model.updateMIDI();
    const auto strict=mapEvents(model.getNoteEvents());
    const auto regions=createChordRegionsFromNotes(createConsensusNotes(sensitive,strict),
                                                   0.4*2.0,(duration-0.4)*2.0);
    std::cout<<"Plucked-decay inference: sensitive "<<sensitive.size()<<", strict "<<strict.size()<<std::endl;
    for(const auto& region:regions)
        std::cout<<region.startPpq<<"-"<<region.endPpq<<" "<<region.name<<'\n';
    return expectedProgression(regions);
}

bool runWorkerAtRate(double sampleRate,bool tempoChange=false,bool textured=false,bool antiPhaseStereo=false,
                     float inputGain=1.0f,bool isolatedPeak=false)
{
    const auto audio=createAudio(sampleRate,textured);
    SharedChordSession::instance().clear();
    AudioTranscriptionRefiner refiner;
    refiner.prepare(sampleRate);
    refiner.setTransportPlaying(true);
    constexpr int blockSize=512;
    for(size_t first=0;first<audio.size();first+=blockSize)
    {
        const auto samples=(int)juce::jmin((size_t)blockSize,audio.size()-first);
        const auto seconds=(double)first/sampleRate;
        const auto bpm=tempoChange&&seconds>=3.0?90.0:120.0;
        const auto ppq=tempoChange&&seconds>=3.0?6.0+(seconds-3.0)*1.5:seconds*2.0;
        juce::AudioBuffer<float> block(antiPhaseStereo?2:1,samples);
        block.copyFrom(0,0,audio.data()+first,samples);
        block.applyGain(0,0,samples,inputGain);
        if(antiPhaseStereo)
        {
            block.copyFrom(1,0,audio.data()+first,samples);
            block.applyGain(1,0,samples,-inputGain);
        }
        const auto peakSample=audio.size()/2;
        if(isolatedPeak&&peakSample>=first&&peakSample<first+(size_t)samples)
            block.setSample(0,(int)(peakSample-first),0.95f);
        refiner.pushAudio(block,ppq,bpm);
    }
    refiner.setTransportPlaying(false);
    const auto timeout=juce::Time::getMillisecondCounter()+12000;
    while(refiner.status()!=AudioTranscriptionRefiner::Status::refined
          &&refiner.status()!=AudioTranscriptionRefiner::Status::error
          &&juce::Time::getMillisecondCounter()<timeout)
        juce::Thread::sleep(20);
    const auto regions=SharedChordSession::instance().snapshot().regions;
    std::cout<<sampleRate<<" Hz worker"<<(tempoChange?" with tempo change":"")
             <<(textured?" textured":"")<<(antiPhaseStereo?" anti-phase stereo":"")<<": "
             <<(inputGain<1.0f?" quiet":"")<<(isolatedPeak?" with isolated peak":"")<<" "
             <<refiner.statusText()<<", regions: "<<regions.size()<<std::endl;
    refiner.release();
    if(!expectedProgression(regions))
    {
        for(const auto& region:regions)
            std::cerr<<region.startPpq<<'-'<<region.endPpq<<' '<<region.name
                     <<" confidence "<<region.confidence<<'\n';
        return false;
    }
    if(tempoChange)
    {
        const std::array<double,3> expectedStarts{3.0,6.0,8.25};
        for(size_t index=0;index<expectedStarts.size();++index)
            if(std::abs(regions[index+1].startPpq-expectedStarts[index])>0.15)return false;
    }
    return true;
}

bool runSilentWorker()
{
    constexpr double sampleRate=22050.0;
    SharedChordSession::instance().replaceRegions({{0.8,11.5,"C","Audio",0.8f,{},false}});
    AudioTranscriptionRefiner refiner;
    refiner.prepare(sampleRate);
    refiner.setTransportPlaying(true);
    constexpr int blockSize=512;
    const auto sampleCount=(size_t)(sampleRate*duration);
    for(size_t first=0;first<sampleCount;first+=blockSize)
    {
        const auto samples=(int)juce::jmin((size_t)blockSize,sampleCount-first);
        juce::AudioBuffer<float> block(1,samples);
        block.clear();
        refiner.pushAudio(block,(double)first/sampleRate*2.0,120.0);
    }
    refiner.setTransportPlaying(false);
    const auto timeout=juce::Time::getMillisecondCounter()+3000;
    while(refiner.status()!=AudioTranscriptionRefiner::Status::refined
          &&refiner.status()!=AudioTranscriptionRefiner::Status::error
          &&juce::Time::getMillisecondCounter()<timeout)
        juce::Thread::sleep(20);
    const auto regions=SharedChordSession::instance().snapshot().regions;
    std::cout<<"Silent worker: "<<refiner.statusText()<<", regions: "<<regions.size()<<std::endl;
    refiner.release();
    return regions.empty();
}

bool runArpeggioWorker()
{
    constexpr double sampleRate=44100.0;
    const auto audio=createArpeggiatedAudio(sampleRate);
    SharedChordSession::instance().clear();
    AudioTranscriptionRefiner refiner;
    refiner.prepare(sampleRate);
    refiner.setTransportPlaying(true);
    constexpr int blockSize=512;
    for(size_t first=0;first<audio.size();first+=blockSize)
    {
        const auto samples=(int)juce::jmin((size_t)blockSize,audio.size()-first);
        juce::AudioBuffer<float> block(1,samples);
        block.copyFrom(0,0,audio.data()+first,samples);
        refiner.pushAudio(block,(double)first/sampleRate*2.0,120.0);
    }
    refiner.setTransportPlaying(false);
    const auto timeout=juce::Time::getMillisecondCounter()+10000;
    while(refiner.status()!=AudioTranscriptionRefiner::Status::refined
          &&refiner.status()!=AudioTranscriptionRefiner::Status::error
          &&juce::Time::getMillisecondCounter()<timeout)
        juce::Thread::sleep(20);
    const auto regions=SharedChordSession::instance().snapshot().regions;
    std::cout<<"Arpeggio worker: "<<refiner.statusText()<<", regions: "<<regions.size()<<std::endl;
    if(!expectedProgression(regions))for(const auto& region:regions)
        std::cerr<<region.startPpq<<'-'<<region.endPpq<<' '<<region.name<<'\n';
    refiner.release();
    if(!expectedProgression(regions))return false;
    const std::array<double,3> expectedStarts{3.0,6.0,9.0};
    for(size_t index=0;index<expectedStarts.size();++index)
        if(std::abs(regions[index+1].startPpq-expectedStarts[index])>0.25)return false;
    return true;
}

bool runPluckedWorker()
{
    constexpr double sampleRate=44100.0;
    const auto audio=createPluckedDecayAudio(sampleRate);
    SharedChordSession::instance().clear();
    AudioTranscriptionRefiner refiner;
    refiner.prepare(sampleRate);
    refiner.setTransportPlaying(true);
    constexpr int blockSize=512;
    for(size_t first=0;first<audio.size();first+=blockSize)
    {
        const auto samples=(int)juce::jmin((size_t)blockSize,audio.size()-first);
        juce::AudioBuffer<float> block(1,samples);
        block.copyFrom(0,0,audio.data()+first,samples);
        refiner.pushAudio(block,(double)first/sampleRate*2.0,120.0);
    }
    refiner.setTransportPlaying(false);
    const auto timeout=juce::Time::getMillisecondCounter()+10000;
    while(refiner.status()!=AudioTranscriptionRefiner::Status::refined
          &&refiner.status()!=AudioTranscriptionRefiner::Status::error
          &&juce::Time::getMillisecondCounter()<timeout)
        juce::Thread::sleep(20);
    const auto regions=SharedChordSession::instance().snapshot().regions;
    std::cout<<"Plucked-decay worker: "<<refiner.statusText()<<", regions: "<<regions.size()<<std::endl;
    if(!expectedProgression(regions))for(const auto& region:regions)
        std::cerr<<region.startPpq<<'-'<<region.endPpq<<' '<<region.name<<'\n';
    refiner.release();
    if(!expectedProgression(regions))return false;
    const std::array<double,3> expectedStarts{3.0,6.0,9.0};
    for(size_t index=0;index<expectedStarts.size();++index)
        if(std::abs(regions[index+1].startPpq-expectedStarts[index])>0.25)return false;
    return true;
}

bool runRhythmSectionWorker()
{
    constexpr double sampleRate=44100.0;
    const auto audio=createRhythmSectionAudio(sampleRate);
    SharedChordSession::instance().clear();
    AudioTranscriptionRefiner refiner;
    refiner.prepare(sampleRate);
    refiner.setTransportPlaying(true);
    constexpr int blockSize=512;
    for(size_t first=0;first<audio.size();first+=blockSize)
    {
        const auto samples=(int)juce::jmin((size_t)blockSize,audio.size()-first);
        juce::AudioBuffer<float> block(1,samples);
        block.copyFrom(0,0,audio.data()+first,samples);
        refiner.pushAudio(block,(double)first/sampleRate*2.0,120.0);
    }
    refiner.setTransportPlaying(false);
    const auto timeout=juce::Time::getMillisecondCounter()+12000;
    while(refiner.status()!=AudioTranscriptionRefiner::Status::refined
          &&refiner.status()!=AudioTranscriptionRefiner::Status::error
          &&juce::Time::getMillisecondCounter()<timeout)
        juce::Thread::sleep(20);
    const auto regions=SharedChordSession::instance().snapshot().regions;
    std::cout<<"Rhythm-section worker: "<<refiner.statusText()<<", regions: "<<regions.size()<<std::endl;
    if(!expectedProgression(regions))for(const auto& region:regions)
        std::cerr<<region.startPpq<<'-'<<region.endPpq<<' '<<region.name<<'\n';
    refiner.release();
    if(!expectedProgression(regions))return false;
    const std::array<double,3> expectedStarts{3.0,6.0,9.0};
    for(size_t index=0;index<expectedStarts.size();++index)
        if(std::abs(regions[index+1].startPpq-expectedStarts[index])>0.30)return false;
    return true;
}

bool runRecordedGuitarWorker(const juce::String& fileName,const juce::StringArray& expected,
                             const char* label,bool requireDirect)
{
    const auto fixture=juce::File(__FILE__).getParentDirectory().getChildFile("Fixtures")
        .getChildFile(fileName);
    const auto matches=[&](const std::vector<ChordRegionData>& regions)
    {
        if(regions.size()!=(size_t)expected.size())return false;
        for(int index=0;index<expected.size();++index)
            if(regions[(size_t)index].name!=expected[index])return false;
        return true;
    };
    juce::AudioFormatManager formats;formats.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(fixture));
    if(reader==nullptr)
    {
        std::cerr<<"Could not open guitar fixture: "<<fixture.getFullPathName()<<'\n';
        return false;
    }
    const auto sampleCount=(int)reader->lengthInSamples;
    juce::AudioBuffer<float> audio((int)reader->numChannels,sampleCount);
    if(!reader->read(&audio,0,sampleCount,0,true,true))return false;

    std::vector<float> mono((size_t)sampleCount);
    const auto downmix=createPhaseSafeDownmixPlan(audio);
    for(int sample=0;sample<sampleCount;++sample)
        mono[(size_t)sample]=phaseSafeDownmixSample(audio,sample,downmix);
    constexpr double directRate=22050.0;
    const auto resampledCount=(size_t)std::floor((double)sampleCount*directRate/reader->sampleRate);
    std::vector<float> resampled(resampledCount);
    for(size_t sample=0;sample<resampledCount;++sample)
    {
        const auto position=(double)sample*reader->sampleRate/directRate;
        const auto first=(size_t)position;
        const auto second=juce::jmin(first+1,mono.size()-1);
        const auto alpha=(float)(position-(double)first);
        resampled[sample]=mono[first]+(mono[second]-mono[first])*alpha;
    }
    BasicPitch directModel;
    directModel.setParameters(0.74f,0.67f,65.0f);
    directModel.transcribeToMIDI(resampled.data(),(int)resampled.size());
    const auto directSensitive=mapEvents(directModel.getNoteEvents());
    directModel.setParameters(0.60f,0.56f,90.0f);
    directModel.updateMIDI();
    const auto directStrict=mapEvents(directModel.getNoteEvents());
    const auto directNotes=createConsensusNotes(directSensitive,directStrict);
    const auto directRegions=createChordRegionsFromNotes(directNotes,0.8,
                                                          (double)resampled.size()/directRate*2.0-0.01);
    std::cout<<label<<" direct: sensitive "<<directSensitive.size()<<", strict "
             <<directStrict.size()<<", regions "<<directRegions.size()<<std::endl;
    for(const auto& region:directRegions)
        std::cout<<region.startPpq<<'-'<<region.endPpq<<' '<<region.name
                 <<" confidence "<<region.confidence<<'\n';
    if(!matches(directRegions))
        for(const auto& note:directNotes)
            std::cout<<"  note "<<note.midiNote<<' '<<note.startPpq<<'-'<<note.endPpq
                     <<" confidence "<<note.confidence<<'\n';

    SharedChordSession::instance().clear();
    AudioTranscriptionRefiner refiner;
    refiner.prepare(reader->sampleRate);
    refiner.setTransportPlaying(true);
    constexpr int blockSize=512;
    for(int first=0;first<sampleCount;first+=blockSize)
    {
        const auto samples=juce::jmin(blockSize,sampleCount-first);
        juce::AudioBuffer<float> block(audio.getNumChannels(),samples);
        for(int channel=0;channel<audio.getNumChannels();++channel)
            block.copyFrom(channel,0,audio,channel,first,samples);
        refiner.pushAudio(block,(double)first/reader->sampleRate*2.0,120.0);
    }
    refiner.setTransportPlaying(false);
    const auto timeout=juce::Time::getMillisecondCounter()+16000;
    while(refiner.status()!=AudioTranscriptionRefiner::Status::refined
          &&refiner.status()!=AudioTranscriptionRefiner::Status::error
          &&juce::Time::getMillisecondCounter()<timeout)
        juce::Thread::sleep(20);
    const auto regions=SharedChordSession::instance().snapshot().regions;
    std::cout<<label<<" worker: "<<refiner.statusText()<<", regions: "<<regions.size()<<std::endl;
    for(const auto& region:regions)
    {
        std::cout<<region.startPpq<<'-'<<region.endPpq<<' '<<region.name
                 <<" confidence "<<region.confidence<<'\n';
        if(!region.alternatives.isEmpty())
            std::cout<<"  alternatives: "<<region.alternatives.joinIntoString(", ")<<'\n';
    }
    refiner.release();
    return matches(regions)&&(!requireDirect||matches(directRegions));
}

bool runPercussiveWorker()
{
    constexpr double sampleRate=22050.0;
    std::vector<float> audio((size_t)(sampleRate*duration),0.0f);
    uint32_t randomState=0x91e10da5u;
    for(size_t sample=0;sample<audio.size();++sample)
    {
        randomState=randomState*1664525u+1013904223u;
        const auto noise=(float)((double)(randomState>>8)/(double)0x00ffffff*2.0-1.0);
        const auto seconds=(double)sample/sampleRate;
        const auto withinHit=std::fmod(seconds,0.375);
        audio[sample]=noise*(float)(std::exp(-withinHit*42.0)*0.32);
    }
    SharedChordSession::instance().clear();
    AudioTranscriptionRefiner refiner;
    refiner.prepare(sampleRate);
    refiner.setTransportPlaying(true);
    constexpr int blockSize=512;
    for(size_t first=0;first<audio.size();first+=blockSize)
    {
        const auto samples=(int)juce::jmin((size_t)blockSize,audio.size()-first);
        juce::AudioBuffer<float> block(1,samples);
        block.copyFrom(0,0,audio.data()+first,samples);
        refiner.pushAudio(block,(double)first/sampleRate*2.0,120.0);
    }
    refiner.setTransportPlaying(false);
    const auto timeout=juce::Time::getMillisecondCounter()+7000;
    while(refiner.status()!=AudioTranscriptionRefiner::Status::refined
          &&refiner.status()!=AudioTranscriptionRefiner::Status::error
          &&juce::Time::getMillisecondCounter()<timeout)
        juce::Thread::sleep(20);
    const auto refined=refiner.status()==AudioTranscriptionRefiner::Status::refined;
    const auto regions=SharedChordSession::instance().snapshot().regions;
    std::cout<<"Percussive worker: "<<refiner.statusText()<<", regions: "<<regions.size()<<std::endl;
    refiner.release();
    return refined&&regions.empty();
}

bool runShortStopRestartWorker()
{
    constexpr double sampleRate=22050.0,shortDuration=3.0;
    auto audio=createAudio(sampleRate,false);
    audio.resize((size_t)(sampleRate*shortDuration));
    SharedChordSession::instance().clear();
    AudioTranscriptionRefiner refiner;
    refiner.prepare(sampleRate);
    const auto capture=[&](double basePpq)
    {
        refiner.setTransportPlaying(true);
        constexpr int blockSize=512;
        for(size_t first=0;first<audio.size();first+=blockSize)
        {
            const auto samples=(int)juce::jmin((size_t)blockSize,audio.size()-first);
            juce::AudioBuffer<float> block(1,samples);
            block.copyFrom(0,0,audio.data()+first,samples);
            refiner.pushAudio(block,basePpq+(double)first/sampleRate*2.0,120.0);
        }
        refiner.setTransportPlaying(false);
    };
    const auto waitForRegions=[&](size_t count)
    {
        const auto timeout=juce::Time::getMillisecondCounter()+7000;
        while(SharedChordSession::instance().snapshot().regions.size()<count
              &&refiner.status()!=AudioTranscriptionRefiner::Status::error
              &&juce::Time::getMillisecondCounter()<timeout)
            juce::Thread::sleep(20);
        return SharedChordSession::instance().snapshot().regions.size()>=count;
    };

    capture(0.0);
    if(!waitForRegions(2)){refiner.release();return false;}
    capture(16.0);
    if(!waitForRegions(4)){refiner.release();return false;}
    const auto regions=SharedChordSession::instance().snapshot().regions;
    std::cout<<"Short stop/restart worker: "<<refiner.statusText()<<", regions: "<<regions.size()<<std::endl;
    refiner.release();
    if(regions.size()!=4)return false;
    if(regions[0].name!="Ebmaj7"||regions[1].name!="Fm7"
       ||regions[2].name!="Ebmaj7"||regions[3].name!="Fm7")return false;
    return regions[1].endPpq>5.90&&regions[3].endPpq>21.90
           &&std::abs(regions[2].startPpq-16.8)<0.18
           &&std::abs(regions[3].startPpq-19.0)<0.18;
}

bool runPlayingSeekWorker()
{
    constexpr double sampleRate=22050.0,shortDuration=3.0;
    auto audio=createAudio(sampleRate,false);
    audio.resize((size_t)(sampleRate*shortDuration));
    SharedChordSession::instance().clear();
    AudioTranscriptionRefiner refiner;
    refiner.prepare(sampleRate);
    refiner.setTransportPlaying(true);
    const auto pushAt=[&](double basePpq)
    {
        constexpr int blockSize=512;
        for(size_t first=0;first<audio.size();first+=blockSize)
        {
            const auto samples=(int)juce::jmin((size_t)blockSize,audio.size()-first);
            juce::AudioBuffer<float> block(1,samples);
            block.copyFrom(0,0,audio.data()+first,samples);
            refiner.pushAudio(block,basePpq+(double)first/sampleRate*2.0,120.0);
        }
    };
    pushAt(0.0);
    pushAt(20.0);
    refiner.setTransportPlaying(false);
    const auto timeout=juce::Time::getMillisecondCounter()+12000;
    while(SharedChordSession::instance().snapshot().regions.size()<4
          &&refiner.status()!=AudioTranscriptionRefiner::Status::error
          &&juce::Time::getMillisecondCounter()<timeout)
        juce::Thread::sleep(20);
    const auto regions=SharedChordSession::instance().snapshot().regions;
    std::cout<<"Playing seek worker: "<<refiner.statusText()<<", regions: "<<regions.size()<<std::endl;
    refiner.release();
    return regions.size()==4&&regions[0].name=="Ebmaj7"&&regions[1].name=="Fm7"
           &&regions[2].name=="Ebmaj7"&&regions[3].name=="Fm7"
           &&regions[1].endPpq>5.90&&regions[3].endPpq>25.90
           &&std::abs(regions[2].startPpq-20.8)<0.18;
}
}

int main(int argc,char** argv)
{
    try
    {
        const auto suffix=std::string("neural-smoke-")+std::to_string((long long)getpid());
        setenv("CHORDIZER_SESSION_SUFFIX",suffix.c_str(),1);
        if(argc>1&&std::string(argv[1])=="--recorded-b-minor")
            return runRecordedGuitarWorker("Bm7, F#7, Gmaj, A13.wav",
                                           {"Bm7","F#7","G","A13"},
                                           "Recorded B minor progression",false)?0:3;
        std::cout<<"Starting dual-threshold neural inference"<<std::endl;
        BasicPitch model;
        if(!runDirectInference(model,false)||!runDirectInference(model,true)
           ||!runDirectArpeggioInference(model)||!runDirectPluckedInference(model))return 1;

        if(!runWorkerAtRate(11025.0)||!runWorkerAtRate(22050.0)
           ||!runWorkerAtRate(44100.0,false,true)||!runWorkerAtRate(44100.0,false,false,true)
           ||!runWorkerAtRate(44100.0,false,false,false,0.015f)
           ||!runWorkerAtRate(44100.0,false,false,false,0.015f,true)
           ||!runWorkerAtRate(48000.0,true)||!runArpeggioWorker()||!runPluckedWorker()
           ||!runRhythmSectionWorker()
           ||!runRecordedGuitarWorker("Ebmaj7, Fm7, Bb7, Ebmaj7.wav",
                                      {"Ebmaj7","Fm7","Bb7","Ebmaj7"},
                                      "Recorded Eb progression",true)
           ||!runRecordedGuitarWorker("Bm7, F#7, Gmaj, A13.wav",
                                      {"Bm7","F#7","G","A13"},
                                      "Recorded B minor progression",false)
           ||!runPercussiveWorker()
           ||!runSilentWorker()||!runShortStopRestartWorker()||!runPlayingSeekWorker())return 3;
        return 0;
    }
    catch(const std::exception& error)
    {
        std::cerr<<"Neural model error: "<<error.what()<<'\n';
        return 2;
    }
}
