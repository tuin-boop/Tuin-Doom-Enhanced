"""The carved body (death_hull.py) as a puppet: rigged, laid on the floor in
death_relief.py's poses, and drawn two ways, beside death_relief's relief.

Rig, found in 3D: from the floor up, each slice of the body splits into
pieces; where two pieces of leg stop being two is the crotch. The legs are
tracked up from the feet piece by piece (the walk frame's stride parts them
front to back, which the front drawing alone can't see), with hip, knee and
ankle at the joint finder's proportions (35% of the leg's length above the
crotch, 22% above the floor, the knee between), at the middle of each leg's
slice. The neck is the head finder's on rotation 1, the chest joint between.
Below the hips, voxels outside both legs (a gun) ride with the chest.

Drawn two ways: as cubes straight into the view (what the GPU would do with
the body in the world), and redrawn at sprite scale, one pixel per unit at
the body's distance, then enlarged like a sprite (the projection back into
the game). Both lit like death_relief: the art's colour times a light from
above and in front, from normals that turn with the body.

Usage: python3 -I tests/death_hull_pose.py doom2.wad build/death-hull-pose.html [--sprite POSSA --relief POSSG1]
"""
import argparse
import base64
import html
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from death_hull import Grid, align, carve, normals, texture, views_of  # noqa: E402
from death_lab import png, read_wad  # noqa: E402
from death_pose import rig  # noqa: E402
from death_relief import (IDENT, POSES, View, add, apply, draw_body, find_head, mesh, mul, ramp, rot, shade,  # noqa: E402
                          skeleton, solve, sub, tube_heights)
from death_relief import posed as relief_posed  # noqa: E402
from death_lab import Picture  # noqa: E402

def pieces(grid, k):
    """A slice's connected pieces (4-neighbours), as lists of (i, j), largest first."""
    seen, out = set(), []
    for j in range(grid.ny):
        for i in range(grid.nx):
            if (i, j) in seen or not grid.at(i, j, k):
                continue
            todo, piece = [(i, j)], []
            seen.add((i, j))
            while todo:
                a, b = todo.pop()
                piece.append((a, b))
                for c, d in ((a + 1, b), (a - 1, b), (a, b + 1), (a, b - 1)):
                    if (c, d) not in seen and grid.at(c, d, k):
                        seen.add((c, d))
                        todo.append((c, d))
            out.append(piece)
    return sorted(out, key=len, reverse=True)


def middle(piece):
    return sum(p[0] for p in piece) / len(piece), sum(p[1] for p in piece) / len(piece)


def body_rig(grid, front_pic, heights):
    """Hips, knees and ankles at the given heights (the hand rig's: the carved legs are too rough
    to read them from); each slice below the hips split between two leg middles that start on
    the two largest pieces at the floor and follow their own voxels up (two-means in the slice).
    Voxels far from both (a gun) ride with the chest."""
    hip, knee, ankle = heights
    start = next(k for k in range(grid.nz) if pieces(grid, k))
    feet = pieces(grid, start)
    if len(feet) < 2:
        feet = pieces(grid, start + 1)
    centres = [middle(p) for p in feet[:2]] if len(feet) >= 2 else [middle(feet[0])] * 2
    if len(feet) < 2:
        centres = [(centres[0][0] - 3, centres[0][1]), (centres[0][0] + 3, centres[0][1])]
    track, label = {}, {}
    for k in range(start, grid.nz):
        z = grid.bottom + k + 0.5
        if z >= hip + 1.5:
            break
        cells = [(i, j) for j in range(grid.ny) for i in range(grid.nx) if grid.at(i, j, k)]
        for _ in range(4):
            groups = ([], [])
            for c in cells:
                d = [math.dist(c, m) for m in centres]
                if min(d) <= 6:
                    groups[d.index(min(d))].append(c)
            centres = [middle(g) if g else m for g, m in zip(groups, centres)]
        track[k] = list(centres)
        for c in cells:
            d = [math.dist(c, m) for m in centres]
            label[c + (k,)] = 'carried' if min(d) > 6 and z > ankle else ('l', 'r')[d.index(min(d))]
    world = lambda ij, z: (ij[0] - grid.r + 0.5, ij[1] - grid.r + 0.5, z)
    slice_at = lambda z: track[min(max(int(z - grid.bottom), min(track)), max(track))]
    order = (0, 1) if slice_at(knee)[0][0] <= slice_at(knee)[1][0] else (1, 0)
    if order == (1, 0):
        label = {key: {'l': 'r', 'r': 'l'}.get(v, v) for key, v in label.items()}
    joints = {}
    for side, n in zip(('l', 'r'), order):
        joints['thigh_' + side] = world(slice_at(hip - 2)[n], hip)
        joints['shin_' + side] = world(slice_at(knee)[n], knee)
        joints['ankle_' + side] = world(slice_at(ankle)[n], ankle)
    neck = find_head(front_pic)
    nm = middle(pieces(grid, int(neck[0] - grid.bottom))[0])
    mid = tuple((joints['thigh_l'][q] + joints['thigh_r'][q]) / 2 for q in range(2))
    chest_z = hip + 0.45 * (neck[0] - hip)
    bones = dict(pelvis=(mid[0], mid[1], hip), chest=(mid[0], mid[1], chest_z), head=(neck[1], nm[1] - grid.r + 0.5, neck[0]),
                 thigh_l=joints['thigh_l'], shin_l=joints['shin_l'], thigh_r=joints['thigh_r'], shin_r=joints['shin_r'],
                 hip=hip, chest_z=chest_z, neck=neck[0], knee_l=knee, knee_r=knee,
                 ankle_l=joints['ankle_l'], ankle_r=joints['ankle_r'])
    return bones, label


