#include "NetworkWindow.h"
#include "UIHelpers.h"

namespace hawthorne
{

NetworkPanel::NetworkPanel (NetReceiver& r) : rx (r)
{
    addButton.setTooltip ("A sender on another subnet or VLAN won't be found automatically - add its IP address here.");
    addButton.onClick = [this] { addByAddress(); };
    addButton.setWantsKeyboardFocus (false);
    addAndMakeVisible (addButton);

    latency.setRange (1.0, 100.0, 0.5);
    latency.setValue (rx.getTargetLatencyMs(), juce::dontSendNotification);
    latency.setTextValueSuffix (" ms");
    latency.setTooltip ("Smallest network buffer. Hawthorne measures each sender and uses the smallest buffer that won't drop out "
                        "(it grows by itself if needed). Raise this only for extra safety.");
    latency.onValueChange = [this] { rx.setTargetLatencyMs (latency.getValue()); };
    latency.onDragEnd = [this] { if (onChanged) onChanged(); };
    latency.setWantsKeyboardFocus (false);
    addAndMakeVisible (latency);

    startTimerHz (4);
    rebuildRows();
}

void NetworkPanel::addByAddress()
{
    auto* w = new juce::AlertWindow ("Add sender by address", "IP address of the PC running Hawthorne Link:",
                                     juce::MessageBoxIconType::NoIcon, this);
    w->addTextEditor ("ip", "", "Address:");
    w->addButton ("Add", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int r)
    {
        const auto ip = w->getTextEditorContents ("ip").trim();
        if (r != 1 || ip.isEmpty()) return;
        auto list = rx.getManualAddresses();
        list.addIfNotAlreadyThere (ip);
        rx.setManualAddresses (list);
        if (onChanged) onChanged();
    }), true);
}

void NetworkPanel::rebuildRows()
{
    // Connected senders first (even if not currently seen), then everything discovered.
    std::vector<Row> fresh;
    juce::StringArray names;

    const auto senders = rx.getSenders();
    auto describeSender = [] (const NetReceiver::SenderInfo& s)
    {
        return s.address + "   " + (s.channels > 0 ? juce::String (s.channels) + " ch   " + juce::String (s.sampleRate / 1000.0, 1) + " kHz"
                                                     : juce::String ("no audio device yet"));
    };

    for (int slot = 0; slot < NetReceiver::maxSlots; ++slot)
    {
        const auto name = rx.getSlotName (slot);
        if (name.isEmpty()) continue;
        Row row;
        row.name = name;
        row.connected = true;
        row.slot = slot;
        for (auto& s : senders)
            if (s.name == name) { row.seen = true; row.detail = describeSender (s); }
        if (! row.seen) row.detail = "not seen on the network";
        names.add (name);
        fresh.push_back (std::move (row));
    }

    for (auto& s : senders)
    {
        if (names.contains (s.name)) continue;
        Row row;
        row.name = s.name;
        row.seen = true;
        row.detail = describeSender (s);
        names.add (s.name);
        fresh.push_back (std::move (row));
    }

    juce::String key;
    for (auto& r : fresh) key << r.name << (r.connected ? "*" : "") << "|";

    if (key != rowsKey)
    {
        rowsKey = key;
        for (auto& r : rows) removeChildComponent (r.button.get());
        rows = std::move (fresh);

        for (auto& r : rows)
        {
            r.button = std::make_unique<juce::TextButton> (r.connected ? "DISCONNECT" : "CONNECT");
            r.button->setWantsKeyboardFocus (false);
            if (! r.connected)
            {
                r.button->setColour (juce::TextButton::buttonColourId, Colours::mustard);
                r.button->setColour (juce::TextButton::textColourOffId, Colours::black);
            }
            const auto name = r.name;
            const int slot = r.slot;
            r.button->onClick = [this, name, slot]
            {
                if (slot >= 0)
                    rx.disconnect (slot);
                else if (const int free = rx.firstFreeSlot(); free >= 0)
                    rx.connect (free, name);
                else
                    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::InfoIcon, "Network",
                                                            "Up to " + juce::String (NetReceiver::maxSlots) + " senders can be connected at once.");
                rowsKey.clear();
                rebuildRows();
                if (onChanged) onChanged();
            };
            addAndMakeVisible (r.button.get());
        }
        resized();
    }
    else
    {
        for (size_t i = 0; i < rows.size(); ++i)
        {
            rows[i].detail = fresh[i].detail;
            rows[i].seen = fresh[i].seen;
        }
    }
}

void NetworkPanel::timerCallback()
{
    rebuildRows();
    repaint();
}

