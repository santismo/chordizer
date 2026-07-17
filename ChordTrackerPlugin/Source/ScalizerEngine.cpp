#include "ScalizerEngine.h"
#include <algorithm>
#include <cmath>
#include <limits>

namespace
{
constexpr std::array<const char*, 12> flatNames { "C", "Db", "D", "Eb", "E", "F", "Gb", "G", "Ab", "A", "Bb", "B" };
constexpr std::array<const char*, 12> sharpNames { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };

struct ParsedChord
{
    int root = 0;
    juce::String suffix;
    std::array<bool, 12> tones {};
    bool valid = false;
    bool prefersSharps = false;
};

struct ScaleCandidate
{
    int root = 0;
    juce::String kind = "major";
    std::vector<int> intervals;
    float score = -100000.0f;
};

const juce::StringArray& scaleTypeNamesInternal()
{
    static const juce::StringArray names {
        "major","natural minor","harmonic minor","melodic minor",
        "Dorian","Phrygian","Lydian","Mixolydian","Locrian",
        "Lydian dominant","Phrygian dominant","altered","whole tone",
        "half-whole diminished","whole-half diminished"
    };
    return names;
}

std::vector<int> scaleIntervalsForKind(const juce::String& kind)
{
    if(kind.equalsIgnoreCase("major"))return {0,2,4,5,7,9,11};
    if(kind.equalsIgnoreCase("natural minor"))return {0,2,3,5,7,8,10};
    if(kind.equalsIgnoreCase("harmonic minor"))return {0,2,3,5,7,8,11};
    if(kind.equalsIgnoreCase("melodic minor"))return {0,2,3,5,7,9,11};
    if(kind.equalsIgnoreCase("Dorian"))return {0,2,3,5,7,9,10};
    if(kind.equalsIgnoreCase("Phrygian"))return {0,1,3,5,7,8,10};
    if(kind.equalsIgnoreCase("Lydian"))return {0,2,4,6,7,9,11};
    if(kind.equalsIgnoreCase("Mixolydian"))return {0,2,4,5,7,9,10};
    if(kind.equalsIgnoreCase("Locrian"))return {0,1,3,5,6,8,10};
    if(kind.equalsIgnoreCase("Lydian dominant"))return {0,2,4,6,7,9,10};
    if(kind.equalsIgnoreCase("Phrygian dominant"))return {0,1,4,5,7,8,10};
    if(kind.equalsIgnoreCase("altered"))return {0,1,3,4,6,8,10};
    if(kind.equalsIgnoreCase("whole tone"))return {0,2,4,6,8,10};
    if(kind.equalsIgnoreCase("half-whole diminished"))return {0,1,3,4,6,7,9,10};
    if(kind.equalsIgnoreCase("whole-half diminished"))return {0,2,3,5,6,8,9,11};
    return {};
}

int pitchClassForName(const juce::String& text, int& consumed)
{
    consumed = 0;
    if(text.isEmpty()) return -1;
    const auto letter = juce::CharacterFunctions::toUpperCase(text[0]);
    int pitchClass = -1;
    switch(letter)
    {
        case 'C': pitchClass = 0; break;
        case 'D': pitchClass = 2; break;
        case 'E': pitchClass = 4; break;
        case 'F': pitchClass = 5; break;
        case 'G': pitchClass = 7; break;
        case 'A': pitchClass = 9; break;
        case 'B': pitchClass = 11; break;
        default: return -1;
    }
    consumed = 1;
    if(text.length() > 1 && (text[1] == '#' || text[1] == 'b'))
    {
        pitchClass += text[1] == '#' ? 1 : -1;
        consumed = 2;
    }
    return (pitchClass + 12) % 12;
}

std::vector<int> chordIntervalsForSuffix(const juce::String& suffix)
{
    struct Pattern { const char* token; std::initializer_list<int> intervals; };
    static const Pattern patterns[] {
        {"maj13",{0,2,4,7,9,11}}, {"m13",{0,2,3,7,9,10}}, {"13",{0,2,4,7,9,10}},
        {"maj9#11",{0,2,4,6,7,11}}, {"maj9",{0,2,4,7,11}}, {"mMaj9",{0,2,3,7,11}},
        {"m9",{0,2,3,7,10}}, {"9sus4",{0,2,5,7,10}}, {"9",{0,2,4,7,10}},
        {"7b9",{0,1,4,7,10}}, {"7#9",{0,3,4,7,10}}, {"7#11",{0,4,6,7,10}},
        {"7b13",{0,4,7,8,10}}, {"7b5",{0,4,6,10}}, {"7#5",{0,4,8,10}},
        {"6/9",{0,2,4,7,9}}, {"m6/9",{0,2,3,7,9}}, {"add9",{0,2,4,7}},
        {"m(add9)",{0,2,3,7}}, {"maj7#11",{0,4,6,7,11}}, {"mMaj7",{0,3,7,11}},
        {"maj7",{0,4,7,11}}, {"m7",{0,3,7,10}}, {"7sus4",{0,5,7,10}}, {"m7b5",{0,3,6,10}},
        {"dim7",{0,3,6,9}}, {"7",{0,4,7,10}}, {"m6",{0,3,7,9}}, {"6",{0,4,7,9}},
        {"sus4",{0,5,7}}, {"sus2",{0,2,7}}, {"aug",{0,4,8}}, {"dim",{0,3,6}},
        {"m",{0,3,7}}, {"5",{0,7}}, {"",{0,4,7}}
    };
    for(const auto& pattern : patterns)
        if(suffix == pattern.token)
            return { pattern.intervals.begin(), pattern.intervals.end() };
    if(suffix.startsWith("m") && !suffix.startsWith("maj")) return {0,3,7};
    if(suffix.contains("dim")) return {0,3,6};
    if(suffix.contains("aug") || suffix.contains("#5")) return {0,4,8};
    if(suffix.contains("sus2")) return {0,2,7};
    if(suffix.contains("sus")) return {0,5,7};
    return {0,4,7};
}

ParsedChord parseChord(const juce::String& chordName)
{
    ParsedChord result;
    const auto trimmed = chordName.trim();
    int consumed = 0;
    result.root = pitchClassForName(trimmed, consumed);
    if(result.root < 0) return result;
    result.prefersSharps = trimmed.length() > 1 && trimmed[1] == '#';
    auto symbol = trimmed.substring(consumed);
    const auto slash = symbol.indexOfChar('/');
    // Preserve 6/9 while excluding inversion bass labels.
    if(slash >= 0 && !(symbol.startsWith("6/9") || symbol.startsWith("m6/9")))
        symbol = symbol.substring(0, slash);
    result.suffix = symbol;
    for(const auto interval : chordIntervalsForSuffix(symbol))
        result.tones[(size_t)((result.root + interval) % 12)] = true;
    result.valid = true;
    return result;
}

std::array<bool, 12> maskForScale(int root, const std::vector<int>& intervals)
{
    std::array<bool, 12> mask {};
    for(const auto interval : intervals) mask[(size_t)((root + interval) % 12)] = true;
    return mask;
}

int countOutside(const ParsedChord& chord, const std::array<bool, 12>& scale)
{
    auto outside = 0;
    for(size_t pitchClass = 0; pitchClass < 12; ++pitchClass)
        if(chord.tones[pitchClass] && !scale[pitchClass]) ++outside;
    return outside;
}

juce::String scaleName(const ScaleCandidate& candidate, bool sharps)
{
    const auto* rootName = sharps ? sharpNames[(size_t)candidate.root] : flatNames[(size_t)candidate.root];
    return juce::String(rootName) + " " + candidate.kind;
}

bool parseScaleName(const juce::String& name,ScaleCandidate& candidate)
{
    int consumed=0;
    const auto root=pitchClassForName(name.trim(),consumed);
    if(root<0)return false;
    const auto requestedKind=name.trim().substring(consumed).trim();
    for(const auto& kind:scaleTypeNamesInternal())
    {
        if(!kind.equalsIgnoreCase(requestedKind))continue;
        candidate.root=root;candidate.kind=kind;candidate.intervals=scaleIntervalsForKind(kind);
        return !candidate.intervals.empty();
    }
    return false;
}

ScalizerScaleChoice choiceForCandidate(const ScaleCandidate& candidate,const ParsedChord& chord,bool sharps)
{
    ScalizerScaleChoice result;
    result.rootPitchClass=candidate.root;
    result.name=scaleName(candidate,sharps);
    result.scalePitchClasses=maskForScale(candidate.root,candidate.intervals);
    result.chordPitchClasses=chord.tones;
    result.scaleSize=juce::jmin(8,(int)candidate.intervals.size());
    for(int index=0;index<result.scaleSize;++index)
        result.orderedScalePitchClasses[(size_t)index]=(candidate.root+candidate.intervals[(size_t)index])%12;
    result.valid=true;
    return result;
}

std::vector<ScaleCandidate> makeCandidates(const ParsedChord& current)
{
    std::vector<ScaleCandidate> candidates;
    const std::vector<int> major {0,2,4,5,7,9,11};
    const std::vector<int> naturalMinor {0,2,3,5,7,8,10};
    const std::vector<int> harmonicMinor {0,2,3,5,7,8,11};
    const std::vector<int> melodicMinor {0,2,3,5,7,9,11};
    for(int root = 0; root < 12; ++root)
    {
        candidates.push_back({root,"major",major});
        candidates.push_back({root,"natural minor",naturalMinor});
        candidates.push_back({root,"harmonic minor",harmonicMinor});
        candidates.push_back({root,"melodic minor",melodicMinor});
    }
    if(current.valid)
    {
        candidates.push_back({current.root,"Mixolydian",{0,2,4,5,7,9,10}});
        candidates.push_back({current.root,"Dorian",{0,2,3,5,7,9,10}});
        candidates.push_back({current.root,"Phrygian",{0,1,3,5,7,8,10}});
        candidates.push_back({current.root,"Lydian",{0,2,4,6,7,9,11}});
        candidates.push_back({current.root,"Locrian",{0,1,3,5,6,8,10}});
        candidates.push_back({current.root,"Lydian dominant",{0,2,4,6,7,9,10}});
        candidates.push_back({current.root,"Phrygian dominant",{0,1,4,5,7,8,10}});
        candidates.push_back({current.root,"altered",{0,1,3,4,6,8,10}});
        candidates.push_back({current.root,"whole tone",{0,2,4,6,8,10}});
        candidates.push_back({current.root,"half-whole diminished",{0,1,3,4,6,7,9,10}});
        candidates.push_back({current.root,"whole-half diminished",{0,2,3,5,6,8,9,11}});
    }
    return candidates;
}

float typePreference(const ScaleCandidate& candidate, const ParsedChord& chord)
{
    if(!chord.valid || candidate.root != chord.root) return 0.0f;
    const auto suffix = chord.suffix;
    const auto& kind=candidate.kind;
    if(suffix.contains("7b9")&&kind=="half-whole diminished")return 42.0f;
    if(suffix.contains("7#11")&&kind=="Lydian dominant")return 40.0f;
    if(suffix.contains("7b13")&&kind=="Phrygian dominant")return 40.0f;
    if((suffix.contains("7#9")||suffix.contains("7b5"))&&kind=="altered")return 38.0f;
    if((suffix.contains("#5")||suffix.contains("aug"))&&kind=="whole tone")return 36.0f;
    if(suffix.contains("maj")&&suffix.contains("#11")&&kind=="Lydian")return 40.0f;
    if(suffix.contains("mMaj")&&(kind=="melodic minor"||kind=="harmonic minor"))
        return kind=="melodic minor"?34.0f:30.0f;
    if(suffix.contains("m7b5")&&kind=="Locrian")return 40.0f;
    if(suffix.contains("dim")&&kind=="whole-half diminished")return 38.0f;
    if(suffix == "7" || suffix == "9" || suffix == "13" || suffix.contains("sus"))
        return kind=="Mixolydian"?30.0f:0.0f;
    if(suffix.startsWith("m") && !suffix.startsWith("maj"))
        return kind=="natural minor"?22.0f:kind=="Dorian"?20.0f:kind=="melodic minor"?14.0f:0.0f;
    if((suffix.startsWith("maj")||!suffix.startsWith("m"))&&!suffix.contains("dim"))
        return kind=="major"?28.0f:kind=="Lydian"?20.0f:0.0f;
    return 0.0f;
}
}

