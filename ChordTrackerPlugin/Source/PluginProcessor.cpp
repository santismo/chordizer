#include "PluginProcessor.h"
#include "PluginEditor.h"
#include <cmath>

ChordTrackerProcessor::ChordTrackerProcessor()
#if CHORDIZER_AUDIO_FX
    : AudioProcessor(BusesProperties().withInput("Input",juce::AudioChannelSet::stereo(),true).withOutput("Output",juce::AudioChannelSet::stereo(),true))
#else
    : AudioProcessor(BusesProperties())
#endif
{
    instanceID = SharedChordSession::instance().registerInstance();
#if CHORDIZER_AUDIO_FX
    neuralRefiner=std::make_unique<AudioTranscriptionRefiner>();
#endif
}
ChordTrackerProcessor::~ChordTrackerProcessor()
{
    releaseResources();
    SharedChordSession::instance().unregisterInstance(instanceID);
}
void ChordTrackerProcessor::prepareToPlay(double rate,int)
{
    sampleRate=rate>0?rate:44100.0;midiDetector.reset();scalizerEngine.reset();audioStabilizer.reset();audioWeights.fill(0.0f);audioKeyWeights.fill(0.0f);
    scalizerWasEnabled=false;appliedScalizerConfigRevision=0;
    audioFifo.fill(0.0f);beatHpcp.fill(0.0f);beatBassWeights.fill(0.0f);previousChordinoChroma.fill(0.0f);
    audioFifoPosition=0;beatFrameCount=0;hasPreviousChordinoChroma=false;
    beatFrameWeight=beatPeak=previousBeatPeak=beatChangeEvidence=0.0f;audioBeatIndex=-1;
    for(int i=0;i<audioFrameSize;++i)audioWindow[(size_t)i]=(float)(0.5-0.5*std::cos(2.0*juce::MathConstants<double>::pi*i/(audioFrameSize-1)));
#if CHORDIZER_AUDIO_FX
    neuralRefiner->prepare(sampleRate);
#endif
}
void ChordTrackerProcessor::releaseResources()
{
#if CHORDIZER_AUDIO_FX
    if(neuralRefiner)neuralRefiner->release();
#endif
}
bool ChordTrackerProcessor::isBusesLayoutSupported(const BusesLayout& layouts) const
{
#if CHORDIZER_AUDIO_FX
    return layouts.getMainInputChannelSet() == layouts.getMainOutputChannelSet() && !layouts.getMainInputChannelSet().isDisabled();
#else
    juce::ignoreUnused(layouts); return true;
#endif
}

bool ChordTrackerProcessor::readHost(double& ppq,double& bpm,int& numerator,int& denominator,bool& playing) const
{
    if(audioThreadID.load(std::memory_order_relaxed)!=juce::Thread::getCurrentThreadId()) return false;
    auto* head=getPlayHead(); if(head==nullptr) return false;
    auto position=head->getPosition(); if(!position) return false;
    playing=position->getIsPlaying();
    if(auto value=position->getPpqPosition()) ppq=*value; else return false;
    if(auto value=position->getBpm()) bpm=*value;
    if(auto signature=position->getTimeSignature()) { numerator=signature->numerator; denominator=signature->denominator; }
    return true;
}

bool ChordTrackerProcessor::refreshTransportFromHost()
{
    double ppq=0.0,bpm=120.0;int numerator=4,denominator=4;bool playing=false;
    if(!readHost(ppq,bpm,numerator,denominator,playing))return false;
    cacheHostTransport(ppq,bpm,numerator,denominator,playing);
    SharedChordSession::instance().updateTransport(ppq,bpm,numerator,denominator,playing);
    return true;
}

void ChordTrackerProcessor::cacheHostTransport(double ppq,double bpm,int numerator,int denominator,bool playing) noexcept
{
    hostTransportRevision.fetch_add(1,std::memory_order_acq_rel);
    cachedHostPpq.store(ppq,std::memory_order_relaxed);
    cachedHostBpm.store(bpm,std::memory_order_relaxed);
    cachedHostNumerator.store(numerator,std::memory_order_relaxed);
    cachedHostDenominator.store(denominator,std::memory_order_relaxed);
    cachedHostPlaying.store(playing,std::memory_order_relaxed);
    hostTransportRevision.fetch_add(1,std::memory_order_release);
}

