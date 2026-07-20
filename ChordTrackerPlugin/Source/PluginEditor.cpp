#include "PluginEditor.h"
#include "MidiExport.h"
#include "MidiImport.h"
#include <algorithm>
#include <array>
#include <cmath>

namespace
{
juce::Colour background=SongizerLogicLookAndFeel::window();
juce::Colour panel=SongizerLogicLookAndFeel::panel();
juce::Colour cyan=SongizerLogicLookAndFeel::blue();
juce::Colour playhead=SongizerLogicLookAndFeel::red();
juce::Colour buttonSurface=SongizerLogicLookAndFeel::raised();
constexpr int headerHeight = 34;
std::array<juce::Colour, 20> chordPalette {
    juce::Colour(0xff2dd4bf), juce::Colour(0xffffc857), juce::Colour(0xffff6b6b), juce::Colour(0xff60a5fa),
    juce::Colour(0xff84cc16), juce::Colour(0xffe879f9), juce::Colour(0xffff8a4c), juce::Colour(0xff22d3ee),
    juce::Colour(0xffa78bfa), juce::Colour(0xfff43f5e), juce::Colour(0xff34d399), juce::Colour(0xfffacc15),
    juce::Colour(0xff818cf8), juce::Colour(0xfffb7185), juce::Colour(0xff4ade80), juce::Colour(0xffc084fc),
    juce::Colour(0xfff97316), juce::Colour(0xff38bdf8), juce::Colour(0xffa3e635), juce::Colour(0xfff472b6)
};

constexpr int activePlayerSkinIndex = 9;

float playerInterfacePhase()
{
    return static_cast<float>(std::fmod(juce::Time::getMillisecondCounterHiRes()*0.00008,1.0));
}

bool squarePlayerInterface()
{
    return activePlayerSkinIndex==4||activePlayerSkinIndex==9;
}

float playerCorner()
{
    return squarePlayerInterface()?0.0f:(activePlayerSkinIndex==3?10.0f:3.0f);
}

void drawPlayerTexture(juce::Graphics& graphics,juce::Rectangle<float> bounds,float alpha=1.0f)
{
    switch(activePlayerSkinIndex)
    {
        case 0:
            for(float y=bounds.getY();y<bounds.getBottom();y+=3.0f)
            {
                graphics.setColour(juce::Colours::white.withAlpha(((static_cast<int>(y)%9==0)?0.045f:0.018f)*alpha));
                graphics.drawHorizontalLine((int)y,bounds.getX(),bounds.getRight());
            }
            break;
        case 1:
            graphics.setColour(cyan.withAlpha(0.11f*alpha));
            for(float x=bounds.getX();x<bounds.getRight();x+=12.0f)graphics.drawVerticalLine((int)x,bounds.getY(),bounds.getBottom());
            for(float y=bounds.getY();y<bounds.getBottom();y+=8.0f)graphics.drawHorizontalLine((int)y,bounds.getX(),bounds.getRight());
            break;
        case 4:
            for(float y=bounds.getY()+3.0f;y<bounds.getBottom();y+=7.0f)for(float x=bounds.getX()+3.0f;x<bounds.getRight();x+=7.0f)
            {
                graphics.setColour(cyan.withAlpha(0.05f*alpha));graphics.fillRect(x,y,2.0f,2.0f);
            }
            break;
        case 2:
            for(float y=bounds.getY();y<bounds.getBottom();y+=15.0f)
            {
                graphics.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.11f*alpha),bounds.getX(),y,
                                                               juce::Colours::black.withAlpha(0.11f*alpha),bounds.getX(),y+15.0f,false));
                graphics.fillRect(bounds.getX(),y,bounds.getWidth(),15.0f);
            }
            break;
        case 7:
            graphics.setColour(cyan.withAlpha(0.045f*alpha));
            for(float x=bounds.getX();x<bounds.getRight();x+=10.0f)graphics.drawVerticalLine((int)x,bounds.getY(),bounds.getBottom());
            graphics.setColour(cyan.withAlpha(0.23f*alpha));
            {
                juce::Path trace;trace.startNewSubPath(bounds.getX(),bounds.getCentreY());
                for(float x=bounds.getX();x<bounds.getRight();x+=5.0f)trace.lineTo(x,bounds.getCentreY()+std::sin((x-bounds.getX())*0.11f)*bounds.getHeight()*0.18f);
                graphics.strokePath(trace,juce::PathStrokeType(1.2f));
            }
            break;
        case 3:
            for(float x=bounds.getX();x<bounds.getRight();x+=9.0f)
            {
                graphics.setColour(playhead.withAlpha(0.055f*alpha));graphics.drawVerticalLine((int)x,bounds.getY(),bounds.getBottom());
            }
            break;
        case 5:
            for(float y=bounds.getY();y<bounds.getBottom();y+=3.0f)
            {
                graphics.setColour(juce::Colours::black.withAlpha(0.16f*alpha));
                graphics.drawHorizontalLine((int)y,bounds.getX(),bounds.getRight());
            }
            break;
        case 6:
            graphics.setColour(playhead.withAlpha(0.09f*alpha));
            for(float x=bounds.getX()-bounds.getHeight();x<bounds.getRight();x+=16.0f)
                graphics.drawLine(x,bounds.getBottom(),x+bounds.getHeight(),bounds.getY(),2.0f);
            break;
        case 8:
            graphics.setColour(cyan.withAlpha(0.07f*alpha));
            for(float x=bounds.getX()-20.0f;x<bounds.getRight()+20.0f;x+=20.0f)
            {
                juce::Path diamond;diamond.startNewSubPath(x,bounds.getCentreY());diamond.lineTo(x+10.0f,bounds.getCentreY()-10.0f);
                diamond.lineTo(x+20.0f,bounds.getCentreY());diamond.lineTo(x+10.0f,bounds.getCentreY()+10.0f);diamond.closeSubPath();
                graphics.strokePath(diamond,juce::PathStrokeType(1.0f));
            }
            break;
        case 9:
            for(float y=bounds.getY()+4.0f;y<bounds.getBottom();y+=7.0f)for(float x=bounds.getX()+4.0f;x<bounds.getRight();x+=7.0f)
            {
                graphics.setColour(juce::Colours::white.withAlpha(0.06f*alpha));graphics.fillEllipse(x,y,1.4f,1.4f);
            }
            break;
    }
}

