#include "MidiExport.h"
#include <array>
#include <cmath>

namespace
{
struct ChordShape
{
    const char* suffix;
    std::initializer_list<int> intervals;
};

const std::array<ChordShape,35> shapes{{
    {"maj9#11",{0,4,7,11,14,18}}, {"maj13",{0,4,7,11,14,21}},
    {"mMaj9",{0,3,7,11,14}}, {"m13",{0,3,7,10,14,21}}, {"13",{0,4,7,10,14,21}},
    {"maj9",{0,4,7,11,14}}, {"9sus4",{0,5,7,10,14}}, {"7#11",{0,4,7,10,18}},
    {"7b13",{0,4,7,10,20}}, {"maj7#11",{0,4,7,11,18}}, {"mMaj7",{0,3,7,11}},
    {"m6/9",{0,3,7,9,14}}, {"6/9",{0,4,7,9,14}}, {"m(add9)",{0,3,7,14}},
    {"add9",{0,4,7,14}}, {"7sus4",{0,5,7,10}}, {"m7b5",{0,3,6,10}},
    {"dim7",{0,3,6,9}}, {"7b9",{0,4,7,10,13}}, {"7#9",{0,4,7,10,15}},
    {"7b5",{0,4,6,10}}, {"7#5",{0,4,8,10}}, {"m9",{0,3,7,10,14}},
    {"9",{0,4,7,10,14}}, {"maj7",{0,4,7,11}}, {"m7",{0,3,7,10}},
    {"sus4",{0,5,7}}, {"sus2",{0,2,7}}, {"aug",{0,4,8}}, {"dim",{0,3,6}},
    {"m6",{0,3,7,9}}, {"6",{0,4,7,9}}, {"7",{0,4,7,10}}, {"5",{0,7}},
    {"m",{0,3,7}}
}};

int pitchClassForName(juce::String name)
{
    name=name.trim();
    static constexpr const char* names[]{"C","C#","D","Eb","E","F","F#","G","Ab","A","Bb","B"};
    for(int pitchClass=0;pitchClass<12;++pitchClass)if(name==names[pitchClass])return pitchClass;
    if(name=="Db")return 1;if(name=="D#")return 3;if(name=="Gb")return 6;
    if(name=="G#")return 8;if(name=="A#")return 10;if(name=="Cb")return 11;
    if(name=="B#")return 0;if(name=="Fb")return 4;if(name=="E#")return 5;
    return -1;
}

int rootLength(const juce::String& name)
{
    if(name.isEmpty()||!juce::String("ABCDEFG").containsChar(name[0]))return 0;
    return name.length()>1&&(name[1]=='#'||name[1]=='b')?2:1;
}
}

std::vector<int> chordizerMidiNotesForName(const juce::String& chordName)
{
    const auto length=rootLength(chordName);
    if(length==0)return {};
    const auto root=pitchClassForName(chordName.substring(0,length));
    if(root<0)return {};
    auto suffix=chordName.substring(length).trim();
    auto bass=-1;
    const auto slash=suffix.lastIndexOfChar('/');
    if(slash>=0)
    {
        const auto parsedBass=pitchClassForName(suffix.substring(slash+1));
        if(parsedBass>=0)
        {
            bass=parsedBass;
            suffix=suffix.substring(0,slash);
        }
    }

    std::initializer_list<int> intervals{0,4,7};
    for(const auto& shape:shapes)
        if(suffix==shape.suffix)
        {
            intervals=shape.intervals;
            break;
        }
    std::vector<int> notes;
    if(bass>=0&&bass!=root)notes.push_back(36+bass);
    const auto rootMidi=48+root;
    for(const auto interval:intervals)notes.push_back(rootMidi+interval);
    return notes;
}

juce::String chordizerMidiExportName(const std::vector<ChordRegionData>& regions)
{
    juce::StringArray names;
    for(const auto& region:regions)if(!region.name.isEmpty())names.add(region.name);
    auto result=names.joinIntoString(" - ").trim();
    return result.isNotEmpty()?result.substring(0,120):"Chordizer Chords";
}

