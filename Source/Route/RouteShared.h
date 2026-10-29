#pragma once

// Hawthorne Route: a virtual ASIO driver ("Hawthorne Route") that other programs
// - Reaper, a synth, anything with ASIO - can pick as their audio device. What they
// play out of it arrives in Hawthorne as inputs ROUTE 1-96; back on the driver's
// inputs they get every Hawthorne track (1-96: what each track's meter shows), the
// master (97-98) and LTC of the tape position (99).
//
// The two sides meet in shared memory. Hawthorne's audio callback is the clock: it
// advances a frame counter and signals an event, and the driver runs the other
// program's ASIO callback from that, so both run sample-synchronously - no drift,
// no resampling. When Hawthorne isn't running, the driver keeps time by itself
// (silence) so the other program doesn't stall.
//
// Shared by Hawthorne (Source/Engine) and the driver (Source/RouteDriver). Plain C++.

#include <atomic>
#include <cstdint>

namespace hawthorne::route
{
    // v2: two-way - every Hawthorne track, the master and LTC back to the other program.
    constexpr std::uint32_t magic   = 0x48525432;   // "HRT2"
    constexpr std::uint32_t version = 2;
    constexpr int toHawthorneChannels   = 96;       // the other program's outputs -> Hawthorne inputs ROUTE 1-96
    constexpr int trackChannels         = 96;       // Hawthorne's tracks (what each meter shows) -> the other program's inputs 1-96
    constexpr int masterLeft = trackChannels, masterRight = trackChannels + 1, ltcChannel = trackChannels + 2;
    constexpr int fromHawthorneChannels = trackChannels + 3;   // + master L / R + LTC
    constexpr int ringFrames = 1 << 15;             // 32768 frames per channel (power of two)
    constexpr int ringMask   = ringFrames - 1;

    constexpr const wchar_t* mappingName = L"Local\\HawthorneRouteV2";
    constexpr const wchar_t* tickName    = L"Local\\HawthorneRouteTickV2";

    struct Shared
    {
        std::uint32_t magic, version;
        // Hawthorne side
        std::atomic<std::int32_t> sampleRate;        // Hawthorne's device rate (0 = Hawthorne not running)
        std::atomic<std::int64_t> clock;             // frames Hawthorne's device has run (drives the driver)
        std::atomic<std::int64_t> heartbeatMs;       // Hawthorne's last callback (ms, monotonic) - to tell if it's alive
        std::atomic<std::int64_t> fromWrite;         // frames Hawthorne has written into fromHawthorne
        // driver side
        std::atomic<std::int32_t> driverRunning;     // a program has the driver started
        std::atomic<std::int32_t> driverBlock;       // its buffer size
        std::atomic<std::int64_t> toWrite;           // frames the driver has written into toHawthorne
        std::atomic<std::int64_t> driverClientId;    // changes when a program (re)starts the driver
        char clientName[64];                         // the program's name, for Hawthorne's routing matrix

        float toHawthorne[toHawthorneChannels][ringFrames];
        float fromHawthorne[fromHawthorneChannels][ringFrames];
    };

    static_assert (std::atomic<std::int64_t>::is_always_lock_free, "shared-memory atomics must be lock-free");

    /** Absolute input ids for Hawthorne's routing (beside device inputs and the network's 1000+). */
    constexpr int inputBase = 3000;
    inline bool isRouteInput (int id) noexcept { return id >= inputBase && id < inputBase + toHawthorneChannels; }
    inline int  channelOf (int id) noexcept    { return id - inputBase; }
}
