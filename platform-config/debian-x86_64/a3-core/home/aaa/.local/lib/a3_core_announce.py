"""Core says where the truth is (spec truth-from-core, 2026-10-02): every
2 s, by UDP broadcast on the LAN, /core/here with the URL of the joined
truth and its fingerprint. A device that hears a fingerprint it does not
have fetches the truth and restarts with it."""

import ipaddress

from pythonosc.osc_message_builder import OscMessageBuilder

EVERY_SECONDS = 2.0
#: All ones: IPv4's limited broadcast, a constant of the protocol rather than
#: a fact about the rig -- computed, so the guard against addresses written
#: into the code keeps meaning what it says.
LIMITED_BROADCAST = str(ipaddress.IPv4Address(2 ** 32 - 1))


def broadcast_address(network):
    """The broadcast address of Core's subnet; the limited broadcast for a
    host route, a bare address or anything that does not parse."""
    try:
        interface = ipaddress.ip_interface(network["address"])
    except (KeyError, ValueError):
        return LIMITED_BROADCAST
    if interface.network.prefixlen >= 31:
        return LIMITED_BROADCAST
    return str(interface.network.broadcast_address)


def truth_url(truth):
    return f"http://{truth.host('core')}:{truth.port('core', 'web')}/api/truth"


def announcement(truth):
    builder = OscMessageBuilder(address=truth.address("core.here"))
    builder.add_arg(truth_url(truth))
    builder.add_arg(truth.fingerprint())
    return builder.build().dgram


def announce(sock, target, packet, every, sleep, running, report):
    """Send `packet` to `target` every `every` seconds while `running()`. A
    failed send -- no network yet at boot -- is reported, the loop goes on."""
    while running():
        try:
            sock.sendto(packet, target)
        except OSError as problem:
            report(f"announce to {target}: {problem}")
        sleep(every)
