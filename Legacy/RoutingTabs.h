#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_audio_devices/juce_audio_devices.h>
#include "VintageLookAndFeel.h"
#include "Settings.h"
#include "AudioEngine.h"
#include "Track.h"

// Special settings-key for the LTC decoder input column in the input routing matrix.
// Negative so it can never collide with a real track index.
static constexpr int kLTCInputKey = -2;

//==============================================================================
// RoutingCell — custom painted matrix cell (replaces TextButton).
//   active=false : very dark fill + subtle border
//   active=true  : column colour fill + white LED dot
//   hover        : slight brightness boost on idle cells
//==============================================================================
class RoutingCell : public juce::Component
{
public:
    bool   active  = false;
    bool   enabled = true;
    juce::Colour onColour { VintageLookAndFeel::accentAmber };
    std::function<void()> onClicked;

    explicit RoutingCell(juce::Colour col = juce::Colour(VintageLookAndFeel::accentAmber))
        : onColour(col) {}

    void paint(juce::Graphics& g) override
    {
        const auto b = getLocalBounds().toFloat().reduced(2.0f);
        if (!enabled)
        {
            g.setColour(juce::Colour(0xff0e0c09));
            g.fillRoundedRectangle(b, 3.0f);
            return;
        }
        if (active)
        {
            // Outer glow
            g.setColour(onColour.withAlpha(0.18f));
            g.fillRoundedRectangle(b.expanded(2.0f), 5.0f);
            // Cell fill
            g.setColour(onColour);
            g.fillRoundedRectangle(b, 3.0f);
            // White LED dot in centre
            const float r = juce::jmin(b.getWidth(), b.getHeight()) * 0.20f;
            g.setColour(juce::Colours::white.withAlpha(0.92f));
            g.fillEllipse(b.getCentreX() - r, b.getCentreY() - r, r * 2.0f, r * 2.0f);
        }
        else
        {
            const bool hovered = isMouseOver();
            g.setColour(hovered ? juce::Colour(0xff2c2720) : juce::Colour(0xff181511));
            g.fillRoundedRectangle(b, 3.0f);
            g.setColour(hovered ? juce::Colour(0xff554f48) : juce::Colour(0xff2e2a25));
            g.drawRoundedRectangle(b.reduced(0.5f), 3.0f, 1.0f);
        }
    }

    void mouseDown(const juce::MouseEvent&) override
    {
        if (enabled && onClicked) onClicked();
    }
    void mouseEnter(const juce::MouseEvent&) override { repaint(); }
    void mouseExit (const juce::MouseEvent&) override { repaint(); }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RoutingCell)
};

//==============================================================================
// GridContainer — paints subtle grid lines behind the cells
//==============================================================================
class GridContainer : public juce::Component
{
public:
    int cellSize   = 32;
    int labelWidth = 100;
    int numRows    = 8;
    int numCols    = 8;
    int headerH    = 42;

    void paint(juce::Graphics& g) override
    {
        g.fillAll(juce::Colour(0xff141210));
        g.setColour(juce::Colour(0xff252220));
        // Vertical column dividers
        for (int c = 0; c <= numCols; ++c)
            g.drawVerticalLine(labelWidth + c * cellSize, 0.0f, (float)getHeight());
        // Horizontal row dividers
        for (int r = 0; r <= numRows; ++r)
            g.drawHorizontalLine(headerH + r * cellSize, 0.0f, (float)getWidth());
        // Header bottom separator
        g.setColour(juce::Colour(VintageLookAndFeel::metalDark));
        g.drawHorizontalLine(headerH - 1, 0.0f, (float)getWidth());
        // Label right separator
        g.drawVerticalLine(labelWidth - 1, 0.0f, (float)getHeight());
    }
};

