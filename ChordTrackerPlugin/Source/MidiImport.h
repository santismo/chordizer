#pragma once
#include <JuceHeader.h>
#include "ChordEngine.h"
#include <vector>

struct ChordizerMidiImportResult
{
    std::vector<ChordRegionData> regions;
    double startPpq = 0.0, endPpq = 0.0;
    int noteCount = 0;
    juce::String error;

    bool succeeded() const noexcept { return error.isEmpty() && !regions.empty(); }
};

bool chordizerIsMidiImportFile(const juce::File& file);
ChordizerMidiImportResult importChordizerMidiFile(const juce::File& file,double insertAtPpq);
ChordizerMidiImportResult importChordizerMidiData(const void* data,size_t bytes,double insertAtPpq);
std::vector<ChordRegionData> mergeChordizerImportedMidiRegions(
    const std::vector<ChordRegionData>& existing,
    const std::vector<ChordRegionData>& imported);
