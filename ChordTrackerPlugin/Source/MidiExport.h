#pragma once
#include <JuceHeader.h>
#include "ChordEngine.h"

constexpr int chordizerMidiTicksPerQuarterNote=960;

std::vector<int> chordizerMidiNotesForName(const juce::String& chordName);
juce::String chordizerMidiExportName(const std::vector<ChordRegionData>& regions);
juce::MidiFile createChordizerMidiFile(const std::vector<ChordRegionData>& regions,double bpm,
                                       int numerator,int denominator);
bool writeChordizerMidiFile(const std::vector<ChordRegionData>& regions,double bpm,
                            int numerator,int denominator,const juce::File& destination);
