#include <JuceHeader.h>
#include "PluginProcessor.h"

#include <iostream>
#include <memory>

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI initialiseJuce;
    ChordTrackerProcessor processor;
    processor.replaceRegions ({
        { 0.0, 4.0, "Cmaj7", "MIDI", 0.98f, { "C6", "Em/C" }, true },
        { 4.0, 8.0, "Am7", "MIDI", 0.95f, { "C6/A" } },
        { 8.0, 12.0, "Dm7", "MIDI", 0.96f, { "F6/D" } },
        { 12.0, 16.0, "G7", "MIDI", 0.99f, { "G9", "Bdim/G" }, true },
    });

    std::unique_ptr<juce::AudioProcessorEditor> editor (processor.createEditor());
    editor->setSize (1120, 700);
    constexpr auto scale = 1.5f;
    juce::Image image (juce::Image::ARGB, juce::roundToInt (editor->getWidth() * scale),
                       juce::roundToInt (editor->getHeight() * scale), true);
    {
        juce::Graphics graphics (image);
        graphics.addTransform (juce::AffineTransform::scale (scale));
        editor->paintEntireComponent (graphics, true);
    }

    const auto output = argc > 1 ? juce::File (argv[1])
                                 : juce::File::getCurrentWorkingDirectory().getChildFile ("chordizer-editor.png");
    output.getParentDirectory().createDirectory();
    output.deleteFile();
    auto stream = output.createOutputStream();
    juce::PNGImageFormat png;
    if (stream == nullptr || ! png.writeImageToStream (image, *stream))
    {
        std::cerr << "Could not write " << output.getFullPathName() << '\n';
        return 1;
    }
    std::cout << output.getFullPathName() << '\n';
    return 0;
}
