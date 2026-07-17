#include "MidiImport.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace
{
constexpr double minimumRegionPpq = 0.03125;

struct TimedMidiEvent
{
    double ppq = 0.0;
    juce::MidiMessage message;
};

struct ActiveNote
{
    double startPpq = 0.0;
    float confidence = 1.0f;
};

struct RawNote
{
    double startPpq = 0.0, endPpq = 0.0;
    int midiNote = 60;
    float confidence = 1.0f;
};

bool overlaps(double leftStart,double leftEnd,double rightStart,double rightEnd)
{
    return leftStart < rightEnd && leftEnd > rightStart;
}

bool usableDuration(double start,double end)
{
    return end - start >= minimumRegionPpq;
}

void addIfUsable(std::vector<ChordRegionData>& regions,ChordRegionData region)
{
    if(region.name.isEmpty() || region.name == "--" || !usableDuration(region.startPpq,region.endPpq))
        return;
    regions.push_back(std::move(region));
}

void sortAndMerge(std::vector<ChordRegionData>& regions)
{
    std::sort(regions.begin(),regions.end(),[](const auto& left,const auto& right)
    {
        if(left.startPpq < right.startPpq) return true;
        if(left.startPpq > right.startPpq) return false;
        if(left.locked != right.locked) return left.locked;
        return left.endPpq < right.endPpq;
    });

    std::vector<ChordRegionData> merged;
    merged.reserve(regions.size());
    for(auto& region:regions)
    {
        if(!merged.empty() && !merged.back().locked && !region.locked
           && merged.back().source == region.source && merged.back().name == region.name
           && region.startPpq <= merged.back().endPpq + 0.000001)
        {
            merged.back().endPpq = juce::jmax(merged.back().endPpq,region.endPpq);
            merged.back().confidence = juce::jmax(merged.back().confidence,region.confidence);
            if(!region.alternatives.isEmpty()) merged.back().alternatives = region.alternatives;
        }
        else merged.push_back(std::move(region));
    }
    regions = std::move(merged);
}

std::vector<RawNote> extractMidiNotes(juce::MidiFile& midiFile,const int ticksPerQuarter)
{
    std::vector<RawNote> notes;
    for(int trackIndex=0;trackIndex<midiFile.getNumTracks();++trackIndex)
    {
        const auto* track = midiFile.getTrack(trackIndex);
        if(track == nullptr) continue;

        std::vector<TimedMidiEvent> events;
        events.reserve((size_t) track->getNumEvents());
        auto lastPpq = 0.0;
        for(int eventIndex=0;eventIndex<track->getNumEvents();++eventIndex)
        {
            const auto* holder = track->getEventPointer(eventIndex);
            if(holder == nullptr) continue;
            const auto ppq = holder->message.getTimeStamp() / (double) ticksPerQuarter;
            lastPpq = juce::jmax(lastPpq,ppq);
            if(!holder->message.isNoteOn() && !holder->message.isNoteOff()) continue;
            events.push_back({ppq,holder->message});
        }

        std::sort(events.begin(),events.end(),[](const auto& left,const auto& right)
        {
            if(left.ppq < right.ppq) return true;
            if(left.ppq > right.ppq) return false;
            if(left.message.isNoteOff() != right.message.isNoteOff())
                return left.message.isNoteOff();
            return left.message.getNoteNumber() < right.message.getNoteNumber();
        });

        std::array<std::vector<ActiveNote>,16 * 128> activeNotes;
        const auto activeIndex=[](const juce::MidiMessage& message)
        {
            const auto channel = juce::jlimit(1,16,message.getChannel());
            const auto note = juce::jlimit(0,127,message.getNoteNumber());
            return (size_t) ((channel - 1) * 128 + note);
        };

        for(const auto& event:events)
        {
            const auto& message = event.message;
            if(message.isNoteOn())
            {
                activeNotes[activeIndex(message)].push_back({
                    event.ppq,
                    juce::jlimit(0.05f,1.0f,message.getFloatVelocity())});
            }
            else if(message.isNoteOff())
            {
                auto& stack = activeNotes[activeIndex(message)];
                if(stack.empty()) continue;
                const auto started = stack.front();
                stack.erase(stack.begin());
                if(event.ppq > started.startPpq)
                    notes.push_back({started.startPpq,event.ppq,message.getNoteNumber(),started.confidence});
            }
        }

        for(size_t index=0;index<activeNotes.size();++index)
            for(const auto& started:activeNotes[index])
            {
                const auto fallbackEnd = juce::jmax(lastPpq,started.startPpq + 0.25);
                if(fallbackEnd > started.startPpq)
                    notes.push_back({started.startPpq,fallbackEnd,(int) (index % 128),started.confidence});
            }
    }

    std::sort(notes.begin(),notes.end(),[](const auto& left,const auto& right)
    {
        if(left.startPpq < right.startPpq) return true;
        if(left.startPpq > right.startPpq) return false;
        return left.midiNote < right.midiNote;
    });
    return notes;
}

ChordizerMidiImportResult importMidiStream(juce::InputStream& input,double insertAtPpq)
{
    ChordizerMidiImportResult result;
    result.startPpq = juce::jmax(0.0,insertAtPpq);
    result.endPpq = result.startPpq;

    juce::MidiFile midiFile;
    if(!midiFile.readFrom(input))
    {
        result.error = "The MIDI data is not a readable Standard MIDI file.";
        return result;
    }

    const auto ticksPerQuarter = midiFile.getTimeFormat();
    if(ticksPerQuarter <= 0)
    {
        result.error = "SMPTE-timed MIDI files are not supported yet. Export a PPQ MIDI file from Logic.";
        return result;
    }

    const auto rawNotes = extractMidiNotes(midiFile,ticksPerQuarter);
    result.noteCount = (int) rawNotes.size();
    if(rawNotes.empty())
    {
        result.error = "No MIDI notes were found in the file.";
        return result;
    }

    const auto firstStart = std::min_element(rawNotes.begin(),rawNotes.end(),[](const auto& left,const auto& right)
    {
        return left.startPpq < right.startPpq;
    })->startPpq;
    const auto lastEnd = std::max_element(rawNotes.begin(),rawNotes.end(),[](const auto& left,const auto& right)
    {
        return left.endPpq < right.endPpq;
    })->endPpq;

    std::vector<PitchedNoteRegion> shiftedNotes;
    shiftedNotes.reserve(rawNotes.size());
    for(const auto& note:rawNotes)
    {
        const auto start = result.startPpq + (note.startPpq - firstStart);
        const auto end = result.startPpq + (note.endPpq - firstStart);
        if(end > start)
            shiftedNotes.push_back({start,end,note.midiNote,note.confidence});
    }
    result.endPpq = result.startPpq + juce::jmax(0.0,lastEnd - firstStart);

    result.regions = createChordRegionsFromNotes(shiftedNotes,result.startPpq,result.endPpq);
    for(auto& region:result.regions)
    {
        region.source = "MIDI";
        region.locked = false;
    }
    if(result.regions.empty())
        result.error = "No chord regions could be detected from the MIDI notes.";
    return result;
}
}

