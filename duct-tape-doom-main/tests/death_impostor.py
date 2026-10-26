"""Flattening the ragdoll back into Doom's 2D: the body as fallen (death_ragdoll.py)
drawn three ways beside the vanilla corpse drawing.

- 3D: the relief's quads straight into the view (what the game draws now).
- Squashed: the same quads pressed toward the plane facing the player, their
  depth along the line of sight kept at a share.
- Virtual camera: the posed body rendered by a camera of its own, looking
  down at it from above the player's line of sight (pitch), orthographic, one
  pixel per unit, nearest texel, its direction stepped to the nearest 45
  degrees like Doom's rotations; the picture then stands on an ordinary
  upright sprite card at the body, its lowest solid row on the floor, facing
  the player like every other sprite. id's corpse drawings are this: a body
  seen from above, shown upright.

Usage: python3 -I tests/death_impostor.py doom2.wad build/death-impostor.html [--frame POSSG1 --back POSSA5 --corpse POSSL0]
"""
import argparse
import base64
import html
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from death_lab import Picture, png, read_wad  # noqa: E402
from death_ragdoll import FALLS, draw, run, setup, skin, transforms  # noqa: E402
from death_relief import View, normal, shade, sub  # noqa: E402

STEP = math.radians(45)


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def middle_of(quads):
    pts = [p for q, *_ in quads for p in q]
    return ((min(p[0] for p in pts) + max(p[0] for p in pts)) / 2, (min(p[1] for p in pts) + max(p[1] for p in pts)) / 2)


def squashed(quads, eye, keep):
    """Each point's distance along the line of sight from the body's middle kept at a share."""
    mx, my = middle_of(quads)
    dx, dy = mx - eye[0], my - eye[1]
    n = math.hypot(dx, dy) or 1
    f = (dx / n, dy / n, 0.0)
    out = []
    for q, rest, idx, dim in quads:
        moved = []
        for p in q:
            along = (p[0] - mx) * f[0] + (p[1] - my) * f[1]
            moved.append((p[0] - f[0] * along * (1 - keep), p[1] - f[1] * along * (1 - keep), p[2]))
        out.append((moved, rest, idx, dim))
    return out


def picture(quads, pal, eye, pitch):
    """The body seen by the virtual camera: {(column, row): rgb}, one pixel per unit, x to the
    camera's right, rows down; the camera looks from the player's side, stepped to 45 degrees,
    pitched down by pitch."""
    mx, my = middle_of(quads)
    a = math.atan2(my - eye[1], mx - eye[0])
    a = round(a / STEP) * STEP
    fh = (math.cos(a), math.sin(a))
    p = math.radians(pitch)
    f = (fh[0] * math.cos(p), fh[1] * math.cos(p), -math.sin(p))
    r = (fh[1], -fh[0], 0.0)
    u = cross(r, f)
    o = (mx, my, 0.0)
    depth, image = {}, {}
    for q, rest, idx, dim in quads:
        n = normal(q)
        if dot(n, f) >= 0:
            continue                                # turned away from the virtual camera
        rgb = shade(pal, idx, n, dim * (0.4 + 0.6 * abs(normal(rest)[1])))
        pts = [(dot(sub(c, o), r), -dot(sub(c, o), u), dot(sub(c, o), f)) for c in q]
        for tri in ((pts[0], pts[1], pts[2]), (pts[0], pts[2], pts[3])):
            x0, x1 = math.floor(min(t[0] for t in tri)), math.ceil(max(t[0] for t in tri))
            y0, y1 = math.floor(min(t[1] for t in tri)), math.ceil(max(t[1] for t in tri))
            area = (tri[1][0] - tri[0][0]) * (tri[2][1] - tri[0][1]) - (tri[2][0] - tri[0][0]) * (tri[1][1] - tri[0][1])
            if abs(area) < 1e-9:
                continue
            for j in range(y0, y1 + 1):
                for i in range(x0, x1 + 1):
                    px, py = i + 0.5, j + 0.5
                    w0 = ((tri[1][0] - px) * (tri[2][1] - py) - (tri[2][0] - px) * (tri[1][1] - py)) / area
                    w1 = ((tri[2][0] - px) * (tri[0][1] - py) - (tri[0][0] - px) * (tri[2][1] - py)) / area
                    w2 = 1 - w0 - w1
                    if w0 < 0 or w1 < 0 or w2 < 0:
                        continue
                    d = w0 * tri[0][2] + w1 * tri[1][2] + w2 * tri[2][2]
                    if d < depth.get((i, j), 1e9):
                        depth[i, j] = d
                        image[i, j] = rgb
    return image, (mx, my)


