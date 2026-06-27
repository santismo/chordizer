#pragma once
#include <JuceHeader.h>
#include "ChordEngine.h"
#if CHORDIZER_AUDIO_FX
#include "AudioTranscriptionRefiner.h"
#endif

class ChordTrackerProcessor final : public juce::AudioProcessor
{
public:
    ChordTrackerProcessor();
    ~ChordTrackerProcessor() override;
    void prepareToPlay(double, int) override;
    void releaseResources() override;
    bool isBusesLayoutSupported(const BusesLayout&) const override;
    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }
    const juce::String getName() const override { return JucePlugin_Name; }
    bool acceptsMidi() const override { return CHORDIZER_AUDIO_FX == 0; }
    bool producesMidi() const override { return CHORDIZER_AUDIO_FX == 0; }
    bool isMidiEffect() const override { return CHORDIZER_AUDIO_FX == 0; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int,const juce::String&) override {}
    void getStateInformation(juce::MemoryBlock&) override;
    void setStateInformation(const void*,int) override;
    ChordSessionSnapshot sessionSnapshot() const { return SharedChordSession::instance().snapshot(); }
    void clearSession() { SharedChordSession::instance().clear(); }
    void renameRegion(size_t index,const juce::String& name) { SharedChordSession::instance().renameRegion(index,name); }
    void deleteRegion(size_t index) { SharedChordSession::instance().deleteRegion(index); }
    bool resizeRegion(size_t index,double startPpq,double endPpq)
        { return SharedChordSession::instance().resizeRegion(index,startPpq,endPpq); }
    bool quantizeRegion(size_t index,double gridPpq)
        { return SharedChordSession::instance().quantizeRegion(index,gridPpq); }
    bool extendRegionToNext(size_t index) { return SharedChordSession::instance().extendRegionToNext(index); }
    void replaceRegions(const std::vector<ChordRegionData>& regions) { SharedChordSession::instance().replaceRegions(regions); }
    void setTextScale(bool leadSheet,float scale) { SharedChordSession::instance().setTextScale(leadSheet,scale); }
    bool isListening() const noexcept { return listen.load(); }
    void setListening(bool value) noexcept { listen.store(value); }
    bool refreshTransportFromHost();
    juce::String analysisStatusText() const;
    juce::String sourceName() const { return CHORDIZER_AUDIO_FX ? "Audio" : "MIDI"; }
    int savedEditorWidth(bool forLeadSheet) const noexcept { return forLeadSheet?leadEditorWidth.load():timelineEditorWidth.load(); }
    int savedEditorHeight(bool forLeadSheet) const noexcept { return forLeadSheet?leadEditorHeight.load():timelineEditorHeight.load(); }
    double savedTimelineZoom() const noexcept { return timelineZoom.load(); }
    double savedTimelineScroll() const noexcept { return timelineScroll.load(); }
    bool savedLeadSheetView() const noexcept { return leadSheetView.load(); }
    bool savedLeadSheetSingleColumn() const noexcept { return leadSheetSingleColumn.load(); }
    void updateEditorState(int width, int height, double zoom, double scroll,
                           bool leadSheet, bool singleColumn) noexcept;
private:
    bool readHost(double&,double&,int&,int&,bool&) const;
    bool analyzeAudio(const juce::AudioBuffer<float>&, double, juce::String&, float&,
                      juce::StringArray&, double&, bool&);
    std::array<float,12> calculateHpcpFrame(int& bassPitchClass) const;
    MidiChordDetector midiDetector;
    AudioChordStabilizer audioStabilizer;
    std::atomic<bool> listen { true };
    std::atomic<bool> leadSheetView { false }, leadSheetSingleColumn { false };
    std::atomic<int> timelineEditorWidth { 980 }, timelineEditorHeight { 320 };
    std::atomic<int> leadEditorWidth { 980 }, leadEditorHeight { 560 };
    std::atomic<double> timelineZoom { 16.0 }, timelineScroll { 0.0 };
    std::atomic<juce::Thread::ThreadID> audioThreadID { nullptr };
    uint64_t instanceID = 0;
    double sampleRate = 44100.0, lastPublishPpq = -1000.0;
    std::array<float,12> audioWeights{}, audioKeyWeights{};
    static constexpr int audioFrameSize=16384,audioHopSize=4096;
    std::array<float,audioFrameSize> audioFifo{},audioWindow{};
    std::array<float,12> beatHpcp{},beatBassWeights{};
    int audioFifoPosition=0,beatFrameCount=0;
    float beatFrameWeight=0.0f,beatPeak=0.0f,previousBeatPeak=0.0f;
    int64_t audioBeatIndex=-1;
#if CHORDIZER_AUDIO_FX
    std::unique_ptr<AudioTranscriptionRefiner> neuralRefiner;
#endif
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordTrackerProcessor)
};
