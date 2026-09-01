###########################################################################
#   Copyright (C) 1998-2026 by authors (see AUTHORS.txt)                  #
#                                                                         #
#   This file is part of LuxRender.                                       #
#                                                                         #
#   LuxRender is free software; you can redistribute it and/or modify     #
#   it under the terms of the GNU General Public License as published by  #
#   the Free Software Foundation; either version 3 of the License, or     #
#   any later version.                                                    #
#                                                                         #
#   LuxRender is distributed in the hope that it will be useful,          #
#   but WITHOUT ANY WARRANTY; without even the implied warranty of        #
#   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the          #
#   GNU General Public License for more details.                          #
#                                                                         #
#   You should have received a copy of the GNU General Public License     #
#   along with this program. If not, see <http://www.gnu.org/licenses/>   #
#                                                                         #
#   This project is based on PBRT; see <http://www.pbrt.org>              #
###########################################################################

MESSAGE(STATUS "CMAKE VERSION DETECTED " ${CMAKE_VERSION})
cmake_minimum_required(VERSION 3.25)

###################################
# Set the compiler and build type #
###################################

include(${CMAKE_CURRENT_SOURCE_DIR}/../cmake/CompilerSetup.cmake)

##################
# Set up folders #
##################

# This points to LuxRender/ext.
set(EXT_DIR ${CMAKE_CURRENT_SOURCE_DIR})

# This points to LuxRender/ext/build.
set(BUILD_DIR ${EXT_DIR}/build)

# This points to LuxRender/ext/build/bin.
set(BIN_DIR ${BUILD_DIR}/bin)

# This points to LuxRender/ext/build/lib.
set(LIB_DIR ${BUILD_DIR}/lib)

# This points to LuxRender/ext/build/include.
set(INC_DIR ${BUILD_DIR}/include)

#This points to LuxRender/ext/build/pfx.
set(PFX_DIR ${EXT_DIR}/pfx)

#####################
# Setup the project #
#####################

project(LuxRenderDeps)

include(ExternalProject)

##############################
# Set the default build args #
##############################

file(READ "${EXT_DIR}/cpython/Include/patchlevel.h" _lux_patchlevel_h)
string(REGEX MATCH "#define[ \t]+PY_MAJOR_VERSION[ \t]+([0-9]+)" _ ""
	${_lux_patchlevel_h})
set(LUX_PYTHON_MAJOR "${CMAKE_MATCH_1}")
string(REGEX MATCH "#define[ \t]+PY_MINOR_VERSION[ \t]+([0-9]+)" _ ""
	${_lux_patchlevel_h})
set(LUX_PYTHON_MINOR "${CMAKE_MATCH_1}")
set(LUX_PYTHON_INCLUDE_DIR
	"${INC_DIR}/python${LUX_PYTHON_MAJOR}.${LUX_PYTHON_MINOR}")
message(STATUS "Vendored CPython headers: ${LUX_PYTHON_INCLUDE_DIR}")

set(COMMON_EXT_PROJECT_ARGS
	-DCMAKE_CXX_STANDARD=17
	-DCMAKE_BUILD_TYPE=${CMAKE_BUILD_TYPE}
	"-DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS} -I\"${LUX_PYTHON_INCLUDE_DIR}\" -w"
	"-DCMAKE_C_FLAGS=${CMAKE_C_FLAGS} -w"
	-DCMAKE_POSITION_INDEPENDENT_CODE=TRUE
    -DCMAKE_INSTALL_PREFIX=${BUILD_DIR}
    -DCMAKE_INSTALL_LIBDIR=lib
    -DCMAKE_INSTALL_INCLUDEDIR=include
	-DCMAKE_FIND_ROOT_PATH_MODE_PROGRAM=NEVER
    -DCMAKE_FIND_ROOT_PATH_MODE_LIBRARY=ONLY
    -DCMAKE_FIND_ROOT_PATH_MODE_INCLUDE=ONLY
    -DCMAKE_FIND_ROOT_PATH_MODE_PACKAGE=ONLY
    -DCMAKE_PREFIX_PATH=${BUILD_DIR}
)

