###########################################################################
#   Copyright (C) 1998-2013 by authors (see AUTHORS.txt)                  #
#                                                                         #
#   This file is part of Lux.                                             #
#                                                                         #
#   Lux is free software; you can redistribute it and/or modify           #
#   it under the terms of the GNU General Public License as published by  #
#   the Free Software Foundation; either version 3 of the License, or     #
#   (at your option) any later version.                                   #
#                                                                         #
#   Lux is distributed in the hope that it will be useful,                #
#   but WITHOUT ANY WARRANTY; without even the implied warranty of        #
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         #
#   GNU General Public License for more details.                          #
#                                                                         #
#   You should have received a copy of the GNU General Public License     #
#   along with this program.  If not, see <http://www.gnu.org/licenses/>. #
#                                                                         #
#   Lux website: http://www.luxrender.net                                 #
###########################################################################

MESSAGE(STATUS "CMAKE VERSION DETECTED " ${CMAKE_VERSION})
cmake_minimum_required(VERSION 3.25)

####################
# Set the compiler #
####################

if(DEFINED CMAKE_C_COMPILER AND DEFINED CMAKE_CXX_COMPILER)
    if(CMAKE_CXX_COMPILER MATCHES "icpx$")
        set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIE")
        set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIE")
    endif()
    message(STATUS "Using forwarded C:   ${CMAKE_C_COMPILER}")
    message(STATUS "Using forwarded CXX: ${CMAKE_CXX_COMPILER}")
else()

# Use the Intel compiler if available.
if(DEFINED ENV{ONEAPI_ROOT})
    set(INTEL_CC "$ENV{ONEAPI_ROOT}/compiler/latest/bin/icx")
    set(INTEL_CXX "$ENV{ONEAPI_ROOT}/compiler/latest/bin/icpx")
    set(LLD_LINKER "$ENV{ONEAPI_ROOT}/compiler/latest/bin/lld")
else()
    find_program(INTEL_CC NAMES icx)
    find_program(INTEL_CXX NAMES icpx)
    find_program(LLD_LINKER NAMES lld)
endif()

# Look for system Clang if Intel wasn't found.
if(NOT INTEL_CC)
    find_program(CLANG_CC NAMES clang)
    find_program(CLANG_CXX NAMES clang++)
    if(NOT LLD_LINKER)
        find_program(LLD_LINKER NAMES lld)
    endif()
endif()

if(INTEL_CC AND INTEL_CXX)
    set(CMAKE_C_COMPILER ${INTEL_CC})
    set(CMAKE_CXX_COMPILER ${INTEL_CXX})
    
    # Force LTO code generation to emit Position Independent Code for executables
    set(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fPIE")
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fPIE")

    set(CMAKE_LINKER_TYPE LLD)
    
    message(STATUS "Using Intel OneAPI C: ${CMAKE_C_COMPILER}")
    message(STATUS "Using Intel OneAPI CXX: ${CMAKE_CXX_COMPILER}")
    message(STATUS "Using Intel OneAPI LLD: ${LLD_LINKER}")
    set(USING_CLANG TRUE)
    set(USING_INTEL TRUE)
elseif(CLANG_CC AND CLANG_CXX AND LLD_LINKER)
    set(CMAKE_C_COMPILER ${CLANG_CC})
    set(CMAKE_CXX_COMPILER ${CLANG_CXX})
    set(CMAKE_LINKER_TYPE LLD)
    message(STATUS "Using LLVM Clang: ${CMAKE_C_COMPILER}")
    set(USING_CLANG TRUE)
else()
    message(STATUS "Using GCC")
    set(USING_CLANG FALSE)
endif()

execute_process(COMMAND nproc OUTPUT_VARIABLE NPROC_VALUE OUTPUT_STRIP_TRAILING_WHITESPACE)

endif() # forwarded-compiler guard

#####################
# Setup the project #
#####################

PROJECT(lux)
SET(VERSION 1.9)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

SET(LUX_CMAKE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
SET(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} 
	"${LUX_CMAKE_DIR}"
	"${LUX_CMAKE_DIR}/SpecializedConfig"
)
option(LUX_DOCUMENTATION "Generate project documentation" OFF)

############################
# Use LTO on Clang configs #
############################

# Keyed off the standard compiler-identification variables rather than the
# hand-maintained USING_CLANG flag, so it also holds when the toolchain is
# forwarded from the superbuild (where USING_CLANG is never set). IntelLLVM is
# listed explicitly because icpx reports that ID, not "Clang" - matching the
# previous behaviour where the Intel branch also set USING_CLANG TRUE.
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang"
   OR CMAKE_CXX_COMPILER_ID STREQUAL "IntelLLVM")
    message(STATUS "Enabling ThinLTO (-flto=thin)")
    add_compile_options(-flto=thin)
    add_link_options(-flto=thin)
