// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "OnboardLink.h"

#include <mutex>
#include <vector>

namespace a3app
{
namespace
{
bool isOnPath (const a3core::Platform& platform, const juce::String& program)
{
    const auto path = platform.environment ("PATH").value_or ("/usr/local/bin:/usr/bin:/bin");
    for (const auto& dir : juce::StringArray::fromTokens (juce::String (path), ":", {}))
        if (dir.isNotEmpty() && juce::File (dir).getChildFile (program).existsAsFile())
            return true;
    return false;
}

class RealOnboardLink final : public OnboardLink
{
public:
    RealOnboardLink (const a3core::Platform& platformToUse, int replyTimeout) : platform (platformToUse), replyTimeoutMs (replyTimeout) {}

    bool installed() const override { return isOnPath (platform, "onboard") && isOnPath (platform, "dbus-send"); }

    // --print-reply makes dbus-send wait for onboard's answer, so its exit code says whether anyone took the call.
    bool call (const std::string& method) override
    {
        auto dbusSend = platform.spawn ({ "dbus-send", "--session", "--print-reply", "--reply-timeout=" + std::to_string (replyTimeoutMs),
                                          "--dest=org.onboard.Onboard", "/org/onboard/Onboard/Keyboard",
                                          "org.onboard.Onboard.Keyboard." + method },
                                        false, a3core::ProcessGroup::inherit, a3core::ErrorOutput::discard);
        if (dbusSend == nullptr)
            return false;
        const auto status = dbusSend->wait ((replyTimeoutMs + 500) / 1000.0);
        if (! status)
        {
            dbusSend->kill();
            dbusSend->wait (1.0);
            return false;
        }
        return *status == 0;
    }

    // Onboard is the desktop's keyboard, not this program's child: its own process group, so a Ctrl-C in
    // our terminal leaves it. The platform's children keep no descriptor but 0, 1 and 2, which StemDeck
    // closed by hand (a crashed program must find its sockets free). The handle is kept only to reap it.
    bool launch() override
    {
        const std::lock_guard<std::mutex> lock (mutex);
        reapEnded();
        auto started = platform.spawn ({ "onboard" }, false, a3core::ProcessGroup::own, a3core::ErrorOutput::discard);
        if (started == nullptr)
            return false;
        launched.push_back (std::move (started));
        return true;
    }

private:
    void reapEnded()
    {
        for (auto it = launched.begin(); it != launched.end();)
            it = (*it)->poll() ? launched.erase (it) : it + 1;
    }

    const a3core::Platform& platform;
    const int replyTimeoutMs;
    std::mutex mutex;
    std::vector<std::unique_ptr<a3core::Child>> launched;
};
}

std::shared_ptr<OnboardLink> makeOnboardLink (const a3core::Platform& platform, int replyTimeoutMs)
{
    return std::make_shared<RealOnboardLink> (platform, replyTimeoutMs);
}
}