juce::StringArray scalizerScaleTypeNames()
{
    return scaleTypeNamesInternal();
}

juce::String makeScalizerScaleName(int rootPitchClass,int scaleTypeIndex,bool preferSharps)
{
    const auto& types=scaleTypeNamesInternal();
    if(scaleTypeIndex<0||scaleTypeIndex>=types.size())return {};
    const auto root=(rootPitchClass%12+12)%12;
    return juce::String(preferSharps?sharpNames[(size_t)root]:flatNames[(size_t)root])+" "+types[scaleTypeIndex];
}

ScalizerScaleChoice inferScalizerScale(const std::vector<ChordRegionData>& regions, size_t regionIndex)
{
    ScalizerScaleChoice result;
    if(regionIndex >= regions.size()) return result;
    const auto current = parseChord(regions[regionIndex].name);
    if(!current.valid) return result;
    if(regions[regionIndex].scaleOverride.isNotEmpty())
    {
        ScaleCandidate manual;
        if(parseScaleName(regions[regionIndex].scaleOverride,manual))
            return choiceForCandidate(manual,current,regions[regionIndex].scaleOverride.containsChar('#'));
    }
    const auto previous = regionIndex > 0 ? parseChord(regions[regionIndex - 1].name) : ParsedChord{};
    const auto next = regionIndex + 1 < regions.size() ? parseChord(regions[regionIndex + 1].name) : ParsedChord{};
    auto candidates = makeCandidates(current);
    for(auto& candidate : candidates)
    {
        const auto mask = maskForScale(candidate.root, candidate.intervals);
        auto currentOutside = countOutside(current, mask);
        // The perfect fifth is commonly retained in a written 7#9 voicing even
        // though the altered scale treats it as an avoid/altered degree.
        if(candidate.kind=="altered"&&current.suffix.contains("7#9")
           &&current.tones[(size_t)((current.root+7)%12)]&&!mask[(size_t)((current.root+7)%12)])
            currentOutside=juce::jmax(0,currentOutside-1);
        candidate.score = currentOutside == 0 ? 200.0f : -1000.0f * currentOutside;
        if(candidate.root==current.root)candidate.score+=36.0f;
        candidate.score += typePreference(candidate, current);
        if(previous.valid)
        {
            const auto outside=countOutside(previous,mask);
            candidate.score+=outside==0?1.0f:-1.5f*outside;
        }
        if(next.valid)
        {
            const auto outside=countOutside(next,mask);
            candidate.score+=outside==0?1.5f:-2.0f*outside;
        }
    }
    const auto best = std::max_element(candidates.begin(), candidates.end(),
                                       [](const auto& left, const auto& right){ return left.score < right.score; });
    if(best == candidates.end()) return result;
    return choiceForCandidate(*best,current,current.prefersSharps);
}

