cmake_minimum_required(VERSION 3.16)

# Clang is the reference compiler for this port: it accepts the `#import`
# directive and the Apple-flavoured idioms in the upstream sources without
# argument. Override with -DCMAKE_CXX_COMPILER=... if you prefer GCC, which
# also compiles these sources -- it just spells the warning switches
# differently (see the compile options on synthone_core below).
#
# Skipped when a toolchain file is in play: cross-compiling to Windows names
# its own compilers, and picking up the host's clang there would be wrong.
if(NOT CMAKE_TOOLCHAIN_FILE)
    if(NOT DEFINED CMAKE_C_COMPILER AND NOT DEFINED ENV{CC})
        find_program(CLANG_C clang)
        if(CLANG_C)
            set(CMAKE_C_COMPILER "${CLANG_C}")
        endif()
    endif()
    if(NOT DEFINED CMAKE_CXX_COMPILER AND NOT DEFINED ENV{CXX})
        find_program(CLANG_CXX clang++)
        if(CLANG_CXX)
            set(CMAKE_CXX_COMPILER "${CLANG_CXX}")
        endif()
    endif()
endif()

project(synthone_linux LANGUAGES C CXX)

#
# AudioKit Synth One - Linux and Windows port
#
# Builds the original Synth One DSP kernel against a portable compatibility
# layer (linux/compat) instead of AudioKit/CoreAudio, and hosts it on:
#
#   Linux    JACK and/or PortAudio out, ALSA sequencer MIDI in
#   Windows  PortAudio out (WASAPI/WMME/DirectSound), WinMM MIDI in
#
# The Apple sources under ../AudioKitSynthOne/DSP are compiled in place; only
# the three files whose held-note bookkeeping is written in Objective-C are
# replaced (see linux/src/S1DSPKernel+*.cpp).
#
# Windows binaries are cross-compiled from Linux with MinGW-w64; see
# cmake/toolchain-x86_64-w64-mingw32.cmake and build-windows.sh. Nothing here
# is Windows-native -- there is no MSVC path.
#

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_C_STANDARD 11)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

set(CMAKE_CXX_FLAGS_RELEASE "-O2 -g")
set(CMAKE_C_FLAGS_RELEASE "-O2 -g")

find_package(Threads REQUIRED)

# ---------------------------------------------------------------------------
# Fetching third-party sources
#
# Dependencies are fetched into the CMake binary dir at configure time, never
# into the source tree. A shallow fetch of one revision is used rather than
# FetchContent so an offline build can point -D<NAME>_ROOT at a prebuilt tree
# and skip the network entirely.
# ---------------------------------------------------------------------------

function(s1_fetch_git out_dir name url rev sentinel)
    string(TOUPPER "${name}" upper)
    set(root "${${upper}_ROOT}")
    if(root)
        set(dir "${root}")
    else()
        set(dir "${CMAKE_BINARY_DIR}/_deps/${name}")
    endif()

    if(NOT EXISTS "${dir}/${sentinel}")
        message(STATUS "Fetching ${name} into ${dir}")
        execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory "${dir}")
        execute_process(COMMAND git init -q .
            COMMAND_ERROR_IS_FATAL ANY WORKING_DIRECTORY "${dir}")
        execute_process(COMMAND git fetch --depth 1 -q "${url}" "${rev}"
            COMMAND_ERROR_IS_FATAL ANY WORKING_DIRECTORY "${dir}")
        execute_process(COMMAND git checkout -q FETCH_HEAD
            COMMAND_ERROR_IS_FATAL ANY WORKING_DIRECTORY "${dir}")
    endif()

    set(${out_dir} "${dir}" PARENT_SCOPE)
endfunction()

# ---------------------------------------------------------------------------
# Soundpipe
#
# Not packaged on Arch, so it is fetched and built into the CMake binary dir at
# configure time. Point -DSOUNDPIPE_ROOT at a prebuilt tree to build offline.
# ---------------------------------------------------------------------------

set(SOUNDPIPE_ROOT "" CACHE PATH "Prebuilt Soundpipe tree (skips fetching)")
s1_fetch_git(SOUNDPIPE_DIR soundpipe
    "https://github.com/PaulBatchelor/Soundpipe.git"
    "3efb43bdabd0ed23b17c694292b5a79f1692a3ea"
    "Makefile")

