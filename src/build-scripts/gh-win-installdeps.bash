#!/usr/bin/env bash

# Copyright Contributors to the Open Shading Language project.
# SPDX-License-Identifier: BSD-3-Clause
# https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

echo "gh-win-installdeps.bash"
env | sort

# DEP_DIR="$PWD/ext/dist"
DEP_DIR="$PWD/dist"
mkdir -p "$DEP_DIR"
mkdir -p ext
VCPKG_INSTALLATION_ROOT=/c/vcpkg

export CMAKE_PREFIX_PATH=${CMAKE_PREFIX_PATH:=.}
export CMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH;$DEP_DIR"
export CMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH;$VCPKG_INSTALLATION_ROOT/installed/x64-windows-release"
export PATH="$PATH:$DEP_DIR/bin:$DEP_DIR/lib:$VCPKG_INSTALLATION_ROOT/installed/x64-windows-release/bin:$PWD/ext/dist/bin:$PWD/ext/dist/lib"
export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:$DEP_DIR/bin:$VCPKG_INSTALLATION_ROOT/installed/x64-windows-release/bin"
export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:$DEP_DIR/lib:$VCPKG_INSTALLATION_ROOT/installed/x64-windows-release/lib"

# export MY_CMAKE_FLAGS="$MY_CMAKE_FLAGS -DCMAKE_TOOLCHAIN_FILE=$VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake"
# export OPENEXR_CMAKE_FLAGS="$OPENEXR_CMAKE_FLAGS -DCMAKE_TOOLCHAIN_FILE=$VCPKG_INSTALLATION_ROOT/scripts/buildsystems/vcpkg.cmake"

#ls -l "C:/Program Files (x86)/Microsoft Visual Studio/*/Enterprise/VC/Tools/MSVC" && true
#ls -l "C:/Program Files (x86)/Microsoft Visual Studio" && true


if [[ "$PYTHON_VERSION" == "3.7" ]] ; then
    export CMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH;/c/hostedtoolcache/windows/Python/3.7.9/x64"
    export Python_EXECUTABLE="/c/hostedtoolcache/windows/Python/3.7.9/x64/python.exe"
    export PYTHONPATH=$OpenImageIO_ROOT/lib/python${PYTHON_VERSION}/site-packages
elif [[ "$PYTHON_VERSION" == "3.9" ]] ; then
    export CMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH;/c/hostedtoolcache/windows/Python/3.9.13/x64"
    export Python_EXECUTABLE="/c/hostedtoolcache/windows/Python/3.9.13/x64/python3.exe"
    export PYTHONPATH=$OpenImageIO_ROOT/lib/python${PYTHON_VERSION}/site-packages
fi
pip install numpy


########################################################################
# Dependency method #1: Use vcpkg (disabled)
#
# Currently we are not using this, but here it is for reference:
#
echo "All pre-installed VCPkg installs:"
vcpkg list
echo "---------------"
# vcpkg update
# 

#vcpkg install libdeflate:x64-windows-release
#vcpkg install zlib:x64-windows-release
vcpkg install tiff:x64-windows-release
# vcpkg install libpng:x64-windows-release
# vcpkg install giflib:x64-windows-release
vcpkg install freetype:x64-windows-release
# # vcpkg install openexr:x64-windows-release
vcpkg install libjpeg-turbo:x64-windows-release

vcpkg install pugixml:x64-windows-release
#vcpkg install libdeflate:x64-windows-release
# vcpkg install zlib:x64-windows-release
# vcpkg install tiff:x64-windows-release
# vcpkg install libpng:x64-windows-release
# vcpkg install giflib:x64-windows-release
# vcpkg install freetype:x64-windows-release
# # vcpkg install openexr:x64-windows-release
# vcpkg install libjpeg-turbo:x64-windows-release
vcpkg install opencolorio:x64-windows-release
# vcpkg install openimageio:x64-windows-release

# Needed by llvm
vcpkg install libxml2

