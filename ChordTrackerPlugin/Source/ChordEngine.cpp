#include "ChordEngine.h"
#include <algorithm>
#include <cmath>
#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <limits>
#include <numeric>
#include <sys/file.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

namespace
{
constexpr uint32_t sharedMagic=0x43545348, sharedVersion=4;
constexpr size_t maximumSharedRegions=2048, wireNameLength=48, maximumAlternatives=4;

struct SharedRegionWire
{
    double startPpq=0.0,endPpq=0.0;
    float confidence=1.0f;
    uint8_t source=0,locked=0;
    char name[wireNameLength]{};
    char alternatives[maximumAlternatives][wireNameLength]{};
};

struct SharedSessionWire
{
    uint32_t magic=0,version=0;
    pthread_mutex_t mutex{};
    uint64_t revision=1,nextID=1;
    int32_t instances=0,numerator=4,denominator=4;
    double playhead=0.0,bpm=120.0,lastMidiPpq=-1000.0;
    uint8_t playing=0;
    float timelineTextScale=1.0f,leadTextScale=1.0f;
    uint32_t regionCount=0;
    SharedRegionWire regions[maximumSharedRegions]{};
};

struct WireLock
{
    explicit WireLock(SharedSessionWire* state,bool tryOnly=false):wire(state)
    {
        if(wire==nullptr)return;
        const auto result=tryOnly?pthread_mutex_trylock(&wire->mutex):pthread_mutex_lock(&wire->mutex);
        if(result==0)locked=true;
    }
    ~WireLock(){if(locked)pthread_mutex_unlock(&wire->mutex);}
    SharedSessionWire* wire=nullptr;
    bool locked=false;
};

void copyWireString(char* destination,const juce::String& source)
{
    std::memset(destination,0,wireNameLength);
    const auto utf8=source.toRawUTF8();
    std::strncpy(destination,utf8,wireNameLength-1);
}

ChordRegionData fromWire(const SharedRegionWire& wire)
{
    ChordRegionData result;
    result.startPpq=wire.startPpq; result.endPpq=wire.endPpq; result.confidence=wire.confidence;
    result.name=juce::String::fromUTF8(wire.name); result.source=wire.source==1?"Audio":"MIDI"; result.locked=wire.locked!=0;
    for(size_t i=0;i<maximumAlternatives;++i) if(wire.alternatives[i][0]!=0) result.alternatives.add(juce::String::fromUTF8(wire.alternatives[i]));
    return result;
}

void setWireAlternatives(SharedRegionWire& wire,const juce::StringArray& alternatives)
{
    for(size_t i=0;i<maximumAlternatives;++i)
        copyWireString(wire.alternatives[i],i<(size_t)alternatives.size()?alternatives[(int)i]:juce::String{});
}

bool coversPpq(const SharedRegionWire& region,double ppq,double tolerance=0.0625)
{
    return ppq>=region.startPpq-tolerance
        &&ppq<juce::jmax(region.endPpq,region.startPpq+0.03125);
}

bool overlapsRange(const SharedRegionWire& region,double startPpq,double endPpq)
{
    const auto regionEnd=juce::jmax(region.endPpq,region.startPpq+0.03125);
    return region.startPpq<endPpq&&regionEnd>startPpq;
}

void eraseWireRegion(SharedSessionWire& wire,uint32_t index)
{
    std::memmove(wire.regions+index,wire.regions+index+1,
                 (wire.regionCount-index-1)*sizeof(SharedRegionWire));
    --wire.regionCount;
}

uint32_t insertionIndex(const SharedSessionWire& wire,double ppq)
{
    uint32_t index=0;
    while(index<wire.regionCount&&wire.regions[index].startPpq<=ppq)++index;
    return index;
}
}

SharedChordSession& SharedChordSession::instance() { static SharedChordSession value; return value; }
SharedChordSession::SharedChordSession()
{
    auto fileName=juce::String("Shared Session");
    if(const auto* suffix=std::getenv("CHORDIZER_SESSION_SUFFIX"))
        fileName+="-"+juce::String(suffix).retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-");
    const auto directory=juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
        .getChildFile("Application Support").getChildFile("Santismo").getChildFile("Chordizer");
    if(!directory.createDirectory())return;
    const auto path=directory.getChildFile(fileName+".session");
    sharedFile=::open(path.getFullPathName().toRawUTF8(),O_RDWR|O_CREAT,0600);
    if(sharedFile<0)return;
    ::flock(sharedFile,LOCK_EX);
    if(::ftruncate(sharedFile,(off_t)sizeof(SharedSessionWire))!=0){::flock(sharedFile,LOCK_UN);::close(sharedFile);sharedFile=-1;return;}
    auto* wire=(SharedSessionWire*)::mmap(nullptr,sizeof(SharedSessionWire),PROT_READ|PROT_WRITE,MAP_SHARED,sharedFile,0);
    if(wire==MAP_FAILED){::flock(sharedFile,LOCK_UN);::close(sharedFile);sharedFile=-1;return;}
    if(wire->magic!=sharedMagic||wire->version!=sharedVersion)
    {
        std::memset(wire,0,sizeof(SharedSessionWire));
        pthread_mutexattr_t attributes{}; pthread_mutexattr_init(&attributes);
        pthread_mutexattr_setpshared(&attributes,PTHREAD_PROCESS_SHARED);
#if defined(PTHREAD_MUTEX_ROBUST)
        pthread_mutexattr_setrobust(&attributes,PTHREAD_MUTEX_ROBUST);
#endif
        pthread_mutex_init(&wire->mutex,&attributes); pthread_mutexattr_destroy(&attributes);
        wire->version=sharedVersion; wire->revision=1; wire->nextID=1; wire->numerator=4; wire->denominator=4;
        wire->bpm=120.0; wire->lastMidiPpq=-1000.0; wire->timelineTextScale=1.0f; wire->leadTextScale=1.0f;
        wire->magic=sharedMagic;
    }
    sharedMemory=wire;
    ::flock(sharedFile,LOCK_UN);
}

SharedChordSession::~SharedChordSession()
{
    if(sharedMemory!=nullptr)::munmap(sharedMemory,sizeof(SharedSessionWire));
    if(sharedFile>=0)::close(sharedFile);
}

uint64_t SharedChordSession::registerInstance()
{
    if(auto* wire=(SharedSessionWire*)sharedMemory)
    {
        WireLock lock(wire);
        if(lock.locked)
        {
            if(wire->instances==0){wire->regionCount=0;wire->playhead=0.0;wire->playing=0;wire->lastMidiPpq=-1000.0;}
            ++wire->instances;++wire->revision;return wire->nextID++;
        }
    }
    std::lock_guard lock(mutex);++instances;++revision;return nextID++;
}
void SharedChordSession::unregisterInstance(uint64_t)
{
    if(auto* wire=(SharedSessionWire*)sharedMemory){WireLock lock(wire);if(lock.locked){wire->instances=juce::jmax(0,wire->instances-1);++wire->revision;return;}}
    std::lock_guard lock(mutex);instances=juce::jmax(0,instances-1);++revision;
}

void SharedChordSession::publishChord(double ppq, const juce::String& chord, const juce::String& source,
                                      float confidence, ChordUpdateKind kind, const juce::StringArray& alternatives)
{
    if (chord.isEmpty() || chord == "--" || kind == ChordUpdateKind::none) return;
    if(auto* wire=(SharedSessionWire*)sharedMemory)
    {
        WireLock lock(wire,true); if(!lock.locked)return;
        const auto audio=source=="Audio";
        if(!audio)wire->lastMidiPpq=ppq;

        // Manual edits are canonical. MIDI is the next most reliable source and
        // replaces an Audio estimate only where both sources cover the same time.
        for(uint32_t index=0;index<wire->regionCount;++index)
            if(wire->regions[index].locked
               &&(audio?overlapsRange(wire->regions[index],ppq,ppq+1.0):coversPpq(wire->regions[index],ppq)))return;
        if(audio)
        {
            for(uint32_t index=0;index<wire->regionCount;++index)
                if(wire->regions[index].source==0&&overlapsRange(wire->regions[index],ppq,ppq+1.0))return;
        }
        else
        {
            for(uint32_t index=0;index<wire->regionCount;)
            {
                const auto& region=wire->regions[index];
                if(region.source==1&&!region.locked&&coversPpq(region,ppq))eraseWireRegion(*wire,index);
                else ++index;
            }
        }

        auto activeIndex=wire->regionCount;
        for(uint32_t index=0;index<wire->regionCount;++index)
            if(wire->regions[index].source==(audio?1:0)&&wire->regions[index].startPpq<=ppq)
                activeIndex=index;
        const auto begins=kind==ChordUpdateKind::start||activeIndex==wire->regionCount;
        if(!begins)
        {
            auto& region=wire->regions[activeIndex];
            if(kind==ChordUpdateKind::refine&&!region.locked)copyWireString(region.name,chord);
            region.endPpq=juce::jmax(region.endPpq,ppq+(audio?1.0:0.0));region.confidence=juce::jmax(region.confidence,confidence);
            if(!alternatives.isEmpty())setWireAlternatives(region,alternatives);
            ++wire->revision;return;
        }

        if(wire->regionCount>=maximumSharedRegions)
        {
            std::memmove(wire->regions,wire->regions+512,(maximumSharedRegions-512)*sizeof(SharedRegionWire));
            wire->regionCount-=512;
        }
        const auto insertAt=insertionIndex(*wire,ppq);
        std::memmove(wire->regions+insertAt+1,wire->regions+insertAt,
                     (wire->regionCount-insertAt)*sizeof(SharedRegionWire));
        ++wire->regionCount;
        if(insertAt>0)wire->regions[insertAt-1].endPpq=ppq;
        auto& region=wire->regions[insertAt];region={};
        region.startPpq=ppq;
        region.endPpq=insertAt+1<wire->regionCount?wire->regions[insertAt+1].startPpq:ppq+(audio?1.0:0.03125);
        region.confidence=confidence;region.source=audio?1:0;
        copyWireString(region.name,chord);setWireAlternatives(region,alternatives);
        ++wire->revision;return;
    }
    std::unique_lock lock(mutex, std::try_to_lock);
    if (!lock.owns_lock()) return;
    const auto beginsRegion = regions.empty() || kind == ChordUpdateKind::start || regions.back().source != source;
    if (beginsRegion)
    {
        if (!regions.empty() && ppq >= regions.back().startPpq)
            regions.back().endPpq = ppq;
        regions.push_back({ppq, ppq + 0.03125, chord, source, confidence, alternatives});
        if (regions.size() > 4096) regions.erase(regions.begin(), regions.begin() + 1024);
    }
    else
    {
        if (kind == ChordUpdateKind::refine && !regions.back().locked)
            regions.back().name = chord;
        regions.back().endPpq = juce::jmax(regions.back().endPpq, ppq);
        regions.back().confidence = juce::jmax(regions.back().confidence, confidence);
        if (!alternatives.isEmpty()) regions.back().alternatives = alternatives;
    }
    ++revision;
}

void SharedChordSession::renameRegion(size_t index, const juce::String& name)
{
    const auto trimmed = name.trim();
    if (trimmed.isEmpty()) return;
    if(auto* wire=(SharedSessionWire*)sharedMemory){WireLock lock(wire);if(!lock.locked||index>=wire->regionCount)return;copyWireString(wire->regions[index].name,trimmed);wire->regions[index].confidence=1.0f;wire->regions[index].locked=1;++wire->revision;return;}
    std::lock_guard lock(mutex);
    if (index >= regions.size()) return;
    regions[index].name = trimmed;
    regions[index].confidence = 1.0f;
    regions[index].locked = true;
    ++revision;
}

void SharedChordSession::deleteRegion(size_t index)
{
    if(auto* wire=(SharedSessionWire*)sharedMemory){WireLock lock(wire);if(!lock.locked||index>=wire->regionCount)return;std::memmove(wire->regions+index,wire->regions+index+1,(wire->regionCount-index-1)*sizeof(SharedRegionWire));--wire->regionCount;++wire->revision;return;}
    std::lock_guard lock(mutex);
    if (index >= regions.size()) return;
    regions.erase(regions.begin() + (std::ptrdiff_t)index);
    ++revision;
}

