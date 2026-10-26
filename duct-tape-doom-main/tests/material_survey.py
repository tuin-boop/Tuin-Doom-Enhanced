"""Survey how the IWADs' maps use each wall texture and flat, and guess what
it is made of from the company it keeps, to check and extend the acoustic
material table (platform/games/doom/materials.c).

Usage: python3 tests/material_survey.py report.html doom.wad doom2.wad [tnt.wad ...]
       [--json out.json] [--seed NAME=class ...] [--decisions material-decisions.json]
A WAD argument may add PWADs to its base: doom2.wad+sid.wad.

Every sector is a bag of the surfaces seen from inside it (floor, ceiling and
the wall parts facing it), weighted by area. Surfaces that share rooms link
up, and the classes the current table gives (plus a few sure stone and
fabric names) spread over those links. The report lists where the table and
the maps disagree and unlabelled textures the maps say aren't stone. Every row takes your call (a
dropdown, or hover and press 1-7); the report exports them as JSON, and
--decisions reads them back as seeds. Needs a C compiler to load the
current table from materials.c.
"""
import base64
import ctypes
import html
import json
import math
import os
import struct
import subprocess
import sys
import tempfile
import zlib
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CLASSES = ['stone', 'metal', 'wood', 'flesh', 'soil', 'liquid', 'fabric']
# The footstep each class makes (audio/steps.c).
STEP_FOR = {'stone': 'stone', 'metal': 'metal', 'wood': 'wood', 'flesh': 'soft', 'soil': 'soft',
            'fabric': 'soft', 'liquid': 'splash'}
# The table can't say "stone" apart from "unknown", so some sure stone joins
# the seeds. Prefixes, like the table's.
STONE_WALLS = ['ROCK', 'STONE', 'BRICK', 'MARBLE', 'GSTONE', 'SP_ROCK', 'ASHWALL', 'CEMENT',
               'BSTONE', 'BRSTONE', 'STUCCO', 'BIGBRIK']
STONE_FLATS = ['RROCK', 'GRNROCK', 'MFLR8_1']
EXTRA_SEEDS = {}
CLOSED_RISER = 24  # a lower part this short is a step


def load_table():
    """The current answers of materials.c, compiled into a throwaway library."""
    out = Path(tempfile.mkdtemp()) / 'materials.so'
    subprocess.run(['cc', '-shared', '-fPIC', '-O1', str(ROOT / 'platform/games/doom/materials.c'),
                    '-o', str(out)], check=True)
    lib = ctypes.CDLL(str(out))
    for f in (lib.Game_WallSurface, lib.Game_FlatSurface):
        f.argtypes, f.restype = [ctypes.c_char_p], ctypes.c_int
    return lib


def lumps_of(path):
    data = Path(path).read_bytes()
    count, directory = struct.unpack_from('<ii', data, 4)
    out = []
    for i in range(count):
        offset, size, name = struct.unpack_from('<ii8s', data, directory + 16 * i)
        out.append((name.split(b'\0')[0].decode('ascii', 'replace').upper(), data[offset:offset + size]))
    return out


MAP_LUMPS = ['THINGS', 'LINEDEFS', 'SIDEDEFS', 'VERTEXES', 'SEGS', 'SSECTORS', 'NODES', 'SECTORS']


