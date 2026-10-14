#!/usr/bin/env bash
# Build a bfin-elf GCC (C, C++) with newlib in WSL/Linux, on top of the binutils
# installed by build_bfin_toolchain.sh (run that first).
# GMP/MPFR/MPC are built in-tree via download_prerequisites, so no extra apt packages.
# Result: $PREFIX/bin/bfin-elf-gcc, bfin-elf-g++
set -euo pipefail

GCC_VERSION=${GCC_VERSION:-14.2.0}
NEWLIB_VERSION=${NEWLIB_VERSION:-4.4.0.20231231}
WORK=${WORK:-$HOME/op1-toolchain}
PREFIX=${PREFIX:-$WORK/bfin}
JOBS=$(nproc)
export PATH="$PREFIX/bin:$PATH"

command -v bfin-elf-as >/dev/null || { echo "bfin-elf binutils not found in $PREFIX/bin"; exit 1; }

mkdir -p "$WORK/src"
cd "$WORK/src"
[ -f gcc-$GCC_VERSION.tar.xz ] || wget -q https://ftp.gnu.org/gnu/gcc/gcc-$GCC_VERSION/gcc-$GCC_VERSION.tar.xz
[ -d gcc-$GCC_VERSION ] || tar xf gcc-$GCC_VERSION.tar.xz
[ -f newlib-$NEWLIB_VERSION.tar.gz ] || wget -q https://sourceware.org/pub/newlib/newlib-$NEWLIB_VERSION.tar.gz
[ -d newlib-$NEWLIB_VERSION ] || tar xf newlib-$NEWLIB_VERSION.tar.gz
(cd gcc-$GCC_VERSION && [ -e gmp ] || ./contrib/download_prerequisites --no-isl)

GCC_COMMON=(--target=bfin-elf --prefix="$PREFIX" --disable-nls --disable-werror
            --disable-shared --disable-threads --disable-libssp --disable-libquadmath
            --disable-libgomp --disable-libstdcxx-pch --with-newlib)

# Stage 1: compiler and libgcc only.
mkdir -p "$WORK/build-gcc1" && cd "$WORK/build-gcc1"
"../src/gcc-$GCC_VERSION/configure" "${GCC_COMMON[@]}" --enable-languages=c --without-headers
make -j"$JOBS" all-gcc all-target-libgcc && make install-gcc install-target-libgcc

# newlib (C library) for bfin-elf.
mkdir -p "$WORK/build-newlib" && cd "$WORK/build-newlib"
"../src/newlib-$NEWLIB_VERSION/configure" --target=bfin-elf --prefix="$PREFIX" --disable-nls
make -j"$JOBS" && make install

# Stage 2: full C and C++ compiler against newlib.
mkdir -p "$WORK/build-gcc2" && cd "$WORK/build-gcc2"
"../src/gcc-$GCC_VERSION/configure" "${GCC_COMMON[@]}" --enable-languages=c,c++
make -j"$JOBS" && make install

ls -l "$PREFIX/bin"