juce::MidiFile createChordizerMidiFile(const std::vector<ChordRegionData>& regions,double bpm,
                                       int numerator,int denominator)
{
    juce::MidiFile file;
    file.setTicksPerQuarterNote(chordizerMidiTicksPerQuarterNote);
    if(regions.empty())return file;
    const auto origin=regions.front().startPpq;
    juce::MidiMessageSequence track;
    auto trackName=juce::MidiMessage::textMetaEvent(3,chordizerMidiExportName(regions));
    trackName.setTimeStamp(0.0);track.addEvent(trackName);
    auto tempo=juce::MidiMessage::tempoMetaEvent((int)std::llround(60000000.0/juce::jlimit(1.0,999.0,bpm)));
    tempo.setTimeStamp(0.0);track.addEvent(tempo);
    auto signature=juce::MidiMessage::timeSignatureMetaEvent(juce::jmax(1,numerator),
                                                              juce::jmax(1,denominator));
    signature.setTimeStamp(0.0);track.addEvent(signature);

    auto lastTick=0.0;
    for(const auto& region:regions)
    {
        const auto startTick=std::llround((region.startPpq-origin)*chordizerMidiTicksPerQuarterNote);
        const auto endTick=juce::jmax((double)startTick+1.0,
            (double)std::llround((region.endPpq-origin)*chordizerMidiTicksPerQuarterNote));
        for(const auto note:chordizerMidiNotesForName(region.name))
        {
            auto on=juce::MidiMessage::noteOn(1,note,(juce::uint8)96);on.setTimeStamp((double)startTick);
            auto off=juce::MidiMessage::noteOff(1,note);off.setTimeStamp(endTick);
            track.addEvent(on);track.addEvent(off);
        }
        lastTick=juce::jmax(lastTick,endTick);
    }
    auto end=juce::MidiMessage::endOfTrack();end.setTimeStamp(lastTick);track.addEvent(end);
    track.sort();file.addTrack(track);
    return file;
}

bool writeChordizerMidiFile(const std::vector<ChordRegionData>& regions,double bpm,
                            int numerator,int denominator,const juce::File& destination)
{
    if(regions.empty()||!destination.getParentDirectory().createDirectory())return false;
    destination.deleteFile();
    juce::FileOutputStream output(destination);
    if(!output.openedOk())return false;
    return createChordizerMidiFile(regions,bpm,numerator,denominator).writeTo(output,1);
}

double scalizerRecordingBarOrigin(const ScalizerRecordingTake& take) noexcept
{
    if(take.firstPpq<0.0)return 0.0;
    const auto barLength=juce::jmax(0.0625,(double)juce::jmax(1,take.numerator)*4.0
                                             /(double)juce::jmax(1,take.denominator));
    return juce::jmax(0.0,std::floor((take.firstPpq+1.0e-9)/barLength)*barLength);
}

juce::MidiFile createScalizerRecordingMidiFile(const ScalizerRecordingTake& take)
{
    juce::MidiFile file;file.setTicksPerQuarterNote(chordizerMidiTicksPerQuarterNote);
    if(take.events.empty())return file;
    const auto origin=scalizerRecordingBarOrigin(take);
    juce::MidiMessageSequence track;
    auto trackName=juce::MidiMessage::textMetaEvent(3,"Chordizer Scalizer Take");
    trackName.setTimeStamp(0.0);track.addEvent(trackName);
    auto tempo=juce::MidiMessage::tempoMetaEvent((int)std::llround(60000000.0/juce::jlimit(1.0,999.0,take.bpm)));
    tempo.setTimeStamp(0.0);track.addEvent(tempo);
    auto signature=juce::MidiMessage::timeSignatureMetaEvent(juce::jmax(1,take.numerator),
                                                              juce::jmax(1,take.denominator));
    signature.setTimeStamp(0.0);track.addEvent(signature);

    std::array<std::array<int,128>,16> activeNotes {};
    auto lastTick=0.0;
    for(const auto& recorded:take.events)
    {
        if(recorded.size<1||recorded.size>3)continue;
        const auto tick=(double)juce::jmax<int64_t>(0,std::llround((recorded.ppq-origin)*chordizerMidiTicksPerQuarterNote));
        juce::MidiMessage message(recorded.data.data(),(int)recorded.size,tick);
        if(message.getChannel()>=1&&message.getChannel()<=16)
        {
            auto& active=activeNotes[(size_t)(message.getChannel()-1)][(size_t)message.getNoteNumber()];
            if(message.isNoteOn())++active;
            else if(message.isNoteOff()&&active>0)--active;
        }
        track.addEvent(message);lastTick=juce::jmax(lastTick,tick);
    }
    const auto takeEnd=take.endPpq>=0.0?take.endPpq:take.events.back().ppq;
    const auto closingTick=(double)juce::jmax<int64_t>((int64_t)lastTick+1,
        std::llround((takeEnd-origin)*chordizerMidiTicksPerQuarterNote));
    for(int channel=0;channel<16;++channel)for(int note=0;note<128;++note)
        for(int outstanding=0;outstanding<activeNotes[(size_t)channel][(size_t)note];++outstanding)
        {
            auto off=juce::MidiMessage::noteOff(channel+1,note);off.setTimeStamp(closingTick);track.addEvent(off);
        }
    auto end=juce::MidiMessage::endOfTrack();end.setTimeStamp(closingTick+1.0);track.addEvent(end);
    track.sort();file.addTrack(track);return file;
}

bool writeScalizerRecordingMidiFile(const ScalizerRecordingTake& take,const juce::File& destination)
{
    if(take.events.empty()||!destination.getParentDirectory().createDirectory())return false;
    destination.deleteFile();juce::FileOutputStream output(destination);
    return output.openedOk()&&createScalizerRecordingMidiFile(take).writeTo(output,1);
}
