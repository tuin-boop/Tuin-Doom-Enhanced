"""Legs swept, simulated: the fall comes out of the hit instead of one authored curve.

The authored legs-swept fall (death_pose.failing/down, death_falls.cpp
legsPose) plays one motion for every kill. Here a small body model runs per
kill, in the drawing's plane, in units and tics, and hands the game the same
controls it already draws (each hip's drop, the pelvis's slide, the torso's
turn, the legs' swing about the pelvis, the sink):
- Knees: each hip is held up by its leg's muscle and pulled down by gravity,
  damped. The muscle lets go at a moment and a pace: the hit leg first (at
  once and fast for a low hit, a tic or so later for a body hit, faster the
  harder the hit), the other leg a few tics later and slower. The pelvis is
  rigid: the hips can't part by more than half its width.
- Pelvis: the push gives it a slide toward the failing side, braked while
  the legs still hold; it drifts toward the lower hip.
- Torso: an inverted pendulum on the pelvis. It balances while the legs
  hold (a pull back to upright that fades with them), is thrown toward the
  side the pelvis drops, and a hit above the hips first snaps the shoulders
  back the other way.
- Support gone (the pelvis half way down, both legs let go, or the torso
  tipped past its feet): the pelvis
  falls freely to the floor, the legs straighten and swing out about it, and
  the torso keeps going over under gravity.
Variety comes from the hit (force, height) and a per-kill seed (when the
other leg goes, how fast each lets go, how hard the body balances).
The game runs the same model (death_falls.cpp simulateLegs). The lying drawing takes over where the silhouettes overlap most, as before,
measured per kill.

Usage: python3 -I tests/death_sim.py doom2.wad build/death-sim.html [--monster POSS|SPOS|CPOS|TROO|SARG|BOSS]
"""
import base64
import html
import math
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from death_lab import Picture, png, read_wad  # noqa: E402
from death_lab import smooth  # noqa: E402
from death_pose import RIGS, MECHANICS, SUB, cells, down, drawn, floats, legs_column, pose, rig  # noqa: E402
from joint_finder import find, find_head  # noqa: E402

PAIN = dict(POSS='G1', SPOS='G1', CPOS='G1', TROO='H1', SARG='H1', BOSS='H1')
DEATH = dict(POSS='HIJKL', SPOS='HIJKL', CPOS='HIJKLMN', TROO='IJKLM', SARG='IJKLMN', BOSS='IJKLMNO')
GRAVITY = 0.5          # units per tic per tic: brisker than a real body of Doom's scale, like Doom's own


def muscle(t, start, pace):
    """How much a leg still holds: all of it until start, then letting go at the pace (tics)."""
    return 1.0 if t <= start else math.exp(-(t - start) / pace)


def simulate(joints, force, high, seed, side=-1, tics=48, head=False, chest=False):
    """The fall's controls per tic, from the hit (force 0.4-1, height -1 legs .. 1 shoulders), a seed, and
    the side it goes (-1 the drawing's left, +1 its right): the leg on that side gives. Worked out for
    the left and turned over for the right (legs swapped, sideways motion and turns reversed), the art
    itself unmirrored."""
    out = fall_left(joints, force, high, seed, tics, head, chest)
    if side < 0:
        return out
    return [dict(c, drops=c['drops'][::-1], shift=-c['shift'], torso=-c['torso'], legs=-c['legs'], head=-c['head'])
            for c in out]


