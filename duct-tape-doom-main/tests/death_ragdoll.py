"""A ragdoll for round 15's relief body: the hit decides the fall, the body
lands as a body, and no vanilla death drawing is a target.

The doll: eleven points on the drawing's own skeleton (head, neck, both
shoulders, the spine at the chest joint, both hips, knees and ankles),
Verlet-integrated in game tics, held
together by bone lengths (the torso as two stiff blocks hinged at the
spine, the head on the neck), knees that bend only forward and never fold
flat, and a floor with friction.

Muscles: extra lengths across the joints (hip to ankle, spine to knee, neck
to hips, head to spine) hold the standing pose at first and let go on a
schedule set by the hit: a chest blast keeps the legs locked while the
shoulders are thrown, legs swept lets the knees go first, a head shot
cuts everything at once.

The settle rule: while it falls, the body is twisted about its own length so
it lands with its front up or toward the player, whichever is nearer (the
drawing only covers its front; round 15's lesson). Nothing turns it once it
lies.

The relief from death_relief (two-sided, back from rotation 5, edge-on parts
darkened) is skinned to the doll: each part's frame from its points.
Frames are drawn every 3 tics, the way the falls step in game.

--guided: the doll on strings, aimed at the zombieman's shotgun death
(POSSH-L) and shown beside it; see "Guided deaths" below for the knobs.

Usage: python3 -I tests/death_ragdoll.py doom2.wad build/death-ragdoll.html [--frame POSSG1 --back POSSA5]
       python3 -I tests/death_ragdoll.py doom2.wad build/death-guided.html --guided [--ratchet 0.9 --ratchet-tics 3
              --give 4 --key 0.35 --flinch --landing butt|flat|topple --launch 2.5 --lift 2 --spin 0.08 --no-variants]
"""
import argparse
import base64
import html
import math
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from death_lab import Picture, png, read_wad  # noqa: E402
from death_pose import RIGS, rig  # noqa: E402
from joint_finder import find  # noqa: E402
from death_relief import (View, add, boxier, find_head, side_profile, mesh, normal, shade, side_scaled, skeleton, sub,  # noqa: E402
                          tube_heights, weights)
from voxel_reference import lump  # noqa: E402

CELL = 4.0              # a floor height map's cells (Doll.floor), units
EDGE = 2.0              # how round a step's edge over a drop is (Doll.hold), units
GRAVITY = 0.3           # units per tic per tic: earth's at the body's scale (56 units ~ 1.7 m); Doom's 1.0 drops a body in 8 tics
SUBSTEPS = 4
STEP = 3                # tics per drawn pose


def dot(a, b):
    return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]


def cross(a, b):
    return (a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0])


def scale(a, s):
    return (a[0] * s, a[1] * s, a[2] * s)


def unit(a):
    n = math.sqrt(dot(a, a)) or 1.0
    return scale(a, 1 / n)


def mid(a, b):
    return scale(add(a, b), 0.5)


def turn(v, axis, angle):
    """v turned about a unit axis (Rodrigues)."""
    c, s = math.cos(angle), math.sin(angle)
    return add(add(scale(v, c), scale(cross(axis, v), s)), scale(axis, dot(axis, v) * (1 - c)))


# -- The doll.
def rest_points(pic, bones, joints):
    l, r = joints['left'], joints['right']
    neck = bones['neck']
    hu = bones['head'][0]
    # Shoulders: the body's width a little under the neck, from the drawing's widest run there.
    row = max(0, min(pic.height - 1, pic.top - int(neck - 3)))
    xs = [c - pic.left for c in range(pic.width) if pic.texels[row][c] >= 0]
    mx = bones['pelvis'][0]
    half = 0.8 * min(mx - min(xs), max(xs) - mx) if xs else 7
    at = lambda u, z: (u, 0.0, z)
    return dict(head=at(hu, pic.top - 4), neck=at(hu, neck), sh_l=at(mx - half, neck - 3), sh_r=at(mx + half, neck - 3),
                spine=bones['chest'], hip_l=at(*l['hip']), hip_r=at(*r['hip']), knee_l=at(*l['knee']), knee_r=at(*r['knee']),
                ank_l=at(*l['ankle']), ank_r=at(*r['ankle']))


RADIUS = dict(head=4.0, neck=2.5, sh_l=2.5, sh_r=2.5, spine=4.0, hip_l=3.0, hip_r=3.0, knee_l=2.0, knee_r=2.0, ank_l=1.5, ank_r=1.5)
BONES = [('head', 'neck'), ('neck', 'sh_l'), ('neck', 'sh_r'), ('sh_l', 'sh_r'), ('sh_l', 'spine'), ('sh_r', 'spine'), ('neck', 'spine'),
         ('spine', 'hip_l'), ('spine', 'hip_r'), ('hip_l', 'hip_r'),
         ('hip_l', 'knee_l'), ('knee_l', 'ank_l'), ('hip_r', 'knee_r'), ('knee_r', 'ank_r')]
SOFT = [('head', 'sh_l'), ('head', 'sh_r'), ('sh_l', 'hip_l'), ('sh_r', 'hip_r'), ('sh_l', 'hip_r'), ('sh_r', 'hip_l')]
MUSCLES = dict(legs=[('hip_l', 'ank_l'), ('hip_r', 'ank_r'), ('spine', 'knee_l'), ('spine', 'knee_r'), ('knee_l', 'knee_r'), ('ank_l', 'ank_r')],
               trunk=[('neck', 'hip_l'), ('neck', 'hip_r'), ('sh_l', 'hip_r'), ('sh_r', 'hip_l')],
               neck=[('head', 'spine')])


