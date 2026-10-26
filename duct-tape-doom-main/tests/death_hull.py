"""Can a monster's eight walk-frame rotations make a body convincing enough to
redraw it from angles id never drew? The first test of a hidden 3D puppet
behind the procedural deaths: carve, texture, redraw.

Carve: a 1-unit voxel grid around the thing's origin; each rotation's
drawing (frame A, mirrored where the WAD shares one, A2A8) seen straight on
from its angle (Doom's rotation k: the viewer 45(k-1) degrees round from
the thing's facing, counter-clockwise from above), orthographic, placed by
its own offsets. A voxel stays where enough rotations see the body over it
(a vote, so one sloppy drawing can't cut the body away). The drawings'
offsets disagree by a unit or two, so each rotation's sideways offset is
then nudged to where the carved body's outline best matches its drawing,
and the body carved again.

Texture: each surface voxel takes its colour from the drawing that faces it
most squarely and sees it (nothing of the body in front of it from there),
one drawing per voxel, never blended, so every colour is an original
palette colour. Surfaces no drawing faces (tops of heads and shoulders)
are darkened by how edge-on the best drawing sees them.

Redraw: the body seen orthographically at one pixel per unit from any
angle, a new drawing at the WAD's own scale. Rows: the WAD's drawings, the
body redrawn at the same angles (a check), and the angles in between,
which no drawing exists for (the test).

Writes an HTML page and a contact sheet (keep them in build/, out of git:
they embed id's art).
Usage: python3 -I tests/death_hull.py doom2.wad build/death-hull.html [--sprite POSSA --vote 7]
"""
import argparse
import base64
import html
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from death_lab import Picture, png, read_wad  # noqa: E402


class View:
    """One rotation's drawing as a silhouette and colours on the plane across its line of sight."""
    def __init__(self, pic, flip, angle):
        self.pic, self.flip, self.angle = pic, flip, angle
        a = math.radians(angle)
        self.toward = (math.sin(a), -math.cos(a), 0.0)          # from the thing to the viewer
        self.right = (math.cos(a), math.sin(a), 0.0)            # the viewer's right
        self.shift = 0.0

    def texel(self, u, z):
        """Palette index at a point on the drawing's plane (units right of the origin, height), or -1."""
        p = self.pic
        c = math.floor(u + self.shift + p.left)
        r = math.floor(p.top - z)
        if not (0 <= c < p.width and 0 <= r < p.height):
            return -1
        return p.texels[r][p.width - 1 - c if self.flip else c]

    def project(self, x, y, z):
        return x * self.right[0] + y * self.right[1], z


def views_of(lumps, sprite, sign):
    """The eight rotations of a frame (sprite = 'POSSA'); sign picks which way round they go."""
    out = []
    for k in range(1, 9):
        for name in lumps:
            if name[:5] == sprite and len(name) in (6, 8):
                if name[5] == str(k) and name[4] == sprite[4]:
                    out.append(View(Picture(lumps[name]), False, sign * 45 * (k - 1)))
                    break
                if len(name) == 8 and name[6] == sprite[4] and name[7] == str(k):
                    out.append(View(Picture(lumps[name]), True, sign * 45 * (k - 1)))
                    break
        else:
            raise SystemExit(f'{sprite}: no rotation {k}')
    return out


class Grid:
    def __init__(self, views):
        self.r = max(max(v.pic.width - v.pic.left, v.pic.left) for v in views) + 2
        self.top = max(v.pic.top for v in views) + 1
        self.bottom = min(v.pic.top - v.pic.height for v in views)
        self.nx = self.ny = 2 * self.r
        self.nz = self.top - self.bottom
        self.solid = bytearray(self.nx * self.ny * self.nz)

    def centre(self, i, j, k):
        return i - self.r + 0.5, j - self.r + 0.5, self.bottom + k + 0.5

    def index(self, i, j, k):
        return (k * self.ny + j) * self.nx + i

    def at(self, i, j, k):
        return 0 <= i < self.nx and 0 <= j < self.ny and 0 <= k < self.nz and self.solid[self.index(i, j, k)]

    def cell(self, x, y, z):
        return math.floor(x + self.r), math.floor(y + self.r), math.floor(z - self.bottom)


def carve(grid, views, vote):
    for k in range(grid.nz):
        for j in range(grid.ny):
            for i in range(grid.nx):
                x, y, z = grid.centre(i, j, k)
                seen = sum(1 for v in views if v.texel(*v.project(x, y, z)) >= 0)
                grid.solid[grid.index(i, j, k)] = seen >= vote
    return sum(grid.solid)


def outline(grid, view):
    """The carved body's silhouette from a view: set of (column, row) cells on the drawing's grid."""
    cells = set()
    for k in range(grid.nz):
        for j in range(grid.ny):
            for i in range(grid.nx):
                if grid.solid[grid.index(i, j, k)]:
                    u, z = view.project(*grid.centre(i, j, k))
                    cells.add((math.floor(u), math.floor(z)))
    return cells