std::vector<ScalizerScaleChoice> inferScalizerScales(const std::vector<ChordRegionData>& regions)
{
    std::vector<ScalizerScaleChoice> choices;
    choices.reserve(regions.size());
    for(size_t index = 0; index < regions.size(); ++index) choices.push_back(inferScalizerScale(regions, index));
    return choices;
}

void ScalizerEngine::reset() noexcept
{
    for(auto& channel : activeMappings) for(auto& mapping : channel) mapping.count = 0;
    for(auto& channel : outputRefCounts) channel.fill(0);
}

void ScalizerEngine::releaseAll(juce::MidiBuffer& output, int samplePosition) noexcept
{
    for(int channel = 0; channel < 16; ++channel)
        for(int note = 0; note < 128; ++note)
            if(outputRefCounts[(size_t)channel][(size_t)note] > 0)
                output.addEvent(juce::MidiMessage::noteOff(channel + 1, note), samplePosition);
    reset();
}

int ScalizerEngine::nearestAllowedNote(int note, const std::array<bool, 12>& allowed) noexcept
{
    if(allowed[(size_t)(note % 12)]) return note;
    for(int distance = 1; distance < 12; ++distance)
    {
        const auto down = note - distance;
        const auto up = note + distance;
        if(down >= 0 && allowed[(size_t)(down % 12)]) return down;
        if(up <= 127 && allowed[(size_t)(up % 12)]) return up;
    }
    return note;
}

