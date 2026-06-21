#include <JuceHeader.h>
#include "../Source/ChordEngine.h"
#include "../Source/MidiExport.h"
#include <iostream>
#include <sys/wait.h>
#include <unistd.h>

namespace
{
int failures = 0;

void expect(bool condition, const char* message)
{
    if (condition) return;
    ++failures;
    std::cerr << "FAIL: " << message << '\n';
}

juce::String identify(std::initializer_list<int> notes)
{
    std::array<float, 12> weights {};
    auto bass = -1;
    for (const auto note : notes)
    {
        if (bass < 0) bass = note % 12;
        weights[(size_t)(note % 12)] += 1.0f;
    }
    float confidence = 0.0f;
    return identifyChord(weights, confidence, bass);
}

juce::String identifyHarmonicAudio(std::initializer_list<double> frequencies)
{
    constexpr int size=16384;constexpr double rate=44100.0;
    std::array<float,size> audio{},window{};
    for(int i=0;i<size;++i)
    {
        const auto time=i/rate;window[(size_t)i]=(float)(0.5-0.5*std::cos(2.0*juce::MathConstants<double>::pi*i/(size-1)));
        double sample=0.0;
        for(const auto frequency:frequencies)for(int harmonic=1;harmonic<=8;++harmonic)
            sample+=std::sin(2.0*juce::MathConstants<double>::pi*frequency*harmonic*time)/(harmonic*harmonic*0.55+0.45);
        audio[(size_t)i]=(float)(sample/juce::jmax(1,(int)frequencies.size()));
    }
    const auto profile=calculateConstantQHpcp(audio.data(),window.data(),size,rate);
    float confidence=0.0f;return identifyChord(profile,confidence);
}
}

