// Hawthorne Link - streams audio inputs from this PC to Hawthorne on another PC.

#include <juce_audio_utils/juce_audio_utils.h>
#include "../Network/NetSender.h"
#include "../UI/HawthorneLookAndFeel.h"
#include "../UI/UIHelpers.h"

namespace hawthorne
{

//==============================================================================
class LinkComponent : public juce::Component,
                      private juce::AudioIODeviceCallback,
                      private juce::ChangeListener,
                      private juce::Timer
{
public:
    explicit LinkComponent (juce::ApplicationProperties& p) : props (p)
    {
        setLookAndFeel (&lnf);
        juce::LookAndFeel::setDefaultLookAndFeel (&lnf);

        auto* s = props.getUserSettings();
        sender.setRedundancy (s->getBoolValue ("redundancy", true));
        juce::String err;
        networkOk = sender.start (s->getValue ("name", juce::SystemStats::getComputerName()), err);

        nameEditor.setText (sender.getName(), juce::dontSendNotification);
        nameEditor.setTooltip ("How this PC appears in Hawthorne's NETWORK window");
        nameEditor.onTextChange = [this]
        {
            sender.setName (nameEditor.getText());
            props.getUserSettings()->setValue ("name", sender.getName());
        };
        addAndMakeVisible (nameEditor);

        redundancy.setToggleState (sender.getRedundancy(), juce::dontSendNotification);
        redundancy.setTooltip ("Every packet also carries the previous one, so a lost packet costs nothing (doubles the bandwidth)");
        redundancy.onClick = [this]
        {
            sender.setRedundancy (redundancy.getToggleState());
            props.getUserSettings()->setValue ("redundancy", redundancy.getToggleState());
            restartAudio();     // takes effect on a new session
        };
        addAndMakeVisible (redundancy);

        auto saved = s->getXmlValue ("audioDeviceState");
        deviceManager.initialise (8, 0, saved.get(), true);
        deviceManager.addChangeListener (this);
        deviceManager.addAudioCallback (this);

        selector = std::make_unique<juce::AudioDeviceSelectorComponent> (deviceManager, 1, net::maxChannels, 0, 0, false, false, false, false);
        addAndMakeVisible (*selector);

        startTimerHz (20);
        setSize (900, 640);
    }

    ~LinkComponent() override
    {
        deviceManager.removeAudioCallback (this);
        deviceManager.removeChangeListener (this);
        deviceManager.closeAudioDevice();
        sender.stop();
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
        setLookAndFeel (nullptr);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (Colours::bg);
        g.setColour (Colours::panel);
        g.fillRect (header);
        g.setColour (Colours::mustard);
        g.setFont (monoFont (20.0f, true));
        g.drawText ("H A W T H O R N E   L I N K", header.withTrimmedLeft (18), juce::Justification::centredLeft);
        g.setColour (Colours::textDim);
        g.setFont (monoFont (11.0f));
        g.drawText ("v" HAWTHORNE_VERSION, header.withTrimmedRight (18), juce::Justification::centredRight);

        g.setFont (monoFont (11.0f, true));
        g.drawText ("NAME", nameEditor.getBounds().translated (-60, 0).withWidth (56), juce::Justification::centredRight);

        // Status
        auto st = statusArea;
        g.setFont (monoFont (13.0f, true));
        auto subs = sender.getSubscribers();
        auto* dev = deviceManager.getCurrentAudioDevice();

        juce::String line1;
        juce::Colour c1 = Colours::text;
        if (! networkOk)                 { line1 = "NETWORK UNAVAILABLE - is another copy running?"; c1 = Colours::red; }
        else if (dev == nullptr)         { line1 = "NO AUDIO DEVICE - choose one below"; c1 = Colours::red; }
        else if (subs.empty())           { line1 = "READY - waiting for Hawthorne to connect"; c1 = Colours::mustard; }
        else                             { line1 = "STREAMING " + juce::String (numChannels.load()) + " ch to "
                                                   + juce::String ((int) subs.size()) + (subs.size() == 1 ? " receiver" : " receivers"); c1 = Colours::mustard; }
        g.setColour (c1);
        g.drawText (line1, st.removeFromTop (22), juce::Justification::centredLeft);

        g.setFont (monoFont (12.0f));
        g.setColour (Colours::textDim);
        juce::StringArray to;
        for (auto& s : subs) to.add (s.address);
        juce::String line2;
        if (dev != nullptr)
            line2 << dev->getTypeName() << ": " << dev->getName() << "   " << juce::String (dev->getCurrentSampleRate() / 1000.0, 1)
                  << " kHz   " << dev->getCurrentBufferSizeSamples() << " smp";
        if (! to.isEmpty()) line2 << "   ->  " << to.joinIntoString (", ");
        g.drawText (line2, st.removeFromTop (20), juce::Justification::centredLeft, true);

        juce::String line3 = "packets " + juce::String (sender.getPacketsSent());
        if (sender.getQueueOverruns() > 0) line3 << "   !! network couldn't keep up: " << sender.getQueueOverruns() << " packets dropped";
        line3 << "   port " << sender.getControlPort();
        g.setColour (sender.getQueueOverruns() > 0 ? Colours::red : Colours::textDim);
        g.drawText (line3, st.removeFromTop (20), juce::Justification::centredLeft, true);

        g.setColour (Colours::textDim);
        g.setFont (uiFont (12.0f));
        g.drawFittedText ("For bit-perfect transfer, link the two interfaces with word clock (this PC's interface following "
                          "Hawthorne's) and run both at the same sample rate. Otherwise Hawthorne corrects the drift.",
                          st.removeFromTop (34), juce::Justification::topLeft, 2);

        // Meters
        const int n = numChannels.load();
        if (n > 0)
        {
            const float w = (float) meterArea.getWidth() / (float) n;
            for (int ch = 0; ch < n; ++ch)
            {
                auto r = juce::Rectangle<float> ((float) meterArea.getX() + ch * w, (float) meterArea.getY(),
                                                 juce::jmax (2.0f, w - 2.0f), (float) meterArea.getHeight());
                g.setColour (Colours::black);
                g.fillRect (r);
                const float level = juce::jlimit (0.0f, 1.0f, (juce::Decibels::gainToDecibels (meters[(size_t) ch], -60.0f) + 60.0f) / 60.0f);
                g.setColour (meters[(size_t) ch] > 0.89f ? Colours::red : Colours::mustard);
                g.fillRect (r.withTrimmedTop (r.getHeight() * (1.0f - level)));
            }
        }
    }

    void resized() override
    {
        auto r = getLocalBounds();
        header = r.removeFromTop (48);
        r.reduce (16, 10);
        auto top = r.removeFromTop (28);
        top.removeFromLeft (60);
        nameEditor.setBounds (top.removeFromLeft (260));
        top.removeFromLeft (20);
        redundancy.setBounds (top.removeFromLeft (220));
        r.removeFromTop (10);
        statusArea = r.removeFromTop (100);
        meterArea = r.removeFromTop (60);
        r.removeFromTop (10);
        selector->setBounds (r);
    }

private:
    // Audio
    void audioDeviceAboutToStart (juce::AudioIODevice* d) override
    {
        const int ch = juce::jlimit (1, net::maxChannels, d->getActiveInputChannels().countNumberOfSetBits());
        numChannels = ch;
        sender.prepare (d->getCurrentSampleRate(), ch, d->getInputLatencyInSamples() + d->getCurrentBufferSizeSamples());
    }

    void audioDeviceStopped() override {}

    void audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut, int n,
                                           const juce::AudioIODeviceCallbackContext&) override
    {
        for (int o = 0; o < numOut; ++o)
            if (out[o] != nullptr) juce::FloatVectorOperations::clear (out[o], n);

        sender.push (in, numIn, n);

        for (int ch = 0; ch < juce::jmin (numIn, net::maxChannels); ++ch)
            if (in[ch] != nullptr)
            {
                const auto mm = juce::FloatVectorOperations::findMinAndMax (in[ch], n);
                const float p = juce::jmax (-mm.getStart(), mm.getEnd());
                if (p > peaks[(size_t) ch].load (std::memory_order_relaxed))
                    peaks[(size_t) ch].store (p, std::memory_order_relaxed);
            }
    }

