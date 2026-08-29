###########################################################################
#   Copyright (C) 1998-2026 by authors (see AUTHORS.txt)                  #
#                                                                         #
#   This file is part of LuxRender.                                       #
#                                                                         #
#   LuxRender is free software; you can redistribute it and/or modify     #
#   it under the terms of the GNU General Public License as published by  #
#   the Free Software Foundation; either version 3 of the License, or     #
#   (at your option) any later version.                                   #
#                                                                         #
#   LuxRender is distributed in the hope that it will be useful,          #
#   but WITHOUT ANY WARRANTY; without even the implied warranty of        #
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         #
#   GNU General Public License for more details.                          #
#                                                                         #
#   You should have received a copy of the GNU General Public License     #
#   along with this program.  If not, see <http://www.gnu.org/licenses/>. #
#                                                                         #
#   LuxRender website: https://github.com/rrubberr/LuxRender              #
###########################################################################

MESSAGE(STATUS "CMAKE VERSION DETECTED " ${CMAKE_VERSION})
cmake_minimum_required(VERSION 3.25)

####################
# Set the compiler #
####################

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
    
    # 2. Bake linker fixes directly into the LLD call rule.
    set(CMAKE_C_USING_LINKER_LLD "-fuse-ld=lld -pie -Wl,--ignore-data-address-equality")
    set(CMAKE_CXX_USING_LINKER_LLD "-fuse-ld=lld -pie -Wl,--ignore-data-address-equality")
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

#####################
# Setup the project #
#####################

PROJECT(lux)
SET(VERSION 1.9)

include(ExternalProject)

##################
# Set up folders #
##################

# This points to LuxRender/ext.
set(EXT_DIR ${CMAKE_CURRENT_SOURCE_DIR}/ext)

# This points to LuxRender/ext/build.
set(LUX_BUILD_DIR ${EXT_DIR}/build)

# This points to LuxRender/ext/build/bin.
set(LUX_BIN_DIR ${LUX_BUILD_DIR}/bin)

# This points to LuxRender/ext/build/lib.
set(LUX_LIB_DIR ${LUX_BUILD_DIR}/lib)

# This points to LuxRender/ext/build/include.
set(LUX_INC_DIR ${LUX_BUILD_DIR}/include)

#This points to LuxRender/ext/build/pfx.
set(LUX_PFX_DIR ${LUX_BUILD_DIR}/pfx)

##############################
# Set the default build type #
##############################

IF (NOT CMAKE_BUILD_TYPE)
	SET(CMAKE_BUILD_TYPE Release)
ENDIF(NOT CMAKE_BUILD_TYPE)

MESSAGE(STATUS "Building mode: " ${CMAKE_BUILD_TYPE})

##############################
# Set the default build args #
##############################

set(
	COMMON_LUX_PROJECT_ARGS
    -DCMAKE_CXX_STANDARD=17
	-DCMAKE_POSITION_INDEPENDENT_CODE=TRUE
    -DCMAKE_PREFIX_PATH=${LUX_BUILD_DIR}
    -DBoost_LIBRARY_DIRS=${LUX_LIB_DIR}
    -DBoost_INCLUDE_DIRS=${LUX_INC_DIR}
    -DBoost_NO_SYSTEM_PATHS=ON
    -DBoost_USE_STATIC_LIBS=ON
)

SET(CMAKE_EXPORT_COMPILE_COMMANDS ON)



##############
# Build Deps #
##############

add_subdirectory(ext)

###################
# Build LuxRender #
###################

set(LUX_CXX_FLAGS "${CMAKE_CXX_FLAGS} -DBOOST_BIND_GLOBAL_PLACEHOLDERS -w -I${LUX_INC_DIR}/Imath -I${LUX_INC_DIR}/OpenEXR")

# Where the finished binaries are collected.
set(LUX_RELEASE_DIR "${CMAKE_BINARY_DIR}/release" CACHE PATH
    "Directory the LuxRender binaries are copied to after the build")

# The inner project's build directory.
set(LUX_INNER_BUILD_DIR ${LUX_PFX_DIR}/LuxRender/build)

# One COMMAND per copy.
set(LUX_INSTALL_COMMANDS
    ${CMAKE_COMMAND} -E make_directory "${LUX_RELEASE_DIR}"
    COMMAND ${CMAKE_COMMAND} -E copy "${LUX_INNER_BUILD_DIR}/luxcomp"    "${LUX_RELEASE_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy "${LUX_INNER_BUILD_DIR}/luxconsole" "${LUX_RELEASE_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy "${LUX_INNER_BUILD_DIR}/luxmerger"  "${LUX_RELEASE_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy "${LUX_INNER_BUILD_DIR}/luxrender"  "${LUX_RELEASE_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy "${LUX_INNER_BUILD_DIR}/pylux.so"   "${LUX_RELEASE_DIR}/"
    COMMAND ${CMAKE_COMMAND} -E copy "${LUX_INNER_BUILD_DIR}/liblux.so"  "${LUX_RELEASE_DIR}/"
)

ExternalProject_Add(
    LuxRender
    DEPENDS
        boost
        OpenImageIO
        embree
    SOURCE_DIR ${CMAKE_SOURCE_DIR}/src/lux
    BINARY_DIR ${LUX_INNER_BUILD_DIR}
    PREFIX ${LUX_PFX_DIR}/LuxRender
    BUILD_ALWAYS 1
    INSTALL_DIR ${LUX_BUILD_DIR}
    CMAKE_ARGS
        ${COMMON_LUX_PROJECT_ARGS}
        -DCMAKE_PREFIX_PATH=${LUX_BUILD_DIR}
        -DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
        "-DCMAKE_FIND_LIBRARY_SUFFIXES=.a"
        "-DTIFF_LIBRARY=${LUX_LIB_DIR}/libtiff.a"
        "-DJPEG_LIBRARY=${LUX_LIB_DIR}/libjpeg.a"
        "-DPNG_LIBRARY=${LUX_LIB_DIR}/libpng16.a"
        "-Dminizip-ng_LIBRARY=${LUX_LIB_DIR}/libminizip.a"
        -DJPEG_INCLUDE_DIR=${LUX_INC_DIR}
        -DPython3_ROOT_DIR=${LUX_BUILD_DIR}
        -DPython3_FIND_STRATEGY=LOCATION
        -DPython3_FIND_REGISTRY=NEVER
        -DPython3_FIND_FRAMEWORK=NEVER
        -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
        -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DCMAKE_LINKER_TYPE=${CMAKE_LINKER_TYPE}
		-DEXT_DIR=${EXT_DIR}
    CMAKE_CACHE_ARGS
        "-DCMAKE_CXX_FLAGS:STRING=${LUX_CXX_FLAGS}"
    INSTALL_COMMAND ${LUX_INSTALL_COMMANDS}
)
