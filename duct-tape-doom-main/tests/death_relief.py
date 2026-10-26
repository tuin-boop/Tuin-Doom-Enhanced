"""Can a dead monster lie on the floor as a body rather than a card? A still
test: a drawing's texels given thickness, posed by a skeleton, seen from the
player's eye, beside the vanilla corpse drawing.

The body: each texel rises as the round tube sprite_normals.cpp gives it
(the radius of the largest disk inside the silhouette covering it, at true
depth here, not the shading's 1.75), a bas-relief mesh over the texel
corners, closed by walls at the outline. Three bodies are compared: a flat
card, a front-only relief (flat back) and a two-sided one (the drawing's
own texels mirrored behind, a pillow).

The skeleton: the hand rig's hips, knees and ankles (death_pose.RIGS), the
neck from joint_finder.find_head and a chest joint between, so the body is
pelvis, chest, head and two thighs and shins. Joints rotate in 3D; texels
blend between parts over a unit or two at each joint (linear blend), and
anything the rig carries rides with the chest. Each pose is set on the
floor by its lowest point.

Lighting: the art's own colour times a light from above and in front of the
player, from the posed surface (normals turning with the bones) or, in one
column, from the upright surface (normals frozen, as the falls draw today).

Writes an HTML page (keep it in build/, out of git: it embeds id's art).
Usage: python3 -I tests/death_relief.py doom2.wad build/death-relief.html [--sprite POSSG1 --corpse POSSL0]
"""
import argparse
import base64
import html
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from death_lab import Picture, png, read_wad  # noqa: E402
from death_pose import rig  # noqa: E402
from joint_finder import find_head  # noqa: E402

EYE = 41.0                      # the player's eye above the floor
LIGHT = (0.35, -0.5, 0.79)      # toward the light: above, from the player's side


# -- The body: texel heights, then a closed mesh over the texel corners.
def tube_heights(pic):
    """Each opaque texel's height above the drawing's plane (units), as sprite_normals.cpp
    inflates it but at true depth: a round tube of the largest disk covering the texel."""
    w, h = pic.width, pic.height
    opaque = lambda c, r: 0 <= c < w and 0 <= r < h and pic.texels[r][c] >= 0
    edge = [(c, r) for r in range(-1, h + 1) for c in range(-1, w + 1)
            if not opaque(c, r) and any(opaque(c + dc, r + dr) for dc, dr in ((1, 0), (-1, 0), (0, 1), (0, -1)))]
    dist = {}
    for r in range(h):
        for c in range(w):
            if opaque(c, r):
                dist[c, r] = math.sqrt(min((c - x) ** 2 + (r - y) ** 2 for x, y in edge))
    thick = dict(dist)
    for (c, r), d in dist.items():
        if any(dist.get((c + k, r + j), 0) >= d + math.hypot(j, k) - 0.01
               for j in (-1, 0, 1) for k in (-1, 0, 1) if j or k):
            continue                                # not a maximal disk
        n = int(d)
        for j in range(-n, n + 1):
            for k in range(-n, n + 1):
                if (c + k, r + j) in thick and j * j + k * k <= d * d:
                    thick[c + k, r + j] = max(thick[c + k, r + j], d)
    height = {}
    for key, d in dist.items():
        e, rad = d - 0.5, thick[key] - 0.5
        height[key] = math.sqrt(max(0.0, e * (2 * rad - e)))

    def blur(hs, dc, dr, sigma):
        radius = int(math.ceil(sigma * 2.5))
        out = {}
        for (c, r) in hs:
            s = wt = 0.0
            for t in range(-radius, radius + 1):
                v = hs.get((c + t * dc, r + t * dr))
                if v is not None:
                    g = math.exp(-0.5 * t * t / (sigma * sigma))
                    s += v * g
                    wt += g
            out[c, r] = s / wt
        return out
    height = blur(height, 1, 0, 1.2)
    return blur(height, 0, 1, min(max(h * 0.04, 1.5), 4.0))


BANDS = ((0.0, 0.45), (0.45, 0.8), (0.8, 1.2))     # legs, trunk, head: shares of the drawing's height