#############################################
# Optional serialized builds for debugging #
#############################################

# Each external project is chained so one project builds at a time.
# Enable with: cmake -DLUX_EXT_SERIAL=ON ..
option(LUX_EXT_SERIAL "Build external projects one at a time" OFF)

function(lux_external_project_add TARGET_NAME)
	if(LUX_EXT_SERIAL AND DEFINED LUX_EXT_PREV_TARGET)
		ExternalProject_Add(${TARGET_NAME} ${ARGN} DEPENDS ${LUX_EXT_PREV_TARGET})
	else()
		ExternalProject_Add(${TARGET_NAME} ${ARGN})
	endif()

	if(LUX_EXT_SERIAL)
		set(LUX_EXT_PREV_TARGET ${TARGET_NAME} PARENT_SCOPE)
	endif()
endfunction()

if(LUX_EXT_SERIAL)
	message(STATUS "LuxRender ext: serialized external project builds ENABLED")
endif()

#################
# Build cpython #
#################

lux_external_project_add(
    cpython
    SOURCE_DIR ${EXT_DIR}/cpython
    PREFIX ${PFX_DIR}/cpython
    INSTALL_DIR ${BUILD_DIR}
    CONFIGURE_COMMAND
        ${EXT_DIR}/cpython/configure
        --prefix=${BUILD_DIR}
        --disable-shared
        --with-ensurepip=no
        --disable-test-modules
        CFLAGS=-O2\ -w\ -fPIC
        LDFLAGS=-Wl,-rpath,$$ORIGIN
    BUILD_COMMAND make
    INSTALL_COMMAND make install
)

#################
# Build zlib-ng #
#################

lux_external_project_add(
	zlib-ng
	SOURCE_DIR ${EXT_DIR}/zlib-ng
	PREFIX ${PFX_DIR}/zlib-ng
	INSTALL_DIR ${BUILD_DIR}
	CMAKE_ARGS
		${COMMON_EXT_PROJECT_ARGS}
		-DZLIB_COMPAT=TRUE
		-DBUILD_TESTING=FALSE
        -DBUILD_SHARED_LIBS=FALSE
)

###############
# Build bzip2 #
###############

lux_external_project_add(
	bzip2
	SOURCE_DIR ${EXT_DIR}/bzip2
	PREFIX ${PFX_DIR}/bzip2
	INSTALL_DIR ${BUILD_DIR}
	CMAKE_ARGS
		${COMMON_EXT_PROJECT_ARGS}
		-DENABLE_LIB_ONLY=ON
		-DENABLE_APP=OFF
		-DENABLE_EXAMPLES=OFF
		-DENABLE_TESTS=OFF
		-DENABLE_DOCS=OFF
)

############
# Build xz #
############

lux_external_project_add(
	xz
	SOURCE_DIR ${EXT_DIR}/xz
	PREFIX ${PFX_DIR}/xz
	INSTALL_DIR ${BUILD_DIR}
	CMAKE_ARGS
		${COMMON_EXT_PROJECT_ARGS}
)

###################
# Build robin-map #
###################

lux_external_project_add(
	robin-map
	SOURCE_DIR ${EXT_DIR}/robin-map
	PREFIX ${PFX_DIR}/robin-map
	INSTALL_DIR ${BUILD_DIR}
	CMAKE_ARGS
		${COMMON_EXT_PROJECT_ARGS}
		-DTSL_ROBIN_MAP_ENABLE_INSTALL=TRUE
)

##################
# Build yaml-cpp #
##################

