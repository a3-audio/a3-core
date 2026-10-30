// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace a3apptest
{
/** A window without a window system: JUCE's own input routing (sources,
    click counting, hit tests) runs through it, while Xvfb without a window
    manager cannot host a real one (BadAtom). Nothing is drawn. */
class FakePeer : public juce::ComponentPeer
{
public:
    explicit FakePeer (juce::Component& c) : juce::ComponentPeer (c, 0) {}

    using juce::ComponentPeer::globalToLocal;
    using juce::ComponentPeer::localToGlobal;

    void* getNativeHandle() const override { return nullptr; }
    void setVisible (bool) override {}
    void setTitle (const juce::String&) override {}
    void setBounds (const juce::Rectangle<int>&, bool) override {}
    juce::Rectangle<int> getBounds() const override { return component.getBounds(); }
    juce::Point<float> localToGlobal (juce::Point<float> p) override { return p + component.getPosition().toFloat(); }
    juce::Point<float> globalToLocal (juce::Point<float> p) override { return p - component.getPosition().toFloat(); }
    void setMinimised (bool) override {}
    bool isMinimised() const override { return false; }
    bool isShowing() const override { return true; }
    void setFullScreen (bool) override {}
    bool isFullScreen() const override { return false; }
    void setIcon (const juce::Image&) override {}
    bool contains (juce::Point<int> p, bool) const override { return component.getLocalBounds().contains (p); }
    OptionalBorderSize getFrameSizeIfPresent() const override { return {}; }
    juce::BorderSize<int> getFrameSize() const override { return {}; }
    bool setAlwaysOnTop (bool) override { return false; }
    void toFront (bool) override {}
    void toBehind (juce::ComponentPeer*) override {}
    bool isFocused() const override { return false; }
    void grabFocus() override {}
    void repaint (const juce::Rectangle<int>&) override {}
    void performAnyPendingRepaintsNow() override {}
    void setAlpha (float) override {}
    juce::StringArray getAvailableRenderingEngines() override { return {}; }
    void textInputRequired (juce::Point<int>, juce::TextInputTarget&) override {}
};

/** A top-level component whose peer is a FakePeer. */
class PeerHost : public juce::Component
{
public:
    juce::ComponentPeer* createNewPeer (int, void*) override { return new FakePeer (*this); }
};
}
