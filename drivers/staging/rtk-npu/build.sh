#!/bin/bash

if [ "$2" = "1619b" ]; then
    SOC_NAME="1619b"
elif [ "$2" = "kent" ]; then
    SOC_NAME="kent"
else
    echo "Usage: ./build.sh [arm64|android12|android14] [1619b|kent]"
    exit
fi

if [ "$1" =  "arm64" ]; then
    #TOOLCHAIN="/PATH/TO/ARM64CC"
    #BUILD_OPT="$BUILD_OPT PATH=$TOOLCHAIN:$PATH"
    BUILD_OPT="$BUILD_OPT CROSS_COMPILE=aarch64-linux-gnu-"
    BUILD_OPT="$BUILD_OPT ARCH_TYPE=arm64"
    BUILD_OPT="$BUILD_OPT AQROOT=$PWD"
    BUILD_OPT="$BUILD_OPT KERNEL_DIR=/PATH/TO/KERNEL"
    BUILD_OPT="$BUILD_OPT SOC_PLATFORM=realtek-$SOC_NAME"
elif [ "$1" = "android12" ]; then
    ANDROID_BUILD_SCRIPTS="/PATH/TO/ANDROID/BUILD_SCRIPTS"
    TOOLCHAIN="${ANDROID_BUILD_SCRIPTS}/phoenix/toolchain/clang/clang-r416183b1/bin"
    BUILD_OPT="$BUILD_OPT PATH=$TOOLCHAIN:$PATH"
    BUILD_OPT="$BUILD_OPT CROSS_COMPILE=aarch64-linux-gnu-"
    BUILD_OPT="$BUILD_OPT ARCH_TYPE=arm64"
    BUILD_OPT="$BUILD_OPT AQROOT=$PWD"
    BUILD_OPT="$BUILD_OPT KERNEL_DIR=${ANDROID_BUILD_SCRIPTS}/linux-kernel"
    BUILD_OPT="$BUILD_OPT CC=clang LD=ld.lld CLANG_TRIPLE=aarch64-linux-gnu- LLVM=1 LLVM_IAS=1"
    BUILD_OPT="$BUILD_OPT SOC_PLATFORM=realtek-$SOC_NAME"
elif [ "$1" = "android14" ]; then
    ANDROID_BUILD_SCRIPTS="/PATH/TO/ANDROID/BUILD_SCRIPTS"
    TOOLCHAIN="${ANDROID_BUILD_SCRIPTS}/prebuilts/clang/host/linux-x86/clang-r487747c/bin"
    BUILD_OPT="$BUILD_OPT PATH=$TOOLCHAIN:$PATH"
    BUILD_OPT="$BUILD_OPT CROSS_COMPILE=aarch64-linux-gnu-"
    BUILD_OPT="$BUILD_OPT ARCH_TYPE=arm64"
    BUILD_OPT="$BUILD_OPT AQROOT=$PWD"
    BUILD_OPT="$BUILD_OPT KERNEL_DIR=${ANDROID_BUILD_SCRIPTS}/kernel/rtk/src/common-kernel/u-5.15"
    BUILD_OPT="$BUILD_OPT CC=clang LD=ld.lld CLANG_TRIPLE=aarch64-linux-gnu- LLVM=1 LLVM_IAS=1"
    BUILD_OPT="$BUILD_OPT SOC_PLATFORM=realtek-$SOC_NAME"
else
    echo "Usage: ./build.sh [arm64|android12|android14] [1619b|kent]"
    exit
fi

make $BUILD_OPT -f Kbuild -j$(nproc)
