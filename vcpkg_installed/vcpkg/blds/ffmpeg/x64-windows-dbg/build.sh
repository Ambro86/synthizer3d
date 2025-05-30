#!/usr/bin/env bash

set -e

move_binary() {
    SOURCE=$1
    TARGET=$2
    BINARY=$3

    # run lipo over the command to check whether it really
    # is a binary that we need to merge architectures
    lipo $SOURCE/$BINARY -info &> /dev/null || return 0

    # get the directory name the file is in
    DIRNAME=$(dirname $BINARY)

    # ensure the directory to move the binary to exists
    mkdir -p $TARGET/$DIRNAME

    # now finally move the binary
    mv $SOURCE/$BINARY $TARGET/$BINARY
}

move_binaries() {
    SOURCE=$1
    TARGET=$2

    [ ! -d $SOURCE ] && return 0
    pushd $SOURCE

    for BINARY in $(find . -type f); do
        move_binary $SOURCE $TARGET $BINARY
    done

    popd
}

merge_binaries() {
    TARGET=$1
    SOURCE=$2

    shift
    shift

    pushd $SOURCE/$1
    BINARIES=$(find . -type f)
    popd

    for BINARY in $BINARIES; do
        COMMAND="lipo -create -output $TARGET/$BINARY"

        for ARCH in $@; do
            COMMAND="$COMMAND -arch $ARCH $SOURCE/$ARCH/$BINARY"
        done

        $($COMMAND)
    done
}

PATH_TO_BUILD_DIR="C:/fork/synthizer3d/vcpkg_installed/vcpkg/blds/ffmpeg/x64-windows-dbg"
PATH_TO_SRC_DIR="C:/fork/synthizer3d/vcpkg_installed/vcpkg/blds/ffmpeg/src/n7.1.1-07cb3aa68a.clean"
PATH_TO_PACKAGE_DIR="C:/fork/synthizer3d/vcpkg_installed/vcpkg/pkgs/ffmpeg_x64-windows/debug"

JOBS=17

OSX_ARCHS=""
OSX_ARCH_COUNT=0

# Default to hardware concurrency if unset.
: ${JOBS:=$(nproc)}

# Disable asm and x86asm on all android targets because they trigger build failures:
# arm64 Android build fails with 'relocation R_AARCH64_ADR_PREL_PG_HI21 cannot be used against symbol ff_cos_32; recompile with -fPIC'
# x86 Android build fails with 'error: inline assembly requires more registers than available'.
# x64 Android build fails with 'relocation R_X86_64_PC32 cannot be used against symbol ff_h264_cabac_tables; recompile with -fPIC'
if [ "" = "Android" ]; then
    OPTIONS_arm=" --disable-asm --disable-x86asm"
    OPTIONS_arm64=" --disable-asm --disable-x86asm"
    OPTIONS_x86=" --disable-asm --disable-x86asm"
    OPTIONS_x86_64="${OPTIONS_x86}"
else
    OPTIONS_arm=" --disable-asm --disable-x86asm"
    OPTIONS_arm64=" --enable-asm --disable-x86asm"
    OPTIONS_x86=" --enable-asm --enable-x86asm"
    OPTIONS_x86_64="${OPTIONS_x86}"
fi

build_ffmpeg() {
    # extract build architecture
    BUILD_ARCH=$1
    shift

    echo "BUILD_ARCH=${BUILD_ARCH}"

    # get architecture-specific options
    OPTION_VARIABLE="OPTIONS_${BUILD_ARCH}"
    echo "OPTION_VARIABLE=${OPTION_VARIABLE}"

    echo "=== CONFIGURING ==="

    sh "$PATH_TO_SRC_DIR/configure" "--prefix=$PATH_TO_PACKAGE_DIR" --toolchain=msvc --enable-pic --disable-doc --enable-debug --enable-runtime-cpudetect --disable-autodetect --target-os=win32 --enable-w32threads --enable-d3d11va --enable-d3d12va --enable-dxva2 --enable-mediafoundation --disable-inline-asm --cc=cl.exe --host_cc=cl.exe --cxx=cl.exe --windres=rc.exe --ld=link.exe --ar='ar-lib lib.exe' --ranlib=: --disable-ffmpeg --disable-ffplay --disable-ffprobe --enable-avcodec --enable-avdevice --enable-avformat --enable-avfilter --disable-postproc --enable-swresample --enable-swscale --disable-alsa --disable-amf --disable-libaom --disable-libass --disable-avisynth --disable-bzlib --disable-libdav1d --disable-libfdk-aac --disable-libfontconfig --disable-libharfbuzz --disable-libfreetype --disable-libfribidi --disable-iconv --disable-libilbc --disable-lzma --disable-libmp3lame --disable-libmodplug --disable-cuda --disable-nvenc --disable-nvdec  --disable-cuvid --disable-ffnvcodec --disable-opencl --disable-opengl --disable-libopenh264 --disable-libopenjpeg --disable-libopenmpt --disable-openssl --enable-schannel --disable-libopus --disable-sdl2 --disable-libsnappy --disable-libsoxr --disable-libspeex --disable-libssh --disable-libtensorflow --disable-libtesseract --disable-libtheora --disable-libvorbis --disable-libvpx --disable-libwebp --disable-libx264 --disable-libx265 --disable-libxml2 --disable-zlib --disable-libsrt --disable-libmfx --enable-cross-compile --disable-static --enable-shared --extra-cflags=-DHAVE_UNISTD_H=0 --pkg-config="C:/Users/ambro/AppData/Local/vcpkg/downloads/tools/msys2/21caed2f81ec917b/mingw64/bin/pkg-config.exe" --disable-optimizations --extra-ldflags=-libpath:"C:/fork/synthizer3d/vcpkg_installed/x64-windows/debug/lib" --arch=${BUILD_ARCH} ${!OPTION_VARIABLE} $@

    echo "=== BUILDING ==="

    make -j${JOBS} V=1

    echo "=== INSTALLING ==="

    make install
}

cd "$PATH_TO_BUILD_DIR"

if [ $OSX_ARCH_COUNT -gt 0 ]; then
    for ARCH in $OSX_ARCHS; do
        echo "=== CLEANING FOR $ARCH ==="

        make clean && make distclean

        build_ffmpeg $ARCH --extra-cflags=-arch --extra-cflags=$ARCH --extra-ldflags=-arch --extra-ldflags=$ARCH

        echo "=== COLLECTING BINARIES FOR $ARCH ==="

        move_binaries $PATH_TO_PACKAGE_DIR/lib $PATH_TO_BUILD_DIR/stage/$ARCH/lib
        move_binaries $PATH_TO_PACKAGE_DIR/bin $PATH_TO_BUILD_DIR/stage/$ARCH/bin
    done

    echo "=== MERGING ARCHITECTURES ==="

    merge_binaries $PATH_TO_PACKAGE_DIR $PATH_TO_BUILD_DIR/stage $OSX_ARCHS
else
    build_ffmpeg x86_64
fi