bool SharedChordSession::resizeRegion(size_t index,double startPpq,double endPpq)
{
    constexpr double minimumDuration=0.03125;
    const auto apply=[&](auto& list,size_t count)
    {
        if(index>=count)return false;
        const auto minimumStart=index>0?list[index-1].endPpq:0.0;
        const auto maximumEnd=index+1<count?list[index+1].startPpq:std::numeric_limits<double>::max();
        const auto constrainedStart=juce::jlimit(minimumStart,
            juce::jmax(minimumStart,maximumEnd-minimumDuration),startPpq);
        const auto constrainedEnd=juce::jlimit(constrainedStart+minimumDuration,maximumEnd,endPpq);
        if(std::abs(list[index].startPpq-constrainedStart)<0.000001
           &&std::abs(list[index].endPpq-constrainedEnd)<0.000001)return false;
        list[index].startPpq=constrainedStart;list[index].endPpq=constrainedEnd;
        list[index].locked=1;
        return true;
    };
    if(auto* wire=(SharedSessionWire*)sharedMemory)
    {
        WireLock lock(wire);if(!lock.locked)return false;
        if(!apply(wire->regions,wire->regionCount))return false;
        ++wire->revision;return true;
    }
    std::lock_guard lock(mutex);
    if(!apply(regions,regions.size()))return false;
    ++revision;return true;
}

bool SharedChordSession::quantizeRegion(size_t index,double gridPpq)
{
    constexpr double minimumDuration=0.03125;
    const auto grid=juce::jlimit(0.03125,4.0,gridPpq);
    const auto snap=[grid](double value){return juce::jmax(0.0,std::round(value/grid)*grid);};
    const auto apply=[&](auto& list,size_t count)
    {
        if(index>=count)return false;
        auto start=snap(list[index].startPpq);
        auto end=snap(list[index].endPpq);
        if(end<start+minimumDuration)end=start+minimumDuration;
        const auto minimumStart=index>0?list[index-1].endPpq:0.0;
        const auto maximumEnd=index+1<count?list[index+1].startPpq:std::numeric_limits<double>::max();
        start=juce::jlimit(minimumStart,juce::jmax(minimumStart,maximumEnd-minimumDuration),start);
        end=juce::jlimit(start+minimumDuration,maximumEnd,end);
        if(std::abs(list[index].startPpq-start)<0.000001
           &&std::abs(list[index].endPpq-end)<0.000001)return false;
        list[index].startPpq=start;list[index].endPpq=end;list[index].locked=1;
        return true;
    };
    if(auto* wire=(SharedSessionWire*)sharedMemory)
    {
        WireLock lock(wire);if(!lock.locked)return false;
        if(!apply(wire->regions,wire->regionCount))return false;
        ++wire->revision;return true;
    }
    std::lock_guard lock(mutex);
    if(!apply(regions,regions.size()))return false;
    ++revision;return true;
}

bool SharedChordSession::extendRegionToNext(size_t index)
{
    const auto apply=[&](auto& list,size_t count)
    {
        if(index+1>=count||list[index+1].startPpq<=list[index].endPpq+0.000001)return false;
        list[index].endPpq=list[index+1].startPpq;list[index].locked=1;
        return true;
    };
    if(auto* wire=(SharedSessionWire*)sharedMemory)
    {
        WireLock lock(wire);if(!lock.locked)return false;
        if(!apply(wire->regions,wire->regionCount))return false;
        ++wire->revision;return true;
    }
    std::lock_guard lock(mutex);
    if(!apply(regions,regions.size()))return false;
    ++revision;return true;
}

void SharedChordSession::replaceRegions(const std::vector<ChordRegionData>& replacement)
{
    if(auto* wire=(SharedSessionWire*)sharedMemory)
    {
        WireLock lock(wire);if(!lock.locked)return;
        wire->regionCount=(uint32_t)juce::jmin(replacement.size(),maximumSharedRegions);
        for(uint32_t index=0;index<wire->regionCount;++index)
        {
            const auto& source=replacement[index];auto& destination=wire->regions[index];destination={};
            destination.startPpq=source.startPpq;destination.endPpq=source.endPpq;
            destination.confidence=source.confidence;destination.source=source.source=="Audio"?1:0;
            destination.locked=source.locked?1:0;copyWireString(destination.name,source.name);
            setWireAlternatives(destination,source.alternatives);
        }
        ++wire->revision;return;
    }
    std::lock_guard lock(mutex);regions=replacement;++revision;
}

void SharedChordSession::replaceAudioRegions(double startPpq,double endPpq,
                                             const std::vector<ChordRegionData>& replacement)
{
    if(endPpq<=startPpq)return;
    const auto compose=[&](const auto& existing)
    {
        std::vector<ChordRegionData> result;
        result.reserve(existing.size()+replacement.size()+2);
        for(const auto& region:existing)
        {
            if(region.source!="Audio"||region.locked||region.endPpq<=startPpq||region.startPpq>=endPpq)
            {
                result.push_back(region);
                continue;
            }
            if(region.startPpq<startPpq)
            {
                auto prefix=region;prefix.endPpq=startPpq;
                if(prefix.endPpq-prefix.startPpq>=0.03125)result.push_back(prefix);
            }
            if(region.endPpq>endPpq)
            {
                auto suffix=region;suffix.startPpq=endPpq;
                if(suffix.endPpq-suffix.startPpq>=0.03125)result.push_back(suffix);
            }
        }
        for(auto region:replacement)
        {
            region.startPpq=juce::jmax(startPpq,region.startPpq);
            region.endPpq=juce::jmin(endPpq,region.endPpq);
            region.source="Audio";region.locked=false;
            if(region.name.isEmpty()||region.name=="--"||region.endPpq-region.startPpq<0.03125)continue;
            bool blocked=false;
            for(const auto& authoritative:existing)
                if((authoritative.locked||authoritative.source=="MIDI")
                   &&authoritative.startPpq<region.endPpq&&authoritative.endPpq>region.startPpq)
                {blocked=true;break;}
            if(!blocked)result.push_back(std::move(region));
        }
        std::sort(result.begin(),result.end(),[](const auto& left,const auto& right)
        {
            if(left.startPpq<right.startPpq)return true;
            if(left.startPpq>right.startPpq)return false;
            return left.locked&&!right.locked;
        });
        const auto baseName=[](const juce::String& chord)
        {
            return chord.upToFirstOccurrenceOf("/",false,false);
        };
        const auto rootName=[&](const juce::String& chord)
        {
            const auto base=baseName(chord);
            return base.length()>1&&(base[1]=='b'||base[1]=='#')?base.substring(0,2):base.substring(0,1);
        };
        const auto simpleRootTriad=[&](const juce::String& chord)
        {
            const auto base=baseName(chord);const auto root=rootName(base);
            return base==root||base==root+"m";
        };
        std::vector<ChordRegionData> merged;
        merged.reserve(result.size());
        for(auto& region:result)
        {
            if(!merged.empty()&&!region.locked&&!merged.back().locked
               &&region.source=="Audio"&&merged.back().source=="Audio")
            {
                const auto previousBase=merged.back().name.upToFirstOccurrenceOf("/",false,false);
                const auto regionBase=region.name.upToFirstOccurrenceOf("/",false,false);
                constexpr double neuralSeamTolerance=0.1875;
                if(previousBase==regionBase
                   &&region.startPpq<=merged.back().endPpq+neuralSeamTolerance)
                {
                    merged.back().endPpq=juce::jmax(merged.back().endPpq,region.endPpq);
                    if(region.confidence>merged.back().confidence)
                    {
                        merged.back().name=region.name;
                        merged.back().confidence=region.confidence;
                    }
                    if(!region.alternatives.isEmpty())merged.back().alternatives=region.alternatives;
                    continue;
                }
                if(rootName(previousBase)==rootName(regionBase)
                   &&!simpleRootTriad(previousBase)&&simpleRootTriad(regionBase)
                   &&region.startPpq<=merged.back().endPpq+neuralSeamTolerance
                   &&std::abs(region.startPpq-startPpq)<=0.35)
                {
                    merged.back().endPpq=juce::jmax(merged.back().endPpq,region.endPpq);
                    continue;
                }
                const auto seamDistance=region.startPpq-merged.back().endPpq;
                if(std::abs(seamDistance)<=neuralSeamTolerance)
                {
                    const auto boundary=(region.startPpq+merged.back().endPpq)*0.5;
                    merged.back().endPpq=boundary;
                    region.startPpq=boundary;
                }
            }
            if(!merged.empty()&&!region.locked&&!merged.back().locked
               &&region.source==merged.back().source&&region.name==merged.back().name
               &&region.startPpq<=merged.back().endPpq+0.03125)
            {
                merged.back().endPpq=juce::jmax(merged.back().endPpq,region.endPpq);
                merged.back().confidence=juce::jmax(merged.back().confidence,region.confidence);
                if(!region.alternatives.isEmpty())merged.back().alternatives=region.alternatives;
            }
            else merged.push_back(std::move(region));
        }
        for(size_t index=1;index+1<merged.size();)
        {
            auto& previous=merged[index-1];const auto& middle=merged[index];const auto& next=merged[index+1];
            const auto previousBase=baseName(previous.name),middleBase=baseName(middle.name);
            const auto nextBase=baseName(next.name);
            const auto contiguous=middle.startPpq<=previous.endPpq+0.1875
                                  &&next.startPpq<=middle.endPpq+0.1875;
            const auto touchesReplacementSeam=std::abs(middle.startPpq-startPpq)<=0.1875
                                              ||std::abs(middle.endPpq-startPpq)<=0.1875;
            if(previous.source=="Audio"&&middle.source=="Audio"&&next.source=="Audio"
               &&!previous.locked&&!middle.locked&&!next.locked&&contiguous&&touchesReplacementSeam
               &&previousBase==nextBase&&simpleRootTriad(middleBase)
               &&rootName(previousBase)==rootName(middleBase)
               &&middle.endPpq-middle.startPpq<=0.75)
            {
                previous.endPpq=juce::jmax(previous.endPpq,next.endPpq);
                if(next.confidence>previous.confidence)
                {
                    previous.name=next.name;previous.confidence=next.confidence;
                    previous.alternatives=next.alternatives;
                }
                merged.erase(merged.begin()+(std::ptrdiff_t)index,
                             merged.begin()+(std::ptrdiff_t)index+2);
                continue;
            }
            ++index;
        }
        return merged;
    };

    if(auto* wire=(SharedSessionWire*)sharedMemory)
    {
        WireLock lock(wire);if(!lock.locked)return;
        std::vector<ChordRegionData> existing;existing.reserve(wire->regionCount);
        for(uint32_t index=0;index<wire->regionCount;++index)existing.push_back(fromWire(wire->regions[index]));
        const auto result=compose(existing);
        wire->regionCount=(uint32_t)juce::jmin(result.size(),maximumSharedRegions);
        for(uint32_t index=0;index<wire->regionCount;++index)
        {
            const auto& source=result[index];auto& destination=wire->regions[index];destination={};
            destination.startPpq=source.startPpq;destination.endPpq=source.endPpq;
            destination.confidence=source.confidence;destination.source=source.source=="Audio"?1:0;
            destination.locked=source.locked?1:0;copyWireString(destination.name,source.name);
            setWireAlternatives(destination,source.alternatives);
        }
        ++wire->revision;return;
    }
    std::lock_guard lock(mutex);regions=compose(regions);++revision;
}

