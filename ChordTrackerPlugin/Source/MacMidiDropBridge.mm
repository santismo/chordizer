#import <AppKit/AppKit.h>
#import <objc/runtime.h>
#include "MacMidiDropBridge.h"

#if JUCE_MAC

#include <cstdio>
#include <map>
#include <optional>
#include <set>

namespace
{
const void* bridgeAssociationKey = &bridgeAssociationKey;

using DragOperationImp = NSDragOperation (*)(id, SEL, id<NSDraggingInfo>);
using PerformDragImp = BOOL (*)(id, SEL, id<NSDraggingInfo>);
using DragExitImp = void (*)(id, SEL, id<NSDraggingInfo>);

struct OriginalMethods
{
    IMP draggingEntered = nullptr;
    IMP draggingUpdated = nullptr;
    IMP performDragOperation = nullptr;
    IMP draggingExited = nullptr;
};

std::map<Class,OriginalMethods>& originalMethodsByClass()
{
    static std::map<Class,OriginalMethods> originals;
    return originals;
}

juce::String nsStringToString(NSString* string)
{
    return string == nil ? juce::String{} : juce::String::fromUTF8([string UTF8String]);
}

NSString* stringToNSString(const juce::String& string)
{
    return [NSString stringWithUTF8String:string.toRawUTF8()];
}

juce::File dropLogFile()
{
    return juce::File::getSpecialLocation(juce::File::userHomeDirectory)
        .getChildFile("Library")
        .getChildFile("Application Support")
        .getChildFile("Santismo")
        .getChildFile("Chordizer")
        .getChildFile("drop-debug.log");
}

void appendDropLog(const juce::String& message)
{
    auto file = dropLogFile();
    file.getParentDirectory().createDirectory();
    file.appendText(juce::Time::getCurrentTime().toString(true,true,true,true) + "  " + message + "\n");
}

NSArray<NSString*>* supportedTypes()
{
    return @[
        NSPasteboardTypeFileURL,
        NSPasteboardTypeString,
        NSPasteboardTypeURL,
        @"com.apple.pasteboard.promised-file-url",
        @"com.apple.pasteboard.promised-file-content-type",
        @"public.midi-audio",
        @"public.midi",
        @"com.apple.midi",
        @"com.apple.musicapps.LGRegionsPboardType",
        @"com.apple.musicapps.CLgDragEditorPboardType",
        @"com.apple.musicapps.GChChordPboardType",
        @"com.apple.musicapps.ScisPBoardType",
        @"GarageBand AU Click Drag Pasteboard Type",
        @"GarageBand Audio Unit Pasteboard Type",
        @"LogicPasteBoardMarker",
        @"LocalSongClipboard"
    ];
}

juce::StringArray pasteboardTypes(NSPasteboard* pasteboard)
{
    juce::StringArray result;
    for(NSString* type in [pasteboard types])
        result.add(nsStringToString(type));
    return result;
}

uint32_t readBigEndianUInt32(const uint8_t* bytes)
{
    return ((uint32_t) bytes[0] << 24u) | ((uint32_t) bytes[1] << 16u)
         | ((uint32_t) bytes[2] << 8u) | (uint32_t) bytes[3];
}

std::optional<std::pair<size_t,size_t>> findStandardMidiRange(NSData* data)
{
    if(data == nil || [data length] < 22) return {};
    const auto* bytes = static_cast<const uint8_t*>([data bytes]);
    const auto length = (size_t) [data length];
    for(size_t offset = 0; offset + 14 <= length; ++offset)
    {
        if(bytes[offset] != 'M' || bytes[offset + 1] != 'T'
           || bytes[offset + 2] != 'h' || bytes[offset + 3] != 'd')
            continue;
        const auto headerLength = readBigEndianUInt32(bytes + offset + 4);
        if(headerLength < 6 || offset + 8 + headerLength > length) continue;
        const auto trackCount = ((uint16_t) bytes[offset + 10] << 8u) | (uint16_t) bytes[offset + 11];
        if(trackCount == 0 || trackCount > 512) continue;

        auto position = offset + 8 + headerLength;
        uint16_t foundTracks = 0;
        while(foundTracks < trackCount && position + 8 <= length)
        {
            if(bytes[position] != 'M' || bytes[position + 1] != 'T'
               || bytes[position + 2] != 'r' || bytes[position + 3] != 'k')
                break;
            const auto trackLength = readBigEndianUInt32(bytes + position + 4);
            if(position + 8 + trackLength > length) break;
            position += 8 + trackLength;
            ++foundTracks;
        }
        if(foundTracks == trackCount && position > offset)
            return std::make_pair(offset,position - offset);
    }
    return {};
}

juce::MemoryBlock extractStandardMidiData(NSData* data)
{
    if(auto range = findStandardMidiRange(data))
    {
        const auto* bytes = static_cast<const uint8_t*>([data bytes]);
        return juce::MemoryBlock(bytes + range->first,range->second);
    }
    return {};
}

juce::String abbreviate(juce::String text,int maximumLength = 420)
{
    text = text.replaceCharacters("\r\n\t","   ");
    while(text.contains("  ")) text = text.replace("  "," ");
    return text.length() > maximumLength ? text.substring(0,maximumLength) + "..." : text;
}

juce::String hexPreview(NSData* data,size_t maximumBytes = 64)
{
    if(data == nil || [data length] == 0) return {};
    const auto* bytes = static_cast<const uint8_t*>([data bytes]);
    const auto count = juce::jmin(maximumBytes,(size_t) [data length]);
    juce::String hex, ascii;
    char byteText[4]{};
    for(size_t index = 0; index < count; ++index)
    {
        std::snprintf(byteText,sizeof(byteText),"%02X",bytes[index]);
        if(index != 0) hex += " ";
        hex += byteText;
        ascii += bytes[index] >= 32 && bytes[index] < 127 ? juce::String::charToString((juce_wchar) bytes[index]) : ".";
    }
    return hex + " | " + ascii;
}

void addStringValue(juce::StringArray& values,const juce::String& value)
{
    if(value.isNotEmpty()) values.addIfNotAlreadyThere(value);
}

void collectPropertyListStrings(id value,juce::StringArray& strings)
{
    if(value == nil) return;
    if([value isKindOfClass:[NSString class]])
    {
        addStringValue(strings,nsStringToString((NSString*) value));
        return;
    }
    if([value isKindOfClass:[NSNumber class]])
    {
        addStringValue(strings,nsStringToString([(NSNumber*) value stringValue]));
        return;
    }
    if([value isKindOfClass:[NSArray class]])
    {
        for(id child in (NSArray*) value) collectPropertyListStrings(child,strings);
        return;
    }
    if([value isKindOfClass:[NSDictionary class]])
    {
        for(id key in (NSDictionary*) value)
        {
            collectPropertyListStrings(key,strings);
            collectPropertyListStrings([(NSDictionary*) value objectForKey:key],strings);
        }
    }
}

juce::StringArray promisedContentHints(NSPasteboard* pasteboard)
{
    juce::StringArray hints;
    for(NSString* type in @[
        @"com.apple.pasteboard.promised-file-content-type",
        @"com.apple.pasteboard.promised-file-name",
        @"com.apple.pasteboard.promised-suggested-file-name",
        @"com.apple.musicapps.file-name"])
    {
        collectPropertyListStrings([pasteboard propertyListForType:type],hints);
        collectPropertyListStrings([pasteboard stringForType:type],hints);
    }

    if(@available(macOS 10.12, *))
    {
        NSArray* promises = [pasteboard readObjectsForClasses:@[[NSFilePromiseReceiver class]] options:nil];
        for(NSFilePromiseReceiver* receiver in promises)
        {
            for(NSString* type in [receiver fileTypes])
                addStringValue(hints,nsStringToString(type));
        }
    }
    return hints;
}

bool hintLooksLikeMidi(const juce::String& hint)
{
    return hint.containsIgnoreCase("midi") || hint.containsIgnoreCase("smf")
        || hint.endsWithIgnoreCase(".mid") || hint.endsWithIgnoreCase(".midi")
        || hint.endsWithIgnoreCase(".smf");
}

bool hasMidiFilePromise(NSPasteboard* pasteboard)
{
    const auto hints = promisedContentHints(pasteboard);
    for(const auto& hint:hints)
        if(hintLooksLikeMidi(hint))
            return true;
    return false;
}

juce::String promiseDiagnostic(NSPasteboard* pasteboard)
{
    const auto hints = promisedContentHints(pasteboard);
    return hints.isEmpty() ? juce::String{} : "Promise hints: " + hints.joinIntoString(", ");
}

void logPasteboardPayloads(NSPasteboard* pasteboard,const juce::String& context)
{
    appendDropLog(context + " " + promiseDiagnostic(pasteboard));
    for(NSString* type in [pasteboard types])
    {
        const auto typeName = nsStringToString(type);
        NSData* data = [pasteboard dataForType:type];
        juce::String line = context + " payload " + typeName;
        if(data != nil) line += " bytes=" + juce::String((int64) [data length]);
        id plist = [pasteboard propertyListForType:type];
        if(plist != nil) line += " plist=" + abbreviate(nsStringToString([plist description]));
        if(data != nil && [data length] > 0) line += " preview=" + hexPreview(data);
        if(auto range = findStandardMidiRange(data))
            line += " embedded-smf-offset=" + juce::String((int64) range->first)
                 + " embedded-smf-bytes=" + juce::String((int64) range->second);
        appendDropLog(line);
    }
}

juce::StringArray filePathsFromPasteboard(NSPasteboard* pasteboard)
{
    juce::StringArray files;
    NSArray* urls = [pasteboard readObjectsForClasses:@[[NSURL class]] options:nil];
    for(NSURL* url in urls)
        if([url isFileURL])
            files.add(nsStringToString([url path]));

    JUCE_BEGIN_IGNORE_DEPRECATION_WARNINGS
    id legacyFiles = [pasteboard propertyListForType:NSFilenamesPboardType];
    JUCE_END_IGNORE_DEPRECATION_WARNINGS
    if([legacyFiles isKindOfClass:[NSArray class]])
        for(NSString* path in (NSArray*) legacyFiles)
            files.addIfNotAlreadyThere(nsStringToString(path));

    return files;
}

juce::MemoryBlock midiDataFromPasteboard(NSPasteboard* pasteboard,juce::String& sourceName)
{
    for(NSString* type in @[@"public.midi-audio", @"public.midi", @"com.apple.midi"])
    {
        NSData* data = [pasteboard dataForType:type];
        auto midi = extractStandardMidiData(data);
        if(midi.getSize() > 0)
        {
            sourceName = nsStringToString(type);
            return midi;
        }
    }

    for(NSString* type in [pasteboard types])
    {
        NSData* data = [pasteboard dataForType:type];
        auto midi = extractStandardMidiData(data);
        if(midi.getSize() > 0)
        {
            sourceName = nsStringToString(type) + " embedded Standard MIDI";
            appendDropLog("found embedded Standard MIDI data in pasteboard type " + nsStringToString(type)
                          + " bytes=" + juce::String((int64) midi.getSize()));
            return midi;
        }
    }

    return {};
}

bool hasPrivateLogicRegionType(NSPasteboard* pasteboard)
{
    for(NSString* type in @[
        @"com.apple.musicapps.LGRegionsPboardType",
        @"com.apple.musicapps.CLgDragEditorPboardType",
        @"com.apple.musicapps.GChChordPboardType",
        @"com.apple.musicapps.ScisPBoardType",
        @"LogicPasteBoardMarker",
        @"LocalSongClipboard"])
    {
        if([[pasteboard types] containsObject:type])
            return true;
    }
    return false;
}

bool hasPromiseReceiver(NSPasteboard* pasteboard)
{
    if(@available(macOS 10.12, *))
    {
        NSArray* promises = [pasteboard readObjectsForClasses:@[[NSFilePromiseReceiver class]] options:nil];
        return [promises count] > 0;
    }
    return false;
}

struct BridgeRecord
{
    MacMidiDropBridge::DropCallback onDrop;
    MacMidiDropBridge::HoverCallback onHover;
    juce::Component::SafePointer<juce::Component> owner;
    NSView* view = nil;
    juce::String lastLoggedTypes;

