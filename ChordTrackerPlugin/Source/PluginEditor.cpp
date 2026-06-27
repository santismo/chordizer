#include "PluginEditor.h"
#include "MidiExport.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace
{
const auto background = juce::Colour(0xff111416);
const auto panel = juce::Colour(0xff1b2023);
const auto cyan = juce::Colour(0xff38d6c7);
const auto playhead = juce::Colour(0xffff5263);
constexpr int headerHeight = 34;

const std::array<juce::Colour, 20> chordPalette {
    juce::Colour(0xff2dd4bf), juce::Colour(0xffffc857), juce::Colour(0xffff6b6b), juce::Colour(0xff60a5fa),
    juce::Colour(0xff84cc16), juce::Colour(0xffe879f9), juce::Colour(0xffff8a4c), juce::Colour(0xff22d3ee),
    juce::Colour(0xffa78bfa), juce::Colour(0xfff43f5e), juce::Colour(0xff34d399), juce::Colour(0xfffacc15),
    juce::Colour(0xff818cf8), juce::Colour(0xfffb7185), juce::Colour(0xff4ade80), juce::Colour(0xffc084fc),
    juce::Colour(0xfff97316), juce::Colour(0xff38bdf8), juce::Colour(0xffa3e635), juce::Colour(0xfff472b6)
};

std::vector<size_t> regionColourIndices(const ChordSessionSnapshot& session)
{
    std::vector<size_t> result;
    result.reserve(session.regions.size());
    auto previous=chordPalette.size();
    for(size_t index=0;index<session.regions.size();++index)
    {
        auto candidate=((size_t)(uint32_t)session.regions[index].name.hashCode()+index*7u)%chordPalette.size();
        if(candidate==previous) candidate=(candidate+1u+index%5u)%chordPalette.size();
        result.push_back(candidate);
        previous=candidate;
    }
    return result;
}

float responsiveScale(juce::Rectangle<int> area,bool leadSheet)
{
    const auto baseArea=leadSheet?980.0f*560.0f:980.0f*320.0f;
    return juce::jlimit(0.68f,2.35f,std::sqrt((float)juce::jmax(1,area.getWidth()*area.getHeight())/baseArea));
}

juce::PropertiesFile& globalSettings()
{
    static juce::PropertiesFile settings([]
    {
        juce::PropertiesFile::Options options;
        options.applicationName="Chordizer";options.filenameSuffix="settings";options.folderName="Santismo";
        options.osxLibrarySubFolder="Application Support";return options;
    }());
    return settings;
}
}

void ChordizerIconButton::paintButton(juce::Graphics& graphics,bool highlighted,bool down)
{
    auto area=getLocalBounds().toFloat().reduced(1.0f);
    auto fill=getToggleState()?cyan.withAlpha(0.78f):juce::Colour(0xff272d31);
    if(highlighted)fill=fill.brighter(0.10f);
    if(down)fill=fill.darker(0.12f);
    if(!isEnabled())fill=fill.withAlpha(0.35f);
    graphics.setColour(fill);graphics.fillRoundedRectangle(area,4.0f);
    graphics.setColour(getToggleState()?juce::Colours::black
                                       :juce::Colours::white.withAlpha(isEnabled()?0.82f:0.32f));
    auto box=area.reduced(6.0f);const auto x=box.getX(),y=box.getY(),w=box.getWidth(),h=box.getHeight();
    const auto stroke=juce::PathStrokeType(1.7f,juce::PathStrokeType::curved,juce::PathStrokeType::rounded);
    juce::Path path;
    switch(icon)
    {
        case Icon::view:
            if(getToggleState())
            {
                const auto cellW=(w-2.0f)*0.5f,cellH=(h-2.0f)*0.5f;
                for(int row=0;row<2;++row)for(int column=0;column<2;++column)
                    graphics.drawRoundedRectangle(x+column*(cellW+2.0f),y+row*(cellH+2.0f),cellW,cellH,1.0f,1.4f);
            }
            else
            {
                graphics.drawHorizontalLine((int)(y+h*0.25f),x,x+w);
                graphics.drawHorizontalLine((int)(y+h*0.75f),x,x+w);
                graphics.fillRect(x+w*0.32f,y,w*0.10f,h);
                graphics.fillRect(x+w*0.47f,y+h*0.25f,w*0.35f,h*0.50f);
            }
            break;
        case Icon::oneMeasure:
            graphics.drawRoundedRectangle(box,1.5f,1.7f);
            graphics.drawVerticalLine((int)(x+w*0.5f),y+2.0f,y+h-2.0f);break;
        case Icon::edit:
            path.startNewSubPath(x+w*0.20f,y+h*0.78f);path.lineTo(x+w*0.32f,y+h*0.52f);
            path.lineTo(x+w*0.73f,y+h*0.11f);path.lineTo(x+w*0.89f,y+h*0.27f);
            path.lineTo(x+w*0.48f,y+h*0.68f);path.closeSubPath();graphics.strokePath(path,stroke);
            graphics.drawLine(x+w*0.16f,y+h*0.84f,x+w*0.45f,y+h*0.75f,1.7f);break;
        case Icon::smaller:
            graphics.drawLine(x,y+h*0.5f,x+w,y+h*0.5f,2.0f);break;
        case Icon::larger:
            graphics.drawLine(x,y+h*0.5f,x+w,y+h*0.5f,2.0f);
            graphics.drawLine(x+w*0.5f,y,x+w*0.5f,y+h,2.0f);break;
        case Icon::listen:
            path.startNewSubPath(x,y+h*0.50f);path.lineTo(x+w*0.18f,y+h*0.50f);
            path.lineTo(x+w*0.31f,y+h*0.18f);path.lineTo(x+w*0.48f,y+h*0.82f);
            path.lineTo(x+w*0.64f,y+h*0.30f);path.lineTo(x+w*0.78f,y+h*0.62f);path.lineTo(x+w,y+h*0.62f);
            graphics.strokePath(path,stroke);break;
        case Icon::clear:
            graphics.drawRoundedRectangle(x+w*0.24f,y+h*0.25f,w*0.52f,h*0.66f,1.0f,1.6f);
            graphics.drawLine(x+w*0.16f,y+h*0.20f,x+w*0.84f,y+h*0.20f,1.7f);
            graphics.drawLine(x+w*0.38f,y+h*0.08f,x+w*0.62f,y+h*0.08f,1.7f);
            graphics.drawVerticalLine((int)(x+w*0.42f),y+h*0.36f,y+h*0.78f);
            graphics.drawVerticalLine((int)(x+w*0.58f),y+h*0.36f,y+h*0.78f);break;
        case Icon::copy:
            graphics.drawRoundedRectangle(x+w*0.08f,y+h*0.18f,w*0.62f,h*0.68f,1.0f,1.5f);
            graphics.drawRoundedRectangle(x+w*0.30f,y+h*0.05f,w*0.62f,h*0.68f,1.0f,1.5f);break;
        case Icon::quantize:
            for(int i=0;i<4;++i)
                graphics.drawVerticalLine((int)(x+w*(0.18f+i*0.21f)),y+h*0.10f,y+h*0.90f);
            graphics.drawLine(x+w*0.08f,y+h*0.68f,x+w*0.92f,y+h*0.68f,1.4f);
            path.startNewSubPath(x+w*0.20f,y+h*0.34f);path.lineTo(x+w*0.41f,y+h*0.34f);path.lineTo(x+w*0.41f,y+h*0.55f);
            graphics.strokePath(path,stroke);
            path.clear();path.startNewSubPath(x+w*0.66f,y+h*0.34f);path.lineTo(x+w*0.50f,y+h*0.34f);path.lineTo(x+w*0.50f,y+h*0.55f);
            graphics.strokePath(path,stroke);break;
        case Icon::undo:
        case Icon::redo:
        {
            const auto reverse=icon==Icon::undo;
            const auto startX=reverse?x+w*0.82f:x+w*0.18f;
            const auto endX=reverse?x+w*0.25f:x+w*0.75f;
            path.startNewSubPath(startX,y+h*0.76f);
            path.cubicTo(reverse?x+w*0.88f:x+w*0.12f,y+h*0.25f,endX,y+h*0.22f,endX,y+h*0.45f);
            graphics.strokePath(path,stroke);
            juce::Path arrow;arrow.startNewSubPath(endX,y+h*0.45f);
            arrow.lineTo(reverse?endX+w*0.28f:endX-w*0.28f,y+h*0.35f);
            arrow.lineTo(reverse?endX+w*0.08f:endX-w*0.08f,y+h*0.67f);arrow.closeSubPath();graphics.fillPath(arrow);break;
        }
    }
}