void drawPlayerFrame(juce::Graphics& graphics,juce::Rectangle<float> bounds,const juce::String& caption)
{
    const auto corner=playerCorner();
    graphics.setColour(panel);graphics.fillRoundedRectangle(bounds,4.0f);
    graphics.setColour(SongizerLogicLookAndFeel::line().withAlpha(0.82f));graphics.drawRoundedRectangle(bounds.reduced(0.5f),4.0f,1.0f);
    auto display=bounds.reduced(6.0f,4.0f).removeFromTop(13.0f);
    graphics.setColour(background.darker(0.25f));graphics.fillRoundedRectangle(display,squarePlayerInterface()?0.0f:2.0f);
    graphics.setColour(SongizerLogicLookAndFeel::line());graphics.drawRoundedRectangle(display,3.0f,1.0f);
    graphics.setColour(SongizerLogicLookAndFeel::muted());graphics.setFont(juce::FontOptions(9.0f));
    graphics.drawFittedText(caption,display.toNearestInt().reduced(4,0),juce::Justification::centredLeft,1);

}

void drawPlayerSpectrum(juce::Graphics& graphics,juce::Rectangle<float> bounds)
{
    const auto phase=playerInterfacePhase();const auto bars=juce::jmax(8,(int)(bounds.getWidth()/8.0f));const auto width=bounds.getWidth()/(float)bars;
    graphics.setColour(background.darker(0.25f));graphics.fillRect(bounds);
    for(int i=0;i<bars;++i)
    {
        const auto energy=0.20f+0.75f*std::abs(std::sin(phase*10.0f+(float)i*(0.37f+activePlayerSkinIndex*0.02f)));
        const auto height=bounds.getHeight()*energy;
        graphics.setColour((i%5==0?playhead:cyan).withAlpha(0.88f));
        graphics.fillRect(bounds.getX()+i*width+1.0f,bounds.getBottom()-height,juce::jmax(1.0f,width-2.0f),height);
    }
}

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
        options.applicationName="Chordizer";options.filenameSuffix="settings";options.folderName="Songizer";
        options.osxLibrarySubFolder="Application Support";return options;
    }());
    return settings;
}

