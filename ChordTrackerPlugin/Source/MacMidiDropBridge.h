#pragma once
#include <JuceHeader.h>
#include <functional>

class MacMidiDropBridge final
{
public:
    struct DropData
    {
        juce::StringArray files;
        juce::MemoryBlock midiData;
        juce::String sourceName;
        juce::String pasteboardTypes;
        juce::String diagnostic;
    };

    using DropCallback = std::function<bool(const DropData&)>;
    using HoverCallback = std::function<void(bool)>;

    MacMidiDropBridge(juce::Component& owner,DropCallback onDrop,HoverCallback onHover);
    ~MacMidiDropBridge();

    void refresh();

private:
    struct Impl;
    std::unique_ptr<Impl> impl;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MacMidiDropBridge)
};
