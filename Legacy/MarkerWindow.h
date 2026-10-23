#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "VintageLookAndFeel.h"
#include "AudioEngine.h"

//==============================================================================
// Single marker row in the list
class MarkerRow : public juce::Component
{
public:
    MarkerRow(Marker* markerPtr, AudioEngine* engine)
        : marker(markerPtr), audioEngine(engine)
    {
        // Number label
        numberLabel.setText(juce::String(marker->getNumber()), juce::dontSendNotification);
        numberLabel.setFont(juce::Font(14.0f, juce::Font::bold));
        numberLabel.setColour(juce::Label::textColourId, juce::Colour(VintageLookAndFeel::accentRed));
        numberLabel.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(numberLabel);
        
        // Time label
        double seconds = marker->getTimestamp();
        int mins = (int)seconds / 60;
        int secs = (int)seconds % 60;
        int ms = (int)((seconds - (int)seconds) * 100);
        juce::String timeText = juce::String(mins) + ":" + 
                               juce::String(secs).paddedLeft('0', 2) + "." +
                               juce::String(ms).paddedLeft('0', 2);
        
        timeLabel.setText(timeText, juce::dontSendNotification);
        timeLabel.setFont(juce::Font(12.0f, juce::Font::bold));
        timeLabel.setColour(juce::Label::textColourId, juce::Colour(VintageLookAndFeel::accentAmber));
        timeLabel.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(timeLabel);
        
        // Label editor
        labelEditor.setText(marker->getLabel());
        labelEditor.setFont(juce::Font(11.0f));
        labelEditor.setColour(juce::TextEditor::backgroundColourId, juce::Colour(0xff2a2520));
        labelEditor.setColour(juce::TextEditor::textColourId, juce::Colour(VintageLookAndFeel::panelCream));
        labelEditor.setColour(juce::TextEditor::outlineColourId, juce::Colour(VintageLookAndFeel::metalDark));
        labelEditor.onReturnKey = [this]() {
            marker->setLabel(labelEditor.getText());
            labelEditor.setHighlightedRegion(juce::Range<int>(0, 0));
        };
        addAndMakeVisible(labelEditor);
        
        // Jump button
        jumpButton.setButtonText("→");
        jumpButton.setColour(juce::TextButton::buttonColourId, juce::Colour(VintageLookAndFeel::bgPanel));
        jumpButton.setColour(juce::TextButton::textColourOffId, juce::Colour(VintageLookAndFeel::accentAmber));
        jumpButton.onClick = [this]() {
            if (audioEngine)
                audioEngine->goToMarker(marker->getNumber());
        };
        addAndMakeVisible(jumpButton);
        
        // Delete button
        deleteButton.setButtonText("X");
        deleteButton.setColour(juce::TextButton::buttonColourId, juce::Colour(VintageLookAndFeel::bgPanel));
        deleteButton.setColour(juce::TextButton::textColourOffId, juce::Colour(VintageLookAndFeel::accentRed));
        deleteButton.onClick = [this]() {
            if (onDelete)
                onDelete(marker->getNumber());
        };
        addAndMakeVisible(deleteButton);
    }
    
    void paint(juce::Graphics& g) override
    {
        auto bounds = getLocalBounds();
        
        // Background
        g.setColour(juce::Colour(VintageLookAndFeel::bgTrack));
        g.fillRect(bounds);
        
        // Border
        g.setColour(juce::Colour(VintageLookAndFeel::metalDark));
        g.drawRect(bounds, 1);
    }
    
    void resized() override
    {
        auto bounds = getLocalBounds().reduced(4);
        
        // Layout: [#] [Time] [Label Editor............] [→] [X]
        numberLabel.setBounds(bounds.removeFromLeft(30));
        bounds.removeFromLeft(4);
        timeLabel.setBounds(bounds.removeFromLeft(70));
        bounds.removeFromLeft(4);
        
        auto buttons = bounds.removeFromRight(50);
        deleteButton.setBounds(buttons.removeFromRight(22));
        buttons.removeFromLeft(2);
        jumpButton.setBounds(buttons);
        
        bounds.removeFromLeft(4);
        labelEditor.setBounds(bounds);
    }
    
    std::function<void(int)> onDelete;
    
private:
    Marker* marker;
    AudioEngine* audioEngine;
    
