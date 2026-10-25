#pragma once

#include "TapeEngine.h"

namespace hawthorne
{

//==============================================================================
/** MIDI Time Code out: quarter-frame messages following the tape (and a
    full-frame message on every locate / stop), on a 1 ms high-resolution timer. */
class MtcOutput : private juce::HighResolutionTimer
{
public:
    explicit MtcOutput (TapeEngine& e) : engine (e) {}
    ~MtcOutput() override { close(); }

    bool open (const juce::String& deviceIdentifier);
    void close();
    bool isOpen() const noexcept { return output != nullptr; }

    /** The messages for one position (for tests). piece 0-7, or -1 for a full-frame message. */
    static juce::MidiMessage quarterFrame (int piece, const timecode::Time&, timecode::Rate);
    static juce::MidiMessage fullFrame (const timecode::Time&, timecode::Rate);

private:
    void hiResTimerCallback() override;
    void send (const juce::MidiMessage&);

    TapeEngine& engine;
    std::unique_ptr<juce::MidiOutput> output;
    juce::CriticalSection lock;
    bool wasRolling = false;
    juce::int64 lastQ = 0;
    double lastStoppedFrame = -1.0;
};

//==============================================================================
/** MIDI Time Code in: quarter-frame and full-frame messages -> TapeEngine::pushTimecode. */
class MtcInput : private juce::MidiInputCallback
{
public:
    explicit MtcInput (TapeEngine& e) : engine (e) {}
    ~MtcInput() override { close(); }

    bool open (const juce::String& deviceIdentifier);
    void close();
    bool isOpen() const noexcept { return input != nullptr; }

    /** Feed a message directly (tests). atMs on the hi-res millisecond clock. */
    void handle (const juce::MidiMessage&, double atMs);

private:
    void handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m) override { handle (m, m.getTimeStamp() * 1000.0); }

    TapeEngine& engine;
    std::unique_ptr<juce::MidiInput> input;
    std::array<int, 8> nibbles {};
    int seen = 0, expect = 0;
};

} // namespace hawthorne
