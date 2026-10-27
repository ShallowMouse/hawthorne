#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include "../Route/RouteShared.h"

namespace hawthorne
{

//==============================================================================
/**
    Hawthorne's side of Hawthorne Route (see Source/Route/RouteShared.h): another
    program plays into the "Hawthorne Route" ASIO driver and its outputs arrive
    here as inputs ROUTE 1-32; Hawthorne's master mix goes back to it. Hawthorne's
    audio callback is the clock that runs the other program.

    Audio thread: beginBlock() (read what arrived), channel(), endBlock() (send the
    master, advance the clock). Windows only; elsewhere it does nothing.
*/
class RouteHost
{
public:
    RouteHost() = default;
    ~RouteHost() { close(); }
    RouteHost (const RouteHost&) = delete;

    void open (double sampleRate, int maxBlockSize);     // (RouteHost.cpp: the Windows shared memory)
    void close();

    /** Is a program playing into the driver? */
    bool isConnected() const noexcept
    {
        return shared != nullptr && shared->driverRunning.load() != 0;
    }

    juce::String getClientName() const
    {
        return "Hawthorne Route";
    }

    /** The virtual ASIO driver: is it registered (so other programs can pick it)? */
    static bool isDriverInstalled();
    /** Put the bundled driver in place and register it (asks Windows for permission). Returns an error, or empty. */
    static juce::String installDriver();
    static juce::String uninstallDriver();

    /** The extra delay of ROUTE inputs (the other program's block + ours), in samples. */
    int getLatency() const noexcept { return latency; }

    //--------------------------------------------------------------------------
    // Audio thread
    void beginBlock (int n) noexcept
    {
        live = false;
        if (shared == nullptr || n > bufs.getNumSamples()) return;
        if (shared->driverRunning.load (std::memory_order_relaxed) == 0) { readPos = -1; return; }
        const auto w = shared->toWrite.load (std::memory_order_acquire);
        const auto client = shared->driverClientId.load (std::memory_order_relaxed);
        const int theirBlock = juce::jmax (16, (int) shared->driverBlock.load (std::memory_order_relaxed));
        const juce::int64 target = theirBlock + n + 32;
        // (re)lock: a new program, or we've drifted out of the window
        if (readPos < 0 || client != clientId || w - readPos > (juce::int64) route::ringFrames / 2 || w - readPos < 0)
        {
            readPos = w - target;
            clientId = client;
        }
        latency = (int) target;
        for (int ch = 0; ch < route::toHawthorneChannels; ++ch)
        {
            float* d = bufs.getWritePointer (ch);
            const float* ring = shared->toHawthorne[ch];
            for (int k = 0; k < n; ++k)
            {
                const auto p = readPos + k;
                d[k] = p < w ? ring[p & route::ringMask] : 0.0f;
            }
        }
        readPos += n;
        live = true;
    }

    /** One ROUTE input for this block (nullptr if nothing is connected). */
    const float* channel (int ch) const noexcept
    {
        return live && juce::isPositiveAndBelow (ch, route::toHawthorneChannels) ? bufs.getReadPointer (ch) : nullptr;
    }

    /** Send the master mix back and advance the clock that runs the other program. */
    void endBlock (const float* masterL, const float* masterR, int n) noexcept
    {
        if (shared == nullptr) return;
        const auto w = shared->fromWrite.load (std::memory_order_relaxed);
        const float* src[2] = { masterL, masterR };
        for (int ch = 0; ch < route::fromHawthorneChannels; ++ch)
        {
            float* ring = shared->fromHawthorne[ch];
            for (int k = 0; k < n; ++k)
                ring[(w + k) & route::ringMask] = src[ch] != nullptr ? src[ch][k] : 0.0f;
        }
        shared->fromWrite.store (w + n, std::memory_order_release);
        shared->clock.store (shared->clock.load (std::memory_order_relaxed) + n, std::memory_order_release);
        signal();
    }

private:
    void signal() noexcept;             // heartbeat + wake the driver
    void* mapping = nullptr;            // (Windows handles)
    void* tick = nullptr;
    route::Shared* shared = nullptr;
    juce::AudioBuffer<float> bufs;
    juce::int64 readPos = -1, clientId = -1;
    int latency = 0;
    bool live = false;
};

} // namespace hawthorne