//==============================================================================
// RoutingMatrixTab — Input routing
//   Rows   = hardware input channels (labelled on the left)
//   Cols   = tracks (T1…TN) + LTC decoder column
//   Rule   = single active cell per column (one input per track / LTC)
//   Extra  = LTC Chase toggle at top
//==============================================================================
class RoutingMatrixTab : public juce::Component
{
public:
    RoutingMatrixTab(Settings* settingsPtr, juce::AudioDeviceManager* deviceMgr,
                     AudioEngine* audioEnginePtr, int trackCount = 8, int startTrackIdx = 0)
        : settings(settingsPtr), audioDeviceManager(deviceMgr),
          audioEngine(audioEnginePtr), numTracks(trackCount),
          startTrackIndex(startTrackIdx), numInputs(8)    {
        try
        {
            // ---- LTC Chase strip ----
            ltcSlaveLabel.setText("LTC Chase (slave transport to incoming LTC):",
                                  juce::dontSendNotification);
            ltcSlaveLabel.setFont(juce::Font(12.0f, juce::Font::bold));
            ltcSlaveLabel.setColour(juce::Label::textColourId, juce::Colour(0xff6699ff));
            addAndMakeVisible(ltcSlaveLabel);

            ltcSlaveToggle.setButtonText("  CHASE");
            ltcSlaveToggle.setClickingTogglesState(true);
            ltcSlaveToggle.setColour(juce::ToggleButton::textColourId, juce::Colour(0xff6699ff));
            ltcSlaveToggle.setColour(juce::ToggleButton::tickColourId, juce::Colour(0xff6699ff));
            ltcSlaveToggle.setColour(juce::ToggleButton::tickDisabledColourId, juce::Colour(0xff444444));
            bool slave = settings ? settings->getLTCSlaveMode() : false;
            ltcSlaveToggle.setToggleState(slave, juce::dontSendNotification);
            ltcSlaveToggle.onClick = [this] {
                if (settings) settings->setLTCSlaveMode(ltcSlaveToggle.getToggleState());
                if (audioEngine) {
                    int ch = settings ? settings->getLTCInputChannel() : -1;
                    audioEngine->setLTCInputEnabled(ch >= 0, ltcSlaveToggle.getToggleState());
                }
            };
            addAndMakeVisible(ltcSlaveToggle);

            // ---- Matrix ----
            matrixViewport.setViewedComponent(&gridContainer, false);
            matrixViewport.setScrollBarsShown(true, true);
            addAndMakeVisible(matrixViewport);

            updateInputCount();
            buildColumnInfos();
            buildMatrix();
            loadRouting();
        }
        catch (const std::exception& e)
        {
            juce::Logger::writeToLog("RoutingMatrixTab error: " + juce::String(e.what()));
        }
    }

    void updateInputCount()
    {
        numInputs = 8;
        if (audioDeviceManager)
            if (auto* dev = audioDeviceManager->getCurrentAudioDevice())
                numInputs = juce::jmax(1, dev->getInputChannelNames().size());
    }

    void setTrackCount(int count)
    {
        numTracks = count;
        updateInputCount();
        cells.clear();
        rowLabels.clear();
        colLabels.clear();
        gridContainer.removeAllChildren();
        buildColumnInfos();
        buildMatrix();
        loadRouting();
        resized();
    }

    // ColInfo: settingsKey = track index or kLTCInputKey for LTC decoder
    struct ColInfo { int settingsKey; juce::String label; juce::Colour colour; };
    std::vector<ColInfo> columnInfos;

    void buildColumnInfos()
    {
        columnInfos.clear();
        // Iterate engine indices startTrackIndex .. startTrackIndex+numTracks-1
        // settingsKey = engine index (stable); label = user-facing track number
        for (int t = startTrackIndex; t < startTrackIndex + numTracks; ++t)
        {
            Track* tr = audioEngine ? audioEngine->getTrack(t) : nullptr;
            if (tr && tr->isMixdown()) continue;       // mixdown has no record input
            if (tr && tr->isClickTrack()) continue;    // click is generated, not recorded
            if (tr && tr->isTimecodeTrack()) continue; // LTC generator needs no input (handled below)
            int userNum = t - startTrackIndex + 1;
            columnInfos.push_back({ t, "T" + juce::String(userNum), juce::Colour(VintageLookAndFeel::metalLight) });
        }
        // LTC decoder input — always present so user can route hardware LTC input
        columnInfos.push_back({ kLTCInputKey, "LTC\nIN", juce::Colour(0xff6699ff) });
    }