def between(a, b):
    """The smallest rotation turning direction a onto direction b."""
    na, nb = math.sqrt(sum(x * x for x in a)), math.sqrt(sum(x * x for x in b))
    a, b = tuple(x / na for x in a), tuple(x / nb for x in b)
    axis = (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])
    if math.sqrt(sum(x * x for x in axis)) < 1e-6:
        return rot((1, 0, 0), 0)
    return rot(axis, math.degrees(math.acos(max(-1.0, min(1.0, sum(a[i] * b[i] for i in range(3)))))))


def transpose(m):
    return tuple(tuple(m[j][i] for j in range(3)) for i in range(3))


def neutral(bones, pose):
    """A pose made for straight, upright legs, put on a stance whose legs aren't: each thigh is
    first turned straight down, each shin straight down under it, then the pose's turns follow."""
    out = dict(pose)
    down = (0, 0, -1)
    for side in ('l', 'r'):
        hip, knee, ankle = bones['thigh_' + side], bones['shin_' + side], bones['ankle_' + side]
        nt = between(sub(knee, hip), down)
        ns = between(sub(ankle, knee), apply(transpose(nt), down))
        pt, ps = pose.get('thigh_' + side, IDENT), pose.get('shin_' + side, IDENT)
        out['thigh_' + side] = mul(pt, nt)
        out['shin_' + side] = mul(mul(transpose(nt), mul(ps, nt)), ns)
    return out


def weights(bones, p, side):
    """death_relief.weights with the leg chosen by the 3D label, not by a split across the picture."""
    if side == 'carried':
        return {'chest': 1.0}
    z = p[2]
    w = {}
    up = ramp(z, bones['hip'], 1.5) if side else 1.0
    if up > 0:
        c, hd = ramp(z, bones['chest_z']), ramp(z, bones['neck'])
        for part, v in (('pelvis', 1 - c), ('chest', c * (1 - hd)), ('head', c * hd)):
            if v > 0:
                w[part] = up * v
    if up < 1 and side:
        shin = 1 - ramp(z, bones['knee_' + side])
        w['thigh_' + side] = (1 - up) * (1 - shin)
        w['shin_' + side] = (1 - up) * shin
    return w


def posed_voxels(grid, colour, surface, bones, label, pose, drop_front=None, straighten=True):
    """Every coloured surface voxel moved by the skeleton: (centre, normal, colour), set on the floor."""
    xf = solve(bones, neutral(bones, pose) if straighten else pose)
    out = []
    for key, (idx, dim, _) in colour.items():
        p = grid.centre(*key)
        if drop_front is not None and p[1] < bones['chest'][1] - drop_front:
            continue                                # the rifle held out in front: dropped
        w = weights(bones, p, label.get(key))
        q = (0.0, 0.0, 0.0)
        for part, v in w.items():
            m, t = xf[part]
            q = add(q, tuple(v * x for x in add(apply(m, p), t)))
        main = max(w, key=w.get)
        out.append((q, apply(xf[main][0], surface[key]), xf[main][0], idx, dim))
    lo = min(p[2] for p, *_ in out)
    xs = [p[0] for p, *_ in out]
    ys = [p[1] for p, *_ in out]
    shift = (-(min(xs) + max(xs)) / 2, -(min(ys) + max(ys)) / 2, -lo + 0.5)
    return [(add(p, shift), n, m, idx, dim) for p, n, m, idx, dim in out]