def fall_left(joints, force, high, seed, tics, head=False, chest=False):
    """Legs swept, or with head set a shot through the head ("strings cut"): both knees go at
    once, nothing balances, the pelvis drops nearly in place, the head snaps toward the fall's
    side and the body follows it down."""
    rnd = random.Random(seed)
    leg = joints['left']['hip'][1] - joints['left']['ankle'][1]
    k = leg / 23                                   # the zombie's leg, hip to ankle
    width = joints['right']['hip'][0] - joints['left']['hip'][0]
    low, shoulders = high < -0.2, high > 0.3
    # The hit leg: kicked out at once by a low hit, let go a tic or two after a body or shoulder hit.
    hit_start = 0.0 if low else (0.5 if not shoulders else 1.5) + 1.5 * rnd.random()
    hit_pace = (0.8 if low else 2.0) * (1.4 - 0.5 * force) * (0.8 + 0.4 * rnd.random())
    # The other leg: from going with it to standing a while (the body then pivots over it).
    other_start = hit_start + 0.5 + 11 * rnd.random() ** 2
    other_pace = 2.0 + 5 * rnd.random()
    balance = 0.01 + 0.25 * rnd.random()           # limp .. fighting to stay upright, while the legs hold
    give = 0.08 + 0.12 * rnd.random()              # knee damping: folding .. sagging
    torso_len = 0.8 * leg
    if head:
        hit_start, hit_pace = 0.0, 0.7 + 0.5 * rnd.random()
        other_start, other_pace = 0.6 * rnd.random(), 0.8 + 0.7 * rnd.random()
        balance, give = 0.0, 0.10 + 0.08 * rnd.random()
    snap = 0.35 + 0.25 * rnd.random()              # the head's snap (radians), toward the fall's side
    if chest:
        # A blast to the chest, on the hit drawing: the knees lock a few tics while the shoulders
        # are kicked over, then give.
        hit_start, hit_pace = 2.5 + 2 * rnd.random(), 1.5 + rnd.random()
        other_start, other_pace = hit_start + 0.5 + 3 * rnd.random(), 2 + 2 * rnd.random()
        balance = 0.02 + 0.06 * rnd.random()
    drops, speeds = [0.0, 0.0], [(0.6 + 1.2 * force) * k if low else 0.0, 0.0]
    x, vx = 0.0, -(0.15 + 0.35 * force) * k        # slid toward the failing side (left, as rigged)
    if head:
        speeds = [(0.3 + 0.3 * rnd.random()) * k, (0.3 + 0.3 * rnd.random()) * k]   # the knees fold at once
        vx *= 0.3
    th = 0.0
    w = -0.10 * force * high if shoulders else (rnd.random() - 0.5) * 0.04   # shoulders snap back, or a reflex
    if head:
        w = 0.07 + 0.05 * rnd.random()             # nothing holds the torso: it goes over after the head
    if chest:
        w = (0.12 + 0.12 * force) * (0.8 + 0.4 * rnd.random())   # the shoulders kicked over
        vx = -(0.3 + 0.5 * force) * k
    neck = lambda t: snap * (min(1.0, t) * (0.6 + 0.4 * math.exp(-max(0.0, t - 1) / 2))) if head else 0.0
    gone, sink, vsink = None, 0.0, 0.0
    hip_h = joints['left']['hip'][1]
    out, h = [], 0.25
    for step in range(tics * 4 + 1):
        t = step * h
        if step % 4 == 0:
            if gone is None:
                out.append(dict(drops=list(drops), shift=x, torso=th, legs=0.0, sink=0.0, u=0.0, head=neck(t)))
            else:
                room = max(1.0, hip_h - sum(gone['drops']) / 2 - 6 * k)
                u = min(1.0, sink / room)
                out.append(dict(drops=[d * (1 - u) for d in gone['drops']], shift=x, torso=th, legs=1.3 * u, sink=sink, u=u,
                                head=neck(t)))
        held = [muscle(t, hit_start, hit_pace), muscle(t, other_start, other_pace)]
        m = sum(held) / 2
        if gone is None:
            for i in (0, 1):
                speeds[i] += (GRAVITY * (1 - held[i]) - give * speeds[i]) * h
                drops[i] = min(0.85 * leg, drops[i] + speeds[i] * h)
            spread = drops[0] - drops[1]
            if abs(spread) > 0.8 * width:
                # A rigid pelvis: a leg still holding stops the other hip (the body hangs on it);
                # one that has let go comes along.
                lo, hi_ = (0, 1) if spread > 0 else (1, 0)
                excess = abs(spread) - 0.8 * width
                if held[hi_] > 0.5:
                    drops[lo] -= excess
                    speeds[lo] = min(speeds[lo], speeds[hi_])
                else:
                    drops[hi_] += excess
                    speeds[hi_] = max(speeds[hi_], speeds[lo] * 0.7)
            vx += (-(0.12 + 0.3 * m) * vx - 0.01 * spread * k) * h
            x += vx * h
            alpha = (GRAVITY / torso_len) * math.sin(th) - balance * m * th - 0.2 * w + 0.03 * (speeds[0] - speeds[1]) / k
            w += alpha * h
            th += w * h
            # Support gone: the pelvis half down, both legs let go, or the torso's weight past the feet.
            if (drops[0] + drops[1]) / 2 > 0.45 * leg or max(held) < 0.12 or th > 0.9:
                gone = dict(t=t, drops=list(drops))
        else:
            vsink += GRAVITY * h
            room = max(1.0, hip_h - sum(gone['drops']) / 2 - 6 * k)
            sink = min(room, sink + vsink * h)
            x += vx * h
            vx *= 0.95
            w += ((GRAVITY / torso_len) * math.sin(max(0.25, th)) - 0.1 * w) * h
            th = min(1.45, th + w * h)
    return out


