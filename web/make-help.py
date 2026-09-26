#!/usr/bin/env python3
"""Writes the in-page game guide (dist/help.html) for the web build.

The game content comes from the desktop key guides in
~/Desktop/Games/Roguelikes/Docs (build-docs.py + guides.py), so both guides
stay in sync; only the saving and "playing in the browser" parts are
written here, because they differ on the web."""
import html, importlib.util, os, sys

DOCS = os.path.expanduser('~/Desktop/Games/Roguelikes/Docs')
PAGE = 'dynahack.html'

sys.path.insert(0, DOCS)
spec = importlib.util.spec_from_file_location('build_docs', os.path.join(DOCS, 'build-docs.py'))
docs = importlib.util.module_from_spec(spec)
spec.loader.exec_module(docs)
from guides import GUIDES, SAVING   # noqa: E402

game = next(g for g in docs.GAMES if g['file'] == PAGE)
guide = dict(GUIDES.get(PAGE, {}))
info = dict(game['info'])
kbd = docs.kbd
esc = html.escape

WEB = '''<ul>
<li>The map is drawn with the NetHack 3.4.3 16×16 tiles (DynaHack has none of its own; its new monsters borrow look-alikes). <em>Zoom −</em> / <em>Zoom +</em> change the tile size, <em>Tiles</em> switches to the game's characters; <em>Windows</em> hides, shows and rearranges the windows (drag a title bar, drag the gaps).</li>
<li><strong>Keys:</strong> <kbd>h</kbd><kbd>j</kbd><kbd>k</kbd><kbd>l</kbd><kbd>y</kbd><kbd>u</kbd><kbd>b</kbd><kbd>n</kbd> or the arrow keys move you (Home, PgUp, End, PgDn for the diagonals); capital letters run. Alt+letter gives the M- commands. <kbd>_</kbd> travels to a spot you pick; click a menu row to pick it. The game's own options menu is not available here.</li>
<li><em>Sound</em> and <em>Music</em> in the top bar are off at first: Sound plays short effects (hits, misses, doors, "You hear", stairs, level up), Music a town tune in Mine Town, the Town and the Black Market. The choice is remembered.</li>
<li>Browsers keep a few shortcuts for themselves (<kbd>Ctrl+W</kbd>, <kbd>Ctrl+T</kbd>, <kbd>Ctrl+N</kbd>, and <kbd>Cmd</kbd> shortcuts on a Mac), so those never reach the game.</li>
<li>If the game ever crashes, a message appears at the top; reload the page: the game log is replayed up to your last move.</li>
</ul>'''

KEY_HINTS = [
    ('v / ~', 'Auto-explore: walk to the nearest unexplored spot'),
    ('Enter / ?', 'Menu of all commands'),
    ('i', 'Inventory with a cursor: Enter = everything you can do with the item'),
    ('< / >', 'Go up / down (walks to the nearest known staircase)'),
    ('S', 'Save; reload the page to continue'),
]


def dl(items):
    return '<dl>' + ''.join(f'<dt>{kbd(k)}</dt><dd>{esc(d)}</dd>' for k, d in items) + '</dl>'


def section(anchor, title, body):
    return f'<h2 id="h-{anchor}">{esc(title)}</h2>{body}'


parts = []
toc = [('about', 'About the game'), ('keys', 'Keyboard controls'), ('saving', 'Saving your game'),
       ('tips', 'Tips'), ('guide', "New player's guide"), ('web', 'Playing in the browser')]
parts.append('<p>' + esc(game['tagline']) + '</p>' + info['About the game'] + '<ul class="toc">' +
             ''.join(f'<li><a href="#h-{a}">{esc(t)}</a></li>' for a, t in toc) + '</ul>')


parts.append(section('about', 'About the game',
                     guide.pop('How DynaHack differs from NetHack')))

ess = ''.join(f'<div class="box"><h3>{esc(cat)}</h3>{dl(items)}</div>' for cat, items in game['essentials'])
all_keys = game['all']() if callable(game['all']) else game['all']
full = ''.join(f'<div>{kbd(k)}<span>{esc(d)}</span></div>' for k, d in all_keys)
parts.append(section('keys', 'Keyboard controls',
                     '<div class="box key"><h3>The keys to remember</h3>' + dl(KEY_HINTS) + '</div>'
                     '<h3>Essential keys</h3><div class="grid">' + ess + '</div>'
                     '<details><summary>Complete key list (' + str(len(all_keys)) + ' commands)</summary>'
                     '<div class="all">' + full + '</div></details>'))

parts.append(section('saving', 'Saving your game', SAVING[PAGE]))
parts.append(section('tips', 'Tips', info['Tips']))
parts.append(section('guide', "New player's guide",
                     ''.join(f'<h3>{esc(t)}</h3>{b}' for t, b in guide.items())))
parts.append(section('web', 'Playing in the browser', WEB))

# RVIP: About this version (W1)
parts.append('<h2 id="h-version">About this version</h2><ul>'
             '<li>DynaHack 0.6.0 (Tung Nguyen, 2016), commit <code>25aaf2a</code> of '
             '<a href="https://github.com/tung/DynaHack/tree/25aaf2a">github.com/tung/DynaHack</a>.</li>'
             '<li>Our changes (browser client, auto-explore stops and stairs walking, command menu, inventory item menus, tiles, sound, web build) '
             'are in <a href="https://github.com/memmaker/dynahack">github.com/memmaker/dynahack</a>, one commit per topic on top of the upstream commit.</li>'
             '<li>Tiles: the NetHack 3.4.3 set as shipped with SLASH\'EM 0.0.7E7F3; sounds and music are synthesized for this port (<code>web/mksounds.py</code>, public domain). '
             'Licence: NetHack General Public License.</li></ul>')
print('\n'.join(parts))
