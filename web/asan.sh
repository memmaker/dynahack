#!/bin/sh
# Native AddressSanitizer run of libnitrohack + web/webwin.c (headless:
# random keys, then S to save), twice: new game, then restore + play + save.
# Needs the native cmake build in build/ (generated headers, monstr.c, and a
# native nhdat: the wasm one in web/gen has 4-byte longs), e.g.
#   mkdir build && cd build && cmake -DALL_STATIC=TRUE -DUSE_OSX_HOMEBREW_CURSES=TRUE \
#     -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_C_FLAGS="-w -fcommon -std=gnu89 \
#     -I/opt/homebrew/opt/ncurses/include -DNCURSES_WIDECHAR=1" .. && make -j8
# Objects go to a temp dir that is deleted afterwards.
set -e
cd "$(dirname "$0")/.."
T=$(mktemp -d) B=build/libnitrohack
cc -g -O1 -fsanitize=address -fno-omit-frame-pointer -w -fcommon -std=gnu99 -DSTATIC_BUILD \
	-Iinclude -Ilibnitrohack/include -I$B/include \
	libnitrohack/src/*.c $B/src/monstr.c web/gen/src/tiletab.c web/webwin.c -lz -o "$T/dynahack"
cp $B/dat/nhdat libnitrohack/dat/license "$T/"
cd "$T"
for run in 1 2 3; do
	echo "== run $run"; ASAN_OPTIONS=detect_leaks=0 ./dynahack || echo "exit $?"
	ls save log 2>/dev/null
done
cd / && rm -rf "$T"