    void buildMatrix()
    {
        const int numCols = (int)columnInfos.size();

        // Column header labels
        for (int c = 0; c < numCols; ++c)
        {
            auto* lbl = new juce::Label();
            lbl->setText(columnInfos[c].label, juce::dontSendNotification);
            lbl->setColour(juce::Label::textColourId, columnInfos[c].colour);
            lbl->setFont(juce::Font(9.0f, juce::Font::bold));
            lbl->setJustificationType(juce::Justification::centred);
            gridContainer.addAndMakeVisible(lbl);
            colLabels.add(lbl);
        }

        // Row labels (input channel names) and cells
        for (int r = 0; r < numInputs; ++r)
        {
            auto* rowLbl = new juce::Label();
            juce::String name = settings ? settings->getInputChannelName(r) : ("IN " + juce::String(r + 1));
            rowLbl->setText(name, juce::dontSendNotification);
            rowLbl->setFont(juce::Font(10.0f));
            rowLbl->setColour(juce::Label::textColourId, juce::Colour(0xffccbb99));
            rowLbl->setJustificationType(juce::Justification::centredRight);
            gridContainer.addAndMakeVisible(rowLbl);
            rowLabels.add(rowLbl);

            for (int c = 0; c < numCols; ++c)
            {
                auto* cell = new RoutingCell(columnInfos[c].colour);
                cell->onClicked = [this, r, c]() { handleCellClick(c, r); };
                gridContainer.addAndMakeVisible(cell);
                cells.add(cell);
            }
        }

        updateContainerSize();
        gridContainer.numRows = numInputs;
        gridContainer.numCols = numCols;
        gridContainer.cellSize   = CELL_SIZE;
        gridContainer.labelWidth = LABEL_WIDTH;
        gridContainer.headerH    = HEADER_HEIGHT;
    }

    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(VintageLookAndFeel::bgTrack)); }

    void resized() override
    {
        auto b = getLocalBounds().reduced(8);

        // LTC chase strip at top
        auto strip = b.removeFromTop(28);
        ltcSlaveLabel.setBounds(strip.removeFromLeft(300));
        ltcSlaveToggle.setBounds(strip.removeFromLeft(100));
        b.removeFromTop(4);

        // Matrix viewport fills rest
        matrixViewport.setBounds(b);
        updateContainerSize();
        layoutCells();
    }

    void updateContainerSize()
    {
        const int numCols = (int)columnInfos.size();
        int w = LABEL_WIDTH + numCols * CELL_SIZE + 2;
        int h = HEADER_HEIGHT + numInputs * CELL_SIZE + 2;
        gridContainer.setSize(w, h);
    }

    void layoutCells()
    {
        const int numCols = (int)columnInfos.size();
        // Column header labels
        for (int c = 0; c < colLabels.size(); ++c)
            colLabels[c]->setBounds(LABEL_WIDTH + c * CELL_SIZE, 0, CELL_SIZE, HEADER_HEIGHT);
        // Row labels + cells
        for (int r = 0; r < numInputs; ++r)
        {
            if (r < rowLabels.size())
                rowLabels[r]->setBounds(2, HEADER_HEIGHT + r * CELL_SIZE, LABEL_WIDTH - 4, CELL_SIZE);
            for (int c = 0; c < numCols; ++c)
            {
                int idx = r * numCols + c;
                if (idx < cells.size())
                    cells[idx]->setBounds(LABEL_WIDTH + c * CELL_SIZE + 1,
                                          HEADER_HEIGHT + r * CELL_SIZE + 1,
                                          CELL_SIZE - 2, CELL_SIZE - 2);
            }
        }
    }

    void handleCellClick(int col, int row)
    {
        const int numCols = (int)columnInfos.size();
        if (col < 0 || col >= numCols) return;
        int idx = row * numCols + col;
        if (idx >= cells.size()) return;

        bool wasActive = cells[idx]->active;
        // Toggle this cell
        cells[idx]->active = !wasActive;
        cells[idx]->repaint();

        if (!wasActive)
        {
            // Turn off all other rows in this column (single-select)
            for (int r = 0; r < numInputs; ++r)
            {
                if (r == row) continue;
                int i = r * numCols + col;
                if (i < cells.size() && cells[i]->active)
                {
                    cells[i]->active = false;
                    cells[i]->repaint();
                }
            }
            // Save
            if (settings)
            {
                if (columnInfos[col].settingsKey == kLTCInputKey)
                    settings->setLTCInputChannel(row);
                else
                    settings->setTrackInputMapping(columnInfos[col].settingsKey, row);
            }
        }
        else
        {
            // Deselected — clear the mapping
            if (settings)
            {
                if (columnInfos[col].settingsKey == kLTCInputKey)
                    settings->setLTCInputChannel(-1);
                else
                    settings->setTrackInputMapping(columnInfos[col].settingsKey, -1);
            }
        }
    }

    void loadRouting()
    {
        if (!settings) return;
        const int numCols = (int)columnInfos.size();
        for (int c = 0; c < numCols; ++c)
        {
            int assignedRow = -1;
            if (columnInfos[c].settingsKey == kLTCInputKey)
                assignedRow = settings->getLTCInputChannel();
            else
                assignedRow = settings->getTrackInputMapping(columnInfos[c].settingsKey);

            for (int r = 0; r < numInputs; ++r)
            {
                int idx = r * numCols + c;
                if (idx < cells.size())
                    cells[idx]->active = (r == assignedRow);
            }
        }
        for (auto* cell : cells) cell->repaint();
    }

    void refreshRowLabels()
    {
        for (int r = 0; r < rowLabels.size() && r < numInputs; ++r)
        {
            juce::String name = settings ? settings->getInputChannelName(r) : ("IN " + juce::String(r + 1));
            rowLabels[r]->setText(name, juce::dontSendNotification);
        }
    }

    void saveRouting() {}

