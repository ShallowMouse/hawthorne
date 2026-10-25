#pragma once

#include "NetProtocol.h"
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

namespace hawthorne
{

//==============================================================================
/**
    Hawthorne Link receiver: up to four senders on the network, each appearing
    in Hawthorne as a block of extra inputs.

    Clocking. Each sender's frames are stamped with its sample clock. The
    receiver keeps a small buffer (target latency) and steers its read rate so
    the buffer stays put:
      - if the two machines share a word clock the rates match exactly; after
        ~25 s that is detected and the stream is LOCKED: samples are copied
        straight through, bit-perfect, never resampled.
      - otherwise the stream is DRIFT CORRECTED with the varispeed sinc
        resampler (error < -100 dB), so it never clicks, drops or drifts.

    Threads: network threads write the per-sender ring buffers; the audio
    thread reads them (lock-free; every frame is stamped so a late, lost or
    stale frame is never played as garbage).
*/
class NetReceiver
{
public:
    static constexpr int maxSlots  = 4;
    static constexpr int inputBase = 1000;     // absolute input id = inputBase + slot * net::maxChannels + channel
    static constexpr int maxBlock  = 256;      // process() in chunks of at most this
    static constexpr int ringFrames = 1 << 15;

    static bool isNetworkInput (int id) noexcept { return id >= inputBase && id < inputBase + maxSlots * net::maxChannels; }
    static int slotOf (int id) noexcept          { return (id - inputBase) / net::maxChannels; }
    static int channelOf (int id) noexcept       { return (id - inputBase) % net::maxChannels; }
    static int inputId (int slot, int ch) noexcept { return inputBase + slot * net::maxChannels + ch; }

    NetReceiver();
    ~NetReceiver();

    //==========================================================================
    // Message thread
    bool start (juce::String& error);          // opens the sockets, starts listening
    void stop();
    bool isRunning() const noexcept { return running.load(); }
    int  getDataPort() const noexcept { return boundDataPort.load(); }

    struct SenderInfo
    {
        uint32_t senderId = 0;
        juce::String name, address;
        int controlPort = 0, channels = 0;
        double sampleRate = 0.0;
        juce::uint32 lastSeenMs = 0;
    };
    std::vector<SenderInfo> getSenders() const;

    /** Senders on another subnet / VLAN: asked for directly. */
    void setManualAddresses (const juce::StringArray& ips);
    juce::StringArray getManualAddresses() const;

    /** Subscribes a slot to a sender, by name (survives the sender restarting). */
    void connect (int slot, const juce::String& senderName);
    void disconnect (int slot);
    juce::String getSlotName (int slot) const;
    int findSlot (const juce::String& senderName) const;
    int firstFreeSlot() const;

    enum class State { off, searching, waiting, rateMismatch, syncing, corrected, locked };
    struct Status
    {
        State state = State::off;
        juce::String name;
        int channels = 0;
        double senderRate = 0.0, ppm = 0.0, bufferMs = 0.0, targetMs = 0.0;
        double senderLatencyMs = 0.0;     // the sender's own input latency + audio buffer
        juce::int64 dropouts = 0, packets = 0;
    };
    Status getStatus (int slot) const;
    static juce::String describe (State);

    /** The smallest network buffer to use; it grows by itself if the stream needs more. */
    void setTargetLatencyMs (double ms) noexcept { targetLatencyMs = juce::jlimit (1.0, 200.0, ms); }
    double getTargetLatencyMs() const noexcept   { return targetLatencyMs.load(); }

    //==========================================================================
    // Audio thread
    void prepare (double sampleRate) noexcept;  // before process(); resets the clock loops
    void process (int numFrames) noexcept;      // numFrames <= maxBlock
    /** One block of a channel after process(); nullptr when that slot has no live audio. */
    const float* getChannel (int slot, int ch) const noexcept;
    bool isLive (int slot) const noexcept;

    //==========================================================================
    // Network thread (public for tests)
    void handleDatagram (const void* data, int size, const juce::String& fromAddress) noexcept;
    /** Tests (audio thread, after process): sender-frame position of the block's first sample and the rate used. */
    void getReadPosition (int slot, double& pos0, double& rate) const noexcept;
    /** Tests: subscribe a slot to a sender id directly, without discovery. */
    void connectToSenderId (int slot, uint32_t senderId, const juce::String& name);

private:
    struct Slot;
    void handleAudio (const net::PacketHeader&, const uint8_t* payload) noexcept;
    void handleControl (const juce::String& msg, const juce::String& fromAddress);
    void writeBlock (Slot&, uint32_t session, juce::uint64 first, int frames, int channels, const uint8_t* pcm) noexcept;
    void processSlot (Slot&, int n) noexcept;
    void ensureChecked (Slot&, juce::int64 upTo) noexcept;
    void networkLoop();
    void controlLoop();
    void sendSubscriptions();

    std::array<std::unique_ptr<Slot>, maxSlots> slots;
    std::atomic<double> sampleRate { 48000.0 }, targetLatencyMs { 3.0 };

    mutable juce::CriticalSection lock;          // senders + manual addresses + slot names
    std::vector<SenderInfo> senders;
    juce::StringArray manualAddresses;
    struct Outgoing { juce::String address; int port; juce::String msg; };
    std::vector<Outgoing> outbox;                // sent by the control thread (sockets aren't shared across threads for writing)
    std::atomic<bool> subscribeNow { false };

    std::unique_ptr<juce::DatagramSocket> dataSocket, announceSocket;
    std::unique_ptr<juce::Thread> dataThread, controlThread;
    std::atomic<bool> running { false };
    std::atomic<int> boundDataPort { 0 };
    const uint32_t receiverId;

    std::vector<float> weights;                  // audio thread scratch
    std::vector<int> bases;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (NetReceiver)
};

} // namespace hawthorne
