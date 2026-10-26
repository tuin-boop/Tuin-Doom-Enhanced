"""Ragdolls on Doom's floors: a height map instead of one flat floor.

Doom gives a thing the HIGHEST floor anywhere under its box (p_map.c: tmfloorz
takes every line's opening bottom), and a corpse rests at that height. A
zombie killed at a stair edge with its middle over the lower step is held up
at the top step: its body, drawn over the lower one, floats. The ragdoll so
far lay on one flat floor at that same height, so it floated too, sank into a
step up behind it, and lay in the air over a drop.

The height map: the floor's height under each doll point, sampled once at the
kill on a 4-unit grid around the body (here from a made-up scene, in game from
the sectors), relative to the thing's own floor. A point meeting a step's
riser from the side stops as at a wall; one coming from above lands on top.
The card the game draws the body into then stands on the body's lowest
surface, not the thing's floor.

A body over a ledge must go over when more of it hangs than lies on top. Four
things held it hanging (death_ragdoll.Doll):
- points met the floor by their centres, so a hanging body lay in the face
  and its thighs pulled the knees straight down: now each point is a ball
  against the map's cells, and edges over a drop are rounded (EDGE) as a
  thigh resting on the corner would be;
- floor friction took 40% of a point's sideways speed each substep however
  lightly it rested: now Coulomb friction from how deep the floor pushed
  back, once a substep, mu 0.6 (1.0 still held bodies), edges slick;
- the key strings faded only near the floor, which hung 32 units below:
  now they also let go KEY_FADE tics after the last key;
- touching down with the head damped the body still, standing on its head
  against the face: the damping now waits for the torso to be down.
Even so some go head first and end at the foot of the drop with their legs
up the face, held by it, as bodies do.
The step-up row's "air 12" is the grid: its cells put that step's face at 12,
the scene at 14.

Scenes vary only away from the viewer (y), the way the shot throws the body:
see SCENES. Each is shown with today's flat floor and with the height map, for
the guided shot death and today's chest blast: a cut through the scene from
the side, every 5 tics as the game draws poses, and the player's view of the
body at rest.

Usage: python3 -I tests/death_floors.py doom2.wad build/death-floors.html [--frame POSSG1 --back POSSA5]
"""
import argparse
import base64
import html
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import death_ragdoll as R  # noqa: E402
from death_lab import read_wad  # noqa: E402
from death_relief import View  # noqa: E402

CELL = R.CELL               # the height map's grid (units)
THING_RADIUS = 20.0         # the zombieman's: the box Doom takes the highest floor under
# Floor profiles away from the viewer: (from y, height), each holding until the next. The thing
# stands at y = 0; the shot throws the body toward +y.
SCENES = [
    ('Flat', 'The control: nothing changes.', [(-1e9, 0)]),
    ('On a stair edge', 'Killed with its middle over the lower step, its box still touching the top one (y < -8): '
     'Doom holds the corpse up at the top step, 16 units over the floor it lies on.', [(-1e9, 0), (-8, -16)]),
    ('Stairs down behind', 'Steps of 12 down, every 16 units behind it: the body falls down the stairs.',
     [(-1e9, 0), (8, -12), (24, -24), (40, -36)]),
    ('Step up behind', 'A 12-unit step 14 units behind: the body lands on it instead of sinking into it.',
     [(-1e9, 0), (14, 12)]),
    ('Ledge behind', 'A drop of 48, 12 units behind (a corpse never slides off a drop over 24 in the game; the doll may '
     'tip over it).', [(-1e9, 0), (12, -48)]),
]
FALLS = [('Guided shot death', 'guided'), ('Chest blast (today\'s)', 'chest')]
TICS = 60
SHOW = (1, 6, 11, 16, 21, 31, TICS)


def height(profile, y):
    h = profile[0][1]
    for start, value in profile:
        if y >= start:
            h = value
    return h


def doom_floor(profile):
    """The thing's floor as Doom finds it: the highest under its box."""
    return max(height(profile, y / 4) for y in range(int(-4 * THING_RADIUS), int(4 * THING_RADIUS) + 1))


def floor_map(profile, base):
    """The height map: the floor at each 4-unit cell's middle, the thing's floor 0."""
    return lambda x, y: height(profile, (math.floor(y / CELL) + 0.5) * CELL) - base