def drawn(view, shift):
    p, out = view.pic, set()
    for r in range(p.height):
        for c in range(p.width):
            if p.texels[r][p.width - 1 - c if view.flip else c] >= 0:
                out.add((math.floor(c - p.left - shift + 0.5), math.floor(p.top - r - 0.5)))
    return out


def iou(a, b):
    return len(a & b) / max(1, len(a | b))


def align(grid, views, vote, rounds=2):
    """Nudge each rotation sideways (whole units, at most 3) to where the carved body's outline
    matches its drawing best, carving again after each round."""
    for _ in range(rounds):
        moved = False
        for v in views:
            body = outline(grid, v)
            best = max(range(-3, 4), key=lambda s: (iou(body, drawn(v, v.shift + s)), -abs(s)))
            if best:
                v.shift += best
                moved = True
        carve(grid, views, vote)
        if not moved:
            break


# -- Texture.
def normals(grid):
    """Outward normals of surface voxels, from a 5x5x5 blurred occupancy's gradient."""
    out = {}
    for k in range(grid.nz):
        for j in range(grid.ny):
            for i in range(grid.nx):
                if not grid.solid[grid.index(i, j, k)]:
                    continue
                if all(grid.at(i + a, j + b, k + c) for a, b, c in ((1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1))):
                    continue
                g = [0.0, 0.0, 0.0]
                for c in range(-2, 3):
                    for b in range(-2, 3):
                        for a in range(-2, 3):
                            if (a or b or c) and grid.at(i + a, j + b, k + c):
                                w = 1.0 / (a * a + b * b + c * c)
                                g[0] -= a * w
                                g[1] -= b * w
                                g[2] -= c * w
                n = math.sqrt(sum(x * x for x in g)) or 1
                out[i, j, k] = (g[0] / n, g[1] / n, g[2] / n)
    return out


def visible(grid, i, j, k, d):
    """Nothing solid between the voxel and a viewer in direction d (marching half units)."""
    x, y, z = grid.centre(i, j, k)
    for s in range(2, 4 * grid.r):
        t = s * 0.5
        c = grid.cell(x + d[0] * t, y + d[1] * t, z + d[2] * t)
        if c != (i, j, k) and grid.at(*c):
            return False
        if not (0 <= c[0] < grid.nx and 0 <= c[1] < grid.ny and 0 <= c[2] < grid.nz):
            return True
    return True


def texture(grid, views, surface):
    """Each surface voxel's colour: (palette index, dimming, the view it came from)."""
    colour = {}
    for (i, j, k), n in surface.items():
        order = sorted(views, key=lambda v: -(n[0] * v.toward[0] + n[1] * v.toward[1]))
        x, y, z = grid.centre(i, j, k)
        for v in order:
            if not visible(grid, i, j, k, v.toward):
                continue
            idx = v.texel(*v.project(x, y, z))
            if idx < 0:                           # voted in, outside this drawing: the nearest texel
                u, zz = v.project(x, y, z)
                near = [(du * du + dz * dz, v.texel(u + du, zz + dz)) for du in (-1, 0, 1) for dz in (-1, 0, 1)]
                near = [p for p in near if p[1] >= 0]
                if not near:
                    continue
                idx = min(near)[1]
            facing = max(0.0, n[0] * v.toward[0] + n[1] * v.toward[1])
            colour[i, j, k] = (idx, 0.45 + 0.55 * min(1.0, facing / 0.5), v)
            break
    # Surface voxels no drawing sees (pockets, tops behind the brow): the nearest coloured
    # neighbour's colour, a little darker, growing inward a ring at a time.
    for _ in range(3):
        grown = {}
        for key in surface:
            if key in colour:
                continue
            near = [colour[n] for n in ((key[0] + a, key[1] + b, key[2] + c) for a in (-1, 0, 1) for b in (-1, 0, 1) for c in (-1, 0, 1))
                    if n in colour]
            if near:
                idx, dim, v = near[0]
                grown[key] = (idx, dim * 0.8, v)
        colour.update(grown)
    return colour


# -- Redraw: orthographic, one pixel per unit, from any angle.
def redraw(grid, colour, pal, angle, pad=2):
    a = math.radians(angle)
    toward = (math.sin(a), -math.cos(a), 0.0)
    right = (math.cos(a), math.sin(a), 0.0)
    w = 2 * grid.r
    h = grid.nz + pad
    rows = []
    for py in range(h):
        z = grid.bottom + grid.nz - py - 0.5
        row = []
        for px in range(w):
            u = px - grid.r + 0.5
            ox, oy = u * right[0] + toward[0] * grid.r * 1.5, u * right[1] + toward[1] * grid.r * 1.5
            rgb = (24, 24, 30)
            for s in range(int(grid.r * 3 / 0.5)):
                t = s * 0.5
                c = grid.cell(ox - toward[0] * t, oy - toward[1] * t, z)
                if grid.at(*c):
                    hit = colour.get(c)
                    if hit is None:                 # an inside voxel reached through a gap: darkest edge
                        rgb = (10, 8, 8)
                    else:
                        idx, dim, _ = hit
                        rgb = tuple(int(pal[idx * 3 + q] * dim) for q in range(3))
                    break
            row.append(rgb)
        rows.append(row)
    return rows


