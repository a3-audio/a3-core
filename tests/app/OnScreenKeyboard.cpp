// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// The window's on-screen keyboard (StemDeck's, ported): the policy's commands reach onboard in order, the
// failure paths say so in one line, the real link asks the platform, and the KEYS button sits in the window.
#include <atomic>
#include <chrono>
#include <mutex>
#include <vector>

#include <gtest/gtest.h>

#include "FakePlatform.h"
#include "FakeView.h"
#include "Look.h"
#include "MainWindow.h"
#include "OnScreenKeyboard.h"
#include "PageSnapshot.h"

namespace
{
using a3app::OnScreenKeyboard;

// Onboard on paper: what was asked of it, and what it answers.
struct FakeOnboard final : a3app::OnboardLink
{
    std::atomic<bool> isInstalled { true };
    std::atomic<bool> answers { true };     // the D-Bus name is there
    std::atomic<bool> starts { true };
    std::mutex mutex;
    std::vector<std::string> calls;   // every call() and "launch"

    bool installed() const override { return isInstalled; }
    bool call (const std::string& method) override
    {
        const std::lock_guard<std::mutex> lock (mutex);
        calls.push_back (method);
        return answers;
    }
    bool launch() override
    {
        const std::lock_guard<std::mutex> lock (mutex);
        calls.push_back ("launch");
        return starts;
    }
    std::vector<std::string> asked()
    {
        const std::lock_guard<std::mutex> lock (mutex);
        return calls;
    }
};

const a3app::KeyboardTimings quick { 20, 5, 3 };

// The message thread's work (the hint arrives by callAsync) and the worker's, until `done` or a second is up.
template <class Done>
bool pumpUntil (Done done)
{
    const auto end = std::chrono::steady_clock::now() + std::chrono::seconds (2);
    while (std::chrono::steady_clock::now() < end)
    {
        juce::MessageManager::getInstance()->runDispatchLoopUntil (10);
        if (done())
            return true;
    }
    return done();
}

struct Keyboard : ::testing::Test
{
    juce::MessageManagerLock lock;   // the test runner's thread must stay out of component code
    a3app::Look look;
    std::shared_ptr<FakeOnboard> onboard = std::make_shared<FakeOnboard>();
    Keyboard() { juce::LookAndFeel::setDefaultLookAndFeel (&look); }
    ~Keyboard() override { juce::LookAndFeel::setDefaultLookAndFeel (nullptr); }
};
}

TEST_F (Keyboard, ATextFieldTakingFocusShowsOnboardAndAButtonLeavingHidesItAfterThePause)
{
    OnScreenKeyboard keyboard (onboard, quick);
    juce::TextEditor field;
    juce::TextButton button;
    keyboard.focusMovedTo (&field);
    ASSERT_TRUE (pumpUntil ([&] { return onboard->asked().size() == 1; }));
    EXPECT_EQ (onboard->asked(), (std::vector<std::string> { "Show" }));

    keyboard.focusMovedTo (&button);
    EXPECT_EQ (onboard->asked().size(), 1u) << "not at once";
    ASSERT_TRUE (pumpUntil ([&] { return onboard->asked().size() == 2; }));
    EXPECT_EQ (onboard->asked().back(), "Hide");
}

TEST_F (Keyboard, AReadOnlyEditorIsNoTextField)
{
    OnScreenKeyboard keyboard (onboard, quick);
    juce::TextEditor readOnly;
    readOnly.setReadOnly (true);
    keyboard.focusMovedTo (&readOnly);
    pumpUntil ([&] { return false; });
    EXPECT_TRUE (onboard->asked().empty());
}

TEST_F (Keyboard, WithoutOnboardKeysSaysSoInOneLineAndAsksNothing)
{
    onboard->isInstalled = false;
    OnScreenKeyboard keyboard (onboard, quick);
    juce::String hint;
    keyboard.onHint = [&] (const juce::String& line) { hint = line; };
    EXPECT_FALSE (keyboard.isAvailable());
    keyboard.keysPressed();
    EXPECT_EQ (hint, "On-screen keyboard: onboard is not installed");
    juce::TextEditor field;
    keyboard.focusMovedTo (&field);
    EXPECT_TRUE (onboard->asked().empty());
}

