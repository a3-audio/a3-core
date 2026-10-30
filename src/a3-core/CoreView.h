// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// What the window may read of Core. Core is the serve thread's alone (P8): the HTTP threads never call
// into it. The serve thread renders its status and its rig as JSON now and then and puts the text
// here, under a mutex; a request reads the latest copy, which is at most one publishing old.
#pragma once

#include <mutex>
#include <string>

namespace a3core
{
class CoreView
{
public:
    struct Texts
    {
        std::string status = "{}", rig = "{}", departments = R"({"departments":[]})";   // canonical JSON
    };

    void publish (Texts texts)
    {
        const std::scoped_lock hold (lock);
        latest = std::move (texts);
    }

    Texts texts() const
    {
        const std::scoped_lock hold (lock);
        return latest;
    }

private:
    mutable std::mutex lock;
    Texts latest;
};
}
