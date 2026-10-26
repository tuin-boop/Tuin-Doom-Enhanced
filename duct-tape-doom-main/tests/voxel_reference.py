"""Reference only: an artist's voxel models of Doom's monsters (KVX, one per
frame, from a local folder, never shipped or committed) as ground truth for
how thick the sprites really are, to tune DERP's own guess from the art.

The guess (death_relief.tube_heights, from sprite_normals.cpp): each texel
rises as a round tube of the largest disk inside the silhouette covering it,
so a part is as deep as it is wide. The voxels say how deep each part
really is. This lines each model up with its front drawing (rotation 1),
measures the true thickness behind every texel, and compares.

KVX (Ken Silverman's voxel format): header (byte count, x/y/z size, pivot in
1/256 units), column offsets, then per column slabs of (top, length, cull
flags, colours); z runs down; only surface voxels are stored; the last 768
bytes are a 6-bit palette.

Usage: python3 -I tests/voxel_reference.py doom2.wad VOXELDIR build/voxel-reference.html [--monsters POSS,SPOS,...]
"""
import argparse
import base64
import html
import math
import os
import struct
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from death_lab import Picture, png, read_wad  # noqa: E402
from death_relief import boxier, side_profile, side_scaled, tube_heights  # noqa: E402


def read_kvx(path):
    d = open(path, 'rb').read()
    _, xs, ys, zs, xp, yp, zp = struct.unpack_from('<7i', d, 0)
    xoff = struct.unpack_from(f'<{xs + 1}i', d, 28)
    base = 28 + (xs + 1) * 4
    xyoff = [struct.unpack_from(f'<{ys + 1}h', d, base + x * (ys + 1) * 2) for x in range(xs)]
    data = 28
    surface = {}
    for x in range(xs):
        for y in range(ys):
            pos = data + xoff[x] + xyoff[x][y]
            end = data + xoff[x] + xyoff[x][y + 1]
            while pos < end:
                top, length = d[pos], d[pos + 1]
                for k in range(length):
                    surface[x, y, top + k] = d[pos + 3 + k]
                pos += 3 + length
    pal = bytes(min(255, v * 4) for v in d[-768:])
    return dict(size=(xs, ys, zs), pivot=(xp / 256, yp / 256, zp / 256), surface=surface, pal=pal)


def solid(model):
    """Every voxel inside the surface: what a flood from outside can't reach."""
    xs, ys, zs = model['size']
    wall = model['surface']
    outside = set()
    todo = [(-1, -1, -1)]
    outside.add(todo[0])
    while todo:
        x, y, z = todo.pop()
        for a, b, c in ((1, 0, 0), (-1, 0, 0), (0, 1, 0), (0, -1, 0), (0, 0, 1), (0, 0, -1)):
            n = (x + a, y + b, z + c)
            if n in outside or (x + a, y + b, z + c) in wall:
                continue
            if -1 <= n[0] <= xs and -1 <= n[1] <= ys and -1 <= n[2] <= zs:
                outside.add(n)
                todo.append(n)
    return {(x, y, z) for x in range(xs) for y in range(ys) for z in range(zs) if (x, y, z) not in outside}


# The body frame (death_relief's rest pose): x right as the front drawing is seen, y away from
# its viewer, z up, units from the thing's origin. KVX's horizontal axes may face any way: the
# eight turns and mirrors are tried against the drawing.
TURNS = [(sx, sy, swap) for swap in (False, True) for sx in (1, -1) for sy in (1, -1)]


def placed(model, cells, turn, lift=0):
    sx, sy, swap = turn
    xp, yp, zp = model['pivot']
    out = {}
    for (x, y, z) in cells:
        a, b = x + 0.5 - xp, y + 0.5 - yp
        if swap:
            a, b = b, a
        out[(math.floor(sx * a), math.floor(sy * b), math.floor(zp - z - 0.5) + lift)] = (x, y, z)
    return out


def drawn_cells(pic):
    return {(c - pic.left, pic.top - r - 1) for r in range(pic.height) for c in range(pic.width) if pic.texels[r][c] >= 0}


def iou(a, b):
    return len(a & b) / max(1, len(a | b))


