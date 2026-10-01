"""A stem assignment as REAPER messages and as what Core announces.

Every send and analog mute in full: the caller drops what it already sent
(broadcast and the REAPER side both remember), and a start-up replay needs
the full set anyway. Send volume, not send mute: REAPER's OSC has no send
mute (Default.ReaperOSC, 2026-10-01).
"""


def reaper_messages(stems, layout, address):
    out = []
    for pair, (track, sends) in enumerate(layout.pairs, start=1):
        for channel, send in enumerate(sends[:4]):
            on = stems.channel_pair[channel] == pair
            out.append((address("track_send", track=track, send=send),
                        layout.send_unity if on else 0.0))
        out.append((address("track_send", track=track, send=sends[4]),
                    0.0 if stems.muted_on_return(pair) else layout.send_unity))
    for channel, track in enumerate(layout.analog):
        out.append((address("track_mute", track=track),
                    1.0 if stems.channel_pair[channel] else 0.0))
    return out


def announcements(stems, truth):
    out = [(truth.address("channel.stem", ch=c + 1), pair)
           for c, pair in enumerate(stems.channel_pair)]
    muted = int(bool(stems.return_cursor) and stems.muted_on_return(stems.return_cursor))
    out.append((truth.address("fx-return.stem"), [stems.return_cursor, muted]))
    return out


def changed_messages(messages, last_sent):
    """The messages whose value differs from what was last sent, in order.

    `last_sent` is the caller's memory ({address: value}); it is updated with
    the returned pairs, so a second call with the same messages returns [].
    Clear the dict to force a full send."""
    changed = [(address, value) for address, value in messages
               if last_sent.get(address) != value]
    last_sent.update(changed)
    return changed
