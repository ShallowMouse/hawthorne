#include "Mtc.h"

namespace hawthorne
{

namespace
{
    int mtcRateCode (timecode::Rate r)
    {
        switch (r)
        {
            case timecode::Rate::fps24:       return 0;
            case timecode::Rate::fps25:       return 1;
            case timecode::Rate::fps2997drop: return 2;
            case timecode::Rate::fps2997:
            case timecode::Rate::fps30:       return 3;
        }
        return 1;
    }

    timecode::Rate rateFromCode (int code, timecode::Rate preferred)
    {
        switch (code & 3)
        {
            case 0: return timecode::Rate::fps24;
            case 1: return timecode::Rate::fps25;
            case 2: return timecode::Rate::fps2997drop;
            default: return preferred == timecode::Rate::fps2997 ? timecode::Rate::fps2997 : timecode::Rate::fps30;
        }
    }
}

//==============================================================================
juce::MidiMessage MtcOutput::quarterFrame (int piece, const timecode::Time& t, timecode::Rate r)
{
    int v = 0;
    switch (piece)
    {
        case 0: v = t.frames & 0xf;          break;
        case 1: v = (t.frames >> 4) & 1;     break;
        case 2: v = t.seconds & 0xf;         break;
        case 3: v = (t.seconds >> 4) & 3;    break;
        case 4: v = t.minutes & 0xf;         break;
        case 5: v = (t.minutes >> 4) & 3;    break;
        case 6: v = t.hours & 0xf;           break;
        default: v = ((t.hours >> 4) & 1) | (mtcRateCode (r) << 1); break;
    }
    return juce::MidiMessage::quarterFrame (piece, v);
}

juce::MidiMessage MtcOutput::fullFrame (const timecode::Time& t, timecode::Rate r)
{
    const uint8_t data[] = { 0x7f, 0x7f, 0x01, 0x01, (uint8_t) ((mtcRateCode (r) << 5) | t.hours),
                             (uint8_t) t.minutes, (uint8_t) t.seconds, (uint8_t) t.frames };
    return juce::MidiMessage::createSysExMessage (data, (int) sizeof (data));
}

bool MtcOutput::open (const juce::String& id)
{
    close();
    auto out = juce::MidiOutput::openDevice (id);
    if (out == nullptr)
        return false;
    {
        const juce::ScopedLock sl (lock);
        output = std::move (out);
        wasRolling = false;
        lastStoppedFrame = -1.0;
    }
    startTimer (1);
    return true;
}

void MtcOutput::close()
{
    stopTimer();
    const juce::ScopedLock sl (lock);
    output.reset();
}

void MtcOutput::send (const juce::MidiMessage& m)
{
    if (output != nullptr)
        output->sendMessageNow (m);
}

void MtcOutput::hiResTimerCallback()
{
    const juce::ScopedLock sl (lock);
    if (output == nullptr)
        return;

    const auto snap = engine.getTapeSnapshot();
    const auto rate = engine.getTimecodeRate();
    const double fps = timecode::fps (rate);
    auto framesAt = [&] (double tape) { return (tape / snap.sampleRate + engine.getTimecodeStart()) * fps; };

    if (! snap.rolling)
    {
        const double f = std::floor (framesAt (snap.tapePos));
        if (wasRolling || f != lastStoppedFrame)
        {
            send (fullFrame (timecode::fromFrames ((juce::int64) f, rate), rate));   // where we stopped / located
            lastStoppedFrame = f;
        }
        wasRolling = false;
        return;
    }

    const double tape = snap.tapePos + (juce::Time::getMillisecondCounterHiRes() - snap.atMs) * snap.sampleRate / 1000.0 * snap.rate;
    const auto q = (juce::int64) std::floor (framesAt (tape) * 4.0);

    if (! wasRolling || q < lastQ || q - lastQ > 16)
    {
        // start / jump: tell the receiver where we are, then quarter frames from a whole frame pair
        send (fullFrame (timecode::fromFrames (q / 4, rate), rate));
        lastQ = q - (((q % 8) + 8) % 8) - 1;
        wasRolling = true;
    }

    while (lastQ < q)
    {
        ++lastQ;
        const int piece = (int) (((lastQ % 8) + 8) % 8);
        const auto frame0 = (lastQ - piece) / 4;     // the (even) frame this group of eight describes
        send (quarterFrame (piece, timecode::fromFrames (frame0, rate), rate));
    }
    lastStoppedFrame = -1.0;
}

//==============================================================================
bool MtcInput::open (const juce::String& id)
{
    close();
    input = juce::MidiInput::openDevice (id, this);
    if (input == nullptr)
        return false;
    seen = 0;
    expect = 0;
    input->start();
    return true;
}

void MtcInput::close()
{
    if (input != nullptr)
        input->stop();
    input.reset();
}

void MtcInput::handle (const juce::MidiMessage& m, double atMs)
{
    const auto preferred = engine.getTimecodeRate();

    if (m.isQuarterFrame())
    {
        const int piece = m.getQuarterFrameSequenceNumber();
        if (piece == expect)  ++seen;                  // consecutive pieces so far
        else if (piece == 0)  seen = 1;
        else                  { seen = 0; expect = 0; return; }
        nibbles[(size_t) piece] = m.getQuarterFrameValue();
        expect = (piece + 1) % 8;
        if (piece != 7 || seen < 8)
            return;

        timecode::Time t;
        t.frames  = nibbles[0] | ((nibbles[1] & 1) << 4);
        t.seconds = nibbles[2] | ((nibbles[3] & 3) << 4);
        t.minutes = nibbles[4] | ((nibbles[5] & 3) << 4);
        t.hours   = nibbles[6] | ((nibbles[7] & 1) << 4);
        const auto r = rateFromCode (nibbles[7] >> 1, preferred);
        // the eight pieces describe the frame where piece 0 was sent; piece 7 comes 1 3/4 frames later
        const double frames = (double) timecode::toFrames (t, r) + 1.75;
        engine.pushTimecode (frames / timecode::fps (r), atMs);
        return;
    }

    if (m.isSysEx() && m.getSysExDataSize() >= 8)
    {
        const auto* d = m.getSysExData();
        if (d[0] == 0x7f && d[2] == 0x01 && d[3] == 0x01)
        {
            timecode::Time t { d[4] & 0x1f, d[5] & 0x3f, d[6] & 0x3f, d[7] & 0x1f };
            const auto r = rateFromCode (d[4] >> 5, preferred);
            engine.pushTimecode ((double) timecode::toFrames (t, r) / timecode::fps (r), atMs);
            seen = 0;
            expect = 0;
        }
    }
}

} // namespace hawthorne