def line_up(model, cells, front, side, pal):
    """The turn and lift whose front and left-side views best match rotation 1 and 3
    (rotation 3 sees the body's left, from +x: its right on screen is +y), outlines first;
    a body is nearly its own mirror, so the front's colours (texel by texel, near or not)
    decide which way it faces. Returns (outline match, turn, lift)."""
    want_front, want_side = drawn_cells(front), drawn_cells(side)
    colours = {(c - front.left, front.top - r - 1): front.texels[r][c] for r in range(front.height) for c in range(front.width)
               if front.texels[r][c] >= 0}
    best = None
    for turn in TURNS:
        for lift in range(-6, 7):
            body = placed(model, cells, turn, lift)
            f = {(x, z) for x, y, z in body}
            s = {(y, z) for x, y, z in body}
            shape = iou(f, want_front) + iou(s, want_side)
            seen = {}
            for (x, y, z), src in body.items():
                col = model['surface'].get(src)
                if col is not None and ((x, z) not in seen or y < seen[x, z][0]):
                    seen[x, z] = (y, col)
            near = [sum((model['pal'][col * 3 + q] - pal[colours[k] * 3 + q]) ** 2 for q in range(3)) < 2700
                    for k, (_, col) in seen.items() if k in colours]
            score = shape + sum(near) / max(1, len(near))
            if best is None or score > best[0]:
                best = (score, turn, lift, shape / 2)
    return best[3], best[1], best[2]


def measure(body, front, heights, side=None):
    """Per texel of the front drawing: the voxels' true thickness behind it (back minus front, units)
    and our two-sided tube guess (twice the height), with the run width of its row (the part's width)."""
    depth = {}
    for (x, y, z) in body:
        lo, hi = depth.get((x, z), (y, y))
        depth[x, z] = (min(lo, y), max(hi, y))
    rows = []
    for r in range(front.height):
        runs, start = [], None
        for c in range(front.width + 1):
            on = c < front.width and front.texels[r][c] >= 0
            if on and start is None:
                start = c
            if not on and start is not None:
                runs.append((start, c - 1))
                start = None
        for a, b in runs:
            for c in range(a, b + 1):
                key = (c - front.left, front.top - r - 1)
                if key in depth:
                    lo, hi = depth[key]
                    rows.append(dict(u=key[0], z=key[1], true=hi - lo + 1, guess=2 * heights[c, r], width=b - a + 1, front=lo, back=hi,
                                     side=side.get((c, r), 0.0) if side else 0.0))
    return rows