void SharedChordSession::updateTransport(double ppq, double bpm, int n, int d, bool playing)
{
    if(auto* wire=(SharedSessionWire*)sharedMemory)
    {
        WireLock lock(wire,true);if(!lock.locked)return;
        if(std::abs(wire->playhead-ppq)<0.000001&&std::abs(wire->bpm-bpm)<0.000001
           &&wire->numerator==n&&wire->denominator==d&&wire->playing==(playing?1:0))return;
        wire->playhead=ppq;wire->bpm=bpm;wire->numerator=n;wire->denominator=d;wire->playing=playing?1:0;++wire->revision;return;
    }
    std::unique_lock lock(mutex, std::try_to_lock); if (!lock.owns_lock()) return;
    if(std::abs(playhead-ppq)<0.000001&&std::abs(tempo-bpm)<0.000001&&timeNumerator==n
       &&timeDenominator==d&&isPlaying==playing)return;
    playhead = ppq; tempo = bpm; timeNumerator = n; timeDenominator = d; isPlaying = playing; ++revision;
}
ChordSessionSnapshot SharedChordSession::snapshot() const
{
    if(auto* wire=(SharedSessionWire*)sharedMemory)
    {
        WireLock lock(wire);if(lock.locked)
        {
            ChordSessionSnapshot result;result.playheadPpq=wire->playhead;result.bpm=wire->bpm;result.numerator=wire->numerator;
            result.denominator=wire->denominator;result.instanceCount=wire->instances;result.playing=wire->playing!=0;result.revision=wire->revision;
            result.timelineTextScale=wire->timelineTextScale;result.leadSheetTextScale=wire->leadTextScale;result.regions.reserve(wire->regionCount);
            for(uint32_t i=0;i<wire->regionCount;++i)result.regions.push_back(fromWire(wire->regions[i]));
            return result;
        }
    }
    std::lock_guard lock(mutex);
    ChordSessionSnapshot result;result.regions=regions;result.playheadPpq=playhead;result.bpm=tempo;result.numerator=timeNumerator;
    result.denominator=timeDenominator;result.instanceCount=instances;result.playing=isPlaying;result.revision=revision;
    result.timelineTextScale=timelineTextScale;result.leadSheetTextScale=leadTextScale;return result;
}
void SharedChordSession::clear()
{
    if(auto* wire=(SharedSessionWire*)sharedMemory){WireLock lock(wire);if(lock.locked){wire->regionCount=0;++wire->revision;}return;}
    std::lock_guard lock(mutex);regions.clear();++revision;
}

void SharedChordSession::setTextScale(bool leadSheet,float scale)
{
    scale=juce::jlimit(0.5f,3.0f,scale);
    if(auto* wire=(SharedSessionWire*)sharedMemory){WireLock lock(wire);if(lock.locked){(leadSheet?wire->leadTextScale:wire->timelineTextScale)=scale;++wire->revision;}return;}
    std::lock_guard lock(mutex);(leadSheet?leadTextScale:timelineTextScale)=scale;++revision;
}

namespace
{
struct Pattern { const char* suffix; std::initializer_list<int> tones; };
const Pattern patterns[] = {
    {"maj13",{0,2,4,7,9,11}}, {"m13",{0,2,3,7,9,10}}, {"13",{0,2,4,7,9,10}},
    {"maj9#11",{0,2,4,6,7,11}}, {"maj9",{0,2,4,7,11}}, {"mMaj9",{0,2,3,7,11}},
    {"m9",{0,2,3,7,10}}, {"9sus4",{0,2,5,7,10}}, {"9",{0,2,4,7,10}},
    {"7b9",{0,1,4,7,10}}, {"7#9",{0,3,4,7,10}}, {"7#11",{0,4,6,7,10}},
    {"7b13",{0,4,7,8,10}}, {"7b5",{0,4,6,10}}, {"7#5",{0,4,8,10}},
    {"6/9",{0,2,4,7,9}}, {"m6/9",{0,2,3,7,9}}, {"add9",{0,2,4,7}},
    {"m(add9)",{0,2,3,7}}, {"maj7#11",{0,4,6,7,11}}, {"mMaj7",{0,3,7,11}},
    {"maj7",{0,4,7,11}}, {"m7",{0,3,7,10}}, {"7sus4",{0,5,7,10}}, {"7",{0,4,7,10}},
    {"m7b5",{0,3,6,10}}, {"dim7",{0,3,6,9}}, {"6",{0,4,7,9}}, {"m6",{0,3,7,9}},
    {"sus4",{0,5,7}}, {"sus2",{0,2,7}}, {"aug",{0,4,8}}, {"dim",{0,3,6}},
    {"m",{0,3,7}}, {"",{0,4,7}}, {"5",{0,7}}
};
const char* names[] = {"C","Db","D","Eb","E","F","Gb","G","Ab","A","Bb","B"};
const char* sharpNames[] = {"C","C#","D","D#","E","F","F#","G","G#","A","A#","B"};

struct KeyEstimate { int root=0; bool minor=false; float score=0.0f; };

KeyEstimate estimateKey(const std::array<float,12>* context)
{
    if(context==nullptr)return {};
    static const int majorScale[] {0,2,4,5,7,9,11};
    static const int minorScale[] {0,2,3,5,7,8,10};
    KeyEstimate best;best.score=-1.0f;
    for (int root=0; root<12; ++root) for (int minor=0; minor<2; ++minor)
    {
        const auto* scale = minor != 0 ? minorScale : majorScale;
        float inside=0.0f, outside=0.0f;
        for (int pc=0; pc<12; ++pc)
        {
            bool inScale=false;
            for (int i=0; i<7; ++i) if ((root+scale[i])%12==pc) { inScale=true; break; }
            (inScale?inside:outside)+=(*context)[(size_t)pc];
        }
        const auto score=inside-outside*0.45f;
        if(score>best.score)best={root,minor!=0,score};
    }
    return best;
}

bool contextPrefersSharps(const std::array<float, 12>* context)
{
    const auto key=estimateKey(context);
    if (key.minor)
        return key.root==4||key.root==11||key.root==6||key.root==1||key.root==8;
    return key.root==7||key.root==2||key.root==9||key.root==4||key.root==11||key.root==6;
}

bool pitchClassIsInKey(int pitchClass,const KeyEstimate& key)
{
    static const int majorScale[] {0,2,4,5,7,9,11};
    static const int minorScale[] {0,2,3,5,7,8,10};
    const auto* scale=key.minor?minorScale:majorScale;
    for(int index=0;index<7;++index)if((key.root+scale[index])%12==pitchClass)return true;
    return false;
}

juce::String chordWithoutBass(const juce::String& chord)
{
    return chord.upToFirstOccurrenceOf("/",false,false);
}

juce::String chordRoot(const juce::String& chord)
{
    const auto base=chordWithoutBass(chord);
    return base.length()>1&&(base[1]=='b'||base[1]=='#')?base.substring(0,2):base.substring(0,1);
}

struct ScoredChordCandidate
{
    float score=0.0f;
    int root=0;
    const Pattern* pattern=nullptr;
};

std::vector<ScoredChordCandidate> rankChordCandidates(const std::array<float,12>& weights,
                                                       int bassPitchClass,
                                                       const std::array<float,12>* keyContext,
                                                       float additionalComplexityPenalty)
{
    const auto total=std::accumulate(weights.begin(),weights.end(),0.0f);
    if(total<0.001f)return {};
    const auto peak=*std::max_element(weights.begin(),weights.end());
    const auto estimatedKey=estimateKey(keyContext);
    float keyContextTotal=0.0f;
    if(keyContext!=nullptr)keyContextTotal=std::accumulate(keyContext->begin(),keyContext->end(),0.0f);
    std::vector<ScoredChordCandidate> candidates;
    candidates.reserve(std::size(patterns)*12);
    for(int root=0;root<12;++root)for(const auto& pattern:patterns)
    {
        float hit=0.0f,outside=0.0f,missingPenalty=0.0f;
        const auto dominantThirteen=std::strcmp(pattern.suffix,"13")==0;
        const auto hasThirteenShell=dominantThirteen&&bassPitchClass==root
            &&weights[(size_t)root]>=peak*0.18f
            &&weights[(size_t)((root+4)%12)]>=peak*0.18f
            &&weights[(size_t)((root+10)%12)]>=peak*0.08f
            &&weights[(size_t)((root+9)%12)]>=peak*0.18f;
        for(int pc=0;pc<12;++pc)
        {
            const auto interval=(pc-root+12)%12;
            const bool required=std::find(pattern.tones.begin(),pattern.tones.end(),interval)!=pattern.tones.end();
            (required?hit:outside)+=weights[(size_t)pc];
        }
        for(const auto interval:pattern.tones)
        {
            const auto present=weights[(size_t)((root+interval)%12)]>=peak*0.18f;
            if(!present)
            {
                if(hasThirteenShell&&(interval==2||interval==7))missingPenalty+=0.01f;
                else missingPenalty+=interval==7?0.05f:(interval==0?0.08f:0.13f);
            }
        }
        const auto rootBonus=weights[(size_t)root]>=peak*0.18f?0.07f:0.0f;
        const auto bassBonus=bassPitchClass==root?0.035f:0.0f;
        float keyPenalty=0.0f;
        if(keyContext!=nullptr&&keyContextTotal>0.3f&&additionalComplexityPenalty>0.0f)
            for(const auto interval:pattern.tones)
            {
                const auto pc=(root+interval)%12;
                if(weights[(size_t)pc]>=peak*0.18f&&!pitchClassIsInKey(pc,estimatedKey))keyPenalty+=0.055f;
            }
        const auto complexitySteps=hasThirteenShell?0:juce::jmax(0,(int)pattern.tones.size()-4);
        const auto shellBonus=hasThirteenShell?0.13f:0.0f;
        auto characteristicPenalty=0.0f;
        if(additionalComplexityPenalty>0.0f)
        {
            auto characteristicInterval=-1;
            if(std::strcmp(pattern.suffix,"7b9")==0)characteristicInterval=1;
            else if(std::strcmp(pattern.suffix,"7#9")==0)characteristicInterval=3;
            else if(std::strcmp(pattern.suffix,"7#11")==0)characteristicInterval=6;
            else if(std::strcmp(pattern.suffix,"7b13")==0)characteristicInterval=8;
            if(characteristicInterval>=0
               &&weights[(size_t)((root+characteristicInterval)%12)]<peak*0.30f)
                characteristicPenalty+=0.16f;
            if(std::strcmp(pattern.suffix,"mMaj7")==0||std::strcmp(pattern.suffix,"mMaj9")==0)
                characteristicPenalty+=0.10f;
        }
        const auto score=hit/total-outside/total*0.72f-missingPenalty-keyPenalty
                         +rootBonus+bassBonus-(float)pattern.tones.size()*0.002f
                         -complexitySteps*additionalComplexityPenalty+shellBonus-characteristicPenalty;
        candidates.push_back({score,root,&pattern});
    }
    std::sort(candidates.begin(),candidates.end(),[](const auto& left,const auto& right)
    {
        if(left.score>right.score)return true;
        if(left.score<right.score)return false;
        if(left.root<right.root)return true;
        if(left.root>right.root)return false;
        return std::strcmp(left.pattern->suffix,right.pattern->suffix)<0;
    });
    return candidates;
}

juce::String formatChordCandidate(const ScoredChordCandidate& candidate,int bassPitchClass,
                                  const std::array<float,12>* keyContext)
{
    const auto* noteNames=contextPrefersSharps(keyContext)?sharpNames:names;
    auto value=juce::String(noteNames[candidate.root])+candidate.pattern->suffix;
    if(bassPitchClass>=0&&bassPitchClass!=candidate.root)value+="/"+juce::String(noteNames[bassPitchClass%12]);
    return value;
}

float pitchProfileDistance(const std::array<float,12>& left,const std::array<float,12>& right)
{
    const auto leftTotal=std::accumulate(left.begin(),left.end(),0.0f);
    const auto rightTotal=std::accumulate(right.begin(),right.end(),0.0f);
    if(leftTotal<0.001f||rightTotal<0.001f)return 1.0f;
    float distance=0.0f;
    for(size_t index=0;index<left.size();++index)
        distance+=std::abs(left[index]/leftTotal-right[index]/rightTotal);
    return juce::jlimit(0.0f,1.0f,distance*0.5f);
}
}