int main(int argc,char** argv)
{
    if(std::getenv("CHORDIZER_SESSION_SUFFIX")==nullptr)
    {
        const auto suffix=std::string("tests-")+std::to_string((long long)getpid());
        setenv("CHORDIZER_SESSION_SUFFIX",suffix.c_str(),1);
    }
    if(argc>1&&std::string(argv[1])=="--publish-audio")
    {
        SharedChordSession::instance().publishChord(8.0,"Am7","Audio",0.8f,ChordUpdateKind::start);
        return 0;
    }
    expect(identify({60, 64, 67}) == "C", "C major triad");
    expect(identify({60, 64, 67, 71, 74}) == "Cmaj9", "C major ninth");
    expect(identify({60, 61, 64, 67, 70}) == "C7b9", "C altered dominant");
    expect(identify({64, 67, 71, 72}) == "Cmaj7/E", "slash chord inversion");
    std::array<float,12> cSharpMinor{}; cSharpMinor[1]=1; cSharpMinor[4]=1; cSharpMinor[8]=1;
    std::array<float,12> aMajorContext{}; for(const auto pc:{9,11,1,2,4,6,8}) aMajorContext[(size_t)pc]=1;
    float spellingConfidence=0.0f; juce::StringArray spellingAlternatives;
    expect(identifyChord(cSharpMinor,spellingConfidence,1,&spellingAlternatives,&aMajorContext)=="C#m",
           "sharp-key context controls enharmonic spelling");
    expect(!spellingAlternatives.isEmpty(),"ranked chord alternatives are retained");
    std::array<float,12> noisyFMinor{};
    for(const auto pc:{5,8,0,3})noisyFMinor[(size_t)pc]=1.0f;
    for(const auto pc:{11,6,10})noisyFMinor[(size_t)pc]=0.18f;
    float noisyConfidence=0.0f;
    const auto noisyFMinorName=identifyChord(noisyFMinor,noisyConfidence,5,nullptr,nullptr,0.08f);
    if(noisyFMinorName!="Fm7")std::cerr<<"Noisy F minor detected as "<<noisyFMinorName<<'\n';
    expect(noisyFMinorName=="Fm7",
           "Audio complexity penalty prefers the supported seventh chord over noisy extensions");
    std::array<float,12> aThirteenShell{};
    aThirteenShell[9]=1.0f;aThirteenShell[1]=0.92f;aThirteenShell[7]=0.20f;aThirteenShell[6]=0.86f;
    float shellConfidence=0.0f;
    expect(identifyChord(aThirteenShell,shellConfidence,9,nullptr,nullptr,0.08f)=="A13",
           "Audio scoring recognizes a root-third-seventh-thirteenth shell voicing");
    aThirteenShell[7]=0.0f;
    expect(identifyChord(aThirteenShell,shellConfidence,9,nullptr,nullptr,0.08f)!="A13",
           "A sixth without dominant-seventh evidence is not promoted to a thirteenth chord");
    std::array<float,12> weakAlteredFSharp{};
    weakAlteredFSharp[6]=1.0f;weakAlteredFSharp[10]=0.86f;weakAlteredFSharp[1]=0.75f;
    weakAlteredFSharp[4]=0.82f;weakAlteredFSharp[7]=0.20f;
    float alteredConfidence=0.0f;
    std::array<float,12> bMinorContext{};
    for(const auto pc:{11,1,2,4,6,7,9})bMinorContext[(size_t)pc]=1.0f;
    const auto weakAlteredName=identifyChord(weakAlteredFSharp,alteredConfidence,6,nullptr,&bMinorContext,0.08f);
    if(weakAlteredName!="F#7")std::cerr<<"Weak altered dominant detected as "<<weakAlteredName<<'\n';
    expect(weakAlteredName=="F#7",
           "A weak audio b9 overtone does not promote a dominant seventh to 7b9");
    expect(identify({59,62,66,70})=="BmMaj7","Explicit MIDI minor-major seventh remains available");

    std::vector<PitchedNoteRegion> transcribedProgression;
    const auto addTranscribedChord=[&](double start,double end,std::initializer_list<int> pitches)
    {
        for(const auto pitch:pitches)transcribedProgression.push_back({start,end,pitch,0.92f});
    };
    addTranscribedChord(0.0,4.0,{51,55,58,62});
    addTranscribedChord(4.0,8.0,{53,56,60,63});
    addTranscribedChord(8.0,12.0,{46,50,53,56});
    addTranscribedChord(12.0,16.0,{51,55,58,62});
    const auto transcribedRegions=createChordRegionsFromNotes(transcribedProgression,0.0,16.0);
    expect(transcribedRegions.size()==4,"neural note events create one stable region per chord");
    if(transcribedRegions.size()==4)
    {
        expect(transcribedRegions[0].name=="Ebmaj7"&&transcribedRegions[1].name=="Fm7"
               &&transcribedRegions[2].name=="Bb7"&&transcribedRegions[3].name=="Ebmaj7",
               "neural note segmentation resolves the reported Eb major progression");
        expect(std::abs(transcribedRegions[1].startPpq-4.0)<0.000001
               &&std::abs(transcribedRegions[2].startPpq-8.0)<0.000001,
               "neural chord boundaries remain PPQ aligned");
    }
    const std::vector<PitchedNoteRegion> sensitiveCmaj7{{0.0,4.0,60,0.82f},{0.0,4.0,64,0.78f},
                                                         {0.0,4.0,67,0.80f},{0.0,4.0,71,0.40f},
                                                         {0.0,4.0,66,0.10f}};
    const std::vector<PitchedNoteRegion> strictC{{0.01,3.99,60,0.72f},{0.01,3.99,64,0.70f},
                                                  {0.01,3.99,67,0.71f}};
    const auto consensus=createConsensusNotes(sensitiveCmaj7,strictC);
    const auto consensusRegions=createChordRegionsFromNotes(consensus,0.0,4.0);
    if(consensusRegions.size()!=1||consensusRegions.front().name!="Cmaj7")
        for(const auto& region:consensusRegions)std::cerr<<"Consensus Cmaj7 detected as "<<region.name<<'\n';
    expect(consensusRegions.size()==1&&consensusRegions.front().name=="Cmaj7",
           "dual-threshold consensus keeps a quiet real seventh while suppressing a weak chromatic note");

    HarmonicFrameEvidence cMaj7Frame;
    cMaj7Frame.startPpq=0.0;cMaj7Frame.endPpq=4.0;cMaj7Frame.bassPitchClass=0;
    for(const auto pitchClass:{0,4,7,11})cMaj7Frame.pitchWeights[(size_t)pitchClass]=1.0f;
    const std::vector<HarmonicFrameEvidence> cMaj7Frames{cMaj7Frame};
    const std::vector<PitchedNoteRegion> missingSeventh{{0.0,4.0,60,0.9f},{0.0,4.0,64,0.9f},
                                                        {0.0,4.0,67,0.9f}};
    const auto recoveredSeventh=createChordRegionsFromNotes(missingSeventh,0.0,4.0,0.125,&cMaj7Frames);
    expect(recoveredSeventh.size()==1&&recoveredSeventh.front().name=="Cmaj7",
           "harmonic evidence restores a seventh missed by neural note transcription");
    HarmonicFrameEvidence bMinorSeventhFrame;
    bMinorSeventhFrame.startPpq=0.0;bMinorSeventhFrame.endPpq=4.0;bMinorSeventhFrame.bassPitchClass=11;
    bMinorSeventhFrame.pitchWeights[11]=1.0f;bMinorSeventhFrame.pitchWeights[2]=0.92f;
    bMinorSeventhFrame.pitchWeights[6]=0.90f;bMinorSeventhFrame.pitchWeights[9]=0.24f;
    const std::vector<PitchedNoteRegion> bMinorTriad{{0.0,4.0,47,0.9f},{0.0,4.0,50,0.9f},
                                                     {0.0,4.0,54,0.9f}};
    const std::vector<HarmonicFrameEvidence> bMinorSeventhFrames{bMinorSeventhFrame};
    const auto recoveredMinorSeventh=createChordRegionsFromNotes(bMinorTriad,0.0,4.0,0.125,
                                                                  &bMinorSeventhFrames);
    if(recoveredMinorSeventh.size()!=1||recoveredMinorSeventh.front().name!="Bm7")
        for(const auto& region:recoveredMinorSeventh)std::cerr<<"Recovered B minor seventh as "<<region.name<<'\n';
    expect(recoveredMinorSeventh.size()==1&&recoveredMinorSeventh.front().name=="Bm7",
           "harmonic and key evidence restore a quiet minor seventh");

    auto hallucinatedSharp=missingSeventh;
    hallucinatedSharp.push_back({0.0,4.0,71,0.72f});
    hallucinatedSharp.push_back({0.0,4.0,66,0.20f});
    const auto correctedHallucination=createChordRegionsFromNotes(hallucinatedSharp,0.0,4.0,0.125,&cMaj7Frames);
    expect(correctedHallucination.size()==1&&correctedHallucination.front().name=="Cmaj7",
           "harmonic evidence suppresses a weak neural chromatic hallucination");

    const auto acousticOnly=createChordRegionsFromNotes({},0.0,4.0,0.125,&cMaj7Frames);
    expect(acousticOnly.size()==1&&acousticOnly.front().name=="Cmaj7",
           "sparse harmonic evidence can recover a chord when neural transcription misses every note");
    std::vector<HarmonicFrameEvidence> harmonicProgression;
    const auto addHarmonicFrames=[&](double start,double end,std::initializer_list<int> pitchClasses,int bass)
    {
        for(auto centre=start+0.0625;centre<end;centre+=0.125)
        {
            HarmonicFrameEvidence frame;
            frame.startPpq=centre-0.0625;frame.endPpq=centre+0.0625;
            frame.bassPitchClass=bass;frame.confidence=0.9f;
            frame.changeConfidence=start>0.0&&centre<start+0.13?0.95f:0.0f;
            for(const auto pitchClass:pitchClasses)frame.pitchWeights[(size_t)pitchClass]=1.0f;
            harmonicProgression.push_back(frame);
        }
    };
    addHarmonicFrames(0.0,4.0,{3,7,10,2},3);
    addHarmonicFrames(4.0,8.0,{5,8,0,3},5);
    addHarmonicFrames(8.0,12.0,{10,2,5,8},10);
    addHarmonicFrames(12.0,16.0,{3,7,10,2},3);
    const auto harmonicOnlyProgression=createChordRegionsFromNotes({},0.0,16.0,0.125,
                                                                   &harmonicProgression);
    expect(harmonicOnlyProgression.size()==4
           &&harmonicOnlyProgression[0].name=="Ebmaj7"
           &&harmonicOnlyProgression[1].name=="Fm7"
           &&harmonicOnlyProgression[2].name=="Bb7"
           &&harmonicOnlyProgression[3].name=="Ebmaj7",
           "harmonic changes recover a complete progression without neural note attacks");
    if(harmonicOnlyProgression.size()==4)
        expect(std::abs(harmonicOnlyProgression[1].startPpq-4.0625)<0.07
               &&std::abs(harmonicOnlyProgression[2].startPpq-8.0625)<0.07
               &&std::abs(harmonicOnlyProgression[3].startPpq-12.0625)<0.07,
               "harmonic change centres refine PPQ boundaries without note onsets");
    const auto makeFrame=[](double centre,std::initializer_list<int> pitchClasses,float change)
    {
        HarmonicFrameEvidence frame;
        frame.startPpq=centre-0.0625;frame.endPpq=centre+0.0625;
        frame.confidence=1.0f;frame.changeConfidence=change;
        for(const auto pitchClass:pitchClasses)frame.pitchWeights[(size_t)pitchClass]=1.0f;
        return frame;
    };
    std::vector<HarmonicFrameEvidence> transientFrames{
        makeFrame(1.0,{0,4,7,11},0.0f),makeFrame(1.125,{1,6,8},0.9f),
        makeFrame(1.25,{0,4,7,11},0.9f)};
    stabilizeHarmonicFrames(transientFrames);
    expect(transientFrames[1].pitchWeights[0]>0.65f&&transientFrames[1].pitchWeights[4]>0.65f
           &&transientFrames[1].changeConfidence<0.30f&&transientFrames[2].changeConfidence<0.40f,
           "out-and-back one-frame chroma excursions are suppressed");
    std::vector<HarmonicFrameEvidence> sustainedChangeFrames{
        makeFrame(2.0,{0,4,7,11},0.0f),makeFrame(2.125,{5,8,0,3},0.9f),
        makeFrame(2.25,{5,8,0,3},0.05f)};
    stabilizeHarmonicFrames(sustainedChangeFrames);
    expect(sustainedChangeFrames[1].pitchWeights[5]>0.99f
           &&sustainedChangeFrames[1].changeConfidence>0.85f,
           "sustained harmonic changes bypass transient smoothing");
    HarmonicFrameEvidence broadbandFrame;
    broadbandFrame.startPpq=0.0;broadbandFrame.endPpq=4.0;
    broadbandFrame.pitchWeights.fill(1.0f);
    const std::vector<HarmonicFrameEvidence> broadbandFrames{broadbandFrame};
    expect(createChordRegionsFromNotes({},0.0,4.0,0.125,&broadbandFrames).empty(),
           "broadband harmonic profiles do not create false chord regions");

    const std::vector<PitchedNoteRegion> heldCommonTones{{0.0,8.0,64,0.9f},{0.0,8.0,67,0.9f},
                                                         {0.0,4.0,60,0.9f},{0.0,4.0,71,0.9f},
                                                         {4.0,8.0,57,0.9f},{4.0,8.0,60,0.9f}};
    const auto commonToneRegions=createChordRegionsFromNotes(heldCommonTones,0.0,8.0);
    expect(commonToneRegions.size()==2&&commonToneRegions[0].name=="Cmaj7"
           &&commonToneRegions[1].name=="Am7",
           "neural segmentation changes harmony while common chord tones remain sustained");

    const std::vector<PitchedNoteRegion> octaveDoubledMaj7{{0.0,4.0,48,0.94f},{0.0,4.0,60,0.92f},
                                                            {0.0,4.0,72,0.90f},{0.0,4.0,64,0.86f},
                                                            {0.0,4.0,67,0.84f},{0.0,4.0,71,0.48f},
                                                            {0.0,4.0,38,0.06f}};
    const auto doubledRegions=createChordRegionsFromNotes(octaveDoubledMaj7,0.0,4.0);
    expect(doubledRegions.size()==1&&doubledRegions.front().name=="Cmaj7",
           "octave-doubled roots and a weak false bass do not hide a quieter major seventh");

    std::vector<PitchedNoteRegion> closeChords;
    const auto addCloseChord=[&](double start,std::initializer_list<int> pitches)
    {
        for(const auto pitch:pitches)closeChords.push_back({start,start+0.5,pitch,0.92f});
    };
    addCloseChord(0.0,{60,64,67});addCloseChord(0.5,{62,65,69});
    addCloseChord(1.0,{64,67,71});addCloseChord(1.5,{65,69,72});
    const auto closeRegions=createChordRegionsFromNotes(closeChords,0.0,2.0);
    expect(closeRegions.size()==4&&closeRegions[0].name=="C"&&closeRegions[1].name=="Dm"
           &&closeRegions[2].name=="Em"&&closeRegions[3].name=="F",
           "sequence decoding preserves adjacent half-beat chords");

    std::vector<PitchedNoteRegion> arpeggiatedProgression;
    const auto addArpeggio=[&](double start,std::initializer_list<int> pitches)
    {
        for(int repetition=0;repetition<4;++repetition)
        {
            size_t voice=0;
            for(const auto pitch:pitches)
            {
                const auto noteStart=start+(double)repetition+(double)voice*0.10;
                arpeggiatedProgression.push_back({noteStart,noteStart+0.07,pitch,0.90f});
                ++voice;
            }
        }
    };
    addArpeggio(0.0,{48,52,55,59});
    addArpeggio(4.0,{53,56,60,63});
    const auto arpeggioRegions=createChordRegionsFromNotes(arpeggiatedProgression,0.0,8.0);
    if(arpeggioRegions.size()!=2)for(const auto& region:arpeggioRegions)
        std::cerr<<"Arpeggio "<<region.startPpq<<'-'<<region.endPpq<<' '<<region.name<<'\n';
    expect(arpeggioRegions.size()==2&&arpeggioRegions[0].name=="Cmaj7"
           &&arpeggioRegions[1].name=="Fm7",
           "decaying harmonic memory combines non-overlapping arpeggio tones");
    if(arpeggioRegions.size()==2)
        expect(arpeggioRegions[1].startPpq>=4.0&&arpeggioRegions[1].startPpq<4.45,
               "an arpeggiated chord change remains aligned to its attack burst");

    std::vector<PitchedNoteRegion> fastStaccatoChords;
    const std::array<std::array<int,3>,4> fastPitches{{{{60,64,67}},{{62,65,69}},
                                                       {{64,67,71}},{{65,69,72}}}};
    for(size_t chordIndex=0;chordIndex<fastPitches.size();++chordIndex)
        for(size_t voice=0;voice<fastPitches[chordIndex].size();++voice)
        {
            const auto noteStart=(double)chordIndex*0.5+(double)voice*0.075;
            fastStaccatoChords.push_back({noteStart,noteStart+0.055,
                                          fastPitches[chordIndex][voice],0.92f});
        }
    const auto fastStaccatoRegions=createChordRegionsFromNotes(fastStaccatoChords,0.0,2.0);
    if(fastStaccatoRegions.size()!=4)for(const auto& region:fastStaccatoRegions)
        std::cerr<<"Fast staccato "<<region.startPpq<<'-'<<region.endPpq<<' '<<region.name<<'\n';
    expect(fastStaccatoRegions.size()==4&&fastStaccatoRegions[0].name=="C"
           &&fastStaccatoRegions[1].name=="Dm"&&fastStaccatoRegions[2].name=="Em"
           &&fastStaccatoRegions[3].name=="F",
           "harmonic memory resets quickly enough for adjacent staccato chords");

    std::vector<PitchedNoteRegion> repeatedSingleTone;
    for(double start=0.0;start<2.0;start+=0.15)
        repeatedSingleTone.push_back({start,start+0.05,60,0.95f});
    expect(createChordRegionsFromNotes(repeatedSingleTone,0.0,2.0).empty(),
           "repeated single notes cannot accumulate into a false chord");

    std::vector<PitchedNoteRegion> transientHarmonic{{0.0,4.0,60,0.9f},{0.0,4.0,64,0.9f},
                                                      {0.0,4.0,67,0.9f},{0.0,4.0,71,0.75f},
                                                      {1.875,2.0,66,0.95f},{1.875,2.0,69,0.75f}};
    const auto transientRegions=createChordRegionsFromNotes(transientHarmonic,0.0,4.0);
    if(transientRegions.size()!=1||transientRegions.front().name!="Cmaj7")for(const auto& region:transientRegions)
        std::cerr<<"Transient "<<region.startPpq<<'-'<<region.endPpq<<' '<<region.name<<'\n';
    expect(transientRegions.size()==1&&transientRegions.front().name=="Cmaj7",
           "sequence decoding rejects a one-slice harmonic transcription burst");

    std::vector<PitchedNoteRegion> inversionCadence;
    const auto addInversion=[&](double start,std::initializer_list<int> pitches)
    {
        for(const auto pitch:pitches)inversionCadence.push_back({start,start+4.0,pitch,0.9f});
    };
    addInversion(0.0,{52,55,59,60});   // Cmaj7/E
    addInversion(4.0,{52,55,57,60});   // Am7/E
    addInversion(8.0,{53,57,60,62});   // Dm7/F
    addInversion(12.0,{53,55,59,62});  // G7/F
    const auto inversionRegions=createChordRegionsFromNotes(inversionCadence,0.0,16.0);
    expect(inversionRegions.size()==4&&inversionRegions[0].name=="Cmaj7/E"
           &&(inversionRegions[1].name=="Am7/E"||inversionRegions[1].name=="C6/E")
           &&(inversionRegions[2].name=="Dm7/F"||inversionRegions[2].name=="F6")
           &&inversionRegions[3].name=="G7/F",
           "sequence decoding retains inversions across a common-tone voice-led cadence");
    if(inversionRegions.size()==4)
        expect((inversionRegions[1].name=="Am7/E"||inversionRegions[1].alternatives.contains("Am7/E"))
               &&(inversionRegions[2].name=="Dm7/F"||inversionRegions[2].alternatives.contains("Dm7/F")),
               "pitch-set-equivalent relative-minor interpretations remain available as candidates");
    std::array<float,12> ambiguousBb7{};
    for(const auto pc:{10,2,5})ambiguousBb7[(size_t)pc]=1.0f;
    ambiguousBb7[8]=0.48f;ambiguousBb7[9]=0.53f;
    std::array<float,12> ebMajorContext{};for(const auto pc:{3,5,7,8,10,0,2})ebMajorContext[(size_t)pc]=1.0f;
    float dominantConfidence=0.0f;
    expect(identifyChord(ambiguousBb7,dominantConfidence,10,nullptr,&ebMajorContext,0.08f)=="Bb7",
           "Eb-major context resolves ambiguous Bb dominant versus major seventh");

    AudioChordStabilizer audioStabilizer;
    expect(audioStabilizer.process("Ebmaj7",0.9f,{},0.0).kind==ChordUpdateKind::start,
           "Audio stabilizer starts an initial seventh chord immediately");
    const auto inversionTail=audioStabilizer.process("Ebmaj7/D",0.65f,{},1.0,false);
    expect(inversionTail.kind==ChordUpdateKind::extend&&inversionTail.chord=="Ebmaj7",
           "bass fluctuation does not split an established chord");
    const auto fMinorChange=audioStabilizer.process("Fm7",0.9f,{},4.0,false);
    expect(fMinorChange.chord=="Fm7"&&fMinorChange.kind==ChordUpdateKind::start,
           "Audio stabilizer accepts a simple chord change even when onset detection misses it");
    juce::StringArray fMinorAlternative;fMinorAlternative.add("Fm7");
    expect(audioStabilizer.process("Abm13",0.55f,fMinorAlternative,6.0).chord=="Fm7",
           "brief complex Audio candidate does not split an established chord");
    expect(audioStabilizer.process("Abm13",0.55f,fMinorAlternative,7.0).chord=="Fm7",
           "repeated noisy candidate still preserves a supported current chord");
    const auto dominantChange=audioStabilizer.process("Bb7",0.9f,{},8.0,false);
    expect(dominantChange.chord=="Bb7"&&dominantChange.kind==ChordUpdateKind::start,
           "next simple dominant replaces the rejected candidate without an onset gate");
    expect(audioStabilizer.process("Abmaj9#11",0.55f,{},10.0,false).chord=="Bb7"
           &&audioStabilizer.process("Abmaj9#11",0.55f,{},11.0,false).chord=="Bb7",
           "two-beat advanced false positive does not split a dominant region");
    expect(audioStabilizer.process("Ebmaj7",0.9f,{},12.0).chord=="Ebmaj7",
           "reported progression resolves back to Eb major seventh");
    expect(audioStabilizer.process("Eb5",0.55f,{},13.0,false).chord=="Ebmaj7",
           "decaying power-chord tail does not split an established seventh chord");
    audioStabilizer.reset();audioStabilizer.process("C",0.9f,{},0.0);
    audioStabilizer.process("Cmaj9",0.8f,{},1.0,false);audioStabilizer.process("Cmaj9",0.8f,{},2.0,false);
    const auto confirmedExtension=audioStabilizer.process("Cmaj9",0.8f,{},3.0,false);
    expect(confirmedExtension.kind==ChordUpdateKind::start&&std::abs(confirmedExtension.regionStartPpq-1.0)<0.000001,
           "sustained advanced Audio chord is confirmed at its original beat");

    MidiChordDetector detector;
    juce::MidiBuffer firstAttack;
    firstAttack.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
    firstAttack.addEvent(juce::MidiMessage::noteOn(1, 64, (juce::uint8)100), 1);
    const auto started = detector.process(firstAttack,0.0);
    expect(started.kind == ChordUpdateKind::start, "first playable attack starts a region");

    juce::MidiBuffer addedFifth;
    addedFifth.addEvent(juce::MidiMessage::noteOn(1, 67, (juce::uint8)100), 0);
    expect(detector.process(addedFifth,0.02).kind==ChordUpdateKind::extend,
           "held-note addition remains provisional during overlap grace");
    const auto refined=detector.process({},0.2);
    expect(refined.kind == ChordUpdateKind::refine, "held-note addition refines after overlap grace");
    expect(refined.chord == "C", "refined attack resolves to C major");

    juce::MidiBuffer release;
    release.addEvent(juce::MidiMessage::noteOff(1, 64), 0);
    expect(detector.process(release,0.3).kind == ChordUpdateKind::extend, "release arms rather than starts a region");

    juce::MidiBuffer nextAttack;
    nextAttack.addEvent(juce::MidiMessage::noteOn(1, 62, (juce::uint8)100), 0);
    expect(detector.process(nextAttack,0.35).kind == ChordUpdateKind::start, "attack after release starts a region");

    MidiChordDetector commonToneDetector;
    juce::MidiBuffer cMajorAttack;
    for(const auto note:{60,64,67})cMajorAttack.addEvent(juce::MidiMessage::noteOn(1,note,(juce::uint8)100),0);
    expect(commonToneDetector.process(cMajorAttack,0.0).kind==ChordUpdateKind::start,"common-tone test starts C major");
    juce::MidiBuffer earlyReplacementNotes;
    earlyReplacementNotes.addEvent(juce::MidiMessage::noteOn(1,65,(juce::uint8)100),0);
    earlyReplacementNotes.addEvent(juce::MidiMessage::noteOn(1,69,(juce::uint8)100),1);
    expect(commonToneDetector.process(earlyReplacementNotes,1.0).kind==ChordUpdateKind::extend,
           "early replacement notes do not rename the old chord");
    juce::MidiBuffer lateOldNoteReleases;
    lateOldNoteReleases.addEvent(juce::MidiMessage::noteOff(1,64),0);
    lateOldNoteReleases.addEvent(juce::MidiMessage::noteOff(1,67),1);
    const auto commonToneChange=commonToneDetector.process(lateOldNoteReleases,1.03);
    expect(commonToneChange.kind==ChordUpdateKind::start,"overlapping common-tone chord starts a new region");
    expect(std::abs(commonToneChange.regionStartPpq-1.0)<0.000001,"common-tone region starts at replacement attack");

    MidiChordDetector sustainDetector;
    juce::MidiBuffer sustainedAttack;
    sustainedAttack.addEvent(juce::MidiMessage::noteOn(1,60,(juce::uint8)100),0);
    sustainedAttack.addEvent(juce::MidiMessage::noteOn(1,64,(juce::uint8)100),1);
    sustainedAttack.addEvent(juce::MidiMessage::noteOn(1,67,(juce::uint8)100),2);
    expect(sustainDetector.process(sustainedAttack,0.0).kind==ChordUpdateKind::start,"sustain test starts a chord");
    juce::MidiBuffer pedalAndRelease;
    pedalAndRelease.addEvent(juce::MidiMessage::controllerEvent(1,64,127),0);
    pedalAndRelease.addEvent(juce::MidiMessage::noteOff(1,60),1);
    pedalAndRelease.addEvent(juce::MidiMessage::noteOff(1,64),2);
    pedalAndRelease.addEvent(juce::MidiMessage::noteOff(1,67),3);
    expect(sustainDetector.process(pedalAndRelease,0.5).kind==ChordUpdateKind::extend,
           "damper pedal extends a released chord");
    juce::MidiBuffer sustainedNextAttack;
    sustainedNextAttack.addEvent(juce::MidiMessage::noteOn(1,62,(juce::uint8)100),0);
    sustainedNextAttack.addEvent(juce::MidiMessage::noteOn(1,65,(juce::uint8)100),1);
    sustainedNextAttack.addEvent(juce::MidiMessage::noteOn(1,69,(juce::uint8)100),2);
    expect(sustainDetector.process(sustainedNextAttack,0.75).kind==ChordUpdateKind::start,
           "new held notes start a chord without pedal-latched pitch pollution");

    auto& session = SharedChordSession::instance();
    session.clear();
    session.updateTransport(9.25,96.0,3,4,false);
    const auto stoppedTransport=session.snapshot();
    expect(std::abs(stoppedTransport.playheadPpq-9.25)<0.000001&&!stoppedTransport.playing
           &&std::abs(stoppedTransport.bpm-96.0)<0.000001&&stoppedTransport.numerator==3,
           "stopped host transport updates are published to every Chordizer view");
    const auto stableTransportRevision=stoppedTransport.revision;
    session.updateTransport(9.25,96.0,3,4,false);
    expect(session.snapshot().revision==stableTransportRevision,
           "unchanged editor transport polling does not churn the shared session");
    session.publishChord(1.0, "C", "MIDI", 1.0f, ChordUpdateKind::start);
    session.publishChord(1.1, "Cmaj7", "MIDI", 1.0f, ChordUpdateKind::refine);
    session.publishChord(1.25, "Dm", "MIDI", 1.0f, ChordUpdateKind::start);
    const auto snapshot = session.snapshot();
    expect(snapshot.regions.size() == 2, "refinement does not create another region");
    expect(snapshot.regions.front().name == "Cmaj7", "refinement renames the active region");
    expect(std::abs(snapshot.regions.front().endPpq - 1.25) < 0.000001, "adjacent regions share a boundary without overlap");
    session.renameRegion(0,"C6");
    expect(session.snapshot().regions.front().name=="C6","manual region rename");
    session.renameRegion(1,"Dm7");
    session.publishChord(1.4,"D7","MIDI",1.0f,ChordUpdateKind::refine);
    expect(session.snapshot().regions[1].name=="Dm7","manual rename is locked against live refinement");
    session.deleteRegion(0);
    expect(session.snapshot().regions.size()==1,"manual region deletion");
    const auto deletedSnapshot=session.snapshot().regions;
    std::vector<ChordRegionData> restoredRegions{{0.0,1.0,"C","Audio",0.8f,{"Cm"},true},
                                                  {1.0,2.0,"G7","MIDI",0.9f,{},false}};
    session.replaceRegions(restoredRegions);
    const auto restoredSnapshot=session.snapshot();
    expect(restoredSnapshot.regions.size()==2&&restoredSnapshot.regions[0].name=="C"
           &&restoredSnapshot.regions[0].source=="Audio"&&restoredSnapshot.regions[0].locked,
           "shared region snapshots restore complete undo state");
    session.replaceRegions(deletedSnapshot);

    session.replaceRegions({{0.0,1.0,"Cmaj7","MIDI",1.0f,{},false},
                            {2.0,3.0,"Fm7","MIDI",1.0f,{},false},
                            {5.0,6.0,"Bb7","MIDI",1.0f,{},false}});
    expect(session.extendRegionToNext(0),"right-click extension fills a gap to the next chord");
    expect(std::abs(session.snapshot().regions[0].endPpq-2.0)<0.000001,
           "gap extension ends exactly at the next chord");
    expect(!session.extendRegionToNext(2),"the final chord cannot auto-extend without a chord to its right");
    expect(session.resizeRegion(1,1.0,4.5),"region edges can be resized");
    const auto resized=session.snapshot().regions;
    expect(std::abs(resized[1].startPpq-2.0)<0.000001&&std::abs(resized[1].endPpq-4.5)<0.000001
           &&resized[1].locked,"resizing clamps against neighbours and locks the manual edit");

    const std::vector<ChordRegionData> midiExportRegions{{2.0,4.0,"Ebmaj7","MIDI",1.0f,{},true},
                                                         {5.0,6.5,"Fm7/C","MIDI",1.0f,{},true}};
    const auto midiFile=createChordizerMidiFile(midiExportRegions,120.0,4,4);
    expect(midiFile.getTimeFormat()==chordizerMidiTicksPerQuarterNote&&midiFile.getNumTracks()==1,
           "MIDI export uses a high-resolution PPQ track");
    const auto* midiTrack=midiFile.getTrack(0);
    auto noteOns=0;double firstOn=-1.0,lastOn=-1.0,lastOff=-1.0;juce::String exportedTrackName;
    if(midiTrack!=nullptr)for(int event=0;event<midiTrack->getNumEvents();++event)
    {
        const auto& message=midiTrack->getEventPointer(event)->message;
        if(message.isTrackNameEvent())exportedTrackName=message.getTextFromTextMetaEvent();
        if(message.isNoteOn())
        {
            if(firstOn<0.0)firstOn=message.getTimeStamp();
            lastOn=message.getTimeStamp();++noteOns;
        }
        if(message.isNoteOff())lastOff=juce::jmax(lastOff,message.getTimeStamp());
    }
    expect(noteOns==9&&std::abs(firstOn)<0.000001
           &&std::abs(lastOn-3.0*chordizerMidiTicksPerQuarterNote)<0.000001
           &&std::abs(lastOff-4.5*chordizerMidiTicksPerQuarterNote)<0.000001,
           "MIDI export preserves selected chord starts, gaps, and durations");
    expect(exportedTrackName=="Ebmaj7 - Fm7/C"&&chordizerMidiExportName(midiExportRegions)==exportedTrackName,
           "MIDI export embeds the selected chord names as the Logic region name");
    const auto slashVoicing=chordizerMidiNotesForName("Fm7/C");
    expect(!slashVoicing.empty()&&slashVoicing.front()==36,
           "MIDI export places a slash bass below the chord voicing");

    session.replaceRegions({{0.0,4.0,"C","Audio",0.6f,{},false},
                            {4.0,8.0,"G7","MIDI",0.95f,{},false},
                            {8.0,12.0,"Am","Audio",1.0f,{},true}});
    session.replaceAudioRegions(0.0,12.0,{{0.0,4.0,"Dm7","Audio",0.9f,{},false},
                                          {4.0,8.0,"F","Audio",0.9f,{},false},
                                          {8.0,12.0,"E7","Audio",0.9f,{},false}});
    const auto neuralReplacement=session.snapshot();
    expect(neuralReplacement.regions.size()==3&&neuralReplacement.regions[0].name=="Dm7",
           "neural refinement replaces unlocked Audio estimates");
    expect(neuralReplacement.regions[1].name=="G7"&&neuralReplacement.regions[1].source=="MIDI",
           "neural refinement preserves overlapping MIDI authority");
    expect(neuralReplacement.regions[2].name=="Am"&&neuralReplacement.regions[2].locked,
           "neural refinement preserves manual Audio edits");

    session.replaceRegions({{0.0,4.0,"Cmaj7","Audio",0.72f,{},false},
                            {4.0,8.0,"G7","Audio",0.72f,{},false}});
    session.replaceAudioRegions(2.0,6.0,{{2.0,3.91,"Cmaj7/E","Audio",0.84f,{},false},
                                         {4.09,6.0,"G7","Audio",0.86f,{},false}});
    const auto stitchedNeural=session.snapshot();
    expect(stitchedNeural.regions.size()==2
           &&stitchedNeural.regions[0].name.upToFirstOccurrenceOf("/",false,false)=="Cmaj7"
           &&stitchedNeural.regions[1].name=="G7",
           "overlapping neural windows merge equivalent inversion labels at their seams");
    if(stitchedNeural.regions.size()==2)
        expect(std::abs(stitchedNeural.regions[0].endPpq-stitchedNeural.regions[1].startPpq)<0.000001,
               "neural window timing jitter is reconciled to one PPQ boundary");
    session.replaceRegions({{0.0,3.75,"Ebmaj7","Audio",0.86f,{},false},
                            {3.75,4.0,"Eb","Audio",0.72f,{},false}});
    session.replaceAudioRegions(4.0,5.0,{{4.0,5.0,"Ebmaj7","Audio",0.84f,{},false}});
    const auto decaySeam=session.snapshot();
    expect(decaySeam.regions.size()==1&&decaySeam.regions.front().name=="Ebmaj7"
           &&std::abs(decaySeam.regions.front().endPpq-5.0)<0.000001,
           "a simple decay label spanning neural windows merges into the surrounding extension");
    session.replaceRegions(deletedSnapshot);

    SharedChordSession peerSession;
    const auto peerID=peerSession.registerInstance();
    peerSession.setTextScale(false,1.7f);
    expect(std::abs(session.snapshot().timelineTextScale-1.7f)<0.001f,"text scale is shared across mapped instances");
    session.publishChord(2.0,"G7","MIDI",0.8f,ChordUpdateKind::start);
    expect(peerSession.snapshot().regions.back().name=="G7","regions are shared across independent mappings");
    peerSession.unregisterInstance(peerID);

    session.clear();
    session.publishChord(4.0,"C","Audio",0.7f,ChordUpdateKind::start);
    expect(session.snapshot().regions.front().source=="Audio","audio-only analysis creates a shared region");
    session.publishChord(4.25,"Cmaj7","MIDI",0.95f,ChordUpdateKind::start);
    auto mergedSnapshot=session.snapshot();
    expect(mergedSnapshot.regions.size()==1&&mergedSnapshot.regions.front().source=="MIDI",
           "MIDI replaces only an overlapping Audio estimate");
    session.publishChord(5.0,"Dm","MIDI",0.95f,ChordUpdateKind::start);
    session.publishChord(4.0,"F","Audio",0.7f,ChordUpdateKind::start);
    mergedSnapshot=session.snapshot();
    expect(mergedSnapshot.regions.size()==2&&mergedSnapshot.regions[0].startPpq<=mergedSnapshot.regions[1].startPpq,
           "delayed Audio observations cannot append out of PPQ order");

    session.clear();
    const auto child=fork();
    if(child==0){setenv("TMPDIR","/tmp/chord-tracker-isolated-host",1);execl(argv[0],argv[0],"--publish-audio",nullptr);_exit(127);}
    int childStatus=0;if(child>0)waitpid(child,&childStatus,0);
    const auto crossProcessSnapshot=session.snapshot();
    expect(child>0&&WIFEXITED(childStatus)&&WEXITSTATUS(childStatus)==0,"cross-process Audio publisher runs");
    expect(crossProcessSnapshot.regions.size()==1&&crossProcessSnapshot.regions.front().name=="Am7"
           &&crossProcessSnapshot.regions.front().source=="Audio",
           "Audio regions are visible through a separately hosted MIDI session with a different temp directory");

    constexpr int frameSize=16384;constexpr double rate=44100.0;
    std::array<float,frameSize> audio{},window{};
    for(int i=0;i<frameSize;++i)
    {
        const auto time=i/rate;window[(size_t)i]=(float)(0.5-0.5*std::cos(2.0*juce::MathConstants<double>::pi*i/(frameSize-1)));
        audio[(size_t)i]=(float)((std::sin(2.0*juce::MathConstants<double>::pi*261.6256*time)
                               +std::sin(2.0*juce::MathConstants<double>::pi*329.6276*time)
                               +std::sin(2.0*juce::MathConstants<double>::pi*391.9954*time))/3.0);
    }
    const auto hpcp=calculateConstantQHpcp(audio.data(),window.data(),frameSize,rate);
    expect(hpcp[0]>0.45f&&hpcp[4]>0.35f&&hpcp[7]>0.35f,"constant-Q HPCP resolves a C-major frame");
    juce::AudioBuffer<float> antiPhaseStereo(2,frameSize);
    antiPhaseStereo.copyFrom(0,0,audio.data(),frameSize);
    antiPhaseStereo.copyFrom(1,0,audio.data(),frameSize);
    antiPhaseStereo.applyGain(1,0,frameSize,-1.0f);
    const auto antiPhasePlan=createPhaseSafeDownmixPlan(antiPhaseStereo);
    std::array<float,frameSize> phaseSafeMono{};
    double phaseSafeEnergy=0.0;
    for(int sample=0;sample<frameSize;++sample)
    {
        phaseSafeMono[(size_t)sample]=phaseSafeDownmixSample(antiPhaseStereo,sample,antiPhasePlan);
        phaseSafeEnergy+=(double)phaseSafeMono[(size_t)sample]*phaseSafeMono[(size_t)sample];
    }
    const auto phaseSafeHpcp=calculateConstantQHpcp(phaseSafeMono.data(),window.data(),frameSize,rate);
    float phaseSafeConfidence=0.0f;
    expect(antiPhasePlan.weightedStereo&&phaseSafeEnergy/(double)frameSize>0.001,
           "phase-safe downmix preserves opposite-polarity stereo energy");
    expect(identifyChord(phaseSafeHpcp,phaseSafeConfidence)=="C",
           "live phase-safe downmix retains the source chord");
    antiPhaseStereo.applyGain(1,0,frameSize,-1.0f);
    const auto inPhasePlan=createPhaseSafeDownmixPlan(antiPhaseStereo);
    expect(!inPhasePlan.weightedStereo
           &&std::abs(phaseSafeDownmixSample(antiPhaseStereo,100,inPhasePlan)-audio[100])<0.000001f,
           "correlated stereo uses the ordinary average");
    const auto harmonicC=identifyHarmonicAudio({130.8128,164.8138,195.9977});
    const auto harmonicDm=identifyHarmonicAudio({146.8324,174.6141,220.0});
    const auto harmonicG7=identifyHarmonicAudio({97.9989,123.4708,146.8324,174.6141});
    if(harmonicC!="C")std::cerr<<"Harmonic C detected as "<<harmonicC<<'\n';
    if(harmonicDm!="Dm")std::cerr<<"Harmonic Dm detected as "<<harmonicDm<<'\n';
    if(harmonicG7!="G7")std::cerr<<"Harmonic G7 detected as "<<harmonicG7<<'\n';
    expect(harmonicC=="C"&&harmonicDm=="Dm"&&harmonicG7=="G7","harmonic-rich audio chord recognition");

    if (failures == 0) std::cout << "Chordizer engine tests passed\n";
    return failures == 0 ? 0 : 1;
}