private:
    static constexpr int CELL_SIZE    = 32;
    static constexpr int LABEL_WIDTH  = 100;
    static constexpr int HEADER_HEIGHT = 42;

    Settings*                  settings;
    juce::AudioDeviceManager*  audioDeviceManager;
    AudioEngine*               audioEngine;
    int numTracks, numInputs, startTrackIndex = 0;

    juce::Label        ltcSlaveLabel;
    juce::ToggleButton ltcSlaveToggle;

    GridContainer                  gridContainer;
    juce::Viewport                 matrixViewport;
    juce::OwnedArray<juce::Label>  colLabels;
    juce::OwnedArray<juce::Label>  rowLabels;
    juce::OwnedArray<RoutingCell>  cells;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(RoutingMatrixTab)
};

//==============================================================================
// OutputRoutingTab — Output routing
//   Rows   = hardware output channels (labelled on the left)
//   Cols   = all tracks (T1…TN, CLK, LTC, MX L, MX R)
//   Rule   = multiple cells can be active in a column (fan-out routing)
//==============================================================================
class OutputRoutingTab : public juce::Component
{
public:
    OutputRoutingTab(Settings* settingsPtr, juce::AudioDeviceManager* deviceMgr,
                     AudioEngine* enginePtr, int trackCount = 8)
        : settings(settingsPtr), audioDeviceManager(deviceMgr),
          audioEngine(enginePtr), numTracks(trackCount), numOutputs(8)
    {
        try
        {
            matrixViewport.setViewedComponent(&gridContainer, false);
            matrixViewport.setScrollBarsShown(true, true);
            addAndMakeVisible(matrixViewport);

            updateOutputCount();
            buildColumnInfos();
            buildMatrix();
            loadRouting();
        }
        catch (const std::exception& e)
        {
            juce::Logger::writeToLog("OutputRoutingTab error: " + juce::String(e.what()));
        }
    }