endif()

##############################
# Set the default build type #
##############################

IF (NOT CMAKE_BUILD_TYPE)
	SET(CMAKE_BUILD_TYPE Release)
ENDIF(NOT CMAKE_BUILD_TYPE)

MESSAGE(STATUS "Building mode: " ${CMAKE_BUILD_TYPE})

# Export symbols on Linux. The LINKER: generator-expression form quotes the
# path correctly under every generator and, unlike the unquoted set() it
# replaces, it never shadows the linker flags the superbuild passes in
# (findings #1, #2, #35). The manual "-rpath,$ORIGIN" that used to be baked in
# here is gone: RPATH is owned by CMAKE_BUILD_WITH_INSTALL_RPATH /
# CMAKE_INSTALL_RPATH below, and having both produced "$ORIGIN:$ORIGIN".
if(CMAKE_SYSTEM_NAME MATCHES "Linux")
	add_link_options(
		"LINKER:--version-script=${CMAKE_SOURCE_DIR}/cmake/exportmaps/linux_symbol_exports.map"
	)
endif()

#############################################################################
######################    FIND PROJECT DEPENDENCIES    ######################
#############################################################################

INCLUDE(Dependencies)

#############################################################################
##########################      LINKER FLAGS     ############################
#############################################################################

# --ignore-data-address-equality is an LLD-only flag that used to be baked into
# CMAKE_<LANG>_USING_LINKER_LLD in the Intel branch only, so it disappeared
# whenever the build fell back to plain Clang (finding #4). Probe it once
# against the linker CMAKE_LINKER_TYPE actually selected, and apply it to every
# target. This must run after project(): the check needs an enabled CXX
# language, and the compiler-selection block above runs before PROJECT().
#
# Two deliberate departures from the plan's snippet, both verified on this
# toolchain:
#   - check_cxx_source_compiles(), not check_cxx_compiler_flag(). The latter
#     only *compiles* its test, so it reports Success even for a nonsense
#     linker flag and would never actually gate on linker support. The
#     source-compiles form links the test (confirmed: a bogus -Wl flag fails,
#     this one succeeds) and uses the -fuse-ld=lld selected by
#     CMAKE_LINKER_TYPE.
#   - the flag is probed in "-Wl,..." form, not "LINKER:...". Generator
#     expressions are not expanded inside the try-compile, so the LINKER: form
#     always reports unsupported and the flag would be dropped again.
include(CheckCXXSourceCompiles)
set(CMAKE_REQUIRED_FLAGS "-Wl,--ignore-data-address-equality")
check_cxx_source_compiles("int main() { return 0; }"
	HAVE_LINKER_IGNORE_DATA_EQ)
set(CMAKE_REQUIRED_FLAGS "")
if(HAVE_LINKER_IGNORE_DATA_EQ)
	add_link_options("-Wl,--ignore-data-address-equality")
endif()

#############################################################################
############################    DOCUMENTATION    ############################
#############################################################################

IF(LUX_DOCUMENTATION)
	INCLUDE(documentation)
ENDIF(LUX_DOCUMENTATION)

#Generate the config.h file
CONFIGURE_FILE(${CMAKE_SOURCE_DIR}/config.h.cmake ${CMAKE_BINARY_DIR}/config.h)
ADD_DEFINITIONS(-DLUX_USE_CONFIG_H)

#############################################################################
#########################      LUXRAYS MERGE      ###########################
#############################################################################

