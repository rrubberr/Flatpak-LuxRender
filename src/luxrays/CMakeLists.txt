################################################################################
# Copyright 1998-2015 by authors (see AUTHORS.txt)
#
#   This file is part of LuxRender.
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#     http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
################################################################################

MESSAGE(STATUS "CMAKE VERSION DETECTED " ${CMAKE_VERSION})
	cmake_minimum_required(VERSION 3.10)

####################
# Set the compiler #
####################

if(APPLE)
    # Use Xcode's Clang
    find_program(CLANG_CC NAMES clang)
    find_program(CLANG_CXX NAMES clang++)
    
    set(CMAKE_C_COMPILER ${CLANG_CC})
    set(CMAKE_CXX_COMPILER ${CLANG_CXX})
    message(STATUS "Using Apple Clang: ${CMAKE_C_COMPILER}")

    execute_process(COMMAND sysctl -n hw.ncpu OUTPUT_VARIABLE NPROC_VALUE OUTPUT_STRIP_TRAILING_WHITESPACE)
else()
    # Use the Intel compiler if available.
    if(DEFINED ENV{ONEAPI_ROOT})
        set(INTEL_CC "$ENV{ONEAPI_ROOT}/compiler/latest/bin/icx")
        set(INTEL_CXX "$ENV{ONEAPI_ROOT}/compiler/latest/bin/icpx")
        set(LLD_LINKER "$ENV{ONEAPI_ROOT}/compiler/latest/bin/lld")
    else()
        find_program(INTEL_CC NAMES icx)
        find_program(INTEL_CXX NAMES icpx)
        find_program(LLD_LINKER NAMES lld) # Only look globally if Intel env isn't set
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
endif()

##########################
# Set the CMake policies #
##########################

IF(COMMAND cmake_policy)
	cmake_policy(SET CMP0003 NEW)
    cmake_policy(SET CMP0074 NEW)
	cmake_policy(SET CMP0111 OLD)
    cmake_policy(SET CMP0144 OLD)
	cmake_policy(SET CMP0148 OLD)
	if(POLICY CMP0167)
    cmake_policy(SET CMP0167 OLD)
    endif()
ENDIF(COMMAND cmake_policy)

#####################
# Setup the project #
#####################

project(LuxRays)

############################
# Use LTO on Clang configs #
############################

if(USING_CLANG)
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

################################################################################
#
# Include necessary submodules
#
################################################################################

set(CMAKE_MODULE_PATH
	"${LuxRays_SOURCE_DIR}"
	"${LuxRays_SOURCE_DIR}/cmake"
	"${LuxRays_SOURCE_DIR}/cmake/Utils"
	"${LuxRays_SOURCE_DIR}/cmake/Packages"
	"${LuxRays_SOURCE_DIR}/cmake/SpecializedConfig"
)

# CMake needs to know which configuration to generate by default.
IF (NOT CMAKE_BUILD_TYPE)
	SET(CMAKE_BUILD_TYPE Release)
ENDIF(NOT CMAKE_BUILD_TYPE)

SET(CMAKE_BUILD_TYPE ${CMAKE_BUILD_TYPE} CACHE STRING "assure config" FORCE) # makes sure type is shown in cmake gui

message(STATUS "Building mode: " ${CMAKE_BUILD_TYPE})

INCLUDE(PlatformSpecific)
INCLUDE(Configuration)
INCLUDE(KernelPreprocess)

# Install CMake modules
#add_subdirectory(CMake)

SET(LuxRays_INCLUDE_DIR "${LuxRays_SOURCE_DIR}/include")
include_directories("${LuxRays_INCLUDE_DIR}")

# Find dependencies
include(Dependencies)

if (NOT Boost_FOUND)
	MESSAGE(FATAL_ERROR "--> Could not locate required Boost files - Please check ${BOOST_SEARCH_PATH}")
endif()

################################################################################
#
# Build options
#
################################################################################

ADD_DEFINITIONS("-DLUXRAYS_DISABLE_OPENCL")

################################################################################
#
# LuxRays libraries
#
################################################################################

add_subdirectory(src/luxrays)

################################################################################
#
# For non win32 we'll have to copy everything to a single dir
#
################################################################################

INCLUDE(AssembleBinDirs)