# Soundpipe builds with its own Makefile, so cross-compiling means handing it
# the target's compiler and archiver explicitly -- it has no notion of a
# toolchain file. Nothing else about it is host-specific.
set(_sp_make_args "")
if(CMAKE_CROSSCOMPILING)
    list(APPEND _sp_make_args
        "CC=${CMAKE_C_COMPILER}"
        "AR=${CMAKE_AR}"
        "RANLIB=${CMAKE_RANLIB}")
endif()

if(NOT EXISTS "${SOUNDPIPE_DIR}/libsoundpipe.a")
    message(STATUS "Building Soundpipe")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E copy_if_different config.def.mk config.mk
        COMMAND_ERROR_IS_FATAL ANY
        WORKING_DIRECTORY "${SOUNDPIPE_DIR}")
    # NO_LIBSNDFILE keeps Soundpipe free of external dependencies; every module
    # Synth One uses is in the dependency-free set.
    include(ProcessorCount)
    ProcessorCount(_sp_jobs)
    if(_sp_jobs EQUAL 0)
        set(_sp_jobs 4)
    endif()
    # Only libsoundpipe.a: the default target also builds util/wav2smp, which is
    # of no use here and does not cross-compile cleanly.
    execute_process(
        COMMAND make NO_LIBSNDFILE=1 ${_sp_make_args} -j${_sp_jobs} libsoundpipe.a
        RESULT_VARIABLE _sp_build_rc
        OUTPUT_QUIET
        WORKING_DIRECTORY "${SOUNDPIPE_DIR}")
    if(NOT _sp_build_rc EQUAL 0)
        message(FATAL_ERROR "Soundpipe build failed (rc=${_sp_build_rc})")
    endif()
endif()

add_library(soundpipe STATIC IMPORTED GLOBAL)
set_target_properties(soundpipe PROPERTIES
    IMPORTED_LOCATION "${SOUNDPIPE_DIR}/libsoundpipe.a"
    INTERFACE_INCLUDE_DIRECTORIES "${SOUNDPIPE_DIR}/h")

# ---------------------------------------------------------------------------
# Paths into the iOS source tree
# ---------------------------------------------------------------------------

set(S1_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/..")
set(DSP "${S1_ROOT}/AudioKitSynthOne/DSP")

if(NOT EXISTS "${DSP}/Kernel/S1DSPKernel.hpp")
    message(FATAL_ERROR "Cannot find the Synth One DSP sources at ${DSP}. "
                        "Configure this directory from inside a checkout of the app.")
endif()

# ---------------------------------------------------------------------------
# synthone_core: the DSP engine
# ---------------------------------------------------------------------------

# Original Apple sources, compiled as plain C++ against linux/compat.
set(S1_UPSTREAM_SOURCES
    "${DSP}/Kernel/S1DSPKernel.mm"
    "${DSP}/Kernel/S1DSPKernel+process.mm"
    "${DSP}/Kernel/S1DSPKernel+toggleKeys.mm"
    "${DSP}/Kernel/S1DSPKernel+MIDI.mm"
    "${DSP}/Kernel/S1DSPKernel+destroy.mm"
    "${DSP}/Kernel/S1DSPKernel+setup.mm"
    "${DSP}/Kernel/S1DSPKernel+parameters.mm"
    "${DSP}/Kernel/S1DSPKernel+tapers.mm"
    "${DSP}/Kernel/S1DSPKernel+getSetSynthParameters.mm"
    "${DSP}/Kernel/S1DSPKernel+getSetDependentParameters.mm"
    "${DSP}/Note State/S1NoteState.mm"
    "${DSP}/Sequencer/S1Sequencer.mm"
)

# .mm here is Objective-C++ only incidentally; force the C++ frontend.
set_source_files_properties(${S1_UPSTREAM_SOURCES} PROPERTIES
    LANGUAGE CXX
    COMPILE_OPTIONS "-x;c++")

add_library(synthone_core STATIC
    ${S1_UPSTREAM_SOURCES}
    # NB: linux/src/oscmorph2d.c is used in place of DSP/Kernel/oscmorph2d.c,
    # which is a stale 1-D revision that no longer matches its caller.
    src/oscmorph2d.c
    src/s1_port.c
    src/S1AudioUnit.cpp
    src/Json.cpp
    src/Engine.cpp
    src/PlatformPaths.cpp
    "src/S1DSPKernel+startStopNotes.cpp"
    "src/S1DSPKernel+reset.cpp"
    "src/S1DSPKernel+didChanges.cpp"
)

target_include_directories(synthone_core PUBLIC
    # compat must precede the DSP tree so its S1AudioUnit.h / AEArray.h win.
    "${CMAKE_CURRENT_SOURCE_DIR}/compat"
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
    "${DSP}"
    "${DSP}/Kernel"
    "${DSP}/Note State"
    "${DSP}/Rate"
    "${DSP}/Sequencer"
)
# Deliberately absent: ${DSP}/Audio Unit and ${DSP}/TAAE -- the compat versions
# of those headers must be the ones that resolve.

target_link_libraries(synthone_core PUBLIC soundpipe m)

# Wavetables and preset banks are read from the source tree at runtime; this is
# the fallback when --resources is not given.
target_compile_definitions(synthone_core PUBLIC
    "S1_DEFAULT_RESOURCE_DIR=\"${S1_ROOT}/AudioKitSynthOne\""
    "S1_TUNINGS_JSON=\"${CMAKE_CURRENT_SOURCE_DIR}/data/tunings.json\"")

# Xcode-style prefix header: supplies the standard headers the upstream sources
# get transitively from libc++/Foundation on Apple platforms.
target_compile_options(synthone_core PRIVATE
    "-include${CMAKE_CURRENT_SOURCE_DIR}/compat/S1Prefix.h"
    -Wall
    # Upstream idioms that are fine but noisy outside Xcode.
    -Wno-unused-variable
    -Wno-unused-but-set-variable
)

# The two compilers spell the #import and constructor-ordering warnings
# differently, and each rejects the other's switch.
#
# The #import suppression is PUBLIC because S1Parameter.h imports AKInterop.h,
# so every consumer of Engine.h meets the directive too -- keeping it PRIVATE
# leaves the warning firing in host/main.cpp and synthone_offline.cpp.
#
# The constructor-ordering switches are C++-only and warn when handed to the
# C compiler, so they are gated on the language being compiled.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(synthone_core
        PUBLIC  -Wno-import-preprocessor-directive-pedantic  # #import in C++
        PRIVATE -Wno-missing-declarations                    # typedef AK_ENUM(x) {...};
                $<$<COMPILE_LANGUAGE:CXX>:-Wno-reorder-ctor> # kernel ctor init-list order
    )
