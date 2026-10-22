#!/bin/zsh
# Profile data for a profile-guided build of the render (about -35% time per
# block, NOTES.md step 15): build an instrumented bench, train it on the worst
# case (six Chords), on every machine in turn (BENCH_MODE=mix) and on the
# kit, and merge the counts into build/render.profdata. The plugin's CMake
# uses that file when it exists. Rerun after translate.py changes render.c
# (a stale profile is ignored, with a warning, and the build is just slower).
#   pgo.sh [STATE_DIR]
cd "$(dirname "$0")"
S=${1:-"$HOME/Library/Application Support/CyclesEmu/state"}
clang -O2 -fprofile-instr-generate -Wno-parentheses-equality \
    -o build/bench-gen bench.c engine.c runtime.c build/render.c || exit 1
rm -f build/pgo-*.profraw(N)
LLVM_PROFILE_FILE=build/pgo-chord.profraw build/bench-gen "$S" 10 || exit 1
BENCH_MODE=mix LLVM_PROFILE_FILE=build/pgo-mix.profraw build/bench-gen "$S" 30 || exit 1
BENCH_MODE=kit LLVM_PROFILE_FILE=build/pgo-kit.profraw build/bench-gen "$S" 10 || exit 1
xcrun llvm-profdata merge -o build/render.profdata build/pgo-*.profraw && echo "-> build/render.profdata"
