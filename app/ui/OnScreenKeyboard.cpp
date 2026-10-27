// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Ported from StemDeck (Source/OnScreenKeyboard.cpp). Adapted: the namespace, the link behind which
// dbus-send and the launch sit (OnboardLink), the timings as a parameter, and focusMovedTo as the entry the
// desktop's focus listener and the tests share.
#include "OnScreenKeyboard.h"

#include <deque>
#include <optional>

namespace a3app
{
namespace
{
bool isTextField (juce::Component* component)
{
    const auto* editor = dynamic_cast<juce::TextEditor*> (component);
    return editor != nullptr && ! editor->isReadOnly();
}
}

//==============================================================================
// One queue, one thread: the calls keep their order (a show and the hide right after it must not
// overtake each other) and none waits on the UI.
class OnScreenKeyboard::Worker final : private juce::Thread
{
public:
    enum class Request { show, hide, toggle };
    enum class Failure { couldNotStart, noAnswer };

    Worker (std::shared_ptr<OnboardLink> linkToUse, KeyboardTimings timingsToUse, std::function<void (Failure)> reportFailure)
        : juce::Thread ("On-screen keyboard"), link (std::move (linkToUse)), timings (timingsToUse), report (std::move (reportFailure))
    {
        startThread();
    }

    ~Worker() override
    {
        signalThreadShouldExit();
        wake.signal();
        stopThread (3000);
    }

    void post (Request request)
    {
        {
            const juce::ScopedLock lock (queueLock);
            queue.push_back (request);
        }
        wake.signal();
    }

private:
    void run() override
    {
        while (! threadShouldExit())
        {
            wake.wait (-1);

            while (! threadShouldExit())
            {
                const auto next = takeNext();

                if (! next.has_value())
                    break;

                perform (*next);
            }
        }
    }

    std::optional<Request> takeNext()
    {
        const juce::ScopedLock lock (queueLock);

        if (queue.empty())
            return std::nullopt;

        const auto request = queue.front();
        queue.pop_front();
        return request;
    }

    void perform (Request request)
    {
        if (request == Request::hide)
        {
            // Not running is as good as hidden.
            link->call ("Hide");
            return;
        }

        if (link->call (request == Request::toggle ? "ToggleVisible" : "Show"))
            return;

        // Not running: both a show and a toggle mean "show".
        startAndShow();
    }

    void startAndShow()
    {
        if (! link->launch())
        {
            report (Failure::couldNotStart);
            return;
        }

        // Onboard shows itself on start, unless it is set to start minimised; asking until it answers covers both.
        for (int poll = 0; poll < timings.startupPolls; ++poll)
        {
            wait (timings.startupPollMs);

            if (threadShouldExit())
                return;

            if (link->call ("Show"))
                return;
        }

        report (Failure::noAnswer);
    }

    std::shared_ptr<OnboardLink> link;
    KeyboardTimings timings;
    std::function<void (Failure)> report;
    juce::CriticalSection queueLock;
    std::deque<Request> queue;
    juce::WaitableEvent wake;
};

//==============================================================================
OnScreenKeyboard::OnScreenKeyboard (std::shared_ptr<OnboardLink> link, KeyboardTimings timings)
    : hideDebounceMs (timings.hideDebounceMs), policy (link->installed())
{
    // Created here, on the message thread; the worker only copies it.
    juce::WeakReference<OnScreenKeyboard> self (this);

    worker = std::make_unique<Worker> (std::move (link), timings, [self] (Worker::Failure failure)
    {
        juce::MessageManager::callAsync ([self, failure]
        {
            if (auto* keyboard = self.get())
            {
                if (failure == Worker::Failure::couldNotStart)
                    keyboard->onboardFailed ("On-screen keyboard: onboard could not be started", false);
                else
                    keyboard->onboardFailed ("On-screen keyboard: onboard did not answer", true);
            }
        });
    });

    juce::Desktop::getInstance().addFocusChangeListener (this);
}

OnScreenKeyboard::~OnScreenKeyboard()
{
    juce::Desktop::getInstance().removeFocusChangeListener (this);
    stopTimer();
    worker.reset();
}

void OnScreenKeyboard::keysPressed()
{
    run (policy.keysPressed());
}

void OnScreenKeyboard::focusMovedTo (juce::Component* focused)
{
    run (policy.focusChanged (isTextField (focused) ? focused : nullptr));
}

void OnScreenKeyboard::timerCallback()
{
    stopTimer();
    run (policy.hideDue());
}

void OnScreenKeyboard::run (KeyboardPolicy::Command command)
{
    using Command = KeyboardPolicy::Command;

    switch (command)
    {
        case Command::none:     return;
        case Command::show:     worker->post (Worker::Request::show); return;
        case Command::hide:     worker->post (Worker::Request::hide); return;
        case Command::toggle:   worker->post (Worker::Request::toggle); return;
        case Command::hideSoon: startTimer (hideDebounceMs); return;
        case Command::hint:
            if (onHint != nullptr)
                onHint ("On-screen keyboard: onboard is not installed");
            return;
    }
}

void OnScreenKeyboard::onboardFailed (const juce::String& why, bool stillAvailable)
{
    policy.setAvailable (stillAvailable);

    if (onHint != nullptr)
        onHint (why);
}
}