def side_depths(side):
    """Per height, the body's depth as rotation 3 draws it: the median width of the row's runs
    (two or more texels; legs apart in a stride are two runs, each a leg's depth)."""
    out = {}
    for r in range(side.height):
        runs, start = [], None
        for c in range(side.width + 1):
            on = c < side.width and side.texels[r][c] >= 0
            if on and start is None:
                start = c
            if not on and start is not None:
                if c - start >= 2:
                    runs.append(c - start)
                start = None
        if runs:
            out[side.top - r - 1] = sorted(runs)[len(runs) // 2]
    return out


def side_guess(front, heights, depths):
    """Thickness per front texel from the side drawing: the row's depth there, shaped across each
    front run like the tube (the texel's height over the tallest in its run)."""
    out = {}
    for r in range(front.height):
        z = front.top - r - 1
        d = depths.get(z) or depths.get(z + 1) or depths.get(z - 1)
        c = 0
        while c < front.width:
            if front.texels[r][c] < 0:
                c += 1
                continue
            a = c
            while c < front.width and front.texels[r][c] >= 0:
                c += 1
            top = max(heights[x, r] for x in range(a, c)) or 1
            for x in range(a, c):
                out[x, r] = (d * heights[x, r] / top) if d else 2 * heights[x, r]
    return out


def band_stats(rows, top):
    out = []
    for name, lo, hi in (('legs', -99, 0.45), ('torso', 0.45, 0.8), ('head', 0.8, 99)):
        sel = [r for r in rows if lo <= r['z'] / top < hi]
        if not sel:
            continue
        t = sum(r['true'] for r in sel) / len(sel)
        g = sum(r['guess'] for r in sel) / len(sel)
        w = sum(r['width'] for r in sel) / len(sel)
        out.append((name, len(sel), t, g, w))
    return out


def render(cells_rgb, w0, w1, z0, z1, scale=4):
    rows = []
    for z in range(z1, z0 - 1, -1):
        row = []
        for u in range(w0, w1 + 1):
            row.append(cells_rgb.get((u, z), (24, 24, 30)))
        rows.append([p for p in row for _ in range(scale)])
    out = []
    for row in rows:
        out.extend([row] * scale)
    return png(len(out[0]), len(out), [[q for p in row for q in p] for row in out])


def views(body, model, pic, pal):
    """The model seen from the front and the left side (nearest voxel colour), and the drawing."""
    front, side = {}, {}
    for (x, y, z), src in body.items():
        col = model['surface'].get(src)
        if col is None:
            continue
        rgb = tuple(model['pal'][col * 3:col * 3 + 3])
        if (x, z) not in front or y < front[x, z][0]:
            front[x, z] = (y, rgb)
        if (y, z) not in side or x > side[y, z][0]:
            side[y, z] = (x, rgb)
    sprite = {(c - pic.left, pic.top - r - 1): tuple(pal[pic.texels[r][c] * 3:pic.texels[r][c] * 3 + 3])
              for r in range(pic.height) for c in range(pic.width) if pic.texels[r][c] >= 0}
    return {k: v[1] for k, v in front.items()}, {k: v[1] for k, v in side.items()}, sprite


def lump(lumps, name):
    """A frame-rotation's lump (name like POSSA3) and whether it is stored as its mirror (POSSA7 in POSSA3A7)."""
    for key in lumps:
        if key[:4] == name[:4] and key[4:6] == name[4:6] and len(key) in (6, 8):
            return key, False
        if key[:4] == name[:4] and len(key) == 8 and key[6:8] == name[4:6]:
            return key, True
    return None, False


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('wad')
    ap.add_argument('voxels')
    ap.add_argument('out')
    ap.add_argument('--monsters', default='POSS,SPOS,TROO,SARG,BOSS,SKUL,HEAD,PLAY')
    args = ap.parse_args()
    lumps = read_wad(args.wad)
    pal = lumps['PLAYPAL'][:768]
    page = ['<!doctype html><meta charset="utf-8"><title>Voxel reference</title>',
            '<style>body{background:#16161a;color:#ddd;font:13px system-ui;margin:16px}figure{display:inline-block;margin:0 6px 10px 0}'
            'figcaption{font-size:11px;color:#999}img{image-rendering:pixelated;display:block}td,th{padding:2px 8px;text-align:right}</style>',
            '<h1>Voxel reference: true thickness vs the tube guess (rotation 1, frame A)</h1>']
    table = []
    for m in args.monsters.split(','):
        path = os.path.join(args.voxels, m.lower() + 'a.kvx')
        fname, _ = lump(lumps, m + 'A1')
        sname, sflip = lump(lumps, m + 'A3')
        if not os.path.exists(path) or not fname or not sname:
            print(m, 'missing', file=sys.stderr)
            continue
        model = read_kvx(path)
        cells = solid(model)
        front, side = Picture(lumps[fname]), Picture(lumps[sname])
        if sflip:                                   # rotation 3 stored as the mirror of 7: flip it back
            side.texels = [row[::-1] for row in side.texels]
            side.left = side.width - side.left
        score, turn, lift = line_up(model, cells, front, side, pal)
        score *= 2
        body = placed(model, cells, turn, lift)
        heights = tube_heights(front)
        rows = measure(body, front, heights, side_guess(front, heights, side_depths(side)))
        banded = side_scaled(front, heights, side)
        bmap = {}
        for rr in range(front.height):
            for cc in range(front.width):
                if (cc, rr) in banded:
                    bmap[cc - front.left, front.top - rr - 1] = 2 * banded[cc, rr]
        for row in rows:
            row['band'] = bmap.get((row['u'], row['z']), row['guess'])
        pf, pb = side_profile(front, heights, side)
        pm = {(cc - front.left, front.top - rr - 1): pf[cc, rr] + pb[cc, rr] for (cc, rr) in pf}
        fm = {(cc - front.left, front.top - rr - 1): (pf[cc, rr], pb[cc, rr]) for (cc, rr) in pf}
        for row in rows:
            row['profile'] = pm.get((row['u'], row['z']), row['band'])
            f, b = fm.get((row['u'], row['z']), (row['guess'] / 2, row['guess'] / 2))
            row['pfront'], row['pback'] = f, b
        perr = sum(abs(r['pfront'] - (-r['front'])) + abs(r['pback'] - (r['back'] + 1)) for r in rows) / max(1, len(rows))
        terr = sum(abs(r['guess'] / 2 - (-r['front'])) + abs(r['guess'] / 2 - (r['back'] + 1)) for r in rows) / max(1, len(rows))
        for power in (0.6, 0.4):
            boxed = boxier(front, banded, power)
            bm = {(cc - front.left, front.top - rr - 1): 2 * v for (cc, rr), v in boxed.items()}
            for row in rows:
                row[f'box{power}'] = bm.get((row['u'], row['z']), row['band'])
        mae = lambda key: sum(abs(r[key] - r['true']) for r in rows) / max(1, len(rows))
        k = sum(r['true'] * r['guess'] for r in rows) / max(1e-6, sum(r['guess'] ** 2 for r in rows))
        oracle = sum(abs(k * r['guess'] - r['true']) for r in rows) / max(1, len(rows))
        print(f'{m}: mean error per texel: tube {mae("guess"):.1f}, side drawing per row {mae("side"):.1f}, '
              f'side band {mae("band"):.1f}, box0.6 {mae("box0.6"):.1f}, side profile {mae("profile"):.1f}; '
              f'front+back placement error: tube {terr:.1f}, side profile {perr:.1f}; '
              f'tube scaled by the best k={k:.2f} {oracle:.1f} (units)', file=sys.stderr)
        stats = band_stats(rows, front.top)
        allt = sum(r['true'] for r in rows) / max(1, len(rows))
        allg = sum(r['guess'] for r in rows) / max(1, len(rows))
        print(f'{m}: fit {score / 2:.2f} turn {turn} lift {lift}; mean thickness true {allt:.1f}, guess {allg:.1f}; '
              + '; '.join(f'{n} true {t:.1f} guess {g:.1f} width {w:.1f}' for n, _, t, g, w in stats), file=sys.stderr)
        table.append((m, score / 2, allt, allg, stats))
        fv, sv, sp = views(body, model, front, pal)
        keys = list(fv) + list(sv) + list(sp)
        u0 = min(k[0] for k in keys) - 1
        u1 = max(k[0] for k in keys) + 1
        z0 = min(k[1] for k in keys) - 1
        z1 = max(k[1] for k in keys) + 1
        page.append(f'<h2>{m} (outline match {score / 2:.2f})</h2>')
        for label, cells_rgb in ((f'{fname} (the drawing)', sp), ('voxels, front', fv), ('voxels, left side', sv)):
            page.append(f'<figure><img src="data:image/png;base64,{base64.b64encode(render(cells_rgb, u0, u1, z0, z1)).decode()}">'
                        f'<figcaption>{html.escape(label)}</figcaption></figure>')
    page.append('<h2>Thickness behind each texel (units), true vs guessed</h2><table><tr><th>monster</th><th>match</th><th>true</th><th>guess</th>'
                '<th>legs true/guess/width</th><th>torso</th><th>head</th></tr>')
    for m, fit, t, g, stats in table:
        cells = {n: f'{tt:.1f} / {gg:.1f} / {w:.1f}' for n, _, tt, gg, w in stats}
        page.append(f'<tr><td>{m}</td><td>{fit:.2f}</td><td>{t:.1f}</td><td>{g:.1f}</td>'
                    + ''.join(f'<td>{cells.get(n, "")}</td>' for n in ('legs', 'torso', 'head')) + '</tr>')
    page.append('</table>')
    open(args.out, 'w').write('\n'.join(page))


if __name__ == '__main__':
    main()