TEST_F (Keyboard, NoDBusNameAfterTheStartupDelaySaysSoAndStaysAvailable)
{
    onboard->answers = false;
    OnScreenKeyboard keyboard (onboard, quick);
    juce::String hint;
    keyboard.onHint = [&] (const juce::String& line) { hint = line; };
    keyboard.keysPressed();
    ASSERT_TRUE (pumpUntil ([&] { return hint.isNotEmpty(); }));
    EXPECT_EQ (hint, "On-screen keyboard: onboard did not answer");
    EXPECT_TRUE (keyboard.isAvailable());
    // toggle, launch, then three polls of Show
    EXPECT_EQ (onboard->asked(), (std::vector<std::string> { "ToggleVisible", "launch", "Show", "Show", "Show" }));
}

TEST_F (Keyboard, OnboardThatCannotBeStartedMakesTheKeyboardUnavailable)
{
    onboard->answers = false;
    onboard->starts = false;
    OnScreenKeyboard keyboard (onboard, quick);
    juce::String hint;
    keyboard.onHint = [&] (const juce::String& line) { hint = line; };
    keyboard.keysPressed();
    ASSERT_TRUE (pumpUntil ([&] { return hint.isNotEmpty(); }));
    EXPECT_EQ (hint, "On-screen keyboard: onboard could not be started");
    EXPECT_FALSE (keyboard.isAvailable());
}

TEST_F (Keyboard, OnboardNotRunningIsStartedOnceAndThenShown)
{
    onboard->answers = false;
    OnScreenKeyboard keyboard (onboard, { 20, 20, 40 });   // 800 ms of polling: ample to answer in
    juce::String hint;
    keyboard.onHint = [&] (const juce::String& line) { hint = line; };
    keyboard.keysPressed();
    ASSERT_TRUE (pumpUntil ([&] { return onboard->asked().size() >= 3; }));
    onboard->answers = true;   // it claims its name now
    ASSERT_TRUE (pumpUntil ([&] { return onboard->asked().size() >= 4 && onboard->asked().back() == "Show"; }));
    // Shown means the polling stopped at the answer: nothing more is asked and no failure is reported.
    const auto answeredAt = onboard->asked().size();
    pumpUntil ([&] { return false; });
    EXPECT_EQ (onboard->asked().size(), answeredAt);
    EXPECT_LT (answeredAt, 2u + 40u);
    EXPECT_TRUE (hint.isEmpty()) << hint;
    const auto asked = onboard->asked();
    EXPECT_EQ (std::count (asked.begin(), asked.end(), "launch"), 1);
}

namespace
{
struct RealLink : ::testing::Test
{
    fake::FakePlatform platform;
    juce::File bin = juce::File::createTempFile ("bin");
    RealLink()
    {
        bin.createDirectory();
        platform.env["PATH"] = bin.getFullPathName().toStdString();
    }
    ~RealLink() override { bin.deleteRecursively(); }
    void install (const char* program) { bin.getChildFile (program).replaceWithText ("#!/bin/sh\n"); }
};
}

TEST_F (RealLink, IsInstalledOnlyWithOnboardAndDbusSendOnThePlatformsPath)
{
    auto link = a3app::makeOnboardLink (platform);
    EXPECT_FALSE (link->installed());
    install ("onboard");
    EXPECT_FALSE (link->installed());
    install ("dbus-send");
    EXPECT_TRUE (link->installed());
    EXPECT_TRUE (platform.children.empty()) << "looking is not spawning";
}

TEST_F (RealLink, AsksOnboardWithDbusSendAndTakesItsExitCode)
{
    auto link = a3app::makeOnboardLink (platform);
    platform.spawnedStatus = 0;
    EXPECT_TRUE (link->call ("Show"));
    ASSERT_EQ (platform.children.size(), 1u);
    const auto& argv = platform.children[0]->argv;
    EXPECT_EQ (argv.front(), "dbus-send");
    EXPECT_EQ (argv.back(), "org.onboard.Onboard.Keyboard.Show");
    EXPECT_NE (std::find (argv.begin(), argv.end(), "--dest=org.onboard.Onboard"), argv.end());

    platform.spawnedStatus = 256;   // exit code 1
    EXPECT_FALSE (link->call ("Hide"));
}

TEST_F (RealLink, ADbusSendThatNeverAnswersIsKilledAndCountsAsNoAnswer)
{
    auto link = a3app::makeOnboardLink (platform);
    EXPECT_FALSE (link->call ("Show"));   // the fake child never ends
    ASSERT_EQ (platform.children.size(), 1u);
    EXPECT_EQ (platform.children[0]->killed, 1);
}