ChordTrackerEditor::ChordTrackerEditor(ChordTrackerProcessor& owner)
    : AudioProcessorEditor(&owner), chordProcessor(owner)
{
    timelineZoomBars = juce::jlimit(0.01, 256.0, chordProcessor.savedTimelineZoom());
    timelineScrollPpq = juce::jmax(0.0, chordProcessor.savedTimelineScroll());
    leadSheet = chordProcessor.savedLeadSheetView();
    leadSheetSingleColumn = chordProcessor.savedLeadSheetSingleColumn();

    const auto initialWidth = chordProcessor.savedEditorWidth(leadSheet);
    const auto initialHeight = chordProcessor.savedEditorHeight(leadSheet);
    setName({});
    setSize(initialWidth, initialHeight);
    setResizable(true, true);
    setResizeLimits(420, 100, 1800, 1100);
    setWantsKeyboardFocus(true);

    if(chordProcessor.sessionSnapshot().instanceCount<=1)
    {
        chordProcessor.setTextScale(false,(float)globalSettings().getDoubleValue("timelineTextScale",1.0));
        chordProcessor.setTextScale(true,(float)globalSettings().getDoubleValue("leadTextScale",1.0));
    }

    for (auto* button : { &viewButton, &leadZoomButton, &editButton,
                          &smallerTextButton, &largerTextButton, &listenButton, &clearButton, &copyButton, &quantizeButton,
                          &undoButton, &redoButton })
    {
        addAndMakeVisible(button);
    }

    viewButton.setTooltip(leadSheet?"Show Timeline view":"Show Lead Sheet view");
    leadZoomButton.setTooltip("Full-width Lead Sheet measures");
    editButton.setTooltip("Edit chord regions");
    smallerTextButton.setTooltip("Smaller chord names in this view");
    largerTextButton.setTooltip("Larger chord names in this view");
    listenButton.setTooltip("Listen for chords");
    clearButton.setTooltip("Clear chord regions");
    copyButton.setTooltip("Copy selected chord names");
    quantizeButton.setTooltip("Quantize selected chord start and end to the nearest 1/16 note");
    undoButton.setTooltip("Undo chord edit");
    redoButton.setTooltip("Redo chord edit");

    viewButton.setClickingTogglesState(true);
    viewButton.setToggleState(leadSheet, juce::dontSendNotification);
    viewButton.onClick = [this] { switchView(viewButton.getToggleState()); };

    leadZoomButton.setClickingTogglesState(true);
    leadZoomButton.setToggleState(leadSheetSingleColumn, juce::dontSendNotification);
    leadZoomButton.setVisible(leadSheet);
    leadZoomButton.onClick = [this] { setLeadSheetSingleColumn(leadZoomButton.getToggleState()); };

    editButton.setClickingTogglesState(true);
    editButton.onClick=[this]{editMode=editButton.getToggleState();commitRegionEdit();selectedRegion.reset();clearRangeSelection();repaint();};
    smallerTextButton.onClick=[this]{adjustTextScale(-0.1f);};
    largerTextButton.onClick=[this]{adjustTextScale(0.1f);};

    listenButton.setClickingTogglesState(true);
    listenButton.setToggleState(chordProcessor.isListening(), juce::dontSendNotification);
    listenButton.onClick = [this] { chordProcessor.setListening(listenButton.getToggleState()); };
    analysisStatus=chordProcessor.analysisStatusText();
    listenButton.setTooltip("Listen for chords\n"+analysisStatus);
    clearButton.onClick = [this] { performRegionEdit([this]{chordProcessor.clearSession();});clearRangeSelection(); };
    copyButton.onClick = [this] { copySelectedChordNames(); };
    quantizeButton.onClick = [this] { quantizeSelectedRegions(0.25); };
    undoButton.onClick = [this] { undoRegionEdit(); };
    redoButton.onClick = [this] { redoRegionEdit(); };

    addAndMakeVisible(chordNameEditor);
    chordNameEditor.setVisible(false);
    chordNameEditor.setSelectAllWhenFocused(true);
    chordNameEditor.onReturnKey=[this]{commitRegionEdit();};
    chordNameEditor.onEscapeKey=[this]{editingRegion.reset();chordNameEditor.setVisible(false);};
    chordNameEditor.onFocusLost=[this]{if(editingRegion.has_value()&&!quickEditMenuOpen)commitRegionEdit();};
    chordNameEditor.keyHandler=[this](const juce::KeyPress& key){return handleChordEditorKey(key);};

    initialising = false;
    persistEditorState();
    startTimerHz(30);
}