    explicit BridgeRecord(juce::Component& component,
                          MacMidiDropBridge::DropCallback drop,
                          MacMidiDropBridge::HoverCallback hover)
        : onDrop(std::move(drop)), onHover(std::move(hover)), owner(&component) {}

    void logTypes(NSPasteboard* pasteboard,const juce::String& context)
    {
        const auto types = pasteboardTypes(pasteboard).joinIntoString(", ");
        if(types == lastLoggedTypes && context == "move") return;
        lastLoggedTypes = types;
        appendDropLog(context + " pasteboard types: " + types);
    }

    bool canHandle(id<NSDraggingInfo> sender,bool log)
    {
        NSPasteboard* pasteboard = [sender draggingPasteboard];
        if(log)
        {
            logTypes(pasteboard,"enter");
            appendDropLog("enter " + promiseDiagnostic(pasteboard));
        }

        juce::String source;
        if(midiDataFromPasteboard(pasteboard,source).getSize() > 0) return true;

        const auto files = filePathsFromPasteboard(pasteboard);
        for(const auto& path:files)
            if(path.endsWithIgnoreCase(".mid") || path.endsWithIgnoreCase(".midi") || path.endsWithIgnoreCase(".smf"))
                return true;

        return hasMidiFilePromise(pasteboard) || hasPrivateLogicRegionType(pasteboard);
    }