class Doll:
    floor = None    # floor(x, y): the floor's height under a point, the thing's own floor 0 (death_floors.py); None: flat
    # Floor friction. 'damped': a point on the floor loses 40% of its sideways speed each substep,
    # however lightly it rests (a leg lying on a ledge held a whole body hanging off it). 'contact':
    # Coulomb friction as position-based dynamics does it (Mueller et al. 2007, Macklin et al. 2014):
    # bounded by how deep the floor had to push the point back up, so a hard landing stops it dead
    # and a leg resting under its own weight slides when the body hanging off it pulls.
    # Flat falls barely change ('damped' -> 'contact': rest 26 -> 28 tics after landing, curl and face
    # down alike over 120 falls); over a ledge only 'contact' lets the hanging weight drag a body off.
    friction = 'contact'
    STICK, SLIDE = 0.6, 0.5     # contact friction: static and sliding, per unit pushed back up (cloth on stone; 1.0 held bodies hanging)
    SLICK_EDGES = True          # no friction where a rounded step edge pushes (it gripped a body hanging off a ledge)

    def __init__(self, rest):
        self.rest = rest
        self.p = dict(rest)
        self.prev = dict(rest)
        self.len = {pair: math.dist(rest[pair[0]], rest[pair[1]]) for pair in BONES + SOFT + sum(MUSCLES.values(), [])}
        self.len_nk = {s: math.dist(rest['neck'], rest['knee_' + s]) for s in ('l', 'r')}

    def ground(self, q):
        return self.floor(q[0], q[1]) if self.floor else 0.0

    def up(self, n):
        """How high a point stands over the floor under it."""
        return self.p[n][2] - self.ground(self.p[n])

    def push(self, name, v):
        """Add a velocity (units per tic)."""
        self.prev[name] = sub(self.prev[name], scale(v, 1.0 / SUBSTEPS))

    def front(self):
        """The front of the body (rest: toward the player, -y)."""
        p = self.p
        return unit(cross(sub(p['sh_r'], p['sh_l']), sub(p['neck'], mid(p['hip_l'], p['hip_r']))))

    def link(self, a, b, length, k):
        pa, pb = self.p[a], self.p[b]
        d = sub(pb, pa)
        dist = math.sqrt(dot(d, d)) or 1e-6
        corr = scale(d, k * 0.5 * (dist - length) / dist)
        self.p[a] = add(pa, corr)
        self.p[b] = sub(pb, corr)

    def tic(self, muscle, shape=None, k=0.0):
        """One tic. shape, k: a key pose the body's shape is pulled toward, k of the way a tic
        (shape_match; guided deaths only)."""
        dt = 1.0 / SUBSTEPS
        k_sub = 1 - (1 - min(k, 0.99)) ** (1.0 / SUBSTEPS)
        for _ in range(SUBSTEPS):
            for n in self.p:
                cur, old = self.p[n], self.prev[n]
                v = sub(cur, old)
                if self.friction == 'damped' and cur[2] <= RADIUS[n] + self.ground(cur) + 0.05:      # on the floor: friction
                    v = (v[0] * 0.6, v[1] * 0.6, v[2])
                v = scale(v, 0.9 if self.lying() else 0.985)   # down: it stops soon
                self.prev[n] = cur
                self.p[n] = add(add(cur, v), (0, 0, -GRAVITY * dt * dt))
            if shape is not None and k_sub > 0:
                shape_match(self, shape, k_sub)
            self.pressed = {}
            for _ in range(6):
                for pair in BONES:
                    self.link(*pair, self.len[pair], 1.0)
                for pair in SOFT:
                    self.link(*pair, self.len[pair], 0.15)
                for group, pairs in MUSCLES.items():
                    k = muscle.get(group, 0.0)
                    if k > 0:
                        for pair in pairs:
                            self.link(*pair, self.len[pair], k)
                self.limits()
                for n in self.p:
                    self.hold(n)
            if self.friction == 'contact':
                self.rub()

    def hold(self, n):
        """The floor: bodies land dead, no bounce. Over a height map each point is a ball against
        the map's cells (CELL wide, solid below their floor): a top pushes it up, a step's face
        sideways, an edge out along the line from the edge, so a body hanging over a drop hangs
        clear of the face and its weight drags the rest over (by centres alone it lay in the face
        and hung by its knees). A point that came at a step's face from the side, below the top,
        stops there first (a ball's push would lift it onto the step)."""
        x, y, z = self.p[n]
        px, py, pz = self.prev[n]
        r = RADIUS[n]
        if not self.floor:
            if z >= r:
                return
            depth, z = r - z, r
        else:
            g = self.ground(self.p[n])
            if z < g and g - self.ground(self.prev[n]) > r and pz < g + r:
                x, y = px, py
            depth = 0.0
            for _ in range(3):                  # the deepest cell each round
                best, push = 0.0, None
                for i in range(math.floor((x - r) / CELL), math.floor((x + r) / CELL) + 1):
                    for j in range(math.floor((y - r) / CELL), math.floor((y + r) / CELL) + 1):
                        top = self.floor((i + 0.5) * CELL, (j + 0.5) * CELL)
                        if z - r >= top:
                            continue
                        # Edges over a drop are rounded (EDGE): the cell's box shrunk on those sides, met by a
                        # ball EDGE bigger. A limb lying over the edge then sits on a slope that sheds it, as a
                        # thigh would rest on the corner (bones don't collide, only points).
                        lower = lambda di, dj: self.floor((i + di + 0.5) * CELL, (j + dj + 0.5) * CELL) < top - 0.5
                        cut = (EDGE if lower(-1, 0) else 0, EDGE if lower(1, 0) else 0, EDGE if lower(0, -1) else 0, EDGE if lower(0, 1) else 0)
                        e = EDGE if any(cut) else 0.0
                        x0, x1, y0, y1 = i * CELL + cut[0], (i + 1) * CELL - cut[1], j * CELL + cut[2], (j + 1) * CELL - cut[3]
                        q = (min(max(x, x0), x1), min(max(y, y0), y1), min(z, top - e))
                        d = (x - q[0], y - q[1], z - q[2])
                        dist = math.sqrt(dot(d, d))
                        if dist < 1e-6:
                            way, amount = (0.0, 0.0, 1.0), top + r - z     # inside the cell: up onto it
                        elif dist < r + e:
                            way, amount = scale(d, 1 / dist), r + e - dist
                        else:
                            continue
                        if amount > best:
                            best, push = amount, way
                if push is None:
                    break
                x, y, z = x + push[0] * best, y + push[1] * best, z + push[2] * best
                if push[2] > 0.999 or not self.SLICK_EDGES:
                    depth += push[2] * best         # friction from a floor's top; an edge is slick
            if depth <= 0 and (x, y, z) == self.p[n]:
                return
        if depth > 0:
            self.pressed[n] = self.pressed.get(n, 0.0) + depth
        self.p[n] = (x, y, z)
        if depth > 0:
            self.prev[n] = (px, py, max(pz, z))

    def rub(self):
        """Contact friction, once a substep (Mueller et al. 2007): a point the floor pushed back up
        by depth in all keeps none of its sideways motion this substep if that's under STICK*depth,
        else loses SLIDE*depth of it."""
        for n, depth in self.pressed.items():
            x, y, z = self.p[n]
            px, py, _ = self.prev[n]
            dx, dy = x - px, y - py
            slide = math.hypot(dx, dy)
            if slide <= self.STICK * depth:
                self.p[n] = (px, py, z)
            elif slide > 0:
                keep = 1 - self.SLIDE * depth / slide
                self.p[n] = (px + dx * keep, py + dy * keep, z)

    def pelvis_front(self):
        """The front of the hips (the knees' forward; the shoulders' front flips when the trunk bends)."""
        p = self.p
        hips = mid(p['hip_l'], p['hip_r'])
        return unit(cross(sub(p['hip_r'], p['hip_l']), sub(p['spine'], hips)))

    def apart(self, a, b, least, k=1.0):
        """Push two points apart to at least a length (a one-sided link)."""
        if math.dist(self.p[a], self.p[b]) < least:
            self.link(a, b, least, k)

    def limits(self):
        """Joint limits: the spine bends at most about 45 degrees at the chest joint (neck kept
        90% of its straight distance from the hips), a thigh swings no further back than 30
        degrees behind the hips, and knees bend as below."""
        for s in ('l', 'r'):
            self.apart('neck', 'hip_' + s, 0.9 * self.len[('neck', 'hip_' + s)], 0.5)
        n = self.pelvis_front()
        up = unit(sub(self.p['spine'], mid(self.p['hip_l'], self.p['hip_r'])))
        for s in ('l', 'r'):
            hip = self.p['hip_' + s]
            d = unit(sub(self.p['knee_' + s], hip))
            want = d
            if dot(want, n) < -0.5:                 # swung back past 30 degrees
                want = unit(add(want, scale(n, -0.5 - dot(want, n))))
            if dot(want, up) > 0.34:                # flexed past 110 degrees toward the chest
                want = unit(sub(want, scale(up, dot(want, up) - 0.34)))
            if want != d:
                self.swing(s, d, want)
        self.knees()

    def swing(self, s, d, want, k=0.5):
        """Turn a whole leg (knee and ankle, and where they were) about its hip, part of the way
        from direction d to want: a limit that keeps the bones' lengths and adds no speed."""
        axis = cross(d, want)
        size = math.sqrt(dot(axis, axis))
        if size < 1e-6:
            return
        angle = k * math.atan2(size, dot(d, want))
        axis = scale(axis, 1 / size)
        hip = self.p['hip_' + s]
        for name in ('knee_' + s, 'ank_' + s):
            self.p[name] = add(hip, turn(sub(self.p[name], hip), axis, angle))
            self.prev[name] = add(hip, turn(sub(self.prev[name], hip), axis, angle))

    def knees(self):
        """A knee is a hinge about the hips' side axis: the shin may only swing back from the
        thigh's line (cross(thigh, shin) along the side axis); a knee bent the wrong way is
        mirrored across the hip-ankle line, which keeps both bones' lengths (easing it toward
        straight broke them, and the bones and the hinge then fought and threw the body), its
        last position mirrored too so the flip adds no speed. Legs never fold tighter than 40%
        of their length."""
        side = unit(sub(self.p['hip_r'], self.p['hip_l']))
        for s in ('l', 'r'):
            hip, knee, ank = self.p['hip_' + s], self.p['knee_' + s], self.p['ank_' + s]
            bend = dot(cross(unit(sub(knee, hip)), unit(sub(ank, knee))), side)
            if bend < -0.15:                        # bent the wrong way by over ~9 degrees (less doesn't show;
                # a straight leg on the floor flipped every substep and twitched)
                axis = unit(sub(ank, hip))
                mirror = lambda q: add(hip, sub(scale(axis, 2 * dot(sub(q, hip), axis)), sub(q, hip)))
                self.p['knee_' + s] = mirror(knee)
                self.prev['knee_' + s] = mirror(self.prev['knee_' + s])
                self.flips = getattr(self, 'flips', 0) + 1
            full = self.len[('hip_' + s, 'knee_' + s)] + self.len[('knee_' + s, 'ank_' + s)]
            if math.dist(hip, ank) < 0.55 * full:
                self.link('hip_' + s, 'ank_' + s, 0.55 * full, 0.5)
            if self.lying() and math.dist(hip, ank) < 0.85 * full:
                self.link('hip_' + s, 'ank_' + s, 0.85 * full, 0.03)   # lying, a leg slackens out
            # Lying, the hips ease open too, the thigh out toward the trunk's line (neck to knee at 95% of
            # standing): a body sat down by legs swept or a side hit, then rolled drawn side up, lay curled
            # with its knees to its chest (7 and 6 in 12 of those falls; "a stomach ache").
            if self.lying() and math.dist(self.p['neck'], self.p['knee_' + s]) < 0.95 * self.len_nk[s]:
                self.link('neck', 'knee_' + s, 0.95 * self.len_nk[s], 0.03)

    def lying(self):
        """Down with its torso, not just touched down: the damping that stops a body soon, and the
        legs slackening out, wait for this. Keyed to touching down (the head too), an upside-down
        body freezes standing on its head against a wall."""
        return getattr(self, 'landed', False) and (self.up('spine') < 8 or self.up('neck') < 7)

    def low(self):
        return self.up('spine') < 10 and self.up('neck') < 12

    def steer(self):
        """While the body falls, twist it about its own length so it lands with its drawn side up
        or toward the player (whichever is the shorter turn): the twist needed, spread over the
        time left before the chest reaches the floor, eased in a little each tic. Nothing turns a
        body once it lies; a corpse rolling over by itself would look wrong."""
        if self.up('spine') < 6 or self.up('neck') < 5 or self.up('head') < RADIUS['head'] + 0.5:
            self.landed = True                      # down: nothing steers it again
        if getattr(self, 'landed', False):
            return
        hips = mid(self.p['hip_l'], self.p['hip_r'])
        axis = unit(sub(self.p['neck'], hips))
        if self.up('spine') >= 7 and axis[2] > math.cos(math.radians(60)):
            # Still tipping: tip the whole body back about its feet, away from its own front, so
            # it goes down on its back (the drawn side up) the way a shot body sits back, not
            # forward on its face. Pushing the trunk alone folded it backward over the hips.
            # Harder the more it leans forward over its front (a kneeling body about to fold onto
            # its face), barely when it already goes back.
            f = self.pelvis_front()
            back = unit((-f[0], -f[1], 0.0))
            lean = sub(self.p['neck'], hips)
            forward = max(0.0, -dot(unit((lean[0], lean[1], 0.0)), back)) if abs(lean[0]) + abs(lean[1]) > 1e-3 else 0.0
            feet = min(self.p['ank_l'][2], self.p['ank_r'][2])
            for k in self.p:
                self.push(k, scale(back, (0.05 + 0.3 * forward) * max(0.0, self.p[k][2] - feet) / 40))
        if self.up('spine') < 14 or axis[2] > math.cos(math.radians(25)):
            return                                  # upright still, or nearly down: no twist
        n = self.front()
        flat = lambda v: unit(sub(v, scale(axis, dot(v, axis))))
        np_ = flat(n)
        best = None
        for want in ((0, 0, 1), (0, -1, 0)):
            w = flat(want)
            angle = math.atan2(dot(axis, cross(np_, w)), dot(np_, w))
            if best is None or abs(angle) < abs(best):
                best = angle
        if abs(best) > 2.6 and getattr(self, 'way', None):      # nearly opposite: keep the way chosen
            best = abs(best) * self.way
        self.way = 1 if best >= 0 else -1
        left = math.sqrt(2 * max(1.0, self.up('spine') - 4) / GRAVITY)
        want_spin = max(-0.15, min(0.15, best / max(3.0, left)))
        spin = getattr(self, 'spin', 0.0)
        delta = (want_spin - spin) * 0.5
        self.spin = spin + delta
        for k in self.p:
            self.push(k, scale(cross(axis, sub(self.p[k], hips)), delta))