def posed(pic, joints, c):
    """The drawing posed by one tic's controls: as death_pose.down, from the sim's numbers; the head
    (above the neck joint, joints['neck'] = (height, middle)) turned about the neck by c['head']."""
    drops = dict(left=c['drops'][0], right=c['drops'][1])
    pts = pose(pic, joints, drops, 1, c['torso'], c['shift'])
    if c.get('head') and joints.get('neck'):
        nh, nu = joints['neck']
        l, r = joints['left']['hip'], joints['right']['hip']
        mid0 = ((l[0] + r[0]) / 2, l[1])
        mid1 = (mid0[0] + c['shift'], mid0[1] - sum(c['drops']) / 2)
        tilt = 0.25 * math.atan2(c['drops'][0] - c['drops'][1], r[0] - l[0]) + c['torso']
        cs, sn = math.cos(tilt), math.sin(tilt)
        pivot = (mid1[0] + (nu - mid0[0]) * cs - (nh - mid0[1]) * sn, mid1[1] + (nu - mid0[0]) * sn + (nh - mid0[1]) * cs)
        hc, hs = math.cos(c['head']), math.sin(c['head'])
        src = [(x - pic.left + su, pic.top - r_ - sv) for r_ in range(pic.height) for x in range(pic.width)
               if pic.texels[r_][x] >= 0 for su in SUB for sv in SUB]
        out = []
        for (sx, sy), (x, y, i, w) in zip(src, pts):
            wh = smooth((sy - (nh - 1)) / 2)
            hx = pivot[0] + (x - pivot[0]) * hc - (y - pivot[1]) * hs
            hy = pivot[1] + (x - pivot[0]) * hs + (y - pivot[1]) * hc
            out.append((x + (hx - x) * wh, y + (hy - y) * wh, i, w))
        pts = out
    if c['legs'] == 0 and c['sink'] == 0:
        low = min(y for _, y, _, _ in pts)
        return [(x, y - min(0.0, low), i, w) for x, y, i, w in pts]
    l, r = joints['left']['hip'], joints['right']['hip']
    pelvis = ((l[0] + r[0]) / 2 + c['shift'], l[1] - sum(c['drops']) / 2)
    cs, sn = math.cos(c['legs']), math.sin(c['legs'])
    out = []
    for x, y, i, w in pts:
        lx = pelvis[0] + (x - pelvis[0]) * cs - (y - pelvis[1]) * sn
        ly = pelvis[1] + (x - pelvis[0]) * sn + (y - pelvis[1]) * cs
        out.append((x + (lx - x) * (1 - w), y + (ly - y) * (1 - w) - c['sink'], i, w))
    low = min(y for _, y, _, _ in out)
    return [(x, y - min(0.0, low), i, w) for x, y, i, w in out]


