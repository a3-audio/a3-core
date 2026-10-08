#!/usr/bin/env python3
"""Where an OSC address, a port or an IP is still written outside the truth.

Every one of them is written once, in a3-osc.json (decided 2026-09-30), and
every device reads it from there. A literal left in a device's code is a
second truth -- the way the mixer's Core address was, wrong on three branches
at once, and OSC over UDP never said so.

This looks through the code of every repo of the system for string literals
that are one of our addresses or an IP, and for number literals that are one
of the truth's ports. The loaders are allowed their words, and so is anything
named in ALLOWED with the reason; tests, builds and vendored code are not
looked at.
"""

import ast
import re
import sys
from pathlib import Path

#: The repos, by name, and where each sits in the a3-system umbrella's
#: checkout (~/a3-system since 2026-10-04), relative to it.
REPOS = ("a3-core", "a3-motion-ui", "a3-mixer", "beat-analyzer", "stemdeck")
CHECKOUT_PATHS = {"a3-motion-ui": "a3-motion/ui"}


def checkouts(root):
    """Each repo's checkout, beside the a3-core checkout `root`."""
    return {name: root.parent / CHECKOUT_PATHS.get(name, name) for name in REPOS}

CODE = {".py", ".cpp", ".cc", ".h", ".hh", ".service", ".sh"}

#: Never looked at: tests say the words on purpose, builds and vendored code
#: are not ours to hold.
SKIPPED_DIRS = {"tests", "test", "a3-motion-tests", "build", "build-make",
                "external", "JuceLibraryCode", ".git", "__pycache__",
                "node_modules", "hardware", "docs", "cmake-build-debug",
                "cmake-build-release", "patches"}

#: The families of our words. Not `fx`: that family became `filter` on
#: 2026-09-30, and `/fx/` in code is REAPER's (`/track/N/fx/...`).
OUR_WORDS = re.compile(
    r"^/(channel|master|filter|aux-return|stemdeck|vu|beat|tap|clockmode|state|device|core)(/|$)")
IPV4 = re.compile(r"\b(?:\d{1,3}\.){3}\d{1,3}\b")


def _is_second_truth(value):
    if OUR_WORDS.match(value):
        return True
    # "every interface" is an idiom, not a machine; a help text that shows
    # the shape of an answer ("127.0.0.1:PORT") names none either.
    ips = [ip for ip in IPV4.findall(value) if ip != "0.0.0.0"]
    return bool(ips) and ":PORT" not in value

#: path (relative to its repo, with the repo's name first) -> why it may.
ALLOWED = {
    # The readers of the truth: their job is to know its shape.
    "a3-core/platform-config/debian-x86_64/a3-core/home/aaa/.local/lib/a3_osc.py":
        "the truth's reader in Python",
    "a3-motion-ui/src/a3-motion-engine/OscTruth.cc":
        "the truth's reader in Motion",
    "a3-mixer/software/scripts/a3_mixer_osc.py":
        "the truth's reader on the desk",
    "stemdeck/Source/OscTruth.cpp":
        "the truth's reader in StemDeck",
    # The analyzer reads the .env block a3-core renders; an .env from before
    # the block keeps these values, which are the truth's of 2026-09-30.
    "beat-analyzer/include/config/osc_words.h":
        "defaults for an .env without the a3-osc block",
    # The guard names the literals it lets stand.
    "a3-core/tools/second_truth.py": "the guard's own allow-list",
}

#: (path, literal) -> why this one literal may stand where it stands.
ALLOWED_LITERALS = {
    ("a3-core/platform-config/debian-x86_64/a3-core/home/aaa/.local/lib/a3_core_traffic.py", 50000):
        "a count of unknown addresses, not a port",
    ("a3-mixer/software/scripts/a3_mixer_truth.py", 7790):
        "the port every device knows before it has a truth (spec truth-from-core)",
    ("a3-mixer/software/scripts/a3_mixer_truth.py", "/core/here"):
        "the word a device hears before it has a truth (spec truth-from-core)",
    ("stemdeck/Source/TruthKeeper.h", 7790):
        "the port StemDeck knows before it has a truth (spec truth-from-core)",
    ("stemdeck/Source/TruthKeeper.h", "/core/here"):
        "the word StemDeck hears before it has a truth (spec truth-from-core)",
    ("a3-motion-ui/src/a3-motion-engine/TruthKeeper.hh", 7790):
        "the port Motion knows before it has a truth (spec truth-from-core)",
    ("a3-motion-ui/src/a3-motion-engine/TruthKeeper.hh", "/core/here"):
        "the word Motion hears before it has a truth (spec truth-from-core)",
    ("stemdeck/Source/DataPaths.cpp", "/stemdeck"):
        "the last part of StemDeck's data folder, ~/.local/share/stemdeck, not an address",
    ("beat-analyzer/src/main.cpp", "  Beispiel: OSC_HOST_Protokol=127.0.0.1:9000\\n"):
        "the usage text's example of a target line",
}


