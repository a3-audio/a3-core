"""CMake refuses to build with a JUCE whose FFT is not the patched one.

JUCE's own FFTW plans make FFTW mallocs scratch space on every execute, which breaks the audio callback's no-allocation
rule (EngineAllocation.*, FftBackend.TransformsAllocateNothing). cmake/patches/juce-9.0.3-fftw-realtime.patch fixes
it, and has to be applied by hand to every JUCE install that builds the Core. A package built against an unpatched JUCE
would xrun on the rig without a single test having run, so the configure step checks (2026-11-06).
"""

import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "cmake" / "CheckJuceFftPatch.cmake"
PATCH = ROOT / "cmake" / "patches" / "juce-9.0.3-fftw-realtime.patch"
MARKER = "a3-core patch: the real transforms are planned and run out of place"


def check(fft_file, allow_unpatched=False):
    command = ["cmake", f"-DJUCE_FFT_FILE={fft_file}"]
    if allow_unpatched:
        command.append("-DA3_ALLOW_UNPATCHED_JUCE_FFT=ON")
    command += ["-P", str(SCRIPT)]
    return subprocess.run(command, capture_output=True, text=True)


class TheConfigureStepChecksTheJuceFft(unittest.TestCase):
    def setUp(self):
        self.dir = tempfile.TemporaryDirectory()
        self.addCleanup(self.dir.cleanup)
        self.fft = Path(self.dir.name) / "juce_FFT.cpp"

    def test_an_unpatched_file_is_refused_and_the_patch_is_named(self):
        self.fft.write_text("struct FFTWImpl { enum { unaligned = (1 << 1), estimate = (1 << 6) }; };\n")
        result = check(self.fft)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("juce-9.0.3-fftw-realtime.patch", result.stderr)
        self.assertIn("patch -p1", result.stderr)

    def test_the_older_in_place_patch_is_refused_too(self):
        self.fft.write_text("noBuffering = (1 << 11),   // FFTW_NO_BUFFERING ... (a3-core patch)\n")
        self.assertNotEqual(check(self.fft).returncode, 0)

    def test_a_patched_file_passes(self):
        self.fft.write_text(f"// {MARKER}\n")
        result = check(self.fft)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_a_missing_file_is_refused_not_skipped(self):
        result = check(Path(self.dir.name) / "nowhere" / "juce_FFT.cpp")
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("juce_FFT.cpp", result.stderr)

    def test_the_way_out_is_explicit_and_only_a_warning(self):
        self.fft.write_text("// pristine\n")
        result = check(self.fft, allow_unpatched=True)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn("CMake Warning", result.stderr)

    def test_the_marker_is_in_the_patch_the_repo_ships(self):
        self.assertIn(MARKER, PATCH.read_text())


if __name__ == "__main__":
    unittest.main()
