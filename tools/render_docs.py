#!/usr/bin/env python3
"""a3-doc's OSC and port tables, rendered from the one truth.

The OSC reference was wrong in four places on 2026-09-29 -- each a copy that
had not followed a change. Its tables are a view of a3-osc.json now: this
writes them between markers in a3-doc's pages, and the prose around them
stays the writers'.

  <!-- a3-osc:addresses -->   every address of ours: pattern, types, who
  <!-- /a3-osc:addresses -->  sends it, who hears it, what it means
  <!-- a3-osc:vu -->          the meters: /vu/N and what each measures
  <!-- a3-osc:ports -->        who listens where

Usage: render_docs.py [A3_DOC_CHECKOUT]   (default: web/a3-doc beside a3-core)
"""

import re
import sys
from pathlib import Path

#: The pages that carry tables, relative to an a3-doc checkout.
PAGES = ("src/ressources/osc.md", "src/ressources/ports.md")


def _row(cells):
    return "| " + " | ".join(cells) + " |"


def addresses_table(truth):
    rows = [_row(["Address", "Types", "From", "To", "Meaning"]),
            _row(["---"] * 5)]
    for key, entry in truth.addresses().items():
        told = entry.get("relayed_to", []) + entry.get("announced_to", [])
        to = ", ".join(entry["to"])
        if told:
            to += " (Core passes it on to " + ", ".join(told) + ")"
        rows.append(_row([f"`{entry['pattern']}`", entry.get("args", "") or "-",
                          ", ".join(entry["from"]), to, entry["meaning"]]))
    return "\n".join(rows)


def vu_table(truth):
    pattern = truth.pattern("vu")
    rows = [_row(["Address", "Meter"]), _row(["---"] * 2)]
    for number, name in enumerate(truth.vu_meters(), start=1):
        rows.append(_row([f"`{pattern.replace('{n}', str(number))}`", name]))
    return "\n".join(rows)


def ports_table(truth):
    rows = [_row(["Program", "Role", "Host", "Port", "Carries"]),
            _row(["---"] * 5)]
    for listener in truth.listeners():
        host = listener["host"]
        address = truth.host(host)
        rows.append(_row([listener["program"], listener["role"],
                          f"{host} ({address})", str(listener["port"]),
                          listener.get("carries", "")]))
    return "\n".join(rows)


TABLES = {"addresses": addresses_table, "vu": vu_table, "ports": ports_table}


def put_tables(text, truth):
    """`text` with every marked table replaced by its render; nothing else
    changes, and a page without markers comes back as it was."""
    for name, render in TABLES.items():
        begin, end = f"<!-- a3-osc:{name} -->", f"<!-- /a3-osc:{name} -->"
        block = re.compile(re.escape(begin) + r".*?" + re.escape(end), re.DOTALL)
        text = block.sub(lambda _m: f"{begin}\n{render(truth)}\n{end}", text)
    return text


def main(argv):
    root = Path(__file__).resolve().parents[1]
    sys.path.insert(0, str(root / "platform-config/debian-x86_64/a3-core/home/aaa/.local/lib"))
    import a3_osc
    truth = a3_osc.load(root / "platform-config/debian-x86_64/a3-core/usr/share/a3/a3-osc.json")
    doc = Path(argv[0]) if argv else root.parent / "web" / "a3-doc"
    for page in PAGES:
        path = doc / page
        before = path.read_text()
        after = put_tables(before, truth)
        if after != before:
            path.write_text(after)
            print(f"rendered {path}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