# -- Falls: the hit (where, which way, how hard) and the muscle schedule.
def fade(t, hold, over):
    return max(0.0, min(1.0, 1 - (t - hold) / over)) if t > hold else 1.0


FALLS = [
    ('Chest blast (shotgun, close): launched back and up, shoulders first, legs locked a moment',
     dict(hit=('spine', 'sh_l', 'sh_r', 'neck'), push=(0.0, 2.2, 0.4), legs=(9, 6), trunk=(5, 10), neck=(2, 6), launch=(2.5, 2.0, 0.08))),
    ('Legs swept (pistol, low): knees give first, the torso follows',
     dict(hit=('hip_l', 'knee_l', 'spine'), push=(1.1, 0.5, 0.0), legs=(0, 5), trunk=(10, 10), neck=(6, 10))),
    ('Hit from the side (chaingun, high left): spun and dropped',
     dict(hit=('sh_l', 'neck', 'spine'), push=(1.6, 0.7, 0.0), legs=(4, 8), trunk=(5, 10), neck=(0, 4))),
    ('Head shot: strings cut',
     dict(hit=('head',), push=(0.0, 1.2, 0.0), legs=(0, 2), trunk=(0, 2), neck=(0, 2))),
    ('Toward the player (shot from behind): pitches forward',
     dict(hit=('spine', 'sh_l', 'sh_r'), push=(0.0, -1.8, 0.3), legs=(7, 6), trunk=(5, 10), neck=(2, 6))),
]


