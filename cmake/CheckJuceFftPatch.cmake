# SPDX-FileCopyrightText: 2026 Raphael Eismann
# SPDX-License-Identifier: GPL-3.0-or-later
# Refuses to configure against a JUCE whose FFT is not the patched one.
#
# JUCE plans its FFTW transforms in a way that makes FFTW malloc scratch space on every execute, which breaks the audio
# callback's no-allocation rule. cmake/patches/juce-9.0.3-fftw-realtime.patch fixes that; it is applied by hand to every
# JUCE install that builds the Core, so a package built on an unpatched one would xrun on the rig without a test having
# run. Usable as a function (a3_require_patched_juce_fft <juce_dsp target>) or as a script for the tests:
#   cmake -DJUCE_FFT_FILE=<path to juce_FFT.cpp> [-DA3_ALLOW_UNPATCHED_JUCE_FFT=ON] -P cmake/CheckJuceFftPatch.cmake

set(A3_JUCE_FFT_PATCH_MARKER "a3-core patch: the real transforms are planned and run out of place")

function(a3_check_juce_fft_patch fft_file)
    if(NOT EXISTS "${fft_file}")
        message(FATAL_ERROR "Cannot check JUCE's FFT: ${fft_file} (juce_FFT.cpp) does not exist.")
    endif()
    file(READ "${fft_file}" fft_text)
    string(FIND "${fft_text}" "${A3_JUCE_FFT_PATCH_MARKER}" marker_at)
    if(NOT marker_at EQUAL -1)
        return()
    endif()
    set(how_to_fix_text "${fft_file} is not the patched juce_FFT.cpp.\n")
    string(APPEND how_to_fix_text "The Core runs JUCE's FFT on FFTW, and unpatched JUCE makes it allocate in the audio callback (xruns on the rig).\n")
    string(APPEND how_to_fix_text "Apply cmake/patches/juce-9.0.3-fftw-realtime.patch to this JUCE install, once:\n")
    string(APPEND how_to_fix_text "  patch -p1 -d <JUCE prefix>/include/JUCE-9.0.3 < cmake/patches/juce-9.0.3-fftw-realtime.patch\n")
    string(APPEND how_to_fix_text "(the older juce-9.0.3-fftw-no-buffering.patch is not enough: take it back with patch -R first).")
    if(A3_ALLOW_UNPATCHED_JUCE_FFT)
        message(WARNING "${how_to_fix_text}\nContinuing because A3_ALLOW_UNPATCHED_JUCE_FFT is ON; "
            "EngineAllocation.* and FftBackend.TransformsAllocateNothing will fail.")
        return()
    endif()
    message(FATAL_ERROR "${how_to_fix_text}\n(A3_ALLOW_UNPATCHED_JUCE_FFT=ON builds anyway, for a machine that only runs tests.)")
endfunction()

# The file the juce_dsp module is compiled from: found through the target's include directories.
function(a3_require_patched_juce_fft dsp_target)
    get_target_property(module_dirs ${dsp_target} INTERFACE_INCLUDE_DIRECTORIES)
    foreach(dir IN LISTS module_dirs)
        if(EXISTS "${dir}/juce_dsp/frequency/juce_FFT.cpp")
            a3_check_juce_fft_patch("${dir}/juce_dsp/frequency/juce_FFT.cpp")
            return()
        endif()
    endforeach()
    message(FATAL_ERROR "Cannot find juce_dsp/frequency/juce_FFT.cpp in ${dsp_target}'s include directories (${module_dirs}), "
        "so JUCE's FFT patch cannot be checked.")
endfunction()

if(CMAKE_SCRIPT_MODE_FILE)
    a3_check_juce_fft_patch("${JUCE_FFT_FILE}")
endif()
