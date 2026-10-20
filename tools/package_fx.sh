#!/bin/bash
# package_fx.sh NAME "ENGINES": build op1-seq with those engines, inject it
# into a copy of work_fx2, add crush.svg and each engine's svg, build the
# package op1_246_rings_NAME.op1, and check it (verify_bootloader, every
# hook target a symbol, the diff against work_fx2).
set -e
NAME=$1
ENGINES=$2
REPO=/i/GitHub/PersonalProjects/OP1.CustomFirmware
cd $REPO
F="op1 firmware"
S=$(mktemp -d)   # scratch: nm output, symbol and hook lists
wsl -e bash -lc "cd /mnt/i/GitHub/PersonalProjects/OP1.CustomFirmware/ports/rings && rm -f build/op1_seq.elf && make op1-seq FX_ENGINES='$ENGINES' 2>&1 | grep -E 'error|undefined|^\.text|^\.bss' | grep -v RWX; test -f build/op1_seq.elf"
wsl -e bash -lc "cd /mnt/i/GitHub/PersonalProjects/OP1.CustomFirmware/ports/rings && ~/op1-toolchain/bfin/bin/bfin-elf-nm build/op1_seq.elf" | tr -d '\r' > $S/nm_$NAME.txt
if grep -q '_GLOBAL__sub_I' $S/nm_$NAME.txt; then echo "static initialiser!"; exit 1; fi
U=$(wsl -e bash -lc "cd /mnt/i/GitHub/PersonalProjects/OP1.CustomFirmware/ports/rings && ~/op1-toolchain/bfin/bin/bfin-elf-nm -u build/op1_seq.elf | wc -l" | tr -d '\r')
echo "undefined symbols: $U"
END=$(grep ' ___image_end$' $S/nm_$NAME.txt | awk '{print $1}')
echo "image end: 0x$END (limit 0x03000000)"
W="$F/work_$NAME"
rm -rf "$W"
cp -r "$F/work_fx2" "$W"
.venv/Scripts/python tools/inject_app.py "$F/op1_246/OP1_vdk.ldr" "$W/OP1_vdk.ldr.new" ports/rings/build/op1_seq.elf > "$F/work_${NAME}_inject.log" 2>&1
mv "$W/OP1_vdk.ldr.new" "$W/OP1_vdk.ldr"
cp ports/fx/op1/crush.svg "$W/content/display/"
# The Mutable Instruments screens (replacing the base work dir's copies).
for m in rings clouds marbles grids; do cp ports/$m/op1/$m.svg "$W/content/display/"; done
for e in $ENGINES; do
  svg=$(echo $e | tr -d '_').svg
  [ $e = tape ] && svg=tapesat.svg
  # Never replace a stock file.
  if [ -e "$F/op1_246/content/display/$svg" ]; then echo "STOCK NAME CLASH: $svg"; exit 1; fi
  cp ports/fx/op1/$svg "$W/content/display/"
done
rm -f "$F/op1_246_rings_$NAME.op1"
.venv/Scripts/python tools/build_package.py "$W" "$F/op1_246_rings_$NAME.op1" 2>&1 | tail -4
awk '{print $1}' $S/nm_$NAME.txt | sort -u > $S/syms_$NAME.txt
tr -d '\r' < "$F/work_${NAME}_inject.log" | grep -oE -- '-> (0x)?02c[0-9a-f]{5}' | sed -E 's/-> (0x)?//' | sort -u > $S/targets_$NAME.txt
good=0; bad=0
while read t; do
  if grep -qx "0*$t" $S/syms_$NAME.txt; then good=$((good+1)); else bad=$((bad+1)); echo "bad hook $t"; fi
done < $S/targets_$NAME.txt
echo "hooks: $good good, $bad bad"
grep "insn16" "$F/work_${NAME}_inject.log" | tr -d '\r'
diff -rq "$F/work_fx2" "$W" || true
.venv/Scripts/python tools/pkg_check.py "$F/op1_246_rings_$NAME.op1" "$W" | tail -1 | grep -q "mismatches 0" || { echo "PACKAGE != WORK DIR"; exit 1; }
[ $bad = 0 ]