def sim_column(pic, joints, lying, rest_frames, controls, side, tics=5):
    """A simulated fall handed to the lying drawing where they overlap most. The lying drawings lie to
    their right, so a fall to the left takes them mirrored."""
    flip = side < 0
    target = {dx: cells(drawn(lying, flip, dx)) for dx in range(-10, 11)}
    best = (0.0, len(controls) - 1, 0)
    frames = [posed(pic, joints, c) for c in controls]
    for t, c in enumerate(controls):
        if not 0.4 <= c['u'] <= 0.8:
            continue
        mine = cells(frames[t])
        for dx, theirs in target.items():
            iou = len(mine & theirs) / len(mine | theirs)
            if iou > best[0]:
                best = (iou, t, dx)
    iou, land, dx = best
    middle = lambda pts: sum(y for _, y, _, _ in pts) / len(pts)
    top = lambda pts: max(y for _, y, _, _ in pts)
    height = max(0.0, middle(frames[land]) - middle(drawn(lying, flip, dx)))
    speed = max(2.5, top(frames[max(0, land - 1)]) - top(frames[land]))
    drop = (-speed + math.sqrt(speed * speed + 2 * 1.0 * height))
    floor_at = land + drop

    def at(t):
        if t < land:
            c = controls[int(t)]
            return frames[int(t)], ('buckling' if c['u'] == 0 else 'support gone')
        if t < floor_at:
            into = t - land
            return drawn(lying, flip, dx, lift=max(0.0, height - speed * into - 0.5 * into * into)), 'lying drawing, still falling'
        if t < floor_at + tics:
            into = t - floor_at
            return drawn(lying, flip, dx, 0.12 * math.sin(math.pi * min(1.0, into / 4))), 'lands'
        k = min(len(rest_frames) - 1, int((t - floor_at - tics) // tics))
        return drawn(rest_frames[k], flip, dx), 'lying'
    return at, f'hand-off tic {land}, overlap {iou:.2f}'


def main():
    args = sys.argv[1:]
    monster = 'POSS'
    if '--monster' in args:
        k = args.index('--monster')
        monster = args[k + 1]
        del args[k:k + 2]
    if len(args) != 2 or monster not in PAIN:
        raise SystemExit(__doc__)
    lumps = read_wad(args[0])
    pal = lumps['PLAYPAL'][:768]
    name = monster + PAIN[monster]
    if name in RIGS:
        pic, joints = rig(lumps, pal, name)
    else:
        pic = Picture(lumps[name])
        joints, why = find(pic, pal)
        if not joints:
            raise SystemExit(f'{name}: no rig ({why})')
    frames = [Picture(lumps[monster + f + '0']) for f in DEATH[monster]]
    lying = next(i for i, p in enumerate(frames) if i and p.top - floats(p) < 0.6 * frames[0].top)
    hits = [('low, weak', 0.4, -0.6), ('low, hard', 1.0, -0.6), ('body, weak', 0.4, 0.0), ('head shot', 0.5, 1.0)]
    found = find_head(pic)
    if found:
        joints = dict(joints, neck=(found[0], found[1]))
    seeds = [(11, -1), (23, 1), (37, 1), (51, -1)]     # (seed, side): toward the weapon and away from it
    columns = [[None] * (1 + len(seeds)) for _ in hits]
    notes = []
    authored = legs_column(pic, joints, frames[lying], (frames[lying + 1:] or frames[lying:]), MECHANICS[0], 5)
    for row, (label, force, high) in enumerate(hits):
        columns[row][0] = authored[0]
        for col, (seed, side) in enumerate(seeds):
            at, note = sim_column(pic, joints, frames[lying], (frames[lying + 1:] or frames[lying:]),
                                  simulate(joints, force, high, seed, side, head=label == 'head shot'), side)
            columns[row][1 + col] = at
            notes.append(f'{label}, seed {seed}, {"left" if side < 0 else "right"}: {note}')
            print(notes[-1])
    size = max(p.width for p in frames + [pic]) + 30
    cell, ch, floor, scale = max(96, size), max(64, pic.top + 14), max(58, pic.top + 8), 2
    steps = 48
    images = []
    for t in range(steps):
        canvas = [[(24, 24, 28)] * (cell * len(columns[0])) for _ in range(ch * len(hits))]
        for row in range(len(hits)):
            for y in range(row * ch + floor, (row + 1) * ch):
                canvas[y] = [(52, 48, 44)] * (cell * len(columns[0]))
            for col, at in enumerate(columns[row]):
                pts, _ = at(t)
                for x, y, idx, w in sorted(pts, key=lambda p: p[3]):
                    cx, cy = col * cell + cell // 2 + int(math.floor(x)), row * ch + floor - int(math.ceil(y))
                    if col * cell <= cx < (col + 1) * cell and row * ch <= cy < (row + 1) * ch:
                        canvas[cy][cx] = tuple(pal[idx * 3:idx * 3 + 3])
        big = []
        for line in canvas:
            wide = []
            for px in line:
                wide.extend(list(px) * scale)
            big.extend([wide] * scale)
        images.append(base64.b64encode(png(len(canvas[0]) * scale, len(canvas) * scale, big)).decode())
    heads = ''.join(f'<div>{html.escape(n)}</div>' for n in ['Authored (every kill)'] + [f'Seed {s}, falls {"left" if d < 0 else "right"}' for s, d in seeds])
    rows = ''.join(f'<div style="height:{ch * scale}px;display:flex;align-items:center">{html.escape(h[0])}</div>' for h in hits)
    page = f"""<!doctype html><meta charset="utf-8"><title>Simulated falls</title>
<style>body{{background:#151517;color:#ddd;font:14px system-ui;margin:24px}}button{{margin:12px 8px 0 0;padding:6px 12px}}
.t{{display:flex;margin-left:120px}}.t div{{width:{cell * scale}px;text-align:center}}.r{{width:120px}}</style>
<h2>Legs swept: authored vs simulated ({monster})</h2>
<p>Rows: the hit (force, height). Columns: the authored fall, the same for every kill, then the simulated one for four seeds,
falling to the drawing's left or right (the art unmirrored).
Stepped every 3 tics as in the game; "Every tic" shows the sim's own frames.</p>
<div class="t">{heads}</div><div style="display:flex"><div class="r">{rows}</div><img id="f" style="image-rendering:pixelated"></div>
<div><button id="p">Pause</button><button id="s">Slow motion</button><button id="e">Every tic</button><span id="c"></span></div>
<p style="color:#999">{html.escape('; '.join(notes))}</p>
<script>const F={images!r};let i=0,run=true,slow=false,every=false;const img=document.getElementById('f'),c=document.getElementById('c');
function show(){{const k=every?i:Math.floor(i/3)*3;img.src='data:image/png;base64,'+F[k];c.textContent='tic '+k;}}
function tick(){{if(run){{i=(i+1)%F.length;show();}}setTimeout(tick,(slow?4:1)*1000/35+(i==F.length-1?1200:0));}}
document.getElementById('p').onclick=e=>{{run=!run;e.target.textContent=run?'Pause':'Play';}};
document.getElementById('s').onclick=e=>{{slow=!slow;e.target.textContent=slow?'Normal speed':'Slow motion';}};
document.getElementById('e').onclick=e=>{{every=!every;e.target.textContent=every?'Game stepping':'Every tic';}};
document.addEventListener('keydown',e=>{{if(e.key=='ArrowRight'){{run=false;i=(i+1)%F.length;show();}}if(e.key=='ArrowLeft'){{run=false;i=(i+F.length-1)%F.length;show();}}}});
show();tick();</script>"""
    open(args[1], 'w').write(page)
    print('wrote', args[1], len(images), 'frames')


if __name__ == '__main__':
    main()