echo "$VCPKG_INSTALLATION_ROOT"
ls "$VCPKG_INSTALLATION_ROOT" || true
echo "$VCPKG_INSTALLATION_ROOT/installed/x64-windows-release"
ls "$VCPKG_INSTALLATION_ROOT/installed/x64-windows-release" || true
echo "$VCPKG_INSTALLATION_ROOT/installed/x64-windows-release/lib"
ls "$VCPKG_INSTALLATION_ROOT/installed/x64-windows-release/lib" || true
echo "$VCPKG_INSTALLATION_ROOT/installed/x64-windows-release/bin"
ls "$VCPKG_INSTALLATION_ROOT/installed/x64-windows-release/bin" || true
echo "$VCPKG_INSTALLATION_ROOT/installed/x64-windows-release/include"
ls "$VCPKG_INSTALLATION_ROOT/installed/x64-windows-release/include" || true
# 
# # export PATH="$PATH:$DEP_DIR/bin:$VCPKG_INSTALLATION_ROOT/installed/x64-windows-release/bin"
# export PATH="$DEP_DIR/lib:$DEP_DIR/bin:$PATH:$VCPKG_INSTALLATION_ROOT/installed/x64-windows-release/lib"
export CMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH:$VCPKG_INSTALLATION_ROOT/installed/x64-windows-release"
# export LD_LIBRARY_PATH="$LD_LIBRARY_PATH:$VCPKG_INSTALLATION_ROOT/installed/x64-windows-release/lib:$DEP_DIR/lib:$DEP_DIR/bin"
# 
echo "All VCPkg installs:"
vcpkg list
echo "CHECKPOINT 10: CXX=$CXX, CC=$CC"
#
########################################################################


########################################################################
# Dependency method #2: Build from source ourselves
#
#

src/build-scripts/build_zlib.bash
export ZLIB_ROOT=$PWD/ext/dist

src/build-scripts/build_libpng.bash
export PNG_ROOT=$PWD/ext/dist

# We're currently getting libtiff from vcpkg
# src/build-scripts/build_libtiff.bash
# export TIFF_ROOT=$PWD/ext/dist

# We're currently getting jpeg from vcpkg
# LIBJPEGTURBO_CONFIG_OPTS=-DWITH_SIMD=OFF
# # ^^ because we're too lazy to build nasm
# src/build-scripts/build_libjpeg-turbo.bash
# export JPEGTurbo_ROOT=$PWD/ext/dist

source src/build-scripts/build_pybind11.bash
export pybind11_ROOT=$PWD/ext/dist


# curl --location https://ffmpeg.zeranoe.com/builds/win64/dev/ffmpeg-4.2.1-win64-dev.zip -o ffmpeg-dev.zip
# unzip ffmpeg-dev.zip
# FFmpeg_ROOT=$PWD/ffmpeg-4.2.1-win64-dev

echo "CMAKE_PREFIX_PATH = $CMAKE_PREFIX_PATH"
echo "CHECKPOINT 20: CXX=$CXX, CC=$CC"

# if [[ "$OPENEXR_VERSION" != "" ]] ; then
#     OPENEXR_CXX_FLAGS=" /W1 /EHsc /DWIN32=1 "
#     #OPENEXR_BUILD_TYPE=$CMAKE_BUILD_TYPE
#     OPENEXR_INSTALL_DIR=$DEP_DIR
#     source src/build-scripts/build_openexr.bash
#     export PATH="$OPENEXR_INSTALL_DIR/bin:$OPENEXR_INSTALL_DIR/lib:$PATH"
#     export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$PATH
#     # the above line is admittedly sketchy
# fi

if [[ "$OPENEXR_VERSION" != "" ]] ; then
    source src/build-scripts/build_openexr.bash
fi

if [[ "$PUGIXML_VERSION" != "" ]] ; then
    source src/build-scripts/build_pugixml.bash
    export OSL_CMAKE_FLAGS+=" -DUSE_EXTERNAL_PUGIXML=1 "
fi

if [[ "$OPENCOLORIO_VERSION" != "" ]] ; then
    source src/build-scripts/build_opencolorio.bash
fi