juce::String intervalName(int degree)
{
    static const std::array<const char*,9> names {"","","2nd","3rd","4th","5th","6th","7th","octave"};
    return degree>=2&&degree<=8?names[(size_t)degree]:juce::String(degree);
}
}

void ChordizerIconButton::paintButton(juce::Graphics& graphics,bool highlighted,bool down)
{
    auto area=getLocalBounds().toFloat().reduced(1.0f);
    auto fill=getToggleState()?cyan.withAlpha(0.78f):buttonSurface;
    if(highlighted)fill=fill.brighter(0.10f);
    if(down)fill=fill.darker(0.12f);
    if(!isEnabled())fill=fill.withAlpha(0.35f);
    graphics.setColour(fill);graphics.fillRoundedRectangle(area,4.0f);
    graphics.setColour((getToggleState()?cyan:SongizerLogicLookAndFeel::line()).withAlpha(0.90f));graphics.drawRoundedRectangle(area,4.0f,1.0f);
    graphics.setColour(getToggleState()?juce::Colours::white
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
        case Icon::importMidi:
            graphics.drawRoundedRectangle(x+w*0.12f,y+h*0.62f,w*0.76f,h*0.28f,1.5f,1.6f);
            graphics.drawLine(x+w*0.50f,y+h*0.08f,x+w*0.50f,y+h*0.55f,2.0f);
            path.startNewSubPath(x+w*0.30f,y+h*0.38f);
            path.lineTo(x+w*0.50f,y+h*0.59f);
            path.lineTo(x+w*0.70f,y+h*0.38f);
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
        case Icon::scalizer:
        {
            const std::array<float,3> noteX { 0.18f, 0.50f, 0.82f };
            const std::array<float,3> noteY { 0.72f, 0.50f, 0.28f };
            for(size_t index=0;index<noteX.size();++index)
            {
                graphics.fillEllipse(x+w*(noteX[index]-0.10f),y+h*(noteY[index]-0.10f),w*0.20f,h*0.20f);
                graphics.drawLine(x+w*(noteX[index]+0.08f),y+h*noteY[index],
                                  x+w*(noteX[index]+0.08f),y+h*(noteY[index]-0.33f),1.5f);
            }
            graphics.drawLine(x+w*0.12f,y+h*0.86f,x+w*0.88f,y+h*0.14f,1.4f);break;
        }
        case Icon::harmony:
        {
            const std::array<float,3> noteX { 0.22f, 0.52f, 0.82f };
            const std::array<float,3> noteY { 0.70f, 0.48f, 0.29f };
            for(size_t index=0;index<noteX.size();++index)
            {
                graphics.fillEllipse(x+w*(noteX[index]-0.10f),y+h*(noteY[index]-0.10f),w*0.20f,h*0.20f);
                graphics.drawLine(x+w*(noteX[index]+0.08f),y+h*noteY[index],
                                  x+w*(noteX[index]+0.08f),y+h*(noteY[index]-0.34f),1.5f);
            }
            graphics.drawLine(x+w*0.30f,y+h*0.36f,x+w*0.60f,y+h*0.14f,1.7f);
            graphics.drawLine(x+w*0.60f,y+h*0.14f,x+w*0.90f,y+h*0.14f,1.7f);break;
        }
    }
}

