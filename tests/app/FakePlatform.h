// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// A platform for the app's tests: an environment and a home of the test's, units it says are
// enabled, and children that only exist on paper -- the test ends them and sees what was done to them.
#pragma once

#include <csignal>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <string>
#include <vector>

#include "Platform.h"

namespace fake
{
struct ChildState
{
    std::vector<std::string> argv;
    int pid = 0;
    std::optional<int> status;   // set: the child has ended
    int terminated = 0, killed = 0;
    bool endsOnTerminate = true;
};

class FakeChild final : public a3core::Child
{
public:
    explicit FakeChild (std::shared_ptr<ChildState> s) : state (std::move (s)) {}
    int pid() const override { return state->pid; }
    int stdoutFd() const override { return -1; }
    std::optional<int> poll() override { return state->status; }
    std::optional<int> wait (double) override { return state->status; }
    void terminate() override
    {
        ++state->terminated;
        if (state->endsOnTerminate && ! state->status)
            state->status = SIGTERM;   // as a wait status: killed by SIGTERM
    }
    void kill() override
    {
        ++state->killed;
        if (! state->status)
            state->status = SIGKILL;
    }

private:
    std::shared_ptr<ChildState> state;
};

class FakePlatform final : public a3core::Platform
{
public:
    std::map<std::string, std::string> env;
    juce::File homeDir { "/home/tester" };
    std::string host = "test-host";
    std::set<std::string> enabledUnits;
    mutable std::vector<std::string> unitsAsked;
    mutable std::vector<std::shared_ptr<ChildState>> children;   // every spawn, in order
    mutable int failingSpawns = 0;                               // the next n spawns start nothing

    std::optional<std::string> environment (const std::string& name) const override
    {
        const auto found = env.find (name);
        return found == env.end() ? std::nullopt : std::optional<std::string> (found->second);
    }
    juce::File home() const override { return homeDir; }
    juce::File stateDir() const override
    {
        const auto xdg = environment ("XDG_STATE_HOME");
        return (xdg ? juce::File (*xdg) : homeDir.getChildFile (".local/state")).getChildFile ("a3-core");
    }
    juce::File configDir() const override { return homeDir.getChildFile (".config/a3"); }
    juce::File packageShare() const override { return homeDir.getChildFile (".local/share/a3-core"); }
    std::string hostname() const override { return host; }
    bool unitEnabled (const std::string& unit) const override
    {
        unitsAsked.push_back (unit);
        return enabledUnits.count (unit) > 0;
    }
    juce::File self() const override { return juce::File ("/usr/bin/a3-core"); }
    std::unique_ptr<a3core::Child> spawn (const std::vector<std::string>& argv, bool) const override
    {
        if (failingSpawns > 0)
        {
            --failingSpawns;
            return nullptr;
        }
        auto state = std::make_shared<ChildState>();
        state->argv = argv;
        state->pid = 1000 + static_cast<int> (children.size());
        children.push_back (state);
        return std::make_unique<FakeChild> (state);
    }

    // The children whose argv is exactly this, in spawn order.
    std::vector<std::shared_ptr<ChildState>> spawned (const std::vector<std::string>& argv) const
    {
        std::vector<std::shared_ptr<ChildState>> found;
        for (const auto& child : children)
            if (child->argv == argv)
                found.push_back (child);
        return found;
    }
};
}