cp $DEP_DIR/lib/*.lib $DEP_DIR/bin || true
cp $DEP_DIR/bin/*.dll $DEP_DIR/lib || true

echo "CHECKPOINT 30: CXX=$CXX, CC=$CC"

if [[ "$OPENIMAGEIO_VERSION" != "" ]] ; then
    # There are many parts of OIIO we don't need to build for OSL's tests
    export ENABLE_iinfo=0 ENABLE_iv=0 ENABLE_igrep=0
    export ENABLE_iconvert=0 ENABLE_testtex=0
    # For speed of compiling OIIO, disable the file formats that we don't
    # need for OSL's tests
    export ENABLE_BMP=0 ENABLE_cineon=0 ENABLE_DDS=0 ENABLE_DPX=0 ENABLE_FITS=0
    export ENABLE_ICO=0 ENABLE_iff=0 ENABLE_jpeg2000=0 ENABLE_PNM=0 ENABLE_PSD=0
    export ENABLE_RLA=0 ENABLE_SGI=0 ENABLE_SOCKET=0 ENABLE_SOFTIMAGE=0
    export ENABLE_TARGA=0 ENABLE_WEBP=0 ENABLE_jpegxl=0 ENABLE_libuhdr=0
    # We don't need to run OIIO's tests
    export OPENIMAGEIO_CMAKE_FLAGS+=" -DOIIO_BUILD_TESTING=OFF -DOIIO_BUILD_TESTS=0"
    # Don't let warnings in OIIO break OSL's CI run
    export OPENIMAGEIO_CMAKE_FLAGS+=" -DSTOP_ON_WARNING=OFF"
    export OPENIMAGEIO_CMAKE_FLAGS+=" -DUSE_OPENGL=0"
    export OPENIMAGEIO_CMAKE_FLAGS+=" -DUSE_OPENCV=0 -DUSE_FFMPEG=0 -DUSE_QT=0"
    if [[ "${OPENIMAGEIO_UNITY:-1}" != "0" ]] ; then
        # Speed up the OIIO build by doing a "unity" build. (Note: this is
        # only a savings in CI where there are only 1-2 cores available.)
        export OPENIMAGEIO_CMAKE_FLAGS+=" -DCMAKE_UNITY_BUILD=ON -DCMAKE_UNITY_BUILD_MODE=BATCH"
    fi
    export OPENIMAGEIO_CMAKE_FLAGS+=" -DCMAKE_CXX_COMPILER='$CXX'"
    export OPENIMAGEIO_CMAKE_FLAGS+=" -DCMAKE_CC_COMPILER='$CC'"
    source src/build-scripts/build_openimageio.bash
fi

cp $DEP_DIR/lib/*.lib $DEP_DIR/bin || true
cp $DEP_DIR/bin/*.dll $DEP_DIR/lib || true
echo "DEP_DIR $DEP_DIR :"
ls -R -l "$DEP_DIR"

if [[ "$LLVM_VERSION" != "" ]] ; then
    source src/build-scripts/build_llvm.bash
#else

fi

# export LLVM_ROOT="C:/Program Files/LLVM"
# export LLVM_DIRECTORY="C:/Program Files/LLVM"
echo "LLVM_ROOT = $LLVM_ROOT"
ls "$LLVM_ROOT/*" || true

echo "C:/Program Files/LLVM:"
ls "C:/Program Files/LLVM/*" || true

echo "./llvm contents: $PWD/llvm"
ls "./llvm" || true
ls "./llvm/*" || true

# source src/build-scripts/build_openexr.bash
# export CMAKE_PREFIX_PATH="$CMAKE_PREFIX_PATH;$OPENEXR_ROOT"
# source src/build-scripts/build_opencolorio.bash


mkdir -p winflexbison
pushd winflexbison
WFBZIP=win_flex_bison-2.5.25.zip
curl --location https://github.com/lexxmark/winflexbison/releases/download/v2.5.25/$WFBZIP -o $WFBZIP
unzip $WFBZIP
export FLEX_ROOT=$PWD
export BISON_ROOT=$PWD
#OSL_CMAKE_FLAGS+=" -DFLEX_EXECUTABLE=$PWD/win_flex.exe"
#OSL_CMAKE_FLAGS+=" -DBISON_EXECUTABLE=$PWD/win_bison.exe"
ls .
popd


# Save the env for use by other stages
src/build-scripts/save-env.bash