juce::Rectangle<int> ChordTrackerEditor::contentBounds() const
{
    auto bounds = getLocalBounds();
    bounds.removeFromTop(headerHeight);
    return bounds.reduced(6, 3);
}

int ChordTrackerEditor::leadSheetMeasureCount() const
{
    const auto height = juce::jmax(1, contentBounds().getHeight() - 8);
    if (leadSheetSingleColumn)
        return juce::jmax(1, height / 64);
    const auto rows = height >= 300 ? 4 : (height >= 145 ? 2 : 1);
    return rows * 4;
}

void ChordTrackerEditor::persistEditorState()
{
    if (initialising) return;
    chordProcessor.updateEditorState(getWidth(), getHeight(), timelineZoomBars, timelineScrollPpq,
                                     leadSheet, leadSheetSingleColumn);
}

void ChordTrackerEditor::switchView(bool showLeadSheet)
{
    if(leadSheet==showLeadSheet) return;
    commitRegionEdit();
    persistEditorState();
    leadSheet=showLeadSheet;
    viewButton.setToggleState(leadSheet,juce::dontSendNotification);
    viewButton.setTooltip(leadSheet?"Show Timeline view":"Show Lead Sheet view");
    leadZoomButton.setVisible(leadSheet);
    const auto width=chordProcessor.savedEditorWidth(leadSheet);
    const auto height=chordProcessor.savedEditorHeight(leadSheet);
    initialising=true;
    setSize(width,height);
    initialising=false;
    resized();
    if(leadSheet&&leadSheetSingleColumn) centreLeadSheetOnPlayhead();
    persistEditorState();
    repaint();
}

void ChordTrackerEditor::centreLeadSheetOnPlayhead()
{
    const auto beats = (double) juce::jmax(1, snapshot.numerator);
    const auto currentMeasure = juce::jmax(0, (int) std::floor(snapshot.playheadPpq / beats));
    timelineScrollPpq = juce::jmax(0.0, (currentMeasure - leadSheetMeasureCount() / 2) * beats);
}

void ChordTrackerEditor::setLeadSheetSingleColumn(bool enabled)
{
    leadSheetSingleColumn = enabled;
    leadZoomButton.setToggleState(enabled, juce::dontSendNotification);
    if (enabled) centreLeadSheetOnPlayhead();
    persistEditorState();
    repaint();
}

void ChordTrackerEditor::timerCallback()
{
    auto next = chordProcessor.sessionSnapshot();
    const auto playheadMoved=std::abs(next.playheadPpq-snapshot.playheadPpq)>0.0001;
    bool viewportChanged=false;
    if (!leadSheet && next.playing)
    {
        const auto visiblePpq=timelineZoomBars*juce::jmax(1,next.numerator);
        const auto followedStart=juce::jmax(0.0,next.playheadPpq-visiblePpq*0.36);
        if(std::abs(followedStart-timelineScrollPpq)>visiblePpq*0.0001)
        {
            timelineScrollPpq=followedStart;
            viewportChanged=true;
            persistEditorState();
        }
    }
    else if (!leadSheet && playheadMoved)
    {
        const auto visiblePpq=timelineZoomBars*juce::jmax(1,next.numerator);
        const auto leftGuard=timelineScrollPpq+visiblePpq*0.08;
        const auto rightGuard=timelineScrollPpq+visiblePpq;
        if(next.playheadPpq<leftGuard||next.playheadPpq>rightGuard)
        {
            const auto followedStart=juce::jmax(0.0,next.playheadPpq-visiblePpq*0.36);
            timelineScrollPpq=followedStart;
            viewportChanged=true;
            persistEditorState();
        }
    }
    else if (leadSheet && next.playing)
    {
        const auto beats = (double) juce::jmax(1, next.numerator);
        const auto currentMeasure = juce::jmax(0, (int) std::floor(next.playheadPpq / beats));
        const auto visibleMeasures = leadSheetMeasureCount();
        const auto firstMeasure = (int) std::floor(timelineScrollPpq / beats);
        const auto lowerGuard = leadSheetSingleColumn ? 1 : 0;
        const auto upperGuard = leadSheetSingleColumn ? juce::jmax(1, visibleMeasures - 2) : visibleMeasures - 1;
        if (currentMeasure < firstMeasure + lowerGuard || currentMeasure > firstMeasure + upperGuard)
        {
            const auto desiredFirst = leadSheetSingleColumn
                                          ? juce::jmax(0, currentMeasure - visibleMeasures / 2)
                                          : (currentMeasure / visibleMeasures) * visibleMeasures;
            timelineScrollPpq = desiredFirst * beats;
            viewportChanged=true;
            persistEditorState();
        }
    }

    if (next.revision != snapshot.revision || std::abs(next.playheadPpq - snapshot.playheadPpq) > 0.001
        || viewportChanged)
    {
        snapshot = std::move(next);
        if(selectionAnchor.has_value()&&*selectionAnchor>=snapshot.regions.size())clearRangeSelection();
        if(selectionEnd.has_value()&&*selectionEnd>=snapshot.regions.size())clearRangeSelection();
        repaint();
    }
    undoButton.setEnabled(!undoHistory.empty());
    redoButton.setEnabled(!redoHistory.empty());
    const auto nextStatus=chordProcessor.analysisStatusText();
    if(nextStatus!=analysisStatus)
    {
        analysisStatus=nextStatus;
        listenButton.setTooltip("Listen for chords\n"+analysisStatus);
        repaint(juce::Rectangle<int>(getWidth()-20,0,20,headerHeight));
    }
}

void ChordTrackerEditor::paint(juce::Graphics& graphics)
{
    graphics.fillAll(background);
    auto statusColour=cyan;
    if(analysisStatus.containsIgnoreCase("error")||analysisStatus.containsIgnoreCase("unavailable"))statusColour=playhead;
    else if(analysisStatus.containsIgnoreCase("analyzing"))statusColour=juce::Colour(0xffffc857);
    else if(analysisStatus.containsIgnoreCase("loading")||analysisStatus.containsIgnoreCase("off"))statusColour=juce::Colours::grey;
    graphics.setColour(statusColour);
    graphics.fillEllipse((float)getWidth()-13.0f,13.0f,7.0f,7.0f);
    if(editMode)
    {
        auto header=getLocalBounds().removeFromTop(headerHeight);header.removeFromLeft(380);
        graphics.setColour(juce::Colours::white.withAlpha(0.62f));graphics.setFont(juce::FontOptions(10.0f));
        graphics.drawFittedText("Drag edges: resize  |  Right-click: fill gap  |  Click: menu",header.reduced(3,0),
                                juce::Justification::centredLeft,1);
    }
    const auto content = contentBounds();
    graphics.setColour(panel);
    graphics.fillRoundedRectangle(content.toFloat(), 4.0f);
    if (leadSheet) drawLeadSheet(graphics, content.reduced(5), snapshot);
    else drawTimeline(graphics, content.reduced(5), snapshot);
}

