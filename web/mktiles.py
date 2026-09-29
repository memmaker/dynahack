#!/usr/bin/env python3
"""NetHack 3.4.3-style 16x16 text tiles (web/tiles/, from SLASH'EM) ->
tiles.png (40 per row, RGBA) + a C table: DynaHack display symbol -> tile.

usage: mktiles.py <tiles dir> <symbols.tsv from web/tiledump.c> <png> <c>

Matching is by name: the game's nh_get_drawing_info() names (dumped by
tiledump.c) against the tile file names; BG/TRAP/EFFECT/MON/DSM/OBJ/LOOK hold the stand-ins for
symbols the 3.4.3 set has no tile for (from the same set, never another).
The monster/object tile background (71,108,108) becomes transparent so C
can put the floor tile under them.  Prints coverage per class; fails unless every symbol has a tile
(real or same-set stand-in) and real coverage stays >= 88%.
"""
import re, sys
from PIL import Image

PER_ROW, BK = 40, (71, 108, 108)
tdir, symfile, png, cfile = sys.argv[1:5]

tiles, names, part = [], [], {}          # part: file -> first index
for f in ('monsters', 'objects', 'other'):
    part[f] = len(tiles)
    pal, rows = {}, None
    for line in open(f'{tdir}/{f}.txt', encoding='latin-1'):
        m = re.match(r'^(\S) = \((\d+), (\d+), (\d+)\)', line)
        if m:
            pal[m.group(1)] = tuple(int(m.group(i)) for i in (2, 3, 4))
        elif line.startswith('# tile'):
            names.append((f, re.match(r'# tile \d+ \((.*)\)', line).group(1)))
        elif line.startswith('{'):
            rows = []
        elif line.startswith('}'):
            assert len(rows) == 16, (f, len(tiles))
            clear = f != 'other'
            tiles.append([[pal[c] + ((0,) if clear and pal[c] == BK else (255,)) for c in r] for r in rows])
            rows = None
        elif rows is not None:
            rows.append(line.strip())
assert len(names) == len(tiles)

# derived tile, same set: the remembered dark floor = the lit floor at 45%
# (3.4.3 has no dark-floor tile; DynaHack shows it as a dim '.')
room = [n for n in range(len(names)) if names[n] == ('other', 'floor of a room')][0]
DARKROOM = len(tiles)
tiles.append([[tuple(int(c * .45) for c in p[:3]) + (255,) for p in r] for r in tiles[room]])
names.append(('other', 'dark floor (derived)'))

def find(f, name, last=False):
    hits = [i for i, (ff, n) in enumerate(names) if ff == f and n == name]
    return (hits[-1] if last else hits[0]) if hits else None

# DynaHack background/trap/effect names -> other.txt names (3.4.3 cmap)
WALLS = 'vwall hwall tlcorn trcorn blcorn brcorn crwall tuwall tdwall tlwall trwall'.split()
BG = dict(zip(WALLS, [part['other'] + 1 + i for i in range(11)]))
BG.update({'unexplored': -1, 'stone': 'dark part of a room', 'corr': 'corridor', 'litcorr': 'lit corridor',
           'room': 'floor of a room', 'darkroom': DARKROOM, 'pool': 'water', 'air': 'air', 'cloud': 'cloud',
           'water': ('water', True), 'ice': 'ice', 'lava': 'molten lava', 'ndoor': 'doorway',
           'vodoor': 'open door', 'hodoor': ('open door', True), 'vcdoor': 'closed door',
           'hcdoor': ('closed door', True), 'bars': 'iron bars', 'tree': 'tree', 'upstair': 'staircase up',
           'dnstair': 'staircase down', 'upladder': 'ladder up', 'dnladder': 'ladder down',
           'upsstair': 'staircase up', 'dnsstair': 'staircase down', 'altar': 'altar', 'grave': 'grave',
           'throne': 'opulent throne', 'sink': 'sink', 'fountain': 'fountain',
           'vodbridge': 'lowered drawbridge', 'hodbridge': ('lowered drawbridge', True),
           'vcdbridge': 'raised drawbridge', 'hcdbridge': ('raised drawbridge', True),
           'features': -1,
           # stand-ins (no 3.4.3 tile)
           'swamp': 'water', 'deadtree': 'tree', 'magic_chest': find('objects', 'chest')})
EFFECT = {'digbeam': 'cmap / dig beam', 'flashbeam': 'cmap / camera flash',
          'boomleft': 'cmap / thrown boomerang, open left', 'boomright': 'cmap / thrown boomerang, open right',
          'shield1': 'cmap / magic shield 1', 'shield2': 'cmap / magic shield 2',
          'shield3': 'cmap / magic shield 3', 'shield4': 'cmap / magic shield 4',
          'gascloud': 'cloud'}                                   # stand-in
