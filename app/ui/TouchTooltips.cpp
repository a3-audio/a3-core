// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "TouchTooltips.h"

namespace a3app
{
namespace
{
const juce::Identifier adoptedTip { "a3AdoptedTooltip" };
constexpr int tickMs = 100;
}

void TipPolicy::release (const void* owner, const juce::String& tip, juce::uint32 now)
{
    pending = {};
    if (owner != nullptr && tip.isNotEmpty())
        pending = { owner, tip, now };
}

juce::String TipPolicy::tick (const void* under, juce::uint32 now)
{
    if (pending.owner == nullptr)
        return {};
    const auto age = now - pending.releasedAt;
    if (under != pending.owner || age >= delayMs + lifetimeMs)
    {
        pending = {};   // once gone, gone until the next release: nothing flickers back
        return {};
    }
    return age >= delayMs ? pending.tip : juce::String();
}

juce::String helpFor (juce::Component* component, juce::Component*& owner)
{
    for (auto* c = component; c != nullptr; c = c->getParentComponent())
    {
        juce::String tip;
        if (auto* client = dynamic_cast<juce::TooltipClient*> (c))
            tip = client->getTooltip();
        if (tip.isEmpty())
            tip = c->getProperties()[adoptedTip].toString();
        if (tip.isNotEmpty())
        {
            owner = c;
            return tip;
        }
    }
    owner = nullptr;
    return {};
}

void adoptTooltips (juce::Component& root)
{
    for (auto* child : root.getChildren())
    {
        if (auto* client = dynamic_cast<juce::SettableTooltipClient*> (child))
            if (const auto tip = client->getTooltip(); tip.isNotEmpty())
            {
                child->getProperties().set (adoptedTip, tip);
                client->setTooltip ({});
            }
        adoptTooltips (*child);
    }
}

TouchTooltips::TouchTooltips (juce::Component& p) : parent (p)
{
    setAlwaysOnTop (true);
    setInterceptsMouseClicks (false, false);
    parent.addChildComponent (this);
    juce::Desktop::getInstance().addGlobalMouseListener (this);
    startTimer (tickMs);
}

TouchTooltips::~TouchTooltips()
{
    juce::Desktop::getInstance().removeGlobalMouseListener (this);
}

void TouchTooltips::pressed()
{
    policy.press();
    setVisible (false);
}

void TouchTooltips::released (juce::Component* at, juce::Point<int> inParent, juce::uint32 now)
{
    juce::Component* owner = nullptr;
    const auto tip = helpFor (at, owner);
    anchor = inParent;
    policy.release (owner, tip, now);
}

// Above the finger in the lower half of the window, below it in the upper; centred on it across, inside the window.
void TouchTooltips::tickAt (juce::Component* under, juce::uint32 now)
{
    juce::Component* owner = nullptr;
    helpFor (under, owner);
    const auto tip = policy.tick (owner, now);
    if (tip.isEmpty())
        return setVisible (false);
    if (tip != showing || ! isVisible())
    {
        showing = tip;
        const auto size = getLookAndFeel().getTooltipBounds (tip, {}, parent.getLocalBounds());
        const int fingerClearance = juce::jmax (size.getHeight() / 2, parent.getHeight() / 30);
        const bool above = anchor.y > parent.getHeight() / 2;
        const auto top = above ? anchor.y - fingerClearance - size.getHeight() : anchor.y + fingerClearance;
        setBounds (juce::Rectangle<int> (anchor.x - size.getWidth() / 2, top, size.getWidth(), size.getHeight())
                       .constrainedWithin (parent.getLocalBounds()));
        repaint();
    }
    setVisible (true);
    toFront (false);
}

void TouchTooltips::paint (juce::Graphics& g) { getLookAndFeel().drawTooltip (g, showing, getWidth(), getHeight()); }

juce::Component* TouchTooltips::componentAt (juce::Point<int> inParent) const
{
    auto* found = parent.getComponentAt (inParent);
    return found == this ? nullptr : found;
}

void TouchTooltips::timerCallback()
{
    const auto pointer = parent.getLocalPoint (nullptr, juce::Desktop::getInstance().getMainMouseSource().getScreenPosition().roundToInt());
    tickAt (componentAt (pointer), juce::Time::getMillisecondCounter());
}

void TouchTooltips::mouseDown (const juce::MouseEvent&) { pressed(); }

void TouchTooltips::mouseUp (const juce::MouseEvent& e)
{
    if (! parent.isShowing())
        return;
    const auto inParent = e.getEventRelativeTo (&parent).getPosition();
    if (parent.getLocalBounds().contains (inParent))
        released (componentAt (inParent), inParent, juce::Time::getMillisecondCounter());
}
}
