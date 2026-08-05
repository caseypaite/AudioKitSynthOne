#
# Cross-compile the Linux port for 64-bit Windows with MinGW-w64.
#
#   cmake -S . -B build-windows -G Ninja \
#         -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-x86_64-w64-mingw32.cmake
#
# The toolchain itself is not assumed to be on PATH: point -DMINGW_PREFIX at the
# tree holding usr/bin/x86_64-w64-mingw32-gcc, or put those drivers on PATH and
# leave it unset. build-windows.sh does this for you.
#

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR x86_64)

set(TOOLCHAIN_TRIPLE x86_64-w64-mingw32)

# Look for the drivers under MINGW_PREFIX first, then on PATH.
set(MINGW_PREFIX "" CACHE PATH "Root of an unpacked MinGW-w64 toolchain")

# CMake re-runs this file inside the try_compile subprojects it uses to probe
# the compiler, and those do not inherit cache variables from the outer build.
# Without this, -DMINGW_PREFIX=... is invisible there and the find_program calls
# below fail with "Cannot find x86_64-w64-mingw32-gcc" during project().
list(APPEND CMAKE_TRY_COMPILE_PLATFORM_VARIABLES MINGW_PREFIX)
if(MINGW_PREFIX)
    set(_mingw_bin_hint "${MINGW_PREFIX}/usr/bin" "${MINGW_PREFIX}/bin")
else()
    set(_mingw_bin_hint "")
endif()

foreach(_tool C_COMPILER:gcc CXX_COMPILER:g++ RC_COMPILER:windres)
    string(REPLACE ":" ";" _pair "${_tool}")
    list(GET _pair 0 _var)
    list(GET _pair 1 _exe)
    find_program(_found_${_var} "${TOOLCHAIN_TRIPLE}-${_exe}" HINTS ${_mingw_bin_hint})
    if(NOT _found_${_var})
        message(FATAL_ERROR
            "Cannot find ${TOOLCHAIN_TRIPLE}-${_exe}. Install mingw-w64, or pass "
            "-DMINGW_PREFIX=/path/to/toolchain.")
    endif()
    set(CMAKE_${_var} "${_found_${_var}}")
endforeach()

find_program(CMAKE_AR "${TOOLCHAIN_TRIPLE}-ar" HINTS ${_mingw_bin_hint})
find_program(CMAKE_RANLIB "${TOOLCHAIN_TRIPLE}-ranlib" HINTS ${_mingw_bin_hint})
find_program(CMAKE_STRIP "${TOOLCHAIN_TRIPLE}-strip" HINTS ${_mingw_bin_hint})

# Search the target sysroot for libraries and headers, but keep using host
# programs (git, make, pkg-config) to drive the dependency builds.
get_filename_component(_mingw_bin "${CMAKE_C_COMPILER}" DIRECTORY)
get_filename_component(_mingw_root "${_mingw_bin}" DIRECTORY)
set(CMAKE_FIND_ROOT_PATH "${_mingw_root}/${TOOLCHAIN_TRIPLE}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE ONLY)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE ONLY)

# The dependencies are all cross-built as static libraries, and libgcc/libstdc++
# are linked in statically too, so the binaries need no DLLs beyond what ships
# with Windows itself.
set(CMAKE_EXE_LINKER_FLAGS_INIT "-static-libgcc -static-libstdc++ -static")