def aim(view, body):
    pts = [p for p, *_ in body]
    view.frame(tuple((min(p[i] for p in pts) + max(p[i] for p in pts)) / 2 for i in range(3)))


def draw_cubes(view, body, pal):
    """Each voxel as a cube turned with its part, drawn straight into the view."""
    aim(view, body)
    faces = [((1, 0, 0), (0, 1, 0), (0, 0, 1)), ((-1, 0, 0), (0, 0, 1), (0, 1, 0)), ((0, 1, 0), (0, 0, 1), (1, 0, 0)),
             ((0, -1, 0), (1, 0, 0), (0, 0, 1)), ((0, 0, 1), (1, 0, 0), (0, 1, 0)), ((0, 0, -1), (0, 1, 0), (1, 0, 0))]
    for p, n, m, idx, dim in body:
        rgb = shade(pal, idx, n, dim)
        for f, a, b in faces:
            fw, aw, bw = apply(m, f), apply(m, a), apply(m, b)
            c = add(p, tuple(0.5 * x for x in fw))
            if sum(fw[i] * (view.cam[i] - c[i]) for i in range(3)) <= 0:
                continue
            q = [add(c, tuple(0.5 * (sa * aw[i] + sb * bw[i]) for i in range(3))) for sa, sb in ((-1, -1), (1, -1), (1, 1), (-1, 1))]
            view.triangle(q[0], q[1], q[2], rgb)
            view.triangle(q[0], q[2], q[3], rgb)


def nearest_colour(pal, rgb, cache={}):
    if rgb not in cache:
        cache[rgb] = min(range(256), key=lambda i: sum((pal[i * 3 + q] - rgb[q]) ** 2 for q in range(3)))
    return cache[rgb]


def tidy(rgb, pal):
    """The redraw as palette indices, each pixel snapped to PLAYPAL, then a lone pixel (at most
    one neighbour of its colour) takes the colour most of its eight neighbours share (4+)."""
    h, w = len(rgb), len(rgb[0])
    idx = [[None if c is None else nearest_colour(pal, c) for c in row] for row in rgb]
    out = [row[:] for row in idx]
    for j in range(h):
        for i in range(w):
            if idx[j][i] is None:
                continue
            around = [idx[j + b][i + a] for a in (-1, 0, 1) for b in (-1, 0, 1)
                      if (a or b) and 0 <= i + a < w and 0 <= j + b < h and idx[j + b][i + a] is not None]
            if around.count(idx[j][i]) <= 1 and around:
                top = max(set(around), key=around.count)
                if around.count(top) >= 4:
                    out[j][i] = top
    return [[None if c is None else tuple(pal[c * 3:c * 3 + 3]) for c in row] for row in out]


