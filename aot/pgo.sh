#!/bin/zsh
# Profile data for a profile-guided build of the render (about -35% time per
# block, NOTES.md step 15): build an instrumented bench, train it on the worst
# case (six Chords), on every machine in turn (BENCH_MODE=mix) and on the
# kit, and merge the counts into build/render.profdata. The plugin's CMake
# uses that file when it exists. Rerun after translate.py changes render.c
# (a stale profile is ignored, with a warning, and the build is just slower).
#   pgo.sh [STATE_DIR [BUILD_DIR]]    (BUILD_DIR: build, or build-st for the
#                                      Syntakt-engine build, NOTES.md step 16)
cd "$(dirname "$0")"
S=${1:-"$HOME/Library/Application Support/CyclesEmu/state"}
B=${2:-build}
clang -O2 -fprofile-instr-generate -Wno-parentheses-equality \
    -o $B/bench-gen bench.c engine.c runtime.c $B/render.c || exit 1
rm -f $B/pgo-*.profraw(N)
LLVM_PROFILE_FILE=$B/pgo-chord.profraw $B/bench-gen "$S" 10 || exit 1
# every machine the build has: 11 with the Syntakt engines (build-st)
M=6; [[ $B == *-st ]] && M=11
BENCH_MODE=mix BENCH_MACHINES=$M LLVM_PROFILE_FILE=$B/pgo-mix.profraw $B/bench-gen "$S" 44 || exit 1
BENCH_MODE=kit LLVM_PROFILE_FILE=$B/pgo-kit.profraw $B/bench-gen "$S" 10 || exit 1
xcrun llvm-profdata merge -o $B/render.profdata $B/pgo-*.profraw && echo "-> $B/render.profdata"