lux_external_project_add(
	yaml-cpp
	SOURCE_DIR ${EXT_DIR}/yaml-cpp
	PREFIX ${PFX_DIR}/yaml-cpp
	INSTALL_DIR ${BUILD_DIR}
	CMAKE_ARGS
		${COMMON_EXT_PROJECT_ARGS}
		-DYAML_CPP_BUILD_CONTRIB=FALSE
		-DYAML_CPP_BUILD_TOOLS=FALSE
		-DYAML_BUILD_SHARED_LIBS=FALSE
)

##################
# Build pybind11 #
##################

lux_external_project_add(
	pybind11
    DEPENDS cpython
	SOURCE_DIR ${EXT_DIR}/pybind11
	PREFIX ${PFX_DIR}/pybind11
	INSTALL_DIR ${BUILD_DIR}
	CMAKE_ARGS
		${COMMON_EXT_PROJECT_ARGS}
		-DPYBIND11_TEST=FALSE
)

##################
# Build pystring #
##################

lux_external_project_add(
	pystring
	SOURCE_DIR ${EXT_DIR}/pystring
	PREFIX ${PFX_DIR}/pystring
	INSTALL_DIR ${BUILD_DIR}
	CMAKE_ARGS
		${COMMON_EXT_PROJECT_ARGS}
        -DBUILD_SHARED_LIBS=FALSE

)

####################
# Build minizip-ng #
####################

lux_external_project_add(
	minizip-ng
    DEPENDS zlib-ng bzip2 xz
	SOURCE_DIR ${EXT_DIR}/minizip-ng
	PREFIX ${PFX_DIR}/minizip-ng
	INSTALL_DIR ${BUILD_DIR}
	CMAKE_ARGS
		${COMMON_EXT_PROJECT_ARGS}
		-DMZ_ZLIB_FLAVOR=zlib
		-DMZ_PKCRYPT=FALSE
		-DMZ_WZAES=FALSE
		-DMZ_OPENSSL=FALSE
        -DMZ_COMPAT=TRUE
        -DMZ_ZLIB=TRUE
        -DMZ_BZIP2=FALSE
        -DMZ_LZMA=FALSE
        -DMZ_ZSTD=FALSE
        -DMZ_LIBBSD=FALSE
        -DMZ_ICONV=TRUE
		-DMZ_PPMD=OFF
        -DBUILD_SHARED_LIBS=FALSE
)

##################
# Build libexpat #
##################

lux_external_project_add(
	libexpat
	SOURCE_DIR ${EXT_DIR}/libexpat/expat
	PREFIX ${PFX_DIR}/libexpat
	INSTALL_DIR ${BUILD_DIR}
	CMAKE_ARGS
		${COMMON_EXT_PROJECT_ARGS}
		-DBUILD_SHARED_LIBS=FALSE
)

####################
# Build libdeflate #
####################

lux_external_project_add(
	libdeflate
	SOURCE_DIR ${EXT_DIR}/libdeflate
	PREFIX ${PFX_DIR}/libdeflate
	INSTALL_DIR ${BUILD_DIR}
	CMAKE_ARGS
		${COMMON_EXT_PROJECT_ARGS}
		-DLIBDEFLATE_BUILD_STATIC_LIB=ON
		-DLIBDEFLATE_BUILD_SHARED_LIB=OFF
)

###############
# Build FFTW3 #
###############

lux_external_project_add(
	fftw
	URL ${EXT_DIR}/fftw/fftw-3.3.11.tar.gz
	PREFIX ${PFX_DIR}/fftw
	INSTALL_DIR ${BUILD_DIR}
	CMAKE_ARGS
		${COMMON_EXT_PROJECT_ARGS}
		-DBUILD_SHARED_LIBS=OFF
		-DCMAKE_POLICY_VERSION_MINIMUM=3.5
)

################
# Build libpng #
################