ChordTrackerEditor::ChordTrackerEditor(ChordTrackerProcessor& owner)
    : AudioProcessorEditor(&owner), chordProcessor(owner)
{
    setLookAndFeel(&logicLookAndFeel);
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
                          &smallerTextButton, &largerTextButton, &listenButton, &importButton, &clearButton, &copyButton, &quantizeButton,
                          &undoButton, &redoButton, &scalizerButton, &harmonyButton })
    {
        addAndMakeVisible(button);
    }
    addAndMakeVisible(lockModeButton);
    lockModeButton.setColour(juce::TextButton::buttonColourId,buttonSurface);
    lockModeButton.setColour(juce::TextButton::buttonOnColourId,cyan);
    lockModeButton.setColour(juce::TextButton::textColourOffId,juce::Colours::white.withAlpha(0.82f));
    lockModeButton.setColour(juce::TextButton::textColourOnId,juce::Colours::white);

    viewButton.setTooltip(leadSheet?"Show Timeline view":"Show Lead Sheet view");
    leadZoomButton.setTooltip("Full-width Lead Sheet measures");
    editButton.setTooltip("Edit chord regions");
    smallerTextButton.setTooltip("Smaller chord names in this view");
    largerTextButton.setTooltip("Larger chord names in this view");
    listenButton.setTooltip("Listen for chords");
    importButton.setTooltip("Import MIDI at the playhead");
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
    importButton.onClick = [this] { chooseMidiImportFile(); };
    clearButton.onClick = [this] { performRegionEdit([this]{chordProcessor.clearSession();});clearRangeSelection(); };
    copyButton.onClick = [this] { copySelectedChordNames(); };
    quantizeButton.onClick = [this] { quantizeSelectedRegions(0.25); };
    undoButton.onClick = [this] { undoRegionEdit(); };
    redoButton.onClick = [this] { redoRegionEdit(); };
    scalizerButton.setClickingTogglesState(true);
    scalizerButton.setTooltip("Constrain live MIDI to the active chord region");
    scalizerButton.onClick=[this]
    {
        chordProcessor.setScalizerEnabled(scalizerButton.getToggleState());
        updateScalizerControls();repaint();
    };
    lockModeButton.setTooltip("Switch between scale notes and chord tones");
    lockModeButton.onClick=[this]
    {
        const auto next=chordProcessor.scalizerLockMode()==ScalizerLockMode::scale
                        ?ScalizerLockMode::chordTones:ScalizerLockMode::scale;
        chordProcessor.setScalizerLockMode(next);updateScalizerControls();repaint();
    };
    harmonyButton.setTooltip("Configure up to three diatonic harmony voices");
    harmonyButton.onClick=[this]{showHarmonyMenu();};
    updateScalizerControls();

    addAndMakeVisible(chordNameEditor);
    chordNameEditor.setVisible(false);
    chordNameEditor.setSelectAllWhenFocused(true);
    chordNameEditor.onReturnKey=[this]{commitRegionEdit();};
    chordNameEditor.onEscapeKey=[this]{editingRegion.reset();chordNameEditor.setVisible(false);};
    chordNameEditor.onFocusLost=[this]{if(editingRegion.has_value()&&!quickEditMenuOpen)commitRegionEdit();};
    chordNameEditor.keyHandler=[this](const juce::KeyPress& key){return handleChordEditorKey(key);};

    juce::Component::SafePointer<ChordTrackerEditor> safe(this);
    nativeMidiDropBridge=std::make_unique<MacMidiDropBridge>(*this,
        [safe](const MacMidiDropBridge::DropData& data)
        {
            return safe!=nullptr&&safe->handleNativeMidiDrop(data);
        },
        [safe](bool hovering)
        {
            if(safe!=nullptr)safe->setMidiDropHover(hovering);
        });

    initialising = false;
    persistEditorState();
    startTimerHz(30);
}

ChordTrackerEditor::~ChordTrackerEditor()
{
    setLookAndFeel(nullptr);
}

void ChordTrackerEditor::updateScalizerControls()
{
    const auto supported=chordProcessor.supportsScalizer();
    scalizerButton.setVisible(supported);lockModeButton.setVisible(supported);harmonyButton.setVisible(supported);
    if(!supported)return;
    const auto enabled=chordProcessor.isScalizerEnabled();
    scalizerButton.setToggleState(enabled,juce::dontSendNotification);
    lockModeButton.setButtonText(chordProcessor.scalizerLockMode()==ScalizerLockMode::chordTones?"Chord":"Scale");
    lockModeButton.setEnabled(enabled);harmonyButton.setEnabled(enabled);
    bool harmonyEnabled=false;
    for(int index=0;index<3;++index)
        harmonyEnabled|=chordProcessor.scalizerHarmonyVoice(index).degree>=2;
    harmonyButton.setToggleState(harmonyEnabled,juce::dontSendNotification);
}


