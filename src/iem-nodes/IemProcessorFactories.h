// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once
#include "IemNode.h"

// One factory per IEM plug-in, each in the only TU that includes that plug-in's
// PluginProcessor.h. A processor whose bus layout is refused yields empty Parts.
namespace a3::iem
{
IemNode::Parts createMultiEncoder (int numInputs, int numOutputs);
IemNode::Parts createSimpleDecoder (int numInputs, int numOutputs);
IemNode::Parts createBinauralDecoder (int numInputs, int numOutputs);
IemNode::Parts createDualDelay (int numInputs, int numOutputs);
}
