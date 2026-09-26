# DynaHack — handover

## RVIP progress

### Stage 1 (get + build) — done 2026-09-26
- Folder `~/Games/dynahack`. Upstream https://github.com/tung/DynaHack, branch
  `unnethack`, commit `25aaf2ab6a27a9104864d22337d7117c7d261571` (2016-02-17),
  **DynaHack 0.6.0** (`include/nitrohack.h`). Clone unshallowed (full history);
  marker commit `upstream DynaHack 0.6.0 @ 25aaf2a` (empty) on top, then ours.
  No remote besides `origin`; nothing pushed.
- **Case O**, NetHack4 family (NitroHack → DynaHack, UnNetHack content): game
  library `libnitrohack/` (API `include/nitrohack.h`, `struct nh_window_procs`,
  `nh_command()` driven by the client), curses client `nitrohack/` (not used on
  the web). From O-SLASH'EM applies: wasm-built data tools run under node;
  `toplines`/`parse()` notes do NOT apply (different API). O-Hack: nothing.
- **Web frontend:** `web/webwin.c` (window procs + command loop + character
  selection + save discovery, replaces `nitrohack/`), `web/dynahack.js` (draws,
  from SLASH'EM's `slashem.js`), `web/index.html`, `web/build.sh`, `web/asan.sh`.
  JS protocol = SLASH'EM's: `js_map(cells, chars, x, y, lev)` (tile index -1 for
  now; char | colour << 8 from `nh_get_drawing_info()`), `js_text(id, s)`
  0 prompt / 1 status / 2 inventory / 3 pop-up / 4-6 messages, rows
  `tile\tletter\tsel\tcolour\ttext`; `js_key(peek, at_cmd)`; `js_end()`.
- **Build:** `sh web/build.sh` → `web/dist` (no native build needed). Data: the
  game's own `makedefs`/`dgn_comp`/`lev_comp`/`dlb` compiled with emcc
  (`-sNODERAWFS -sENVIRONMENT=node -sUSE_ZLIB=1`) and run under node into
  `web/gen/` (headers, `monstr.c`, 176 `.lev`, `nhdat` 1 MB) — native tools
  write 8-byte longs. `rm -rf web/gen` redoes the data. Game: all
  `libnitrohack/src/*.c` + `web/gen/src/monstr.c` + `web/webwin.c`, emcc
  `-O2 -w -fcommon -std=gnu99 -DSTATIC_BUILD -sUSE_ZLIB=1 -sASYNCIFY
  -sASYNCIFY_STACK_SIZE=131072 -sSTACK_SIZE=2097152 -sALLOW_MEMORY_GROWTH
  -sEXIT_RUNTIME=1 -sINITIAL_MEMORY=64MB -sEXPORTED_RUNTIME_METHODS=FS,IDBFS,HEAP32
  -sFORCE_FILESYSTEM -lidbfs.js -sENVIRONMENT=web`. No function-pointer-cast
  warnings (`-Wcast-function-type-strict` clean).
- **Files:** `nhdat` + `license` preloaded read-only at `/dynahack-data`
  (DATAPREFIX); IDBFS mounted at `/dynahack` (DB name `/dynahack`) holds
  `save/<time>_<name>.nhgame`, `log/` (finished games), `dumps/`, record,
  `web-layout.json`. Deviation from "preload to /dynahack": a preload under
  the IDBFS mount would be hidden or copied into IndexedDB (SLASH'EM copies
  its seed every load); a separate read-only prefix avoids both.
- **Saves:** the `.nhgame` file is DynaHack's continuous game log. `S` = save
  and end (overlay "Play again"); on load `find_save()` takes the newest
  saved/crashed log and `nh_restore_game()` restores it; a closed tab leaves
  it "inpr" = crashed and the library replays the log (tested: T:28 back after
  a reload without saving). JS syncs IDBFS every 2 s while waiting for a key
  plus every 15 s / on hide, so the log is always current = autosave for free.
  Finished games move to `log/`.
- **auto_more (3d):** `win_pause(P_MESSAGE)` is a no-op; `P_MAP` (detection
  display) waits for a key ("(press any key)" prompt). No `msg` option needed.
- **Native build (optional, only for `web/asan.sh`):** `brew` cmake/ncurses
  present; `cmake -DALL_STATIC=TRUE -DUSE_OSX_HOMEBREW_CURSES=TRUE
  -DCMAKE_POLICY_VERSION_MINIMUM=3.5 -DCMAKE_C_FLAGS="-w -fcommon -std=gnu89
  -I/opt/homebrew/opt/ncurses/include -DNCURSES_WIDECHAR=1" ..` (full command in
  `web/asan.sh`). `build/` removed afterwards.
