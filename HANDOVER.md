# DynaHack — handover

Web port of DynaHack 0.6.0 (all RVIP stages 1–9 done, live at
https://ruzzoli.de/roguelikes/dynahack/). Procedure: `~/Games/rvip-tools/RVIP.md`.

## Source and repo
- Upstream https://github.com/tung/DynaHack, branch `unnethack`, commit `25aaf2a`
  (2016-02-17); empty marker commit `2cacdfa1` "upstream DynaHack 0.6.0 @ 25aaf2a",
  then ours. Repo https://github.com/memmaker/dynahack (remote `memmaker`, branch
  `unnethack`); README links upstream and the compare view `25aaf2a...unnethack`.
- Case O, NetHack4 family (NitroHack → DynaHack, UnNetHack content): game library
  `libnitrohack/` (API `include/nitrohack.h`, `nh_command()` driven by the client).
  The curses client `nitrohack/` is not used; `web/webwin.c` replaces it.

## Build and deploy
- `sh web/build.sh` → `web/dist` (no native build). Data tools (`makedefs`,
  `dgn_comp`, `lev_comp`, `dlb`) are compiled with emcc and run under node into
  `web/gen/` (native tools write 8-byte longs); `rm -rf web/gen` redoes the data.
  Game: `libnitrohack/src/*.c` + `web/gen/src/monstr.c` + `web/webwin.c`
  (flags in build.sh: `-sASYNCIFY -sUSE_ZLIB=1 -lidbfs.js …`).
- `web/asan.sh`: native `-fsanitize=address` build with headless random-key stubs
  (needs brew cmake/ncurses; full cmake command inside).
- `web/deploy.sh` → `ruzzoli.de:/var/www/ruzzoli.de/roguelikes/dynahack`.
- Shared page code from the parent folder: `../rvip-wm.js`, `../rvip-app.js`,
  `../rvip-sound.js` (loaded, not copied).

## File map
- `web/webwin.c`: window procs, command loop, character selection, save discovery,
  Enter menu (`cmd_menu()`), inventory actions (`item_action()`, `set_next()`),
  tiles (`cell_tile()`), Visible window (`js_text(7)`), prompt line (`js_text(8)`),
  sound (`sound_msg()`, `sound_level()`), beacon `js_beacon`.
- `web/dynahack.js` (draws; layout in `/dynahack/web-layout.json`), `web/index.html`.
- `web/tiledump.c` → `web/gen/symbols.tsv` → `web/mktiles.py` → `dist/tiles.png` +
  `web/gen/src/tiletab.c`. `web/make-help.py` → `dist/help.html`.
  `web/mksounds.py` → `dist/sound/*.wav` (own synth, CC0).
- Library changes: `do.c` `dostairwalk()` / `walk_to_stairs()` (command
  `stairwalk`, sent by webwin.c for `<`/`>`; old logs with `move` replay
  unchanged), `pline.c` `vpline()` (any message stops explore/stairs walk),
  `hack.c` (TEST_TRAV refuses boulders during autoexplore), `cmd.c` `cmdlist[]`
  (`~` = autoexplore altkey, help texts), `end.c` `done()` → beacon.

## Facts and gotchas
- Files: `nhdat` + `license` preloaded read-only at `/dynahack-data`; IDBFS at
  `/dynahack` holds `save/<time>_<name>.nhgame`, `log/`, `dumps/`, record, layout.
- Saves: the `.nhgame` file is the game's continuous log. JS syncs IDBFS every 2 s
  while idle, every 15 s and on hide, so a reload without saving replays the log
  (crash = autosave). `S` saves and ends. Replay of T:720 takes ≈1.5 s.
- Never call state-changing library functions (`nh_describe_pos()` →
  `describe_object()` → `mksobj()`) outside the log: replay desyncs. That's why
  Visible lists no objects.
- Library `xmalloc()` results die after the next API call; webwin.c copies them.
- NetHack4 `enum nh_direction` order is W NW N NE E SE S SW (`"hykulnjb"`).
- Tiles: SLASH'EM's NetHack 3.4.3-style set only (`web/tiles/`), 1000/1132 real
  (88.3 %) + 132 same-set stand-ins (user decision 2026-09-29; upstream has no
  tiles). Build fails if a symbol falls through or real coverage drops below 88 %.
  Objects match by class + description (appearance), not name.
- Beacon: `ev` ASCENDED/DEFIED = win, QUIT/ESCAPED = quit, else death; killer =
  species name via `killer_mon`. Win path checked in code only.
- Browser pane: send `<`/`>` via a dispatched `KeyboardEvent`; a background tab
  gets no `resize`; delete the `/dynahack` DB from a plain page on the same origin.

## Open
- Every message stops explore, incl. `[HP+1=…]` regen notes and "You hear…".
- A hostile that stays in view stops each explore press after one step, silently.
- `<`/`>` pick the nearest stairs by straight-line distance; levitating `>` just
  says floating.
- Visible lists no objects (needs a side-effect-free appearance name in the lib).
- `web_getpos()` calls `nh_describe_pos()` (mksobj) for its cursor text: check it
  can't desync replay.
- Map clicks ignored; `O` options menu not wired (unbound, "Unknown command").
- Reopened inventory starts at the top; item menus have no floor/equipment lists;
  `hostile_in_view()` counts remembered detected monsters.
- No pet marker. Magic chest in item lists shows the tile before (library
  `otype = CHEST`, not +1).
- No shop music (API has no shop flag); town music not heard in real play.
- The library echoes answered prompts (`<Really save? [yn]: n>`) into the prompt
  line until the next command key.
- Shrine: Sean Hunt as NetHack4 co-author and Advent Calendar trigger unverified.