def run(rest, fall, tics=60, seed=1, stats=None, every=STEP, drift=True, floor=None):
    """The fall, a pose every `every` tics. The seed varies the push a little, which knee goes
    first and a small kick to each knee, so no two bodies fold alike. drift=False takes the
    pushes' net drift out, as the game does (its own momentum carries the thing). floor: as Doll.floor."""
    rng = random.Random(seed)
    doll = Doll(rest)
    doll.floor = floor
    side = rng.uniform(-0.4, 0.4)                   # a little across, either way
    for n in fall['hit']:
        jitter = tuple(v * (0.8 + 0.4 * rng.random()) for v in fall['push'])
        doll.push(n, add(jitter, (side, 0.0, 0.0)))
    yaw = rng.uniform(-0.1, 0.1)                  # turning a little as it goes (radians a tic)
    for n in doll.p:
        r = sub(doll.p[n], doll.p['spine'])
        doll.push(n, scale((-r[1], r[0], 0.0), yaw))
    front = doll.front()
    for s in ('l', 'r'):                            # knees: a little forward and out, unevenly
        out = -1 if s == 'l' else 1
        doll.push('knee_' + s, add(scale(front, 0.2 + 0.9 * rng.random()), (out * 0.4 * rng.random(), 0, 0)))
    if not drift:
        no_drift(doll)
    if 'launch' in fall:                            # after the drift is out: the launch is kept (as the game)
        launch_body(doll, *fall['launch'], side)
    late = rng.random() * 6                        # one leg lets go later than the other
    shift = {g: rng.uniform(-2, 2) for g in ('legs', 'trunk', 'neck')}   # and the hit's timing, a little
    frames = []
    for t in range(tics):
        muscle = {g: fade(t - shift[g], *fall[g]) for g in ('legs', 'trunk', 'neck')}
        muscle['legs'] = fade(t - late - shift['legs'], *fall['legs'])
        doll.tic(muscle)
        if t >= 2:
            doll.steer()
        if stats is not None:
            stats['peak'] = max(stats.get('peak', 0.0), sum(dot(sub(doll.p[k], doll.prev[k]), sub(doll.p[k], doll.prev[k])) for k in doll.p))
        if t % every == 0:
            frames.append(dict(doll.p))
    if stats is not None:
        doll.p = frames[-1] if frames else doll.p
        stats['front'] = doll.front()
        stats['travel'] = math.dist(frames[-1]['spine'][:2], rest['spine'][:2])
    return frames


# -- Skinning: each relief part follows a frame built from its doll points.
# The gun is a bone of its own: every carried texel follows it alone, never blended, so it stays
# hard however the body bends (the rig boxes a gun held above the hips too, for this). It is held
# in the chest's frame for now; let go, it would be the rifle dropping.
PARTS = dict(pelvis=('hips', 'spine', ('hip_l', 'hip_r')), chest=('spine', 'neck', ('sh_l', 'sh_r')),
             head=('neck', 'head', ('sh_l', 'sh_r')), thigh_l=('hip_l', 'knee_l', ('hip_l', 'hip_r')),
             shin_l=('knee_l', 'ank_l', ('hip_l', 'hip_r')), thigh_r=('hip_r', 'knee_r', ('hip_l', 'hip_r')),
             shin_r=('knee_r', 'ank_r', ('hip_l', 'hip_r')), gun=('spine', 'neck', ('sh_l', 'sh_r')))


def basis(points, origin, tip, side):
    get = lambda n: mid(points['hip_l'], points['hip_r']) if n == 'hips' else points[n]
    o = get(origin)
    u = unit(sub(get(tip), o))
    s = sub(points[side[1]], points[side[0]])
    s = unit(sub(s, scale(u, dot(s, u))))
    return o, (u, s, cross(u, s))


def transforms(rest, now):
    out = {}
    for part, (o, tip, side) in PARTS.items():
        o0, b0 = basis(rest, o, tip, side)
        o1, b1 = basis(now, o, tip, side)
        # R = B1 * B0^T: rest axis k goes to current axis k.
        m = tuple(tuple(sum(b1[k][i] * b0[k][j] for k in range(3)) for j in range(3)) for i in range(3))
        t = sub(o1, tuple(sum(m[i][j] * o0[j] for j in range(3)) for i in range(3)))
        out[part] = (m, t)
    return out


def skin(quads, bones, carried, xf):
    cache = {}

    def move(p, carry):
        key = (p, carry)
        if key not in cache:
            q = (0.0, 0.0, 0.0)
            for part, v in ({'gun': 1.0} if carry else weights(bones, p, carry)).items():
                m, t = xf[part]
                q = add(q, scale(add(tuple(sum(m[i][j] * p[j] for j in range(3)) for i in range(3)), t), v))
            cache[key] = q
        return cache[key]
    return [([move(p, texel in carried) for p in corners], corners, idx, dim) for corners, texel, idx, dim in quads]


def draw(view, quads, pal, aim):
    view.frame(aim)
    for q, rest, idx, dim in quads:
        n = normal(q)
        if dot(n, sub(view.cam, q[0])) <= 0:
            continue
        dim *= 0.4 + 0.6 * abs(normal(rest)[1])
        rgb = shade(pal, idx, n, dim)
        view.triangle(q[0], q[1], q[2], rgb)
        view.triangle(q[0], q[2], q[3], rgb)


def setup(lumps, pal, frame, back, depth):
    """A drawing's rig, skeleton, the doll's rest points and its relief quads (depth as --depth)."""
    if frame in RIGS:
        pic, joints = rig(lumps, pal, frame)
    else:                                           # no hand marks: the joint finder's
        pic = Picture(lumps[frame])
        joints, why = find(pic, pal)
        if joints is None:
            raise SystemExit(f'{frame}: no rig ({why})')
    bones = skeleton(joints, find_head(pic))
    rest = rest_points(pic, bones, joints)
    heights = tube_heights(pic)
    back_heights = None
    if depth in ('profile', 'side', 'box'):
        name, flip = lump(lumps, frame[:5] + '3')
        side = Picture(lumps[name])
        if flip:
            side.texels = [row[::-1] for row in side.texels]
            side.left = side.width - side.left
        if depth == 'profile':
            heights, back_heights = side_profile(pic, heights, side)
        else:
            heights = side_scaled(pic, heights, side)
        if depth == 'box':
            heights = boxier(pic, heights, 0.6)
    return pic, joints, bones, rest, mesh(pic, heights, 1.0, 1.0, Picture(lumps[back]), back_heights)