- **ASan:** native `-fsanitize=address` build of libnitrohack + `webwin.c`
  with headless stubs (random keys, then Esc×8, `S`, `y`), 3 runs chained:
  new game → save → restore → play → save/death, repeated 4 times. Found and fixed
  (commit `port:`): `nh_get_savegame_status()` sscanf `%64s`/`%16s` one byte
  past the buffers; `makeplural()` "shuriken" test read before short strings.
  Plus my own use-after-free: the library frees `xmalloc()` results (drawing
  info, command list) after the next API call — `webwin.c` copies them.
- **Tiles decision (rule: own set ≥95%, else 3.4.3-style, never mix):**
  DynaHack ships **no tiles** (no `tilesets/`, no `win/share`). Counted by name
  (`nh_get_drawing_info` + `objects.c`/`monst.c`):
  - SLASH'EM `win/share` (NetHack 3.4.3-style): monsters 371/403 (92.1%),
    objects 538/538, map/traps/effects 175/181 → **1094/1132 glyphs = 96.6%**.
    Gaps: 32 monsters (DynaHack/UnNetHack dragons tatzelworm…guivre + babies,
    gold dragon, locust, enormous rat, rodent of unusual size, disintegrator,
    miner, prison guard, lava demon, giant turtle, convict, Robert the Lifer,
    Tiamat, Warden Arianna, inmate), swamp, dead tree, magic chest, vibrating
    square, shuriken trap, gas cloud.
  - nethack50 (3.7 set): monsters 348/403 (86%) → lower.
  - **Decision: SLASH'EM's 3.4.3-style set alone**, gaps filled by stand-ins
    from the same set (e.g. new dragons → existing dragons, gold dragon →
    yellow). Stage 1 build is **text mode** (coloured glyphs); stage 4 adds
    the sheet: map `nh_symdef` names → tile index in C (objects: dbuf `obj` is
    already the appearance index, `obfuscate_object()`; object symnames carry
    "wand of"/"ring of"… prefixes and "unnamed N" for nameless ones — match
    on class + description, not the bare name).
- **Tested in the browser pane** (own tab, port 8431, 1280×800): role/race/
  alignment menus (letters + Random) → name → intro pop-up → coloured map,
  Messages, Status (2 lines + status items), Inventory (coloured by object) →
  moves, kitten fights → `i` pop-up → `?` command list → `#` + Enter = all
  commands, `#version` → `S` y → overlay → reload restores → 300 random keys,
  no console errors → reload without saving replays. `/dynahack` DB deleted.
- **Quirks:** the library echoes every answered prompt as a message
  (`<Really save? [yn]: y>`); kept (game's own log). Commands come from
  `nh_get_commands()` (defkey/altkey, M- keys = Alt); hjklyubn move, HJKL… run,
  Ctrl+dir `go2` unless the Ctrl key is bound (^L redraw); digits = count.
  Map x starts at 1 (column 0 unused, as NetHack).
- **Open problems (later stages):** no tiles yet (stage 4); no Help button /
  help.html, no sound (stage 5/6); `PICK_INVACTION` inventory shows a plain
  list (stage 3 item menus); map clicks ignored; `O` options menu not wired
  (library options need a UI; `O` is unbound → "Unknown command"); bg hints
  (red stair background) dropped; `W1` card/help links still to do.
- **Next: stage 2 (explore + stairs).** DynaHack already has
  **`autoexplore` on `v`** (`libnitrohack/src/cmd.c` `doautoexplore()` sets
  `iflags.autoexplore`, stepping + stops in `hack.c` `autoexplore_msg()` /
  travel code, also `allmain.c`, `do.c`, `pickup.c`, `dothrow.c`) and
  **`travel` on `_`** (`dotravel()`). Stage 2 = check they meet RVIP step 2
  (doors, traps, stops), and make `<`/`>` off-stairs walk to known stairs
  (hook `web/webwin.c` `get_command()` or the library's `dodown`/`doup` in
  `do.c` via its travel code).

Suggested RVIP.md lessons (not edited): NetHack4-family games need a new
client in place of `nitrohack/` (window procs + command loop, ~1000 lines);
`xmalloc()` results die after the next API call; the `.nhgame` log gives
crash-proof autosave if IDBFS syncs while idle; a headless random-key stub
build of the web client makes native ASan cheap; `DIR` is a libc type name.
