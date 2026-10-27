// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Test-only: the unchanged upstream Isolator3 (MIT, (c) 2018 Chris Johnson).
#include "WrappedReference.h"
#include "airwindows/Isolator3/Isolator3.h"

std::unique_ptr<a3ref::Reference> a3ref::makeIsolator3 (float sampleRate)
{
    return std::make_unique<Wrapped<Isolator3>> (sampleRate);
}