def draw_sprite_scale(view, body, pal, dist, tidy_up=False):
    """Redrawn at one pixel per unit at the body's distance (each voxel's 2x2x2 points into the
    nearest pixel, nearest in front wins), then enlarged to the view's size like a sprite."""
    aim(view, body)
    small = dist / view.f                         # view pixels per sprite pixel, inverted
    sw, sh = int(view.w * small) + 2, int(view.h * small) + 2
    depth = [[1e9] * sw for _ in range(sh)]
    rgb = [[None] * sw for _ in range(sh)]
    for p, n, m, idx, dim in body:
        colour = shade(pal, idx, n, dim)
        for a in (-0.25, 0.25):
            for b in (-0.25, 0.25):
                for c in (-0.25, 0.25):
                    x, y, z = view.camera(add(p, apply(m, (a, b, c))))
                    if z < 1:
                        continue
                    px = int((view.cx + view.f * x / z) * small)
                    py = int((view.cy - view.f * y / z) * small)
                    if 0 <= px < sw and 0 <= py < sh and z < depth[py][px]:
                        depth[py][px] = z
                        rgb[py][px] = colour
    if tidy_up:
        rgb = tidy(rgb, pal)
    for j in range(view.h):
        for i in range(view.w):
            q = rgb[int(j * small)][int(i * small)]
            if q is not None:
                view.rgb[j][i] = q


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('wad')
    ap.add_argument('out')
    ap.add_argument('--sprite', default='POSSA')
    ap.add_argument('--relief', default='POSSG1')
    ap.add_argument('--back', default='POSSA5')
    ap.add_argument('--vote', type=int, default=7)
    ap.add_argument('--drop-front', type=float, default=None, help='leave out voxels this far in front of the chest (a rifle held out)')
    args = ap.parse_args()
    lumps = read_wad(args.wad)
    pal = lumps['PLAYPAL'][:768]
    views = views_of(lumps, args.sprite, 1)
    grid = Grid(views)
    carve(grid, views, args.vote)
    align(grid, views, args.vote)
    surface = normals(grid)
    colour = texture(grid, views, surface)
    bones, label = body_rig(grid, views[0].pic, (27.0, 15.5, 4.0))
    print('rig', {k: tuple(round(x, 1) for x in v) if isinstance(v, tuple) else round(v, 1) for k, v in bones.items()}, file=sys.stderr)
    # death_relief's best body, for comparison.
    pic, joints = rig(lumps, pal, args.relief)
    rbones = skeleton(joints, find_head(pic))
    rmesh = mesh(pic, tube_heights(pic), 1.0, 1.0, Picture(lumps[args.back]))
    columns = [('Relief from ' + args.relief + ' (round 15), 128', 'relief', 128, 0),
               ('Carved body as cubes, 128', 'cubes', 128, 0),
               ('Carved body redrawn at sprite scale, 128', 'sprite', 128, 0),
               ('Redrawn, palette-snapped and lone pixels tidied, 128', 'tidy', 128, 0),
               ('Tidied, 256', 'tidy', 256, 0),
               ('Tidied, walked 70 degrees round', 'tidy', 128, 70)]
    rows = []
    for title, pose in POSES:
        body = posed_voxels(grid, colour, surface, bones, label, pose, args.drop_front)
        cells = []
        for label_, how, dist, orbit in columns:
            v = View(dist, orbit)
            if how == 'relief':
                draw_body(v, pic, pal, relief_posed(rmesh, rbones, joints['carried_texels'], pose), False, True)
            elif how == 'cubes':
                draw_cubes(v, body, pal)
            else:
                draw_sprite_scale(v, body, pal, dist, how == 'tidy')
            cells.append((label_, v))
        rows.append((title, cells))
        print(title, file=sys.stderr)
    page = ['<!doctype html><meta charset="utf-8"><title>Death hull poses</title>',
            '<style>body{background:#16161a;color:#ddd;font:13px system-ui;margin:16px}figure{display:inline-block;margin:0 6px 10px 0}'
            'figcaption{font-size:11px;color:#999}img{image-rendering:pixelated;display:block}h2{font-size:14px;margin:18px 0 6px}</style>',
            f'<h1>{html.escape(args.sprite)} carved from eight rotations, posed</h1>']
    for title, cells in rows:
        page.append(f'<h2>{html.escape(title)}</h2>')
        for label_, v in cells:
            page.append(f'<figure><img src="data:image/png;base64,{base64.b64encode(v.png()).decode()}">'
                        f'<figcaption>{html.escape(label_)}</figcaption></figure>')
    open(args.out, 'w').write('\n'.join(page))
    cw, ch = rows[0][1][0][1].w, rows[0][1][0][1].h
    sheet = [[(16, 16, 20)] * (len(columns) * (cw + 4)) for _ in range(len(rows) * (ch + 4))]
    for i, (_, cells) in enumerate(rows):
        for k, (_, v) in enumerate(cells):
            for j in range(ch):
                sheet[i * (ch + 4) + j][k * (cw + 4):k * (cw + 4) + cw] = v.rgb[j]
    open(os.path.splitext(args.out)[0] + '.png', 'wb').write(
        png(len(sheet[0]), len(sheet), [[c for p in row for c in p] for row in sheet]))


if __name__ == '__main__':
    main()