lux_external_project_add(
	libpng
    DEPENDS zlib-ng
	SOURCE_DIR ${EXT_DIR}/libpng
	PREFIX ${PFX_DIR}/libpng
	INSTALL_DIR ${BUILD_DIR}
	CMAKE_ARGS
		${COMMON_EXT_PROJECT_ARGS}
		-DPNG_STATIC=TRUE
		-DPNG_SHARED=FALSE
		-DPNG_TESTS=FALSE
		-DPNG_EXECUTABLES=FALSE
		-DZLIB_INCLUDE_DIR=${INC_DIR}
        -DZLIB_LIBRARY=${LIB_DIR}/libz.a
)

#################
# Build OpenJPH #
#################

lux_external_project_add(
	OpenJPH
	SOURCE_DIR ${EXT_DIR}/OpenJPH
	PREFIX ${PFX_DIR}/OpenJPH
	INSTALL_DIR ${BUILD_DIR}
	CMAKE_ARGS
		${COMMON_EXT_PROJECT_ARGS}
		-DOJPH_BUILD_EXECUTABLES=FALSE
		-DOJPH_ENABLE_TIFF_SUPPORT=FALSE
        -DBUILD_SHARED_LIBS=FALSE
)

#######################
# Build libjpeg-turbo #
#######################

lux_external_project_add(
	libjpeg-turbo
	SOURCE_DIR ${EXT_DIR}/libjpeg-turbo
	PREFIX ${PFX_DIR}/libjpeg-turbo
	INSTALL_DIR ${BUILD_DIR}
	CMAKE_ARGS
		${COMMON_EXT_PROJECT_ARGS}
		-DWITH_TESTS=FALSE
		-DWITH_TURBOJPEG=FALSE
        -DENABLE_SHARED=FALSE
		-DENABLE_STATIC=TRUE
		-DWITH_JPEG7=TRUE
		-DWITH_JPEG8=TRUE
)

#################
# Build libtiff #
#################

lux_external_project_add(
	libtiff
    DEPENDS zlib-ng libdeflate
	SOURCE_DIR ${EXT_DIR}/libtiff
	PREFIX ${PFX_DIR}/libtiff
	INSTALL_DIR ${BUILD_DIR}
	CMAKE_ARGS
		${COMMON_EXT_PROJECT_ARGS}
		-Dtiff-static=TRUE
		-Dtiff-tools=FALSE
		-Dtiff-tests=FALSE
		-Dtiff-contrib=FALSE
		-Dtiff-docs=FALSE
		-Dtiff-deprecated=FALSE
		-Dlibdeflate=FALSE
		-Dlzma=FALSE
		-Dzstd=FALSE
		-Dwebp=FALSE
		-Dtiff-cxx=FALSE
		-Dtiff-opengl=FALSE
		-Dlerc=FALSE
		-Djbig=FALSE
		-Djpeg12=FALSE
		-Djpeg=FALSE
)

###############
# Build Imath #
###############

lux_external_project_add(
	Imath
	SOURCE_DIR ${EXT_DIR}/Imath
	PREFIX ${PFX_DIR}/Imath
	INSTALL_DIR ${BUILD_DIR}
	CMAKE_ARGS
		${COMMON_EXT_PROJECT_ARGS}
        -DBUILD_SHARED_LIBS=FALSE
)

#################
# Build openexr #
#################

lux_external_project_add(
	openexr
	DEPENDS OpenJPH Imath zlib-ng minizip-ng bzip2 xz
	SOURCE_DIR ${EXT_DIR}/openexr
	PREFIX ${PFX_DIR}/openexr
	INSTALL_DIR ${BUILD_DIR}
	CMAKE_ARGS
		${COMMON_EXT_PROJECT_ARGS}
		-DOPENEXR_BUILD_TOOLS=FALSE
		-DOPENEXR_BUILD_EXAMPLES=FALSE
		-DOPENEXR_INSTALL_DOCS=FALSE
		-DOPENEXR_BUILD_PYTHON=FALSE
		-DOPENEXR_INSTALL_TOOLS=FALSE
		-DOPENEXR_INSTALL_DEVELOPER_TOOLS=FALSE
		-DOPENEXR_FORCE_INTERNAL_OPENJPH=FALSE # Do not use internal. This will break the universe.
        -DBUILD_SHARED_LIBS=FALSE
	CMAKE_CACHE_ARGS
		"-DCMAKE_CXX_FLAGS:STRING=${CMAKE_CXX_FLAGS} -I${INC_DIR}/openjph"
)