juce::String identifyChord(const std::array<float, 12>& weights, float& confidence, int bassPitchClass,
                           juce::StringArray* alternatives, const std::array<float, 12>* keyContext,
                           float additionalComplexityPenalty)
{
    const auto candidates=rankChordCandidates(weights,bassPitchClass,keyContext,additionalComplexityPenalty);
    if(candidates.empty()) return "--";
    const auto gap=candidates.size()>1?candidates[0].score-candidates[1].score:0.25f;
    confidence=juce::jlimit(0.0f,1.0f,(candidates[0].score+0.25f)/1.25f*(0.72f+juce::jlimit(0.0f,0.28f,gap)));
    const auto result=formatChordCandidate(candidates.front(),bassPitchClass,keyContext);
    if(alternatives!=nullptr)
    {
        alternatives->clear();
        for(size_t i=1;i<candidates.size()&&alternatives->size()<4;++i)
        {
            const auto candidate=formatChordCandidate(candidates[i],bassPitchClass,keyContext);
            if(candidate!=result&&!alternatives->contains(candidate)) alternatives->add(candidate);
        }
    }
    return result;
}

std::vector<PitchedNoteRegion> createConsensusNotes(const std::vector<PitchedNoteRegion>& sensitive,
                                                    const std::vector<PitchedNoteRegion>& strict)
{
    std::vector<PitchedNoteRegion> result;
    result.reserve(sensitive.size()+strict.size());
    std::vector<bool> strictUsed(strict.size(),false);
    for(const auto& note:sensitive)
    {
        auto best=strict.size();
        double bestOverlap=0.0;
        for(size_t index=0;index<strict.size();++index)
        {
            if(strictUsed[index]||strict[index].midiNote!=note.midiNote)continue;
            const auto overlap=juce::jmin(note.endPpq,strict[index].endPpq)
                               -juce::jmax(note.startPpq,strict[index].startPpq);
            const auto shorter=juce::jmin(note.endPpq-note.startPpq,
                                          strict[index].endPpq-strict[index].startPpq);
            if(overlap>juce::jmax(0.0,shorter)*0.20&&overlap>bestOverlap)
            {
                best=index;bestOverlap=overlap;
            }
        }
        auto combined=note;
        if(best<strict.size())
        {
            strictUsed[best]=true;
            combined.startPpq=juce::jmin(note.startPpq,strict[best].startPpq);
            combined.endPpq=juce::jmax(note.endPpq,strict[best].endPpq);
            combined.confidence=juce::jlimit(0.05f,1.0f,
                                             juce::jmax(note.confidence,strict[best].confidence)*1.08f);
        }
        else combined.confidence*=combined.confidence>=0.55f?0.72f:0.45f;
        if(combined.endPpq>combined.startPpq&&combined.confidence>=0.045f)
            result.push_back(combined);
    }
    for(size_t index=0;index<strict.size();++index)
        if(!strictUsed[index]&&strict[index].endPpq>strict[index].startPpq)
        {
            auto note=strict[index];note.confidence*=0.80f;result.push_back(note);
        }
    std::sort(result.begin(),result.end(),[](const auto& left,const auto& right)
    {
        if(left.startPpq<right.startPpq)return true;
        if(left.startPpq>right.startPpq)return false;
        return left.midiNote<right.midiNote;
    });
    return result;
}