def bands(profile, base, lo, hi):
    """The floor as bands (y0, y1, height) between lo and hi, as the height map has it (whole cells)."""
    edges = sorted({lo, hi} | {math.ceil((s - CELL / 2) / CELL) * CELL for s, _ in profile if lo < s < hi})   # the first cell whose middle is past s
    return [(a, b, height(profile, (a + b) / 2) - base) for a, b in zip(edges, edges[1:])]


def draw_scene(view, profile, base, lo, hi, wide=160.0, deep=-80.0, cut=False):
    """Floor tops and the risers between them, as triangles into the view's depth buffer; cut: the
    ground in section instead, a solid profile behind the body (side on, tops and risers are edge-on)."""
    out = bands(profile, base, lo, hi)
    if cut:
        for i, (a, b, h) in enumerate(out):
            g = 70 if i % 2 else 62
            face = [(-40.0, a, deep), (-40.0, b, deep), (-40.0, b, h), (-40.0, a, h)]
            view.triangle(face[0], face[1], face[2], (g, g - 6, g - 14))
            view.triangle(face[0], face[2], face[3], (g, g - 6, g - 14))
        return
    for i, (a, b, h) in enumerate(out):
        g = 64 if i % 2 else 56
        top = [(-wide, a, h), (wide, a, h), (wide, b, h), (-wide, b, h)]
        view.triangle(top[0], top[1], top[2], (g, g - 4, g - 10))
        view.triangle(top[0], top[2], top[3], (g, g - 4, g - 10))
        if i:
            low = min(h, out[i - 1][2])
            high = max(h, out[i - 1][2])
            riser = [(-wide, a, low), (wide, a, low), (wide, a, high), (-wide, a, high)]
            view.triangle(riser[0], riser[1], riser[2], (40, 36, 30))
            view.triangle(riser[0], riser[2], riser[3], (40, 36, 30))
        front = [(-wide, a, deep), (wide, a, deep), (wide, a, h), (-wide, a, h)]
        if i == 0:
            view.triangle(front[0], front[1], front[2], (40, 36, 30))
            view.triangle(front[0], front[2], front[3], (40, 36, 30))


class Section(R.Ortho):
    """A cut through the scene from the side: away from the viewer to the right, s pixels a unit."""
    def __init__(self, s=2, y0=-40, y1=88, z0=-56, z1=60):
        super().__init__(orbit=-90, pitch=0, s=s, half=(y1 - y0) / 2, tall=z1)
        self.shift = (y0 + y1) / 2
        self.h = int((z1 - z0) * s)

    def triangle(self, a, b, c, rgb):
        move = lambda p: (p[0], p[1] - self.shift, p[2])
        super().triangle(move(a), move(b), move(c), rgb)

    def png(self):
        return R.png(self.w, self.h, [[v for p in row for v in (p or (30, 30, 36))] for row in self.rgb])


def fall(rest, kind, floor):
    if kind == 'guided':
        frames, _ = R.run_guided(rest, R.GUIDED, TICS, 1, floor)
    else:
        frames = R.run(rest, R.FALLS[0][1], TICS, 1, every=1, drift=False, floor=floor)
    return frames


def measures(frames, truth, floor):
    """Where the body ends against the real floor: the most air under a resting point (floating),
    the deepest any point sinks below it, the tic it came down, and where the game's card would
    stand: today on the thing's floor (0), with the height map on the body's lowest surface."""
    end = frames[-1]
    gaps = [end[n][2] - R.RADIUS[n] - truth(end[n][0], end[n][1]) for n in end]
    resting = [g for n, g in zip(end, gaps) if end[n][2] - R.RADIUS[n] - (floor(end[n][0], end[n][1]) if floor else 0) < 1]
    down = R.touchdowns(frames, floor)
    card = 0.0 if floor is None else min(end[n][2] - R.RADIUS[n] for n in end)
    under = truth(end['spine'][0], end['spine'][1])
    return dict(air=max(resting) if resting else 0.0, sink=max(0.0, -min(gaps)), down=down, card=card, under=under)