# -- Guided deaths: the doll on strings, aimed at the zombieman's shotgun death (POSSH-L).
# Film's recipe: a real body, a fake force. The ratchet (the stunt wire on a jerk vest) yanks the
# chest along the shot for a few tics, the one force from outside; its net drift is taken out as
# the game does. Everything else acts inside the body: the legs give way, and key strings pull the
# body's shape (never its place or heading) toward id's frames by shape matching (Mueller et al.
# 2005, "Meshless deformations based on shape matching"): each substep the key pose is fitted
# rigidly to the body and every point pulled part of the way to its fitted spot, which moves and
# turns nothing as a whole. The strings are loose while the ratchet pulls, take hold after it
# (Zordan & Hodgins 2002: drop stiffness at the impact, then track again) and let go as the chest
# nears the floor, so the landing is the doll's own. The muscles let go in the order the landing
# asks (Faloutsos et al. 2001: phases that start on conditions, not a clock).
#
# Knobs (GUIDED):
#   ratchet       units per tic added to the chest each ratchet tic (the head 60% of it)
#   ratchet_tics  how long the wire pulls
#   give          tics after the hit when the legs give way (fade over 2 tics); the knees kicked forward
#   key           how far a tic the key strings pull the shape (0: none)
#   flinch        the startle first (trunk and head forward, knees soft) for FLINCH_TICS, the hit after it
#   launch, lift  the whole body thrown back and up at the hit (units per tic), kept: without it the
#                 ratchet only turned the body about its hips and it sat down, "lights out", never flew
#   spin          and a back somersault about its middle (radians a tic): the chest leads, the feet
#                 sweep up, it flies laid back (without, it hopped back upright); 2.5/2/0.08: airborne
#                 ~20 tics, feet ~15 up, down on its back at tic 20 ~37 units back (the game's slide on top)
#   landing       butt: hips land, then the back, the head last (trunk held until the hips are down,
#                 the neck until the back is); flat: everything lets go at the give-way; topple: the
#                 legs stay locked until the chest is low (the plank fall, today's chest blast)
GUIDED = dict(ratchet=0.9, ratchet_tics=3, give=4, key=0.35, flinch=False, landing='butt', launch=2.5, lift=2.0, spin=0.08)
# Key shapes (degrees): the spine arched back at the chest joint, the head thrown back on the neck,
# the thighs swung forward of the hips, the knees bent; at tics after the hit, as POSSH-K show it.
KEYS = [(2, dict(arch=15, head=30, hip=0, knee=5)),      # H: the hit, head snapped back
        (7, dict(arch=35, head=55, hip=15, knee=25)),    # I: arched, the head behind the shoulders
        (12, dict(arch=25, head=50, hip=60, knee=80)),   # J: the knees buckle, it sits back
        (17, dict(arch=10, head=40, hip=75, knee=95))]   # K: on its back, knees up
FLINCH = dict(arch=-12, head=-25, hip=10, knee=20)      # the startle: trunk and head forward, knees soft
FLINCH_TICS = 2
KEY_FADE = 6            # tics the strings take to let go after the last key (hung over a ledge they held its jackknife)
KEY_SPRING = 0.05       # of each key pull, the share kept as speed (shape_match): 0.3 still rocked 10 degrees
CHEST = ('spine', 'neck', 'sh_l', 'sh_r')
ID_FRAMES = ('POSSH0', 'POSSI0', 'POSSJ0', 'POSSK0', 'POSSL0')   # each 5 tics; the game draws a pose
POSE_AT = (1, 6, 11, 16, 21)                                     # after these many tics, the same pace


def no_drift(doll):
    """Take the points' mean sideways velocity out: the game's momentum moves the thing."""
    v = [sub(doll.p[n], doll.prev[n]) for n in doll.p]
    mx, my = sum(a[0] for a in v) / len(v), sum(a[1] for a in v) / len(v)
    for n in doll.p:
        doll.prev[n] = add(doll.prev[n], (mx, my, 0.0))


def launch_body(doll, back, lift, spin, across=0.0):
    """The launch: the whole body thrown back (+y) and up (units per tic), kept (the game's slide comes
    on top), and a back somersault about its middle (spin, radians a tic): the chest leads, the feet
    sweep up, so it flies laid back rather than hopping back upright."""
    mid_y = sum(q[1] for q in doll.p.values()) / len(doll.p)
    mid_z = sum(q[2] for q in doll.p.values()) / len(doll.p)
    for n, q in doll.p.items():
        doll.push(n, (across * back, back + spin * (q[2] - mid_z), lift - spin * (q[1] - mid_y)))


def internal(doll, pushes):
    """Pushes (point: velocity) with their net sideways drift taken out of the whole body: they turn
    and fold it, they don't carry it."""
    mx = sum(v[0] for v in pushes.values()) / len(doll.p)
    my = sum(v[1] for v in pushes.values()) / len(doll.p)
    for n in doll.p:
        v = pushes.get(n, (0.0, 0.0, 0.0))
        doll.push(n, (v[0] - mx, v[1] - my, v[2]))


def rot_x(v, pivot, a):
    """v turned about the side axis through pivot; positive a turns up toward the front (-y)."""
    y, z = v[1] - pivot[1], v[2] - pivot[2]
    c, s = math.cos(a), math.sin(a)
    return (v[0], pivot[1] + y * c - z * s, pivot[2] + y * s + z * c)


def key_shape(rest, arch=0.0, head=0.0, hip=0.0, knee=0.0):
    """The rest skeleton bent by a key's angles (degrees; arch and head back, hip forward, knee
    back: the hinge's way)."""
    r = math.radians
    p = dict(rest)
    for n in ('neck', 'sh_l', 'sh_r', 'head'):
        p[n] = rot_x(p[n], rest['spine'], -r(arch))
    p['head'] = rot_x(p['head'], p['neck'], -r(head))
    for s in ('l', 'r'):
        for n in ('knee_' + s, 'ank_' + s):
            p[n] = rot_x(p[n], rest['hip_' + s], -r(hip))
        p['ank_' + s] = rot_x(p['ank_' + s], p['knee_' + s], r(knee))
    return p


def key_angles(u):
    """The key angles u tics after the hit: eased from rest through KEYS, held at the last."""
    keys = [(0, dict(arch=0, head=0, hip=0, knee=0))] + KEYS
    for (t0, a), (t1, b) in zip(keys, keys[1:]):
        if u < t1:
            f = max(0.0, (u - t0) / (t1 - t0))
            f = f * f * (3 - 2 * f)
            return {k: a[k] + (b[k] - a[k]) * f for k in a}
    return keys[-1][1]


def rotation_of(m):
    """The rotation nearest a 3x3 matrix (its polar part: averaged with its inverse transpose
    until it stops changing); None when it is degenerate or a reflection."""
    for _ in range(40):
        r0, r1, r2 = m
        cof = (cross(r1, r2), cross(r2, r0), cross(r0, r1))
        det = dot(r0, cof[0])
        if det <= 1e-9:
            return None
        n = tuple(tuple(0.5 * (m[i][j] + cof[i][j] / det) for j in range(3)) for i in range(3))
        if max(abs(n[i][j] - m[i][j]) for i in range(3) for j in range(3)) < 1e-7:
            return n
        m = n
    return m


def shape_match(doll, shape, k, spring=None):
    """Pull the body k of the way toward the shape fitted rigidly onto it: no net push, no net turn.
    Only `spring` of each pull is kept as speed, the rest moves the point outright: kept whole, the
    strings rang (the chest thrown back to 59 degrees, sprung to 47, back again: the gun rocking)."""
    spring = KEY_SPRING if spring is None else spring
    names = list(doll.p)
    c = scale(tuple(sum(doll.p[n][i] for n in names) for i in range(3)), 1.0 / len(names))
    q0 = scale(tuple(sum(shape[n][i] for n in names) for i in range(3)), 1.0 / len(names))
    a = [[0.0] * 3 for _ in range(3)]
    for n in names:
        p, q = sub(doll.p[n], c), sub(shape[n], q0)
        for i in range(3):
            for j in range(3):
                a[i][j] += p[i] * q[j]
    r = rotation_of(a)
    if r is None:
        return
    for n in names:
        q = sub(shape[n], q0)
        goal = add(c, tuple(dot(r[i], q) for i in range(3)))
        pull = scale(sub(goal, doll.p[n]), k)
        if doll.up(n) <= RADIUS[n] + 0.05:         # on the floor it holds: moved outright, a planted foot skated
            pull = (0.0, 0.0, pull[2])
        doll.p[n] = add(doll.p[n], pull)
        doll.prev[n] = add(doll.prev[n], scale(pull, 1 - spring))


