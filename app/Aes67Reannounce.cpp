// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "Aes67Reannounce.h"

#include "Render.h"

namespace a3app
{
Command restartAes67Command() { return { "systemctl", "--user", "restart", "a3-aes67.service" }; }

namespace
{
// A restart that failed or timed out (-1) takes the signature away: the senders may still run the old config, and
// a missing signature counts as changed, so the next Save renders and restarts again. A timeout that was in fact a
// slow success costs one extra restart then.
std::string restarted (CommandRunner& runner, const juce::File& home)
{
    const auto ran = runner.run (restartAes67Command());
    if (ran.exitCode == 0)
        return ", a3-aes67 restarted";
    home.getChildFile (a3core::aes67SignaturePath).deleteFile();
    return ", a3-aes67 restart failed (exit " + std::to_string (ran.exitCode) + ")";
}
}

std::string reannounceStreams (const Aes67Render& render, const std::vector<a3core::StreamSpec>& streams, CommandRunner& runner)
{
    try
    {
        const auto announced = a3core::renderedAes67Signature (render.home);
        if (announced && ! a3core::streamsChanged (*announced, streams))
            return {};
        const auto written = a3core::writeAes67Conf (render.truth, render.home, render.hostname, render.aes67Template, streams);
        // Ours after the write, and storing these streams: a kept or removed config is no sender of ours to restart.
        const bool ours = a3core::renderedAes67Signature (render.home) == a3core::streamsSignature (streams);
        return "AES67: " + written + (ours ? restarted (runner, render.home) : "");
    }
    catch (const a3core::RenderError& problem)
    {
        return "AES67: " + problem.what;
    }
}
}