class Game:
    """One IWAD with its PWADs: lumps, flats, textures, maps (last wins)."""

    def __init__(self, spec):
        self.label = '+'.join(os.path.basename(p) for p in spec.split('+'))
        self.lumps, self.flats, self.maps = {}, {}, {}
        texture_lumps = {}
        for path in spec.split('+'):
            lumps = lumps_of(path)
            in_flats = False
            for i, (name, data) in enumerate(lumps):
                if name in ('F_START', 'FF_START'): in_flats = True
                elif name in ('F_END', 'FF_END'): in_flats = False
                elif in_flats and len(data) == 4096: self.flats[name] = data
                self.lumps[name] = data
                if name in ('TEXTURE1', 'TEXTURE2', 'PNAMES'): texture_lumps[name] = data
                if [n for n, _ in lumps[i + 1:i + 9]] == MAP_LUMPS:
                    following = [n for n, _ in lumps[i + 9:i + 12]]
                    if 'BEHAVIOR' not in following:
                        self.maps[name] = {n: d for n, d in lumps[i + 1:i + 9]}
        self.palette = self.lumps['PLAYPAL'][:768]
        self.textures = {}
        pnames = texture_lumps['PNAMES']
        patches = [pnames[4 + 8 * i:12 + 8 * i].split(b'\0')[0].decode('ascii', 'replace').upper()
                   for i in range(struct.unpack_from('<i', pnames)[0])]
        for key in ('TEXTURE1', 'TEXTURE2'):
            if key not in texture_lumps: continue
            t = texture_lumps[key]
            count = struct.unpack_from('<i', t)[0]
            for offset in struct.unpack_from('<%di' % count, t, 4):
                name, _, w, h, _, n = struct.unpack_from('<8sihhih', t, offset)
                parts = [struct.unpack_from('<hhh', t, offset + 22 + 10 * k) for k in range(n)]
                self.textures[name.split(b'\0')[0].decode('ascii', 'replace').upper()] = (
                    w, h, [(x, y, patches[p]) for x, y, p in parts if 0 <= p < len(patches)])

    def texture_height(self, name):
        return self.textures.get(name, (0, 128, []))[1]

    def picture(self, kind, name):
        """(width, height, pixels) with -1 for holes, or None."""
        if kind == 'F':
            data = self.flats.get(name)
            return (64, 64, list(data)) if data else None
        if name not in self.textures: return None
        w, h, parts = self.textures[name]
        pixels = [-1] * (w * h)
        for ox, oy, patch in parts:
            p = self.lumps.get(patch)
            if not p or len(p) < 8: continue
            pw, ph = struct.unpack_from('<hh', p)
            for x in range(pw):
                tx = ox + x
                if not 0 <= tx < w: continue
                at = struct.unpack_from('<i', p, 8 + 4 * x)[0]
                while 0 <= at < len(p) and p[at] != 255:
                    top, length = p[at], p[at + 1]
                    for y in range(length):
                        ty = oy + top + y
                        if 0 <= ty < h and at + 3 + y < len(p): pixels[ty * w + tx] = p[at + 3 + y]
                    at += length + 4
        return w, h, pixels