def draw_card(view, image, at, aim):
    """The picture as a sprite: upright, facing the player, its lowest row on the floor."""
    view.frame(aim)
    if not image:
        return
    bottom = max(j for _, j in image)
    for (i, j), rgb in image.items():
        z = bottom - j
        p = lambda di, dz: (at[0] + view.right[0] * (i + di), at[1] + view.right[1] * (i + di), z + dz)
        q = [p(0, 1), p(1, 1), p(1, 0), p(0, 0)]
        view.triangle(q[0], q[1], q[2], rgb)
        view.triangle(q[0], q[2], q[3], rgb)


def draw_sprite(view, pic, pal, at, aim):
    view.frame(aim)
    ground = max(r for r in range(pic.height) if any(t >= 0 for t in pic.texels[r]))
    for r in range(pic.height):
        for c in range(pic.width):
            idx = pic.texels[r][c]
            if idx < 0:
                continue
            u, z = c - pic.left, ground - r
            p = lambda du, dz: (at[0] + view.right[0] * (u + du), at[1] + view.right[1] * (u + du), z + dz)
            q = [p(0, 1), p(1, 1), p(1, 0), p(0, 0)]
            rgb = tuple(int(pal[idx * 3 + k] * 0.85) for k in range(3))
            view.triangle(q[0], q[1], q[2], rgb)
            view.triangle(q[0], q[2], q[3], rgb)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('wad')
    ap.add_argument('out')
    ap.add_argument('--frame', default='POSSG1')
    ap.add_argument('--back', default='POSSA5')
    ap.add_argument('--corpse', default='POSSL0')
    ap.add_argument('--tics', type=int, default=72)
    args = ap.parse_args()
    lumps = read_wad(args.wad)
    pal = lumps['PLAYPAL'][:768]
    pic, joints, bones, rest, quads = setup(lumps, pal, args.frame, args.back, 'profile')
    corpse = Picture(lumps[args.corpse])
    columns = ['Vanilla corpse drawing', '3D (in game now)', 'Squashed to 35% depth', 'Virtual camera, 30 degrees down',
               'Virtual camera, 50 degrees down']
    scenes = [(FALLS[0], 0, -1), (FALLS[1], 0, -1), (FALLS[2], 0, -1), (FALLS[3], 0, -1), (FALLS[0], 70, -1), (FALLS[2], 70, -1),
              (FALLS[0], 0, 4), (FALLS[1], 0, 5)]
    rows = []
    for (title, fall), orbit, step in scenes:
        frames = run(rest, fall, args.tics)
        pose = frames[step]
        posed = skin(quads, bones, joints['carried_texels'], transforms(rest, pose))
        mx, my = middle_of(posed)
        aim = (mx, my, 10.0)
        cells = []
        for col in range(5):
            v = View(128, orbit, 240, 160)
            eye = v.cam
            if col == 0:
                draw_sprite(v, corpse, pal, (mx, my), aim)
            elif col == 1:
                draw(v, posed, pal, aim)
            elif col == 2:
                draw(v, squashed(posed, eye, 0.35), pal, aim)
            else:
                image, at = picture(posed, pal, eye, 30 if col == 3 else 50)
                draw_card(v, image, at, aim)
            cells.append(v)
        label = f'{title.split(":")[0]}, ' + ('settled' if step < 0 else f'mid-fall (tic {step * 3})') + (f', walked {orbit} degrees round' if orbit else '')
        rows.append((label, cells))
        print(label, file=sys.stderr)
    page = ['<!doctype html><meta charset="utf-8"><title>Death impostor</title>',
            '<style>body{background:#16161a;color:#ddd;font:13px system-ui;margin:16px}figure{display:inline-block;margin:0 6px 10px 0}'
            'figcaption{font-size:11px;color:#999}img{image-rendering:pixelated;display:block}h2{font-size:14px;margin:18px 0 6px}</style>',
            f'<h1>{html.escape(args.frame)}: flattening the ragdoll back into a sprite</h1>']
    for label, cells in rows:
        page.append(f'<h2>{html.escape(label)}</h2>')
        for name, v in zip(columns, cells):
            page.append(f'<figure><img src="data:image/png;base64,{base64.b64encode(v.png()).decode()}" width="480">'
                        f'<figcaption>{html.escape(name)}</figcaption></figure>')
    open(args.out, 'w').write('\n'.join(page))
    cw, ch = rows[0][1][0].w, rows[0][1][0].h
    sheet = [[(16, 16, 20)] * (5 * (cw + 4)) for _ in range(len(rows) * (ch + 4))]
    for i, (_, cells) in enumerate(rows):
        for k, v in enumerate(cells):
            for j in range(ch):
                sheet[i * (ch + 4) + j][k * (cw + 4):k * (cw + 4) + cw] = v.rgb[j]
    open(os.path.splitext(args.out)[0] + '.png', 'wb').write(
        png(len(sheet[0]), len(sheet), [[c for p in row for c in p] for row in sheet]))


if __name__ == '__main__':
    main()
