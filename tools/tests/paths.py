# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
"""Where the tests find the package: one place, so a move is one edit.

The payload (what dpkg unpacks) stays under platform-config/; the maintainer
scripts and the control file live in packaging/DEBIAN beside the deployment's
recipe (packaging/stage), which installer/package.py reads. The program is a
package of its own, a3-core-app: its recipe is packaging/app (stage, DEBIAN/)."""

from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
PACKAGE = ROOT / "platform-config/debian-x86_64/a3-core"
LOCAL = PACKAGE / "home/aaa/.local"
DEBIAN = ROOT / "packaging/DEBIAN"
APP_RECIPE = ROOT / "packaging/app"
APP_DEBIAN = APP_RECIPE / "DEBIAN"