class Survey:
    def __init__(self):
        self.area = defaultdict(float)          # node -> visible area, square units
        self.maps = defaultdict(set)
        self.role = defaultdict(lambda: defaultdict(float))  # node -> role -> area
        self.light = defaultdict(float)
        self.links = defaultdict(lambda: defaultdict(float))
        self.source = {}                        # node -> Game holding its art

    def add_map(self, game, label, lumps):
        verts = [struct.unpack_from('<hh', lumps['VERTEXES'], 4 * i) for i in range(len(lumps['VERTEXES']) // 4)]
        sectors = []
        for i in range(len(lumps['SECTORS']) // 26):
            f, c, fp, cp, light, special, _ = struct.unpack_from('<hh8s8shhh', lumps['SECTORS'], 26 * i)
            sectors.append((f, c, fp.split(b'\0')[0].decode('ascii', 'replace').upper(),
                            cp.split(b'\0')[0].decode('ascii', 'replace').upper(), light, special))
        sides = []
        for i in range(len(lumps['SIDEDEFS']) // 30):
            _, _, up, lo, mid, sec = struct.unpack_from('<hh8s8s8sh', lumps['SIDEDEFS'], 30 * i)
            sides.append(tuple(s.split(b'\0')[0].decode('ascii', 'replace').upper() for s in (up, lo, mid)) + (sec,))
        bags = [defaultdict(float) for _ in sectors]
        doubled = [0.0] * len(sectors)

        def put(sector, node, area, roles=()):
            if area <= 0 or node[1] in ('', '-'): return
            s = sectors[sector]
            bags[sector][node] += area
            self.area[node] += area
            self.maps[node].add(label)
            self.light[node] += area * s[4]
            self.source.setdefault(node, game)
            if s[3] == 'F_SKY1': self.role[node]['outdoor'] += area
            for r in roles: self.role[node][r] += area

        for i in range(len(lumps['LINEDEFS']) // 14):
            v1, v2, _, _, _, right, left = struct.unpack_from('<HHhhhHH', lumps['LINEDEFS'], 14 * i)
            if v1 >= len(verts) or v2 >= len(verts): continue
            (x1, y1), (x2, y2) = verts[v1], verts[v2]
            length = math.hypot(x2 - x1, y2 - y1)
            cross = x1 * y2 - x2 * y1
            faces = [(s, o) for s, o in ((right, left), (left, right)) if s != 0xFFFF and s < len(sides)]
            for n, (side, other) in enumerate(faces):
                up, lo, mid, sec = sides[side]
                if not 0 <= sec < len(sectors): continue
                doubled[sec] += cross if side == right else -cross
                f = sectors[sec]
                back = sides[other][3] if other != 0xFFFF and other < len(sides) else -1
                if not 0 <= back < len(sectors):
                    put(sec, ('W', mid), length * (f[1] - f[0]))
                    continue
                b = sectors[back]
                if not (f[3] == 'F_SKY1' and b[3] == 'F_SKY1'):
                    put(sec, ('W', up), length * (f[1] - b[1]), ('door',) if b[1] <= b[0] else ())
                rise = b[0] - f[0]
                put(sec, ('W', lo), length * rise, ('riser',) if rise <= CLOSED_RISER else ())
                gap = min(f[1], b[1]) - max(f[0], b[0])
                put(sec, ('W', mid), length * min(gap, game.texture_height(mid)), ('masked',))
        for i, s in enumerate(sectors):
            area = abs(doubled[i]) / 2
            closed = s[1] <= s[0]
            damaging = s[5] in (4, 5, 7, 11, 16)
            put(i, ('F', s[2]), 0 if closed else area, ('damaging',) if damaging else ())
            if s[3] != 'F_SKY1': put(i, ('F', s[3]), area, ('door',) if closed else ())
        for bag in bags:
            total = sum(bag.values())
            items = list(bag.items())
            for a, (na, wa) in enumerate(items):
                for nb, wb in items[a + 1:]:
                    w = wa * wb / total
                    self.links[na][nb] += w
                    self.links[nb][na] += w


def seed_class(table, node, overrides):
    kind, name = node
    if node in overrides: return overrides[node]
    if node in EXTRA_SEEDS: return EXTRA_SEEDS[node]
    c = CLASSES[(table.Game_FlatSurface if kind == 'F' else table.Game_WallSurface)(name.encode())]
    if c != 'stone': return c
    stones = STONE_FLATS if kind == 'F' else STONE_WALLS
    return 'stone' if any(name.startswith(p) for p in stones) else None


def spread(survey, seeds, rounds=10):
    """Each surface's class mix from the same kind of surface (walls from
    walls, flats from flats) in the rooms it shares. A seed's vote comes from
    the first round only, its seeded neighbours, so its own label can't echo
    back through the surfaces it labelled. Liquids are known from their
    animation and don't vote: they sit in rooms of any material."""
    nodes = list(survey.area)
    voters = {n: seeds.get(n) if seeds.get(n) != 'liquid' else None for n in nodes}
    dist = {n: [1.0 if c == voters[n] else 0.0 for c in CLASSES] for n in nodes}
    links = {n: [(m, w) for m, w in survey.links[n].items() if m[0] == n[0]] for n in nodes}
    vote = {}
    for round in range(rounds):
        fresh = {}
        for n in nodes:
            if round and seeds.get(n): continue
            v = [0.0] * len(CLASSES)
            for m, w in links[n]:
                d = dist[m]
                for c in range(len(CLASSES)): v[c] += w * d[c]
            total = sum(v)
            fresh[n] = [x / total for x in v] if total else [0.0] * len(CLASSES)
        vote.update(fresh)
        for n in nodes:
            if not voters[n]: dist[n] = vote[n]
    return vote


def held_out(survey, seeds, vote):
    """Share of each seeded class's area whose vote picks it back."""
    hits, totals = defaultdict(float), defaultdict(float)
    for n, seed in seeds.items():
        if not seed or seed == 'liquid' or not any(vote[n]): continue
        totals[seed] += survey.area[n]
        if CLASSES[max(range(len(CLASSES)), key=lambda c: vote[n][c])] == seed: hits[seed] += survey.area[n]
    return {c: hits[c] / totals[c] for c in CLASSES if totals[c]}, sum(hits.values()) / sum(totals.values())


def png(w, h, pixels, palette):
    used = set(pixels)
    plte, trns = bytearray(palette), b''
    if -1 in used:
        hole = next((i for i in range(256) if i not in used), 0)
        plte[3 * hole:3 * hole + 3] = b'\xff\x00\xff'
        trns = bytes([255] * hole + [0])
        pixels = [hole if p < 0 else p for p in pixels]
    raw = b''.join(b'\0' + bytes(pixels[y * w:(y + 1) * w]) for y in range(h))

    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data))
    out = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 3, 0, 0, 0))
    out += chunk(b'PLTE', bytes(plte)) + (chunk(b'tRNS', trns) if trns else b'')
    return out + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b'')


