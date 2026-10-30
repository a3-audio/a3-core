// SPDX-FileCopyrightText: 2026 Raphael Eismann
// SPDX-License-Identifier: GPL-3.0-or-later
#include "MonitorBenchPage.h"

namespace a3app
{
namespace
{
// Where the bench may send: the destinations Core's POST /api/send takes (P7).
const juce::StringArray destinations { "self", "mixer", "motion" };

}

MonitorBenchPage::MonitorBenchPage (CoreClient& c) : client (c)
{
    to.addItemList (destinations, 1);
    to.setSelectedItemIndex (0, juce::dontSendNotification);
    styleField (address, "an OSC address");
    styleField (value, "1, 0.5 or \"text\"");
    recallButton.setButtonText (juce::String::fromUTF8 (client.recallAddress().c_str()));
    sendButton.onClick = [this] { sendNow(); };
    recallButton.onClick = [this] { recallNow(); };
    value.onReturnKey = [this] { sendNow(); };
    look::caption (toCaption);
    look::caption (addressCaption);
    look::caption (valueCaption);
    look::say (toCaption, "TO");
    look::say (addressCaption, "ADDRESS");
    look::say (valueCaption, "VALUE");
    look::caption (answer);
    look::say (answer, client.url());
    for (auto* child : std::initializer_list<juce::Component*> { &to, &address, &value, &sendButton, &recallButton, &toCaption,
                                                                 &addressCaption, &valueCaption, &answer })
        addAndMakeVisible (child);
}

MonitorBenchPage::~MonitorBenchPage() = default;

void MonitorBenchPage::setTo (const juce::String& name) { to.setSelectedItemIndex (juce::jmax (0, destinations.indexOf (name)), juce::dontSendNotification); }
void MonitorBenchPage::setAddress (const juce::String& text) { address.setText (text, false); }
void MonitorBenchPage::setValue (const juce::String& text) { value.setText (text, false); }

void MonitorBenchPage::sendNow()
{
    show (client.send (to.getText().toStdString(), address.getText().toStdString(), value.getText().toStdString()));
}

void MonitorBenchPage::recallNow() { show (client.recall()); }

void MonitorBenchPage::show (const juce::Result& result)
{
    refused = result.failed();
    if (refused)
    {
        look::failure (answer);
        look::say (answer, "refused: " + result.getErrorMessage());
        return;
    }
    look::value (answer);
    look::say (answer, "sent: " + client.lastAnswer());
}

void MonitorBenchPage::paint (juce::Graphics& g) { g.fillAll (palette::surface); }

void MonitorBenchPage::resized()
{
    auto area = getLocalBounds().reduced (getWidth() / 40);
    const int row = look::rowHeight (*this);
    const int gap = row / 4;
    auto captions = area.removeFromTop (row);
    auto fields = area.removeFromTop (row);
    const int toWidth = fields.getWidth() / 7, addressWidth = fields.getWidth() * 3 / 7, valueWidth = fields.getWidth() / 5;
    toCaption.setBounds (captions.removeFromLeft (toWidth + gap));
    addressCaption.setBounds (captions.removeFromLeft (addressWidth + gap));
    valueCaption.setBounds (captions.removeFromLeft (valueWidth));
    to.setBounds (fields.removeFromLeft (toWidth));
    fields.removeFromLeft (gap);
    address.setBounds (fields.removeFromLeft (addressWidth));
    fields.removeFromLeft (gap);
    value.setBounds (fields.removeFromLeft (valueWidth));
    fields.removeFromLeft (gap);
    sendButton.setBounds (fields);
    fitField (address);
    fitField (value);
    area.removeFromTop (gap * 2);
    recallButton.setBounds (area.removeFromTop (row).removeFromLeft (toWidth + addressWidth / 2));
    area.removeFromTop (gap * 2);
    answer.setBounds (area.removeFromTop (row * 2));
}
}