    bool performDrop(id<NSDraggingInfo> sender)
    {
        NSPasteboard* pasteboard = [sender draggingPasteboard];
        const auto types = pasteboardTypes(pasteboard).joinIntoString(", ");
        appendDropLog("drop pasteboard types: " + types);
        logPasteboardPayloads(pasteboard,"drop");

        MacMidiDropBridge::DropData data;
        const auto promiseInfo = promiseDiagnostic(pasteboard);
        data.pasteboardTypes = promiseInfo.isEmpty() ? types : types + "\n" + promiseInfo;
        data.files = filePathsFromPasteboard(pasteboard);
        data.midiData = midiDataFromPasteboard(pasteboard,data.sourceName);
        if(data.midiData.getSize() > 0 || !data.files.isEmpty())
            return onDrop != nullptr && onDrop(data);

        if(hasMidiFilePromise(pasteboard) && receivePromises(pasteboard,data.pasteboardTypes))
            return true;

        if(hasPromiseReceiver(pasteboard))
        {
            appendDropLog("skipping non-MIDI file promise to avoid Logic audio render");
            data.diagnostic = "Logic offered a file promise, but its promise hints were not MIDI. "
                              "Chordizer skipped the promise so Logic would not render the region as audio.";
        }
        data.sourceName = "Logic private region pasteboard";
        return onDrop != nullptr && onDrop(data);
    }