std::vector<ChordRegionData> createChordRegionsFromNotes(const std::vector<PitchedNoteRegion>& notes,
                                                         double startPpq,double endPpq,double slicePpq,
                                                         const std::vector<HarmonicFrameEvidence>* harmonicFrames)
{
    if(endPpq<=startPpq||slicePpq<=0.0
       ||(notes.empty()&&(harmonicFrames==nullptr||harmonicFrames->empty())))return {};
    std::array<float,12> keyContext{};
    for(const auto& note:notes)
    {
        const auto duration=juce::jmax(0.0,juce::jmin(endPpq,note.endPpq)-juce::jmax(startPpq,note.startPpq));
        if(duration>0.0&&note.midiNote>=0)
            keyContext[(size_t)(note.midiNote%12)]+=(float)duration*juce::jlimit(0.05f,1.0f,note.confidence);
    }
    if(harmonicFrames!=nullptr)
        for(const auto& frame:*harmonicFrames)
        {
            const auto duration=juce::jmax(0.0,juce::jmin(endPpq,frame.endPpq)
                                               -juce::jmax(startPpq,frame.startPpq));
            if(duration<=0.0)continue;
            const auto peak=*std::max_element(frame.pitchWeights.begin(),frame.pitchWeights.end());
            auto activePitchClasses=0;
            for(const auto weight:frame.pitchWeights)
                if(weight>=peak*0.10f&&weight>0.06f)++activePitchClasses;
            if(activePitchClasses<2||activePitchClasses>7)continue;
            for(size_t pitchClass=0;pitchClass<keyContext.size();++pitchClass)
                keyContext[pitchClass]+=(float)duration*frame.pitchWeights[pitchClass]
                                        *juce::jlimit(0.0f,1.0f,frame.confidence)*0.30f;
        }

    struct NamedCandidate
    {
        juce::String name,base;
        float score=0.0f;
    };
    struct Slice
    {
        double start=0.0,end=0.0;
        juce::String name,base;
        float confidence=0.0f;
        float attackEvidence=0.0f;
        std::array<float,12> weights{};
        std::vector<NamedCandidate> candidates;
        juce::StringArray alternatives;
    };
    std::vector<Slice> slices;
    const auto count=(size_t)std::ceil((endPpq-startPpq)/slicePpq);
    slices.reserve(count);
    auto memoryEpochStart=startPpq-1.0;
    std::array<bool,12> memoryPitchClasses{};
    auto memoryBassMidi=128;
    for(size_t index=0;index<count;++index)
    {
        Slice slice; slice.start=startPpq+(double)index*slicePpq;
        slice.end=juce::jmin(endPpq,slice.start+slicePpq);
        std::array<bool,12> recentAttackPitchClasses{};
        auto recentAttackCount=0;
        auto earliestRecentAttack=slice.end;
        auto harmonicBurstReset=false;
        for(const auto& note:notes)
            if(note.midiNote>=0&&note.midiNote<=127&&note.confidence>=0.10f
               &&note.startPpq>=slice.end-0.40&&note.startPpq<=slice.end+0.000001)
            {
                const auto pitchClass=(size_t)(note.midiNote%12);
                if(!recentAttackPitchClasses[pitchClass])
                {
                    recentAttackPitchClasses[pitchClass]=true;
                    ++recentAttackCount;
                }
                earliestRecentAttack=juce::jmin(earliestRecentAttack,note.startPpq);
            }
        auto foreignRecentAttackCount=0;
        for(size_t pitchClass=0;pitchClass<memoryPitchClasses.size();++pitchClass)
            if(recentAttackPitchClasses[pitchClass]&&!memoryPitchClasses[pitchClass])
                ++foreignRecentAttackCount;
        auto suspendCarriedMemory=foreignRecentAttackCount>=2;
        if(recentAttackCount>=3)
        {
            if(earliestRecentAttack>=memoryEpochStart+0.45)
            {
                auto overlappingPitchClasses=0;
                for(size_t pitchClass=0;pitchClass<memoryPitchClasses.size();++pitchClass)
                    if(memoryPitchClasses[pitchClass]&&recentAttackPitchClasses[pitchClass])
                        ++overlappingPitchClasses;
                memoryEpochStart=earliestRecentAttack;
                const auto repeatedVoicing=overlappingPitchClasses>=3;
                if(repeatedVoicing)
                    for(size_t pitchClass=0;pitchClass<memoryPitchClasses.size();++pitchClass)
                        memoryPitchClasses[pitchClass]=memoryPitchClasses[pitchClass]
                                                       ||recentAttackPitchClasses[pitchClass];
                else
                {
                    memoryPitchClasses=recentAttackPitchClasses;
                    memoryBassMidi=128;
                }
                auto strongestBurstConfidence=0.0f;
                for(const auto& note:notes)
                    if(note.midiNote>=0&&note.midiNote<=127
                       &&note.startPpq>=memoryEpochStart-0.000001
                       &&note.startPpq<=memoryEpochStart+0.40
                       &&memoryPitchClasses[(size_t)(note.midiNote%12)])
                        strongestBurstConfidence=juce::jmax(strongestBurstConfidence,note.confidence);
                for(const auto& note:notes)
                    if(note.midiNote>=0&&note.midiNote<=127
                       &&note.confidence>=juce::jmax(0.15f,strongestBurstConfidence*0.25f)
                       &&note.startPpq>=memoryEpochStart-0.000001
                       &&note.startPpq<=memoryEpochStart+0.40
                       &&memoryPitchClasses[(size_t)(note.midiNote%12)])
                        memoryBassMidi=juce::jmin(memoryBassMidi,note.midiNote);
                harmonicBurstReset=true;
                suspendCarriedMemory=false;
            }
            else if(std::abs(earliestRecentAttack-memoryEpochStart)<=0.20)
                for(size_t pitchClass=0;pitchClass<memoryPitchClasses.size();++pitchClass)
                    memoryPitchClasses[pitchClass]=memoryPitchClasses[pitchClass]
                                                   ||recentAttackPitchClasses[pitchClass];
        }
        std::array<float,12> summedWeights{},strongestWeights{};
        std::array<float,128> midiWeights{};
        float directEvidence=0.0f,carriedEvidence=0.0f;
        for(const auto& note:notes)
        {
            if(note.midiNote<0||note.midiNote>127)continue;
            const auto overlap=juce::jmin(slice.end,note.endPpq)-juce::jmax(slice.start,note.startPpq);
            auto contribution=0.0f;
            if(overlap>0.0)
            {
                contribution=(float)(overlap/(slice.end-slice.start))
                             *juce::jlimit(0.05f,1.0f,note.confidence);
                directEvidence+=contribution;
            }
            else
            {
                const auto duration=note.endPpq-note.startPpq;
                const auto age=slice.start-note.endPpq;
                const auto pitchClass=(size_t)(note.midiNote%12);
                if(duration<0.035||duration>0.60||age<0.0||age>1.20
                   ||note.startPpq<memoryEpochStart-0.000001
                   ||!memoryPitchClasses[pitchClass]||suspendCarriedMemory)continue;
                const auto articulationWeight=juce::jlimit(0.30,1.0,duration/0.12);
                const auto confirmedConfidence=juce::jmax(0.32f,
                    juce::jlimit(0.05f,1.0f,note.confidence));
                contribution=confirmedConfidence*0.68f*(float)articulationWeight
                             *(float)std::exp(-age/0.90);
                carriedEvidence+=contribution;
            }
            if(contribution<=0.0f)continue;
            const auto pitchClass=(size_t)(note.midiNote%12);
            summedWeights[pitchClass]+=contribution;
            strongestWeights[pitchClass]=juce::jmax(strongestWeights[pitchClass],contribution);
            midiWeights[(size_t)note.midiNote]+=contribution;
            if(overlap>0.0&&note.startPpq>=slice.start-0.000001&&note.startPpq<slice.end+0.000001)
                slice.attackEvidence+=juce::jlimit(0.05f,1.0f,note.confidence);
        }
        for(size_t pitchClass=0;pitchClass<slice.weights.size();++pitchClass)
        {
            // Octave doubling is useful evidence but should not drown out a quieter
            // third or seventh. Keep the strongest voice and compress extra copies.
            slice.weights[pitchClass]=strongestWeights[pitchClass]
                                      +(summedWeights[pitchClass]-strongestWeights[pitchClass])*0.18f;
        }
        std::array<float,12> acousticWeights{},acousticBassVotes{};
        float acousticFrameWeight=0.0f,acousticChangeEvidence=0.0f;
        if(harmonicFrames!=nullptr)
            for(const auto& frame:*harmonicFrames)
            {
                const auto overlap=juce::jmin(slice.end,frame.endPpq)-juce::jmax(slice.start,frame.startPpq);
                if(overlap<=0.0)continue;
                const auto contribution=(float)(overlap/(slice.end-slice.start))
                                        *juce::jlimit(0.0f,1.0f,frame.confidence);
                acousticFrameWeight+=contribution;
                for(size_t pitchClass=0;pitchClass<acousticWeights.size();++pitchClass)
                    acousticWeights[pitchClass]+=frame.pitchWeights[pitchClass]*contribution;
                if(frame.bassPitchClass>=0&&frame.bassPitchClass<12)
                    acousticBassVotes[(size_t)frame.bassPitchClass]+=contribution;
                acousticChangeEvidence=juce::jmax(acousticChangeEvidence,
                                                   frame.changeConfidence*juce::jmin(1.0f,contribution));
            }
        if(acousticFrameWeight>0.0f)
            for(auto& weight:acousticWeights)weight/=acousticFrameWeight;
        const auto acousticPeak=*std::max_element(acousticWeights.begin(),acousticWeights.end());
        auto acousticPitchClasses=0;
        if(acousticPeak>0.0f)
        {
            for(auto& weight:acousticWeights)
            {
                weight/=acousticPeak;
                if(weight>=0.10f)++acousticPitchClasses;
            }
        }
        const auto neuralPeak=*std::max_element(slice.weights.begin(),slice.weights.end());
        if(acousticPeak>0.0f&&acousticPitchClasses>=2&&acousticPitchClasses<=7)
        {
            if(neuralPeak>0.0f)
            {
                const auto memoryDominant=carriedEvidence>0.10f
                                          &&carriedEvidence>directEvidence*0.35f;
                for(size_t pitchClass=0;pitchClass<slice.weights.size();++pitchClass)
                {
                    const auto neural=slice.weights[pitchClass]/neuralPeak;
                    const auto acoustic=acousticWeights[pitchClass];
                    if(memoryDominant)
                        slice.weights[pitchClass]=juce::jmax(neural*0.90f,
                                                             neural*0.68f+acoustic*0.32f);
                    else
                        slice.weights[pitchClass]=neural*0.58f+acoustic*0.27f
                                                 +juce::jmin(neural,acoustic)*0.18f;
                }
            }
            else slice.weights=acousticWeights;
        }
        slice.attackEvidence=harmonicBurstReset?1.0f
            :juce::jmax(juce::jlimit(0.0f,1.0f,slice.attackEvidence/2.0f),
                        acousticChangeEvidence*0.90f);
        const auto strongestMidi=*std::max_element(midiWeights.begin(),midiWeights.end());
        int bass=128;
        for(int midi=0;midi<128;++midi)
            if(midiWeights[(size_t)midi]>=juce::jmax(0.10f,strongestMidi*0.15f))
            {
                bass=midi;
                break;
            }
        const auto acousticBass=(size_t)std::distance(acousticBassVotes.begin(),
                                                      std::max_element(acousticBassVotes.begin(),acousticBassVotes.end()));
        const auto acousticBassStrength=acousticBassVotes[acousticBass];
        auto bassPitchClass=bass<128?bass%12:-1;
        if(memoryBassMidi<128&&memoryPitchClasses[(size_t)(memoryBassMidi%12)]
           &&slice.start-memoryEpochStart<=1.35&&bass>=memoryBassMidi)
            bassPitchClass=memoryBassMidi%12;
        if(acousticBassStrength>0.0f
           &&(bass>=128||midiWeights[(size_t)bass]<strongestMidi*0.35f))
            bassPitchClass=(int)acousticBass;
        int uniquePitchClasses=0;
        const auto combinedPeak=*std::max_element(slice.weights.begin(),slice.weights.end());
        for(const auto weight:slice.weights)if(weight>=combinedPeak*0.10f&&weight>0.06f)++uniquePitchClasses;
        if(uniquePitchClasses>=3)
        {
            slice.name=identifyChord(slice.weights,slice.confidence,bassPitchClass,
                                     &slice.alternatives,&keyContext,0.055f);
            slice.base=chordWithoutBass(slice.name);
            if(slice.confidence>=0.34f)
            {
                const auto ranked=rankChordCandidates(slice.weights,bassPitchClass,&keyContext,0.055f);
                const auto bestScore=ranked.empty()?0.0f:ranked.front().score;
                for(const auto& candidate:ranked)
                {
                    if(slice.candidates.size()>=8||candidate.score<bestScore-0.30f)break;
                    const auto name=formatChordCandidate(candidate,bassPitchClass,&keyContext);
                    const auto base=chordWithoutBass(name);
                    const auto duplicate=std::find_if(slice.candidates.begin(),slice.candidates.end(),
                                                      [&](const auto& existing){return existing.base==base;});
                    if(duplicate==slice.candidates.end())slice.candidates.push_back({name,base,candidate.score});
                }
            }
            else
            {
                slice.name.clear();slice.base.clear();slice.alternatives.clear();
            }
        }
        slices.push_back(std::move(slice));
    }

    // Decode each uninterrupted span jointly. A locally attractive chord must
    // explain enough consecutive slices to pay for a harmonic transition. The
    // transition cost falls at real note attacks or a large pitch-profile change.
    for(size_t groupStart=0;groupStart<slices.size();)
    {
        while(groupStart<slices.size()&&slices[groupStart].candidates.empty())++groupStart;
        if(groupStart>=slices.size())break;
        auto groupEnd=groupStart+1;
        while(groupEnd<slices.size()&&!slices[groupEnd].candidates.empty())++groupEnd;

        std::vector<juce::String> states;
        for(auto index=groupStart;index<groupEnd;++index)
            for(const auto& candidate:slices[index].candidates)
                if(std::find(states.begin(),states.end(),candidate.base)==states.end())states.push_back(candidate.base);
        const auto stateCount=states.size();
        const auto frameCount=groupEnd-groupStart;
        constexpr float infinity=1.0e9f;
        std::vector<float> previous(stateCount,infinity),current(stateCount,infinity);
        std::vector<std::vector<size_t>> backPointers(frameCount,std::vector<size_t>(stateCount,0));
        const auto emissionCost=[&](const Slice& slice,const juce::String& state)
        {
            const auto found=std::find_if(slice.candidates.begin(),slice.candidates.end(),
                                          [&](const auto& candidate){return candidate.base==state;});
            if(found==slice.candidates.end())return 0.90f;
            const auto rank=(float)std::distance(slice.candidates.begin(),found);
            return juce::jmax(0.0f,(slice.candidates.front().score-found->score)*2.35f)+rank*0.018f;
        };
        for(size_t state=0;state<stateCount;++state)
            previous[state]=emissionCost(slices[groupStart],states[state]);

        for(size_t frame=1;frame<frameCount;++frame)
        {
            const auto sliceIndex=groupStart+frame;
            const auto profileChange=pitchProfileDistance(slices[sliceIndex-1].weights,slices[sliceIndex].weights);
            const auto changeEvidence=juce::jmax(profileChange,slices[sliceIndex].attackEvidence*0.85f);
            std::fill(current.begin(),current.end(),infinity);
            for(size_t state=0;state<stateCount;++state)
            {
                const auto emission=emissionCost(slices[sliceIndex],states[state]);
                for(size_t prior=0;prior<stateCount;++prior)
                {
                    auto transition=0.0f;
                    if(states[prior]!=states[state])
                    {
                        transition=0.10f+(1.0f-changeEvidence)*0.34f;
                        if(chordRoot(states[prior])==chordRoot(states[state]))transition+=0.05f;
                    }
                    const auto cost=previous[prior]+transition+emission;
                    if(cost<current[state])
                    {
                        current[state]=cost;
                        backPointers[frame][state]=prior;
                    }
                }
            }
            previous.swap(current);
        }

        auto state=(size_t)std::distance(previous.begin(),std::min_element(previous.begin(),previous.end()));
        std::vector<size_t> decoded(frameCount,state);
        for(size_t frame=frameCount-1;frame>0;--frame)
        {
            state=backPointers[frame][state];
            decoded[frame-1]=state;
        }
        for(size_t frame=0;frame<frameCount;++frame)
        {
            auto& slice=slices[groupStart+frame];
            const auto& selectedBase=states[decoded[frame]];
            const auto selected=std::find_if(slice.candidates.begin(),slice.candidates.end(),
                                             [&](const auto& candidate){return candidate.base==selectedBase;});
            slice.base=selectedBase;
            slice.name=selected==slice.candidates.end()?selectedBase:selected->name;
            if(selected!=slice.candidates.end())
                slice.confidence*=juce::jlimit(0.55f,1.0f,
                    1.0f-(slice.candidates.front().score-selected->score)*0.8f);
            else slice.confidence*=0.55f;
            slice.alternatives.clear();
            for(const auto& candidate:slice.candidates)
                if(candidate.base!=selectedBase&&!slice.alternatives.contains(candidate.name)
                   &&slice.alternatives.size()<4)slice.alternatives.add(candidate.name);
        }
        groupStart=groupEnd;
    }

    // Fill only a one-slice dropout surrounded by the same harmony.
    for(size_t index=1;index+1<slices.size();++index)
        if(slices[index].name.isEmpty()&&!slices[index-1].name.isEmpty()
           &&slices[index-1].base==slices[index+1].base)
        {
            const auto start=slices[index].start,end=slices[index].end;
            slices[index]=slices[index-1];slices[index].start=start;slices[index].end=end;
        }

    // Basic Pitch can emit a brief note tail as a different extension. A candidate
    // shorter than a quarter note is treated as a transition when both neighbours agree.
    for(size_t first=0;first<slices.size();)
    {
        auto last=first+1;
        while(last<slices.size()&&slices[last].base==slices[first].base)++last;
        const auto duration=slices[last-1].end-slices[first].start;
        if(!slices[first].name.isEmpty()&&duration<0.25&&first>0&&last<slices.size()
           &&slices[first-1].base==slices[last].base&&!slices[first-1].base.isEmpty())
            for(auto index=first;index<last;++index)
            {
                const auto start=slices[index].start,end=slices[index].end;
                slices[index]=slices[first-1];slices[index].start=start;slices[index].end=end;
            }
        first=last;
    }

    std::vector<ChordRegionData> result;
    for(const auto& slice:slices)
    {
        if(slice.name.isEmpty())continue;
        if(!result.empty()&&chordWithoutBass(result.back().name)==slice.base
           &&slice.start<=result.back().endPpq+0.000001)
        {
            result.back().endPpq=slice.end;
            if(slice.confidence>result.back().confidence)
            {
                result.back().name=slice.name;result.back().confidence=slice.confidence;
                result.back().alternatives=slice.alternatives;
            }
        }
        else result.push_back({slice.start,slice.end,slice.name,"Audio",slice.confidence,slice.alternatives,false});
    }

    // Inversion labels need sustained bass evidence. A single high-confidence
    // tail frame must not rename an otherwise root-position chord region.
    for(auto& region:result)
    {
        struct LabelVote { juce::String name; float weight=0.0f,confidence=0.0f; juce::StringArray alternatives; };
        std::vector<LabelVote> votes;
        const auto regionBase=chordWithoutBass(region.name);
        for(const auto& slice:slices)
        {
            if(slice.base!=regionBase||slice.name.isEmpty())continue;
            const auto overlap=juce::jmin(region.endPpq,slice.end)-juce::jmax(region.startPpq,slice.start);
            if(overlap<=0.0)continue;
            auto found=std::find_if(votes.begin(),votes.end(),[&](const auto& vote)
            {
                return vote.name==slice.name;
            });
            if(found==votes.end())
            {
                votes.push_back({slice.name,0.0f,slice.confidence,slice.alternatives});
                found=votes.end()-1;
            }
            found->weight+=(float)overlap*(0.55f+slice.confidence*0.45f);
            if(slice.confidence>found->confidence)
            {
                found->confidence=slice.confidence;found->alternatives=slice.alternatives;
            }
        }
        if(!votes.empty())
        {
            const auto selected=std::max_element(votes.begin(),votes.end(),[](const auto& left,const auto& right)
            {
                return left.weight<right.weight;
            });
            region.name=selected->name;region.confidence=juce::jmax(region.confidence,selected->confidence);
            region.alternatives=selected->alternatives;
        }
    }

    // Long bass windows overlap adjacent chords. Require an actual note onset
    // near the region boundary before presenting a slash-bass interpretation.
    const auto namedPitchClass=[](const juce::String& name)
    {
        static constexpr const char* spellings[]{"C","C#","D","Eb","E","F",
                                                  "F#","G","Ab","A","Bb","B"};
        for(int pitchClass=0;pitchClass<12;++pitchClass)
            if(name==spellings[pitchClass])return pitchClass;
        if(name=="Db")return 1;if(name=="Gb")return 6;if(name=="G#")return 8;
        if(name=="A#")return 10;
        return -1;
    };
    for(auto& region:result)
    {
        const auto slash=region.name.lastIndexOfChar('/');
        if(slash<0)continue;
        const auto bassPitchClass=namedPitchClass(region.name.substring(slash+1));
        auto lowestAttackedMidi=128;
        for(const auto& note:notes)
            if(note.midiNote>=0&&note.confidence>=0.10f
               &&note.startPpq>=region.startPpq-0.45&&note.startPpq<=region.startPpq+0.45)
                lowestAttackedMidi=juce::jmin(lowestAttackedMidi,note.midiNote);
        if(lowestAttackedMidi>=128||lowestAttackedMidi%12!=bassPitchClass)
            region.name=region.name.substring(0,slash);
    }

    // A strum becomes identifiable incrementally. Treat a brief triad label at
    // the same attack as a refinement when the following region adds its seventh
    // or extension, rather than rendering both labels as separate chords.
    for(size_t index=0;index+1<result.size();)
    {
        const auto firstBase=chordWithoutBass(result[index].name);
        const auto secondBase=chordWithoutBass(result[index+1].name);
        const auto firstRoot=chordRoot(firstBase);
        const auto simpleTriad=firstBase==firstRoot||firstBase==firstRoot+"m";
        if(simpleTriad&&secondBase.startsWith(firstBase)&&firstBase!=secondBase
           &&result[index].endPpq-result[index].startPpq<=0.50
           &&result[index+1].startPpq<=result[index].endPpq+0.000001)
        {
            result[index+1].startPpq=result[index].startPpq;
            result.erase(result.begin()+(std::ptrdiff_t)index);
            continue;
        }
        ++index;
    }

    const auto nearestAttackBurst=[&](double around)
    {
        std::vector<double> attackStarts;
        for(const auto& note:notes)
            if(note.midiNote>=0&&note.midiNote<=127&&note.confidence>=0.10f
               &&note.startPpq>=around-0.30&&note.startPpq<=around+0.40)
                attackStarts.push_back(note.startPpq);
        std::sort(attackStarts.begin(),attackStarts.end());
        attackStarts.erase(std::unique(attackStarts.begin(),attackStarts.end()),attackStarts.end());
        auto best=std::numeric_limits<double>::infinity();
        for(const auto candidate:attackStarts)
        {
            std::array<bool,12> pitchClasses{};
            auto pitchClassCount=0;
            for(const auto& note:notes)
                if(note.midiNote>=0&&note.midiNote<=127&&note.confidence>=0.10f
                   &&note.startPpq>=candidate-0.000001&&note.startPpq<=candidate+0.35)
                {
                    const auto pitchClass=(size_t)(note.midiNote%12);
                    if(!pitchClasses[pitchClass])
                    {
                        pitchClasses[pitchClass]=true;
                        ++pitchClassCount;
                    }
                }
            if(pitchClassCount>=3&&std::abs(candidate-around)<std::abs(best-around))best=candidate;
        }
        return best;
    };
    const auto nearestHarmonicChange=[&](double around)
    {
        auto best=std::numeric_limits<double>::infinity();
        auto bestScore=0.42f;
        if(harmonicFrames==nullptr)return best;
        for(const auto& frame:*harmonicFrames)
        {
            const auto centre=(frame.startPpq+frame.endPpq)*0.5;
            if(centre<around-0.35||centre>around+0.35)continue;
            const auto proximity=1.0f-(float)(std::abs(centre-around)/0.35);
            const auto score=frame.changeConfidence*(0.75f+proximity*0.25f);
            if(score>bestScore)
            {
                best=centre;bestScore=score;
            }
        }
        return best;
    };
    for(size_t index=0;index<result.size();++index)
    {
        const auto burstStart=nearestAttackBurst(result[index].startPpq);
        const auto harmonicStart=nearestHarmonicChange(result[index].startPpq);
        const auto boundary=std::isfinite(burstStart)?burstStart:harmonicStart;
        if(!std::isfinite(boundary)||std::abs(boundary-result[index].startPpq)>0.30)continue;
        if(index>0)result[index-1].endPpq=boundary;
        result[index].startPpq=boundary;
    }

    // Different extensions of the same root commonly win at the attack and in
    // the decay. Without a second attack they are one chord; retain the label
    // supported for the longer part of the region.
    for(size_t index=1;index<result.size();)
    {
        auto& previous=result[index-1];
        const auto previousDuration=previous.endPpq-previous.startPpq;
        const auto currentDuration=result[index].endPpq-result[index].startPpq;
        const auto previousBase=chordWithoutBass(previous.name);
        const auto currentBase=chordWithoutBass(result[index].name);
        const auto root=chordRoot(previousBase);
        const auto previousIsTriad=previousBase==root||previousBase==root+"m";
        const auto currentIsTriad=currentBase==root||currentBase==root+"m";
        if(currentBase==root+"5"&&currentDuration<=0.75)
        {
            previous.endPpq=result[index].endPpq;
            result.erase(result.begin()+(std::ptrdiff_t)index);
            continue;
        }
        if(root==chordRoot(currentBase)
           &&(previousDuration<=0.50||currentDuration<=0.75
              ||!std::isfinite(nearestAttackBurst(result[index].startPpq))))
        {
            if((previousIsTriad&&!currentIsTriad)
               ||(currentDuration>previousDuration&&!(currentIsTriad&&!previousIsTriad)))
            {
                previous.name=result[index].name;
                previous.confidence=result[index].confidence;
                previous.alternatives=result[index].alternatives;
            }
            previous.endPpq=result[index].endPpq;
            result.erase(result.begin()+(std::ptrdiff_t)index);
            continue;
        }
        ++index;
    }

    // A decay profile can momentarily resemble another extended chord. Keep a
    // short label only when its own boundary has a clustered note attack; an
    // attack later inside the label belongs to the following chord instead.
    for(size_t index=1;index+1<result.size();)
    {
        const auto duration=result[index].endPpq-result[index].startPpq;
        const auto& name=result[index].name;
        const auto transitionLike=name.containsIgnoreCase("sus")||name.containsIgnoreCase("add")
            ||name.containsChar('/')||name.contains("9")||name.contains("11")||name.contains("13");
        const auto nextDuration=result[index+1].endPpq-result[index+1].startPpq;
        if(transitionLike&&duration<=0.40&&nextDuration>=0.75)
        {
            result[index+1].startPpq=result[index].startPpq;
            result.erase(result.begin()+(std::ptrdiff_t)index);
            if(index>1)--index;
            continue;
        }
        if(transitionLike&&duration<=1.50
           &&!std::isfinite(nearestAttackBurst(result[index].startPpq))
           &&std::isfinite(nearestAttackBurst(result[index+1].startPpq)))
        {
            result[index-1].endPpq=result[index].endPpq;
            result.erase(result.begin()+(std::ptrdiff_t)index);
            continue;
        }
        ++index;
    }

    // A single analysis slice between two established chords is normally the
    // release/attack overlap. Remove that label and place the boundary at its centre.
    const auto hasConfirmedAttackBurst=[&](const ChordRegionData& region)
    {
        std::array<bool,12> attackedPitchClasses{};
        auto attackCount=0;
        for(const auto& note:notes)
            if(note.midiNote>=0&&note.midiNote<=127&&note.confidence>=0.10f
               &&note.startPpq>=region.startPpq-0.40
               &&note.startPpq<=region.endPpq+0.000001)
            {
                const auto pitchClass=(size_t)(note.midiNote%12);
                if(!attackedPitchClasses[pitchClass])
                {
                    attackedPitchClasses[pitchClass]=true;
                    ++attackCount;
                }
            }
        return attackCount>=3;
    };

    // Decaying sevenths and extensions can briefly collapse to their root triad.
    // Without a new attack this is loss of upper-note evidence, not a chord change.
    const auto isSimpleRootTriad=[](const juce::String& chord)
    {
        const auto root=chordRoot(chord);
        return chord==root||chord==root+"m";
    };
    const auto extendedChordContainsTriad=[&](const juce::String& extended,const juce::String& triad)
    {
        if(!isSimpleRootTriad(triad))return false;
        const auto extendedRootName=chordRoot(extended),triadRootName=chordRoot(triad);
        const auto extendedRoot=namedPitchClass(extendedRootName),triadRoot=namedPitchClass(triadRootName);
        if(extendedRoot<0||triadRoot<0)return false;
        const auto suffix=extended.substring(extendedRootName.length());
        const Pattern* extendedPattern=nullptr;
        for(const auto& pattern:patterns)
            if(suffix==pattern.suffix){extendedPattern=&pattern;break;}
        if(extendedPattern==nullptr)return false;
        const std::array<int,3> triadIntervals=triad==triadRootName+"m"
            ?std::array<int,3>{0,3,7}:std::array<int,3>{0,4,7};
        for(const auto interval:triadIntervals)
        {
            const auto globalPitchClass=(triadRoot+interval)%12;
            const auto extendedInterval=(globalPitchClass-extendedRoot+12)%12;
            if(std::find(extendedPattern->tones.begin(),extendedPattern->tones.end(),extendedInterval)
               ==extendedPattern->tones.end())return false;
        }
        return true;
    };
    for(size_t index=1;index+1<result.size();)
    {
        const auto previousBase=chordWithoutBass(result[index-1].name);
        const auto currentBase=chordWithoutBass(result[index].name);
        const auto nextBase=chordWithoutBass(result[index+1].name);
        const auto duration=result[index].endPpq-result[index].startPpq;
        if(previousBase==nextBase&&isSimpleRootTriad(currentBase)
           &&chordRoot(previousBase)==chordRoot(currentBase)
           &&duration<=0.75)
        {
            result[index-1].endPpq=result[index+1].endPpq;
            if(result[index+1].confidence>result[index-1].confidence)
            {
                result[index-1].name=result[index+1].name;
                result[index-1].confidence=result[index+1].confidence;
                result[index-1].alternatives=result[index+1].alternatives;
            }
            result.erase(result.begin()+(std::ptrdiff_t)index,
                         result.begin()+(std::ptrdiff_t)index+2);
            continue;
        }
        ++index;
    }
    if(result.size()>=2)
    {
        const auto& tail=result.back();
        const auto previousBase=chordWithoutBass(result[result.size()-2].name);
        const auto tailBase=chordWithoutBass(tail.name);
        if(isSimpleRootTriad(tailBase)
           &&(chordRoot(previousBase)==chordRoot(tailBase)
              ||extendedChordContainsTriad(previousBase,tailBase))
           &&!hasConfirmedAttackBurst(tail))
        {
            result[result.size()-2].endPpq=tail.endPpq;
            result.pop_back();
        }
    }
    for(size_t index=0;index<result.size();)
    {
        const auto regionDuration=result[index].endPpq-result[index].startPpq;
        if(regionDuration<slicePpq*2.5+0.000001
           &&(regionDuration<slicePpq*1.5||!hasConfirmedAttackBurst(result[index])))
        {
            if(index>0&&index+1<result.size())
            {
                const auto boundary=(result[index].startPpq+result[index].endPpq)*0.5;
                result[index-1].endPpq=boundary;result[index+1].startPpq=boundary;
                result.erase(result.begin()+(std::ptrdiff_t)index);continue;
            }
            if(index>0)
            {
                result[index-1].endPpq=result[index].endPpq;
                result.erase(result.begin()+(std::ptrdiff_t)index);continue;
            }
            if(index+1<result.size())
            {
                result[index+1].startPpq=result[index].startPpq;
                result.erase(result.begin()+(std::ptrdiff_t)index);continue;
            }
        }
        ++index;
    }

    for(auto& region:result)
    {
        std::array<float,12> summedWeights{},strongestWeights{};
        std::array<float,128> midiEvidence{};
        for(const auto& note:notes)
        {
            if(note.midiNote<0||note.midiNote>127)continue;
            const auto overlap=juce::jmin(region.endPpq,note.endPpq)
                              -juce::jmax(region.startPpq,note.startPpq);
            if(overlap<=0.0)continue;
            const auto evidence=(float)(overlap/(region.endPpq-region.startPpq))
                               *juce::jlimit(0.05f,1.0f,note.confidence);
            const auto pitchClass=(size_t)(note.midiNote%12);
            summedWeights[pitchClass]+=evidence;
            strongestWeights[pitchClass]=juce::jmax(strongestWeights[pitchClass],evidence);
            midiEvidence[(size_t)note.midiNote]+=evidence;
        }
        std::array<float,12> regionWeights{};
        for(size_t pitchClass=0;pitchClass<regionWeights.size();++pitchClass)
            regionWeights[pitchClass]=strongestWeights[pitchClass]
                                      +(summedWeights[pitchClass]-strongestWeights[pitchClass])*0.18f;
        const auto noteOnlyWeights=regionWeights;
        if(harmonicFrames!=nullptr)
        {
            std::array<float,12> acousticWeights{};auto acousticFrameWeight=0.0f;
            for(const auto& frame:*harmonicFrames)
            {
                const auto overlap=juce::jmin(region.endPpq,frame.endPpq)
                                  -juce::jmax(region.startPpq,frame.startPpq);
                if(overlap<=0.0)continue;
                const auto contribution=(float)(overlap/(region.endPpq-region.startPpq))
                                        *juce::jlimit(0.0f,1.0f,frame.confidence);
                acousticFrameWeight+=contribution;
                for(size_t pitchClass=0;pitchClass<acousticWeights.size();++pitchClass)
                    acousticWeights[pitchClass]+=frame.pitchWeights[pitchClass]*contribution;
            }
            if(acousticFrameWeight>0.0f)
            {
                for(auto& weight:acousticWeights)weight/=acousticFrameWeight;
                const auto acousticPeak=*std::max_element(acousticWeights.begin(),acousticWeights.end());
                const auto neuralPeak=*std::max_element(regionWeights.begin(),regionWeights.end());
                if(acousticPeak>0.0f)
                    for(size_t pitchClass=0;pitchClass<regionWeights.size();++pitchClass)
                    {
                        const auto acoustic=acousticWeights[pitchClass]/acousticPeak;
                        const auto neural=neuralPeak>0.0f?regionWeights[pitchClass]/neuralPeak:0.0f;
                        regionWeights[pitchClass]=neural*0.60f+acoustic*0.27f
                                                 +juce::jmin(neural,acoustic)*0.16f;
                    }
            }
        }
        const auto strongestMidi=*std::max_element(midiEvidence.begin(),midiEvidence.end());
        auto bassMidi=128;
        for(int midi=0;midi<128;++midi)
            if(midiEvidence[(size_t)midi]>=juce::jmax(0.015f,strongestMidi*0.25f))
            {
                bassMidi=midi;
                break;
            }
        if(bassMidi>=128)continue;
        float canonicalConfidence=0.0f;
        juce::StringArray canonicalAlternatives;
        const auto canonical=identifyChord(regionWeights,canonicalConfidence,bassMidi%12,
                                             &canonicalAlternatives,&keyContext,0.055f);
        float noteOnlyConfidence=0.0f;
        juce::StringArray noteOnlyAlternatives;
        const auto noteOnlyCanonical=identifyChord(noteOnlyWeights,noteOnlyConfidence,bassMidi%12,
                                                    &noteOnlyAlternatives,&keyContext,0.055f);
        const auto currentBase=chordWithoutBass(region.name);
        const auto canonicalBase=chordWithoutBass(canonical);
        const auto noteOnlyBase=chordWithoutBass(noteOnlyCanonical);
        const auto currentIsMajorSixth=currentBase.endsWithChar('6')&&!currentBase.endsWith("m6");
        const auto sameRoot=chordRoot(currentBase)==chordRoot(canonicalBase);
        const auto simplerSameRoot=harmonicFrames!=nullptr&&sameRoot&&canonicalBase!=currentBase
                                   &&canonicalBase.length()<currentBase.length();
        const auto regionPeak=*std::max_element(regionWeights.begin(),regionWeights.end());
        const auto currentRootPitchClass=namedPitchClass(chordRoot(currentBase));
        const auto weakMajorSeventh=(currentBase.contains("maj7")||currentBase.contains("maj9"))
                                     &&currentRootPitchClass>=0&&regionPeak>0.0f
                                     &&regionWeights[(size_t)((currentRootPitchClass+11)%12)]<regionPeak*0.20f;
        const auto currentHasFragileColour=currentBase.contains("b9")||currentBase.contains("#9")
                                           ||currentBase.contains("#11")||currentBase.contains("b13")
                                           ||currentBase.contains("mMaj")||weakMajorSeventh;
        const auto simplerNoteOnly=harmonicFrames!=nullptr&&currentHasFragileColour
                                   &&chordRoot(currentBase)==chordRoot(noteOnlyBase)
                                   &&noteOnlyBase!=currentBase&&noteOnlyBase.length()<currentBase.length()
                                   &&noteOnlyConfidence>=0.42f;
        const auto currentBass=region.name.fromFirstOccurrenceOf("/",false,false);
        const auto canonicalUsesCurrentBass=harmonicFrames!=nullptr&&!currentBass.isEmpty()
                                            &&chordRoot(canonicalBase)==currentBass;
        if(simplerNoteOnly)
        {
            region.name=noteOnlyCanonical;
            region.confidence=juce::jmax(region.confidence,noteOnlyConfidence);
            region.alternatives=noteOnlyAlternatives;
        }
        else if((currentIsMajorSixth&&canonicalBase.endsWith("m7"))
           ||(canonicalConfidence>=0.42f&&(simplerSameRoot||canonicalUsesCurrentBass)))
        {
            region.name=canonical;
            region.confidence=juce::jmax(region.confidence,canonicalConfidence);
            region.alternatives=canonicalAlternatives;
        }
        const auto resultingBase=chordWithoutBass(region.name);
        const auto resultingRoot=chordRoot(resultingBase);
        const auto resultingRootPitchClass=namedPitchClass(resultingRoot);
        const auto minorSeventhName=resultingRoot+"m7";
        const auto key=estimateKey(&keyContext);
        if(harmonicFrames!=nullptr&&resultingBase==resultingRoot+"m"
           &&resultingRootPitchClass>=0&&regionPeak>0.0f
           &&region.endPpq-region.startPpq>=1.0
           &&pitchClassIsInKey((resultingRootPitchClass+10)%12,key)
           &&(canonicalAlternatives.contains(minorSeventhName)
              ||noteOnlyAlternatives.contains(minorSeventhName)
              ||region.alternatives.contains(minorSeventhName)))
        {
            region.name=minorSeventhName;
            region.confidence=juce::jmax(region.confidence,canonicalConfidence*0.92f);
        }
    }
    for(size_t index=1;index<result.size();)
    {
        const auto previousBase=chordWithoutBass(result[index-1].name);
        const auto currentBase=chordWithoutBass(result[index].name);
        const auto currentDuration=result[index].endPpq-result[index].startPpq;
        if(chordRoot(previousBase)==chordRoot(currentBase)&&currentDuration<=0.75)
        {
            result[index-1].endPpq=result[index].endPpq;
            result.erase(result.begin()+(std::ptrdiff_t)index);
            continue;
        }
        ++index;
    }
    std::vector<ChordRegionData> canonicalMerged;
    canonicalMerged.reserve(result.size());
    for(auto& region:result)
    {
        if(!canonicalMerged.empty()
           &&chordWithoutBass(canonicalMerged.back().name)==chordWithoutBass(region.name)
           &&region.startPpq<=canonicalMerged.back().endPpq+0.1875)
        {
            canonicalMerged.back().endPpq=juce::jmax(canonicalMerged.back().endPpq,region.endPpq);
            if(region.confidence>canonicalMerged.back().confidence)
            {
                canonicalMerged.back().name=region.name;
                canonicalMerged.back().confidence=region.confidence;
            }
            if(!region.alternatives.isEmpty())canonicalMerged.back().alternatives=region.alternatives;
        }
        else canonicalMerged.push_back(std::move(region));
    }
    return canonicalMerged;
}

