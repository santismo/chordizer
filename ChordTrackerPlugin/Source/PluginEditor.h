#pragma once
#include <JuceHeader.h>
#include "PluginProcessor.h"
#include "MacMidiDropBridge.h"
#include "MidiImport.h"
#include <optional>

class ChordizerIconButton final : public juce::Button
{
public:
    enum class Icon { view, oneMeasure, edit, smaller, larger, listen, importMidi, clear, copy, quantize, undo, redo, scalizer, harmony };
    explicit ChordizerIconButton(Icon iconToUse) : juce::Button(juce::String()), icon(iconToUse) {}
    void paintButton(juce::Graphics&,bool highlighted,bool down) override;
private:
    Icon icon;
};

class ChordTrackerEditor final : public juce::AudioProcessorEditor,
                                 private juce::Timer,
                                 private juce::FileDragAndDropTarget
{
public:
    explicit ChordTrackerEditor(ChordTrackerProcessor&);
    ~ChordTrackerEditor() override;
    void paint(juce::Graphics&) override;
    void resized() override;
    void mouseWheelMove(const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify(const juce::MouseEvent&, float scaleFactor) override;
    void mouseDown(const juce::MouseEvent&) override;
    void mouseDrag(const juce::MouseEvent&) override;
    void mouseUp(const juce::MouseEvent&) override;
    void mouseDoubleClick(const juce::MouseEvent&) override;
    bool keyPressed(const juce::KeyPress&) override;
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void fileDragEnter(const juce::StringArray& files,int x,int y) override;
    void fileDragExit(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files,int x,int y) override;
private:
    class ChordNameEditor final : public juce::TextEditor
    {
    public:
        std::function<bool(const juce::KeyPress&)> keyHandler;
        bool keyPressed(const juce::KeyPress& key) override
        {
            if(keyHandler&&keyHandler(key))return true;
            return juce::TextEditor::keyPressed(key);
        }
    };
    void timerCallback() override;
    void drawTimeline(juce::Graphics&,juce::Rectangle<int>,const ChordSessionSnapshot&);
    void drawLeadSheet(juce::Graphics&,juce::Rectangle<int>,const ChordSessionSnapshot&);
    juce::Rectangle<int> contentBounds() const;
    int leadSheetMeasureCount() const;
    void persistEditorState();
    void setTimelineZoomAround(double bars, double anchor);
    void setLeadSheetSingleColumn(bool enabled);
    void centreLeadSheetOnPlayhead();
    void switchView(bool showLeadSheet);
    std::optional<size_t> regionAtPoint(juce::Point<int>) const;
    std::optional<double> ppqAtPoint(juce::Point<int>) const;
    bool isScaleLanePoint(juce::Point<int>) const;
    juce::Rectangle<int> regionBounds(size_t index) const;
    void beginRegionEdit(size_t index,juce::Point<int> position);
    void commitRegionEdit();
    void showRegionMenu(size_t index,const juce::MouseEvent& event,bool quickEdit=false);
    void adjustTextScale(float delta);
    bool isRegionSelected(size_t index) const;
    void selectRegion(size_t index);
    void clearRangeSelection();
    void copySelectedChordNames();
    std::vector<ChordRegionData> selectedRegions() const;
    void quantizeSelectedRegions(double gridPpq);
    void beginMidiDrag();
    void chooseMidiImportFile();
    bool importMidiFile(const juce::File& file);
    bool importMidiData(const void* data,size_t bytes,const juce::String& sourceName);
    bool applyMidiImport(ChordizerMidiImportResult&& imported);
    bool handleNativeMidiDrop(const MacMidiDropBridge::DropData& data);
    void setMidiDropHover(bool hovering);
    void updateScalizerControls();
    void showHarmonyMenu();
    void deleteSelectedRegions();
    void performRegionEdit(const std::function<void()>& action);
    void undoRegionEdit();
    void redoRegionEdit();
    void restoreRegions(const std::vector<ChordRegionData>& regions);
    bool handleChordEditorKey(const juce::KeyPress& key);
    ChordTrackerProcessor& chordProcessor;
    ChordizerIconButton viewButton{ChordizerIconButton::Icon::view};
    ChordizerIconButton leadZoomButton{ChordizerIconButton::Icon::oneMeasure};
    ChordizerIconButton editButton{ChordizerIconButton::Icon::edit};
    ChordizerIconButton smallerTextButton{ChordizerIconButton::Icon::smaller};
    ChordizerIconButton largerTextButton{ChordizerIconButton::Icon::larger};
    ChordizerIconButton listenButton{ChordizerIconButton::Icon::listen};
    ChordizerIconButton importButton{ChordizerIconButton::Icon::importMidi};
    ChordizerIconButton clearButton{ChordizerIconButton::Icon::clear};
    ChordizerIconButton copyButton{ChordizerIconButton::Icon::copy};
    ChordizerIconButton quantizeButton{ChordizerIconButton::Icon::quantize};
    ChordizerIconButton undoButton{ChordizerIconButton::Icon::undo};
    ChordizerIconButton redoButton{ChordizerIconButton::Icon::redo};
    ChordizerIconButton scalizerButton{ChordizerIconButton::Icon::scalizer};
    juce::TextButton lockModeButton{"Scale"};
    ChordizerIconButton harmonyButton{ChordizerIconButton::Icon::harmony};
    ChordNameEditor chordNameEditor;
    double timelineZoomBars=16.0, timelineScrollPpq=0.0;
    bool leadSheet=false, leadSheetSingleColumn=false, editMode=false,quickEditMenuOpen=false;
    enum class ResizeEdge { none, start, end };
    ResizeEdge resizeEdge=ResizeEdge::none;
    bool resizeChanged=false,midiDragStarted=false,midiFileHover=false;
    bool initialising=true;
    std::optional<size_t> editingRegion;
    std::optional<size_t> selectedRegion;
    std::optional<size_t> selectionAnchor,selectionEnd;
    std::optional<size_t> pointerRegion,resizingRegion;
    juce::Point<int> pointerDownPosition;
    std::vector<ChordRegionData> resizeUndoSnapshot;
    juce::File lastMidiExportFile;
    std::unique_ptr<juce::FileChooser> midiImportChooser;
    std::unique_ptr<MacMidiDropBridge> nativeMidiDropBridge;
    std::vector<std::vector<ChordRegionData>> undoHistory,redoHistory;
    ChordSessionSnapshot snapshot;
    juce::String analysisStatus;
    juce::TooltipWindow tooltipWindow { this, 500 };
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ChordTrackerEditor)
};