void ChordTrackerEditor::showHarmonyMenu()
{
    juce::PopupMenu menu;
    menu.addItem(1,"All voices off");
    menu.addItem(2,"Preset: 3rd above");
    menu.addItem(3,"Preset: 3rds above + below");
    menu.addSeparator();
    for(int voiceIndex=0;voiceIndex<3;++voiceIndex)
    {
        juce::PopupMenu voiceMenu;
        const auto current=chordProcessor.scalizerHarmonyVoice(voiceIndex);
        const auto base=(voiceIndex+1)*100;
        voiceMenu.addItem(base+1,"Off",true,current.degree<2);
        voiceMenu.addSeparator();
        for(int degree=2;degree<=8;++degree)
            voiceMenu.addItem(base+10+degree,intervalName(degree)+" above",true,
                              current.degree==degree&&current.above);
        voiceMenu.addSeparator();
        for(int degree=2;degree<=8;++degree)
            voiceMenu.addItem(base+20+degree,intervalName(degree)+" below",true,
                              current.degree==degree&&!current.above);
        menu.addSubMenu("Voice "+juce::String(voiceIndex+1),voiceMenu);
    }
    juce::Component::SafePointer<ChordTrackerEditor> safe(this);
    menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(harmonyButton),
        [safe](int result)
        {
            if(safe==nullptr||result==0)return;
            if(result==1)
                for(int index=0;index<3;++index)safe->chordProcessor.setScalizerHarmonyVoice(index,{});
            else if(result==2)
            {
                safe->chordProcessor.setScalizerHarmonyVoice(0,{3,true});
                safe->chordProcessor.setScalizerHarmonyVoice(1,{});
                safe->chordProcessor.setScalizerHarmonyVoice(2,{});
            }
            else if(result==3)
            {
                safe->chordProcessor.setScalizerHarmonyVoice(0,{3,true});
                safe->chordProcessor.setScalizerHarmonyVoice(1,{3,false});
                safe->chordProcessor.setScalizerHarmonyVoice(2,{});
            }
            else
            {
                const auto voiceIndex=result/100-1;
                const auto code=result%100;
                if(code==1)safe->chordProcessor.setScalizerHarmonyVoice(voiceIndex,{});
                else if(code>=12&&code<=18)safe->chordProcessor.setScalizerHarmonyVoice(voiceIndex,{code-10,true});
                else if(code>=22&&code<=28)safe->chordProcessor.setScalizerHarmonyVoice(voiceIndex,{code-20,false});
            }
            safe->updateScalizerControls();safe->repaint();
        });
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
    if(nativeMidiDropBridge)nativeMidiDropBridge->refresh();
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
    auto headerFrame=getLocalBounds().toFloat().removeFromTop((float)headerHeight);
    graphics.setColour(panel);
    graphics.fillRect(headerFrame);
    graphics.setColour(cyan.withAlpha(0.28f));
    graphics.drawLine(headerFrame.getX(),headerFrame.getBottom()-0.5f,headerFrame.getRight(),headerFrame.getBottom()-0.5f,1.0f);
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
    graphics.fillRoundedRectangle(content.toFloat(),4.0f);
    graphics.setColour(cyan.withAlpha(0.24f));
    graphics.drawRoundedRectangle(content.toFloat().reduced(0.5f),4.0f,1.0f);
    if (leadSheet) drawLeadSheet(graphics, content.reduced(5), snapshot);
    else drawTimeline(graphics, content.reduced(5), snapshot);
    if(midiFileHover)
    {
        graphics.setColour(cyan.withAlpha(0.86f));
        graphics.drawRoundedRectangle(content.toFloat().reduced(1.5f),4.0f,2.0f);
    }
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
    const auto showScaleLane=chordProcessor.supportsScalizer()&&chordProcessor.isScalizerEnabled();
    const auto inferredScales=showScaleLane?inferScalizerScales(session.regions):std::vector<ScalizerScaleChoice>{};
    auto chordArea=area;
    juce::Rectangle<int> scaleArea;
    if(showScaleLane&&area.getHeight()>=24)
    {
        scaleArea=chordArea.removeFromBottom(juce::jmax(11,(int)std::round(area.getHeight()*0.36)));
        graphics.setColour(cyan.withAlpha(0.18f));
        graphics.drawHorizontalLine(scaleArea.getY(),(float)area.getX(),(float)area.getRight());
    }

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

    const auto regionY = (float)chordArea.getY() + 2.0f;
    const auto regionHeight = (float)juce::jmax(6, chordArea.getHeight() - 4);
    for (size_t index = 0; index < session.regions.size(); ++index)
    {
        const auto& chord = session.regions[index];
        if (chord.endPpq <= start || chord.startPpq >= end) continue;
        const auto x = area.getX() + (float)((chord.startPpq - start) / span) * area.getWidth();
        const auto x2 = area.getX() + (float)((chord.endPpq - start) / span) * area.getWidth();
        const auto rawWidth = x2 - x;
        const auto gap = juce::jmin(1.5f, juce::jmax(0.0f, rawWidth * 0.18f));
        auto box = juce::Rectangle<float>(x + gap, regionY, juce::jmax(0.5f, rawWidth - gap * 2.0f), regionHeight)
                       .getIntersection(chordArea.toFloat());
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

    if(showScaleLane)
    {
        for(size_t index=0;index<session.regions.size()&&index<inferredScales.size();++index)
        {
            const auto& chord=session.regions[index];
            const auto& inferred=inferredScales[index];
            if(!inferred.valid||chord.endPpq<=start||chord.startPpq>=end)continue;
            const auto x=area.getX()+(float)((chord.startPpq-start)/span)*area.getWidth();
            const auto x2=area.getX()+(float)((chord.endPpq-start)/span)*area.getWidth();
            const auto rawWidth=x2-x;
            const auto gap=juce::jmin(1.5f,juce::jmax(0.0f,rawWidth*0.18f));
            auto box=juce::Rectangle<float>(x+gap,(float)scaleArea.getY()+1.0f,
                                            juce::jmax(0.5f,rawWidth-gap*2.0f),
                                            (float)juce::jmax(1,scaleArea.getHeight()-2))
                         .getIntersection(scaleArea.toFloat());
            if(box.isEmpty())continue;
            const auto colour=chordPalette[colourIndices[index]];
            const auto manual=chord.scaleOverride.isNotEmpty();
            graphics.setColour(colour.withAlpha(manual?0.46f:0.28f));graphics.fillRoundedRectangle(box,2.0f);
            graphics.setColour(cyan.withAlpha(manual?0.76f:0.30f));graphics.drawRoundedRectangle(box,2.0f,manual?1.4f:0.8f);
            if(box.getWidth()>=18.0f&&box.getHeight()>=8.0f)
            {
                graphics.setColour(juce::Colours::white.withAlpha(0.78f));
                graphics.setFont(juce::FontOptions(juce::jlimit(7.0f,15.0f,9.0f*scale)));
                graphics.drawFittedText(inferred.name+(manual?"  M":""),box.toNearestInt().reduced(2,0),juce::Justification::centred,1);
            }
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
    const auto showScaleLane=chordProcessor.supportsScalizer()&&chordProcessor.isScalizerEnabled();
    const auto inferredScales=showScaleLane?inferScalizerScales(session.regions):std::vector<ScalizerScaleChoice>{};

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
                auto chordTextBox=chordBox.toNearestInt().reduced(2,0);
                auto scaleTextBox=juce::Rectangle<int>();
                if(showScaleLane&&regionIndex<inferredScales.size()&&inferredScales[regionIndex].valid)
                    scaleTextBox=chordTextBox.removeFromBottom(juce::jmax(8,chordTextBox.getHeight()/3));
                graphics.setColour(juce::Colours::white);
                const auto maximumFont = (leadSheetSingleColumn ? 19.0f : 14.0f)*scale;
                graphics.setFont(juce::FontOptions(juce::jlimit(8.0f, 34.0f, juce::jmin(maximumFont,chordTextBox.getHeight()*0.56f)),
                                                   juce::Font::bold));
                graphics.drawFittedText(chord.name,chordTextBox,juce::Justification::centred,1);
                if(!scaleTextBox.isEmpty())
                {
                    graphics.setColour(cyan.withAlpha(0.82f));
                    graphics.setFont(juce::FontOptions(juce::jlimit(7.0f,13.0f,8.0f*scale)));
                    graphics.drawFittedText(inferredScales[regionIndex].name
                                                +(chord.scaleOverride.isNotEmpty()?"  M":""),scaleTextBox,
                                            juce::Justification::centred,1);
                }
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

bool ChordTrackerEditor::isScaleLanePoint(juce::Point<int> point) const
{
    if(leadSheet||!chordProcessor.supportsScalizer()||!chordProcessor.isScalizerEnabled())return false;
    auto area=contentBounds().reduced(5);
    area.removeFromTop(juce::jlimit(14,22,area.getHeight()/3));
    if(area.getHeight()<24)return false;
    return area.removeFromBottom(juce::jmax(11,(int)std::round(area.getHeight()*0.36))).contains(point);
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

void ChordTrackerEditor::chooseMidiImportFile()
{
    commitRegionEdit();
    auto start=juce::File::getSpecialLocation(juce::File::userDocumentsDirectory);
    midiImportChooser=std::make_unique<juce::FileChooser>("Import MIDI at playhead",start,"*.mid;*.midi;*.smf");
    juce::Component::SafePointer<ChordTrackerEditor> safe(this);
    midiImportChooser->launchAsync(juce::FileBrowserComponent::openMode
                                   | juce::FileBrowserComponent::canSelectFiles,
                                   [safe](const juce::FileChooser& chooser)
    {
        if(safe==nullptr)return;
        const auto file=chooser.getResult();
        if(file.existsAsFile())safe->importMidiFile(file);
    });
}

bool ChordTrackerEditor::importMidiFile(const juce::File& file)
{
    commitRegionEdit();
    clearRangeSelection();
    const auto targetSnapshot=chordProcessor.sessionSnapshot();
    return applyMidiImport(importChordizerMidiFile(file,targetSnapshot.playheadPpq));
}

bool ChordTrackerEditor::importMidiData(const void* data,size_t bytes,const juce::String&)
{
    commitRegionEdit();
    clearRangeSelection();
    const auto targetSnapshot=chordProcessor.sessionSnapshot();
    return applyMidiImport(importChordizerMidiData(data,bytes,targetSnapshot.playheadPpq));
}

bool ChordTrackerEditor::applyMidiImport(ChordizerMidiImportResult&& imported)
{
    if(!imported.succeeded())
    {
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                               "MIDI import failed",
                                               imported.error.isNotEmpty()?imported.error:"No chord regions were imported.");
        return false;
    }

    auto importedRegions=std::move(imported.regions);
    performRegionEdit([this,importedRegions]
    {
        const auto current=chordProcessor.sessionSnapshot();
        chordProcessor.replaceRegions(mergeChordizerImportedMidiRegions(current.regions,importedRegions));
    });
    snapshot=chordProcessor.sessionSnapshot();
    repaint();
    return true;
}

bool ChordTrackerEditor::handleNativeMidiDrop(const MacMidiDropBridge::DropData& data)
{
    setMidiDropHover(false);
    if(data.midiData.getSize()>0)
        return importMidiData(data.midiData.getData(),data.midiData.getSize(),data.sourceName);

    for(const auto& path:data.files)
    {
        const auto file=juce::File(path);
        if(chordizerIsMidiImportFile(file))
            return importMidiFile(file);
    }

    auto message=juce::String("Logic did not expose readable MIDI data for this drag.");
    if(data.diagnostic.isNotEmpty())
        message+="\n\n"+data.diagnostic;
    if(data.files.isEmpty())
        message+="\n\nPasteboard types seen:\n"+data.pasteboardTypes;
    else
        message+="\n\nPromised/dropped files were not MIDI files:\n"+data.files.joinIntoString("\n");
    message+="\n\nA debug log was written to ~/Library/Application Support/Santismo/Chordizer/drop-debug.log.";
    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                           "Logic did not provide MIDI",
                                           message);
    return true;
}

void ChordTrackerEditor::setMidiDropHover(bool hovering)
{
    if(midiFileHover==hovering)return;
    midiFileHover=hovering;
    repaint();
}

bool ChordTrackerEditor::isInterestedInFileDrag(const juce::StringArray& files)
{
    for(const auto& path:files)
        if(chordizerIsMidiImportFile(juce::File(path)))return true;
    return false;
}

void ChordTrackerEditor::fileDragEnter(const juce::StringArray& files,int,int)
{
    setMidiDropHover(isInterestedInFileDrag(files));
}

void ChordTrackerEditor::fileDragExit(const juce::StringArray&)
{
    setMidiDropHover(false);
}

void ChordTrackerEditor::filesDropped(const juce::StringArray& files,int,int)
{
    setMidiDropHover(false);
    for(const auto& path:files)
    {
        const auto file=juce::File(path);
        if(chordizerIsMidiImportFile(file))
        {
            importMidiFile(file);
            repaint();
            return;
        }
    }
    repaint();
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
    if(chordProcessor.supportsScalizer())
    {
        const auto inferred=inferScalizerScale(snapshot.regions,index);
        const auto preferSharps=region.name.containsChar('#')||region.scaleOverride.containsChar('#');
        const auto types=scalizerScaleTypeNames();
        juce::PopupMenu scaleMenu;
        scaleMenu.addItem(900,"Auto (from chord)",true,region.scaleOverride.isEmpty());
        scaleMenu.addSeparator();
        for(int root=0;root<12;++root)
        {
            juce::PopupMenu rootMenu;
            for(int type=0;type<types.size();++type)
            {
                const auto name=makeScalizerScaleName(root,type,preferSharps);
                rootMenu.addItem(1000+root*100+type,types[type],true,
                                 region.scaleOverride.equalsIgnoreCase(name));
            }
            const auto rootName=makeScalizerScaleName(root,0,preferSharps).upToFirstOccurrenceOf(" ",false,false);
            scaleMenu.addSubMenu(rootName,rootMenu);
        }
        const auto scaleLabel=inferred.valid?inferred.name:"Unavailable";
        menu.addSubMenu("Scale  "+scaleLabel+(region.scaleOverride.isNotEmpty()?"  (Manual)":"  (Auto)"),scaleMenu);
        menu.addSeparator();
    }
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
    const auto preferSharps=region.name.containsChar('#')||region.scaleOverride.containsChar('#');
    menu.showMenuAsync(options,[safe,index,region,position,quickEdit,preferSharps](int result)
    {
        if(safe==nullptr)return;
        safe->quickEditMenuOpen=false;
        safe->selectedRegion.reset();
        if(result==0)
        {
            if(quickEdit&&safe->editingRegion.has_value())safe->chordNameEditor.grabKeyboardFocus();
            return;
        }
        if(result==900)
        {
            safe->performRegionEdit([safe,index]{if(safe!=nullptr)safe->chordProcessor.setRegionScaleOverride(index,{});});
        }
        else if(result>=1000&&result<2200)
        {
            const auto encoded=result-1000;
            const auto root=encoded/100,type=encoded%100;
            const auto name=makeScalizerScaleName(root,type,preferSharps);
            if(name.isNotEmpty())safe->performRegionEdit([safe,index,name]
            {
                if(safe!=nullptr)safe->chordProcessor.setRegionScaleOverride(index,name);
            });
        }
        else if(result==2)
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
        if(index.has_value()&&isScaleLanePoint(event.getPosition()))
        {
            clearRangeSelection();selectedRegion=*index;showRegionMenu(*index,event);return;
        }
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
    if(nativeMidiDropBridge)nativeMidiDropBridge->refresh();
    auto controls = getLocalBounds().removeFromTop(headerHeight).reduced(6, 5);
    constexpr int buttonWidth = 24;
    for (auto* button : { &viewButton, &editButton, &smallerTextButton, &largerTextButton,
                          &listenButton, &importButton, &clearButton, &copyButton, &quantizeButton, &undoButton, &redoButton })
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
    if(chordProcessor.supportsScalizer())
    {
        controls.removeFromLeft(4);
        scalizerButton.setBounds(controls.removeFromLeft(buttonWidth));controls.removeFromLeft(4);
        lockModeButton.setBounds(controls.removeFromLeft(50));controls.removeFromLeft(4);
        harmonyButton.setBounds(controls.removeFromLeft(buttonWidth));controls.removeFromLeft(6);
    }
    persistEditorState();
}