#####################
# Build OpenColorIO #
#####################

lux_external_project_add(
	OpenColorIO
    DEPENDS zlib-ng minizip-ng yaml-cpp pystring Imath bzip2 xz
	SOURCE_DIR ${EXT_DIR}/OpenColorIO
	PREFIX ${PFX_DIR}/OpenColorIO
	INSTALL_DIR ${BUILD_DIR}
	CMAKE_ARGS
		${COMMON_EXT_PROJECT_ARGS}
		-DOCIO_BUILD_APPS=FALSE
		-DOCIO_BUILD_OPENFX=FALSE
		-DOCIO_BUILD_TESTS=FALSE
		-DOCIO_BUILD_PYTHON=FALSE
		-DOCIO_BUILD_GPU_TESTS=FALSE
        -DBUILD_SHARED_LIBS=FALSE
        -DOCIO_INSTALL_EXT_PACKAGES=NONE
		-DZLIB_INCLUDE_DIR=${INC_DIR}
        -DZLIB_LIBRARY=${LIB_DIR}/libz.a
)

#####################
# Build OpenImageIO #
#####################

lux_external_project_add(
	OpenImageIO
	DEPENDS robin-map zlib-ng Imath openexr libtiff libjpeg-turbo libpng OpenColorIO
	SOURCE_DIR ${EXT_DIR}/OpenImageIO
	PREFIX ${PFX_DIR}/OpenImageIO
	INSTALL_DIR ${BUILD_DIR}
	CMAKE_ARGS
		${COMMON_EXT_PROJECT_ARGS}
		-DUSE_PYTHON=FALSE
		-DOIIO_BUILD_TESTS=FALSE
		-DOIIO_BUILD_TOOLS=FALSE
		-DEMBEDPLUGINS=TRUE
		-DINSTALL_FONTS=FALSE
		-DBUILD_DOCS=FALSE
		-DINSTALL_DOCS=FALSE
		-DUSE_GIF=FALSE
		-DUSE_FFMPEG=FALSE
		-DUSE_FREETYPE=FALSE
		-DUSE_LIBUHDR=FALSE
		-DUSE_LIBRAW=FALSE
		-DUSE_NUKE=FALSE
		-DUSE_QT=FALSE
		-DUSE_JXL=FALSE
		-DUSE_OPENCV=FALSE
		-DUSE_DCMTK=FALSE
		-DUSE_FFmpeg=FALSE
		-DUSE_LibRaw=FALSE
		-DUSE_OpenVDB=FALSE
		-DUSE_Ptex=FALSE
		-DUSE_WebP=FALSE
		-DUSE_OpenVDB=FALSE
		-DUSE_openjph=FALSE
		-DUSE_Libheif=FALSE
		-DUSE_OpenJPEG=FALSE
        -DUSE_TBB=FALSE
        -DBUILD_SHARED_LIBS=FALSE
		-DZLIB_INCLUDE_DIR=${INC_DIR}
        -DZLIB_LIBRARY=${LIB_DIR}/libz.a
		-DJPEG_ROOT=${BUILD_DIR}
)

#############
# Build TBB #
#############

lux_external_project_add(
	tbb
	SOURCE_DIR ${EXT_DIR}/tbb
	PREFIX ${PFX_DIR}/tbb
	INSTALL_DIR ${BUILD_DIR}
	CMAKE_ARGS
		${COMMON_EXT_PROJECT_ARGS}
		-DTBB_TEST=OFF
		-DBUILD_SHARED_LIBS=OFF
)

