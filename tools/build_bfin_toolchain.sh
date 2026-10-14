#!/usr/bin/env bash
# Build Blackfin (bfin-elf) binutils and GDB (with the bfin simulator) in WSL/Linux.
# Needs: build-essential bison flex texinfo libgmp-dev libmpfr-dev libexpat1-dev zlib1g-dev
# Result: $PREFIX/bin/bfin-elf-objdump, bfin-elf-gdb, bfin-elf-run
set -euo pipefail

BINUTILS_VERSION=2.43
GDB_VERSION=15.2
WORK=${WORK:-$HOME/op1-toolchain}
PREFIX=${PREFIX:-$WORK/bfin}
JOBS=$(nproc)

mkdir -p "$WORK/src" "$PREFIX"
cd "$WORK/src"

for pkg in binutils-$BINUTILS_VERSION gdb-$GDB_VERSION; do
    name=${pkg%-*}
    [ -f $pkg.tar.xz ] || wget -q https://ftp.gnu.org/gnu/$name/$pkg.tar.xz
    [ -d $pkg ] || tar xf $pkg.tar.xz
done

mkdir -p "$WORK/build-binutils" && cd "$WORK/build-binutils"
../src/binutils-$BINUTILS_VERSION/configure --target=bfin-elf --prefix="$PREFIX" \
    --disable-nls --disable-werror --disable-gdb --disable-sim
make -j"$JOBS" && make install

mkdir -p "$WORK/build-gdb" && cd "$WORK/build-gdb"
../src/gdb-$GDB_VERSION/configure --target=bfin-elf --prefix="$PREFIX" \
    --disable-nls --disable-werror --enable-sim --with-python=no
make -j"$JOBS" all-gdb all-sim && make install-gdb install-sim

ls -l "$PREFIX/bin"