TRAP = {'vibrating square': 'magic trap', 'shuriken trap': 'dart trap'}   # stand-ins
# stand-ins for monsters the 3.4.3/SLASH'EM set lacks: nearest of that set.
# DynaHack dragons by colour and breath (as its dragon scale mail names):
DRAGON = {'tatzelworm': 'gray', 'amphitere': 'silver', 'draken': 'red', 'lindworm': 'white',
          'sarkany': 'orange', 'sirrush': 'black', 'leviathan': 'blue', 'wyvern': 'green',
          'gold dragon': 'yellow', 'guivre': 'yellow'}
DSM = {'magic': 'gray', 'reflecting': 'silver', 'fire': 'red', 'ice': 'white', 'sleep': 'orange',
       'disintegration': 'black', 'electric': 'blue', 'poison': 'green', 'stone': 'yellow',
       'acid': 'yellow', 'chromatic': 'shimmering'}
MON = {'locust': 'killer bee', 'enormous rat': 'giant rat', 'rodent of unusual size': 'giant rat',
       'disintegrator': 'rust monster', 'miner': 'watchman', 'prison guard': 'watchman',
       'lava demon': 'efreeti', 'giant turtle': 'crocodile', 'convict': 'prisoner',
       'Robert the Lifer': 'prisoner', 'Warden Arianna': 'watch captain', 'Tiamat': 'Chromatic Dragon',
       'chromatic dragon': 'Chromatic Dragon', 'inmate': 'prisoner'}
for k, c in DRAGON.items():
    MON[k], MON['baby ' + k] = c + ' dragon', f'baby {c} dragon'
# object appearances the set lacks -> a look-alike of the same class
OBJ = {'tinfoil hat': 'dented pot', 'striped shirt': 'T-shirt', 'alchemy smock': 'white coat / lab coat',
       'iron safe': 'large box'}
LOOK = {4: {'quartz': 'glass', 'porcelain': 'ivory', 'ceramic': 'clay', 'mithril': 'silver',
            'platinum': 'silver', 'plastic': 'wooden', 'jacinth': 'ruby', 'citrine': 'topaz',
            'amber': 'tiger eye', 'jet': 'obsidian', 'chrysoberyl': 'jade'},
        5: {'rectangular': 'square', 'elliptic': 'oval', 'spiked': 'pyramidal'},
        8: {'ochre': 'orange', 'amber': 'golden', 'indigo': 'brilliant blue', 'silver': 'white',
            'viscous': 'murky', 'squishy': 'murky', 'greasy': 'golden', 'slimy': 'dark green', 'soapy': 'bubbly',
            'steamy': 'smoky', 'gooey': 'milky'},
        10: {'chartreuse': 'light green', 'decrepit': 'dusty', 'paperback': 'thin', 'crimson': 'red',
             'charcoal': 'dark brown'},
        11: {'porcelain': 'ceramic', 'quartz': 'crystal', 'crusty': 'rusty', 'walnut': 'oak', 'mahogany': 'ebony',
             'cedar': 'pine', 'chrome': 'steel', 'titanium': 'aluminum', 'nickel': 'zinc', 'mithril': 'silver',
             'grooved': 'runed', 'bent': 'curved', 'plastic': 'glass', 'bone': 'marble', 'alabaster': 'marble',
             'orichalcum': 'bronze', 'electrum': 'brass'}}
GENERIC = {'obj': 'strange object', 'mon': 'giant ant'}   # last resort per kind (build fails if used)

gaps, table, cov, generic = [], {}, {}, []
def put(kind, idx, t, name, alias=False):
    if t is None:
        t = find('monsters' if kind == 'mon' else 'objects', GENERIC[kind])
        alias = True
        generic.append(name)
    table.setdefault(kind, {})[idx] = t
    c = cov.setdefault(kind, [0, 0])
    c[1] += 1
    if alias:
        gaps.append(f'{kind} {name} -> {names[t][1] if t >= 0 else "blank"}')
    else:
        c[0] += 1

