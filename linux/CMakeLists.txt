cmake_minimum_required(VERSION 3.16)

# Clang is the reference compiler for this port: it accepts the `#import`
# directive and the Apple-flavoured idioms in the upstream sources without
# argument. Override with -DCMAKE_CXX_COMPILER=... if you prefer GCC.
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

project(synthone_linux LANGUAGES C CXX)

#
# AudioKit Synth One - Linux x86_64 port
#
# Builds the original Synth One DSP kernel against a portable compatibility
# layer (linux/compat) instead of AudioKit/CoreAudio, and hosts it on
# JACK and/or PortAudio with ALSA sequencer MIDI input.
#
# The Apple sources under ../AudioKitSynthOne/DSP are compiled in place; only
# the three files whose held-note bookkeeping is written in Objective-C are
# replaced (see linux/src/S1DSPKernel+*.cpp).
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
# Soundpipe
#
# Not packaged on Arch, so it is fetched and built into the CMake binary dir at
# configure time. Point -DSOUNDPIPE_ROOT at a prebuilt tree to build offline.
# ---------------------------------------------------------------------------

set(SOUNDPIPE_GIT "https://github.com/PaulBatchelor/Soundpipe.git")
set(SOUNDPIPE_REV "3efb43bdabd0ed23b17c694292b5a79f1692a3ea")
set(SOUNDPIPE_ROOT "" CACHE PATH "Prebuilt Soundpipe tree (skips fetching)")

if(SOUNDPIPE_ROOT STREQUAL "")
    set(SOUNDPIPE_DIR "${CMAKE_BINARY_DIR}/_deps/soundpipe")
else()
    set(SOUNDPIPE_DIR "${SOUNDPIPE_ROOT}")
endif()

if(NOT EXISTS "${SOUNDPIPE_DIR}/Makefile")
    message(STATUS "Fetching Soundpipe into ${SOUNDPIPE_DIR}")
    execute_process(
        COMMAND ${CMAKE_COMMAND} -E make_directory "${SOUNDPIPE_DIR}"
        RESULT_VARIABLE _sp_rc)
    execute_process(
        COMMAND git init -q .
        COMMAND_ERROR_IS_FATAL ANY
        WORKING_DIRECTORY "${SOUNDPIPE_DIR}")
    execute_process(
        COMMAND git fetch --depth 1 -q "${SOUNDPIPE_GIT}" "${SOUNDPIPE_REV}"
        COMMAND_ERROR_IS_FATAL ANY
        WORKING_DIRECTORY "${SOUNDPIPE_DIR}")
    execute_process(
        COMMAND git checkout -q FETCH_HEAD
        COMMAND_ERROR_IS_FATAL ANY
        WORKING_DIRECTORY "${SOUNDPIPE_DIR}")
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
    execute_process(
        COMMAND make NO_LIBSNDFILE=1 -j${_sp_jobs}
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
    -Wno-import-preprocessor-directive-pedantic  # #import in C++
    -Wno-missing-declarations                    # typedef AK_ENUM(x) {...};
    -Wno-reorder-ctor                            # kernel ctor init-list order
    -Wno-unused-variable
    -Wno-unused-but-set-variable
)

# ---------------------------------------------------------------------------
# synthone-offline: hardware-free renderer / smoke test
# ---------------------------------------------------------------------------

add_executable(synthone-offline tools/synthone_offline.cpp)
target_link_libraries(synthone-offline PRIVATE synthone_core)

# ---------------------------------------------------------------------------
# synthone: standalone host (JACK and/or PortAudio out, ALSA seq MIDI in)
# ---------------------------------------------------------------------------

find_package(PkgConfig REQUIRED)
pkg_check_modules(JACK IMPORTED_TARGET jack)
pkg_check_modules(GLFW IMPORTED_TARGET glfw3)
pkg_check_modules(GL IMPORTED_TARGET gl)
pkg_check_modules(PORTAUDIO IMPORTED_TARGET portaudio-2.0)
pkg_check_modules(ALSA IMPORTED_TARGET alsa)

if(NOT ALSA_FOUND)
    message(WARNING "alsa-lib not found -- skipping the standalone host. "
                    "Install alsa-lib to build 'synthone'.")
elseif(NOT JACK_FOUND AND NOT PORTAUDIO_FOUND)
    message(WARNING "Neither JACK nor PortAudio found -- skipping the standalone host. "
                    "Install pipewire-jack (or jack2) and/or portaudio.")