def _python_literals(path):
    tree = ast.parse(path.read_text())
    docstrings = {id(node.body[0].value) for node in ast.walk(tree)
                  if isinstance(node, (ast.Module, ast.FunctionDef,
                                       ast.AsyncFunctionDef, ast.ClassDef))
                  and node.body and isinstance(node.body[0], ast.Expr)
                  and isinstance(node.body[0].value, ast.Constant)}
    for node in ast.walk(tree):
        if isinstance(node, ast.Constant) and id(node) not in docstrings \
                and not isinstance(node.value, bool):
            yield node.lineno, node.value


_CPP_COMMENT = re.compile(r"//[^\n]*|/\*.*?\*/", re.DOTALL)
_CPP_STRING = re.compile(r'"((?:[^"\\\n]|\\.)*)"')
_CPP_NUMBER = re.compile(r"(?<![\w.])(\d+)(?![\w.])")


def _cpp_literals(path):
    text = path.read_text(errors="replace")
    # Comments out, keeping the line count, so a line number still points.
    text = _CPP_COMMENT.sub(lambda m: re.sub(r"[^\n]", " ", m.group(0)), text)
    for match in _CPP_STRING.finditer(text):
        yield text.count("\n", 0, match.start()) + 1, match.group(1)
    code = _CPP_STRING.sub('""', text)
    for match in _CPP_NUMBER.finditer(code):
        yield code.count("\n", 0, match.start()) + 1, int(match.group(1))


def _unit_literals(path):
    """The words of a systemd unit's Exec lines: that is where a unit names
    an address -- StemDeck's zita-j2n named Core's old one for days."""
    for number, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
        if not line.startswith("Exec"):
            continue
        for word in line.split("=", 1)[1].split():
            yield number, int(word) if word.isdigit() else word


def _shell_literals(path):
    """The words of a shell script, comment lines left out: a3vnc.sh named
    the Core's address for years where the guard did not look (a3-core#63)."""
    for number, line in enumerate(path.read_text(errors="replace").splitlines(), 1):
        if line.lstrip().startswith("#"):
            continue
        for word in re.split(r"[\s\"'=]+", line):
            if word:
                yield number, int(word) if word.isdigit() else word


def literals(path):
    reader = {".py": _python_literals,
              ".service": _unit_literals,
              ".sh": _shell_literals}.get(path.suffix, _cpp_literals)
    try:
        yield from reader(path)
    except (SyntaxError, UnicodeDecodeError):
        return


def code_files(repo):
    for path in sorted(repo.rglob("*")):
        if path.suffix in CODE and path.is_file() and not path.is_symlink() \
                and not SKIPPED_DIRS & set(path.relative_to(repo).parts):
            yield path


def findings(repos, ports):
    """(repo-relative path, line, literal) for every second truth.

    `repos` maps a repo's name to its checkout; `ports` are the truth's."""
    found = []
    for name, root in sorted(repos.items()):
        for path in code_files(root):
            label = f"{name}/{path.relative_to(root)}"
            if label in ALLOWED:
                continue
            for line, value in literals(path):
                if (label, value) in ALLOWED_LITERALS:
                    continue
                if isinstance(value, str) and _is_second_truth(value):
                    found.append((label, line, value))
                elif isinstance(value, int) and value in ports:
                    found.append((label, line, value))
    return found


def truth_ports(truth):
    return {listener["port"] for listener in truth.listeners()}


def main(argv):
    root = Path(__file__).resolve().parents[1]
    sys.path.insert(0, str(root / "platform-config/debian-x86_64/a3-core/home/aaa/.local/lib"))
    import a3_osc
    truth = a3_osc.load(root / "platform-config/debian-x86_64/a3-core/usr/share/a3/a3-osc.json")
    repos = dict(arg.split("=", 1) for arg in argv) if argv else \
        checkouts(root)
    for label, line, value in findings({k: Path(v) for k, v in repos.items()},
                                       truth_ports(truth)):
        print(f"{label}:{line}: {value!r}")
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
