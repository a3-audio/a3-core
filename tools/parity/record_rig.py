# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
"""Records what B1's Python Core does with the rig's own state files, for RigFiles.RecallMatchesPython.

    A3_CORE_BASELINE=<exported a3-core B1> A3_RIG_STATE=<dir with the rig's state files> \
        /home/aaa/.venv/bin/python3 -I tools/parity/record_rig.py <out dir outside the repo>

Writes <out>/startup-on-rig-files.json (tests/core/rig's steps with the rig's files written in
verbatim) and records <out>/startup-on-rig-files.expected.json from it with record_scenario.py.
The rig's files are private: the out dir must lie outside the repo. Then run the test with
A3_RIG_STATE as above and A3_RIG_EXPECTED=<out>/startup-on-rig-files.expected.json.
"""
import json
import os
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent.parent
NAME = "startup-on-rig-files"
STATE_FILES = ("state.json", "evening.json", "seen.json")


def composed(rig_state):
    scenario = json.loads((REPO / "tests/core/rig" / f"{NAME}.json").read_text())
    scenario["files"] = {name: (rig_state / name).read_text()
                         for name in STATE_FILES if (rig_state / name).is_file()}
    return scenario


def main(arguments):
    if len(arguments) != 1:
        sys.exit(__doc__)
    out = Path(arguments[0]).resolve()
    if out == REPO or REPO in out.parents:
        sys.exit(f"{out} lies inside the repo; the rig's files are private")
    out.mkdir(parents=True, exist_ok=True)
    scenario = out / f"{NAME}.json"
    scenario.write_text(json.dumps(composed(Path(os.environ["A3_RIG_STATE"])), indent=1,
                                   ensure_ascii=False) + "\n")
    subprocess.run([sys.executable, "-I", str(HERE / "record_scenario.py"), str(scenario)], check=True)
    print(out / f"{NAME}.expected.json")


if __name__ == "__main__":
    main(sys.argv[1:])