    void updateOutputCount()
    {
        numOutputs = 8;
        if (audioDeviceManager)
            if (auto* dev = audioDeviceManager->getCurrentAudioDevice())
                numOutputs = juce::jmax(1, dev->getOutputChannelNames().size());
    }

    void setTrackCount(int count)
    {
        numTracks = count;
        updateOutputCount();
        cells.clear();
        rowLabels.clear();
        colLabels.clear();
        gridContainer.removeAllChildren();
        buildColumnInfos();
        buildMatrix();
        loadRouting();
        resized();
    }

    struct ColInfo { int settingsKey; juce::String label; juce::Colour colour; };
    std::vector<ColInfo> columnInfos;

    void buildColumnInfos()
    {
        columnInfos.clear();
        int firstUserIdx = audioEngine ? audioEngine->getFirstUserTrackIndex() : AudioEngine::kVirtualTrackCount;
        for (int t = 0; t < numTracks; ++t)
        {
            Track* tr = audioEngine ? audioEngine->getTrack(t) : nullptr;
            if (tr && tr->isMixdown())
            {
                columnInfos.push_back({ t,                      "MX L", juce::Colour(VintageLookAndFeel::accentGreen) });
                columnInfos.push_back({ t + kStereoRightOffset, "MX R", juce::Colour(0xff66cc88) });
            }
            else if (tr && tr->isClickTrack())
                columnInfos.push_back({ t, "CLK", juce::Colour(VintageLookAndFeel::accentAmber) });
            else if (tr && tr->isTimecodeTrack())
                columnInfos.push_back({ t, "LTC", juce::Colour(0xff6699ff) });
            else
            {
                // User track: label by user-facing number (engine index minus virtual offset)
                int userNum = t - firstUserIdx + 1;
                columnInfos.push_back({ t, "T" + juce::String(userNum), juce::Colour(VintageLookAndFeel::metalLight) });
            }
        }
    }

    void buildMatrix()
    {
        const int numCols = (int)columnInfos.size();

        for (int c = 0; c < numCols; ++c)
        {
            auto* lbl = new juce::Label();
            lbl->setText(columnInfos[c].label, juce::dontSendNotification);
            lbl->setColour(juce::Label::textColourId, columnInfos[c].colour);
            lbl->setFont(juce::Font(9.0f, juce::Font::bold));
            lbl->setJustificationType(juce::Justification::centred);
            gridContainer.addAndMakeVisible(lbl);
            colLabels.add(lbl);
        }

        for (int r = 0; r < numOutputs; ++r)
        {
            auto* rowLbl = new juce::Label();
            juce::String name = settings ? settings->getOutputChannelName(r) : ("OUT " + juce::String(r + 1));
            rowLbl->setText(name, juce::dontSendNotification);
            rowLbl->setFont(juce::Font(10.0f));
            rowLbl->setColour(juce::Label::textColourId, juce::Colour(0xffccbb99));
            rowLbl->setJustificationType(juce::Justification::centredRight);
            gridContainer.addAndMakeVisible(rowLbl);
            rowLabels.add(rowLbl);

            for (int c = 0; c < numCols; ++c)
            {
                auto* cell = new RoutingCell(columnInfos[c].colour);
                cell->onClicked = [this, r, c]() { handleCellClick(c, r); };
                gridContainer.addAndMakeVisible(cell);
                cells.add(cell);
            }
        }

        updateContainerSize();
        gridContainer.numRows    = numOutputs;
        gridContainer.numCols    = numCols;
        gridContainer.cellSize   = CELL_SIZE;
        gridContainer.labelWidth = LABEL_WIDTH;
        gridContainer.headerH    = HEADER_HEIGHT;
    }

