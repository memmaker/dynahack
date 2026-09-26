#!/bin/sh
# Build DynaHack for the browser (Emscripten + Asyncify) into web/dist.
# web/webwin.c is the client (window procs + command loop, in the place
# of nitrohack/), web/dynahack.js draws (tiles: web/mktiles.py), rvip-wm.js places the windows.
# Data: the game's own tools (makedefs, dgn_comp, lev_comp, dlb) built with
# emcc and run under node, because their binary output holds native longs
# (8 bytes natively, 4 in wasm32).  Needs emcc, node, bison, flex.
set -e
cd "$(dirname "$0")/.."
OUT=web/dist G=web/gen
L=libnitrohack U=libnitrohack/util
mkdir -p "$G/include" "$G/src" "$G/util" "$G/dat" "$G/tools"
CI="-Iinclude -I$L/include -I$G/include -I$G/util"
CT="-O1 -w -fcommon -std=gnu99 -DSTATIC_BUILD $CI -sNODERAWFS -sENVIRONMENT=node -sEXIT_RUNTIME=1 -sALLOW_MEMORY_GROWTH -sUSE_ZLIB=1"
T=$G/tools

# generated headers + data once; rm -rf web/gen to redo them
if [ ! -f $G/stage/nhdat ]; then
emcc $CT $U/makedefs.c $L/src/monst.c $L/src/objects.c -o $T/makedefs.js
for o in "-v date.h" "-o onames.h" "-p pm.h" "-w verinfo.h"; do
	set -- $o; node $T/makedefs.js $1 "$G/include/$2"
done
node $T/makedefs.js -m "$G/src/monstr.c"

bison -y --defines=$G/util/dgn_comp.h -o $G/util/dgn_parser.c $U/dgn_comp.y
flex -o$G/util/dgn_scanner.c $U/dgn_comp.l
bison -y --defines=$G/util/lev_comp.h -o $G/util/lev_parser.c $U/lev_comp.y
flex -o$G/util/lev_scanner.c $U/lev_comp.l
emcc $CT $U/dgn_main.c $U/panic.c $G/util/dgn_parser.c $G/util/dgn_scanner.c -o $T/dgn_comp.js
emcc $CT $U/lev_main.c $U/panic.c $L/src/symclass.c $L/src/decl.c $L/src/monst.c \
	$L/src/objects.c $G/util/lev_parser.c $G/util/lev_scanner.c -o $T/lev_comp.js
emcc $CT $U/dlb_main.c $L/src/dlb.c -o $T/dlb.js

D=$L/dat
(cd $G/dat && rm -f ./* &&
	node ../tools/makedefs.js -d ../../../$D/data.base data &&
	node ../tools/makedefs.js -e ../../../$D/dungeon.def dungeon.pdf &&
	node ../tools/makedefs.js -q ../../../$D/quest.txt quest.dat &&
	node ../tools/makedefs.js -r ../../../$D/rumors.tru ../../../$D/rumors.fal rumors &&
	node ../tools/makedefs.js -h ../../../$D/oracles.txt oracles &&
	node ../tools/dgn_comp.js dungeon.pdf &&
	for d in ../../../$D/*.des; do node ../tools/lev_comp.js "$d" >/dev/null; done &&
	cp ../../../$D/history . &&
	node ../tools/dlb.js cf nhdat dungeon quest.dat rumors oracles *.lev history data)
echo "$(ls $G/dat/*.lev | wc -l) levels compiled"
mkdir -p $G/stage && cp $G/dat/nhdat $D/license $G/stage/
fi

rm -rf "$OUT" && mkdir -p "$OUT"
# tiles: the game's display symbol names (web/tiledump.c) matched against the
# NetHack 3.4.3-style text tiles (web/tiles/) -> tiles.png + C table
emcc $CT web/tiledump.c $L/src/drawing.c $L/src/objects.c $L/src/monst.c $L/src/decl.c \
	$L/src/symclass.c -o $T/tiledump.js
node $T/tiledump.js > $G/symbols.tsv
python3 web/mktiles.py web/tiles $G/symbols.tsv "$OUT/tiles.png" $G/src/tiletab.c
emcc -O2 -w -fcommon -std=gnu99 -DSTATIC_BUILD $CI -sUSE_ZLIB=1 \
	$L/src/*.c $G/src/monstr.c $G/src/tiletab.c web/webwin.c \
	--preload-file "$G/stage@/dynahack-data" -o "$OUT/dynahack-core.js" \
	-sASYNCIFY -sASYNCIFY_STACK_SIZE=131072 -sSTACK_SIZE=2097152 \
	-sALLOW_MEMORY_GROWTH -sEXIT_RUNTIME=1 -sINITIAL_MEMORY=64MB \
	-sEXPORTED_FUNCTIONS=_main \
	-sEXPORTED_RUNTIME_METHODS=FS,IDBFS,HEAP32 \
	-sFORCE_FILESYSTEM -lidbfs.js -sENVIRONMENT=web
cp web/index.html web/dynahack.js "$HOME/Games/rvip-tools/web/rvip-wm.js" "$OUT/"
if [ -f web/make-help.py ]; then python3 web/make-help.py > "$OUT/help.html"
else echo "note: help.html not built yet (web/make-help.py comes in RVIP stage 6)"; fi
ls -la "$OUT"