PhaseSafeDownmixPlan createPhaseSafeDownmixPlan(const juce::AudioBuffer<float>& buffer) noexcept
{
    PhaseSafeDownmixPlan plan;
    plan.channels=juce::jmax(1,buffer.getNumChannels());
    if(buffer.getNumChannels()!=2||buffer.getNumSamples()<=0)return plan;
    double leftEnergy=0.0,rightEnergy=0.0,crossEnergy=0.0;
    const auto* left=buffer.getReadPointer(0);
    const auto* right=buffer.getReadPointer(1);
    for(int sample=0;sample<buffer.getNumSamples();++sample)
    {
        leftEnergy+=(double)left[sample]*left[sample];
        rightEnergy+=(double)right[sample]*right[sample];
        crossEnergy+=(double)left[sample]*right[sample];
    }
    plan.dominantChannel=rightEnergy>leftEnergy?1:0;
    const auto denominator=std::sqrt(leftEnergy*rightEnergy);
    plan.weightedStereo=denominator>1.0e-12&&crossEnergy/denominator< -0.45;
    return plan;
}

float phaseSafeDownmixSample(const juce::AudioBuffer<float>& buffer,int sample,
                             const PhaseSafeDownmixPlan& plan) noexcept
{
    if(buffer.getNumChannels()<=0||sample<0||sample>=buffer.getNumSamples())return 0.0f;
    if(plan.weightedStereo&&buffer.getNumChannels()==2)
    {
        const auto secondary=1-plan.dominantChannel;
        return buffer.getSample(plan.dominantChannel,sample)*0.70f
               +buffer.getSample(secondary,sample)*0.30f;
    }
    float mixed=0.0f;
    const auto channels=juce::jmin(plan.channels,buffer.getNumChannels());
    for(int channel=0;channel<channels;++channel)mixed+=buffer.getSample(channel,sample);
    return channels>0?mixed/(float)channels:0.0f;
}

