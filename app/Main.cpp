// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// a3-core: the one program. One JUCEApplication that runs the role its command line names (see
// ProgramArgs.h): core (the C++ Core service), engine (Plan 4's engine without its window), the
// screencast fan-out, the supervisor (--headless: Core and the engine as children); the window,
// the i3 bar and the subcommands come later and only say so. Each role is a RoleRunner the app's
// timer drives on the message thread; SIGTERM and SIGINT only set a flag that the next tick reads.
#include <atomic>
#include <cstdio>
#include <thread>

#include "CoreService.h"
#include "EngineRole.h"
#include "Platform.h"
#include "ProgramArgs.h"
#include "Roles.h"
#include "ScreencastFanout.h"
#include "Supervisor.h"
#include "WindowProcess.h"
#include "ui/MainWindow.h"

namespace
{
using a3app::Role;
using Started = std::variant<std::unique_ptr<a3app::RoleRunner>, int>;

void sayError (const std::string& line) { std::fprintf (stderr, "%s\n", line.c_str()); }

// The core role: the Core service and the rule of its stop and exit (CoreRole).
class CoreRunner final : public a3app::RoleRunner
{
public:
    explicit CoreRunner (std::unique_ptr<a3core::CoreService> started)
        : service (std::move (started)),
          role ([this] { service->stop(); }, [this] (double seconds) { return service->waitUntilFinished (seconds); }) {}
    std::optional<int> tick (bool stopRequested) override { return role.tick (stopRequested); }

private:
    std::unique_ptr<a3core::CoreService> service;
    a3app::CoreRole role;
};

// The fan-out's loop blocks; it runs on a thread of its own until the stop signal.
class FanoutRunner final : public a3app::RoleRunner
{
public:
    FanoutRunner (int port, std::vector<std::string> command, double linger, const a3core::Platform& platform)
    {
        options.platform = &platform;
        loop = std::thread ([this, port, command = std::move (command), linger] {
            result = a3core::runFanout (port, command, linger, stop, options);
            done = true;
        });
    }
    ~FanoutRunner() override
    {
        stop = true;
        loop.join();
    }
    std::optional<int> tick (bool stopRequested) override
    {
        if (stopRequested)
            stop = true;
        if (! done)
            return std::nullopt;
        return result.load();
    }

private:
    a3core::FanoutOptions options;
    std::atomic<bool> stop { false }, done { false };
    std::atomic<int> result { 0 };
    std::thread loop;
};

class SupervisorRunner final : public a3app::RoleRunner
{
public:
    SupervisorRunner (const a3core::Platform& platform, const juce::String& self)
        : supervisor (platform, self, a3core::streamJournal (stdout, stderr))
    {
        supervisor.start (a3app::monotonicSeconds());
    }
    std::optional<int> tick (bool stopRequested) override
    {
        if (! stopRequested)
        {
            supervisor.tick (a3app::monotonicSeconds());
            return supervisor.finished();   // nothing left to run: end as the unit would
        }
        supervisor.stop();
        return 0;
    }

private:
    a3app::Supervisor supervisor;
};

// The window role: Core's client and the engine's remote view from the truth's core.web and engine.control,
// the laptop rule (WindowProcess), and the window over both. Closing asks once about its own children.
class WindowRunner final : public a3app::RoleRunner
{
public:
    WindowRunner (const a3core::Platform& platform, const a3core::Truth& truth)
        : core (a3app::coreClientFor (truth)),
          engine (makeEngineView (truth)),
          process (platform, *core, *engine)
    {
        for (const auto role : process.attach())
            std::printf ("a3-core: started --role %s, it was not running\n", a3app::roleWord (role).c_str());
        std::fflush (stdout);
        engine->refreshNow();   // the first paint shows the engine as it is, not "no engine"
        core->refreshNow (0);
        window = std::make_unique<a3app::MainWindow> (*engine, pageDevices, spatialChoices (platform), false, [this] { askThenQuit(); },
                                                      a3app::MonitorSetup { core.get(), a3app::windowSettingsFile (platform.configDir()) });
        window->open();
    }

    ~WindowRunner() override { window.reset(); }   // before the views its pages read

    std::optional<int> tick (bool stopRequested) override
    {
        if (stopRequested && ! done)
            done = true;   // a signal asks nobody: what it started runs on
        return done ? std::optional<int> (0) : std::nullopt;
    }

private:
    static std::unique_ptr<a3app::RemoteHostView> makeEngineView (const a3core::Truth& truth)
    {
        const auto control = truth.endpoint ("engine", "control");
        return std::make_unique<a3app::RemoteHostView> (control.host, control.port);
    }

    // The directories the ZONES page offers: the user's own and the installed variants, where they exist.
    static std::vector<juce::File> spatialChoices (const a3core::Platform& platform)
    {
        std::vector<juce::File> choices;
        for (const auto& directory : { platform.home().getChildFile (".config/a3/engine/spatial"), juce::File ("/usr/share/a3-engine/spatial"),
                                       juce::File ("/usr/share/a3-engine/spatial-reaper-booth-6.1") })
            if (directory.isDirectory())
                choices.push_back (directory);
        return choices;
    }

    void askThenQuit()
    {
        const auto question = process.quitQuestion();
        if (! question)
        {
            done = true;
            return;
        }
        // Two keys: the first answers 1, the second (and Escape) 0 -- leaving them running is the default.
        juce::AlertWindow::showAsync (juce::MessageBoxOptions()
                                          .withIconType (juce::MessageBoxIconType::QuestionIcon)
                                          .withTitle ("a3-core")
                                          .withMessage (*question)
                                          .withButton ("Stop")
                                          .withButton ("Leave running")
                                          .withAssociatedComponent (window.get()),   // the window's look
                                      [this] (int answer) {
                                          process.quit (answer == 1);
                                          done = true;
                                      });
    }

