#pragma once

#include <JuceHeader.h>

// Shared Songizer control language, derived from Logic Pro's compact
// charcoal controls. Musical state colours remain the responsibility of each
// plug-in; this class unifies the neutral chrome, typography, and geometry.
class SongizerLogicLookAndFeel : public juce::LookAndFeel_V4
{
public:
    static juce::Colour window() noexcept { return juce::Colour (0xff1f1f1f); }
    static juce::Colour panel() noexcept { return juce::Colour (0xff292929); }
    static juce::Colour raised() noexcept { return juce::Colour (0xff414141); }
    static juce::Colour recessed() noexcept { return juce::Colour (0xff111111); }
    static juce::Colour line() noexcept { return juce::Colour (0xff555555); }
    static juce::Colour text() noexcept { return juce::Colour (0xffe6e6e6); }
    static juce::Colour muted() noexcept { return juce::Colour (0xff9a9a9a); }
    static juce::Colour blue() noexcept { return juce::Colour (0xff3478d4); }
    static juce::Colour green() noexcept { return juce::Colour (0xff4cab45); }
    static juce::Colour orange() noexcept { return juce::Colour (0xffee8318); }
    static juce::Colour red() noexcept { return juce::Colour (0xffd6453d); }

    SongizerLogicLookAndFeel()
    {
        setDefaultSansSerifTypefaceName (juce::Font::getDefaultSansSerifFontName());
        setColour (juce::ResizableWindow::backgroundColourId, window());
        setColour (juce::Label::textColourId, text());
        setColour (juce::TextButton::buttonColourId, raised());
        setColour (juce::TextButton::buttonOnColourId, blue());
        setColour (juce::TextButton::textColourOffId, text());
        setColour (juce::TextButton::textColourOnId, juce::Colours::white);
        setColour (juce::ToggleButton::textColourId, text());
        setColour (juce::ToggleButton::tickColourId, blue());
        setColour (juce::ToggleButton::tickDisabledColourId, muted());
        setColour (juce::ComboBox::backgroundColourId, raised());
        setColour (juce::ComboBox::textColourId, text());
        setColour (juce::ComboBox::outlineColourId, line());
        setColour (juce::ComboBox::arrowColourId, text());
        setColour (juce::PopupMenu::backgroundColourId, panel());
        setColour (juce::PopupMenu::textColourId, text());
        setColour (juce::PopupMenu::highlightedBackgroundColourId, blue());
        setColour (juce::PopupMenu::highlightedTextColourId, juce::Colours::white);
        setColour (juce::Slider::backgroundColourId, recessed());
        setColour (juce::Slider::trackColourId, blue());
        setColour (juce::Slider::thumbColourId, juce::Colour (0xffb8b8b8));
        setColour (juce::Slider::textBoxTextColourId, text());
        setColour (juce::Slider::textBoxBackgroundColourId, juce::Colour (0xff181818));
        setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        setColour (juce::TextEditor::backgroundColourId, recessed());
        setColour (juce::TextEditor::textColourId, text());
        setColour (juce::TextEditor::outlineColourId, line());
        setColour (juce::TextEditor::focusedOutlineColourId, blue());
        setColour (juce::ScrollBar::thumbColourId, juce::Colour (0xff6a6a6a));
        setColour (juce::TooltipWindow::backgroundColourId, juce::Colour (0xff303030));
        setColour (juce::TooltipWindow::textColourId, text());
        setColour (juce::TooltipWindow::outlineColourId, line());
    }

    juce::Font getTextButtonFont (juce::TextButton&, int height) override
    {
        return juce::Font (juce::FontOptions (juce::jlimit (10.0f, 13.0f, height * 0.39f)));
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override
    {
        return juce::Font (juce::FontOptions (12.0f));
    }

    juce::Font getPopupMenuFont() override
    {
        return juce::Font (juce::FontOptions (12.0f));
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& button,
                               const juce::Colour& backgroundColour,
                               bool highlighted, bool down) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        auto fill = backgroundColour;
        if (button.getToggleState())
            fill = button.findColour (juce::TextButton::buttonOnColourId);
        if (highlighted)
            fill = fill.brighter (0.07f);
        if (down)
            fill = fill.darker (0.10f);
        if (! button.isEnabled())
            fill = fill.withAlpha (0.42f);

        g.setColour (fill);
        g.fillRoundedRectangle (bounds, 4.0f);
        g.setColour ((button.getToggleState() ? blue().brighter (0.12f) : line()).withAlpha (button.isEnabled() ? 0.88f : 0.35f));
        g.drawRoundedRectangle (bounds, 4.0f, 1.0f);
    }

