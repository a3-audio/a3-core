// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
// JUCE's own X connection and symbols (its documented switch for user code), so no X library of our own is linked.
#define JUCE_GUI_BASICS_INCLUDE_XHEADERS 1
#include "NativeWindow.h"

namespace a3app::native
{
bool viewable (const juce::Component& component)
{
#if JUCE_LINUX || JUCE_BSD
    auto* peer = component.getPeer();
    auto* x = juce::XWindowSystem::getInstanceWithoutCreating();
    if (peer == nullptr || x == nullptr || x->getDisplay() == nullptr)
        return true;
    XWindowAttributes attributes {};
    juce::XWindowSystemUtilities::ScopedXLock lock;
    const auto window = (::Window) reinterpret_cast<std::uintptr_t> (peer->getNativeHandle());
    if (juce::X11Symbols::getInstance()->xGetWindowAttributes (x->getDisplay(), window, &attributes) == 0)
        return true;
    return attributes.map_state == IsViewable;
#else
    juce::ignoreUnused (component);
    return true;
#endif
}
}
