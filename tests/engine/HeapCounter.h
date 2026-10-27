// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include <functional>

// The test binary's C allocator counter (EngineAllocation.cpp replaces malloc and friends).
namespace a3test
{
struct HeapCount
{
    int allocated = 0, released = 0;
};

// What `work` allocates and frees on this thread.
HeapCount heapDuring (const std::function<void()>& work);
}
