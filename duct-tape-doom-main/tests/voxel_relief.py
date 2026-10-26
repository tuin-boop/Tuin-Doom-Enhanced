"""Would a better depth guess make the lying bodies better? death_relief's poses
drawn with the round-tube guess, with depth from the side drawing, and with the
true depth behind each texel from an artist's voxel model (reference only,
never shipped: the ceiling any depth guess could reach), beside that voxel
model itself posed on the same skeleton.

Usage: python3 -I tests/voxel_relief.py doom2.wad VOXELDIR build/voxel-relief.html [--frame POSSG1 --back POSSA5]
"""
import argparse
import base64
import html
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from death_lab import Picture, png, read_wad  # noqa: E402
from death_pose import rig  # noqa: E402
from death_relief import (POSES, View, add, apply, draw_body, find_head, mesh, posed, shade, skeleton, solve,  # noqa: E402
                          tube_heights, weights)
from voxel_reference import line_up, lump, placed, read_kvx, side_depths, side_guess, solid  # noqa: E402


def true_depths(body, pic):
    """Per texel of the drawing: how far the voxels' front stands toward the viewer and their back
    away from it (units, from the drawing's plane through the origin)."""
    depth = {}
    for (x, y, z) in body:
        lo, hi = depth.get((x, z), (y, y))
        depth[x, z] = (min(lo, y), max(hi, y))
    front, back = {}, {}
    for r in range(pic.height):
        for c in range(pic.width):
            key = (c - pic.left, pic.top - r - 1)
            if pic.texels[r][c] >= 0 and key in depth:
                front[c, r] = -depth[key][0]
                back[c, r] = depth[key][1] + 1
    return front, back