void ChordTrackerEditor::drawTimeline(juce::Graphics& graphics, juce::Rectangle<int> area,
                                      const ChordSessionSnapshot& session)
{
    const auto rulerHeight = juce::jlimit(14, 22, area.getHeight() / 3);
    auto ruler = area.removeFromTop(rulerHeight);
    const auto start = timelineScrollPpq;
    const auto beatsPerBar = juce::jmax(1, session.numerator);
    const auto end = start + timelineZoomBars * beatsPerBar;
    const auto span = juce::jmax(0.000001, end - start);
    const auto scale=responsiveScale(contentBounds(),false)*session.timelineTextScale;
    const auto colourIndices=regionColourIndices(session);

    const std::array<double, 8> intervals { (double)beatsPerBar, 1.0, 0.25, 0.0625, 0.015625,
                                            0.00390625, 0.0009765625, 0.000244140625 };
    auto grid = intervals.front();
    for (const auto candidate : intervals)
        if (candidate / span * area.getWidth() >= 24.0) grid = candidate;

    const auto firstTick = std::ceil(start / grid) * grid;
    for (auto tick = firstTick; tick <= end + grid * 0.5; tick += grid)
    {
        const auto x = area.getX() + (float) ((tick - start) / span) * area.getWidth();
        const auto bar = (int) std::round(tick / beatsPerBar);
        const auto isBar = std::abs(tick - bar * beatsPerBar) < grid * 0.1;
        graphics.setColour(juce::Colours::white.withAlpha(isBar ? 0.16f : 0.055f));
        graphics.drawVerticalLine((int)x, (float)ruler.getY(), (float)area.getBottom());
        if (isBar && ruler.getHeight() >= 12)
        {
            graphics.setColour(juce::Colours::white.withAlpha(0.68f));
            graphics.setFont(juce::FontOptions(juce::jlimit(8.0f,18.0f,9.0f*scale)));
            graphics.drawText(juce::String(bar + 1), (int)x + 3, ruler.getY(), 42, ruler.getHeight(),
                              juce::Justification::centredLeft);
        }
    }

    const auto regionY = (float)area.getY() + 2.0f;
    const auto regionHeight = (float)juce::jmax(6, area.getHeight() - 4);
    for (size_t index = 0; index < session.regions.size(); ++index)
    {
        const auto& chord = session.regions[index];
        if (chord.endPpq <= start || chord.startPpq >= end) continue;
        const auto x = area.getX() + (float)((chord.startPpq - start) / span) * area.getWidth();
        const auto x2 = area.getX() + (float)((chord.endPpq - start) / span) * area.getWidth();
        const auto rawWidth = x2 - x;
        const auto gap = juce::jmin(1.5f, juce::jmax(0.0f, rawWidth * 0.18f));
        auto box = juce::Rectangle<float>(x + gap, regionY, juce::jmax(0.5f, rawWidth - gap * 2.0f), regionHeight)
                       .getIntersection(area.toFloat());
        if (box.isEmpty()) continue;

        const auto colour = chordPalette[colourIndices[index]];
        graphics.setColour(colour.withAlpha(0.86f));
        graphics.fillRoundedRectangle(box, juce::jmin(2.5f, box.getWidth() * 0.2f));
        graphics.setColour(background.withAlpha(0.9f));
        graphics.drawRoundedRectangle(box, juce::jmin(2.5f, box.getWidth() * 0.2f), 1.0f);
        if(isRegionSelected(index))
        {
            graphics.setColour(juce::Colours::white.withAlpha(0.92f));
            graphics.drawRoundedRectangle(box.reduced(1.0f),juce::jmin(2.5f,box.getWidth()*0.2f),2.0f);
        }
        if(editMode&&box.getWidth()>=5.0f)
        {
            graphics.setColour(juce::Colours::white.withAlpha(0.82f));
            graphics.fillRect(box.getX(),box.getY()+2.0f,2.0f,juce::jmax(2.0f,box.getHeight()-4.0f));
            graphics.fillRect(box.getRight()-2.0f,box.getY()+2.0f,2.0f,juce::jmax(2.0f,box.getHeight()-4.0f));
        }
        if (box.getWidth() >= 10.0f && box.getHeight() >= 10.0f)
        {
            graphics.setColour(juce::Colours::black);
            const auto fontSize=juce::jlimit(8.0f,32.0f,juce::jmin(regionHeight*0.58f,13.0f*scale));
            graphics.setFont(juce::FontOptions(fontSize, juce::Font::bold));
            graphics.drawFittedText(chord.name, box.toNearestInt().reduced(2, 0), juce::Justification::centred, 1);
        }
    }

    const auto playheadX = area.getX() + (float)((session.playheadPpq - start) / span) * area.getWidth();
    if (playheadX >= area.getX() - 1 && playheadX <= area.getRight() + 1)
    {
        graphics.setColour(playhead);
        graphics.fillRect(playheadX, (float)ruler.getY(), 2.0f, (float)area.getBottom() - ruler.getY());
    }
}

