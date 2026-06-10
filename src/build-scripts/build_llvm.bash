#!/usr/bin/env bash

# Utility script to download and build LLVM & clang
#
# Copyright Contributors to the Open Shading Language project.
# SPDX-License-Identifier: BSD-3-Clause
# https://github.com/AcademySoftwareFoundation/OpenShadingLanguage

# Exit the whole script if any command fails.
set -ex

echo "Building LLVM ${LLVM_VERSION}"
uname

if [[ "$LLVM_APT" != "" ]] ; then
    # Modern Linux installation from LLVM's llvm.sh script that installs an
    # apt package from their repo. Needs 1-part version (e.g., 21).
    wget https://apt.llvm.org/llvm.sh
    chmod +x llvm.sh
    sudo ./llvm.sh $LLVM_VERSION
    sudo apt-get install -y libclang-${LLVM_VERSION}-dev clang-${LLVM_VERSION}
    : ${LLVM_INSTALL_DIR:=/usr/lib/llvm-${LLVM_VERSION}}
    ls $LLVM_INSTALL_DIR || true
elif [[ `uname` == "Linux" ]] ; then
    # Older Linux installation from downloaded artifacts on the llvm GitHub
    # releases page. Needs full 3-part version (18.1.8). Only works for
    # versions 18 and older.
    : ${LLVM_VERSION:=18.1.8}
    : ${LLVM_INSTALL_DIR:=${PWD}/llvm-install}
    : ${LLVM_DISTRO_NAME:=ubuntu-18.04}
    : ${LLVM_TAR_BASENAME:=clang+llvm-${LLVM_VERSION}-x86_64-linux-gnu-${LLVM_DISTRO_NAME}}
    : ${LLVMTAR:=${LLVM_TAR_BASENAME}.tar.xz}
    : ${LLVM_UNPACK_DIR:=${LLVM_TAR_BASENAME}}
    # : ${LLVM_UNPACK_DIR:=clang+llvm-${LLVM_VERSION}-x86_64-linux-gnu-${LLVM_DISTRO_NAME}}
    echo LLVMTAR = $LLVMTAR
    echo LLVM_UNPACK_DIR = $LLVM_UNPACK_DIR
    echo LLVM_INSTALL_DIR = $LLVM_INSTALL_DIR
    curl --location https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_VERSION}/${LLVMTAR} -o $LLVMTAR
    ls -l $LLVMTAR
    tar xf $LLVMTAR
    rm -f $LLVMTAR
    ls
    mv ${LLVM_UNPACK_DIR} $LLVM_INSTALL_DIR
elif [[ `uname -s` == "Windows" || "${RUNNER_OS}" == "Windows" ]] ; then
    echo "Installing Windows LLVM"
    : ${LLVM_VERSION:=18.1.8}
    : ${LLVM_INSTALL_DIR:=${PWD}/llvm-install}
    LLVMTAR=clang+llvm-${LLVM_VERSION}-x86_64-pc-windows-msvc.tar.xz
    echo LLVMTAR = $LLVMTAR
    curl --location https://github.com/llvm/llvm-project/releases/download/llvmorg-${LLVM_VERSION}/${LLVMTAR} -o $LLVMTAR
    ls -l $LLVMTAR
    tar xf $LLVMTAR
    rm -f $LLVMTAR
    mv clang+llvm* $LLVM_INSTALL_DIR
else
    echo Bad uname `uname`
fi

echo "Installed LLVM ${LLVM_VERSION} in ${LLVM_INSTALL_DIR}"
ls -a $LLVM_INSTALL_DIR || true
ls -a $LLVM_INSTALL_DIR/* || true
export LLVM_DIRECTORY=$LLVM_INSTALL_DIR
export PATH=${LLVM_INSTALL_DIR}/bin:$PATH
export LLVM_ROOT=${LLVM_INSTALL_DIR}
