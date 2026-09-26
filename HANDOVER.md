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

### Stage 2 (explore + stairs) — done 2026-09-26
- **Explore = DynaHack's own `autoexplore`**, key `v`, plus `~` as its altkey
  (`libnitrohack/src/cmd.c` `cmdlist[]`; `~` was unbound). It already met
  step 2 except one gap: BFS over the player's memory (`hack.c` `unexplored()`
  on `mem_bg`/`mem_stepped`, `findtravelpath()`), one step per turn (multi
  loop in `allmain.c` `command_input()`), stops on a hostile in view
  (`lookaround()`), avoids seen traps, water, lava, swamp (`test_move()` with
  `flags.run == 8`), skips boulders and known-locked doors (`mem_door_l`),
  opens doors by walking into them (`domove()` → `doopen()`, "This door is
  locked." and no lock picking). **Added:** any new message stops it —
  `pline.c` `vpline()`: `multi > 0 && flags.travel && (iflags.autoexplore ||
  iflags.rvip_stairs)` → `nomul(0)`.
- **Key interrupt** was already in stage 1: `web/webwin.c` `commandloop()`
  passes `count = -1` to `nh_command()` when `js_key(1,0)` sees a waiting key
  during `MULTI_IN_PROGRESS`; `command_input()` then `nomul`s. Logged, so
  replays stay deterministic.
- **Stairs:** `do.c` `walk_to_stairs(up)` called where `doup()`/`dodown()`
  would say "You can't go up/down here." (after the pit/trapdoor checks):
  nearest (straight-line) of `upstair`/`upladder`/`sstairs` (or `dn…`) whose
  remembered `mem_bg` is a stair/ladder symbol = "known grid" test; sets
  `u.tx/u.ty` + travel flags like `dotravel()`, `iflags.rvip_stairs = '<'/'>'`
  (new field, `include/flag.h`). Arrival: `allmain.c` `command_input()` multi
  branch, at `u.tx/u.ty` with travel still on → `nomul` + `doup()`/`dodown()`.
  `nomul()` (`hack.c`) clears `rvip_stairs`, so any disturbance cancels;
  pressing again resumes (or climbs when already there).
- **Help:** `?` menu = `cmdlist[]` descriptions: autoexplore "explore (also ~)
  until a monster, message or key stops it", move "…< > stairs, off them walk
  to the nearest known ones"; header hint in `web/index.html`.
- **Stage 1 bug fixed:** `webwin.c` `key_dir()` used SLASH'EM's `"hyku lnjb"`
  (3.4.3 index order); NetHack4's `enum nh_direction` is W NW N NE E SE S SW,
  so `l` went SE, `n` S, `j` SW, `b` = up. Now `"hykulnjb"`/`"47896321"`.
- **Tested** (browser pane, own tab, port 8437, Valkyrie "rviptest"): `~`/`v`
  explored Dlvl 1–3 (picked up gold/items, "The door opens.", "This door is
  locked." once and not retried), stopped on grid bugs, lichens, rats, gnomes,
  a hobbit (silent `lookaround` stop) and on every message; `<` walked 23
  turns to the up stairs → "Still climb?" (n); `>` walked to the down stairs
  and descended twice (Dlvl 2, 3); `>` + queued `s` = one step then search;
  `?` lists the new texts; reload replayed the crashed game; no console
  errors. `/dynahack` DB deleted.
- **Open:** every message stops explore, including DynaHack's `[HP+1=…]`
  regen notes and "You hear…", so explore needs many presses (RVIP rule; an
  exemption for HP notes would help); a hostile that stays in view (sessile
  ones excepted) stops every press after one step, silently; "nearest" stairs
  by straight-line distance; levitating `>` still just says floating.
- **Next: stage 3 (Enter menu + inventory).** DynaHack has its own item action
  menu: `nh_get_object_commands(count, invlet)` (`cmd.c:1392`) lists the
  commands that fit an item, and `i`/`I` open the inventory with
  `PICK_INVACTION` (`invent.c` `display_inventory()`, `dotypeinv()`); the
  curses client (`nitrohack/src/menu.c`) turns a pick into that action menu —
  `webwin.c` still shows a plain list. Enter is free at the command prompt;
  the `?` command menu (`cmd_menu()`) is the base for the Enter menu.

### Stage 3 (Enter menu + inventory) — done 2026-09-26
- All in `web/webwin.c` (client); library only: `cmd.c` `cmdlist[]` inventory
  description (the `?`/Enter menu help text). Page hint in `web/index.html`.
- **Enter menu:** Enter (and `?`, and `#` + empty line) at the command prompt =
  `cmd_menu()`: every non-debug command from `nh_get_commands()`, grouped by
  `cmd_group()` on the `cmdlist[]` flags: Moving (`CMD_MOVE`, autoexplore,
  travel, plus rows `<` `>` `#`), Items (`CMD_ARG_OBJ`), Actions, Information
  (`CMD_NOTIME`/`CMD_HELP`). Key column: key, `^X`, `M-x`, `hjklyubn` for move,
  `#name` for keyless. The chosen command returns into `get_command()` like any
  key (direction asked if needed); `<` `>` `#` are queued as a key (`qkey`).
  The command's own printable key selects its row (pop mode `inv = 3` hides
  the letter column so keys are not shown twice); 8/2/arrows, Enter/5/6,
  Esc/4/0/., mouse click.
- **Inventory `i`:** `PICK_INVACTION` lists (`web_display_objects`) run as a
  cursor list (pop `inv = 1`, keys in `run_pop()`): letter / `+` = main action,
  `-` drop, `*` or Ctrl+letter examine (`whatisinv` → encyclopedia), Enter /
  Space / 5 / click = item menu, 0 / . / Esc close, any other key closes and is
  run as a command. `item_action()`: main = first of takeoff, remove, eat,
  drink, read, zap, apply, put on, wear, wield offered by the library's
  `nh_get_object_commands()` for that item, else examine; the item menu is that
  list itself (its keys work; `/ describe` = examine). **Actions run as the
  next command:** `set_next(name, invlet)` → `get_command()` returns it with
  `CMD_ARG_OBJ` (as the curses client's `set_next_command`), so they are logged
  and replay normally. **Reopen:** `reopen_inv` → after that command's
  `nh_command()` returns `READY_FOR_INPUT` (multi-turn eating included),
  `commandloop()` queues `inventory` unless `hostile_in_view()` (a map monster
  not tame/peaceful/warning).
- Shift+letter drop: not done, A–Z are item letters (52 slots); `-` drops.
  Ctrl+j / Ctrl+m can't examine (= LF/CR); `*` works for every item.
- **Item prompts:** `web_query_key()` recognises getobj's "What do you want to
  …? [… or ?*]" and answers `?` itself once per command (`auto_objlist`), so the
  library opens its own list (`display_pickinv`, PICK_ONE, pop `inv = 2`, title
  = the question, cursor on the first candidate `objcur`); one candidate → `*`
  (full list: `?` with one item only plines it). Where `-` is a valid answer
  (wield, engrave, …) the letter prompt stays and Enter opens the list.
- 3d: no `--More--` (stage 1 `web_pause`), birth checked again: none.
- **Tested** (own tab, port 8443, 1280×800, Healer "rvipt3"): Enter menu lists
  all groups (95 rows), 8/2/arrows move, cursor+Enter ran explore, `i` via its
  key, click ran `#version`; `i` list: `k` ate an apple → list reopened, Numpad2
  ×8 + Numpad5 → potion menu → `q` quaffed → reopened; `-` dropped gold; `*`
  and Ctrl+k examined (encyclopedia), Esc → reopened; Numpad0 closed; `s` in
  the list closed it and searched; `e` → list with cursor on the apples → ate;
  `w` letter prompt, Enter → list; with a kobold zombie in view eating did not
  reopen; no console errors. `/dynahack` DB deleted.
- **Open:** reopened list starts at the top (cursor not kept); item menus have
  no 4/6 "switch list" (no floor/equipment lists in this client); prompts
  with `-` start as a letter prompt; `hostile_in_view()` counts remembered
  detected monsters too.
- **Next: stage 4 (tiles).** Stage 1 decision: SLASH'EM's NetHack 3.4.3-style
  set alone (`~/Games/slashem/win/share/{monsters,objects,other}.txt`), 96.6%
  coverage, gaps filled by stand-ins from the same set (new dragons → existing
  dragons, gold dragon → yellow …), nearest-neighbour scaling. `redraw()` sends
  `-1` tile indexes now (`cells[]`); map `nh_symdef` names → tile index in C.

### Stage 4 (tiles) — done 2026-09-26
- **Tile set:** NetHack 3.4.3-style 16×16 text tiles as shipped with SLASH'EM
  0.0.7E7F3, copied unchanged into `web/tiles/{monsters,objects,other}.txt`
  (`web/tiles/README`: NetHack General Public License = DynaHack's
  `libnitrohack/dat/license`). One set, no mixing. One derived tile: the
  remembered dark floor = lit floor at 45 % (3.4.3 has none).
- **Generator:** `web/build.sh` builds `web/tiledump.c` (emcc, node; links only
  drawing/objects/monst/decl/symclass.c) → `web/gen/symbols.tsv` = the names
  of `nh_get_drawing_info()` (+ object name/desc/class, monster class) →
  `web/mktiles.py` → `dist/tiles.png` (1405 tiles, 40 per row, RGBA: the
  (71,108,108) background of monster/object tiles is transparent) +
  `web/gen/src/tiletab.c` (`tile_bg/trap/obj/mon/warn/expl/zap/effect/invis/
  swallow[]`). Objects match by class + description (the appearance the dbuf
  index stands for; tile classes from SLASH'EM's object order, `CLASS_START`),
  monsters by name (`human were…` = the later tile), map/traps/effects via
  the `BG`/`TRAP`/`EFFECT` tables (walls positional, `unexplored` = blank).
- **Coverage (build prints it):** bg 45/48, traps 22/24, monsters 370/403,
  objects 445/538, warnings/explosions/zaps/invisible/swallow 100 %, effects
  8/9 → **1000/1132 = 88.3 %** exact. Stage 1's 96.6 % was wrong: it counted
  object names, not appearances (DynaHack has ~50 extra random appearances:
  scroll labels, ring gems, wand materials, potion/spellbook colours). Build
  fails below 88 % (`ponytail:` note in mktiles.py). Below the RVIP 95 % goal;
  3.4.3-style is already the fallback set, so kept — user may decide.
- **Stand-ins (all from the same set, printed by the build):** swamp → water,
  dead tree → tree, magic chest → chest (object tile), vibrating square → magic
  trap, shuriken trap → dart trap, gas cloud → cloud; dragons by colour/breath:
  tatzelworm gray, amphitere silver, draken red, lindworm white, sarkany orange,
  sirrush black, leviathan blue, wyvern green, gold dragon + guivre yellow (and
  their babies), chromatic dragon + Tiamat → Chromatic Dragon; locust → killer
  bee, enormous rat / rodent of unusual size → giant rat, disintegrator → rust
  monster, miner / prison guard → watchman, lava demon → fire elemental, giant
  turtle → crocodile, convict / inmate / Robert the Lifer → prisoner, Warden
  Arianna → watch captain; the 11 effect-named dragon scale (mail)s → the colour
  above (chromatic → shimmering); tinfoil hat → dented pot, striped shirt →
  T-shirt, alchemy smock → lab coat, iron safe → large box; missing appearances
  → a look-alike of the class (`LOOK` table: rings quartz→glass, jacinth→ruby,
  …; potions squishy→murky, indigo→brilliant blue, …; spellbooks chartreuse→
  light green, …; wands walnut→oak, chrome→steel, …; amulets rectangular→square,
  spiked→pyramidal); the 18 extra scroll labels → a scroll tile (3.4.3 scroll
  tiles are all identical); other-appearance of a known object (buckled
  kicking boots, leather-bound detect monsters) → its own 3.4.3 tile.
- **C decides (W0):** `web/webwin.c` `cell_tile()` (same layer order as
  `cell_sym()`) + `tile_bg[bg]` under it; `js_map` cells = (top+1) | (floor+1)
  << 16, 0 = blank. Hero = dbuf monster = role/race monster (`display_self`)
  → role tile (female wizard → wizard). Items in lists: `obj_tile()` from
  `nh_objitem.otype` (already the appearance, 1-based), row field 0.
- **Loader:** `web/dynahack.js` `draw()`/`blit()` (floor, then top tile),
  `imageSmoothingEnabled = false` after every resize, backing store = CSS ×
  dpr; list tiles `.ti` = 1em (font height) with `background-size: 40em`,
  `image-rendering: pixelated`. Scale: cell 12–64 CSS px (fit + Zoom ±4).
  **Tiles/Text toggle** kept (button `btn-tiles`, saved in web-layout.json,
  cost: 6 lines, SLASH'EM's); hero box drawn only in text mode. No pref files.
- **Checked** (own tab, port 8451, dpr 2, female human Wizard "rvipt4", died on
  Dlvl 2): canvas pixels read with JS and matched against tiles.png per cell —
  every non-blank cell (up to 508) identified at 100 % and contains only its
  tile's + one floor tile's colours at cell 12 and 20 (24/40 device px): hero =
  wizard tile, kitten, newt, jackal, sewer rat, grid bug; gold, boulder, gem
  (appearance), elven mithril-coat; walls, doorways, closed doors, corridors,
  lit corridor, floor, up/down stairs, fountain, a seen hole (trap). Inventory
  tiles 13 px at 13 px font; each appearance tile = Discoveries (piece of
  cloth, DAIYEN FOOELS, stained, vellum, milky, orange, wooden, agate, granite;
  stand-ins indigo → brilliant blue, plastic → glass, GNIK SISI VLE → scroll).
  Unexplored = blank. No console errors. `/dynahack` DB deleted.
- **Not seen in play** (table-checked only): dark remembered floor (1404 for
  `darkroom`, needs a dark room), remembered invisible `I` (637), explosions/
  zaps/swallow. Engravings: DynaHack's dbuf has no engraving layer, nothing to
  show. Wall variants: not exposed by the API (`dgnflags` bghints only) → plain
  walls everywhere, also Sokoban/Mines.
- **Open:** coverage 88.3 % < 95 % (see above); no pet marker (optional); magic
  chest in item lists uses the library's `otype = CHEST` (not +1) → shows as
  the tile before (library quirk, rare).
- **Next: stage 5 (web page):** window layout (Visible, Equipment windows),
  persistence (layout already in IDBFS), autosave (stage 1: the `.nhgame` log +
  2 s idle sync already gives crash-proof log-replay recovery, tested), Help
  button + `help.html`, `deploy.sh`.

Suggested RVIP.md lessons (not edited): count tile coverage per *display
symbol* including random appearances (object descriptions), not per object
name; a tiny emcc+node dumper of the game's own symbol table makes name
matching exact; 3.4.3's (71,108,108) tile background can be made transparent
to draw the floor under monsters/objects.

### Stage 5 (web page) — done 2026-09-26, tested locally, NOT deployed
- **Harness** (`web/dynahack.js`, `web/index.html`, from SLASH'EM's): rvip-wm.js
  tiling, one/multi-window, Windows drop-down, rename/A−/A+/× on hover, Reset
  windows; default on Map, Log messages, Status, Inventory, **Visible** (new).
  Automatic split + cell size (12–64, fit) until dragged/zoomed. Layout, zoom,
  font, titles, Tiles/Text in `/dynahack/web-layout.json` (IDBFS). Map camera
  `RvipWM.center` with the hero cell from C (`js_map`). Top bar: "DynaHack",
  Help, Zoom, Tiles, Windows, Export/Import save, New character, version line
  `Based on DynaHack 0.6.0 · tung/DynaHack @ 25aaf2a`.
- **Visible window** (`webwin.c` `redraw()`, `js_text(7)`): monsters on the map
  but the hero, from dbuf + `nh_get_drawing_info()` names/colours/tiles, with
  tame/peaceful and "(sensed)" for detected ones. **Objects skipped:** the API
  names them only via `nh_describe_pos()`, whose `describe_object()` calls
  `mksobj()` (bumps `flags.ident`) = game state change outside the log → replay
  desync. Needs a side-effect-free object-appearance name in the library.
- **Prompt line** (`js_text(8)`): the open question (`promptbuf`) or else the
  newest message of the current action (`toplast`, cleared when a command key
  is read in `get_command()`); `RvipWM.prompt.wait(at_cmd && !popup)` from
  every key poll (`at_cmd` = `get_command()` reading a command). `js_text(0)`
  still feeds the question into the Log window only.
- **Persistence/recovery (unchanged from stage 1, verified):** the `.nhgame`
  log is written per command; JS syncs IDBFS every 2 s while waiting for a
  key, every 15 s, on `visibilitychange`/`pagehide`, after save/end. Reload =
  crashed log → `nh_restore_game()` replays it, no question. `S` saves (end
  snapshot in the log). New character clears `save/*.nhgame` only; Import
  writes the file and reloads. Game end → sync → "Play again".
  `unhandledrejection`/`error` → "The game crashed … reload".
- **Replay timing:** T:720 (130 KB log): page load → playable ≈1.5 s after a
  reload without saving (full replay). The library's replay checkpoints
  (`logreplay.c` `make_checkpoint`) are in-memory only (replay viewer), so no
  shortcut; not needed at this speed.
- **Fixed:** saved zoom was ignored for the canvas size on load (`measure()`
  only ran in auto mode) → drawn at the saved cell in a 32-px-cell canvas.
- **Help:** button fetches `help.html` on first open (404 message until stage 6),
  Escape closes, no keys reach the game while open. `build.sh` builds it only
  when `web/make-help.py` exists (prints a "not built yet" note).
- `web/deploy.sh` (step-9 guard) → `ruzzoli.de:/var/www/ruzzoli.de/roguelikes/dynahack`,
  not run (no GitHub repo yet).
- **Tested** (own tab, port 8461): birth → tiles → all 5 windows filled →
  `~`, Enter menu, `i`; rename + gutter drag + zoom + A+ survive reload; zoomed
  map (cell 28) follows the hero 1 cell per step; prompt shows `[yn]`/`yes/no`
  questions, hides on a command key; reloads at T:13 and T:720 (×3) resume in
  place; `S` y → Play again → T:732; death → Play again → new game; 300
  random keys, no console errors (only help.html 404); New character (layout
  kept) → Import → T:82; `#quit` → "The game is over"; resize 1000×650 →
  1440×900 → 1200×750 (question open) → 760×500 + one/multi toggle: no page
  scroll, backing store = CSS × dpr. `/dynahack` DB deleted.
- **Open:** Visible lists no objects (above); the Font A−/A+ is one size for
  all text windows (SLASH'EM's); at cell 12 the map is wider than a small map
  window and scrolls (by design); the library echoes answered prompts
  (`<Really save? [yn]: n>`) into the prompt line until the next command key;
  a background browser-pane tab gets no `resize` events (test by dispatching
  one).
- **Next: stage 6** (docs: `build-docs.py` entry + guide/Tips, `web/make-help.py`
  → help.html; sound + music toggles, off by default).

Suggested RVIP.md lessons (not edited): NetHack4-family `nh_describe_pos()`
changes game state (mksobj) → never call it from a client that relies on log
replay; the browser pane doesn't fire `resize`/ResizeObserver for a background
tab under `resize_window`; restore saved zoom before the first `measure()`.
