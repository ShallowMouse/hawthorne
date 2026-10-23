#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class VintageLookAndFeel : public juce::LookAndFeel_V4
{
public:
    // Exact colors from web CSS
    static constexpr uint32_t bgDark = 0xff1a1512;
    static constexpr uint32_t bgPanel = 0xff2d2520;
    static constexpr uint32_t bgTrack = 0xff1e1b18;
    static constexpr uint32_t accentRed = 0xff8b3a3a;
    static constexpr uint32_t accentAmber = 0xffb8860b;
    static constexpr uint32_t accentGreen = 0xff5a7a5a;
    static constexpr uint32_t metalLight = 0xff8a8a7a;
    static constexpr uint32_t metalDark = 0xff3a362f;
    static constexpr uint32_t waveformColor = 0xff6b8e6b;
    static constexpr uint32_t tapeBeige = 0xffd4c4a8;
    static constexpr uint32_t panelCream = 0xffe8dcc8;
    
    VintageLookAndFeel()
    {
        setColour(juce::ResizableWindow::backgroundColourId, juce::Colour(bgDark));
        setColour(juce::TextButton::buttonColourId, juce::Colour(metalLight));
        setColour(juce::TextButton::textColourOffId, juce::Colour(tapeBeige));
        setColour(juce::Label::textColourId, juce::Colour(panelCream));
    }
    
    void drawButtonBackground(juce::Graphics& g, juce::Button& button, 
                             const juce::Colour& backgroundColour,
                             bool isMouseOverButton, bool isButtonDown) override
    {
        auto bounds = button.getLocalBounds().toFloat();
        
        // Metal gradient button matching CSS
        juce::ColourGradient gradient;
        if (isButtonDown)
        {
            gradient = juce::ColourGradient(
                juce::Colour(0xff5a5a4a), 0.0f, 0.0f,
                juce::Colour(metalLight), 0.0f, bounds.getHeight(), false);
        }
        else if (isMouseOverButton)
        {
            gradient = juce::ColourGradient(
                juce::Colour(0xff9a9a8a), 0.0f, 0.0f,
                juce::Colour(0xff6a6a5a), 0.0f, bounds.getHeight(), false);
        }
        else
        {
            gradient = juce::ColourGradient(
                juce::Colour(metalLight), 0.0f, 0.0f,
                juce::Colour(0xff5a5a4a), 0.0f, bounds.getHeight(), false);
        }
        
        g.setGradientFill(gradient);
        g.fillRoundedRectangle(bounds.reduced(1), 6.0f);
        
        // Border
        g.setColour(juce::Colour(metalDark));
        g.drawRoundedRectangle(bounds.reduced(1), 6.0f, 2.0f);
        
        // Top highlight for 3D effect
        if (!isButtonDown)
        {
            g.setColour(juce::Colours::white.withAlpha(0.2f));
            g.drawLine(bounds.getX() + 4, bounds.getY() + 2, 
                      bounds.getRight() - 4, bounds.getY() + 2, 1.0f);
        }
    }
    
    juce::Font getLabelFont(juce::Label& label) override
    {
        return juce::Font(juce::Font::getDefaultMonospacedFontName(), 13.0f, juce::Font::bold);
    }
    
    juce::Font getTextButtonFont(juce::TextButton&, int buttonHeight) override
    {
        return juce::Font(juce::Font::getDefaultMonospacedFontName(), 12.0f, juce::Font::bold);
    }
};