void ChordTrackerEditor::drawLeadSheet(juce::Graphics& graphics, juce::Rectangle<int> area,
                                       const ChordSessionSnapshot& session)
{
    const auto columns = leadSheetSingleColumn ? 1 : 4;
    const auto gap = leadSheetSingleColumn ? 5 : 7;
    const auto measureCount = leadSheetMeasureCount();
    const auto rows = juce::jmax(1, (measureCount + columns - 1) / columns);
    const auto width = (area.getWidth() - gap * (columns - 1)) / columns;
    const auto height = juce::jmax(1, (area.getHeight() - gap * (rows - 1)) / rows);
    const auto beats = (double)juce::jmax(1, session.numerator);
    const auto firstMeasure = juce::jmax(0, (int)std::floor(timelineScrollPpq / beats));
    const auto scale=responsiveScale(contentBounds(),true)*session.leadSheetTextScale;
    const auto colourIndices=regionColourIndices(session);

    for (int index = 0; index < measureCount; ++index)
    {
        const auto measure = firstMeasure + index;
        const auto row = index / columns;
        const auto column = index % columns;
        auto frame = juce::Rectangle<int>(area.getX() + column * (width + gap), area.getY() + row * (height + gap),
                                          width, height);
        const auto measureStart = measure * beats;
        const auto measureEnd = measureStart + beats;
        const auto current = session.playheadPpq >= measureStart && session.playheadPpq < measureEnd;

        graphics.setColour(current ? cyan.withAlpha(0.14f) : juce::Colours::white.withAlpha(0.028f));
        graphics.fillRoundedRectangle(frame.toFloat(), 3.0f);
        graphics.setColour(current ? cyan : juce::Colours::white.withAlpha(0.2f));
        graphics.drawRoundedRectangle(frame.toFloat(), 3.0f, 1.0f);

        auto content = frame;
        auto numberArea = content.removeFromTop(juce::jlimit(11, 18, height / 4));
        graphics.setColour(juce::Colours::white.withAlpha(0.55f));
        graphics.setFont(juce::FontOptions(juce::jlimit(8.0f,17.0f,9.0f*scale)));
        graphics.drawText(juce::String(measure + 1), numberArea.reduced(4, 0), juce::Justification::centredLeft);

        for (size_t regionIndex = 0; regionIndex < session.regions.size(); ++regionIndex)
        {
            const auto& chord = session.regions[regionIndex];
            if (chord.endPpq <= measureStart || chord.startPpq >= measureEnd) continue;
            const auto chordStart = juce::jmax(chord.startPpq, measureStart);
            const auto chordEnd = juce::jmin(chord.endPpq, measureEnd);
            const auto x = content.getX() + (float)((chordStart - measureStart) / beats) * content.getWidth();
            const auto x2 = content.getX() + (float)((chordEnd - measureStart) / beats) * content.getWidth();
            const auto rawWidth = x2 - x;
            const auto edgeGap = juce::jmin(1.5f, juce::jmax(0.0f, rawWidth * 0.18f));
            auto chordBox = juce::Rectangle<float>(x + edgeGap, (float)content.getY(),
                                                    juce::jmax(0.5f, rawWidth - edgeGap * 2.0f),
                                                    (float)content.getHeight());
            const auto colour = chordPalette[colourIndices[regionIndex]];
            graphics.setColour(colour.withAlpha(0.52f));
            graphics.fillRect(chordBox);
            graphics.setColour(background.withAlpha(0.9f));
            graphics.drawRect(chordBox, 1.0f);
            if(isRegionSelected(regionIndex))
            {
                graphics.setColour(juce::Colours::white.withAlpha(0.92f));
                graphics.drawRect(chordBox.reduced(1.0f),2.0f);
            }
            if(editMode&&chordBox.getWidth()>=5.0f)
            {
                graphics.setColour(juce::Colours::white.withAlpha(0.82f));
                graphics.fillRect(chordBox.getX(),chordBox.getY()+2.0f,2.0f,
                                  juce::jmax(2.0f,chordBox.getHeight()-4.0f));
                graphics.fillRect(chordBox.getRight()-2.0f,chordBox.getY()+2.0f,2.0f,
                                  juce::jmax(2.0f,chordBox.getHeight()-4.0f));
            }
            if (chordBox.getWidth() >= 10.0f && content.getHeight() >= 10)
            {
                graphics.setColour(juce::Colours::white);
                const auto maximumFont = (leadSheetSingleColumn ? 19.0f : 14.0f)*scale;
                graphics.setFont(juce::FontOptions(juce::jlimit(8.0f, 34.0f, juce::jmin(maximumFont,content.getHeight()*0.56f)),
                                                   juce::Font::bold));
                graphics.drawFittedText(chord.name, chordBox.toNearestInt().reduced(2, 0),
                                        juce::Justification::centred, 1);
            }
        }

        if (current)
        {
            const auto progress = juce::jlimit(0.0, 1.0, (session.playheadPpq - measureStart) / beats);
            const auto x = content.getX() + (float)progress * content.getWidth();
            graphics.setColour(playhead);
            graphics.fillRect(x, (float)content.getY(), 2.0f, (float)content.getHeight());
        }
    }
}

std::optional<double> ChordTrackerEditor::ppqAtPoint(juce::Point<int> point) const
{
    auto area=contentBounds().reduced(5);
    if(!area.contains(point)) return {};
    double ppq=-1.0;
    if(!leadSheet)
    {
        const auto rulerHeight=juce::jlimit(14,22,area.getHeight()/3);
        area.removeFromTop(rulerHeight);
        if(!area.contains(point)||area.getWidth()<=0) return {};
        const auto beats=(double)juce::jmax(1,snapshot.numerator);
        ppq=timelineScrollPpq+(point.x-area.getX())/(double)area.getWidth()*timelineZoomBars*beats;
    }
    else
    {
        const auto columns=leadSheetSingleColumn?1:4;
        const auto gap=leadSheetSingleColumn?5:7;
        const auto count=leadSheetMeasureCount();
        const auto rows=juce::jmax(1,(count+columns-1)/columns);
        const auto width=(area.getWidth()-gap*(columns-1))/columns;
        const auto height=juce::jmax(1,(area.getHeight()-gap*(rows-1))/rows);
        const auto beats=(double)juce::jmax(1,snapshot.numerator);
        const auto firstMeasure=juce::jmax(0,(int)std::floor(timelineScrollPpq/beats));
        for(int index=0;index<count;++index)
        {
            const auto row=index/columns, column=index%columns;
            auto frame=juce::Rectangle<int>(area.getX()+column*(width+gap),area.getY()+row*(height+gap),width,height);
            frame.removeFromTop(juce::jlimit(11,18,height/4));
            if(!frame.contains(point)||frame.getWidth()<=0) continue;
            ppq=(firstMeasure+index)*beats+(point.x-frame.getX())/(double)frame.getWidth()*beats;
            break;
        }
    }
    if(ppq<0.0)return {};
    return ppq;
}

std::optional<size_t> ChordTrackerEditor::regionAtPoint(juce::Point<int> point) const
{
    const auto ppq=ppqAtPoint(point);
    if(!ppq.has_value())return {};
    for(size_t index=snapshot.regions.size();index>0;--index)
    {
        const auto& region=snapshot.regions[index-1];
        if(*ppq>=region.startPpq&&*ppq<=region.endPpq) return index-1;
    }
    return {};
}