    void restartAudio()
    {
        auto setup = deviceManager.getAudioDeviceSetup();
        deviceManager.setAudioDeviceSetup (setup, true);
        deviceManager.restartLastAudioDevice();
    }

    void changeListenerCallback (juce::ChangeBroadcaster*) override
    {
        if (auto xml = deviceManager.createStateXml())
        {
            props.getUserSettings()->setValue ("audioDeviceState", xml.get());
            props.saveIfNeeded();
        }
        repaint();
    }

    void timerCallback() override
    {
        for (size_t ch = 0; ch < meters.size(); ++ch)
        {
            const float p = peaks[ch].exchange (0.0f);
            meters[ch] = juce::jmax (p, meters[ch] * 0.85f);
        }
        repaint();
    }

    juce::ApplicationProperties& props;
    HawthorneLookAndFeel lnf;
    juce::AudioDeviceManager deviceManager;
    NetSender sender;
    bool networkOk = false;
    std::atomic<int> numChannels { 0 };
    std::array<std::atomic<float>, net::maxChannels> peaks {};
    std::array<float, net::maxChannels> meters {};

    juce::TextEditor nameEditor;
    juce::ToggleButton redundancy { "Redundant packets" };
    std::unique_ptr<juce::AudioDeviceSelectorComponent> selector;
    juce::Rectangle<int> header, statusArea, meterArea;
};

//==============================================================================
class LinkApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return "Hawthorne Link"; }
    const juce::String getApplicationVersion() override { return HAWTHORNE_VERSION; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String&) override
    {
        juce::PropertiesFile::Options o;
        o.applicationName = "Hawthorne Link";
        o.filenameSuffix = ".settings";
        o.folderName = "Hawthorne";
        o.osxLibrarySubFolder = "Application Support";
        props.setStorageParameters (o);

        window = std::make_unique<Window> (props);
    }

    void shutdown() override { window = nullptr; }
    void systemRequestedQuit() override { quit(); }

private:
    struct Window : juce::DocumentWindow
    {
        explicit Window (juce::ApplicationProperties& p)
            : juce::DocumentWindow ("Hawthorne Link", Colours::bg, juce::DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new LinkComponent (p), true);
            setResizable (true, false);
            setResizeLimits (640, 480, 4000, 3000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

    juce::ApplicationProperties props;
    std::unique_ptr<Window> window;
};

} // namespace hawthorne

START_JUCE_APPLICATION (hawthorne::LinkApplication)