void ChordTrackerProcessor::applyCachedHostTransport(ChordSessionSnapshot& snapshot) const noexcept
{
    for(int attempt=0;attempt<3;++attempt)
    {
        const auto before=hostTransportRevision.load(std::memory_order_acquire);
        if(before==0||(before&1u)!=0)continue;
        const auto ppq=cachedHostPpq.load(std::memory_order_relaxed);
        const auto bpm=cachedHostBpm.load(std::memory_order_relaxed);
        const auto numerator=cachedHostNumerator.load(std::memory_order_relaxed);
        const auto denominator=cachedHostDenominator.load(std::memory_order_relaxed);
        const auto playing=cachedHostPlaying.load(std::memory_order_relaxed);
        if(before!=hostTransportRevision.load(std::memory_order_acquire))continue;
        snapshot.playheadPpq=ppq;snapshot.bpm=bpm;snapshot.numerator=numerator;
        snapshot.denominator=denominator;snapshot.playing=playing;
        return;
    }
}

ChordSessionSnapshot ChordTrackerProcessor::sessionSnapshot() const
{
    auto snapshot=SharedChordSession::instance().snapshot();
    applyCachedHostTransport(snapshot);
    return snapshot;
}

ChordinoChromaFrame ChordTrackerProcessor::calculateHpcpFrame()
{
    auto frame=calculateChordinoChromaFrame(audioFifo.data(),audioWindow.data(),audioFrameSize,sampleRate,
                                            hasPreviousChordinoChroma?&previousChordinoChroma:nullptr);
    if(*std::max_element(frame.chroma.begin(),frame.chroma.end())>0.0f)
    {
        previousChordinoChroma=frame.chroma;
        hasPreviousChordinoChroma=true;
    }
    return frame;
}

bool ChordTrackerProcessor::analyzeAudio(const juce::AudioBuffer<float>& buffer,double ppq,juce::String& chord,
                                         float& confidence,juce::StringArray& alternatives,double& analysisPpq,bool& onset)
{
    const auto beat=(int64_t)std::floor(ppq);
    bool completed=false;onset=false;
    if(audioBeatIndex<0)audioBeatIndex=beat;
    else if(beat!=audioBeatIndex)
    {
        if(std::abs(beat-audioBeatIndex)<=2&&beatFrameCount>0)
        {
            const auto retain=beatChangeEvidence>0.42f?0.05f:0.18f;
            for(size_t i=0;i<12;++i)
            {
                const auto observed=beatHpcp[i]/beatFrameWeight;
                audioWeights[i]=audioWeights[i]*retain+observed*(1.0f-retain);
            }
            const auto bass=beatBassWeights[(size_t)std::distance(beatBassWeights.begin(),std::max_element(beatBassWeights.begin(),beatBassWeights.end()))]>0.0f
                                ?(int)std::distance(beatBassWeights.begin(),std::max_element(beatBassWeights.begin(),beatBassWeights.end())):-1;
            chord=identifyChord(audioWeights,confidence,bass,&alternatives,&audioKeyWeights,0.08f);
            for(size_t i=0;i<12;++i)
            {
                const auto observed=beatHpcp[i]/beatFrameWeight;
                audioKeyWeights[i]=audioKeyWeights[i]*0.97f+observed*0.03f;
            }
            confidence*=juce::jlimit(0.45f,1.0f,beatFrameWeight/(float)juce::jmax(1,beatFrameCount));
            onset=previousBeatPeak<=1.0e-5f||beatPeak>=previousBeatPeak*1.12f||beatChangeEvidence>0.42f;
            analysisPpq=(double)audioBeatIndex;completed=chord!="--";
        }
        previousBeatPeak=beatPeak;beatPeak=0.0f;
        beatHpcp.fill(0.0f);beatBassWeights.fill(0.0f);beatFrameCount=0;beatFrameWeight=0.0f;
        beatChangeEvidence=0.0f;audioBeatIndex=beat;
    }
    const auto downmixPlan=createPhaseSafeDownmixPlan(buffer);
    for(int sample=0;sample<buffer.getNumSamples();++sample)
    {
        const auto mixed=phaseSafeDownmixSample(buffer,sample,downmixPlan);
        beatPeak=juce::jmax(beatPeak,std::abs(mixed));
        audioFifo[(size_t)audioFifoPosition++]=mixed;
        if(audioFifoPosition<audioFrameSize)continue;
        const auto frame=calculateHpcpFrame();
        if(frame.confidence>0.08f&&*std::max_element(frame.chroma.begin(),frame.chroma.end())>0.0f)
        {
            const auto frameWeight=juce::jmax(0.16f,frame.confidence);
            beatFrameWeight=beatFrameWeight*0.35f+frameWeight;
            for(size_t i=0;i<12;++i)beatHpcp[i]=beatHpcp[i]*0.35f+frame.chroma[i]*frameWeight;
            for(auto& value:beatBassWeights)value*=0.35f;
            for(size_t i=0;i<12;++i)beatBassWeights[i]+=frame.bassChroma[i]*frameWeight;
            if(frame.bassPitchClass>=0)beatBassWeights[(size_t)frame.bassPitchClass]+=frameWeight*0.6f;
            beatChangeEvidence=juce::jmax(beatChangeEvidence,frame.changeConfidence*frame.confidence);
            ++beatFrameCount;
        }
        std::copy(audioFifo.end()-audioHopSize,audioFifo.end(),audioFifo.begin());audioFifoPosition=audioHopSize;
    }
    return completed;
}

