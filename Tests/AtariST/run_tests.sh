#!/bin/sh
# Atari ST conversion tests : synthesize flux dumps, convert them to IPF with SugarConvDsk,
# then decode the IPF with the SPS capsimage library and compare every sector with the source.
#
# Usage : run_tests.sh /path/to/SugarConvDsk /path/to/libcapsimage.so [workdir]
# libcapsimage can be built from https://github.com/FrodeSolheim/capsimg (./bootstrap && ./configure && make)
set -e
BIN=$1
LIB=$2
WORK=${3:-./st_test_out}
HERE=$(cd "$(dirname "$0")" && pwd)
mkdir -p "$WORK"
cd "$WORK"
FAIL=0

check() { # name spt sides expect-pattern...
   name=$1; spt=$2; sides=$3; shift 3
   out=$(python3 "$HERE/ipfcheck.py" "$LIB" "${name}_out.IPF" "$name.st" --spt "$spt" --sides "$sides" --quiet)
   echo "$out" | sed "s/^/  /"
   echo "$out" | grep -q "platform=\['Atari ST'\]" || { echo "  FAIL: platform"; FAIL=1; }
   for pat in "$@"; do
      echo "$out" | grep -q -- "$pat" || { echo "  FAIL: expected '$pat'"; FAIL=1; }
   done
}

run_scp() { # name spt sides convert-opts -- mkst-opts -- expectations
   name=$1; spt=$2; sides=$3; shift 3
   python3 "$HERE/mkst.py" "$name" --spt "$spt" --sides "$sides" "$@" > /dev/null
   echo "== $name"
   "$BIN" "$name.scp" "${name}_out" -o=IPF > /dev/null || true
}

run_scp ds80_9 9 2 --cyls 80 --revs 2;                 check ds80_9 9 2 "tracks_with_errors=0 sectors_checked=1440"
run_scp ss80_9 9 1 --cyls 80 --revs 2;                 check ss80_9 9 1 "head=0-0" "tracks_with_errors=0 sectors_checked=720"
run_scp ss80_seq 9 1 --cyls 80 --revs 2 --sequential;  check ss80_seq 9 1 "tracks_with_errors=0 sectors_checked=720"
run_scp ds82_10 10 2 --cyls 82 --revs 2;               check ds82_10 10 2 "cyl=0-81" "tracks_with_errors=0"
run_scp ds83_11 11 2 --cyls 83 --revs 2;               check ds83_11 11 2 "cyl=0-82" "tracks_with_errors=0"
run_scp copylock 9 2 --cyls 80 --revs 2 --copylock 0;  check copylock 9 2 "00.0 .*density R6:1050" "tracks_with_errors=0"
run_scp longtrk 9 2 --cyls 80 --revs 2 --long 1;       check longtrk 9 2 "01.0 .*sectors=10 .*long track" "tracks_with_errors=0"

echo "== kryoflux"
python3 "$HERE/mkst.py" kf --cyls 82 --spt 9 --revs 2 --copylock 0 --kf > /dev/null
"$BIN" kf_kf/track00.0.raw kf_out -o=IPF > /dev/null || true  # exit code is 1 for KryoFlux sources
check kf 9 2 "cyl=0-81" "00.0 .*density R6:1050" "tracks_with_errors=0"

echo "== SCP writer round trip (IPF -> SCP -> IPF : Copylock timing, 82 cylinders, single side)"
for name in copylock ds82_10 ss80_9; do
   "$BIN" "${name}_out.IPF" "${name}_rt" -o=SCP > /dev/null || true
   "$BIN" "${name}_rt.SCP" "${name}_rt" -o=IPF > /dev/null || true
   cp "${name}_rt.IPF" "${name}_out.IPF"
done
check copylock 9 2 "00.0 .*density R6:1050" "tracks_with_errors=0"
check ds82_10 10 2 "cyl=0-81" "tracks_with_errors=0"
check ss80_9 9 1 "head=0-0" "tracks_with_errors=0 sectors_checked=720"

[ $FAIL -eq 0 ] && echo "ALL ATARI ST TESTS PASSED" || { echo "SOME ATARI ST TESTS FAILED"; exit 1; }
