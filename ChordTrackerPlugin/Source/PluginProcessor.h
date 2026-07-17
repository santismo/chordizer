#pragma once
#include <JuceHeader.h>
#include "ChordEngine.h"
#include "ScalizerEngine.h"
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
    ChordSessionSnapshot sessionSnapshot() const;
    void clearSession() { SharedChordSession::instance().clear(); }
    void renameRegion(size_t index,const juce::String& name) { SharedChordSession::instance().renameRegion(index,name); }
    void setRegionScaleOverride(size_t index,const juce::String& scaleName)
        { SharedChordSession::instance().setRegionScaleOverride(index,scaleName); }
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
    bool supportsScalizer() const noexcept { return CHORDIZER_AUDIO_FX == 0; }
    bool isScalizerEnabled() const noexcept { return scalizerEnabled.load(); }
    void setScalizerEnabled(bool enabled) noexcept;
    ScalizerLockMode scalizerLockMode() const noexcept
        { return scalizerChordLock.load() ? ScalizerLockMode::chordTones : ScalizerLockMode::scale; }
    void setScalizerLockMode(ScalizerLockMode mode) noexcept;
    ScalizerHarmonyVoice scalizerHarmonyVoice(int index) const noexcept;
    void setScalizerHarmonyVoice(int index, ScalizerHarmonyVoice voice) noexcept;
    juce::String scalizerHarmonySummary() const;
    void updateEditorState(int width, int height, double zoom, double scroll,
                           bool leadSheet, bool singleColumn) noexcept;
private:
    bool readHost(double&,double&,int&,int&,bool&) const;
    void cacheHostTransport(double ppq,double bpm,int numerator,int denominator,bool playing) noexcept;
    void applyCachedHostTransport(ChordSessionSnapshot&) const noexcept;
    bool analyzeAudio(const juce::AudioBuffer<float>&, double, juce::String&, float&,
                      juce::StringArray&, double&, bool&);
    ChordinoChromaFrame calculateHpcpFrame();
    MidiChordDetector midiDetector;
    ScalizerEngine scalizerEngine;
    AudioChordStabilizer audioStabilizer;
    std::atomic<bool> listen { true };
    std::atomic<bool> scalizerEnabled { false }, scalizerChordLock { false };
    std::array<std::atomic<int>, 3> scalizerVoiceSettings { 0, 0, 0 };
    std::atomic<uint64_t> scalizerConfigRevision { 1 };
    uint64_t appliedScalizerConfigRevision = 0;
    bool scalizerWasEnabled = false;
    std::atomic<bool> leadSheetView { false }, leadSheetSingleColumn { false };
    std::atomic<int> timelineEditorWidth { 980 }, timelineEditorHeight { 320 };
    std::atomic<int> leadEditorWidth { 980 }, leadEditorHeight { 560 };
    std::atomic<double> timelineZoom { 16.0 }, timelineScroll { 0.0 };
    std::atomic<juce::Thread::ThreadID> audioThreadID { nullptr };
    // The editor must never ask a host for playhead data. Keep the most recent
    // audio-thread result here so this instance remains authoritative when a
    // stale Chordizer instance is still publishing to the shared session.
    std::atomic<uint64_t> hostTransportRevision { 0 };
    std::atomic<double> cachedHostPpq { 0.0 }, cachedHostBpm { 120.0 };
    std::atomic<int> cachedHostNumerator { 4 }, cachedHostDenominator { 4 };
    std::atomic<bool> cachedHostPlaying { false };
    uint64_t instanceID = 0;
    double sampleRate = 44100.0, lastPublishPpq = -1000.0;
    std::array<float,12> audioWeights{}, audioKeyWeights{};
    static constexpr int audioFrameSize=16384,audioHopSize=4096;
    std::array<float,audioFrameSize> audioFifo{},audioWindow{};
    std::array<float,12> beatHpcp{},beatBassWeights{},previousChordinoChroma{};
    int audioFifoPosition=0,beatFrameCount=0;
    float beatFrameWeight=0.0f,beatPeak=0.0f,previousBeatPeak=0.0f,beatChangeEvidence=0.0f;
    int64_t audioBeatIndex=-1;
    bool hasPreviousChordinoChroma=false;
#if CHORDIZER_AUDIO_FX
    std::unique_ptr<AudioTranscriptionRefiner> neuralRefiner;
#endif
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordTrackerProcessor)
};