def main():
    global TICS, SHOW
    ap = argparse.ArgumentParser()
    ap.add_argument('wad')
    ap.add_argument('out')
    ap.add_argument('--frame', default='POSSG1')
    ap.add_argument('--back', default='POSSA5')
    ap.add_argument('--friction', choices=('damped', 'contact'), default=R.Doll.friction, help="the doll's floor friction (Doll.friction)")
    ap.add_argument('--tics', type=int, default=TICS, help='tics simulated (the last is shown as at rest)')
    args = ap.parse_args()
    R.Doll.friction = args.friction
    TICS = args.tics
    SHOW = (1, 6, 11, 16, 21, 31, TICS)
    lumps = read_wad(args.wad)
    pal = lumps['PLAYPAL'][:768]
    pic, joints, bones, rest, quads = R.setup(lumps, pal, args.frame, args.back, 'profile')
    carried = joints['carried_texels']
    uri = lambda v: 'data:image/png;base64,' + base64.b64encode(v.png()).decode()
    page = ['<!doctype html><meta charset="utf-8"><title>Ragdoll floors</title>',
            '<style>body{background:#16161a;color:#ddd;font:13px system-ui;margin:16px}figure{display:inline-block;margin:0 6px 10px 0;'
            'vertical-align:top}figcaption{font-size:11px;color:#999}img{image-rendering:pixelated;display:block}h2{font-size:15px;margin:26px 0 4px}'
            'h3{font-size:13px;margin:12px 0 4px;color:#bbb}p{max-width:900px;color:#aaa}.row{white-space:nowrap;overflow-x:auto}</style>',
            f'<h1>{html.escape(args.frame)}: ragdolls on Doom\'s floors</h1>',
            '<p>Today the doll lies on one flat floor at the thing\'s floor, which Doom takes as the highest floor anywhere under the '
            'thing\'s box. With the height map it lies on the floor under each of its points (4-unit cells, sampled once at the kill). '
            'Side cuts: the viewer is to the left, the shot throws the body right; poses at the tics the game draws them. Player\'s view: '
            'the body at rest from 110 units, eye 41 up. Numbers: air = the most floor gap under a point the doll thinks is resting, '
            'sink = deepest point below the real floor, card = where the game would stand the body\'s picture (today the thing\'s '
            'floor, 0) against the real floor under its middle.</p>']
    for title, note, profile in SCENES:
        base = doom_floor(profile)
        truth = lambda x, y, profile=profile, base=base: height(profile, y) - base
        page.append(f'<h2>{html.escape(title)}</h2><p>{html.escape(note)} Thing\'s floor (Doom\'s): {base:g}.</p>')
        for fall_title, kind in FALLS:
            for label, floor in (('today: flat floor', None), ('height map', floor_map(profile, base))):
                frames = fall(rest, kind, floor)
                m = measures(frames, truth, floor)
                page.append(f'<h3>{html.escape(fall_title)}, {label}</h3><div class="row">')
                for n in SHOW:
                    pose = frames[n - 1]
                    v = Section()
                    R.draw(v, R.skin(quads, bones, carried, R.transforms(rest, pose)), pal, None)
                    draw_scene(v, profile, base, -40, 88, cut=True)
                    page.append(f'<figure><img src="{uri(v)}" width="256"><figcaption>after {n} tics</figcaption></figure>')
                end = frames[-1]
                v = View(110, 0, 240, 160)
                R.draw(v, R.skin(quads, bones, carried, R.transforms(rest, end)), pal, (0.0, 20.0, 0.0))
                draw_scene(v, profile, base, -100, 400)
                page.append(f'<figure><img src="{uri(v)}" width="360"><figcaption>at rest, the player\'s view</figcaption></figure></div>')
                order = ', '.join(f'{k} {t}' for k, t in sorted(m['down'].items(), key=lambda kv: kv[1])) or 'never'
                line = (f"air {m['air']:.1f}, sink {m['sink']:.1f}; down: {order}; card at {m['card']:+.1f} "
                        f"over a floor at {m['under']:+.1f} under its middle")
                page.append(f'<p>{html.escape(line)}</p>')
                print(f'{title:20s} {fall_title:22s} {label:18s} {line}', file=sys.stderr)
    open(args.out, 'w').write('\n'.join(page))


if __name__ == '__main__':
    main()