elseif(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
    target_compile_options(synthone_core
        PUBLIC  -Wno-deprecated                        # GCC's "#import is deprecated"
        PRIVATE $<$<COMPILE_LANGUAGE:CXX>:-Wno-reorder> # kernel ctor init-list order
    )
endif()

# ---------------------------------------------------------------------------
# synthone-offline: hardware-free renderer / smoke test
# ---------------------------------------------------------------------------

add_executable(synthone-offline tools/synthone_offline.cpp)
target_link_libraries(synthone-offline PRIVATE synthone_core)

# ---------------------------------------------------------------------------
# sysex_assembler_test: exercises MidiSysEx.h's SysExAssembler/SysExQueue
# against synthetic byte sequences. Header-only and platform-independent, so
# it needs neither ALSA/WinMM nor any of the audio backends -- builds
# unconditionally, unlike the rest of host/.
# ---------------------------------------------------------------------------

add_executable(sysex_assembler_test tools/sysex_assembler_test.cpp)
target_include_directories(sysex_assembler_test PRIVATE host)

# ---------------------------------------------------------------------------
# Platform dependencies
#
# Linux takes its audio, MIDI and windowing libraries from the system through
# pkg-config. Windows has no such package manager, so PortAudio and GLFW are
# cross-built from source into the binary dir, the way Soundpipe and ImGui
# already are.
#
# Both branches resolve into the same vocabulary, so the target definitions
# below are written once:
#
#   S1_MIDI_SOURCE     the MidiInput and MidiOutput implementations for this platform
#   S1_MIDI_LIBS       what that implementation links against
#   S1_MIDI_AVAILABLE  false when the platform's MIDI library is missing
#   S1_JACK_LIB        JACK, or empty when unavailable
#   S1_PORTAUDIO_LIB   PortAudio, or empty when unavailable
#   S1_GLFW_LIB        GLFW, or empty when unavailable
#   S1_GL_LIB          the OpenGL loader library
# ---------------------------------------------------------------------------

if(WIN32)
    set(S1_MIDI_SOURCE host/WinMidi.cpp host/WinMidiOut.cpp)
    set(S1_MIDI_LIBS winmm)
    set(S1_MIDI_AVAILABLE TRUE)

    # JACK exists on Windows but is a niche install there, and PortAudio already
    # reaches the same drivers; the port does not carry it.
    set(S1_JACK_LIB "")

    # Both trees below predate CMake 4, which dropped compatibility with
    # cmake_minimum_required() values under 3.5 -- PortAudio v19.7.0 asks for
    # 2.8 and GLFW 3.4 for 3.4, so each aborts at project() on a current CMake.
    # Neither is ours to patch and both are pinned to their last stable tags, so
    # tell CMake to treat those declarations as 3.5.
    #
    # Scoped deliberately: it is set immediately before the two fetched
    # subprojects and unset immediately after, so it never relaxes policy
    # checking for our own targets.
    set(CMAKE_POLICY_VERSION_MINIMUM 3.5)

    # PortAudio. WASAPI, WMME, DirectSound and WDMKS all build against the SDK
    # headers MinGW ships, so no external SDK is needed. ASIO is deliberately
    # absent: it needs Steinberg's licensed SDK, which cannot be redistributed.
    set(PORTAUDIO_ROOT "" CACHE PATH "Prebuilt PortAudio tree (skips fetching)")
    s1_fetch_git(PORTAUDIO_DIR portaudio
        "https://github.com/PortAudio/portaudio.git" "v19.7.0" "CMakeLists.txt")
    set(PA_BUILD_SHARED OFF CACHE BOOL "" FORCE)
    set(PA_BUILD_STATIC ON CACHE BOOL "" FORCE)
    set(PA_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(PA_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    add_subdirectory("${PORTAUDIO_DIR}" "${CMAKE_BINARY_DIR}/_build/portaudio" EXCLUDE_FROM_ALL)
    set(S1_PORTAUDIO_LIB portaudio_static)
    message(STATUS "Host backend: PortAudio (cross-built)")

    # GLFW.
    set(GLFW_ROOT "" CACHE PATH "Prebuilt GLFW tree (skips fetching)")
    s1_fetch_git(GLFW_DIR glfw "https://github.com/glfw/glfw.git" "3.4" "CMakeLists.txt")
    set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
    set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
    set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
    add_subdirectory("${GLFW_DIR}" "${CMAKE_BINARY_DIR}/_build/glfw" EXCLUDE_FROM_ALL)
    set(S1_GLFW_LIB glfw)
    set(S1_GL_LIB opengl32)

    unset(CMAKE_POLICY_VERSION_MINIMUM)
else()
    set(S1_MIDI_SOURCE host/AlsaMidi.cpp host/AlsaMidiOut.cpp)

    find_package(PkgConfig REQUIRED)
    pkg_check_modules(JACK IMPORTED_TARGET jack)
    pkg_check_modules(GLFW IMPORTED_TARGET glfw3)
    pkg_check_modules(GL IMPORTED_TARGET gl)
    pkg_check_modules(PORTAUDIO IMPORTED_TARGET portaudio-2.0)
    pkg_check_modules(ALSA IMPORTED_TARGET alsa)

    if(ALSA_FOUND)
        set(S1_MIDI_LIBS PkgConfig::ALSA)
        set(S1_MIDI_AVAILABLE TRUE)
    else()
        set(S1_MIDI_AVAILABLE FALSE)
        message(WARNING "alsa-lib not found -- skipping the standalone host. "
                        "Install alsa-lib to build 'synthone'.")
    endif()

    set(S1_JACK_LIB "")
    if(JACK_FOUND)
        set(S1_JACK_LIB PkgConfig::JACK)
        message(STATUS "Host backend: JACK")
    endif()
    set(S1_PORTAUDIO_LIB "")
    if(PORTAUDIO_FOUND)
        set(S1_PORTAUDIO_LIB PkgConfig::PORTAUDIO)
        message(STATUS "Host backend: PortAudio")
    endif()

    set(S1_GLFW_LIB "")
    set(S1_GL_LIB "")
    if(GLFW_FOUND AND GL_FOUND)
        set(S1_GLFW_LIB PkgConfig::GLFW)
        set(S1_GL_LIB PkgConfig::GL)
    endif()
endif()

# ---------------------------------------------------------------------------
# latency_test: round-trip latency measurement
#
# PortAudio only -- no MIDI, no JACK, no DSP engine -- so it is gated
# separately and builds even where S1_MIDI_AVAILABLE is false.
# ---------------------------------------------------------------------------

if(S1_PORTAUDIO_LIB)
    add_executable(latency_test tools/latency_test.cpp)
    target_link_libraries(latency_test PRIVATE ${S1_PORTAUDIO_LIB})
    target_compile_options(latency_test PRIVATE -Wall)
else()
    message(WARNING "PortAudio not found -- skipping latency_test.")
endif()

# ---------------------------------------------------------------------------
# synthone: standalone host
# ---------------------------------------------------------------------------

if(NOT S1_MIDI_AVAILABLE)
    # already warned about above
elseif(NOT S1_JACK_LIB AND NOT S1_PORTAUDIO_LIB)
    message(WARNING "Neither JACK nor PortAudio found -- skipping the standalone host. "
                    "Install pipewire-jack (or jack2) and/or portaudio.")
else()
    # Everything that both the CLI host and the GUI need in order to make sound.
    add_library(synthone_host STATIC
        host/AudioBackend.cpp
        host/ctrldev/ControllerDriverManager.cpp
        host/ctrldev/AkaiMpkMiniMk3.cpp
        ${S1_MIDI_SOURCE}
    )
    target_include_directories(synthone_host PUBLIC host host/ctrldev)
    target_link_libraries(synthone_host PUBLIC synthone_core ${S1_MIDI_LIBS} Threads::Threads)
    target_compile_options(synthone_host PRIVATE -Wall)

    if(S1_JACK_LIB)
        target_sources(synthone_host PRIVATE host/JackBackend.cpp)
        target_compile_definitions(synthone_host PUBLIC S1_HAVE_JACK)
        target_link_libraries(synthone_host PUBLIC ${S1_JACK_LIB})
    endif()
    if(S1_PORTAUDIO_LIB)
        target_sources(synthone_host PRIVATE host/PortAudioBackend.cpp)
        target_compile_definitions(synthone_host PUBLIC S1_HAVE_PORTAUDIO)
        target_link_libraries(synthone_host PUBLIC ${S1_PORTAUDIO_LIB})
    endif()

    add_executable(synthone host/main.cpp)
    target_link_libraries(synthone PRIVATE synthone_host)
    target_compile_options(synthone PRIVATE -Wall)

    # -----------------------------------------------------------------------
    # synthone-gui: full graphical front end
    #
    # Dear ImGui is fetched into the binary dir like Soundpipe; nothing
    # third-party is written into the source tree.
    # -----------------------------------------------------------------------

    if(S1_GLFW_LIB AND S1_GL_LIB)
        set(IMGUI_ROOT "" CACHE PATH "Prebuilt Dear ImGui tree (skips fetching)")
        s1_fetch_git(IMGUI_DIR imgui
            "https://github.com/ocornut/imgui.git" "v1.91.5" "imgui.cpp")

        add_library(imgui STATIC
            "${IMGUI_DIR}/imgui.cpp"
            "${IMGUI_DIR}/imgui_draw.cpp"
            "${IMGUI_DIR}/imgui_tables.cpp"
            "${IMGUI_DIR}/imgui_widgets.cpp"
            "${IMGUI_DIR}/backends/imgui_impl_glfw.cpp"
            "${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp"
        )
        target_include_directories(imgui PUBLIC "${IMGUI_DIR}" "${IMGUI_DIR}/backends")
        target_link_libraries(imgui PUBLIC ${S1_GLFW_LIB} ${S1_GL_LIB})

        add_executable(synthone-gui
            gui/main.cpp
            gui/Widgets.cpp
            gui/Panels.cpp
        )
        target_include_directories(synthone-gui PRIVATE gui)
        target_link_libraries(synthone-gui PRIVATE synthone_host imgui)
        target_compile_options(synthone-gui PRIVATE -Wall)

        # Left as a console application on Windows: the GUI reports backend and
        # GLFW failures on stderr, and with -mwindows those go nowhere, which
        # turns "no audio device" into a window that silently does nothing.
        option(S1_WIN32_GUI_CONSOLE "Keep a console attached to synthone-gui on Windows" ON)
        if(WIN32 AND NOT S1_WIN32_GUI_CONSOLE)
            set_target_properties(synthone-gui PROPERTIES WIN32_EXECUTABLE TRUE)
        endif()

        message(STATUS "GUI: Dear ImGui + GLFW")
    else()
        message(WARNING "glfw3/gl not found -- skipping synthone-gui.")
    endif()
endif()