def touchdowns(frames, floor=None):
    """The first tic the hips, the chest and the head each touched the floor (floor: as Doll.floor)."""
    out = {}
    for t, f in enumerate(frames):
        h = lambda n: f[n][2] - (floor(f[n][0], f[n][1]) if floor else 0.0)
        for name, low in (('hips', min(h('hip_l'), h('hip_r')) < RADIUS['hip_l'] + 1),
                          ('chest', h('spine') < RADIUS['spine'] + 1.5), ('head', h('head') < RADIUS['head'] + 1)):
            if low and name not in out:
                out[name] = t
    return out


def run_guided(rest, g, tics=40, seed=1, floor=None):
    """A guided fall, a pose every tic. The seed varies what it varies unguided: the push a
    little across, a small turn, which leg goes first and the kick to each knee. floor: as Doll.floor."""
    rng = random.Random(seed)
    doll = Doll(rest)
    doll.floor = floor
    r0 = FLINCH_TICS if g['flinch'] else 0
    give = r0 + g['give']
    across = rng.uniform(-0.25, 0.25)
    yaw = rng.uniform(-0.06, 0.06)
    late = rng.random() * 2
    kicks = [0.2 + 0.6 * rng.random() for _ in range(2)]
    ev = {}
    after = lambda e, over: 1.0 if e not in ev else max(0.0, 1 - (t - ev[e]) / over)
    frames = []
    for t in range(tics):
        if t == r0 and (g.get('launch', 0) or g.get('lift', 0)):
            launch_body(doll, g.get('launch', 0), g.get('lift', 0), g.get('spin', 0), across)
        if r0 <= t < r0 + g['ratchet_tics'] and g['ratchet'] > 0:
            # The wire: the chest yanked along the shot (away from the player, a little up); its own
            # drift taken out, so it turns the body and the launch alone carries it.
            internal(doll, {n: scale((across, 1.0, 0.15), g['ratchet'] * (0.6 if n == 'head' else 1.0)) for n in CHEST + ('head',)})
            if t == r0:
                for n in doll.p:
                    r = sub(doll.p[n], doll.p['spine'])
                    doll.push(n, scale((-r[1], r[0], 0.0), yaw))
        if t == give and g['landing'] != 'topple':
            front = doll.pelvis_front()                   # the knees go forward as the legs give
            internal(doll, {'knee_' + s: scale((front[0], front[1], 0.0), kick) for s, kick in zip(('l', 'r'), kicks)})
        if g['landing'] == 'topple':
            muscle = dict(legs=after('low', 3), trunk=after('low', 3), neck=0.5 * after('chest', 1))
        elif g['landing'] == 'flat':
            f = fade(t, give, 2)
            muscle = dict(legs=f, trunk=f, neck=0.5 * f)
        else:
            muscle = dict(legs=fade(t - late, give, 2), trunk=0.8 * after('hips', 3), neck=0.5 * after('chest', 1))
        if t < r0:
            shape, k = key_shape(rest, **FLINCH), 0.5
        else:
            u = t - r0
            ramp = 1.0 if u >= g['ratchet_tics'] + 2 else 0.0 if u < g['ratchet_tics'] else (u - g['ratchet_tics'] + 1) / 3
            height = max(0.0, min(1.0, (doll.up('spine') - 10) / 8))
            height *= max(0.0, min(1.0, 1 - (u - KEYS[-1][0]) / KEY_FADE))   # and after the last key, wherever it is
            shape, k = key_shape(rest, **key_angles(u)), g['key'] * ramp * height * (0.0 if getattr(doll, 'landed', False) else 1.0)
        doll.tic(muscle, shape, k)
        if t >= 2:
            doll.steer()
        for name in touchdowns([doll.p], floor):
            ev.setdefault(name, t)
        if doll.up('spine') < 20:
            ev.setdefault('low', t)
        if getattr(doll, 'landed', False):
            ev.setdefault('landed', t)
        frames.append(dict(doll.p))
    return frames, ev


class Ortho:
    """An orthographic picture like the game's ragdoll card: from orbit degrees round the
    front, tipped down pitch degrees, s pixels a unit, its lowest solid row stood on the floor
    line the way the game stands the card on the floor."""
    def __init__(self, orbit=0.0, pitch=0.0, s=3, half=40, tall=64):
        a, p = math.radians(orbit), math.radians(pitch)
        fh = (math.sin(a), math.cos(a), 0.0)
        self.fwd = (fh[0] * math.cos(p), fh[1] * math.cos(p), -math.sin(p))
        self.right = (fh[1], -fh[0], 0.0)
        self.up = cross(self.right, self.fwd)
        self.cam = scale(self.fwd, -1e6)
        self.s, self.half, self.tall = s, half, tall
        self.w, self.h = int(2 * half * s), int((tall + 4) * s)
        self.floor = tall * s                       # the first row below the floor

    def frame(self, aim=None):
        self.rgb = [[None] * self.w for _ in range(self.h)]
        self.depth = [[1e9] * self.w for _ in range(self.h)]

    def triangle(self, a, b, c, rgb):
        s = [((dot(p, self.right) + self.half) * self.s, (self.tall - dot(p, self.up)) * self.s, dot(p, self.fwd)) for p in (a, b, c)]
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
                d = w0 * s[0][2] + w1 * s[1][2] + w2 * s[2][2]
                if d < self.depth[j][i]:
                    self.depth[j][i] = d
                    self.rgb[j][i] = rgb

    def picture(self, pic, pal, mirror=False):
        """id's frame by its own offsets (feet on the floor), each texel an s-pixel square."""
        self.frame()
        for y in range(pic.height):
            for x in range(pic.width):
                idx = pic.texels[y][x]
                if idx < 0:
                    continue
                u = pic.left - x - 1 if mirror else x - pic.left
                i0, j0 = int((u + self.half) * self.s), int((self.tall - (pic.top - y)) * self.s)
                for j in range(max(0, j0), min(self.h, j0 + self.s)):
                    for i in range(max(0, i0), min(self.w, i0 + self.s)):
                        self.rgb[j][i] = tuple(pal[idx * 3:idx * 3 + 3])
        return self

    def stand(self):
        """Move the picture so its lowest solid row is the floor's top row."""
        rows = [j for j in range(self.h) if any(self.rgb[j])]
        if rows:
            shift = self.floor - 1 - rows[-1]
            blank = [None] * self.w
            self.rgb = [self.rgb[j - shift] if 0 <= j - shift < self.h else list(blank) for j in range(self.h)]
        return self

    def mask(self):
        return {(i, j) for j in range(self.h) for i in range(self.w) if self.rgb[j][i]}

    def png(self):
        rows = []
        for j in range(self.h):
            bg = (34, 34, 40) if j < self.floor else (58, 54, 48) if j == self.floor else (48, 44, 38)
            rows.append([v for p in self.rgb[j] for v in (p or bg)])
        return png(self.w, self.h, rows)


def overlap(a, b):
    return len(a & b) / max(1, len(a | b))


def tilt_of(points):
    """The game's card pitch for a pose: 0 standing, up to 30 degrees down lying, in 5-degree steps."""
    up = unit(sub(points['neck'], mid(points['hip_l'], points['hip_r'])))
    return 5 * round(6 * max(0.0, min(1.0, 1 - up[2])))