    bool receivePromises(NSPasteboard* pasteboard,const juce::String& types)
    {
        if(@available(macOS 10.12, *))
        {
            NSArray* promises = [pasteboard readObjectsForClasses:@[[NSFilePromiseReceiver class]] options:nil];
            if([promises count] == 0)
                return false;

            auto destination = juce::File::getSpecialLocation(juce::File::userHomeDirectory)
                .getChildFile("Library")
                .getChildFile("Caches")
                .getChildFile("Santismo")
                .getChildFile("Chordizer MIDI Promise Drops")
                .getChildFile(juce::Uuid().toString());
            destination.createDirectory();
            appendDropLog("receiving MIDI file promise at " + destination.getFullPathName());

            auto files = std::make_shared<juce::StringArray>();
            auto remaining = std::make_shared<int>((int) [promises count]);
            auto callback = onDrop;
            auto safeOwner = owner;
            auto typesCopy = types;
            NSURL* destinationURL = [NSURL fileURLWithPath:stringToNSString(destination.getFullPathName()) isDirectory:YES];

            for(NSFilePromiseReceiver* receiver in promises)
            {
                [receiver receivePromisedFilesAtDestination:destinationURL
                                                    options:@{}
                                             operationQueue:[NSOperationQueue mainQueue]
                                                     reader:^(NSURL* url, NSError* error)
                {
                    if(error != nil)
                        appendDropLog("file promise failed: " + nsStringToString([error localizedDescription]));
                    else if(url != nil && [url isFileURL])
                        files->add(nsStringToString([url path]));

                    --(*remaining);
                    if(*remaining == 0 && safeOwner != nullptr && callback != nullptr)
                    {
                        MacMidiDropBridge::DropData promisedData;
                        promisedData.files = *files;
                        promisedData.sourceName = "Logic file promise";
                        promisedData.pasteboardTypes = typesCopy;
                        callback(promisedData);
                    }
                }];
            }
            return true;
        }
        return false;
    }
};

BridgeRecord* bridgeForView(id self)
{
    auto* value = (NSValue*) objc_getAssociatedObject(self, bridgeAssociationKey);
    return value == nil ? nullptr : static_cast<BridgeRecord*>([value pointerValue]);
}

OriginalMethods originalMethodsFor(id self)
{
    const auto found = originalMethodsByClass().find([self class]);
    return found == originalMethodsByClass().end() ? OriginalMethods{} : found->second;
}

NSDragOperation callOriginalDrag(IMP imp,id self,SEL selector,id<NSDraggingInfo> sender)
{
    if(imp == nullptr) return NSDragOperationNone;
    return ((DragOperationImp) imp)(self,selector,sender);
}

BOOL callOriginalPerform(IMP imp,id self,SEL selector,id<NSDraggingInfo> sender)
{
    if(imp == nullptr) return NO;
    return ((PerformDragImp) imp)(self,selector,sender);
}

void callOriginalExit(IMP imp,id self,SEL selector,id<NSDraggingInfo> sender)
{
    if(imp != nullptr) ((DragExitImp) imp)(self,selector,sender);
}

NSDragOperation chordizerDraggingEntered(id self,SEL selector,id<NSDraggingInfo> sender)
{
    if(auto* bridge = bridgeForView(self))
        if(bridge->canHandle(sender,true))
        {
            if(bridge->onHover) bridge->onHover(true);
            return NSDragOperationCopy;
        }
    return callOriginalDrag(originalMethodsFor(self).draggingEntered,self,selector,sender);
}

NSDragOperation chordizerDraggingUpdated(id self,SEL selector,id<NSDraggingInfo> sender)
{
    if(auto* bridge = bridgeForView(self))
        if(bridge->canHandle(sender,false))
        {
            if(bridge->onHover) bridge->onHover(true);
            return NSDragOperationCopy;
        }
    return callOriginalDrag(originalMethodsFor(self).draggingUpdated,self,selector,sender);
}

BOOL chordizerPerformDragOperation(id self,SEL selector,id<NSDraggingInfo> sender)
{
    if(auto* bridge = bridgeForView(self))
    {
        if(bridge->onHover) bridge->onHover(false);
        if(bridge->performDrop(sender))
            return YES;
    }
    return callOriginalPerform(originalMethodsFor(self).performDragOperation,self,selector,sender);
}

void chordizerDraggingExited(id self,SEL selector,id<NSDraggingInfo> sender)
{
    if(auto* bridge = bridgeForView(self))
        if(bridge->onHover) bridge->onHover(false);
    callOriginalExit(originalMethodsFor(self).draggingExited,self,selector,sender);
}

void installSwizzles(Class viewClass)
{
    static std::set<Class> installed;
    if(installed.find(viewClass) != installed.end()) return;
    installed.insert(viewClass);
    auto& originals = originalMethodsByClass()[viewClass];

    auto replace = [viewClass](SEL selector, IMP replacement, IMP& original)
    {
        if(auto* method = class_getInstanceMethod(viewClass,selector))
            original = method_setImplementation(method,replacement);
    };

    replace(@selector(draggingEntered:),(IMP) chordizerDraggingEntered,originals.draggingEntered);
    replace(@selector(draggingUpdated:),(IMP) chordizerDraggingUpdated,originals.draggingUpdated);
    replace(@selector(performDragOperation:),(IMP) chordizerPerformDragOperation,originals.performDragOperation);
    replace(@selector(draggingExited:),(IMP) chordizerDraggingExited,originals.draggingExited);
}
}