CSS = """
:root{--bg:#f6f5f2;--fg:#1d1d1f;--muted:#6b6b70;--line:#dddbd5;--card:#fff;
--stone:#8a8d91;--metal:#4c7fb8;--wood:#a8743a;--flesh:#c45a6a;--soil:#6f8f3a;--liquid:#2aa3a3;--fabric:#8b5cc4;--warn:#b5481f}
@media (prefers-color-scheme:dark){:root:not([data-theme=light]){--bg:#161617;--fg:#ececee;--muted:#9a9aa2;--line:#2e2e32;--card:#1f1f22}}
:root[data-theme=dark]{--bg:#161617;--fg:#ececee;--muted:#9a9aa2;--line:#2e2e32;--card:#1f1f22}
body{background:var(--bg);color:var(--fg);font:14px/1.45 -apple-system,system-ui,sans-serif;margin:0;padding:24px 16px;max-width:1200px;margin:auto}
h1{font-size:22px;margin:0 0 4px}h2{font-size:17px;margin:32px 0 6px}p{color:var(--muted);margin:4px 0 12px;max-width:80ch}
table{border-collapse:collapse;width:100%;background:var(--card)}td,th{border-bottom:1px solid var(--line);padding:6px 8px;text-align:left;vertical-align:middle}
th{font-size:12px;color:var(--muted);font-weight:600;position:sticky;top:0;background:var(--card)}
img{image-rendering:pixelated;max-width:128px;max-height:96px;display:block}
.name{font-family:ui-monospace,Menlo,monospace;font-weight:600}.small{font-size:12px;color:var(--muted)}
.bar{display:flex;width:160px;height:10px;border-radius:2px;overflow:hidden;background:var(--line)}
.tag{display:inline-block;padding:1px 6px;border-radius:3px;color:#fff;font-size:12px}
.warn{color:var(--warn);font-weight:600}.wrap{overflow-x:auto}
#bar{position:sticky;top:0;z-index:2;background:var(--card);border-bottom:1px solid var(--line);padding:8px 10px;margin:0 -16px 12px;display:flex;flex-wrap:wrap;gap:8px;align-items:center}
button,select{font:inherit;padding:3px 8px;border:1px solid var(--line);border-radius:4px;background:var(--card);color:var(--fg);cursor:pointer}
tr.done{opacity:.55}tr.hover{outline:2px solid var(--metal)}th{top:46px}
input{font:inherit;padding:6px 8px;border:1px solid var(--line);border-radius:4px;background:var(--card);color:var(--fg);width:min(320px,100%)}
""" + ''.join('.c-%s{background:var(--%s)}' % (c, c) for c in CLASSES)

