#!/usr/bin/env python3
"""Test arenas for the ragdoll falls: a small PWAD of maps built from
rectangles, each with one monster type in a row in front of the player,
facing it, away from it, and to either side. With -ragdolltest the game
kills them with a chosen weapon and aim (platform/scene3d/ragdoll_test.cpp)
and saves the falls as a contact sheet.

  MAP01 flat     open floor
  MAP02 stairs   steps down behind the row (16 units each)
  MAP03 ledge    a 96-unit drop just behind the row
  MAP04 wall     a wall just behind the row
  MAP06 close    a wall 22 units behind the row, the side walls 40 off its ends
  MAP05 clutter  behind each monster in turn a step up, a pillar, open floor, a pit;
                 beyond them a dark floor (light 96)
  MAP07 show     the row facing the player, 90 apart, and behind each something else:
                 a broad column (64), a pit (96 deep) with a pillar standing in it, open
                 floor with two staggered columns, stairs up (16 a step) to a column

The rectangles tile each room in guillotine cuts (every split line runs the
room's whole width or depth), so the BSP is the cut tree, each rectangle a
convex subsector closed by real lines, and no seg is ever split.

  python3 tests/ragdoll_arena.py zombie out.wad [--doom1]
  monsters: zombie, shotgun, imp. --doom1 names the maps E1M1-E1M4.

  python3 tests/ragdoll_arena.py showcase [monster] [weapon[:aim]]
  plays MAP07 in a window with -ragdollloop: the row shot, left to lie, stood up and shot
  again until you quit (default mix, ssg:mix; rocket:mix for explosions). Your graphics.cfg and
  default.cfg are copied to output/ragdoll-showcase/ (with Deaths "Ragdolls, Hollywood")
  and the game uses those, so it can't change yours.

  python3 tests/ragdoll_arena.py run <monster> <map 1-7> <weapon[:aim[:n[:tic]]]> <sheet.png> [--side]
  builds the WAD next to the sheet and plays it with DOOM II: 20 frames 3 tics
  apart from the kills on, one contact sheet. weapon: pistol, shotgun, ssg; aim:
  chest, head, legs, left, right; n keeps only that monster of the row (0-3:
  facing the player, away, east, west), tic kills it then (its random draw).
  --side watches the one victim from across its flight; --log adds the per-tic -ragdolllog to sheet.log. The game reads and writes a
  graphics.cfg and default.cfg beside the sheet (the player's graphics.cfg is
  copied there once), never the player's own. -renderframes etc.: scene.cpp.

No id data: plain geometry and stock texture names only."""
import struct, sys

MONSTERS = {'zombie': 3004, 'shotgun': 9, 'imp': 3001, 'mix': (3004, 9, 3004, 9)}   # mix: the row, west to east
WALL, FLOOR, CEIL = 'GRAY5', 'FLAT19', 'FLAT18'   # light, plain greys in both IWADs: bodies stand out
ROW_Y, PLAYER_Y = 128, -32             # the row, and the player south of it facing north
ROW_X = (-120, -40, 40, 120)
FACINGS = (270, 90, 0, 180)            # toward the player, away, east, west

def arenas():
    # (x0, y0, x1, y1, floor, ceiling[, light]): each a sector of its own, light 255 unless given
    flat = [(-512, -448, 0, 768, 0, 256), (0, -448, 512, 768, 0, 256)]
    stairs = [(-384, -448, 384, 160, 0, 256)]
    y = 160
    for step in range(1, 4):
        stairs.append((-384, y, 384, y + 24, -16 * step, 256)); y += 24
    stairs.append((-384, y, 384, 768, -64, 256))
    ledge = [(-384, -448, 384, 160, 0, 256), (-384, 160, 384, 768, -96, 256)]
    wall = [(-384, -448, 0, 176, 0, 256), (0, -448, 384, 176, 0, 256)]   # two halves: a BSP needs a node
    clutter = [(-384, -448, 384, 160, 0, 256), (-384, 208, 384, 768, 0, 256, 96)]   # dark beyond: lit where it lies?
    for x0, x1, floor, ceiling in ((-384, -160, 0, 256), (-160, -80, 24, 256), (-80, 0, 0, 0),
                                   (0, 80, 0, 256), (80, 160, -32, 256), (160, 384, 0, 256)):
        clutter.append((x0, 160, x1, 208, floor, ceiling))
    close = [(-160, -448, 0, 150, 0, 256), (0, -448, 160, 150, 0, 256)]   # a wall 22 behind, side walls 40 off the ends
    return [flat, stairs, ledge, wall, clutter, close, show_room()]