struct MacMidiDropBridge::Impl
{
    BridgeRecord record;

    Impl(juce::Component& owner,DropCallback onDrop,HoverCallback onHover)
        : record(owner,std::move(onDrop),std::move(onHover)) {}

    ~Impl()
    {
        if(record.view != nil)
        {
            auto* value = (NSValue*) objc_getAssociatedObject(record.view,bridgeAssociationKey);
            if(value != nil && [value pointerValue] == &record)
                objc_setAssociatedObject(record.view,bridgeAssociationKey,nil,OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        }
    }

    void refresh()
    {
        if(record.owner == nullptr) return;
        auto* nextView = static_cast<NSView*>(record.owner->getWindowHandle());
        if(nextView == nil || nextView == record.view) return;

        record.view = nextView;
        installSwizzles([nextView class]);
        [nextView registerForDraggedTypes:supportedTypes()];
        objc_setAssociatedObject(nextView,bridgeAssociationKey,[NSValue valueWithPointer:&record],
                                 OBJC_ASSOCIATION_RETAIN_NONATOMIC);
        appendDropLog("registered native MIDI drop bridge");
    }
};

#else

struct MacMidiDropBridge::Impl
{
    Impl(juce::Component&,DropCallback,HoverCallback) {}
    void refresh() {}
};

#endif

MacMidiDropBridge::MacMidiDropBridge(juce::Component& owner,DropCallback onDrop,HoverCallback onHover)
    : impl(std::make_unique<Impl>(owner,std::move(onDrop),std::move(onHover)))
{
    refresh();
}

MacMidiDropBridge::~MacMidiDropBridge() = default;

void MacMidiDropBridge::refresh()
{
    impl->refresh();
}