juce::Rectangle<int> ChordTrackerEditor::regionBounds(size_t index) const
{
    if(index>=snapshot.regions.size())return {};
    const auto& region=snapshot.regions[index];
    auto area=contentBounds().reduced(5);
    if(!leadSheet)
    {
        area.removeFromTop(juce::jlimit(14,22,area.getHeight()/3));
        const auto beats=(double)juce::jmax(1,snapshot.numerator),span=timelineZoomBars*beats;
        if(region.endPpq<=timelineScrollPpq||region.startPpq>=timelineScrollPpq+span)return {};
        const auto x=area.getX()+(int)std::round((region.startPpq-timelineScrollPpq)/span*area.getWidth());
        const auto x2=area.getX()+(int)std::round((region.endPpq-timelineScrollPpq)/span*area.getWidth());
        return juce::Rectangle<int>(x,area.getY(),juce::jmax(2,x2-x),area.getHeight()).getIntersection(area);
    }
    const auto columns=leadSheetSingleColumn?1:4,gap=leadSheetSingleColumn?5:7,count=leadSheetMeasureCount();
    const auto rows=juce::jmax(1,(count+columns-1)/columns),width=(area.getWidth()-gap*(columns-1))/columns;
    const auto height=juce::jmax(1,(area.getHeight()-gap*(rows-1))/rows);
    const auto beats=(double)juce::jmax(1,snapshot.numerator);
    const auto first=juce::jmax(0,(int)std::floor(timelineScrollPpq/beats));
    for(int visible=0;visible<count;++visible)
    {
        const auto measure=first+visible;
        const auto start=measure*beats,end=start+beats;
        if(region.endPpq<=start||region.startPpq>=end)continue;
        const auto row=visible/columns,column=visible%columns;
        auto frame=juce::Rectangle<int>(area.getX()+column*(width+gap),area.getY()+row*(height+gap),width,height);
        frame.removeFromTop(juce::jlimit(11,18,height/4));
        const auto x=frame.getX()+(int)std::round((juce::jmax(region.startPpq,start)-start)/beats*frame.getWidth());
        const auto x2=frame.getX()+(int)std::round((juce::jmin(region.endPpq,end)-start)/beats*frame.getWidth());
        return juce::Rectangle<int>(x,frame.getY(),juce::jmax(2,x2-x),frame.getHeight()).getIntersection(frame);
    }
    return {};
}

void ChordTrackerEditor::adjustTextScale(float delta)
{
    const auto current=leadSheet?snapshot.leadSheetTextScale:snapshot.timelineTextScale;
    const auto value=juce::jlimit(0.5f,3.0f,current+delta);
    chordProcessor.setTextScale(leadSheet,value);
    globalSettings().setValue(leadSheet?"leadTextScale":"timelineTextScale",value);
    globalSettings().saveIfNeeded();
}

void ChordTrackerEditor::beginRegionEdit(size_t index,juce::Point<int> position)
{
    if(index>=snapshot.regions.size()) return;
    editingRegion=index;
    chordNameEditor.setText(snapshot.regions[index].name,juce::dontSendNotification);
    auto bounds=juce::Rectangle<int>(position.x-80,position.y-13,160,26).constrainedWithin(getLocalBounds().reduced(4));
    chordNameEditor.setBounds(bounds);
    chordNameEditor.setVisible(true);
    chordNameEditor.toFront(true);
    chordNameEditor.grabKeyboardFocus();
    chordNameEditor.selectAll();
}

void ChordTrackerEditor::commitRegionEdit()
{
    if(!editingRegion.has_value()) return;
    const auto index=*editingRegion;
    const auto value=chordNameEditor.getText().trim();
    editingRegion.reset();
    chordNameEditor.setVisible(false);
    if(value.isNotEmpty()&&index<snapshot.regions.size()&&value!=snapshot.regions[index].name)
        performRegionEdit([this,index,value]{chordProcessor.renameRegion(index,value);});
}

bool ChordTrackerEditor::isRegionSelected(size_t index) const
{
    if(!selectionAnchor.has_value())return false;
    const auto other=selectionEnd.value_or(*selectionAnchor);
    return index>=juce::jmin(*selectionAnchor,other)&&index<=juce::jmax(*selectionAnchor,other);
}

void ChordTrackerEditor::selectRegion(size_t index)
{
    if(!selectionAnchor.has_value()||selectionEnd.has_value())
    {
        selectionAnchor=index;
        selectionEnd.reset();
    }
    else selectionEnd=index;
    grabKeyboardFocus();
    repaint();
}

void ChordTrackerEditor::clearRangeSelection()
{
    selectionAnchor.reset();selectionEnd.reset();repaint();
}

void ChordTrackerEditor::copySelectedChordNames()
{
    if(!selectionAnchor.has_value()||snapshot.regions.empty())return;
    const auto other=selectionEnd.value_or(*selectionAnchor);
    const auto first=juce::jmin(*selectionAnchor,other),last=juce::jmin(juce::jmax(*selectionAnchor,other),snapshot.regions.size()-1);
    juce::StringArray names;
    for(auto index=first;index<=last;++index)names.add(snapshot.regions[index].name);
    juce::SystemClipboard::copyTextToClipboard(names.joinIntoString(", "));
}

std::vector<ChordRegionData> ChordTrackerEditor::selectedRegions() const
{
    if(!selectionAnchor.has_value()||snapshot.regions.empty())return {};
    const auto other=selectionEnd.value_or(*selectionAnchor);
    const auto first=juce::jmin(*selectionAnchor,other);
    const auto last=juce::jmin(juce::jmax(*selectionAnchor,other),snapshot.regions.size()-1);
    return {snapshot.regions.begin()+(std::ptrdiff_t)first,
            snapshot.regions.begin()+(std::ptrdiff_t)last+1};
}

void ChordTrackerEditor::quantizeSelectedRegions(double gridPpq)
{
    if(!selectionAnchor.has_value()||snapshot.regions.empty())return;
    const auto other=selectionEnd.value_or(*selectionAnchor);
    const auto first=juce::jmin(*selectionAnchor,other),last=juce::jmin(juce::jmax(*selectionAnchor,other),snapshot.regions.size()-1);
    performRegionEdit([this,first,last,gridPpq]
    {
        for(auto index=first;index<=last;++index)
            chordProcessor.quantizeRegion(index,gridPpq);
    });
    repaint();
}

void ChordTrackerEditor::beginMidiDrag()
{
    const auto regions=selectedRegions();
    if(regions.empty())return;
    auto directory=juce::File::getSpecialLocation(juce::File::tempDirectory)
        .getChildFile("Santismo").getChildFile("Chordizer MIDI Exports");
    auto fileStem=chordizerMidiExportName(regions).replace("/"," over ")
        .retainCharacters("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789 #b()+-_");
    const auto destination=directory.getNonexistentChildFile(fileStem,".mid",false);
    if(!writeChordizerMidiFile(regions,snapshot.bpm,snapshot.numerator,snapshot.denominator,destination))return;
    lastMidiExportFile=destination;
    juce::StringArray files;files.add(destination.getFullPathName());
    midiDragStarted=juce::DragAndDropContainer::performExternalDragDropOfFiles(files,false,this);
}