# Configure the LuxRays version header.
SET(LUXRAYS_VERSION_MAJOR 1)
SET(LUXRAYS_VERSION_MINOR 9)
CONFIGURE_FILE(
	"${CMAKE_SOURCE_DIR}/luxrays/include/luxrays/cfg.h.in"
	"${CMAKE_BINARY_DIR}/luxrays/include/luxrays/cfg.h"
	@ONLY
)

# LuxRays headers are included via the "luxrays/..." prefix.
INCLUDE_DIRECTORIES(BEFORE SYSTEM "${CMAKE_SOURCE_DIR}/luxrays/include")
# The generated cfg.h lives in the build tree.
INCLUDE_DIRECTORIES(BEFORE SYSTEM "${CMAKE_BINARY_DIR}/luxrays/include")

# Do not build OpenCL support.
ADD_DEFINITIONS("-DLUXRAYS_DISABLE_OPENCL")

#############################################################################
#########################      COMPILER FLAGS     ###########################
#############################################################################

# Dade - default compiler options
# Removed "-O3" from definitions in order to have CMake Release/Debug options to work
ADD_DEFINITIONS(-march=native -ftree-vectorize -funroll-loops -fPIC -DHAVE_PTHREAD_H)
add_compile_options(-w)

# Look for the highest supported AVX version.
FIND_PACKAGE(AVX)

set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -fvisibility=hidden -fvisibility-inlines-hidden")

IF(AVX_FOUND)
    MESSAGE(STATUS "AVX ${AVX_VERSION} detected (${AVX_STR}). Adding compiler definitions.")
    
    # Use the flags determined by the FindAVX script
    # This will include -mavx512f if on an AVX-512 machine,
	# -mavx2 on an AVX2 machine, or -mavx on an AVX machine.
    ADD_DEFINITIONS(${AVX_FLAGS})
    
ELSE()
    MESSAGE(STATUS "No AVX support. Reverting to SSE.")
    ADD_DEFINITIONS(-msse2 -mfpmath=sse -march=nocona)
ENDIF()

# RelWithDebInfo defaults to -O2; upgrade to -O3 so profiling builds are as
# fast as Release. For Release CMake already uses -O3 by default.
string(REPLACE "-O2" "-O3" CMAKE_CXX_FLAGS_RELWITHDEBINFO "${CMAKE_CXX_FLAGS_RELWITHDEBINFO}")

# The QBVH accelerator needs to be compiled with much reduced optimizations
# otherwise gcc produces incorrect code and ruins the render on 64bits machines
SET_SOURCE_FILES_PROPERTIES(accelerators/qbvhaccel.cpp COMPILE_FLAGS "-O1")
# Safe fast-math subset for triangle intersection (-ffinite-math-only is intentionally excluded).
SET_SOURCE_FILES_PROPERTIES(shapes/meshbarytriangle.cpp COMPILE_FLAGS "-fno-math-errno -fno-trapping-math -fassociative-math")

#############################################################################
#####################            LINKER INFO           ######################
#############################################################################

LINK_DIRECTORIES(${LINK_DIRECTORIES} ${Boost_LIBRARY_DIRS} )

SET(LUX_LIBRARY luxShared)
ADD_DEFINITIONS(-DLUX_DLL) # for controlling visibility

# Control how the linker handles relative paths
set(CMAKE_BUILD_WITH_INSTALL_RPATH TRUE)

set(CMAKE_INSTALL_RPATH "$ORIGIN")

set(LUX_DEPS
	OpenImageIO::OpenImageIO
	OpenEXR::OpenEXR
	Imath::Imath
	Boost::thread
	Boost::program_options
	Boost::filesystem
	Boost::serialization
	Boost::iostreams
	Boost::regex
	Boost::python
	TIFF::TIFF
	PNG::PNG
	JPEG::JPEG
	FFTW3::fftw3
	${SYS_LIBRARIES}
	embree
)

#############################################################################
#########################      LIBRARY BUILD      ###########################
#############################################################################

INCLUDE(liblux)

#############################################################################
############################   PYTHON LIBRARY    ############################
#############################################################################

INCLUDE(pylux)

#############################################################################
#############################    Binaries       #############################
#############################################################################

INCLUDE(luxconsole)
INCLUDE(luxmerger)
INCLUDE(luxcomp)
INCLUDE(luxrender)