void NetworkPanel::paint (juce::Graphics& g)
{
    g.fillAll (Colours::panel);

    g.setColour (Colours::mustard);
    g.setFont (monoFont (15.0f, true));
    g.drawText ("HAWTHORNE LINK", 16, 12, 300, 24, juce::Justification::centredLeft);

    g.setColour (Colours::textDim);
    g.setFont (uiFont (13.0f));
    g.drawFittedText (rx.isRunning()
                          ? "Run Hawthorne Link on the other PC. Senders on this network appear below; connect one and its "
                            "channels appear as NET inputs in ROUTING. Link both MOTUs with word clock for bit-perfect transfer."
                          : "The network couldn't be opened (is another copy of Hawthorne running?).",
                      helpArea, juce::Justification::topLeft, 3);

    g.setFont (monoFont (11.0f, true));
    g.drawText ("MIN BUFFER", latency.getX() - 90, latency.getY(), 84, latency.getHeight(), juce::Justification::centredRight);

    // Senders
    g.setColour (Colours::mustard);
    g.setFont (monoFont (12.0f, true));
    g.drawText ("SENDERS", listArea.getX(), listArea.getY() - 22, 200, 20, juce::Justification::centredLeft);

    if (rows.empty())
    {
        g.setColour (Colours::textDim);
        g.setFont (uiFont (13.0f));
        g.drawText ("Looking for senders...", listArea.withHeight (30), juce::Justification::centredLeft);
    }

    for (size_t i = 0; i < rows.size(); ++i)
    {
        auto& r = rows[i];
        auto rr = juce::Rectangle<int> (listArea.getX(), listArea.getY() + (int) i * 40, listArea.getWidth(), 36);
        g.setColour (r.connected ? Colours::mustardDim.withAlpha (0.25f) : Colours::black.withAlpha (0.3f));
        g.fillRoundedRectangle (rr.toFloat(), 4.0f);
        auto text = rr.reduced (12, 0).withTrimmedRight (150);
        g.setColour (r.connected ? Colours::mustard : Colours::text);
        g.setFont (uiFont (14.0f, true));
        g.drawText ((r.connected ? "NET " + juce::String (r.slot + 1) + "   " : juce::String()) + r.name,
                    text.removeFromLeft (text.getWidth() / 2), juce::Justification::centredLeft, true);
        g.setColour (r.seen ? Colours::textDim : Colours::red);
        g.setFont (monoFont (12.0f));
        g.drawText (r.detail, text, juce::Justification::centredLeft, true);
    }

    // Connection status
    g.setColour (Colours::mustard);
    g.setFont (monoFont (12.0f, true));
    g.drawText ("CONNECTIONS", statusArea.getX(), statusArea.getY(), 200, 20, juce::Justification::centredLeft);

    int y = statusArea.getY() + 24;
    for (int slot = 0; slot < NetReceiver::maxSlots; ++slot)
    {
        const auto st = rx.getStatus (slot);
        if (st.state == NetReceiver::State::off) continue;

        juce::String line = "NET " + juce::String (slot + 1) + "  " + st.name.paddedRight (' ', 18).substring (0, 18) + "  "
                          + NetReceiver::describe (st.state);
        if (st.state == NetReceiver::State::corrected || st.state == NetReceiver::State::syncing)
            line << "  (" << (st.ppm >= 0 ? "+" : "") << juce::String (st.ppm, 1) << " ppm)";
        if (st.state == NetReceiver::State::rateMismatch)
            line << "  sender " << juce::String (st.senderRate / 1000.0, 1) << " kHz";
        if (st.state == NetReceiver::State::locked || st.state == NetReceiver::State::corrected || st.state == NetReceiver::State::syncing)
            line << "   " << st.channels << " ch   buffer " << juce::String (st.bufferMs, 1) << " ms (auto)"
                 << "   delay " << juce::String (st.senderLatencyMs + st.bufferMs, 1) << " ms";
        line << "   dropouts " << juce::String (st.dropouts);
        if (rx.isSyncing (slot))
            line << "   TAPE SYNC";

        const bool bad = st.state == NetReceiver::State::rateMismatch || st.dropouts > 0;
        g.setColour (st.state == NetReceiver::State::locked ? Colours::mustard : (bad ? Colours::red : Colours::text));
        g.setFont (monoFont (12.0f, st.state == NetReceiver::State::locked));
        g.drawText (line, statusArea.getX(), y, statusArea.getWidth(), 20, juce::Justification::centredLeft, true);
        y += 22;
    }

    if (y == statusArea.getY() + 24)
    {
        g.setColour (Colours::textDim);
        g.setFont (uiFont (13.0f));
        g.drawText ("Nothing connected.", statusArea.getX(), y, 300, 20, juce::Justification::centredLeft);
    }

    g.setColour (Colours::textDim);
    g.setFont (monoFont (11.0f));
    g.drawText ("listening on port " + juce::String (rx.getDataPort()), getLocalBounds().reduced (16, 8).removeFromBottom (16),
                juce::Justification::centredRight);
}

void NetworkPanel::resized()
{
    auto r = getLocalBounds().reduced (16, 12);
    auto top = r.removeFromTop (28);
    addButton.setBounds (top.removeFromRight (170));
    top.removeFromRight (16);
    latency.setBounds (top.removeFromRight (260));

    helpArea = r.removeFromTop (44).withTrimmedTop (6);
    r.removeFromTop (30);

    statusArea = r.removeFromBottom (24 + 22 * NetReceiver::maxSlots + 20);
    listArea = r;

    for (size_t i = 0; i < rows.size(); ++i)
        if (rows[i].button != nullptr)
            rows[i].button->setBounds (listArea.getRight() - 140, listArea.getY() + (int) i * 40 + 5, 130, 26);
}

} // namespace hawthorne