void ChordTrackerEditor::performRegionEdit(const std::function<void()>& action)
{
    auto before=chordProcessor.sessionSnapshot().regions;
    action();
    undoHistory.push_back(std::move(before));
    if(undoHistory.size()>32)undoHistory.erase(undoHistory.begin());
    redoHistory.clear();
}

void ChordTrackerEditor::restoreRegions(const std::vector<ChordRegionData>& regions)
{
    editingRegion.reset();selectedRegion.reset();chordNameEditor.setVisible(false);clearRangeSelection();
    chordProcessor.replaceRegions(regions);
}

void ChordTrackerEditor::undoRegionEdit()
{
    if(undoHistory.empty())return;
    redoHistory.push_back(chordProcessor.sessionSnapshot().regions);
    auto previous=std::move(undoHistory.back());undoHistory.pop_back();restoreRegions(previous);
}

void ChordTrackerEditor::redoRegionEdit()
{
    if(redoHistory.empty())return;
    undoHistory.push_back(chordProcessor.sessionSnapshot().regions);
    auto next=std::move(redoHistory.back());redoHistory.pop_back();restoreRegions(next);
}

void ChordTrackerEditor::deleteSelectedRegions()
{
    if(!selectionAnchor.has_value()||snapshot.regions.empty())return;
    const auto other=selectionEnd.value_or(*selectionAnchor);
    const auto first=juce::jmin(*selectionAnchor,other),last=juce::jmin(juce::jmax(*selectionAnchor,other),snapshot.regions.size()-1);
    performRegionEdit([this,first,last]
    {
        for(auto index=last+1;index>first;--index)chordProcessor.deleteRegion(index-1);
    });
    clearRangeSelection();
}

bool ChordTrackerEditor::handleChordEditorKey(const juce::KeyPress& key)
{
    if(!editingRegion.has_value()||(key.getKeyCode()!=juce::KeyPress::backspaceKey&&key.getKeyCode()!=juce::KeyPress::deleteKey))return false;
    const auto index=*editingRegion;
    editingRegion.reset();selectedRegion.reset();chordNameEditor.setVisible(false);
    performRegionEdit([this,index]{chordProcessor.deleteRegion(index);});
    return true;
}

void ChordTrackerEditor::showRegionMenu(size_t index,const juce::MouseEvent& event,bool quickEdit)
{
    if(index>=snapshot.regions.size()) return;
    const auto region=snapshot.regions[index];
    juce::PopupMenu menu;
    menu.addSectionHeader(region.name);
    menu.addItem(1,"Confidence  "+juce::String((int)std::round(region.confidence*100.0f))+"%",false);
    if(!region.alternatives.isEmpty())
    {
        menu.addSeparator();
        menu.addSectionHeader("Alternatives");
        for(int i=0;i<region.alternatives.size();++i) menu.addItem(100+i,region.alternatives[i]);
    }
    menu.addSeparator();
    menu.addItem(2,"Edit name");
    menu.addItem(3,"Delete region");
    menu.addItem(4,"Quantize start/end 1/16");
    menu.addItem(5,"Quantize start/end 1/32");
    auto anchor=regionBounds(index);
    if(anchor.isEmpty())anchor=juce::Rectangle<int>(event.getPosition().x,event.getPosition().y,1,1);
    const auto position=anchor.getCentre().withY(anchor.getBottom()+14);
    juce::Component::SafePointer<ChordTrackerEditor> safe(this);
    auto options=juce::PopupMenu::Options().withTargetComponent(this).withTargetScreenArea(localAreaToGlobal(anchor))
                     .withPreferredPopupDirection(juce::PopupMenu::Options::PopupDirection::downwards);
    if(quickEdit)quickEditMenuOpen=true;
    menu.showMenuAsync(options,[safe,index,region,position,quickEdit](int result)
    {
        if(safe==nullptr)return;
        safe->quickEditMenuOpen=false;
        safe->selectedRegion.reset();
        if(result==0)
        {
            if(quickEdit&&safe->editingRegion.has_value())safe->chordNameEditor.grabKeyboardFocus();
            return;
        }
        if(result==2)
        {
            if(!quickEdit)safe->beginRegionEdit(index,position);
            else{safe->chordNameEditor.grabKeyboardFocus();safe->chordNameEditor.selectAll();}
        }
        else if(result==3)
        {
            safe->editingRegion.reset();safe->chordNameEditor.setVisible(false);
            safe->performRegionEdit([safe,index]{if(safe!=nullptr)safe->chordProcessor.deleteRegion(index);});
        }
        else if(result==4||result==5)
        {
            safe->editingRegion.reset();safe->chordNameEditor.setVisible(false);
            const auto grid=result==4?0.25:0.125;
            safe->performRegionEdit([safe,index,grid]{if(safe!=nullptr)safe->chordProcessor.quantizeRegion(index,grid);});
        }
        else if(result>=100&&result<100+region.alternatives.size())
        {
            safe->editingRegion.reset();safe->chordNameEditor.setVisible(false);
            const auto name=region.alternatives[result-100];
            safe->performRegionEdit([safe,index,name]{if(safe!=nullptr)safe->chordProcessor.renameRegion(index,name);});
        }
    });
}

void ChordTrackerEditor::mouseDown(const juce::MouseEvent& event)
{
    const auto index=regionAtPoint(event.getPosition());
    pointerDownPosition=event.getPosition();pointerRegion=index;midiDragStarted=false;
    if(event.mods.isRightButtonDown())
    {
        pointerRegion.reset();
        if(index.has_value()&&*index+1<snapshot.regions.size()
           &&snapshot.regions[*index+1].startPpq>snapshot.regions[*index].endPpq+0.000001)
            performRegionEdit([this,index]{chordProcessor.extendRegionToNext(*index);});
        return;
    }
    if(!editMode)
    {
        if(editingRegion.has_value())commitRegionEdit();
        if(index.has_value())
        {
            if(!isRegionSelected(*index))selectRegion(*index);else grabKeyboardFocus();
        }
        else clearRangeSelection();
        return;
    }
    pointerRegion.reset();
    if(!index.has_value()){selectedRegion.reset();commitRegionEdit();return;}
    const auto bounds=regionBounds(*index);
    if(event.mods.isLeftButtonDown()&&!bounds.isEmpty())
    {
        const auto leftDistance=std::abs(event.getPosition().x-bounds.getX());
        const auto rightDistance=std::abs(event.getPosition().x-bounds.getRight());
        if(juce::jmin(leftDistance,rightDistance)<=7)
        {
            commitRegionEdit();
            resizingRegion=index;
            resizeEdge=leftDistance<=rightDistance?ResizeEdge::start:ResizeEdge::end;
            resizeUndoSnapshot=chordProcessor.sessionSnapshot().regions;
            resizeChanged=false;
            return;
        }
    }
    if(editingRegion.has_value()&&*editingRegion==*index){commitRegionEdit();selectedRegion.reset();return;}
    if(selectedRegion.has_value()&&*selectedRegion==*index){selectedRegion.reset();commitRegionEdit();return;}
    selectedRegion=*index;
    showRegionMenu(*index,event);
}