################
# Build Embree #
################

lux_external_project_add(
	embree
	DEPENDS tbb
	SOURCE_DIR ${EXT_DIR}/embree
	PREFIX ${PFX_DIR}/embree
	INSTALL_DIR ${BUILD_DIR}
	CMAKE_ARGS
		${COMMON_EXT_PROJECT_ARGS}
		# Pass the compiler.
        -DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}
        -DCMAKE_CXX_COMPILER=${CMAKE_CXX_COMPILER}
        -DCMAKE_LINKER_TYPE=${CMAKE_LINKER_TYPE}
        # Intel LLD definitions.
        -DCMAKE_C_USING_LINKER_LLD=${CMAKE_C_USING_LINKER_LLD}
        -DCMAKE_CXX_USING_LINKER_LLD=${CMAKE_CXX_USING_LINKER_LLD}
        # Forward PIE and LTO.
        -DCMAKE_CXX_FLAGS=${CMAKE_CXX_FLAGS}
        -DCMAKE_C_FLAGS=${CMAKE_C_FLAGS}
		-DEMBREE_STATIC_LIB=TRUE
		-DEMBREE_TUTORIALS=FALSE
		-DEMBREE_ZIP_MODE=FALSE
		-DEMBREE_STAT_COUNTERS=FALSE
		-DEMBREE_BACKFACE_CULLING=FALSE
		-DEMBREE_GEOMETRY_TRIANGLE=TRUE
		-DEMBREE_GEOMETRY_QUAD=TRUE
		-DEMBREE_GEOMETRY_CURVE=FALSE
		-DEMBREE_GEOMETRY_USER=TRUE
		-DEMBREE_GEOMETRY_SUBDIVISION=FALSE
		-DEMBREE_GEOMETRY_INSTANCE=TRUE
		-DEMBREE_GEOMETRY_INSTANCE_ARRAY=TRUE
		-DEMBREE_GEOMETRY_POINT=TRUE
		-DEMBREE_GEOMETRY_GRID=FALSE
		-DEMBREE_IGNORE_INVALID_RAYS=TRUE
		-DEMBREE_COMPACT_POLYS=TRUE
		-DEMBREE_SYCL_SUPPORT=FALSE
		-DEMBREE_IGNORE_CMAKE_CXX_FLAGS=FALSE
		-DEMBREE_RAY_MASK=TRUE
		-DEMBREE_RAY_PACKETS=TRUE
)

###############
# Build Boost #
###############

set(
	BOOST_MODULES
   "filesystem|system|program_options|regex|thread|serialization|iostreams|python|chrono|array|assert|locale|format|atomic|interprocess|ptr_container|asio|uuid"
)

lux_external_project_add(
	boost
	DEPENDS cpython libexpat fftw
	SOURCE_DIR ${EXT_DIR}/boost
	PREFIX ${PFX_DIR}/boost
	INSTALL_DIR ${BUILD_DIR}
	LIST_SEPARATOR |
	CMAKE_ARGS
		${COMMON_EXT_PROJECT_ARGS}
		-DBOOST_INCLUDE_LIBRARIES=${BOOST_MODULES}
		-DCMAKE_INSTALL_MESSAGE=LAZY		
		-DBOOST_RUNTIME_LINK=static
		-DBOOST_ENABLE_PYTHON=ON
		-DBUILD_SHARED_LIBS=OFF
		-DCMAKE_INSTALL_RPATH_USE_LINK_PATH=TRUE
		-DCMAKE_BUILD_WITH_INSTALL_RPATH=TRUE
		-DPython_ROOT_DIR=${BUILD_DIR}
		-DPython_FIND_STRATEGY=LOCATION
		-DCMAKE_FIND_FRAMEWORK=LAST
)