else()
    set(HOST_SOURCES
        host/main.cpp
        host/AudioBackend.cpp
        host/AlsaMidi.cpp
    )
    if(JACK_FOUND)
        list(APPEND HOST_SOURCES host/JackBackend.cpp)
    endif()
    if(PORTAUDIO_FOUND)
        list(APPEND HOST_SOURCES host/PortAudioBackend.cpp)
    endif()

    add_executable(synthone ${HOST_SOURCES})
    target_link_libraries(synthone PRIVATE synthone_core PkgConfig::ALSA Threads::Threads)
    target_compile_options(synthone PRIVATE -Wall)

    if(JACK_FOUND)
        target_link_libraries(synthone PRIVATE PkgConfig::JACK)
        target_compile_definitions(synthone PRIVATE S1_HAVE_JACK)
        message(STATUS "Host backend: JACK")
    endif()
    if(PORTAUDIO_FOUND)
        target_link_libraries(synthone PRIVATE PkgConfig::PORTAUDIO)
        target_compile_definitions(synthone PRIVATE S1_HAVE_PORTAUDIO)
        message(STATUS "Host backend: PortAudio")
    endif()

    # -----------------------------------------------------------------------
    # synthone-gui: full graphical front end
    #
    # Dear ImGui is fetched into the binary dir like Soundpipe; nothing
    # third-party is written into the source tree.
    # -----------------------------------------------------------------------

    if(GLFW_FOUND AND GL_FOUND)
        set(IMGUI_REV "v1.91.5")
        set(IMGUI_DIR "${CMAKE_BINARY_DIR}/_deps/imgui")
        set(IMGUI_ROOT "" CACHE PATH "Prebuilt Dear ImGui tree (skips fetching)")
        if(NOT IMGUI_ROOT STREQUAL "")
            set(IMGUI_DIR "${IMGUI_ROOT}")
        endif()

        if(NOT EXISTS "${IMGUI_DIR}/imgui.cpp")
            message(STATUS "Fetching Dear ImGui into ${IMGUI_DIR}")
            execute_process(COMMAND ${CMAKE_COMMAND} -E make_directory "${IMGUI_DIR}")
            execute_process(COMMAND git init -q .
                COMMAND_ERROR_IS_FATAL ANY WORKING_DIRECTORY "${IMGUI_DIR}")
            execute_process(
                COMMAND git fetch --depth 1 -q https://github.com/ocornut/imgui.git ${IMGUI_REV}
                COMMAND_ERROR_IS_FATAL ANY WORKING_DIRECTORY "${IMGUI_DIR}")
            execute_process(COMMAND git checkout -q FETCH_HEAD
                COMMAND_ERROR_IS_FATAL ANY WORKING_DIRECTORY "${IMGUI_DIR}")
        endif()

        add_library(imgui STATIC
            "${IMGUI_DIR}/imgui.cpp"
            "${IMGUI_DIR}/imgui_draw.cpp"
            "${IMGUI_DIR}/imgui_tables.cpp"
            "${IMGUI_DIR}/imgui_widgets.cpp"
            "${IMGUI_DIR}/backends/imgui_impl_glfw.cpp"
            "${IMGUI_DIR}/backends/imgui_impl_opengl3.cpp"
        )
        target_include_directories(imgui PUBLIC "${IMGUI_DIR}" "${IMGUI_DIR}/backends")
        target_link_libraries(imgui PUBLIC PkgConfig::GLFW PkgConfig::GL)

        add_executable(synthone-gui
            gui/main.cpp
            gui/Widgets.cpp
            gui/Panels.cpp
            host/AudioBackend.cpp
            host/AlsaMidi.cpp
        )
        if(JACK_FOUND)
            target_sources(synthone-gui PRIVATE host/JackBackend.cpp)
            target_compile_definitions(synthone-gui PRIVATE S1_HAVE_JACK)
            target_link_libraries(synthone-gui PRIVATE PkgConfig::JACK)
        endif()
        if(PORTAUDIO_FOUND)
            target_sources(synthone-gui PRIVATE host/PortAudioBackend.cpp)
            target_compile_definitions(synthone-gui PRIVATE S1_HAVE_PORTAUDIO)
            target_link_libraries(synthone-gui PRIVATE PkgConfig::PORTAUDIO)
        endif()
        target_include_directories(synthone-gui PRIVATE host gui)
        target_link_libraries(synthone-gui
            PRIVATE synthone_core imgui PkgConfig::ALSA Threads::Threads)
        target_compile_options(synthone-gui PRIVATE -Wall)
        message(STATUS "GUI: Dear ImGui + GLFW")
    else()
        message(WARNING "glfw3/gl not found -- skipping synthone-gui.")
    endif()
endif()