TEST_F (RealLink, LaunchStartsOnboardInItsOwnGroupAndReportsAFailedStart)
{
    auto link = a3app::makeOnboardLink (platform);
    EXPECT_TRUE (link->launch());
    ASSERT_EQ (platform.children.size(), 1u);
    EXPECT_EQ (platform.children[0]->argv, (std::vector<std::string> { "onboard" }));
    EXPECT_TRUE (platform.children[0]->ownGroup);
    platform.failingSpawns = 1;
    EXPECT_FALSE (link->launch());
}

namespace
{
using a3apptest::expectPainted;
using a3apptest::snapshot;
using a3apptest::FakeView;
struct KeysInTheWindow : Keyboard
{
    FakeView view;
    juce::AudioDeviceManager manager;
    KeysInTheWindow() { manager.addAudioDeviceType (std::make_unique<a3apptest::NoDevices>()); }
};
}

TEST_F (KeysInTheWindow, TheKeysButtonSitsInTheFooterAndNeverTakesTheFocus)
{
    a3app::MainWindow window (view, manager, {}, false, [] {}, {}, { onboard, quick });
    auto* keys = window.keysButton();
    ASSERT_NE (keys, nullptr);
    EXPECT_FALSE (keys->getMouseClickGrabsKeyboardFocus());   // pressing it must not leave the field it is for
    window.setSize (a3apptest::portraitWidth, a3apptest::portraitHeight);
    EXPECT_TRUE (keys->isShowing() || keys->isVisible());
    EXPECT_GE (keys->getHeight(), 28);   // a fingertip, as the page keys
    EXPECT_TRUE (window.bands().footer.contains (keys->getBounds())) << keys->getBounds().toString();
    EXPECT_LT (keys->getRight(), window.getWidth() / 3);   // at the footer's left, away from Save
    expectPainted (snapshot (*window.getContentComponent(), "window-with-keys"));

    keys->triggerClick();
    ASSERT_TRUE (pumpUntil ([&] { return ! onboard->asked().empty(); }));
    EXPECT_EQ (onboard->asked().front(), "ToggleVisible");
}

TEST_F (KeysInTheWindow, WithoutOnboardTheWindowSaysSoInOneLineWherePeopleLook)
{
    onboard->isInstalled = false;
    a3app::MainWindow window (view, manager, {}, false, [] {}, {}, { onboard, quick });
    EXPECT_TRUE (window.keyboardNote().isEmpty()) << "a laptop with a keyboard is not nagged";
    window.keysButton()->triggerClick();   // delivered by the message loop
    ASSERT_TRUE (pumpUntil ([&] { return window.keyboardNote().isNotEmpty(); }));
    EXPECT_EQ (window.keyboardNote(), "On-screen keyboard: onboard is not installed");
    expectPainted (snapshot (*window.getContentComponent(), "window-keys-hint"));
    const auto bands = window.bands();   // the hint takes its own line above the footer, never the footer itself
    EXPECT_FALSE (bands.page.intersects (bands.footer));
}

TEST_F (KeysInTheWindow, WhenTheHintHasExpiredTheTooltipStillSaysOnboardIsMissing)
{
    onboard->isInstalled = false;
    a3app::MainWindow window (view, manager, {}, false, [] {}, {}, { onboard, quick });
    window.keysButton()->triggerClick();
    ASSERT_TRUE (pumpUntil ([&] { return window.keyboardNote().isNotEmpty(); }));
    for (int tick = 0; tick < 200 && window.keyboardNote().isNotEmpty(); ++tick)
        window.refreshNow();
    ASSERT_TRUE (window.keyboardNote().isEmpty());
    EXPECT_EQ (window.keysButton()->getTooltip(), "On-screen keyboard: onboard is not installed");
}

TEST_F (KeysInTheWindow, AFailedStartIsSaidInTheWindowToo)
{
    onboard->answers = false;
    a3app::MainWindow window (view, manager, {}, false, [] {}, {}, { onboard, quick });
    window.keysButton()->triggerClick();
    ASSERT_TRUE (pumpUntil ([&] { return window.keyboardNote().isNotEmpty(); }));
    EXPECT_EQ (window.keyboardNote(), "On-screen keyboard: onboard did not answer");
}

TEST_F (KeysInTheWindow, AWindowWithoutAKeyboardSetupHasNoKeysButton)
{
    a3app::MainWindow window (view, manager, {}, false, [] {});
    EXPECT_EQ (window.keysButton(), nullptr);
    EXPECT_TRUE (window.keyboardNote().isEmpty());
}
