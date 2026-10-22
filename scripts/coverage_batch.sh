#!/bin/zsh
# Accumulate render coverage over many scenarios (scripts/coverage.py).
# For another firmware folder: CYCLES_FW (see emuhost.py), MACHINES (the
# machine numbers, default 0-5) and COV_OUT (the coverage file), e.g.
#   CYCLES_FW=data-st:model-cycles_OS1.13_syntakt5.syx MACHINES="0 1 2 3 4 5 6 7 8 9 10" \
#     COV_OUT=../../data-st/coverage.json scripts/coverage_batch.sh
cd "$(dirname "$0")/../re/digiemu"
MACHINES=(${=MACHINES:-0 1 2 3 4 5})
OUT=(--out ${COV_OUT:-../../data/coverage.json})
run() { uv run --no-sync python ../../scripts/coverage.py $OUT "$@" 2>&1 | grep -v '^\[' | tail -1; }
FX=(--cc all:80:127 --cc all:12:127 --cc all:13:127)
run --label pattern --instrs 32M
run --label stopped --stop --instrs 16M
for m in $MACHINES; do
  for cs in "0 0" "64 64" "127 127" "0 127" "127 0"; do
    c=${cs% *}; s=${cs#* }
    run --label "mach$m c$c s$s" --cc all:70:$m $FX --cc all:16:$c --cc all:17:$s --retrig all:60:127 --instrs 12M
  done
done
for w in 0 1 2 3 4 5 6; do
  for d in 0 20 40 60 80 100 127; do
    run --label "lfo w$w d$d" --cc all:106:$w --cc all:105:$d --cc all:109:127 --cc all:102:120 --retrig all:48:100 --instrs 6M
  done
done
for mul in 0 12 23; do for rst in 0 2 4; do
  run --label "lfo mul$mul rst$rst" --cc all:103:$mul --cc all:108:$rst --cc all:109:64 --cc all:104:100 --retrig all:60:90 --instrs 6M
done; done
# the knobs the added machines map differently (Syntakt engines: SWEEP,
# CONTOUR and PUNCH are their own parameters)
for m in $MACHINES; do
  [[ $m -lt 6 ]] && continue
  for v in 0 64 127; do
    run --label "mach$m sw/ct/punch $v" --cc all:70:$m $FX --cc all:18:$v --cc all:19:$((127 - v)) \
      --cc all:66:$((v > 63)) --cc all:16:$v --retrig all:48:110 --instrs 10M
  done
done
run --label "punch gate" --cc all:66:1 --cc all:67:1 --retrig all:60:127 --instrs 8M
run --label "pitch lo" --cc all:65:0 --cc all:18:127 --cc all:19:127 --retrig all:30:127 --instrs 8M
run --label "pitch hi" --cc all:65:127 --cc all:18:0 --cc all:19:0 --retrig all:90:1 --instrs 8M
run --label "fx max" --cc all:85:127 --cc all:86:127 --cc all:87:127 --cc all:88:127 --cc all:7:127 --retrig all:60:127 --instrs 8M
run --label "fx min" --cc all:85:0 --cc all:86:0 --cc all:87:0 --cc all:88:0 --cc all:7:0 --cc all:10:0 --retrig all:60:127 --instrs 8M
run --label "pan hi level lo" --cc all:10:127 --cc all:95:10 --retrig all:60:127 --instrs 8M
run --label "muted" --cc all:94:127 --retrig all:60:127 --instrs 8M
