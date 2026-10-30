// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Running pw-metadata and systemctl from the engine: posix_spawn, never fork. The engine holds hundreds of
// megabytes; a fork copies its page tables and has stalled the audio before (StemDeck, 2026-10-29), while
// posix_spawn (vfork-like in glibc) does not. Called on the control thread only, never the audio thread.
#pragma once
#include "AudioTiming.h"
#include <optional>
#include <string>
#include <sys/types.h>

namespace a3app
{
class CommandRunner
{
public:
    struct Result
    {
        int exitCode = -1;    // -1: it did not start, or did not end in time
        std::string output;   // stdout
    };
    virtual ~CommandRunner() = default;
    // Runs and waits (with a time limit), its stdout captured: the quick ones (pw-metadata).
    virtual Result run (const Command&) = 0;
    // Starts one without waiting (systemctl restart takes seconds); false when it did not start.
    virtual bool start (const Command&) = 0;
    // The started one's exit code once it ended, else nullopt.
    virtual std::optional<int> finished() = 0;
};

class SpawnRunner final : public CommandRunner
{
public:
    static constexpr int runTimeoutMillis = 3000;
    ~SpawnRunner() override;
    Result run (const Command&) override;
    bool start (const Command&) override;
    std::optional<int> finished() override;

private:
    pid_t startedPid = -1;
};
}
