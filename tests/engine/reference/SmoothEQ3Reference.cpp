// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Test-only: the unchanged upstream SmoothEQ3 (MIT, (c) 2018 Chris Johnson).
#include "WrappedReference.h"
#include "airwindows/SmoothEQ3/SmoothEQ3.h"

std::unique_ptr<a3ref::Reference> a3ref::makeSmoothEQ3 (float sampleRate)
{
    return std::make_unique<Wrapped<SmoothEQ3>> (sampleRate);
}