    std::unique_ptr<a3app::CoreClient> core;
    std::unique_ptr<a3app::RemoteHostView> engine;
    a3app::WindowProcess process;
    juce::AudioDeviceManager pageDevices;   // only a catalogue of device types for the AUDIO page; never initialised
    std::unique_ptr<a3app::MainWindow> window;
    bool done = false;
};

Started startWindow (const a3core::Platform& platform)
{
    const auto files = a3app::coreFiles (platform);
    auto loaded = a3core::Truth::load (files.truth, files.network);
    if (const auto* refused = std::get_if<a3core::TruthError> (&loaded))
    {
        sayError ("a3-core: " + refused->what);
        return 1;
    }
    const auto& truth = std::get<a3core::Truth> (loaded);
    try
    {
        return std::make_unique<WindowRunner> (platform, truth);
    }
    catch (const a3core::TruthError& missing)
    {
        sayError ("a3-core: the truth has no " + missing.what);
        return 1;
    }
}

Started startCore (const a3app::ProgramArgs& args, const a3core::Platform& platform)
{
    auto options = a3app::coreOptions (args, platform);
    if (const auto* refused = std::get_if<std::string> (&options))
    {
        sayError (*refused);
        return 1;
    }
    auto started = a3core::CoreService::start (std::move (std::get<a3core::ServiceOptions> (options)), a3core::streamJournal (stdout, stderr));
    if (const auto* refused = std::get_if<std::string> (&started))
    {
        sayError (*refused);   // B1's sys.exit("a3-core: ..."): stderr, exit 1
        return 1;
    }
    return std::make_unique<CoreRunner> (std::move (std::get<std::unique_ptr<a3core::CoreService>> (started)));
}

Started startEngine (const a3app::ProgramArgs& args, const a3core::Platform& platform)
{
    auto started = a3app::EngineRole::start (args.engine, platform.home());
    if (const auto* code = std::get_if<int> (&started))
        return *code;
    return std::unique_ptr<a3app::RoleRunner> (std::move (std::get<std::unique_ptr<a3app::EngineRole>> (started)));
}

Started startFanout (const a3app::ProgramArgs& args, const a3core::Platform& platform)
{
    const int port = a3app::fanoutPort (args, platform);
    if (const auto refusal = a3app::fanoutRefusal (port, args.encoder))
    {
        std::fprintf (stderr, "%sa3-core: error: %s\n", a3app::usage, refusal->c_str());   // argparse's parser.error
        return 2;
    }
    std::vector<std::string> command;
    for (const auto& word : args.encoder)
        command.push_back (word.toStdString());
    return std::make_unique<FanoutRunner> (port, std::move (command), args.fanoutLinger, platform);
}

Started notBuiltYet (const char* what)
{
    std::printf ("a3-core: %s is not built yet\n", what);
    std::fflush (stdout);
    return 0;
}

Started startRole (const a3app::ProgramArgs& args, const a3core::Platform& platform)
{
    switch (args.role)
    {
        case Role::core: return startCore (args, platform);
        case Role::engine: return startEngine (args, platform);
        case Role::screencastFanout: return startFanout (args, platform);
        case Role::supervisor:
            return std::make_unique<SupervisorRunner> (platform, platform.self().getFullPathName());
        case Role::window: return startWindow (platform);
        case Role::i3Bar: return notBuiltYet ("--role i3-bar");
        case Role::renderTruth: return notBuiltYet ("--render-truth");
        case Role::truthQuery: return notBuiltYet ("--truth");
    }
    return 2;
}

class A3CoreApp final : public juce::JUCEApplication, private juce::Timer
{
public:
    const juce::String getApplicationName() override { return "a3-core"; }
    const juce::String getApplicationVersion() override { return "0.1"; }
    bool moreThanOneInstanceAllowed() override { return true; }

    void initialise (const juce::String&) override
    {
        juce::String error;
        const auto args = a3app::parseProgramArgs (getCommandLineParameterArray(), error);
        if (! args)
        {
            std::fprintf (stderr, "a3-core: %s\n%s", error.toRawUTF8(), a3app::usage);
            return finish (2);
        }
        a3app::installStopSignals();
        platform = a3core::makePlatform();
        Started started = 0;
        {
            // The role's threads never take the signal: it reaches the message thread only.
            const a3app::ScopedStopSignalsBlocked blocked;
            started = startRole (*args, *platform);
        }
        if (const auto* code = std::get_if<int> (&started))
            return finish (*code);
        runner = std::move (std::get<std::unique_ptr<a3app::RoleRunner>> (started));
        startTimerHz (runner->tickHz());
    }

    void shutdown() override
    {
        stopTimer();
        runner.reset();
    }

    void systemRequestedQuit() override { a3app::requestStop(); }   // a stop like SIGTERM: the role ends itself
    void anotherInstanceStarted (const juce::String&) override {}

private:
    void finish (int code)
    {
        setApplicationReturnValue (code);
        quit();
    }

    void timerCallback() override
    {
        const auto code = runner->tick (a3app::stopRequested());
        if (! code)
            return;
        stopTimer();
        finish (*code);
    }

    std::unique_ptr<a3core::Platform> platform;
    std::unique_ptr<a3app::RoleRunner> runner;
};
}

START_JUCE_APPLICATION (A3CoreApp)