    juce::Label numberLabel;
    juce::Label timeLabel;
    juce::TextEditor labelEditor;
    juce::TextButton jumpButton;
    juce::TextButton deleteButton;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MarkerRow)
};

//==============================================================================
// Marker window with list of all markers
class MarkerWindow : public juce::Component, public juce::Timer
{
public:
    MarkerWindow(AudioEngine* engine)
        : audioEngine(engine)
    {
        // Title
        titleLabel.setText("MARKERS", juce::dontSendNotification);
        titleLabel.setFont(juce::Font(16.0f, juce::Font::bold));
        titleLabel.setColour(juce::Label::textColourId, juce::Colour(VintageLookAndFeel::metalDark));
        titleLabel.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(titleLabel);
        
        // Add marker button
        addButton.setButtonText("+ Add Marker");
        addButton.setColour(juce::TextButton::buttonColourId, juce::Colour(VintageLookAndFeel::bgPanel));
        addButton.setColour(juce::TextButton::textColourOffId, juce::Colour(VintageLookAndFeel::accentGreen));
        addButton.onClick = [this]() {
            if (audioEngine)
            {
                double currentTime = audioEngine->getTransportState().currentTime;
                audioEngine->addMarker(currentTime, "");
                refreshMarkerList();
            }
        };
        addAndMakeVisible(addButton);
        
        // Marker list viewport
        addAndMakeVisible(markerViewport);
        markerViewport.setViewedComponent(&markerContainer, false);
        markerViewport.setScrollBarsShown(true, false);
        
        // Start timer to refresh list
        startTimer(500);  // Refresh every 500ms
    }
    
    void paint(juce::Graphics& g) override
    {
        // Background
        g.fillAll(juce::Colour(VintageLookAndFeel::bgDark));
        
        // Border
        g.setColour(juce::Colour(VintageLookAndFeel::metalDark));
        g.drawRect(getLocalBounds(), 2);
    }
    
    void resized() override
    {
        auto bounds = getLocalBounds().reduced(8);
        
        // Title
        titleLabel.setBounds(bounds.removeFromTop(25));
        bounds.removeFromTop(4);
        
        // Add button
        addButton.setBounds(bounds.removeFromTop(30));
        bounds.removeFromTop(8);
        
        // Marker list
        markerViewport.setBounds(bounds);
        
        refreshMarkerList();
    }
    
    void timerCallback() override
    {
        // Refresh marker list periodically
        refreshMarkerList();
    }
    
    void refreshMarkerList()
    {
        if (!audioEngine) return;
        
        auto* markerManager = audioEngine->getMarkerManager();
        if (!markerManager) return;
        
        // Clear existing rows
        markerContainer.removeAllChildren();
        markerRows.clear();
        
        const auto& markers = markerManager->getMarkers();
        
        if (markers.empty())
        {
            // Show "No markers" message
            auto* label = new juce::Label();
            label->setText("No markers yet. Click + to add one.", juce::dontSendNotification);
            label->setFont(juce::Font(12.0f));
            label->setColour(juce::Label::textColourId, juce::Colour(VintageLookAndFeel::metalDark));
            label->setJustificationType(juce::Justification::centred);
            label->setBounds(0, 0, markerViewport.getWidth() - 20, 40);
            markerContainer.addAndMakeVisible(label);
            markerRows.add(label);
            
            markerContainer.setSize(markerViewport.getWidth() - 20, 40);
            return;
        }
        
        // Create row for each marker
        int y = 0;
        for (const auto& marker : markers)
        {
            auto* row = new MarkerRow(marker.get(), audioEngine);
            row->onDelete = [this, markerManager](int markerNumber) {
                if (audioEngine)
                {
                    audioEngine->removeMarker(markerNumber);
                    refreshMarkerList();
                }
            };
            
            row->setBounds(0, y, markerViewport.getWidth() - 20, 40);
            markerContainer.addAndMakeVisible(row);
            markerRows.add(row);
            
            y += 44;  // 40 height + 4 gap
        }
        
        markerContainer.setSize(markerViewport.getWidth() - 20, y);
    }
    
private:
    AudioEngine* audioEngine;
    
    juce::Label titleLabel;
    juce::TextButton addButton;
    juce::Viewport markerViewport;
    juce::Component markerContainer;
    juce::OwnedArray<juce::Component> markerRows;
    
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MarkerWindow)
};