mon_by_class, objs = {}, []
for line in open(symfile):
    f = line.rstrip('\n').split('\t')
    kind, idx, name = f[0], int(f[1]), f[2]
    if kind == 'mon':
        mlet = int(f[4])
        if name.startswith('human were'):            # the @ form: the later tile of that name
            t, alias = find('monsters', name[6:], last=True), False
        else:
            t, alias = find('monsters', name), False
        if t is None and name in MON:
            t, alias = find('monsters', MON[name]), True
        if t is None:
            t, alias = mon_by_class.get(mlet), True
        mon_by_class.setdefault(mlet, t)
        put(kind, idx, t, name, alias)
    elif kind == 'obj':
        objs.append((idx, name, f[3], int(f[4])))
    elif kind in ('bg', 'trap', 'effect'):
        src = BG if kind == 'bg' else EFFECT if kind == 'effect' else {}
        v = src.get(name, TRAP.get(name, name) if kind == 'trap' else None)
        alias = name in ('swamp', 'deadtree', 'magic_chest', 'gascloud') or name in TRAP
        t = v if isinstance(v, int) else find('other', *(v if isinstance(v, tuple) else (v,))) if v else None
        assert t is not None, (kind, name)
        put(kind, idx, t, name, alias)
    elif kind == 'warn':
        put(kind, idx, find('other', f'warning {idx}'), name)
    elif kind == 'expl':
        for p in range(9):
            put(kind, idx * 9 + p, find('other', f'explosion {name} {p}'), name)
    elif kind == 'zap':
        for p in range(4):
            put(kind, idx * 4 + p, find('other', f'zap {idx} {p}'), name)
# objects: the class of each object tile comes from where it sits in
# objects.txt (SLASH'EM's objects[] order, first index of each class from its
# include/onames.h); match class + description (= the appearance the dbuf
# shows), then name; else the first tile of that class.
CLASS_START = [(0, 1), (1, 2), (101, 3), (190, 4), (223, 5), (237, 6), (298, 7), (336, 8), (368, 9),
               (395, 10), (459, 11), (492, 12), (493, 13), (531, 14), (533, 15), (534, 16), (535, 17)]
otiles = []
for i, (ff, n) in enumerate(names):
    if ff == 'objects':
        k = i - part['objects']
        d, _, nm = n.rpartition(' / ')
        otiles.append((i, max(c for s0, c in CLASS_START if s0 <= k), d, nm))
for idx, name, desc, c in objs:
    m = re.match(r'(\w+) dragon scales?( mail)?$', name)
    if m and m.group(1) in DSM:
        put('obj', idx, find('objects', name.replace(m.group(1), DSM[m.group(1)])), name, True)
        continue
    hit = [i for i, tc, d, nm in otiles if tc == c and d == desc and nm == name] if desc else []
    hit = hit or [i for i, tc, d, nm in otiles if tc == c and desc and (d or nm) == desc]
    same = [i for i, tc, d, nm in otiles if tc == c and nm == (name or desc)]
    if hit or (same and not desc):
        put('obj', idx, (hit or same)[0], name or desc)
    elif name in OBJ:
        put('obj', idx, find('objects', OBJ[name]), name, True)
    elif desc in LOOK.get(c, {}):
        put('obj', idx, [i for i, tc, d, nm in otiles if tc == c and d == LOOK[c][desc]][0], desc, True)
    elif same:          # same object, another appearance of it
        put('obj', idx, same[0], f'{name} ({desc})', True)
    else:
        put('obj', idx, [i for i, tc, d, nm in otiles if tc == c][0], f'{name} ({desc})' if desc else name, True)
put('invis', 0, find('monsters', 'invisible monster'), 'invisible monster')
for p, n in enumerate('top left,top center,top right,middle left,middle right,bottom left,bottom center,bottom right'.split(',')):
    put('swallow', p, find('other', f'cmap / swallow {n}'), n)

hit = tot = 0
for k, (h, n) in cov.items():
    print(f'tiles {k}: {h}/{n} = {100 * h / n:.1f}%')
    hit += h; tot += n
print(f'tiles total: {tot}/{tot} = 100% with a tile: {hit} real ({100 * hit / tot:.1f}%) + '
      f'{len(gaps)} stand-ins from the same set: ' + '; '.join(gaps))
# every symbol gets a same-family tile of this one set (user rule); real
# 3.4.3 tiles cover 88.3% (DynaHack's extra appearances/monsters have none,
# and DynaHack/NitroHack upstream ship no tiles)
assert not generic, f'no same-family stand-in for: {generic}'
assert hit / tot >= .88, 'real tile coverage dropped'

img = Image.new('RGBA', (PER_ROW * 16, -(-len(tiles) // PER_ROW) * 16))
px = img.load()
for i, t in enumerate(tiles):
    for y in range(16):
        for x in range(16):
            px[(i % PER_ROW) * 16 + x, (i // PER_ROW) * 16 + y] = t[y][x]
img.save(png)

with open(cfile, 'w') as o:
    o.write('/* generated by web/mktiles.py: DynaHack display symbol -> tiles.png index */\n')
    for k, d in table.items():
        o.write(f'const short tile_{k}[] = {{' + ','.join(str(d[i]) for i in range(len(d))) + '};\n')
print(len(tiles), 'tiles ->', png)
