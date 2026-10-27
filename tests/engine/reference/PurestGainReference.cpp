// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// Test-only: the unchanged upstream PurestGain (MIT, (c) 2018 Chris Johnson).
#include "WrappedReference.h"
#include "airwindows/PurestGain/PurestGain.h"

std::unique_ptr<a3ref::Reference> a3ref::makePurestGain (float sampleRate)
{
    return std::make_unique<Wrapped<PurestGain>> (sampleRate);
}