# Your calls live in this browser too, so a reload or a fresh report keeps
# them; the file given with --decisions comes first, the browser's on top.
REVIEW_JS = r'''
const STORE='derp-material-decisions';
let calls=Object.assign({},FROM_FILE);
try{Object.assign(calls,JSON.parse(localStorage.getItem(STORE)||'{}'))}catch(e){}
const selects=[...document.querySelectorAll('select[data-n]')];
function save(){try{localStorage.setItem(STORE,JSON.stringify(calls))}catch(e){}}
function set(n,c){if(c)calls[n]=c;else delete calls[n];save();show()}
function show(){
  const done={W:0,F:0},count={W:0,F:0},seen=new Set();
  for(const s of selects){
    const n=s.dataset.n,c=calls[n]||'';s.value=c;s.closest('tr').classList.toggle('done',!!c);
    if(c&&!seen.has(n)){seen.add(n);done[n[0]]+=+s.dataset.s;count[n[0]]++}
  }
  progress.textContent=`${count.W} walls (${Math.round(100*done.W)}% of wall area), ${count.F} flats (${Math.round(100*done.F)}% of flat area)`;
  f(q.value);
}
function f(t){t=t.toLowerCase();for(const r of document.querySelectorAll('#all tr[data-k]'))
  r.style.display=r.dataset.k.includes(t)&&!(hide.checked&&r.classList.contains('done'))?'':'none'}
function out(){const o={};for(const k of Object.keys(calls).sort())o[k]=calls[k];return JSON.stringify(o,null,1)+'\n'}
function copyOut(){navigator.clipboard.writeText(out()).then(()=>progress.textContent+=' — copied')}
function saveOut(){const a=document.createElement('a');a.href=URL.createObjectURL(new Blob([out()],{type:'application/json'}));
  a.download='material-decisions.json';a.click()}
for(const s of selects)s.onchange=()=>set(s.dataset.n,s.value);
let over=null;
document.addEventListener('mouseover',e=>{const r=e.target.closest('tr[data-k]');if(r===over)return;
  over&&over.classList.remove('hover');over=r;r&&r.classList.add('hover')});
document.addEventListener('keydown',e=>{
  if(!over||e.target.tagName==='INPUT'||e.metaKey||e.ctrlKey)return;
  const i='01234567'.indexOf(e.key);if(i<0||i>CLASSES.length)return;
  set(over.querySelector('select').dataset.n,i?CLASSES[i-1]:'');e.preventDefault()});
show();
'''


