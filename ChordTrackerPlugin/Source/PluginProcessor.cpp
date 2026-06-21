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
    sampleRate=rate>0?rate:44100.0;midiDetector.reset();audioStabilizer.reset();audioWeights.fill(0.0f);audioKeyWeights.fill(0.0f);
    audioFifo.fill(0.0f);beatHpcp.fill(0.0f);beatBassWeights.fill(0.0f);audioFifoPosition=0;beatFrameCount=0;
    beatFrameWeight=beatPeak=previousBeatPeak=0.0f;audioBeatIndex=-1;
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
    SharedChordSession::instance().updateTransport(ppq,bpm,numerator,denominator,playing);
    return true;
}

std::array<float,12> ChordTrackerProcessor::calculateHpcpFrame(int& bassPitchClass) const
{
    return calculateConstantQHpcp(audioFifo.data(),audioWindow.data(),audioFrameSize,sampleRate,&bassPitchClass);
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
            for(size_t i=0;i<12;++i)
            {
                const auto observed=beatHpcp[i]/beatFrameWeight;
                audioWeights[i]=audioWeights[i]*0.10f+observed*0.90f;
            }
            const auto bass=beatBassWeights[(size_t)std::distance(beatBassWeights.begin(),std::max_element(beatBassWeights.begin(),beatBassWeights.end()))]>0.0f
                                ?(int)std::distance(beatBassWeights.begin(),std::max_element(beatBassWeights.begin(),beatBassWeights.end())):-1;
            chord=identifyChord(audioWeights,confidence,bass,&alternatives,&audioKeyWeights,0.08f);
            for(size_t i=0;i<12;++i)
            {
                const auto observed=beatHpcp[i]/beatFrameWeight;
                audioKeyWeights[i]=audioKeyWeights[i]*0.97f+observed*0.03f;
            }
            onset=previousBeatPeak<=1.0e-5f||beatPeak>=previousBeatPeak*1.12f;
            analysisPpq=(double)audioBeatIndex;completed=chord!="--";
        }
        previousBeatPeak=beatPeak;beatPeak=0.0f;
        beatHpcp.fill(0.0f);beatBassWeights.fill(0.0f);beatFrameCount=0;beatFrameWeight=0.0f;audioBeatIndex=beat;
    }
    const auto downmixPlan=createPhaseSafeDownmixPlan(buffer);
    for(int sample=0;sample<buffer.getNumSamples();++sample)
    {
        const auto mixed=phaseSafeDownmixSample(buffer,sample,downmixPlan);
        beatPeak=juce::jmax(beatPeak,std::abs(mixed));
        audioFifo[(size_t)audioFifoPosition++]=mixed;
        if(audioFifoPosition<audioFrameSize)continue;
        int bass=-1;const auto frame=calculateHpcpFrame(bass);
        if(*std::max_element(frame.begin(),frame.end())>0.0f)
        {
            beatFrameWeight=beatFrameWeight*0.35f+1.0f;
            for(size_t i=0;i<12;++i)beatHpcp[i]=beatHpcp[i]*0.35f+frame[i];
            for(auto& value:beatBassWeights)value*=0.35f;
            if(bass>=0)beatBassWeights[(size_t)bass]+=1.0f;
            ++beatFrameCount;
        }
        std::copy(audioFifo.end()-audioHopSize,audioFifo.end(),audioFifo.begin());audioFifoPosition=audioHopSize;
    }
    return completed;
}

void ChordTrackerProcessor::processBlock(juce::AudioBuffer<float>& buffer,juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals guard;
#if CHORDIZER_AUDIO_FX
    juce::ignoreUnused(midi);
#else
    buffer.clear();
#endif
    double ppq=0,bpm=120; int n=4,d=4; bool playing=false;
    if(!readHost(ppq,bpm,n,d,playing)) return;
    SharedChordSession::instance().updateTransport(ppq,bpm,n,d,playing);
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
    return "MIDI chord tracking";
#endif
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
    stream.writeInt(0x4354524b); stream.writeInt(4); stream.writeBool(listen.load());
    stream.writeBool(true); stream.writeInt(timelineEditorWidth.load()); stream.writeInt(timelineEditorHeight.load());
    stream.writeDouble(timelineZoom.load()); stream.writeDouble(timelineScroll.load()); stream.writeBool(leadSheetView.load());
    stream.writeBool(leadSheetSingleColumn.load());
    stream.writeInt(leadEditorWidth.load()); stream.writeInt(leadEditorHeight.load());
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
    }
    else { stream.setPosition(0); listen.store(stream.readBool()); }
}
juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new ChordTrackerProcessor(); }
