#!/bin/zsh
# Build the AOT render library (and the -DMC_TRACE variant with --trace).
cd "$(dirname "$0")"
python3 translate.py || exit 1
FLAGS=(-O2 -fPIC -shared -Wno-parentheses-equality)
if [[ $1 == --trace ]]; then
  clang $FLAGS -DMC_TRACE -o build/libmcrender_trace.dylib runtime.c build/render.c
else
  clang $FLAGS -o build/libmcrender.dylib runtime.c build/render.c
fi
