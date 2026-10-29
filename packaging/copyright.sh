# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Sourced by both recipes (packaging/stage, packaging/app/stage): each package
# carries the repository's copyright file under its own name.

# .reuse/dep5 is already in Debian's machine-readable copyright format; its stanzas name MIT,
# CC-BY-3.0, Apache-2.0 and CC-BY-SA-4.0, so the copyright file must carry their texts as License
# paragraphs (copyright-format 1.0: text indented one space, blank lines as " ."). The GPL and
# CC0 texts are referenced through /usr/share/common-licenses, as Debian does.
install_copyright() {
    local src=$1 stage=$2 package=$3 license
    local copyright="$stage/usr/share/doc/$package/copyright"
    install -D -m 644 "$src/.reuse/dep5" "$copyright"
    for license in MIT CC-BY-3.0 Apache-2.0 CC-BY-SA-4.0; do
        {
            printf '\nLicense: %s\n' "$license"
            sed -e 's/^$/./' -e 's/^/ /' "$src/LICENSES/$license.txt"
        } >> "$copyright"
    done
}
