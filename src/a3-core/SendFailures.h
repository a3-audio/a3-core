// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Port of a3_core_send_failures.py (B0 f3345dc7feeb): a send that fails is said once, and does not
// end the message. Right after a boot the network can come up after Core; a failing send must
// neither stop the other destinations nor fill the journal. Only OsError is caught: anything else
// is a fault in Core, not the network, and stays loud.
#pragma once

#include <exception>
#include <functional>
#include <set>
#include <string>

#include "Net.h"

namespace a3core
{
// Python's OSError as a send reports it: `strerror` is what the system said (empty for an error
// made from a message alone), `text` is str(problem).
struct OsError : std::exception
{
    OsError (int errorNumber, std::string systemMessage)
        : strerror (systemMessage), text ("[Errno " + std::to_string (errorNumber) + "] " + systemMessage) {}
    explicit OsError (std::string message) : text (std::move (message)) {}

    const char* what() const noexcept override { return text.c_str(); }

    std::string strerror, text;
};

class SendFailures
{
public:
    // `say` is Python's print: stdout (toStderr false).
    explicit SendFailures (Journal say) : say (std::move (say)) {}

    // Run `send()`; true if it got through.
    bool send (const std::string& destination, const std::function<void()>& send);

private:
    Journal say;
    std::set<std::string> failing;
};
}