def pose_picture(quads, bones, carried, rest, points, pal, orbit=0.0, pitch=None, s=3):
    v = Ortho(orbit, tilt_of(points) if pitch is None else pitch, s)
    draw(v, skin(quads, bones, carried, transforms(rest, points)), pal, None)
    return v.stand()


def engine_poses(frames, ev):
    """The poses the game would draw, as (frame index, tics held): after the first tic, then
    every 5 tics (the zombieman's frames), and the tic it lands."""
    landed = ev.get('landed', -1)
    shown = [n for n in range(len(frames)) if n == 0 or n == landed]
    last = 0
    for n in range(len(frames)):
        if n - last >= 5 or n == landed:
            last = n
            if n not in shown:
                shown.append(n)
    shown.sort()
    return [(n, (shown[i + 1] if i + 1 < len(shown) else len(frames)) - n) for i, n in enumerate(shown)]


def guided_page(args, lumps, pal):
    pic, joints, bones, rest, quads = setup(lumps, pal, args.frame, args.back, args.depth)
    carried = joints['carried_texels']
    g = dict(ratchet=args.ratchet, ratchet_tics=args.ratchet_tics, give=args.give, key=args.key, flinch=args.flinch, landing=args.landing,
             launch=args.launch, lift=args.lift, spin=args.spin)
    knobs = lambda k: (f"ratchet {k['ratchet']:g} x {k['ratchet_tics']} tics, legs give at {k['give']}, key {k['key']:g}, "
                       f"flinch {'on' if k['flinch'] else 'off'}, landing {k['landing']}, launch {k['launch']:g}/{k['lift']:g} spin {k['spin']:g}")
    rows = [('Unguided: today\'s chest blast (drift taken out, as in game)', None), ('Guided: ' + knobs(g), g)]
    if args.variants:
        def vary(title, **change):
            k = {**g, **change}
            if k != g:
                rows.append((title, k))
        vary('No launch (lights out)', launch=0.0, lift=0.0, spin=0.0)
        vary('No ratchet', ratchet=0.0)
        vary(f"Ratchet strong ({g['ratchet'] * 1.8:g})", ratchet=round(g['ratchet'] * 1.8, 2))
        vary('Ratchet long (6 tics)', ratchet_tics=6)
        vary('Legs give at once (tic 0)', give=0)
        vary('Legs give late (tic 9)', give=9)
        vary('No key strings', key=0.0)
        vary(f"Key strings strong ({min(0.9, g['key'] * 2):g})", key=min(0.9, g['key'] * 2))
        vary('Flinch ' + ('off' if g['flinch'] else 'on'), flinch=not g['flinch'])
        for landing in ('butt', 'flat', 'topple'):
            vary(f'Landing: {landing}', landing=landing)
    ids = [Picture(lumps[name]) for name in ID_FRAMES]
    id_masks = [(Ortho().picture(p, pal).mask(), Ortho().picture(p, pal, True).mask()) for p in ids]
    uri = lambda v: 'data:image/png;base64,' + base64.b64encode(v.png()).decode()
    page = ['<!doctype html><meta charset="utf-8"><title>Guided deaths</title>',
            '<style>body{background:#16161a;color:#ddd;font:13px system-ui;margin:16px}figure{display:inline-block;margin:0 6px 10px 0;vertical-align:top}'
            'figcaption{font-size:11px;color:#999}img{image-rendering:pixelated;display:block}h2{font-size:14px;margin:22px 0 6px}'
            'p{max-width:900px;color:#aaa}.row{white-space:nowrap;overflow-x:auto}</style>',
            f'<h1>{html.escape(args.frame)}: guided deaths beside POSSH-L</h1>',
            f'<p>Knobs: {html.escape(knobs(g))}. Front pictures as the game draws the card (orthographic, tipped down as the body goes over, '
            'stood on the floor), at the tics the game would draw a pose (after 1, 6, 11, 16, 21 tics; id\'s frames change at 0, 5, 10, 15, 20). '
            'The number under each is the silhouette overlap with id\'s frame (either way round). Side pictures: walked 90 degrees round, '
            'to catch a puppet. Animations: every tic, and at the game\'s pace. Click an animation to restart it.</p>',
            '<div class="row">']
    for name, p in zip(ID_FRAMES, ids):
        page.append(f'<figure><img src="{uri(Ortho().picture(p, pal))}" width="240"><figcaption>{name}</figcaption></figure>')
    page.append('</div>')
    for title, k in rows:
        if k is None:
            frames = run(rest, FALLS[0][1], args.tics, 1, every=1, drift=False)
            ev = touchdowns(frames)
            for t, f in enumerate(frames):
                if f['spine'][2] < 6 or f['neck'][2] < 5:
                    ev.setdefault('landed', t)
        else:
            frames, ev = run_guided(rest, k, args.tics)
        ev = {**ev, **{n: t for n, t in touchdowns(frames).items() if n not in ev}}
        page.append(f'<h2>{html.escape(title)}</h2><div class="row">')
        scores = []
        for n, (mask, mirrored) in zip(POSE_AT, id_masks):
            v = pose_picture(quads, bones, carried, rest, frames[min(n, len(frames)) - 1], pal)
            m = v.mask()
            scores.append(max(overlap(m, mask), overlap(m, mirrored)))
            page.append(f'<figure><img src="{uri(v)}" width="240"><figcaption>after {n} tics: {scores[-1]:.2f}</figcaption></figure>')
        smooth = [pose_picture(quads, bones, carried, rest, f, pal, s=2) for f in frames]
        poses = engine_poses(frames, ev)
        page.append(f'<figure><img class="anim" data-frames="{" ".join(uri(smooth[n]) for n, _ in poses)}" '
                    f'data-holds="{" ".join(str(h) for _, h in poses)}" width="240"><figcaption>at the game\'s pace</figcaption></figure>')
        page.append(f'<figure><img class="anim" data-frames="{" ".join(uri(v) for v in smooth)}" width="240">'
                    '<figcaption>every tic</figcaption></figure></div><div class="row">')
        for n in POSE_AT:
            v = pose_picture(quads, bones, carried, rest, frames[min(n, len(frames)) - 1], pal, orbit=90, pitch=0, s=2)
            page.append(f'<figure><img src="{uri(v)}" width="160"><figcaption>side, after {n}</figcaption></figure>')
        if k is g:
            for seed in (2, 3, 4, 5):
                other, _ = run_guided(rest, g, args.tics, seed)
                v = pose_picture(quads, bones, carried, rest, other[10], pal, s=2)
                page.append(f'<figure><img src="{uri(v)}" width="160"><figcaption>body {seed}, after 11</figcaption></figure>')
        end = Doll(rest)
        end.p = frames[-1]
        order = ', '.join(f'{n} {t}' for n, t in sorted(((n, t) for n, t in ev.items() if n in ('hips', 'chest', 'head')), key=lambda x: x[1]))
        travel = math.dist(frames[-1]['spine'][:2], rest['spine'][:2])
        line = (f"overlap H-L {' '.join(f'{x:.2f}' for x in scores)} (mean {sum(scores) / len(scores):.2f}); down: {order or 'never'}; "
                f"lands tic {ev.get('landed', '-')}; travel {travel:.0f}; front {tuple(round(x, 2) for x in end.front())}")
        page.append(f'</div><p>{html.escape(line)}</p>')
        print(f'{title}: {line}', file=sys.stderr)
    page.append("""<script>
for (const img of document.querySelectorAll('img.anim')) {
  const frames = img.dataset.frames.split(' ');
  const holds = (img.dataset.holds || '').split(' ').filter(Boolean).map(Number);
  let i = 0;
  img.src = frames[0];
  const step = () => {
    img.src = frames[i];
    const hold = holds.length ? holds[i] : 1;
    i++;
    img.timer = setTimeout(i < frames.length ? step : play, i < frames.length ? hold * 1000 / 35 : 1200);
  };
  const play = () => { clearTimeout(img.timer); i = 0; step(); };
  img.onclick = play; play();
}
</script>""")
    open(args.out, 'w').write('\n'.join(page))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('wad')
    ap.add_argument('out')
    ap.add_argument('--frame', default='POSSG1')
    ap.add_argument('--back', default='POSSA5')
    ap.add_argument('--tics', type=int, default=None, help='tics simulated (default 72, guided 40)')
    ap.add_argument('--depth', choices=('profile', 'side', 'box', 'tube'), default='profile',
                    help='profile: front and back from the side drawing\'s outline; side: as deep as the side drawing shows, per band; box: that with flatter tops '
                         'and steeper sides; tube: as deep as wide')
    ap.add_argument('--check', type=int, default=0, help='only print each fall over this many seeds: peak energy, travel, landing')
    ap.add_argument('--guided', action='store_true', help='the guided deaths page (the knobs below) beside POSSH-L')
    ap.add_argument('--ratchet', type=float, default=GUIDED['ratchet'], help='chest yank, units per tic added each ratchet tic')
    ap.add_argument('--ratchet-tics', type=int, default=GUIDED['ratchet_tics'], help='how many tics the ratchet pulls')
    ap.add_argument('--give', type=int, default=GUIDED['give'], help='tics after the hit when the legs give way')
    ap.add_argument('--key', type=float, default=GUIDED['key'], help='key strings: how far a tic the shape is pulled (0: none)')
    ap.add_argument('--flinch', action=argparse.BooleanOptionalAction, default=GUIDED['flinch'], help='a startle before the hit')
    ap.add_argument('--landing', choices=('butt', 'flat', 'topple'), default=GUIDED['landing'], help='the order the muscles let go')
    ap.add_argument('--launch', type=float, default=GUIDED['launch'], help='the body thrown back at the hit, units per tic')
    ap.add_argument('--lift', type=float, default=GUIDED['lift'], help='and up, units per tic')
    ap.add_argument('--spin', type=float, default=GUIDED['spin'], help='and a back somersault, radians per tic')
    ap.add_argument('--variants', action=argparse.BooleanOptionalAction, default=True, help='also each knob changed alone')
    args = ap.parse_args()
    lumps = read_wad(args.wad)
    pal = lumps['PLAYPAL'][:768]
    args.tics = args.tics or (40 if args.guided else 72)
    if args.guided:
        guided_page(args, lumps, pal)
        return
    if args.check:
        pic, joints, bones, rest, _ = setup(lumps, pal, args.frame, args.back, 'tube')
        for title, fall in FALLS:
            rows = []
            for seed in range(1, args.check + 1):
                st = {}
                run(rest, fall, args.tics, seed, st)
                up = st['front'][2] > 0.5 or st['front'][1] < -0.5
                rows.append(f"{st['peak']:5.1f}/{st['travel']:3.0f}{'' if up else '!'}")
            print(f'{title[:28]:28s} ' + ' '.join(rows))
        return
    pic, joints, bones, rest, quads = setup(lumps, pal, args.frame, args.back, args.depth)
    page = ['<!doctype html><meta charset="utf-8"><title>Death ragdoll</title>',
            '<style>body{background:#16161a;color:#ddd;font:13px system-ui;margin:16px}figure{display:inline-block;margin:0 6px 10px 0}'
            'figcaption{font-size:11px;color:#999}img{image-rendering:pixelated;display:block}h2{font-size:14px;margin:18px 0 6px}</style>',
            f'<h1>{html.escape(args.frame)}: ragdoll falls, posed every {STEP} tics</h1>']
    strips = []
    for title, fall in FALLS:
        frames = run(rest, fall, args.tics)
        end = frames[-1]
        pts = [p for f in frames for p in f.values()]
        aim = ((min(p[0] for p in pts) + max(p[0] for p in pts)) / 2, (min(p[1] for p in pts) + max(p[1] for p in pts)) / 2, 14.0)
        cells = []
        for f in frames:
            v = View(150, 0, 240, 160)
            draw(v, skin(quads, bones, joints['carried_texels'], transforms(rest, f)), pal, aim)
            cells.append(v)
        settled = []
        for orbit in (70, 180):
            v = View(150, orbit, 240, 160)
            draw(v, skin(quads, bones, joints['carried_texels'], transforms(rest, end)), pal, (end['spine'][0], end['spine'][1], 8.0))
            settled.append((orbit, v))
        n = Doll(rest)
        n.p = end
        face = n.front()
        print(f'{title}: lands front {tuple(round(x, 2) for x in face)}, travelled {math.dist(end["spine"][:2], rest["spine"][:2]):.0f} units',
              file=sys.stderr)
        uris = [base64.b64encode(c.png()).decode() for c in cells]
        page.append(f'<h2>{html.escape(title)}</h2>')
        page.append(f'<figure><img class="anim" data-frames="{" ".join(uris)}" src="data:image/png;base64,{uris[0]}" width="480">'
                    f'<figcaption>played every {STEP} tics (click to restart)</figcaption></figure>')
        for orbit, v in settled:
            page.append(f'<figure><img src="data:image/png;base64,{base64.b64encode(v.png()).decode()}" width="480">'
                        f'<figcaption>settled, walked {orbit} degrees round</figcaption></figure>')
        bodies = []
        for seed in (2, 3, 4, 5):
            last = run(rest, fall, args.tics, seed)[-1]
            v = View(150, 0, 240, 160)
            draw(v, skin(quads, bones, joints['carried_texels'], transforms(rest, last)), pal, (last['spine'][0], last['spine'][1], 8.0))
            bodies.append(v)
            page.append(f'<figure><img src="data:image/png;base64,{base64.b64encode(v.png()).decode()}" width="240">'
                        f'<figcaption>same hit, body {seed}</figcaption></figure>')
        strips.append([c for i, c in enumerate(cells) if i % 2 == 0][:8] + [v for _, v in settled] + bodies)
    page.append("""<script>
for (const img of document.querySelectorAll('img.anim')) {
  const frames = img.dataset.frames.split(' ').map(f => 'data:image/png;base64,' + f);
  let i = 0;
  const play = () => { i = 0; clearInterval(img.timer); img.timer = setInterval(() => {
    img.src = frames[i]; i++; if (i >= frames.length) { clearInterval(img.timer); setTimeout(play, 1200); } }, 1000 * 3 / 35); };
  img.onclick = play; play();
}
</script>""")
    open(args.out, 'w').write('\n'.join(page))
    cw, ch = strips[0][0].w, strips[0][0].h
    cols = max(len(s) for s in strips)
    sheet = [[(16, 16, 20)] * (cols * (cw + 4)) for _ in range(len(strips) * (ch + 4))]
    for i, strip in enumerate(strips):
        for k, v in enumerate(strip):
            for j in range(ch):
                sheet[i * (ch + 4) + j][k * (cw + 4):k * (cw + 4) + cw] = v.rgb[j]
    open(os.path.splitext(args.out)[0] + '.png', 'wb').write(
        png(len(sheet[0]), len(sheet), [[c for p in row for c in p] for row in sheet]))


if __name__ == '__main__':
    main()