int ScalizerEngine::degreeMappedNote(int inputNote, const ScalizerScaleChoice& choice,
                                     const std::array<bool, 12>& allowed) noexcept
{
    // Treat consecutive chromatic input keys as consecutive allowed degrees.
    // Anchoring input and output at the active tonic keeps the mapping stable
    // across note attacks while avoiding the repeated steps produced by a
    // nearest-note quantizer.
    std::array<int,12> relativeIntervals {};
    auto allowedCount=0;
    for(int interval=0;interval<12;++interval)
    {
        const auto pitchClass=(choice.rootPitchClass+interval)%12;
        if(allowed[(size_t)pitchClass])relativeIntervals[(size_t)allowedCount++]=interval;
    }
    if(allowedCount==0)return inputNote;

    const auto referenceTonic=60+choice.rootPitchClass;
    const auto degreeOffset=inputNote-referenceTonic;
    auto octaveOffset=degreeOffset/allowedCount;
    auto degreeIndex=degreeOffset%allowedCount;
    if(degreeIndex<0){degreeIndex+=allowedCount;--octaveOffset;}
    const auto outputNote=referenceTonic+octaveOffset*12+relativeIntervals[(size_t)degreeIndex];
    return juce::jlimit(0,127,outputNote);
}

int ScalizerEngine::diatonicHarmonyNote(int note, int degree, bool above,
                                        const ScalizerScaleChoice& choice,
                                        const std::array<bool, 12>& allowed) noexcept
{
    if(degree < 2 || choice.scaleSize < 2) return note;
    // Walk absolute MIDI notes instead of rebuilding the register from the
    // scale root. Rebuilding from pitch classes can add an unwanted octave
    // when the played pitch class is below the root (C/B# in C# major, for
    // example).
    auto target = note;
    auto remainingScaleSteps = degree - 1;
    while(remainingScaleSteps > 0)
    {
        const auto candidate = target + (above ? 1 : -1);
        if(candidate < 0 || candidate > 127) return note;
        target = candidate;
        if(choice.scalePitchClasses[(size_t)(target % 12)]) --remainingScaleSteps;
    }
    if(allowed[(size_t)(target % 12)]) return target;
    // In chord lock mode, move in the selected direction until a chord tone is reached.
    for(int distance = 1; distance < 12; ++distance)
    {
        const auto candidate = target + (above ? distance : -distance);
        if(candidate >= 0 && candidate <= 127 && allowed[(size_t)(candidate % 12)]) return candidate;
    }
    return nearestAllowedNote(target, allowed);
}