def report(path, survey, seeds, table_class, steps, vote, extra_json, decisions):
    per_class, overall = held_out(survey, seeds, vote)
    kind_total = {k: sum(a for n, a in survey.area.items() if n[0] == k) or 1 for k in 'WF'}
    pictures = {}

    def thumb(node):
        if node not in pictures:
            game = survey.source[node]
            p = game.picture(*node)
            pictures[node] = ('<img alt="" src="data:image/png;base64,%s">' %
                              base64.b64encode(png(*p, game.palette)).decode()) if p else ''
        return pictures[node]

    def tag(c):
        return '<span class="tag c-%s">%s</span>' % (c, c) if c else '<span class="small">none</span>'

    def bar(v):
        return '<div class="bar">%s</div>' % ''.join(
            '<span class="c-%s" style="width:%.1f%%" title="%s %.0f%%"></span>' % (c, 100 * x, c, 100 * x)
            for c, x in zip(CLASSES, v) if x > 0.005)

    def top(v):
        best = max(range(len(CLASSES)), key=lambda c: v[c])
        return CLASSES[best], v[best]

    def roles(n):
        a = survey.area[n]
        parts = ['%s %d%%' % (r, 100 * x / a) for r, x in sorted(survey.role[n].items(), key=lambda kv: -kv[1])
                 if x / a >= 0.05]
        parts.append('light %d' % (survey.light[n] / a))
        return ', '.join(parts)

    def company(n):
        near = sorted(survey.links[n].items(), key=lambda kv: -kv[1])[:5]
        total = sum(survey.links[n].values()) or 1
        return ', '.join('%s %d%%' % (m[1], 100 * w / total) for m, w in near)

    def pick(n):
        key = '%s:%s' % n
        return '<select data-n="%s" data-s="%.6f"><option value="">—</option>%s</select>' % (
            html.escape(key), survey.area[n] / kind_total[n[0]], ''.join(
                '<option%s>%s</option>' % (' selected' if decisions.get(key) == c else '', c) for c in CLASSES))

    def row(n, note=''):
        kind, name = n
        step = steps.get(n)
        return ('<tr data-k="%s"><td>%s</td><td>%s</td><td><div class="name">%s</div><div class="small">%s · %.0f m² · %d maps</div></td>'
                '<td>%s%s</td><td>%s</td><td>%s<div class="small">%s %d%%</div></td><td class="small">%s</td><td class="small">%s</td>%s</tr>') % (
            html.escape(name.lower()), pick(n), thumb(n), html.escape(name), 'flat' if kind == 'F' else 'wall',
            survey.area[n] / 1024, len(survey.maps[n]), tag(table_class[n]),
            ' <span class="small">(seed)</span>' if seeds.get(n) and table_class[n] != seeds[n] else '',
            step or '', bar(vote[n]), *top(vote[n]), roles(n), html.escape(company(n)),
            '<td class="small">%s</td>' % note if note is not None else '')

    head = ('<tr><th>your call</th><th></th><th>surface</th><th>table</th><th>step</th><th>maps vote</th><th>where it sits</th>'
            '<th>shares rooms with</th><th>why listed</th></tr>')
    nodes = sorted(survey.area, key=lambda n: -survey.area[n])
    disagree, unknown = [], []
    for n in nodes:
        c, share = top(vote[n])
        seed = seeds.get(n)
        if seed and seed != 'liquid' and c != seed and share >= 0.5 and len(survey.maps[n]) >= 2:
            disagree.append((n, 'maps say %s, table %s' % (c, seed)))
        if not seed and c != 'stone' and share >= 0.4 and len(survey.maps[n]) >= 2:
            unknown.append((n, 'default stone, maps say %s' % c))

    def section(title, text, rows):
        body = ''.join(row(n, note) for n, note in rows) or '<tr><td class="small" colspan="9">nothing</td></tr>'
        return '<h2>%s (%d)</h2><p>%s</p><div class="wrap"><table>%s%s</table></div>' % (
            title, len(rows), text, head, body)

    covered, running = {}, defaultdict(float)
    for n in nodes:
        running[n[0]] += survey.area[n]
        covered[n] = running[n[0]] / kind_total[n[0]]
    games = ', '.join(sorted({g.label for g in survey.source.values()}))
    page = ['<!doctype html><html><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">',
            '<title>Material Survey</title><style>%s</style></head><body>' % CSS,
            '<div id="bar"><b>Review</b> <span id="progress"></span> <button onclick="copyOut()">Copy decisions</button> '
            '<button onclick="saveOut()">Download decisions</button> <label><input type="checkbox" id="hide" '
            'onchange="f(q.value)"> hide reviewed</label> <span class="small">Hover a row and press %s, 0 clears.</span></div>' % (
                ', '.join('%d %s' % (i + 1, c) for i, c in enumerate(CLASSES))),
            '<h1>Material survey</h1><p>%s — %d surfaces over %d maps. Areas are rough square metres (32 units = 1 m). '
            'The vote is what the other surfaces sharing a room with it are, weighted by area; for seeds it leaves '
            'the seed itself out. Seeds: the table\'s non-stone classes, sure stone names%s. Walls vote from walls, '
            'flats from flats; liquids don\'t vote.</p><p>How often the vote gives a seed its own class back, by area: '
            '<b>%d%%</b> overall; %s. Trust it for stone against metal against wood; flesh, soil and fabric need the '
            'pictures (outdoor share helps for soil).</p>' % (
                html.escape(games), len(nodes), len({m for s in survey.maps.values() for m in s}),
                ''.join(', %s = %s' % (n[1], c) for n, c in EXTRA_SEEDS.items()), 100 * overall,
                ', '.join('%s %d%%' % (c, 100 * x) for c, x in per_class.items())),
            section('Table and maps disagree', 'Seeded surfaces whose rooms vote for another class with at least half the weight.', disagree),
            section('Unlabelled, maybe not stone', 'Surfaces the table leaves at the stone default whose rooms lean elsewhere.', unknown),
            '<h2>All surfaces</h2><p>Biggest first. Reverb sums surfaces by area, so the first hundred or two of '
            'each kind decide nearly every room; the last column runs the share of all wall or flat area so far.</p>'
            '<p><input id="q" placeholder="Filter by name" oninput="f(this.value)"></p>',
            '<div class="wrap"><table id="all">%s%s</table></div>' % (head, ''.join(row(n, '%s %d%%' % (
                'walls' if n[0] == 'W' else 'flats', 100 * covered[n])) for n in nodes)),
            '<script>const CLASSES=%s,FROM_FILE=%s;%s</script></body></html>' % (
                json.dumps(CLASSES), json.dumps(decisions), REVIEW_JS)]
    Path(path).write_text(''.join(page))
    if extra_json:
        Path(extra_json).write_text(json.dumps({'%s:%s' % n: {
            'area_m2': round(survey.area[n] / 1024, 1), 'maps': len(survey.maps[n]), 'table': table_class[n],
            'seed': seeds.get(n), 'step': steps.get(n), 'vote': dict(zip(CLASSES, (round(x, 3) for x in vote[n]))),
            'roles': {r: round(x / survey.area[n], 3) for r, x in survey.role[n].items()}} for n in nodes}, indent=1))
    return len(disagree), len(unknown)