    void drawToggleButton (juce::Graphics& g, juce::ToggleButton& button,
                           bool highlighted, bool down) override
    {
        juce::ignoreUnused (down);
        auto box = juce::Rectangle<float> (2.0f, (button.getHeight() - 14.0f) * 0.5f, 14.0f, 14.0f);
        auto fill = button.getToggleState() ? blue() : recessed();
        if (highlighted)
            fill = fill.brighter (0.08f);
        g.setColour (fill.withAlpha (button.isEnabled() ? 1.0f : 0.42f));
        g.fillRoundedRectangle (box, 3.0f);
        g.setColour ((button.getToggleState() ? blue().brighter (0.18f) : line()).withAlpha (0.9f));
        g.drawRoundedRectangle (box, 3.0f, 1.0f);

        if (button.getToggleState())
        {
            juce::Path check;
            check.startNewSubPath (5.0f, box.getCentreY());
            check.lineTo (8.0f, box.getBottom() - 3.5f);
            check.lineTo (13.5f, box.getY() + 3.5f);
            g.setColour (juce::Colours::white.withAlpha (button.isEnabled() ? 1.0f : 0.5f));
            g.strokePath (check, juce::PathStrokeType (1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        g.setColour (button.findColour (juce::ToggleButton::textColourId).withAlpha (button.isEnabled() ? 1.0f : 0.45f));
        g.setFont (juce::FontOptions (12.0f));
        g.drawFittedText (button.getButtonText(), 22, 0, button.getWidth() - 24, button.getHeight(),
                          juce::Justification::centredLeft, 1);
    }

    void drawComboBox (juce::Graphics& g, int width, int height, bool down,
                       int, int, int, int, juce::ComboBox& box) override
    {
        auto bounds = juce::Rectangle<float> (0.5f, 0.5f, width - 1.0f, height - 1.0f);
        auto fill = box.findColour (juce::ComboBox::backgroundColourId);
        if (down)
            fill = fill.darker (0.08f);
        g.setColour (fill);
        g.fillRoundedRectangle (bounds, 4.0f);
        g.setColour (box.findColour (juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle (bounds, 4.0f, 1.0f);

        const auto x = width - 14.0f;
        const auto y = height * 0.5f;
        juce::Path arrow;
        arrow.startNewSubPath (x - 3.0f, y - 1.5f);
        arrow.lineTo (x, y + 1.5f);
        arrow.lineTo (x + 3.0f, y - 1.5f);
        g.setColour (box.findColour (juce::ComboBox::arrowColourId));
        g.strokePath (arrow, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    void drawLinearSlider (juce::Graphics& g, int x, int y, int width, int height,
                           float sliderPos, float minSliderPos, float maxSliderPos,
                           juce::Slider::SliderStyle style, juce::Slider& slider) override
    {
        const bool horizontal = style == juce::Slider::LinearHorizontal
                             || style == juce::Slider::LinearBar;
        const bool vertical = style == juce::Slider::LinearVertical
                           || style == juce::Slider::LinearBarVertical;
        if (! horizontal && ! vertical)
        {
            juce::LookAndFeel_V4::drawLinearSlider (g, x, y, width, height, sliderPos, minSliderPos, maxSliderPos, style, slider);
            return;
        }

        auto track = horizontal
            ? juce::Rectangle<float> ((float) x + 5.0f, (float) y + height * 0.5f - 2.5f, (float) width - 10.0f, 5.0f)
            : juce::Rectangle<float> ((float) x + width * 0.5f - 2.5f, (float) y + 5.0f, 5.0f, (float) height - 10.0f);
        g.setColour (slider.findColour (juce::Slider::backgroundColourId));
        g.fillRoundedRectangle (track, 2.5f);

        auto active = horizontal
            ? juce::Rectangle<float> (track.getX(), track.getY(), juce::jmax (0.0f, sliderPos - track.getX()), track.getHeight())
            : juce::Rectangle<float> (track.getX(), sliderPos, track.getWidth(), juce::jmax (0.0f, track.getBottom() - sliderPos));
        g.setColour (slider.findColour (juce::Slider::trackColourId));
        g.fillRoundedRectangle (active, 2.5f);

        const float radius = slider.isMouseOverOrDragging() ? 7.0f : 6.0f;
        auto centre = horizontal ? juce::Point<float> (sliderPos, track.getCentreY())
                                 : juce::Point<float> (track.getCentreX(), sliderPos);
        g.setColour (juce::Colours::black.withAlpha (0.38f));
        g.fillEllipse (centre.x - radius, centre.y - radius + 1.0f, radius * 2.0f, radius * 2.0f);
        g.setColour (slider.findColour (juce::Slider::thumbColourId));
        g.fillEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
        g.setColour (juce::Colours::white.withAlpha (0.22f));
        g.drawEllipse (centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.0f);
    }

    void drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                           float position, float startAngle, float endAngle, juce::Slider& slider) override
    {
        auto area = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height).reduced (5.0f);
        const auto diameter = juce::jmin (area.getWidth(), area.getHeight());
        area = area.withSizeKeepingCentre (diameter, diameter);
        const auto angle = startAngle + position * (endAngle - startAngle);
        g.setColour (recessed());
        g.fillEllipse (area);
        g.setColour (juce::Colour (0xff898989));
        g.fillEllipse (area.reduced (2.0f));
        g.setColour (line());
        g.drawEllipse (area, 1.0f);
        juce::Path marker;
        marker.addRoundedRectangle (-1.0f, -area.getHeight() * 0.35f, 2.0f, area.getHeight() * 0.28f, 1.0f);
        g.setColour (slider.findColour (juce::Slider::trackColourId));
        g.fillPath (marker, juce::AffineTransform::rotation (angle).translated (area.getCentreX(), area.getCentreY()));
    }
};
