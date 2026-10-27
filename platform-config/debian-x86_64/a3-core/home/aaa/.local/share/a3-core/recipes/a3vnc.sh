#!/bin/bash

# A³ Core is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.

# A³ Core is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with A³ Core.  If not, see <https://www.gnu.org/licenses/>.

# © Copyright 2021 Raphael Eismann, Patric Schmitz

# The Core's address and VNC's port come from a3-osc.json (a3-core#63), asked
# of the a3-core program one value at a time. A3_CORE points at another
# program (tests).
core="${A3_CORE:-/usr/bin/a3-core}"
host="$("$core" --truth host core)" || exit 1
port="$("$core" --truth port x11vnc vnc)" || exit 1
vncviewer QualityLevel 2 "${host}::${port}"
