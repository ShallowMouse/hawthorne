#pragma once

#include "Layout.h"

namespace hawthorne
{

class MainComponent;

/** Blocks for the displays (LAYOUT): "meters", "time", "transport", "varispeed", "tempo",
    "master", "locator", "markers". Live: they work the deck just as the main window does. */
std::unique_ptr<Panel> createDeckBlock (MainComponent&, const juce::String& type);

} // namespace hawthorne