def draw_voxels(view, model, body, bones, pose, pal_unused=None):
    """The artist's model posed on the drawing's skeleton, each surface voxel a cube, its own colours."""
    xf = solve(bones, pose)
    pts = []
    cubes = []
    for (x, y, z), src in body.items():
        col = model['surface'].get(src)
        if col is None:
            continue
        p = (x + 0.5, y + 0.5, z + 0.5)
        w = weights(bones, p, False)
        q = (0.0, 0.0, 0.0)
        for part, v in w.items():
            m, t = xf[part]
            q = add(q, tuple(v * a for a in add(apply(m, p), t)))
        main = max(w, key=w.get)
        cubes.append((q, xf[main][0], col))
        pts.append(q)
    lo = min(p[2] for p in pts)
    cx = (min(p[0] for p in pts) + max(p[0] for p in pts)) / 2
    cy = (min(p[1] for p in pts) + max(p[1] for p in pts)) / 2
    cubes = [((p[0] - cx, p[1] - cy, p[2] - lo), m, col) for p, m, col in cubes]
    view.frame(tuple((min(p[0][i] for p in cubes) + max(p[0][i] for p in cubes)) / 2 for i in range(3)))
    faces = [((1, 0, 0), (0, 1, 0), (0, 0, 1)), ((-1, 0, 0), (0, 0, 1), (0, 1, 0)), ((0, 1, 0), (0, 0, 1), (1, 0, 0)),
             ((0, -1, 0), (1, 0, 0), (0, 0, 1)), ((0, 0, 1), (1, 0, 0), (0, 1, 0)), ((0, 0, -1), (0, 1, 0), (1, 0, 0))]
    for p, m, col in cubes:
        for f, a, b in faces:
            fw, aw, bw = apply(m, f), apply(m, a), apply(m, b)
            c = add(p, tuple(0.5 * x for x in fw))
            if sum(fw[i] * (view.cam[i] - c[i]) for i in range(3)) <= 0:
                continue
            rgb = shade(model['pal'], col, fw)
            q = [add(c, tuple(0.5 * (sa * aw[i] + sb * bw[i]) for i in range(3))) for sa, sb in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
            view.triangle(q[0], q[1], q[2], rgb)
            view.triangle(q[0], q[2], q[3], rgb)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('wad')
    ap.add_argument('voxels')
    ap.add_argument('out')
    ap.add_argument('--frame', default='POSSG1')
    ap.add_argument('--back', default='POSSA5')
    args = ap.parse_args()
    lumps = read_wad(args.wad)
    pal = lumps['PLAYPAL'][:768]
    pic, joints = rig(lumps, pal, args.frame)
    bones = skeleton(joints, find_head(pic))
    back_pic = Picture(lumps[args.back])
    model = read_kvx(os.path.join(args.voxels, args.frame[:5].lower() + '.kvx'))
    cells = solid(model)
    sname, sflip = lump(lumps, args.frame[:5] + '3')
    side = Picture(lumps[sname])
    if sflip:
        side.texels = [row[::-1] for row in side.texels]
        side.left = side.width - side.left
    fit, turn, lift = line_up(model, cells, pic, side, pal)
    body = placed(model, cells, turn, lift)
    print(f'{args.frame}: outline match {fit:.2f}, turn {turn}, lift {lift}', file=sys.stderr)
    heights = tube_heights(pic)
    sided = {k: v / 2 for k, v in side_guess(pic, heights, side_depths(side)).items()}
    tf, tb = true_depths(body, pic)
    meshes = dict(tube=mesh(pic, heights, 1.0, 1.0, back_pic),
                  side=mesh(pic, sided, 1.0, 1.0, back_pic),
                  true=mesh(pic, tf, 1.0, 1.0, back_pic, tb))
    columns = [('Round tube guess (round 15)', 'tube', 128, 0), ('Depth from the side drawing', 'side', 128, 0),
               ('True depth behind each texel (voxel reference)', 'true', 128, 0),
               ('The artist\'s voxel model itself, posed (reference only)', 'voxel', 128, 0),
               ('True depth, walked 70 degrees round', 'true', 128, 70), ('Voxel model, walked 70 degrees round', 'voxel', 128, 70)]
    rows = []
    for title, pose in POSES:
        cells_ = []
        for label, how, dist, orbit in columns:
            v = View(dist, orbit)
            if how == 'voxel':
                draw_voxels(v, model, body, bones, pose)
            else:
                draw_body(v, pic, pal, posed(meshes[how], bones, joints['carried_texels'], pose), False, True)
            cells_.append((label, v))
        rows.append((title, cells_))
        print(title, file=sys.stderr)
    page = ['<!doctype html><meta charset="utf-8"><title>Voxel relief</title>',
            '<style>body{background:#16161a;color:#ddd;font:13px system-ui;margin:16px}figure{display:inline-block;margin:0 6px 10px 0}'
            'figcaption{font-size:11px;color:#999}img{image-rendering:pixelated;display:block}h2{font-size:14px;margin:18px 0 6px}</style>',
            f'<h1>{html.escape(args.frame)}: depth guesses vs the voxel reference, lying down</h1>']
    for title, cells_ in rows:
        page.append(f'<h2>{html.escape(title)}</h2>')
        for label, v in cells_:
            page.append(f'<figure><img src="data:image/png;base64,{base64.b64encode(v.png()).decode()}">'
                        f'<figcaption>{html.escape(label)}</figcaption></figure>')
    open(args.out, 'w').write('\n'.join(page))
    cw, ch = rows[0][1][0][1].w, rows[0][1][0][1].h
    sheet = [[(16, 16, 20)] * (len(columns) * (cw + 4)) for _ in range(len(rows) * (ch + 4))]
    for i, (_, cells_) in enumerate(rows):
        for k, (_, v) in enumerate(cells_):
            for j in range(ch):
                sheet[i * (ch + 4) + j][k * (cw + 4):k * (cw + 4) + cw] = v.rgb[j]
    open(os.path.splitext(args.out)[0] + '.png', 'wb').write(
        png(len(sheet[0]), len(sheet), [[c for p in row for c in p] for row in sheet]))


if __name__ == '__main__':
    main()