void ChordTrackerEditor::mouseDrag(const juce::MouseEvent& event)
{
    if(resizingRegion.has_value())
    {
        const auto ppq=ppqAtPoint(event.getPosition());
        const auto current=chordProcessor.sessionSnapshot();
        if(!ppq.has_value()||*resizingRegion>=current.regions.size())return;
        const auto snapped=std::round(*ppq*64.0)/64.0;
        const auto& region=current.regions[*resizingRegion];
        const auto changed=resizeEdge==ResizeEdge::start
            ?chordProcessor.resizeRegion(*resizingRegion,snapped,region.endPpq)
            :chordProcessor.resizeRegion(*resizingRegion,region.startPpq,snapped);
        resizeChanged=resizeChanged||changed;
        return;
    }
    if(!editMode&&pointerRegion.has_value()&&!midiDragStarted
       &&event.getDistanceFromDragStart()>=5)
        beginMidiDrag();
}

void ChordTrackerEditor::mouseUp(const juce::MouseEvent&)
{
    pointerRegion.reset();midiDragStarted=false;
    if(resizingRegion.has_value()&&resizeChanged)
    {
        undoHistory.push_back(std::move(resizeUndoSnapshot));
        if(undoHistory.size()>32)undoHistory.erase(undoHistory.begin());
        redoHistory.clear();
    }
    resizingRegion.reset();resizeEdge=ResizeEdge::none;resizeChanged=false;
}

void ChordTrackerEditor::mouseDoubleClick(const juce::MouseEvent& event)
{
    if(editMode)return;
    const auto index=regionAtPoint(event.getPosition());
    if(!index.has_value()){clearRangeSelection();return;}
    clearRangeSelection();
    beginRegionEdit(*index,event.getPosition());
    showRegionMenu(*index,event,true);
}

bool ChordTrackerEditor::keyPressed(const juce::KeyPress& key)
{
    if((key.getKeyCode()==juce::KeyPress::backspaceKey||key.getKeyCode()==juce::KeyPress::deleteKey)
       &&selectionAnchor.has_value()){deleteSelectedRegions();return true;}
    if(key.getModifiers().isCommandDown()&&key.getKeyCode()=='z')
    {
        if(key.getModifiers().isShiftDown())redoRegionEdit();else undoRegionEdit();
        return true;
    }
    return AudioProcessorEditor::keyPressed(key);
}

void ChordTrackerEditor::setTimelineZoomAround(double bars, double anchor)
{
    const auto newBars = juce::jlimit(0.01, 256.0, bars);
    const auto beats = (double)juce::jmax(1, snapshot.numerator);
    const auto clampedAnchor = juce::jlimit(0.0, 1.0, anchor);
    const auto anchorPpq = timelineScrollPpq + clampedAnchor * timelineZoomBars * beats;
    timelineScrollPpq = juce::jmax(0.0, anchorPpq - clampedAnchor * newBars * beats);
    timelineZoomBars = newBars;
    persistEditorState();
    repaint();
}

void ChordTrackerEditor::mouseMagnify(const juce::MouseEvent& event, float scaleFactor)
{
    if (scaleFactor <= 0.0f) return;
    if (leadSheet)
    {
        if (scaleFactor > 1.015f && !leadSheetSingleColumn) setLeadSheetSingleColumn(true);
        else if (scaleFactor < 0.985f && leadSheetSingleColumn) setLeadSheetSingleColumn(false);
        return;
    }

    const auto content = contentBounds().reduced(5);
    const auto anchor = content.getWidth() > 0 ? (event.position.x - content.getX()) / content.getWidth() : 0.5f;
    setTimelineZoomAround(timelineZoomBars / juce::jlimit(0.5, 2.0, (double)scaleFactor), anchor);
}

void ChordTrackerEditor::mouseWheelMove(const juce::MouseEvent& event, const juce::MouseWheelDetails& wheel)
{
    if (!contentBounds().contains(event.getPosition()))
    {
        AudioProcessorEditor::mouseWheelMove(event, wheel);
        return;
    }

    auto direction = std::abs(wheel.deltaX) > std::abs(wheel.deltaY) ? wheel.deltaX : wheel.deltaY;
    if (!wheel.isReversed) direction = -direction;
    if (!leadSheet && (event.mods.isCommandDown() || event.mods.isCtrlDown()))
    {
        const auto content = contentBounds();
        const auto anchor = content.getWidth() > 0 ? (event.position.x - content.getX()) / content.getWidth() : 0.5f;
        setTimelineZoomAround(timelineZoomBars * std::pow(2.0, direction * 0.7), anchor);
        return;
    }

    const auto beats = (double)juce::jmax(1, snapshot.numerator);
    if (leadSheet)
        timelineScrollPpq = juce::jmax(0.0, timelineScrollPpq - direction * beats * (leadSheetSingleColumn ? 2.0 : 4.0));
    else
        timelineScrollPpq = juce::jmax(0.0, timelineScrollPpq - direction * timelineZoomBars * beats * 0.45);
    persistEditorState();
    repaint();
}

void ChordTrackerEditor::resized()
{
    auto controls = getLocalBounds().removeFromTop(headerHeight).reduced(6, 5);
    constexpr int buttonWidth = 24;
    for (auto* button : { &viewButton, &editButton, &smallerTextButton, &largerTextButton,
                          &listenButton, &clearButton, &copyButton, &quantizeButton, &undoButton, &redoButton })
    {
        button->setBounds(controls.removeFromLeft(buttonWidth));
        controls.removeFromLeft(4);
    }
    if(leadSheet)
    {
        leadZoomButton.setBounds(controls.removeFromLeft(buttonWidth));
        controls.removeFromLeft(4);
    }
    else leadZoomButton.setBounds({});
    persistEditorState();
}