def sprite_rows(view, pal, grid, pad=2):
    """The WAD drawing on the same canvas as a redraw at its angle."""
    w, h = 2 * grid.r, grid.nz + pad
    rows = []
    for py in range(h):
        z = grid.bottom + grid.nz - py - 0.5
        row = []
        for px in range(w):
            idx = view.texel(px - grid.r + 0.5, z)
            row.append(tuple(pal[idx * 3 + q] for q in range(3)) if idx >= 0 else (24, 24, 30))
        rows.append(row)
    return rows


def scaled(rows, s):
    out = []
    for row in rows:
        line = [p for p in row for _ in range(s)]
        out.extend([line] * s)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('wad')
    ap.add_argument('out')
    ap.add_argument('--sprite', default='POSSA')
    ap.add_argument('--vote', type=int, default=7)
    ap.add_argument('--scale', type=int, default=3)
    args = ap.parse_args()
    lumps = read_wad(args.wad)
    pal = lumps['PLAYPAL'][:768]
    # Which way the rotations go round: the way whose carved body matches its drawings best.
    best = None
    for sign in (1, -1):
        views = views_of(lumps, args.sprite, sign)
        grid = Grid(views)
        carve(grid, views, args.vote)
        fit = sum(iou(outline(grid, v), drawn(v, v.shift)) for v in views) / 8
        print(f'rotations {"counter-clockwise" if sign > 0 else "clockwise"}: outline match {fit:.3f}', file=sys.stderr)
        if best is None or fit > best[0]:
            best = (fit, sign, views, grid)
    _, sign, views, grid = best
    align(grid, views, args.vote)
    fits = [iou(outline(grid, v), drawn(v, v.shift)) for v in views]
    print('shifts', [v.shift for v in views], 'match', [f'{f:.2f}' for f in fits], file=sys.stderr)
    surface = normals(grid)
    colour = texture(grid, views, surface)
    print(f'{sum(grid.solid)} voxels, {len(surface)} on the surface, {len(colour)} coloured', file=sys.stderr)
    # The redraws: matching each drawing, then halfway between.
    same = [redraw(grid, colour, pal, v.angle) for v in views]
    between = [redraw(grid, colour, pal, v.angle + sign * 22.5) for v in views]
    wad = [sprite_rows(v, pal, grid) for v in views]
    # Agreement with the drawings: share of the drawing's texels the redraw gives the same colour.
    same_colour = []
    for v, r, d in zip(views, same, wad):
        hits = total = 0
        for a, b in zip(r, d):
            for p, q in zip(a, b):
                if q != (24, 24, 30):
                    total += 1
                    hits += p == q
        same_colour.append(hits / max(1, total))
    rows = [('The WAD\'s drawings (rotations 1-8)', wad, [f'rotation {k + 1}' + (' (mirrored)' if v.flip else '') for k, v in enumerate(views)]),
            ('The carved body redrawn at the same angles (check)', same,
             [f'{f * 100:.0f}% outline, {c * 100:.0f}% same colour' for f, c in zip(fits, same_colour)]),
            ('Redrawn halfway between (no drawing exists)', between, [f'rotation {k + 1}½' for k in range(8)])]
    s = args.scale
    page = ['<!doctype html><meta charset="utf-8"><title>Death hull</title>',
            '<style>body{background:#16161a;color:#ddd;font:13px system-ui;margin:16px}figure{display:inline-block;margin:0 6px 10px 0}'
            'figcaption{font-size:11px;color:#999}img{image-rendering:pixelated;display:block}h2{font-size:14px;margin:18px 0 6px}</style>',
            f'<h1>{html.escape(args.sprite)}: a body carved from eight rotations</h1>',
            f'<p>Vote {args.vote} of 8; rotations go {"counter-clockwise" if sign > 0 else "clockwise"}; '
            f'sideways nudges {[v.shift for v in views]}. {sum(grid.solid)} voxels.</p>']
    for title, cells, labels in rows:
        page.append(f'<h2>{html.escape(title)}</h2>')
        for cell, label in zip(cells, labels):
            data = png(len(cell[0]) * s, len(cell) * s, [[q for p in row for q in p] for row in scaled(cell, s)])
            page.append(f'<figure><img src="data:image/png;base64,{base64.b64encode(data).decode()}">'
                        f'<figcaption>{html.escape(label)}</figcaption></figure>')
    open(args.out, 'w').write('\n'.join(page))
    cw, ch = len(wad[0][0]) * s, len(wad[0]) * s
    sheet = [[(16, 16, 20)] * (8 * (cw + 4)) for _ in range(3 * (ch + 4))]
    for ri, (_, cells, _) in enumerate(rows):
        for ci, cell in enumerate(cells):
            for y, line in enumerate(scaled(cell, s)):
                sheet[ri * (ch + 4) + y][ci * (cw + 4):ci * (cw + 4) + cw] = line
    open(os.path.splitext(args.out)[0] + '.png', 'wb').write(
        png(len(sheet[0]), len(sheet), [[q for p in row for q in p] for row in sheet]))


if __name__ == '__main__':
    main()