void stabilizeHarmonicFrames(std::vector<HarmonicFrameEvidence>& frames)
{
    if(frames.size()<3)return;
    const auto original=frames;
    for(size_t index=1;index+1<frames.size();++index)
    {
        const auto previousCentre=(original[index-1].startPpq+original[index-1].endPpq)*0.5;
        const auto centre=(original[index].startPpq+original[index].endPpq)*0.5;
        const auto nextCentre=(original[index+1].startPpq+original[index+1].endPpq)*0.5;
        const auto localSupport=juce::jmax(0.01,original[index].endPpq-original[index].startPpq);
        if(centre-previousCentre>localSupport*3.5||nextCentre-centre>localSupport*3.5)continue;

        const auto entering=original[index].changeConfidence;
        const auto leaving=original[index+1].changeConfidence;
        const auto isolatedExcursion=entering>0.45f&&leaving>0.45f;
        const auto sustainedBoundary=entering>0.45f||leaving>0.45f;
        if(sustainedBoundary&&!isolatedExcursion)continue;

        std::array<float,12> medianProfile{};
        for(size_t pitchClass=0;pitchClass<medianProfile.size();++pitchClass)
        {
            std::array<float,3> values{original[index-1].pitchWeights[pitchClass],
                                       original[index].pitchWeights[pitchClass],
                                       original[index+1].pitchWeights[pitchClass]};
            std::sort(values.begin(),values.end());
            medianProfile[pitchClass]=values[1];
        }
        const auto blend=isolatedExcursion?0.72f:0.34f;
        auto distance=0.0f;
        for(size_t pitchClass=0;pitchClass<medianProfile.size();++pitchClass)
        {
            distance+=std::abs(original[index].pitchWeights[pitchClass]-medianProfile[pitchClass]);
            frames[index].pitchWeights[pitchClass]=original[index].pitchWeights[pitchClass]*(1.0f-blend)
                                                   +medianProfile[pitchClass]*blend;
        }
        const auto peak=*std::max_element(frames[index].pitchWeights.begin(),
                                           frames[index].pitchWeights.end());
        if(peak>0.0f)for(auto& weight:frames[index].pitchWeights)weight/=peak;
        const auto coherence=1.0f-juce::jlimit(0.0f,1.0f,distance/6.0f);
        frames[index].confidence*=isolatedExcursion?0.42f+coherence*0.28f
                                                    :0.78f+coherence*0.22f;
        if(isolatedExcursion)
        {
            frames[index].changeConfidence*=0.25f;
            frames[index+1].changeConfidence*=0.40f;
        }
    }
}