void ChordTrackerProcessor::processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals guard;
    audioThreadID.store(juce::Thread::getCurrentThreadId(),std::memory_order_relaxed);
#if CHORDIZER_AUDIO_FX
    juce::ignoreUnused(midi);
#else
    buffer.clear();
#endif
    double ppq=0,bpm=120; int n=4,d=4; bool playing=false;
    const auto hostAvailable=readHost(ppq,bpm,n,d,playing);
    if(hostAvailable)
    {
        cacheHostTransport(ppq,bpm,n,d,playing);
        SharedChordSession::instance().updateTransport(ppq,bpm,n,d,playing);
    }
    else
    {
        const auto current=SharedChordSession::instance().snapshot();
        ppq=current.playheadPpq;bpm=current.bpm;n=current.numerator;d=current.denominator;playing=current.playing;
    }
#if !CHORDIZER_AUDIO_FX
    const auto scalizerOn=scalizerEnabled.load();
    const auto configRevision=scalizerConfigRevision.load();
    if(scalizerWasEnabled && (!scalizerOn || configRevision!=appliedScalizerConfigRevision))
        scalizerEngine.releaseAll(midi,0);
    scalizerWasEnabled=scalizerOn;appliedScalizerConfigRevision=configRevision;
    if(scalizerOn)
    {
        std::array<ScalizerHarmonyVoice,3> voices;
        for(int index=0;index<3;++index)voices[(size_t)index]=scalizerHarmonyVoice(index);
        scalizerEngine.process(midi,SharedChordSession::instance().snapshot().regions,ppq,bpm,sampleRate,
                               scalizerLockMode(),voices);
        return;
    }
#endif
    if(!hostAvailable) return;
    const auto listening=listen.load();
#if CHORDIZER_AUDIO_FX
    neuralRefiner->setTransportPlaying(listening&&playing);
#endif
    if(!listening || !playing) return;
#if CHORDIZER_AUDIO_FX
    neuralRefiner->pushAudio(buffer,ppq,bpm);
#endif
    float confidence=1.0f; ChordUpdateKind kind=ChordUpdateKind::none; juce::String chord; juce::StringArray alternatives; double chordPpq=ppq;
#if CHORDIZER_AUDIO_FX
    bool onset=false;
    if(analyzeAudio(buffer,ppq,chord,confidence,alternatives,chordPpq,onset))
    {
        const auto update=audioStabilizer.process(chord,confidence,alternatives,chordPpq,onset);
        chord=update.chord;kind=update.kind;confidence=update.confidence;alternatives=update.alternatives;
        if(update.regionStartPpq>=0.0)chordPpq=update.regionStartPpq;
    }
#else
    auto update=midiDetector.process(midi,ppq); chord=update.chord; kind=update.kind; confidence=update.confidence; alternatives=update.alternatives;
    if(update.regionStartPpq>=0.0)chordPpq=update.regionStartPpq;
#endif
    const auto immediate=kind==ChordUpdateKind::start||kind==ChordUpdateKind::refine;
    if(chord!="--" && kind!=ChordUpdateKind::none && (immediate || chordPpq-lastPublishPpq>=0.0625 || chordPpq<lastPublishPpq))
    {
        SharedChordSession::instance().publishChord(chordPpq,chord,sourceName(),confidence,kind,alternatives);
        lastPublishPpq=chordPpq;
    }
}
juce::String ChordTrackerProcessor::analysisStatusText() const
{
#if CHORDIZER_AUDIO_FX
    return neuralRefiner?neuralRefiner->statusText():"Neural refiner unavailable";
#else
    if(scalizerEnabled.load())
        return juce::String("Scalizer ")+(scalizerChordLock.load()?"chord lock":"scale lock");
    return "MIDI chord tracking";
#endif
}