bool chordizerIsMidiImportFile(const juce::File& file)
{
    const auto extension = file.getFileExtension();
    return extension.equalsIgnoreCase(".mid") || extension.equalsIgnoreCase(".midi")
        || extension.equalsIgnoreCase(".smf");
}

ChordizerMidiImportResult importChordizerMidiFile(const juce::File& file,double insertAtPpq)
{
    ChordizerMidiImportResult result;
    result.startPpq = juce::jmax(0.0,insertAtPpq);
    result.endPpq = result.startPpq;

    if(!chordizerIsMidiImportFile(file))
    {
        result.error = "Chordizer can import .mid and .midi files.";
        return result;
    }
    if(!file.existsAsFile())
    {
        result.error = "The MIDI file could not be found.";
        return result;
    }

    juce::FileInputStream input(file);
    if(!input.openedOk())
    {
        result.error = "The MIDI file could not be opened.";
        return result;
    }

    return importMidiStream(input,insertAtPpq);
}

ChordizerMidiImportResult importChordizerMidiData(const void* data,size_t bytes,double insertAtPpq)
{
    ChordizerMidiImportResult result;
    result.startPpq = juce::jmax(0.0,insertAtPpq);
    result.endPpq = result.startPpq;
    if(data == nullptr || bytes == 0)
    {
        result.error = "The MIDI data is empty.";
        return result;
    }
    juce::MemoryInputStream input(data,bytes,false);
    return importMidiStream(input,insertAtPpq);
}

std::vector<ChordRegionData> mergeChordizerImportedMidiRegions(
    const std::vector<ChordRegionData>& existing,
    const std::vector<ChordRegionData>& imported)
{
    if(imported.empty()) return existing;

    const auto importStart = std::min_element(imported.begin(),imported.end(),[](const auto& left,const auto& right)
    {
        return left.startPpq < right.startPpq;
    })->startPpq;
    const auto importEnd = std::max_element(imported.begin(),imported.end(),[](const auto& left,const auto& right)
    {
        return left.endPpq < right.endPpq;
    })->endPpq;

    std::vector<ChordRegionData> result;
    std::vector<ChordRegionData> lockedBlockers;
    result.reserve(existing.size() + imported.size());

    for(const auto& region:existing)
    {
        if(!overlaps(region.startPpq,region.endPpq,importStart,importEnd))
        {
            addIfUsable(result,region);
            continue;
        }

        if(region.locked)
        {
            addIfUsable(result,region);
            lockedBlockers.push_back(region);
            continue;
        }

        if(region.startPpq < importStart)
        {
            auto prefix = region;
            prefix.endPpq = juce::jmin(prefix.endPpq,importStart);
            addIfUsable(result,std::move(prefix));
        }
        if(region.endPpq > importEnd)
        {
            auto suffix = region;
            suffix.startPpq = juce::jmax(suffix.startPpq,importEnd);
            addIfUsable(result,std::move(suffix));
        }
    }

    for(auto importedRegion:imported)
    {
        importedRegion.source = "MIDI";
        importedRegion.locked = false;
        std::vector<ChordRegionData> segments { importedRegion };
        for(const auto& blocker:lockedBlockers)
        {
            std::vector<ChordRegionData> nextSegments;
            nextSegments.reserve(segments.size() + 1);
            for(const auto& segment:segments)
            {
                if(!overlaps(segment.startPpq,segment.endPpq,blocker.startPpq,blocker.endPpq))
                {
                    addIfUsable(nextSegments,segment);
                    continue;
                }
                if(segment.startPpq < blocker.startPpq)
                {
                    auto prefix = segment;
                    prefix.endPpq = juce::jmin(prefix.endPpq,blocker.startPpq);
                    addIfUsable(nextSegments,std::move(prefix));
                }
                if(segment.endPpq > blocker.endPpq)
                {
                    auto suffix = segment;
                    suffix.startPpq = juce::jmax(suffix.startPpq,blocker.endPpq);
                    addIfUsable(nextSegments,std::move(suffix));
                }
            }
            segments = std::move(nextSegments);
        }
        for(auto& segment:segments) addIfUsable(result,std::move(segment));
    }

    sortAndMerge(result);
    return result;
}