std::array<float,12> calculateConstantQHpcp(const float* samples,const float* window,int frameSize,double sampleRate,
                                            int* bassPitchClass)
{
    std::array<float,12> hpcp{};
    if(bassPitchClass!=nullptr)*bassPitchClass=-1;
    double energy=0.0;for(int i=0;i<frameSize;++i)energy+=samples[i]*samples[i];
    if(frameSize<=0||energy/frameSize<1.0e-8)return hpcp;
    constexpr int lowestMidi=33,highestMidi=96,pitchCount=highestMidi-lowestMidi+1;
    std::array<float,pitchCount> pitchEnergy{};
    for(int midi=33;midi<=96;++midi)
    {
        double strongest=0.0;
        for(const auto detune:{-0.25,0.0,0.25})
        {
            const auto frequency=440.0*std::pow(2.0,(midi+detune-69.0)/12.0);
            const auto coefficient=2.0*std::cos(2.0*juce::MathConstants<double>::pi*frequency/sampleRate);
            double previous=0.0,previous2=0.0;
            for(int i=0;i<frameSize;++i)
            {
                const auto value=samples[i]*window[i]+coefficient*previous-previous2;
                previous2=previous;previous=value;
            }
            strongest=juce::jmax(strongest,std::sqrt(juce::jmax(0.0,previous2*previous2+previous*previous-coefficient*previous*previous2)));
        }
        const auto frequency=440.0*std::pow(2.0,(midi-69.0)/12.0);
        pitchEnergy[(size_t)(midi-lowestMidi)]=(float)(strongest/std::sqrt(frequency));
    }

    const auto rawPeak=*std::max_element(pitchEnergy.begin(),pitchEnergy.end());
    if(rawPeak<=0.0f)return hpcp;

    // Remove energy that is explained by lower fundamentals before folding the
    // spectrum into pitch classes. This keeps an instrument's partials from
    // being interpreted as independent chord extensions.
    constexpr int harmonicOffsets[]{12,19,24,28,31,34,36};
    constexpr float harmonicWeights[]{0.62f,0.34f,0.24f,0.16f,0.12f,0.09f,0.07f};
    auto residual=pitchEnergy;
    std::array<std::array<float,6>,12> pitchClassSalience{};
    std::array<int,12> salienceCounts{};
    for(int midi=lowestMidi;midi<=highestMidi;++midi)
    {
        const auto direct=residual[(size_t)(midi-lowestMidi)];
        if(direct<rawPeak*0.035f)continue;
        auto& count=salienceCounts[(size_t)(midi%12)];
        if(count<(int)pitchClassSalience[(size_t)(midi%12)].size())
            pitchClassSalience[(size_t)(midi%12)][(size_t)count++]=direct;
        for(size_t harmonic=0;harmonic<std::size(harmonicOffsets);++harmonic)
        {
            const auto partialMidi=midi+harmonicOffsets[harmonic];
            if(partialMidi<=highestMidi)
            {
                auto& partial=residual[(size_t)(partialMidi-lowestMidi)];
                partial=juce::jmax(0.0f,partial-direct*harmonicWeights[harmonic]);
            }
        }
    }
    if(bassPitchClass!=nullptr)
        for(int midi=lowestMidi;midi<=highestMidi;++midi)
            if(residual[(size_t)(midi-lowestMidi)]>=rawPeak*0.15f){*bassPitchClass=midi%12;break;}
    for(size_t pc=0;pc<12;++pc)
    {
        auto& values=pitchClassSalience[pc];
        std::sort(values.begin(),values.end(),std::greater<float>());
        hpcp[pc]=values[0]+values[1]*0.22f+values[2]*0.07f;
    }
    const auto peak=*std::max_element(hpcp.begin(),hpcp.end());
    if(peak>0.0f)for(auto& value:hpcp)value=value/peak<0.055f?0.0f:value/peak;
    return hpcp;
}

void AudioChordStabilizer::reset()
{
    currentChord.clear();pendingChord.clear();currentConfidence=pendingConfidence=0.0f;
    currentAlternatives.clear();pendingAlternatives.clear();pendingStartPpq=-1.0;pendingObservations=0;
}

MidiChordUpdate AudioChordStabilizer::process(const juce::String& chord,float confidence,
                                              const juce::StringArray& alternatives,double ppq,bool onset)
{
    if(chord.isEmpty()||chord=="--")return {};
    const auto setCurrent=[&](double startPpq)
    {
        currentChord=chord;currentConfidence=confidence;currentAlternatives=alternatives;
        pendingChord.clear();pendingAlternatives.clear();pendingStartPpq=-1.0;pendingObservations=0;
        return MidiChordUpdate{currentChord,ChordUpdateKind::start,currentConfidence,currentAlternatives,startPpq};
    };
    if(currentChord.isEmpty())return setCurrent(ppq);
    const auto observedBase=chordWithoutBass(chord),currentBase=chordWithoutBass(currentChord);
    const auto sameBase=observedBase==currentBase;
    const auto sameRoot=chordRoot(observedBase)==chordRoot(currentBase);
    const auto lessSpecificSameRoot=sameRoot&&(observedBase.endsWithChar('5')||observedBase.length()<currentBase.length());
    if(chord==currentChord||sameBase||lessSpecificSameRoot)
    {
        currentConfidence=juce::jmax(currentConfidence,confidence);currentAlternatives=alternatives;
        pendingChord.clear();pendingAlternatives.clear();pendingStartPpq=-1.0;pendingObservations=0;
        return {currentChord,ChordUpdateKind::extend,currentConfidence,currentAlternatives,-1.0};
    }

    const auto advanced=chord.containsChar('9')||chord.contains("11")||chord.contains("13");
    if(!advanced)return setCurrent(ppq);
    if(alternatives.contains(currentChord))
    {
        pendingChord.clear();pendingAlternatives.clear();pendingStartPpq=-1.0;pendingObservations=0;
        return {currentChord,ChordUpdateKind::extend,currentConfidence,currentAlternatives,-1.0};
    }
    if(pendingChord!=chord||pendingStartPpq<0.0||ppq-pendingStartPpq>3.25)
    {
        pendingChord=chord;pendingConfidence=confidence;pendingAlternatives=alternatives;
        pendingStartPpq=ppq;pendingObservations=1;
    }
    else
    {
        ++pendingObservations;pendingConfidence=juce::jmax(pendingConfidence,confidence);
        if(!alternatives.isEmpty())pendingAlternatives=alternatives;
    }
    const auto observationsRequired=onset?2:3;
    if(pendingObservations<observationsRequired)
        return {currentChord,ChordUpdateKind::extend,currentConfidence,currentAlternatives,-1.0};

    const auto confirmedStart=pendingStartPpq;
    currentChord=pendingChord;currentConfidence=pendingConfidence;currentAlternatives=pendingAlternatives;
    pendingChord.clear();pendingAlternatives.clear();pendingStartPpq=-1.0;pendingObservations=0;
    return {currentChord,ChordUpdateKind::start,currentConfidence,currentAlternatives,confirmedStart};
}

void MidiChordDetector::reset()
{
    heldNotes.fill(false);
    sustainedNotes.fill(false);
    keyWeights.fill(0.0f);
    waitingForAttack = true;
    pendingRegionStart = false;
    provisionalAddition = false;
    sustainDown = false;
    pendingStartPpq = provisionalStartPpq = -1.0;
    currentChord.clear();
    currentConfidence = 0.0f;
    currentAlternatives.clear();
}

MidiChordUpdate MidiChordDetector::process(const juce::MidiBuffer& midi,double ppq)
{
    constexpr double overlapGracePpq=0.125;
    bool addedNotes = false,releasedNotes=false;
    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();
        if (message.isNoteOn())
        {
            const auto note = (size_t) message.getNoteNumber();
            if (!heldNotes[note])
            {
                if (waitingForAttack)
                {
                    pendingRegionStart = true;
                    if(pendingStartPpq<0.0)pendingStartPpq=ppq;
                    waitingForAttack = false;
                }
                else if(!pendingRegionStart&&!currentChord.isEmpty()&&!provisionalAddition)
                {
                    provisionalAddition=true;
                    provisionalStartPpq=ppq;
                }
                heldNotes[note] = true;
                sustainedNotes[note] = false;
                addedNotes = true;
            }
        }
        else if (message.isNoteOff())
        {
            const auto note=(size_t)message.getNoteNumber();
            heldNotes[note] = false;
            sustainedNotes[note] = sustainDown;
            waitingForAttack = true;
            releasedNotes=true;
        }
        else if (message.isAllNotesOff())
        {
            heldNotes.fill(false);
            sustainedNotes.fill(false);
            waitingForAttack = true;
            pendingRegionStart = false;
            provisionalAddition = false;
            pendingStartPpq = provisionalStartPpq = -1.0;
        }
        else if(message.isController()&&message.getControllerNumber()==64)
        {
            const auto wasDown=sustainDown;
            sustainDown=message.getControllerValue()>=64;
            if(wasDown&&!sustainDown) sustainedNotes.fill(false);
        }
    }

    if(provisionalAddition&&releasedNotes)
    {
        pendingRegionStart=true;
        pendingStartPpq=provisionalStartPpq;
        provisionalAddition=false;
        provisionalStartPpq=-1.0;
        waitingForAttack=false;
    }

    std::array<float,12> weights{};
    int count = 0, bass = -1;
    for (int note=0; note<128; ++note)
    {
        if (!heldNotes[(size_t)note]) continue;
        if (bass < 0) bass = note % 12;
        weights[(size_t)(note%12)] += 1.0f;
        ++count;
    }
    if (count < 2)
    {
        const auto hasSustained=std::any_of(sustainedNotes.begin(),sustainedNotes.end(),[](bool value){return value;});
        if(!addedNotes&&!pendingRegionStart&&sustainDown&&hasSustained&&!currentChord.isEmpty())
            return {currentChord,ChordUpdateKind::extend,currentConfidence,currentAlternatives,-1.0};
        return {};
    }

    if(addedNotes) for(size_t pc=0;pc<12;++pc) keyWeights[pc]=keyWeights[pc]*0.985f+weights[pc];
    float confidence = 0.0f; juce::StringArray alternatives;
    const auto identified = identifyChord(weights, confidence, bass, &alternatives, &keyWeights);
    if (pendingRegionStart)
    {
        pendingRegionStart = false;
        const auto regionStart=pendingStartPpq;
        pendingStartPpq=-1.0;
        provisionalAddition=false;provisionalStartPpq=-1.0;
        currentChord = identified;
        currentConfidence=confidence; currentAlternatives=alternatives;
        return {currentChord, ChordUpdateKind::start, currentConfidence, currentAlternatives,regionStart};
    }
    if(provisionalAddition)
    {
        if(ppq-provisionalStartPpq<overlapGracePpq)
            return {currentChord,ChordUpdateKind::extend,currentConfidence,currentAlternatives,-1.0};
        provisionalAddition=false;provisionalStartPpq=-1.0;
        currentChord = identified;
        currentConfidence=confidence; currentAlternatives=alternatives;
        return {currentChord, ChordUpdateKind::refine, currentConfidence, currentAlternatives,-1.0};
    }
    if (currentChord.isEmpty()) { currentChord=identified; currentConfidence=confidence; currentAlternatives=alternatives; }
    return {currentChord, ChordUpdateKind::extend, currentConfidence, currentAlternatives,-1.0};
}
