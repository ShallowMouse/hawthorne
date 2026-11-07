#pragma once

// MELVILLE: when this program is built into MELVILLE (MELVILLE_HOSTED), it opens MELVILLE's
// device for its part instead of picking an interface, and leaves the device settings it
// saves alone. Built on its own (the normal build), all of this does nothing.

#include <juce_audio_devices/juce_audio_devices.h>

namespace hawthorne { class NetReceiver; }

#if defined (MELVILLE_HOSTED) && MELVILLE_HOSTED
 #include <MelvilleHosted.h>
 #include <MelvilleNetwork.h>     // MELVILLE owns Hawthorne Link (its NETWORK panel)
#else
namespace melville
{
    inline bool isHosted() noexcept { return false; }
    inline bool attach (juce::AudioDeviceManager&, const char*) { return false; }
    inline hawthorne::NetReceiver* networkReceiver() noexcept { return nullptr; }
}
#endif