void ScalizerEngine::addOutputNote(juce::MidiBuffer& output, int channel, int note, float velocity,
                                   int samplePosition, ActiveMapping& mapping) noexcept
{
    note = juce::jlimit(0, 127, note);
    for(int index = 0; index < mapping.count; ++index)
        if(mapping.outputNotes[(size_t)index] == note) return;
    if(mapping.count >= (int)mapping.outputNotes.size()) return;
    auto& references = outputRefCounts[(size_t)(channel - 1)][(size_t)note];
    if(references == 0) output.addEvent(juce::MidiMessage::noteOn(channel, note, velocity), samplePosition);
    if(references < std::numeric_limits<unsigned short>::max()) ++references;
    mapping.outputNotes[(size_t)mapping.count++] = note;
}

void ScalizerEngine::releaseMapping(juce::MidiBuffer& output, int channel, ActiveMapping& mapping,
                                    float velocity, int samplePosition) noexcept
{
    for(int index = 0; index < mapping.count; ++index)
    {
        const auto note = mapping.outputNotes[(size_t)index];
        auto& references = outputRefCounts[(size_t)(channel - 1)][(size_t)note];
        if(references > 0 && --references == 0)
            output.addEvent(juce::MidiMessage::noteOff(channel, note, velocity), samplePosition);
    }
    mapping.count = 0;
}

