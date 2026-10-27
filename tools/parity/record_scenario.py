# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
"""Records what B1's Python Core does on a scenario, for the C++ Core to replay against.

    A3_CORE_BASELINE=<exported a3-core B1> /home/aaa/.venv/bin/python3 -I \
        tools/parity/record_scenario.py tests/core/scenarios/<name>.json [...]

Writes <name>.expected.json beside each scenario. Each scenario is recorded in a process of its
own: B1's module state (and the patches scenario_core makes) must not carry from one to the next.
The format of both files is in tests/core/scenarios/README.md.
"""
import json
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent


def expected_path(scenario):
    return scenario.with_name(scenario.name.removesuffix(".json") + ".expected.json")


def record_one(scenario):
    sys.path.insert(0, str(HERE))
    from scenario_core import record
    transcript = record(os.environ["A3_CORE_BASELINE"], json.loads(scenario.read_text()))
    expected_path(scenario).write_text(json.dumps(transcript, indent=1, ensure_ascii=False) + "\n")


def main(arguments):
    if not arguments:
        sys.exit(__doc__)
    if len(arguments) == 1:
        record_one(Path(arguments[0]))
        return
    for scenario in arguments:
        subprocess.run([sys.executable, "-I", __file__, scenario], check=True)


if __name__ == "__main__":
    main(sys.argv[1:])