def main(argv):
    args, overrides, extra_json, decisions = [], {}, None, {}
    it = iter(argv)
    for a in it:
        if a == '--json': extra_json = next(it)
        elif a == '--decisions':
            decisions = json.loads(Path(next(it)).read_text())
            for key, c in decisions.items():
                assert c in CLASSES, (key, c)
                overrides[tuple(key.split(':', 1))] = c
        elif a == '--seed':
            name, c = next(it).upper().split('=')
            assert c.lower() in CLASSES, c
            overrides[('F' if name.startswith('F:') else 'W', name.split(':')[-1])] = c.lower()
        else: args.append(a)
    if len(args) < 2:
        sys.exit(__doc__)
    table = load_table()
    survey = Survey()
    for spec in args[1:]:
        game = Game(spec)
        tag = os.path.basename(spec.split('+')[-1]).lower()
        for name, lumps in game.maps.items():
            survey.add_map(game, '%s %s' % (tag, name), lumps)
    seeds = {n: seed_class(table, n, overrides) for n in survey.area}
    table_class = {}
    steps = {}
    for kind, name in survey.area:
        c = CLASSES[(table.Game_FlatSurface if kind == 'F' else table.Game_WallSurface)(name.encode())]
        table_class[(kind, name)] = c
        if kind == 'F': steps[(kind, name)] = STEP_FOR[c]
    vote = spread(survey, seeds)
    counts = report(args[0], survey, seeds, table_class, steps, vote, extra_json, decisions)
    per_class, overall = held_out(survey, seeds, vote)
    print("held-out: %d%% overall, " % (100 * overall) + ", ".join("%s %d%%" % (c, 100 * x) for c, x in per_class.items()))
    print('%s: %d disagree, %d unlabelled non-stone' % ((args[0],) + counts))


if __name__ == '__main__':
    main(sys.argv[1:])
