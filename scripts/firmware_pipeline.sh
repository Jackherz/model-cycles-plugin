#!/bin/zsh
# A firmware build to an engine state, unattended (NOTES.md steps 16-17):
# cold boot in digiemu, render coverage over every machine, a driven
# capture, the state dump. Checks, translation and the plugin build follow
# by hand. ROOT is a folder holding SYX and a devices/ overlay.
#   scripts/firmware_pipeline.sh ROOT SYX [MACHINES] [extra env for coverage, e.g. FILTER=1]
set -e
P=${0:A:h:h}
ROOT=$1 SYX=$2 MACHINES=${3:-"0 1 2 3 4 5 6 7 8 9 10"}
shift 3 2>/dev/null || shift $#
cd $P/re/digiemu
export CYCLES_FW=$ROOT:$SYX CYCLES_MACHINES=$(( ${#${(z)MACHINES}} ))
echo "== boot"; uv run --no-sync python ../../scripts/boot_firmware.py ../../$ROOT $SYX | tail -3
echo "== coverage"; env MACHINES="$MACHINES" COV_OUT=../../$ROOT/coverage.json "$@" $P/scripts/coverage_batch.sh | tail -3
echo "== capture"
uv run --no-sync python ../../scripts/capture.py --calls 200 --midi 20:90:3c:64 --midi 40:b1:46:08 \
  --midi 50:91:3c:70 --midi 60:b0:50:10 --midi 70:b2:46:0a --midi 80:92:40:7f --midi 100:b0:55:30 \
  --midi 110:b3:46:7f --midi 120:80:3c:00 --midi 130:93:3c:60 --midi 140:b1:46:05 --midi 150:91:30:70 \
  --midi 160:b0:4a:20 --midi 165:b1:4a:68 --midi 168:b1:47:60 --midi 170:b0:5e:7f \
  --out ../../$ROOT/cap-driven | tail -2
echo "== state"; uv run --no-sync python ../../aot/dump_state.py ../../$ROOT/cap-driven ../../$ROOT/aot-state
echo "== init sounds"; uv run --no-sync python ../../scripts/init_sounds.py ../../$ROOT/cap-driven \
  $CYCLES_MACHINES ../../$ROOT/aot-state/defaults.json | tail -1
echo "== done"