def boxier(pic, heights, power):
    """Each front run's heights reshaped toward a flat top with steep sides (power under 1): a
    round tube is a dome, which lying down reads as a sausage."""
    out = dict(heights)
    for r in range(pic.height):
        c = 0
        while c < pic.width:
            if (c, r) not in heights:
                c += 1
                continue
            a = c
            while c < pic.width and (c, r) in heights:
                c += 1
            top = max(heights[x, r] for x in range(a, c))
            if top > 0:
                for x in range(a, c):
                    out[x, r] = top * (heights[x, r] / top) ** power
    return out


def side_scaled(pic, heights, side):
    """The tube heights rescaled so the body is as deep as its side drawing (rotation 3) shows,
    per band (legs, trunk, head): a round tube makes every part as deep as it is wide, and a
    soldier lying down came out rounder than his own side view. Per band, the side drawing's
    depth is a low percentile of its rows' run widths (each leg its own run; a gun or an arm
    held out widens a row, so the narrower rows count), the tube's is the median of twice each
    row's tallest texel. The ratio (kept within 0.35-1.6) blends between band middles."""
    def runs(p, r):
        out, start = [], None
        for c in range(p.width + 1):
            on = c < p.width and p.texels[r][c] >= 0
            if on and start is None:
                start = c
            if not on and start is not None:
                if c - start >= 2:
                    out.append(c - start)
                start = None
        return out
    top = pic.top
    marks = []
    for lo, hi in BANDS:
        widths, tubes = [], []
        for r in range(side.height):
            z = side.top - r - 0.5
            if lo <= z / top < hi:
                rs = sorted(runs(side, r))
                if rs:
                    widths.append(rs[len(rs) // 2])
        for r in range(pic.height):
            z = pic.top - r - 0.5
            if lo <= z / top < hi:
                row = [heights[c, r] for c in range(pic.width) if (c, r) in heights]
                if row:
                    tubes.append(2 * max(row))
        if widths and tubes:
            widths.sort()
            tubes.sort()
            ratio = widths[int(0.4 * (len(widths) - 1))] / max(1.0, tubes[len(tubes) // 2])
            marks.append(((lo + min(hi, 1.0)) / 2 * top, min(1.6, max(0.35, ratio))))
    if not marks:
        return dict(heights)

    def at(z):
        if z <= marks[0][0]:
            return marks[0][1]
        for (z0, a), (z1, b) in zip(marks, marks[1:]):
            if z <= z1:
                return a + (b - a) * (z - z0) / (z1 - z0)
        return marks[-1][1]
    return {(c, r): h * at(pic.top - r - 0.5) for (c, r), h in heights.items()}


def side_profile(pic, heights, side, legs=0.45):
    """Front and back heights from the side drawing's own outline (rotation 3: the body's face
    toward the drawing's left, its back to the right, the origin on the drawing's plane), so a
    body seen from the side has the side drawing's profile, not a symmetric bolster: per height
    above the legs, the run around the origin gives how far the body reaches forward and back,
    and each texel of the front drawing's row is raised in proportion to its tube height. In the
    legs (the side view's stride parts them) each leg is as deep as its own run, split evenly.
    Extents kept within 0.3-1.3 of the tube's."""
    def runs(r):
        out, start = [], None
        for c in range(side.width + 1):
            on = c < side.width and side.texels[r][c] >= 0
            if on and start is None:
                start = c
            if not on and start is not None:
                if c - start >= 2:
                    out.append((start - side.left, c - side.left))
                start = None
        return out
    ext = {}
    top = pic.top
    for r in range(side.height):
        z = side.top - r - 1
        rs = runs(r)
        if not rs:
            continue
        if z + 0.5 < legs * top:
            widths = sorted(b - a for a, b in rs)
            w = widths[len(widths) // 2] / 2
            ext[z] = (w, w)
        else:
            a, b = min(rs, key=lambda ab: 0 if ab[0] <= 0 <= ab[1] else min(abs(ab[0]), abs(ab[1])))
            ext[z] = (max(0.5, -a), max(0.5, b))
    # One front and one back reach per band (a low percentile: arms and a gun held out reach
    # forward in some rows; per row they raised the chest into a fin), blended between bands.
    marks = []
    for lo, hi in BANDS:
        zs = [z for z in ext if lo <= (z + 0.5) / top < hi]
        if zs:
            fs = sorted(ext[z][0] for z in zs)
            bs = sorted(ext[z][1] for z in zs)
            marks.append(((lo + min(hi, 1.0)) / 2 * top, fs[int(0.4 * (len(fs) - 1))], bs[int(0.4 * (len(bs) - 1))]))

    def reach(z):
        if z <= marks[0][0]:
            return marks[0][1:]
        for (z0, f0, b0), (z1, f1, b1) in zip(marks, marks[1:]):
            if z <= z1:
                w = (z - z0) / (z1 - z0)
                return f0 + (f1 - f0) * w, b0 + (b1 - b0) * w
        return marks[-1][1:]
    smooth = {z: reach(z) for z in range(int(pic.top - pic.height) - 1, int(pic.top) + 2)} if marks else {}
    front, back = {}, {}
    for r in range(pic.height):
        z = pic.top - r - 1
        row = [heights[c, r] for c in range(pic.width) if (c, r) in heights]
        if not row:
            continue
        peak = max(row) or 1.0
        f, b = smooth.get(z, (peak, peak))
        f = min(max(f, 0.3 * peak), 1.3 * peak)
        b = min(max(b, 0.3 * peak), 1.3 * peak)
        for c in range(pic.width):
            if (c, r) in heights:
                share = heights[c, r] / peak
                front[c, r] = f * share
                back[c, r] = b * share
    return front, back


def back_colour(pic, back_pic, c, r):
    """The back view's texel behind a front texel: the back drawing (rotation 5) seen from
    behind has the body's right on its left, so it is read mirrored, matched by offsets, from
    the nearest solid texel within three; none there (or no back drawing), the front's own."""
    if back_pic is None:
        return pic.texels[r][c]
    u, z = c - pic.left + 0.5, pic.top - r - 0.5
    bc, br = int(math.floor(-u + back_pic.left)), int(math.floor(back_pic.top - z))
    best = None
    for dr in range(-3, 4):
        for dc in range(-3, 4):
            x, y = bc + dc, br + dr
            if 0 <= x < back_pic.width and 0 <= y < back_pic.height and back_pic.texels[y][x] >= 0:
                d = dc * dc + dr * dr
                if best is None or d < best[0]:
                    best = (d, back_pic.texels[y][x])
    return best[1] if best else pic.texels[r][c]


def mesh(pic, heights, front, back, back_pic=None, back_heights=None):
    """Quads (four rest-pose corners, the texel they belong to, palette index, dimming) of the
    closed relief. Rest pose: x right, y away from the player, z up; the drawing faces the
    player (-y). Corner heights are the mean of the four texels around (empty ones 0), so the
    outline sits half raised. The back takes the back drawing's colours when there is one;
    the walls at the outline take the texel's, dimmed like an edge. back_heights, when given,
    raise the back by their own amounts (a body thicker behind than in front)."""
    w, h = pic.width, pic.height
    opaque = lambda c, r: 0 <= c < w and 0 <= r < h and pic.texels[r][c] >= 0
    ch = {}
    for r in range(h + 1):
        for c in range(w + 1):
            ch[c, r] = sum(heights.get((c - a, r - b), 0.0) for a in (0, 1) for b in (0, 1)) / 4
    cb = ch
    if back_heights is not None:
        cb = {k: sum(back_heights.get((k[0] - a, k[1] - b), 0.0) for a in (0, 1) for b in (0, 1)) / 4 for k in ch}
    pt = lambda c, r, side: (c - pic.left, -side * (ch if side > 0 else cb)[c, r], pic.top - r)
    quads = []
    for r in range(h):
        for c in range(w):
            if not opaque(c, r):
                continue
            corners = [(c, r), (c + 1, r), (c + 1, r + 1), (c, r + 1)]
            idx = pic.texels[r][c]
            quads.append(([pt(x, y, front) for x, y in corners], (c, r), idx, 1.0))
            quads.append(([pt(x, y, -back) for x, y in reversed(corners)], (c, r), back_colour(pic, back_pic, c, r), 1.0))
            for (dc, dr), (a, b) in (((0, -1), (0, 1)), ((1, 0), (1, 2)), ((0, 1), (2, 3)), ((-1, 0), (3, 0))):
                if not opaque(c + dc, r + dr):
                    p, q = corners[a], corners[b]
                    quads.append(([pt(*p, front), pt(*p, -back), pt(*q, -back), pt(*q, front)], (c, r), idx, 0.7))
    return quads


# -- The skeleton: 3x3 rotations and rigid transforms (R, t), world = R p + t.
def rot(axis, deg):
    a = math.radians(deg)
    x, y, z = axis
    n = math.sqrt(x * x + y * y + z * z)
    x, y, z = x / n, y / n, z / n
    c, s, k = math.cos(a), math.sin(a), 1 - math.cos(a)
    return ((c + x * x * k, x * y * k - z * s, x * z * k + y * s),
            (y * x * k + z * s, c + y * y * k, y * z * k - x * s),
            (z * x * k - y * s, z * y * k + x * s, c + z * z * k))


def mul(a, b):
    return tuple(tuple(sum(a[i][k] * b[k][j] for k in range(3)) for j in range(3)) for i in range(3))


def apply(m, p):
    return tuple(m[i][0] * p[0] + m[i][1] * p[1] + m[i][2] * p[2] for i in range(3))


def add(a, b):
    return (a[0] + b[0], a[1] + b[1], a[2] + b[2])


def sub(a, b):
    return (a[0] - b[0], a[1] - b[1], a[2] - b[2])


IDENT = rot((1, 0, 0), 0)
X, Y, Z = (1, 0, 0), (0, 1, 0), (0, 0, 1)
PARENT = dict(pelvis=None, chest='pelvis', head='chest', thigh_l='pelvis', shin_l='thigh_l', thigh_r='pelvis', shin_r='thigh_r')


def skeleton(joints, neck):
    """Each part's joint (rest pose, on the drawing's plane), from the rig and the head finder."""
    l, r = joints['left'], joints['right']
    hip = l['hip'][1]
    mid = (l['hip'][0] + r['hip'][0]) / 2
    chest = hip + 0.45 * (neck[0] - hip)
    at = lambda u, z: (u, 0.0, z)
    return dict(pelvis=at(mid, hip), chest=at(mid, chest), head=at(neck[1], neck[0]),
                thigh_l=at(*l['hip']), shin_l=at(*l['knee']), thigh_r=at(*r['hip']), shin_r=at(*r['knee']),
                split=joints['split'], knee_l=l['knee'][1], knee_r=r['knee'][1], hip=hip, chest_z=chest, neck=neck[0])


def solve(bones, pose):
    """World transforms of every part: the root turned by pose['root'], each joint by its own turn
    (in its parent's frame)."""
    out = {}
    for part in PARENT:                             # parents come first
        q = pose.get(part, IDENT)
        j = bones[part]
        if PARENT[part] is None:
            m = mul(pose.get('root', IDENT), q)
            out[part] = (m, sub(j, apply(m, j)))
        else:
            pm, pt = out[PARENT[part]]
            out[part] = (mul(pm, q), add(apply(pm, sub(j, apply(q, j))), pt))
    return out


def ramp(z, joint, half=1.0):
    return min(1.0, max(0.0, (z - joint) / (2 * half) + 0.5))


def weights(bones, p, carried):
    """Part weights of a rest point: up the legs into the pelvis, chest and head, blended over
    a unit or so each side of a joint; the rig's carried texels ride with the chest."""
    if carried:
        return {'chest': 1.0}
    u, z = p[0], p[2]
    w = {}
    up = ramp(z, bones['hip'], 1.5)
    if up > 0:
        c, hd = ramp(z, bones['chest_z']), ramp(z, bones['neck'])
        for part, v in (('pelvis', 1 - c), ('chest', c * (1 - hd)), ('head', c * hd)):
            if v > 0:
                w[part] = w.get(part, 0) + up * v
    if up < 1:
        left = min(1.0, max(0.0, 0.5 - (u - bones['split']) / 3))
        for side, sv in (('l', left), ('r', 1 - left)):
            if sv <= 0:
                continue
            shin = 1 - ramp(z, bones['knee_' + side])
            w['thigh_' + side] = w.get('thigh_' + side, 0) + (1 - up) * sv * (1 - shin)
            w['shin_' + side] = w.get('shin_' + side, 0) + (1 - up) * sv * shin
    return w


def posed(quads, bones, carried, pose):
    """The quads moved by the skeleton, then set down on the floor and centred."""
    xf = solve(bones, pose)
    cache = {}

    def move(p, carry):
        key = (p, carry)
        if key not in cache:
            q = (0.0, 0.0, 0.0)
            for part, v in weights(bones, p, carry).items():
                m, t = xf[part]
                q = add(q, tuple(v * x for x in add(apply(m, p), t)))
            cache[key] = q
        return cache[key]
    out = [([move(p, texel in carried) for p in corners], corners, idx, dim) for corners, texel, idx, dim in quads]
    pts = [p for q, _, _, _ in out for p in q]
    lo = [min(p[i] for p in pts) for i in range(3)]
    hi = [max(p[i] for p in pts) for i in range(3)]
    shift = (-(lo[0] + hi[0]) / 2, -(lo[1] + hi[1]) / 2, -lo[2])
    return [([add(p, shift) for p in q], rest, idx, dim) for q, rest, idx, dim in out]


# -- The poses: where each one leaves the body. Turns are degrees about the rest axes
# (x right, y away, z up); a thigh turned by -x swings its knee toward the player.
POSES = [
    ('Standing (check: should read as the drawing)', {}),
    ('On its back, feet toward you', dict(root=rot(X, -90), thigh_l=rot(X, -20), shin_l=rot(X, 35),
                                          thigh_r=rot(Y, 8), head=rot(Z, 35), chest=rot(Y, 6))),
    ('On its back, lying across', dict(root=mul(rot(Z, 80), rot(X, -90)), thigh_r=rot(X, -40), shin_r=rot(X, 60),
                                       thigh_l=rot(Y, -10), head=rot(Z, -40))),
    ('Face down, head toward you', dict(root=rot(X, 90), thigh_l=rot(Y, 10), shin_r=rot(X, 45), head=rot(Z, 60))),
    ('On its side, curled (drawn side to you)', dict(root=rot(Y, 90), thigh_l=rot(X, -60), shin_l=rot(X, 70),
                                                     thigh_r=rot(X, -45), shin_r=rot(X, 80), chest=rot(X, 25), head=rot(X, 15))),
    ('Slumped on its knees', dict(root=rot(X, 40), thigh_l=rot(X, -120), shin_l=rot(X, 165), thigh_r=rot(X, -115),
                                  shin_r=rot(X, 160), chest=rot(X, 35), head=rot(X, 10))),
]
BODIES = [('Flat card', 0.0, 0.0), ('Front relief', 1.0, 0.0), ('Two-sided relief', 1.0, 1.0)]


# -- Drawing: a z-buffered rasteriser at the player's eye, floor checkered every 16 units.
class View:
    def __init__(self, dist, orbit=0.0, width=300, height=190, focal=480.0):
        a = math.radians(orbit)
        self.fwd, self.right = (math.sin(a), math.cos(a), 0.0), (math.cos(a), -math.sin(a), 0.0)
        self.cam = (-dist * math.sin(a), -dist * math.cos(a), EYE)
        self.w, self.h, self.f = width, height, focal

    def camera(self, p):
        d = sub(p, self.cam)
        return (sum(d[i] * self.right[i] for i in range(3)), d[2], sum(d[i] * self.fwd[i] for i in range(3)))

    def frame(self, aim):
        """Centre the picture on a world point (as a crop of a 960-wide, 90-degree view)."""
        x, y, z = self.camera(aim)
        self.cx, self.cy = self.w / 2 - self.f * x / z, self.h / 2 + self.f * y / z
        self.rgb = [[None] * self.w for _ in range(self.h)]
        self.depth = [[1e9] * self.w for _ in range(self.h)]
        for j in range(self.h):
            for i in range(self.w):
                dx, dy = (i + 0.5 - self.cx) / self.f, (self.cy - j - 0.5) / self.f
                if dy >= -1e-4:
                    self.rgb[j][i] = (34, 34, 40)
                    continue
                t = EYE / -dy
                wx = self.cam[0] + t * (dx * self.right[0] + self.fwd[0])
                wy = self.cam[1] + t * (dx * self.right[1] + self.fwd[1])
                g = 52 if (math.floor(wx / 16) + math.floor(wy / 16)) % 2 else 60
                self.rgb[j][i] = (g, g - 4, g - 10)

    def triangle(self, a, b, c, rgb):
        pts = [self.camera(p) for p in (a, b, c)]
        if min(p[2] for p in pts) < 1:
            return
        s = [(self.cx + self.f * x / z, self.cy - self.f * y / z, 1 / z) for x, y, z in pts]
        area = (s[1][0] - s[0][0]) * (s[2][1] - s[0][1]) - (s[2][0] - s[0][0]) * (s[1][1] - s[0][1])
        if abs(area) < 1e-9:
            return
        x0, x1 = max(0, int(min(p[0] for p in s))), min(self.w - 1, int(max(p[0] for p in s)) + 1)
        y0, y1 = max(0, int(min(p[1] for p in s))), min(self.h - 1, int(max(p[1] for p in s)) + 1)
        for j in range(y0, y1 + 1):
            py = j + 0.5
            for i in range(x0, x1 + 1):
                px = i + 0.5
                w0 = ((s[1][0] - px) * (s[2][1] - py) - (s[2][0] - px) * (s[1][1] - py)) / area
                w1 = ((s[2][0] - px) * (s[0][1] - py) - (s[0][0] - px) * (s[2][1] - py)) / area
                w2 = 1 - w0 - w1
                if w0 < 0 or w1 < 0 or w2 < 0:
                    continue
                inv = w0 * s[0][2] + w1 * s[1][2] + w2 * s[2][2]
                if 1 / inv < self.depth[j][i]:
                    self.depth[j][i] = 1 / inv
                    self.rgb[j][i] = rgb

    def png(self):
        return png(self.w, self.h, [[v for p in row for v in p] for row in self.rgb])


def normal(q):
    u, v = sub(q[2], q[0]), sub(q[3], q[1])
    n = (v[1] * u[2] - v[2] * u[1], v[2] * u[0] - v[0] * u[2], v[0] * u[1] - v[1] * u[0])
    length = math.sqrt(sum(x * x for x in n)) or 1
    return tuple(x / length for x in n)


def shade(pal, idx, n, dim=1.0):
    lit = dim * (0.5 + 0.6 * max(0.0, sum(n[i] * LIGHT[i] for i in range(3)) / math.sqrt(sum(x * x for x in LIGHT))))
    return tuple(min(255, int(pal[idx * 3 + k] * lit)) for k in range(3))


def draw_body(view, pic, pal, quads, frozen=False, grazing=False):
    pts = [p for q, _, _, _ in quads for p in q]
    view.frame(tuple((min(p[i] for p in pts) + max(p[i] for p in pts)) / 2 for i in range(3)))
    for q, rest, idx, dim in quads:
        n = normal(q)
        if sum(n[i] * sub(view.cam, q[0])[i] for i in range(3)) <= 0:
            continue                                # facing away
        if grazing:                                 # no drawing shows a surface turned edge-on to it: darken it
            dim *= 0.4 + 0.6 * abs(normal(rest)[1])
        rgb = shade(pal, idx, normal(rest) if frozen else n, dim)
        view.triangle(q[0], q[1], q[2], rgb)
        view.triangle(q[0], q[2], q[3], rgb)


def draw_sprite(view, pic, pal):
    """A drawing as the game draws it: upright, facing the player, feet at its offset."""
    view.frame((0.0, 0.0, pic.top - pic.height / 2))
    flat = 0.5 + 0.6 * max(0.0, -LIGHT[1]) / math.sqrt(sum(x * x for x in LIGHT))
    for r in range(pic.height):
        for c in range(pic.width):
            idx = pic.texels[r][c]
            if idx < 0:
                continue
            at = lambda u, z: (view.right[0] * u, view.right[1] * u, z)
            u0, z0 = c - pic.left, pic.top - r
            q = [at(u0, z0), at(u0 + 1, z0), at(u0 + 1, z0 - 1), at(u0, z0 - 1)]
            rgb = tuple(min(255, int(pal[idx * 3 + k] * flat)) for k in range(3))
            view.triangle(q[0], q[1], q[2], rgb)
            view.triangle(q[0], q[2], q[3], rgb)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('wad')
    ap.add_argument('out')
    ap.add_argument('--sprite', default='POSSG1')
    ap.add_argument('--corpse', default='POSSL0')
    ap.add_argument('--back', default='POSSA5', help='the back view drawing for the relief\'s back (rotation 5)')
    args = ap.parse_args()
    lumps = read_wad(args.wad)
    pal = lumps['PLAYPAL'][:768]
    pic, joints = rig(lumps, pal, args.sprite)
    neck = find_head(pic)
    if neck is None:
        raise SystemExit(f'{args.sprite}: no head found')
    bones = skeleton(joints, neck)
    heights = tube_heights(pic)
    corpse = Picture(lumps[args.corpse])
    columns = [(f'{name}, 128 units', (depth, back, False), 128, 0, False, False) for name, depth, back in BODIES]
    columns += [(f'Two-sided, back from {args.back}, 128', (1.0, 1.0, True), 128, 0, False, False),
                ('Same, normals frozen (upright)', (1.0, 1.0, True), 128, 0, True, False),
                ('Same, edge-on parts darkened', (1.0, 1.0, True), 128, 0, False, True),
                ('Darkened, 256 units', (1.0, 1.0, True), 256, 0, False, True),
                ('Darkened, 128, walked 70 degrees round', (1.0, 1.0, True), 128, 70, False, True)]
    back_pic = Picture(lumps[args.back]) if args.back else None
    meshes = {key: mesh(pic, heights, key[0], key[1], back_pic if key[2] else None) for _, key, *_ in columns}
    rows = []
    vanilla = []
    for dist, orbit in ((128, 0), (256, 0), (128, 70)):
        v = View(dist, orbit)
        draw_sprite(v, corpse, pal)
        vanilla.append((f'{args.corpse} at {dist} units' + (f', walked {orbit} degrees round' if orbit else ''), v))
    rows.append((f'Vanilla corpse ({args.corpse}): one drawing, turned to face you from anywhere', vanilla))
    for title, pose in POSES:
        cells = []
        for label, key, dist, orbit, frozen, grazing in columns:
            v = View(dist, orbit)
            draw_body(v, pic, pal, posed(meshes[key], bones, joints['carried_texels'], pose), frozen, grazing)
            cells.append((label, v))
        rows.append((title, cells))
        print(title, file=sys.stderr)
    page = ['<!doctype html><meta charset="utf-8"><title>Death relief</title>',
            '<style>body{background:#16161a;color:#ddd;font:13px system-ui;margin:16px}figure{display:inline-block;margin:0 6px 10px 0}'
            'figcaption{font-size:11px;color:#999}img{image-rendering:pixelated;display:block}h2{font-size:14px;margin:18px 0 6px}</style>',
            f'<h1>Lying bodies from {html.escape(args.sprite)}: card vs relief</h1>',
            '<p>Seen from the player\'s eye (41 units up), 90-degree view at 960 wide. Neck found at '
            f'{neck[0]:.1f} units, chest joint at {bones["chest_z"]:.1f}, hips at {bones["hip"]:.1f}.</p>']
    for title, cells in rows:
        page.append(f'<h2>{html.escape(title)}</h2>')
        for label, v in cells:
            page.append(f'<figure><img src="data:image/png;base64,{base64.b64encode(v.png()).decode()}">'
                        f'<figcaption>{html.escape(label)}</figcaption></figure>')
    open(args.out, 'w').write('\n'.join(page))
    # The same cells on one contact sheet (row by row, a gap between), beside the page.
    cw, chh = rows[0][1][0][1].w, rows[0][1][0][1].h
    cols = max(len(cells) for _, cells in rows)
    sheet = [[(16, 16, 20)] * (cols * (cw + 4)) for _ in range(len(rows) * (chh + 4))]
    for i, (_, cells) in enumerate(rows):
        for k, (_, v) in enumerate(cells):
            for j in range(chh):
                sheet[i * (chh + 4) + j][k * (cw + 4):k * (cw + 4) + cw] = v.rgb[j]
    open(os.path.splitext(args.out)[0] + '.png', 'wb').write(
        png(len(sheet[0]), len(sheet), [[c for p in row for c in p] for row in sheet]))


if __name__ == '__main__':
    main()