void ChordTrackerProcessor::setScalizerEnabled(bool enabled) noexcept
{
    if(scalizerEnabled.exchange(enabled)!=enabled)++scalizerConfigRevision;
}

void ChordTrackerProcessor::setScalizerLockMode(ScalizerLockMode mode) noexcept
{
    const auto chordLock=mode==ScalizerLockMode::chordTones;
    if(scalizerChordLock.exchange(chordLock)!=chordLock)++scalizerConfigRevision;
}

ScalizerHarmonyVoice ChordTrackerProcessor::scalizerHarmonyVoice(int index) const noexcept
{
    if(index<0||index>=3)return {};
    const auto packed=scalizerVoiceSettings[(size_t)index].load();
    if(packed==0)return {};
    return {std::abs(packed),packed>0};
}

void ChordTrackerProcessor::setScalizerHarmonyVoice(int index,ScalizerHarmonyVoice voice) noexcept
{
    if(index<0||index>=3)return;
    const auto degree=voice.degree>=2?juce::jlimit(2,8,voice.degree):0;
    const auto packed=degree==0?0:(voice.above?degree:-degree);
    if(scalizerVoiceSettings[(size_t)index].exchange(packed)!=packed)++scalizerConfigRevision;
}

juce::String ChordTrackerProcessor::scalizerHarmonySummary() const
{
    juce::StringArray descriptions;
    for(int index=0;index<3;++index)
    {
        const auto voice=scalizerHarmonyVoice(index);
        if(voice.degree>=2)descriptions.add(juce::String(voice.degree)+(voice.above?"↑":"↓"));
    }
    return descriptions.isEmpty()?"Harmony: Off":"Harmony: "+descriptions.joinIntoString(" ");
}

juce::AudioProcessorEditor* ChordTrackerProcessor::createEditor() { return new ChordTrackerEditor(*this); }
void ChordTrackerProcessor::updateEditorState(int width,int height,double zoom,double scroll,
                                              bool leadSheet,bool singleColumn) noexcept
{
    if(leadSheet){leadEditorWidth.store(width);leadEditorHeight.store(height);}
    else{timelineEditorWidth.store(width);timelineEditorHeight.store(height);}
    timelineZoom.store(zoom); timelineScroll.store(scroll); leadSheetView.store(leadSheet);
    leadSheetSingleColumn.store(singleColumn);
}
void ChordTrackerProcessor::getStateInformation(juce::MemoryBlock& destination)
{
    juce::MemoryOutputStream stream(destination,true);
    stream.writeInt(0x4354524b); stream.writeInt(5); stream.writeBool(listen.load());
    stream.writeBool(true); stream.writeInt(timelineEditorWidth.load()); stream.writeInt(timelineEditorHeight.load());
    stream.writeDouble(timelineZoom.load()); stream.writeDouble(timelineScroll.load()); stream.writeBool(leadSheetView.load());
    stream.writeBool(leadSheetSingleColumn.load());
    stream.writeInt(leadEditorWidth.load()); stream.writeInt(leadEditorHeight.load());
    stream.writeBool(scalizerEnabled.load());stream.writeBool(scalizerChordLock.load());
    for(const auto& setting:scalizerVoiceSettings)stream.writeInt(setting.load());
}
void ChordTrackerProcessor::setStateInformation(const void* data,int bytes)
{
    juce::MemoryInputStream stream(data,(size_t)bytes,false);
    if (bytes >= 8 && stream.readInt() == 0x4354524b)
    {
        const auto version=stream.readInt(); listen.store(stream.readBool());
        juce::ignoreUnused(stream.readBool()); timelineEditorWidth.store(juce::jlimit(420,1800,stream.readInt())); timelineEditorHeight.store(juce::jlimit(100,1100,stream.readInt()));
        timelineZoom.store(juce::jlimit(0.01,256.0,stream.readDouble())); timelineScroll.store(juce::jmax(0.0,stream.readDouble())); leadSheetView.store(stream.readBool());
        if(version>=3) leadSheetSingleColumn.store(stream.readBool());
        if(version>=4){leadEditorWidth.store(juce::jlimit(420,1800,stream.readInt()));leadEditorHeight.store(juce::jlimit(100,1100,stream.readInt()));}
        if(version>=5)
        {
            scalizerEnabled.store(stream.readBool());scalizerChordLock.store(stream.readBool());
            for(auto& setting:scalizerVoiceSettings)setting.store(juce::jlimit(-8,8,stream.readInt()));
            ++scalizerConfigRevision;
        }
    }
    else { stream.setPosition(0); listen.store(stream.readBool()); }
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ChordTrackerProcessor(); }