void ScalizerEngine::process(juce::MidiBuffer& midi,
                             const std::vector<ChordRegionData>& regions,
                             double blockStartPpq,
                             double bpm,
                             double sampleRate,
                             ScalizerLockMode lockMode,
                             const std::array<ScalizerHarmonyVoice, 3>& voices)
{
    if(regions.empty())
    {
        releaseAll(midi,0);
        return;
    }
    const auto choices = inferScalizerScales(regions);
    juce::MidiBuffer output;
    for(const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        const auto samplePosition = metadata.samplePosition;
        const auto ppq = blockStartPpq + (sampleRate > 0.0 ? samplePosition / sampleRate * bpm / 60.0 : 0.0);
        size_t regionIndex = regions.size();
        for(size_t index = 0; index < regions.size(); ++index)
            if(ppq >= regions[index].startPpq && ppq < regions[index].endPpq) { regionIndex = index; break; }

        if(message.isNoteOn())
        {
            const auto channel = message.getChannel();
            const auto inputNote = message.getNoteNumber();
            auto& mapping = activeMappings[(size_t)(channel - 1)][(size_t)inputNote];
            if(mapping.count > 0) releaseMapping(output, channel, mapping, 0.0f, samplePosition);
            if(regionIndex >= choices.size() || !choices[regionIndex].valid)
            {
                output.addEvent(message, samplePosition);
                mapping.outputNotes[0] = inputNote;
                mapping.count = -1; // passthrough note; its original note-off must pass through too
                continue;
            }
            const auto& choice = choices[regionIndex];
            const auto& allowed = lockMode == ScalizerLockMode::scale
                                    ? choice.scalePitchClasses : choice.chordPitchClasses;
            const auto corrected = degreeMappedNote(inputNote,choice,allowed);
            addOutputNote(output, channel, corrected, message.getFloatVelocity(), samplePosition, mapping);
            for(const auto& voice : voices)
                if(voice.degree >= 2)
                    addOutputNote(output, channel,
                                  diatonicHarmonyNote(corrected, voice.degree, voice.above, choice, allowed),
                                  message.getFloatVelocity(), samplePosition, mapping);
            continue;
        }
        if(message.isNoteOff())
        {
            const auto channel = message.getChannel();
            auto& mapping = activeMappings[(size_t)(channel - 1)][(size_t)message.getNoteNumber()];
            if(mapping.count == -1)
            {
                output.addEvent(message, samplePosition);
                mapping.count = 0;
            }
            else if(mapping.count > 0)
                releaseMapping(output, channel, mapping, message.getFloatVelocity(), samplePosition);
            else
                output.addEvent(message, samplePosition);
            continue;
        }
        if(message.isAllNotesOff() || message.isAllSoundOff())
        {
            releaseAll(output, samplePosition);
            output.addEvent(message, samplePosition);
            continue;
        }
        output.addEvent(message, samplePosition);
    }
    midi.swapWith(output);
}
