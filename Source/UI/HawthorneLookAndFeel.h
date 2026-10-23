#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace hawthorne
{

namespace Colours
{
    const juce::Colour bg          { 0xff1b1b1a };
    const juce::Colour panel       { 0xff252523 };
    const juce::Colour panelLight  { 0xff302f2c };
    const juce::Colour edge        { 0xff3c3a35 };
    const juce::Colour mustard     { 0xffd9a521 };
    const juce::Colour mustardDim  { 0xff6e5617 };
    const juce::Colour text        { 0xffe8e2d0 };
    const juce::Colour textDim     { 0xff8a857a };
    const juce::Colour red         { 0xffd8432f };
    const juce::Colour redDim      { 0xff4a211b };
    const juce::Colour black       { 0xff0e0e0d };
}

inline juce::Font monoFont (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions ("Courier New", size, bold ? juce::Font::bold : juce::Font::plain));
}

inline juce::Font uiFont (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

//==============================================================================
class HawthorneLookAndFeel : public juce::LookAndFeel_V4
{
public:
    HawthorneLookAndFeel()
    {
        using namespace juce;
        setColour (ResizableWindow::backgroundColourId, Colours::bg);
        setColour (DocumentWindow::backgroundColourId, Colours::bg);

        setColour (TextButton::buttonColourId, Colours::panelLight);
        setColour (TextButton::buttonOnColourId, Colours::mustard);
        setColour (TextButton::textColourOffId, Colours::text);
        setColour (TextButton::textColourOnId, Colours::black);

        setColour (ComboBox::backgroundColourId, Colours::black);
        setColour (ComboBox::textColourId, Colours::text);
        setColour (ComboBox::outlineColourId, Colours::edge);
        setColour (ComboBox::arrowColourId, Colours::mustard);

        setColour (PopupMenu::backgroundColourId, Colours::panel);
        setColour (PopupMenu::textColourId, Colours::text);
        setColour (PopupMenu::highlightedBackgroundColourId, Colours::mustard);
        setColour (PopupMenu::highlightedTextColourId, Colours::black);

        setColour (Label::textColourId, Colours::text);
        setColour (Label::textWhenEditingColourId, Colours::text);
        setColour (Label::backgroundWhenEditingColourId, Colours::black);
        setColour (Label::outlineWhenEditingColourId, Colours::mustard);
        setColour (TextEditor::backgroundColourId, Colours::black);
        setColour (TextEditor::textColourId, Colours::text);
        setColour (TextEditor::highlightColourId, Colours::mustardDim);
        setColour (TextEditor::outlineColourId, Colours::edge);
        setColour (TextEditor::focusedOutlineColourId, Colours::mustard);
        setColour (CaretComponent::caretColourId, Colours::mustard);

        setColour (AlertWindow::backgroundColourId, Colours::panel);
        setColour (AlertWindow::textColourId, Colours::text);
        setColour (AlertWindow::outlineColourId, Colours::mustard);

        setColour (TooltipWindow::backgroundColourId, Colours::black);
        setColour (TooltipWindow::textColourId, Colours::text);
        setColour (TooltipWindow::outlineColourId, Colours::mustardDim);

        setColour (ListBox::backgroundColourId, Colours::black);
        setColour (ScrollBar::thumbColourId, Colours::mustardDim);
        setColour (Slider::thumbColourId, Colours::mustard);
        setColour (Slider::trackColourId, Colours::mustardDim);
        setColour (ToggleButton::tickColourId, Colours::mustard);
        setColour (ToggleButton::textColourId, Colours::text);

        auto scheme = getDarkColourScheme();
        scheme.setUIColour (ColourScheme::windowBackground, Colours::panel);
        scheme.setUIColour (ColourScheme::widgetBackground, Colours::panelLight);
        scheme.setUIColour (ColourScheme::menuBackground, Colours::panel);
        scheme.setUIColour (ColourScheme::outline, Colours::edge);
        scheme.setUIColour (ColourScheme::defaultText, Colours::text);
        scheme.setUIColour (ColourScheme::defaultFill, Colours::mustard);
        scheme.setUIColour (ColourScheme::highlightedText, Colours::black);
        scheme.setUIColour (ColourScheme::highlightedFill, Colours::mustard);
        scheme.setUIColour (ColourScheme::menuText, Colours::text);
        setColourScheme (scheme);
    }

    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& bgColour,
                               bool highlighted, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (0.5f);
        auto fill = b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId) : bgColour;

        if (! b.isEnabled()) fill = fill.withMultipliedAlpha (0.4f);
        else if (down)       fill = fill.darker (0.25f);
        else if (highlighted) fill = fill.brighter (0.12f);

        g.setColour (fill);
        g.fillRoundedRectangle (r, 3.0f);
        g.setColour (Colours::edge.brighter (b.getToggleState() ? 0.6f : 0.0f));
        g.drawRoundedRectangle (r, 3.0f, 1.0f);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override
    {
        return uiFont (juce::jmin (13.0f, (float) buttonHeight * 0.5f), true);
    }

    juce::Font getComboBoxFont (juce::ComboBox&) override   { return monoFont (12.0f, true); }
    juce::Font getPopupMenuFont() override                  { return uiFont (14.0f); }
    juce::Font getAlertWindowMessageFont() override         { return uiFont (14.0f); }
    juce::Font getAlertWindowTitleFont() override           { return monoFont (18.0f, true); }

    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (0.5f);
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (r, 2.0f);
        g.setColour (box.hasKeyboardFocus (false) ? Colours::mustard : Colours::edge);
        g.drawRoundedRectangle (r, 2.0f, 1.0f);
    }

    void positionComboBoxText (juce::ComboBox& box, juce::Label& label) override
    {
        label.setBounds (box.getLocalBounds().reduced (2, 1));
        label.setFont (getComboBoxFont (box));
        label.setJustificationType (juce::Justification::centred);
    }
};

} // namespace hawthorne