def show_room():
    """MAP07: the front room, and behind the row a grid of cells cut at every edge (so the cuts
    stay guillotine however many columns there are): the lanes' floors, columns closed off."""
    def floor(x, y):                     # the lanes behind the row, at a cell's middle
        if -90 <= x < 0:
            return -96                   # the pit
        if x >= 90:
            return min(64, 16 * (1 + int((y - 160) // 24)))   # stairs up, 16 a step, then the landing
        return 0
    columns = [(-167, 168, -103, 200),   # broad, 20 behind the first
               (-61, 196, -29, 228),     # standing in the pit
               (13, 196, 45, 228),       # open floor: clipped on its right side,
               (56, 252, 88, 284),       # and glanced into
               (119, 236, 151, 268)]     # on the stairs' landing
    xs = sorted({-384, -90, 0, 90, 384} | {c[0] for c in columns} | {c[2] for c in columns})
    ys = sorted({160, 184, 208, 232, 400} | {c[1] for c in columns} | {c[3] for c in columns})
    rects = [(-384, -448, 384, 160, 0, 256)]
    for x0, x1 in zip(xs, xs[1:]):
        for y0, y1 in zip(ys, ys[1:]):
            f = floor((x0 + x1) / 2, (y0 + y1) / 2)
            closed = any(c[0] <= x0 and x1 <= c[2] and c[1] <= y0 and y1 <= c[3] for c in columns)
            rects.append((x0, y0, x1, y1, f, f if closed else 256))
    return rects

# Maps (from 0) laid out otherwise: the row facing the player, spread out, the player further back
SHOWS = {6: {'facings': (270,) * 4, 'row': (-135, -45, 45, 135), 'player': -100}}

def name8(s):
    return s.encode('ascii').ljust(8, b'\0')[:8]

def build(rects, monster, facings=FACINGS, row=ROW_X, player=PLAYER_Y):
    vertices, vindex = [], {}
    def vertex(x, y):
        if (x, y) not in vindex:
            vindex[(x, y)] = len(vertices); vertices.append((x, y))
        return vindex[(x, y)]
    def inside(px, py):
        for i, (x0, y0, x1, y1, *_ ) in enumerate(rects):
            if x0 < px < x1 and y0 < py < y1:
                return i
        return -1
    corners = {(x, y) for (x0, y0, x1, y1, *_ ) in rects for x in (x0, x1) for y in (y0, y1)}
    # The edges, cut at every corner lying on them: (a, b) as points, once each.
    pieces = set()
    for (x0, y0, x1, y1, *_ ) in rects:
        for (ax, ay, bx, by) in ((x0, y0, x1, y0), (x0, y1, x1, y1), (x0, y0, x0, y1), (x1, y0, x1, y1)):
            if ay == by:
                cuts = sorted({ax, bx} | {cx for (cx, cy) in corners if cy == ay and ax < cx < bx})
                pieces |= {(cuts[i], ay, cuts[i + 1], ay) for i in range(len(cuts) - 1)}
            else:
                cuts = sorted({ay, by} | {cy for (cx, cy) in corners if cx == ax and ay < cy < by})
                pieces |= {(ax, cuts[i], ax, cuts[i + 1]) for i in range(len(cuts) - 1)}
    lines, sides, lineAt = [], [], {}
    for (ax, ay, bx, by) in sorted(pieces):
        mx, my = (ax + bx) / 2, (ay + by) / 2
        if ay == by:     # horizontal: front the south side (running east keeps it on the right)
            south, north = inside(mx, my - 0.5), inside(mx, my + 0.5)
            front, back = (south, north) if south >= 0 else (north, -1)
            a, b = ((ax, ay), (bx, by)) if south >= 0 else ((bx, by), (ax, ay))
        else:            # vertical: front the east side (running north keeps it on the right)
            east, west = inside(mx + 0.5, my), inside(mx - 0.5, my)
            front, back = (east, west) if east >= 0 else (west, -1)
            a, b = ((ax, ay), (bx, by)) if east >= 0 else ((bx, by), (ax, ay))
        if front < 0:
            continue
        def side(sector, other):
            if other < 0:
                return (0, 0, '-', '-', WALL, sector)
            lower = WALL if rects[other][4] != rects[sector][4] else '-'
            upper = WALL if rects[other][5] != rects[sector][5] else '-'
            return (0, 0, upper, lower, '-', sector)
        sides.append(side(front, back)); fs = len(sides) - 1
        bs = 0xFFFF
        if back >= 0:
            sides.append(side(back, front)); bs = len(sides) - 1
        flags = 4 if back >= 0 else 1
        lines.append((vertex(*a), vertex(*b), flags, 0, 0, fs, bs))
        lineAt[(a, b)] = len(lines) - 1
    # Segs: each rectangle's lines clockwise round it (it on their right).
    segs, subsectors = [], []
    for i, (x0, y0, x1, y1, *_ ) in enumerate(rects):
        first = len(segs)
        def run(points):
            for p, q in zip(points, points[1:]):
                if (p, q) in lineAt:
                    segs.append((vindex[p], vindex[q], angle(p, q), lineAt[(p, q)], 0, 0))
                elif (q, p) in lineAt:
                    segs.append((vindex[p], vindex[q], angle(p, q), lineAt[(q, p)], 1, 0))
                else:
                    raise SystemExit('no line from %s to %s' % (p, q))
        top = sorted({x for (x, y) in corners if y == y1 and x0 <= x <= x1})
        right = sorted({y for (x, y) in corners if x == x1 and y0 <= y <= y1}, reverse=True)
        bottom = sorted({x for (x, y) in corners if y == y0 and x0 <= x <= x1}, reverse=True)
        left = sorted({y for (x, y) in corners if x == x0 and y0 <= y <= y1})
        run([(x, y1) for x in top]); run([(x1, y) for y in right])
        run([(x, y0) for x in bottom]); run([(x0, y) for y in left])
        subsectors.append((len(segs) - first, first))
    # Nodes: the guillotine cut tree, root last.
    nodes = []
    def tree(members, box):
        if len(members) == 1:
            return 0x8000 | members[0]
        bx0, by0, bx1, by1 = box
        for axis in (0, 1):
            for c in sorted({rects[m][axis] for m in members} | {rects[m][axis + 2] for m in members}):
                if not (box[axis] < c < box[axis + 2]):
                    continue
                low = [m for m in members if rects[m][axis + 2] <= c]
                high = [m for m in members if rects[m][axis] >= c]
                if len(low) + len(high) != len(members):
                    continue
                if axis == 0:    # x = c running north: its front (child 0) the east
                    front, back = (high, (c, by0, bx1, by1)), (low, (bx0, by0, c, by1))
                    line = (c, by0, 0, by1 - by0)
                else:            # y = c running east: its front the south
                    front, back = (low, (bx0, by0, bx1, c)), (high, (bx0, c, bx1, by1))
                    line = (bx0, c, bx1 - bx0, 0)
                kids = [tree(*front), tree(*back)]
                boxes = [(b[3], b[1], b[0], b[2]) for b in (front[1], back[1])]   # top, bottom, left, right
                nodes.append(line + boxes[0] + boxes[1] + tuple(kids))
                return len(nodes) - 1
        raise SystemExit('not a guillotine layout')
    xs = [r[0] for r in rects] + [r[2] for r in rects]; ys = [r[1] for r in rects] + [r[3] for r in rects]
    tree(list(range(len(rects))), (min(xs), min(ys), max(xs), max(ys)))
    things = [(0, player, 90, 1, 7)]
    kinds = MONSTERS[monster] if isinstance(MONSTERS[monster], tuple) else (MONSTERS[monster],) * len(row)
    things += [(x, ROW_Y, f, kind, 7 | 8) for x, f, kind in zip(row, facings, kinds)]
    sectors = [(r[4], r[5], FLOOR, CEIL, r[6] if len(r) > 6 else 255, 0, 0) for r in rects]
    lumps = [
        ('THINGS', b''.join(struct.pack('<5h', *t) for t in things)),
        ('LINEDEFS', b''.join(struct.pack('<7H', *l) for l in lines)),
        ('SIDEDEFS', b''.join(struct.pack('<2h', s[0], s[1]) + name8(s[2]) + name8(s[3]) + name8(s[4]) + struct.pack('<h', s[5]) for s in sides)),
        ('VERTEXES', b''.join(struct.pack('<2h', *v) for v in vertices)),
        ('SEGS', b''.join(struct.pack('<6H', *s) for s in segs)),
        ('SSECTORS', b''.join(struct.pack('<2H', *s) for s in subsectors)),
        ('NODES', b''.join(struct.pack('<12h2H', *n) for n in nodes)),
        ('SECTORS', b''.join(struct.pack('<2h', s[0], s[1]) + name8(s[2]) + name8(s[3]) + struct.pack('<3h', *s[4:]) for s in sectors)),
        ('REJECT', bytes((len(sectors) ** 2 + 7) // 8)),
        ('BLOCKMAP', blockmap(vertices, lines)),
    ]
    return lumps

def angle(p, q):
    if q[1] == p[1]:
        return 0 if q[0] > p[0] else 0x8000
    return 0x4000 if q[1] > p[1] else 0xC000

def blockmap(vertices, lines):
    xs = [v[0] for v in vertices]; ys = [v[1] for v in vertices]
    ox, oy = min(xs) - 8, min(ys) - 8
    cols, rows = (max(xs) - ox) // 128 + 1, (max(ys) - oy) // 128 + 1
    cells = [[] for _ in range(cols * rows)]
    for n, (a, b, *_ ) in enumerate(lines):
        (ax, ay), (bx, by) = vertices[a], vertices[b]
        c0, c1 = sorted(((ax - ox) // 128, (bx - ox) // 128)); r0, r1 = sorted(((ay - oy) // 128, (by - oy) // 128))
        for r in range(max(0, r0 - (ay == by and (ay - oy) % 128 == 0)), min(rows, r1 + 1)):
            for c in range(max(0, c0 - (ax == bx and (ax - ox) % 128 == 0)), min(cols, c1 + 1)):
                cells[r * cols + c].append(n)
    header = 4 + cols * rows
    offsets, data = [], []
    for cell in cells:
        offsets.append(header + len(data)); data += [0] + cell + [0xFFFF]
    return struct.pack('<4h', ox, oy, cols, rows) + struct.pack('<%dH' % len(offsets), *offsets) + struct.pack('<%dH' % len(data), *data)

def run(args, side, trace=False):
    import os, shutil, subprocess
    monster, level, spec, sheet = args
    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.dirname(os.path.abspath(sheet))
    game = os.path.join(here, '..', 'build', 'play', 'Play DOOM.app', 'Contents', 'MacOS', 'Play DOOM')
    iwad = os.path.expanduser('~/Downloads/DOOM-gamedata/gog_doom/doom2.wad')
    wad = os.path.join(out, 'arena-%s.wad' % monster)
    write(wad, monster, False)
    graphics, config = os.path.join(out, 'graphics.cfg'), os.path.join(out, 'default.cfg')
    player = os.path.expanduser('~/Library/Application Support/Local Games/DOOM/graphics.cfg')
    if not os.path.exists(graphics) and os.path.exists(player):
        shutil.copy(player, graphics)
    if not os.path.exists(config):
        shutil.copy(os.path.join(here, '..', 'build', 'play', 'smoke-data', 'smoke.cfg'), config)
    command = [game, '-iwad', iwad, '-file', wad, '-warp', level, '-skill', '3', '-ragdolltest', spec,
               '-rendercheck', os.path.abspath(sheet), '-rendertic', '2', '-renderevery', '3', '-renderframes', '20',
               '-renderscale', '0.3', '-graphics', graphics, '-config', config, '-nosound']
    command += ['-ragdollview', 'side', '-rendercrop', '0.2,0.2,0.8,0.72', '-renderscale', '0.35'] if side else \
               ['-rendercrop', '0.15,0.25,0.85,0.7']
    command += ['-ragdolllog'] if trace else []
    with open(sheet + '.log', 'w') as log:
        code = subprocess.call(command, stdout=log, stderr=subprocess.STDOUT)
    print('%s: exit %d' % (sheet, code))

def showcase(monster, spec):
    import os, shutil, subprocess
    here = os.path.dirname(os.path.abspath(__file__))
    out = os.path.join(here, '..', 'output', 'ragdoll-showcase')
    os.makedirs(out, exist_ok=True)
    wad = os.path.join(out, 'arena-%s.wad' % monster)
    write(wad, monster, False)
    mine = os.path.expanduser('~/Library/Application Support/Local Games/DOOM')
    for name in ('graphics.cfg', 'default.cfg'):     # fresh copies each time: today's settings, never written back
        if os.path.exists(os.path.join(mine, name)):
            shutil.copy(os.path.join(mine, name), os.path.join(out, name))
    graphics = os.path.join(out, 'graphics.cfg')     # whatever yours says: Deaths "Ragdolls, Hollywood"
    lines = open(graphics).read().splitlines() if os.path.exists(graphics) else []
    lines = [l for l in lines if l.split()[:1] not in (['deaths'], ['death_ragdoll'], ['death_blast'])]
    open(graphics, 'w').write('\n'.join(lines + ['deaths 4']) + '\n')
    game = os.path.join(here, '..', 'build', 'play', 'Play DOOM.app', 'Contents', 'MacOS', 'Play DOOM')
    iwad = os.path.expanduser('~/Downloads/DOOM-gamedata/gog_doom/doom2.wad')
    command = [game, '-iwad', iwad, '-file', wad, '-warp', '7', '-skill', '3', '-ragdolltest', spec, '-ragdollloop']
    for flag, name in (('-graphics', 'graphics.cfg'), ('-config', 'default.cfg')):
        if os.path.exists(os.path.join(out, name)):
            command += [flag, os.path.join(out, name)]
    return subprocess.call(command)

def write(path, monster, doom1):
    lumps = []
    for i, rects in enumerate(arenas()):
        lumps.append(('E1M%d' % (i + 1) if doom1 else 'MAP%02d' % (i + 1), b''))
        lumps += build(rects, monster, **SHOWS.get(i, {}))
    body, directory, offset = b'', b'', 12
    for name, data in lumps:
        directory += struct.pack('<2i', offset, len(data)) + name8(name)
        body += data; offset += len(data)
    with open(path, 'wb') as out:
        out.write(b'PWAD' + struct.pack('<2i', len(lumps), offset) + body + directory)

def main():
    args = [a for a in sys.argv[1:] if not a.startswith('--')]
    if args and args[0] == 'showcase':
        if len(args) > 3 or (len(args) > 1 and args[1] not in MONSTERS):
            raise SystemExit(__doc__)
        return showcase(args[1] if len(args) > 1 else 'mix', args[2] if len(args) > 2 else 'ssg:mix')
    if args and args[0] == 'run':
        if len(args) != 5 or args[1] not in MONSTERS:
            raise SystemExit(__doc__)
        return run(args[1:], '--side' in sys.argv, '--log' in sys.argv)
    if len(args) != 2 or args[0] not in MONSTERS:
        raise SystemExit(__doc__)
    write(args[1], args[0], '--doom1' in sys.argv)
    print('%s: %d maps of %s' % (args[1], len(arenas()), args[0]))

if __name__ == '__main__':
    main()