    void paint(juce::Graphics& g) override { g.fillAll(juce::Colour(VintageLookAndFeel::bgTrack)); }

    void resized() override
    {
        matrixViewport.setBounds(getLocalBounds().reduced(8));
        updateContainerSize();
        layoutCells();
    }

    void updateContainerSize()
    {
        const int numCols = (int)columnInfos.size();
        gridContainer.setSize(LABEL_WIDTH + numCols * CELL_SIZE + 2,
                              HEADER_HEIGHT + numOutputs * CELL_SIZE + 2);
    }

    void layoutCells()
    {
        const int numCols = (int)columnInfos.size();
        for (int c = 0; c < colLabels.size(); ++c)
            colLabels[c]->setBounds(LABEL_WIDTH + c * CELL_SIZE, 0, CELL_SIZE, HEADER_HEIGHT);
        for (int r = 0; r < numOutputs; ++r)
        {
            if (r < rowLabels.size())
                rowLabels[r]->setBounds(2, HEADER_HEIGHT + r * CELL_SIZE, LABEL_WIDTH - 4, CELL_SIZE);
            for (int c = 0; c < numCols; ++c)
            {
                int idx = r * numCols + c;
                if (idx < cells.size())
                    cells[idx]->setBounds(LABEL_WIDTH + c * CELL_SIZE + 1,
                                          HEADER_HEIGHT + r * CELL_SIZE + 1,
                                          CELL_SIZE - 2, CELL_SIZE - 2);
            }
        }
    }

    void handleCellClick(int col, int row)
    {
        const int numCols = (int)columnInfos.size();
        if (col < 0 || col >= numCols) return;
        int idx = row * numCols + col;
        if (idx >= cells.size()) return;

        bool wasActive = cells[idx]->active;

        if (!wasActive)
        {
            // Activating this cell: enforce single-source per output row.
            // Clear any other track that was already routed to this output.
            // A track can still appear in multiple rows (one track → many outputs).
            for (int c = 0; c < numCols; ++c)
            {
                if (c == col) continue;
                int i = row * numCols + c;
                if (i < cells.size() && cells[i]->active)
                {
                    cells[i]->active = false;
                    cells[i]->repaint();
                    if (settings)
                        settings->setTrackOutputMapping(columnInfos[c].settingsKey, row, false);
                }
            }
        }

        // Toggle the clicked cell
        cells[idx]->active = !wasActive;
        cells[idx]->repaint();
        if (settings)
            settings->setTrackOutputMapping(columnInfos[col].settingsKey, row, cells[idx]->active);
    }

    void loadRouting()
    {
        if (!settings) return;
        const int numCols = (int)columnInfos.size();
        for (int c = 0; c < numCols; ++c)
            for (int r = 0; r < numOutputs; ++r)
            {
                int idx = r * numCols + c;
                if (idx < cells.size())
                    cells[idx]->active = settings->getTrackOutputMapping(columnInfos[c].settingsKey, r);
            }
        for (auto* cell : cells) cell->repaint();
    }

    void refreshRowLabels()
    {
        for (int r = 0; r < rowLabels.size() && r < numOutputs; ++r)
        {
            juce::String name = settings ? settings->getOutputChannelName(r) : ("OUT " + juce::String(r + 1));
            rowLabels[r]->setText(name, juce::dontSendNotification);
        }
    }

    void saveRouting() {}

private:
    static constexpr int CELL_SIZE     = 32;
    static constexpr int LABEL_WIDTH   = 100;
    static constexpr int HEADER_HEIGHT = 42;

    Settings*                  settings;
    juce::AudioDeviceManager*  audioDeviceManager;
    AudioEngine*               audioEngine;
    int numTracks, numOutputs;

    GridContainer                  gridContainer;
    juce::Viewport                 matrixViewport;
    juce::OwnedArray<juce::Label>  colLabels;
    juce::OwnedArray<juce::Label>  rowLabels;
    juce::OwnedArray<RoutingCell>  cells;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(OutputRoutingTab)
};
