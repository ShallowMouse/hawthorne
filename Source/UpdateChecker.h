#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include <juce_core/juce_core.h>

#ifndef HAWTHORNE_VERSION
#define HAWTHORNE_VERSION "1.0.0"
#endif

//==============================================================================
// Checks ShallowMouse/Hawthorne-Releases for a newer version on launch.
// Uses WinINet so no libcurl dependency.
//==============================================================================
class UpdateChecker : private juce::Thread
{
public:
    UpdateChecker() : juce::Thread("UpdateChecker") {}
    ~UpdateChecker() override { stopThread(3000); }

    // Call once after main window is shown
    void checkAsync() { startThread(); }

private:
    void run() override
    {
        // GitHub API: latest release from public releases repo
        const juce::String url =
            "https://api.github.com/repos/ShallowMouse/Hawthorne-Releases/releases/latest";

        juce::String json = fetchUrl(url);
        if (threadShouldExit() || json.isEmpty()) return;

        juce::String latestTag = parseTagName(json);
        if (latestTag.isEmpty()) return;

        // Strip leading 'v'
        juce::String latestVer = latestTag.trimCharactersAtStart("vV");
        juce::String currentVer = HAWTHORNE_VERSION;

        if (isNewerVersion(latestVer, currentVer))
        {
            juce::String downloadUrl = parseDownloadUrl(json);

            juce::MessageManager::callAsync([latestVer, downloadUrl]()
            {
                auto result = juce::AlertWindow::showOkCancelBox(
                    juce::MessageBoxIconType::InfoIcon,
                    "Update Available",
                    "Hawthorne " + latestVer + " is available.\n"
                    "You are running " + juce::String(HAWTHORNE_VERSION) + ".\n\n"
                    "Download now?",
                    "Download",
                    "Later");

                if (result && downloadUrl.isNotEmpty())
                    juce::URL(downloadUrl).launchInDefaultBrowser();
            });
        }
    }

    // Minimal WinINet HTTP GET — no libcurl needed
    static juce::String fetchUrl(const juce::String& url)
    {
#if JUCE_WINDOWS
        // Use WinINet via JUCE's URL class with a custom user-agent
        juce::URL juceUrl(url);
        auto stream = juceUrl.createInputStream(
            juce::URL::InputStreamOptions(juce::URL::ParameterHandling::inAddress)
                .withConnectionTimeoutMs(8000)
                .withExtraHeaders("User-Agent: Hawthorne/" HAWTHORNE_VERSION));

        if (stream != nullptr)
            return stream->readEntireStreamAsString();
#endif
        return {};
    }

    static juce::String parseTagName(const juce::String& json)
    {
        // Simple manual parse — avoids JSON library dependency
        int idx = json.indexOf("\"tag_name\"");
        if (idx < 0) return {};
        int colon = json.indexOf(idx, ":");
        if (colon < 0) return {};
        int q1 = json.indexOf(colon, "\"");
        if (q1 < 0) return {};
        int q2 = json.indexOf(q1 + 1, "\"");
        if (q2 < 0) return {};
        return json.substring(q1 + 1, q2);
    }

    static juce::String parseDownloadUrl(const juce::String& json)
    {
        // Find the first .exe asset browser_download_url
        int idx = json.indexOf("browser_download_url");
        if (idx < 0) return {};
        int q1 = json.indexOf(idx, "\"");
        if (q1 < 0) return {};
        // skip the key's closing quote and the colon+quote
        int q2 = json.indexOf(q1 + 1, "\"");
        if (q2 < 0) return {};
        int q3 = json.indexOf(q2 + 1, "\"");
        if (q3 < 0) return {};
        int q4 = json.indexOf(q3 + 1, "\"");
        if (q4 < 0) return {};
        return json.substring(q3 + 1, q4);
    }

    // Returns true if a > b (simple semver major.minor.patch compare)
    static bool isNewerVersion(const juce::String& a, const juce::String& b)
    {
        auto split = [](const juce::String& s) -> std::array<int, 3>
        {
            auto parts = juce::StringArray::fromTokens(s, ".", "");
            return {
                parts.size() > 0 ? parts[0].getIntValue() : 0,
                parts.size() > 1 ? parts[1].getIntValue() : 0,
                parts.size() > 2 ? parts[2].getIntValue() : 0
            };
        };
        auto av = split(a), bv = split(b);
        if (av[0] != bv[0]) return av[0] > bv[0];
        if (av[1] != bv[1]) return av[1] > bv[1];
        return av[2] > bv[2];
    }

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(UpdateChecker)
};
