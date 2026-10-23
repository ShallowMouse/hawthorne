#include "TrackStrip.h"

namespace hawthorne
{

//==============================================================================
float LevelMeter::dbToFrac (float db)
{
    // -60 dB .. 0 dB, with more resolution near the top
    const float minDb = -60.0f;
    const float x = juce::jlimit (0.0f, 1.0f, (db - minDb) / -minDb);
    return std::pow (x, 1.6f);
}

void LevelMeter::pushPeak (float p)
{
    const float db = toDb (p);

    // Ballistics at ~30 Hz: instant attack, ~24 dB/s release.
    level = db > level ? db : juce::jmax (db, level - 0.8f);

    if (db >= holdLevel) { holdLevel = db; holdFrames = 45; }
    else if (--holdFrames <= 0) holdLevel = juce::jmax (level, holdLevel - 1.0f);

    if (p >= 0.999f)
        clipped = true;

    repaint();
}

void LevelMeter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    auto clipLight = r.removeFromTop (6.0f);
    r.removeFromTop (2.0f);

    g.setColour (clipped ? Colours::red : Colours::redDim);
    g.fillRoundedRectangle (clipLight, 1.5f);

    g.setColour (Colours::black);
    g.fillRoundedRectangle (r, 2.0f);

    auto inner = r.reduced (2.0f);
    const float h = inner.getHeight();

    // scale ticks
    g.setColour (Colours::edge);
    for (float db : { 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -36.0f, -48.0f })
    {
        const float y = inner.getBottom() - dbToFrac (db) * h;
        g.drawHorizontalLine ((int) y, inner.getX(), inner.getRight());
    }

    const float frac = dbToFrac (level);
    if (frac > 0.0f)
    {
        auto bar = inner.withTop (inner.getBottom() - frac * h);
        juce::ColourGradient grad (Colours::mustard.darker (0.3f), 0, inner.getBottom(),
                                   Colours::red, 0, inner.getY(), false);
        grad.addColour (dbToFrac (-12.0f), Colours::mustard);
        grad.addColour (dbToFrac (-3.0f), Colours::mustard.brighter (0.3f));
        g.setGradientFill (grad);
        g.fillRect (bar);
    }

    const float hf = dbToFrac (holdLevel);
    if (hf > 0.0f)
    {
        g.setColour (holdLevel > -1.0f ? Colours::red : Colours::text);
        g.fillRect (inner.getX(), inner.getBottom() - hf * h - 1.0f, inner.getWidth(), 2.0f);
    }
}

//==============================================================================
TrackStrip::TrackStrip (int i) : index (i)
{
    nameLabel.setFont (uiFont (11.0f, true));
    nameLabel.setJustificationType (juce::Justification::centred);
    nameLabel.setEditable (false, true, false);
    nameLabel.setTooltip ("Double-click to rename");
    nameLabel.onTextChange = [this]
    {
        auto n = nameLabel.getText().trim();
        if (n.isEmpty()) n = "Track " + juce::String (index + 1);
        nameLabel.setText (n, juce::dontSendNotification);
        if (onRename) onRename (index, n);
    };
    addAndMakeVisible (nameLabel);

    inputBox.setTooltip ("Input");
    inputBox.onChange = [this]
    {
        const int id = inputBox.getSelectedId();
        if (id > 0 && onInputChanged)
            onInputChanged (index, id - 2); // id 1 = none (-1)
    };
    addAndMakeVisible (inputBox);

    armButton.setClickingTogglesState (false);
    armButton.setColour (juce::TextButton::buttonOnColourId, Colours::red);
    armButton.setColour (juce::TextButton::textColourOnId, Colours::text);
    armButton.setTooltip ("Record arm");
    armButton.onClick = [this] { if (onArm) onArm (index, ! isArmed); };
    addAndMakeVisible (armButton);

    meter.setTooltip ("Click to reset the clip light");
    addAndMakeVisible (meter);
}

void TrackStrip::setTrackName (const juce::String& n)
{
    if (! nameLabel.isBeingEdited())
        nameLabel.setText (n, juce::dontSendNotification);
}

void TrackStrip::setInputChoices (const juce::StringArray& names, const juce::BigInteger& active, int selected)
{
    inputBox.clear (juce::dontSendNotification);
    inputBox.addItem ("--", 1);

    for (int ch = 0; ch < names.size(); ++ch)
    {
        inputBox.addItem ("In " + juce::String (ch + 1), ch + 2);
        inputBox.setItemEnabled (ch + 2, active[ch]);
    }

    if (selected >= names.size())
        inputBox.addItem ("In " + juce::String (selected + 1) + "?", selected + 2);

    inputBox.setSelectedId (selected + 2, juce::dontSendNotification);

    const bool ok = selected >= 0 && selected < names.size() && active[selected];
    inputBox.setColour (juce::ComboBox::textColourId, ok ? Colours::text : Colours::red);
    inputBox.setTooltip (ok ? names[selected] : (selected < 0 ? "No input" : "Input not active - enable it in Audio Settings"));
}

void TrackStrip::setArmed (bool armed, bool recording, bool flashPhase)
{
    isArmed = armed;
    const bool lit = armed && (! recording ? flashPhase : true);
    armButton.setToggleState (lit, juce::dontSendNotification);
}

void TrackStrip::setTapeSeconds (int secs)
{
    if (secs != tapeSeconds) { tapeSeconds = secs; repaint(); }
}

void TrackStrip::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (1.0f, 0.0f);
    g.setColour (isArmed ? Colours::redDim.withAlpha (0.55f) : Colours::panel);
    g.fillRoundedRectangle (r, 3.0f);

    g.setColour (Colours::mustard);
    g.setFont (monoFont (17.0f, true));
    g.drawText (juce::String (index + 1).paddedLeft ('0', 2), getLocalBounds().removeFromTop (24),
                juce::Justification::centred);

    g.setColour (Colours::textDim);
    g.setFont (monoFont (10.0f));
    g.drawText (tapeSeconds <= 0 ? juce::String ("blank")
                                 : juce::String (tapeSeconds / 60) + ":" + juce::String (tapeSeconds % 60).paddedLeft ('0', 2),
                getLocalBounds().removeFromBottom (16), juce::Justification::centred);
}

void TrackStrip::resized()
{
    auto r = getLocalBounds().reduced (4, 0);
    r.removeFromTop (24);
    nameLabel.setBounds (r.removeFromTop (18));
    r.removeFromTop (4);
    inputBox.setBounds (r.removeFromTop (22));
    r.removeFromTop (6);
    armButton.setBounds (r.removeFromTop (26));
    r.removeFromTop (8);
    r.removeFromBottom (16);
    meter.setBounds (r.withSizeKeepingCentre (juce::jmin (r.getWidth(), 22), r.getHeight()));
}

} // namespace hawthorne
