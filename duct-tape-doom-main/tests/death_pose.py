"""One still pose the death drawings never show: pelvis lowered, torso upright,
knees bent, feet planted. Can it be made convincingly from a monster's own
drawings when the joints are placed by hand? (A diagnostic: if it can't, the
deformation representation needs to change; if it can, the next questions
are animating into it and finding the joints automatically.)

The source drawing is cut into torso, two thighs, two shins and two feet at
hand-marked hips, knees and ankles. The feet stay where they are; each leg is
a pair of rigid bones that keep their lengths, the knee placed by two-bone IK
as the pelvis comes down; the torso rides down on the pelvis. Parts blend by
position over a couple of rows at the ankle, knee and hip so the body stays
joined, and texels are carried forward at sub-texel points, never blended.

Usage: python3 -I tests/death_pose.py doom2.wad build/death-pose.png
       python3 -I tests/death_pose.py doom2.wad build/death-buckle.html --animate   (the buckle, animated)
       python3 -I tests/death_pose.py doom2.wad build/death-fall.html --fall        (legs swept vs chest blast, to the floor)
Add --monster SPOS (shotgun guy) or CPOS (chaingunner); POSS (zombieman) by default.
The stills end with the pain and hit drawings, joints marked and carried texels tinted.
"""
import base64
import html
import math
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from death_lab import Picture, png, pulse, read_wad, smooth  # noqa: E402

# Hand-marked joints of each monster's pain drawing (G1, the legs-swept rig)
# and hit drawing (H0, the chest blast's recoil), front view, in units: right
# of the sprite's origin, height above its feet. Guns hang below the hip line:
# hand-marked boxes (u0, h0, u1, h1, colour) keep their texels with the torso,
# all of them or one colour's; a gun held above the hips is boxed too, for the
# ragdoll's gun bone. The same as platform/death_rigs_stock.h.
RIGS = {
    'POSSG1': dict(split=3.5, carried=[(-21, 21, -3, 29, 'all'), (-21, 29, 16, 39, 'neutral'), (16, 33, 21, 39, 'neutral')],
                   left=dict(hip=(-2.5, 27), knee=(-2.0, 15.5), ankle=(-1.5, 4)),
                   right=dict(hip=(9.0, 27), knee=(8.75, 15.5), ankle=(9.25, 4))),
    'POSSH0': dict(split=4.3, carried=[(-19, 12, 20, 31, 'grey'), (-20, 12, -6.5, 22, 'all')],
                   left=dict(hip=(-1.0, 26), knee=(-1.5, 14), ankle=(-2.0, 4)),
                   right=dict(hip=(9.5, 26), knee=(9.5, 14), ankle=(9.5, 3.5))),
    'SPOSG1': dict(split=4.0, carried=[(-13, 22, -7, 31, 'all')],
                   left=dict(hip=(-2.5, 27), knee=(-2.0, 15.5), ankle=(0.0, 4)),
                   right=dict(hip=(9.5, 27), knee=(9.5, 15.5), ankle=(10.0, 4))),
    'SPOSH0': dict(split=4.0, carried=[(-15, 16, -2.5, 27, 'wood'), (-3, 23.5, 6, 29, 'grey')],
                   left=dict(hip=(-2.5, 27), knee=(-2.0, 15.5), ankle=(0.0, 4)),
                   right=dict(hip=(9.5, 27), knee=(9.5, 15.5), ankle=(10.0, 4))),
    'CPOSG1': dict(split=-2.5, carried=[(-22, 13.75, -4.5, 28, 'neutral')],
                   left=dict(hip=(-10.0, 17.5), knee=(-13.0, 9.5), ankle=(-14.0, 2)),
                   right=dict(hip=(5.0, 17.5), knee=(6.5, 9.5), ankle=(8.5, 2))),
    'CPOSH0': dict(split=4.0, carried=[(-22, 12.75, 24, 30, 'all')],
                   left=dict(hip=(-3.0, 17.5), knee=(-1.5, 9.5), ankle=(0.5, 2)),
                   right=dict(hip=(9.0, 17.5), knee=(9.5, 9.5), ankle=(9.0, 2))),
}
SUB = [(i + 0.5) / 4 for i in range(4)]


def colour_rule(colour, rgb):
    hi, lo = max(rgb), min(rgb)
    return dict(all=True, grey=hi - lo < 24 and hi > 40, wood=rgb[0] >= rgb[1] >= rgb[2] + 5,
                neutral=hi - lo < 24)[colour]


def rig(lumps, pal, name):
    """A drawing's joints with the texels its boxes carry with the torso (x, row)."""
    pic = Picture(lumps[name])
    joints = dict(RIGS[name], sprite=name, carried_texels=set())
    for r in range(pic.height):
        for x in range(pic.width):
            idx = pic.texels[r][x]
            u, h = x - pic.left + 0.5, pic.top - r - 0.5
            if idx >= 0 and any(u0 <= u <= u1 and h0 <= h <= h1 and colour_rule(c, pal[idx * 3:idx * 3 + 3])
                                for u0, h0, u1, h1, c in joints['carried']):
                joints['carried_texels'].add((x, r))
    return pic, joints


def ik(hip, ankle, thigh, shin, outward):
    """Knee for a hip and ankle (units, y up), bones kept at their lengths; outward = -1 or +1 picks the side."""
    dx, dy = hip[0] - ankle[0], hip[1] - ankle[1]
    d = min(max(math.hypot(dx, dy), abs(thigh - shin) + 0.01), thigh + shin - 0.01)
    a = (shin * shin - thigh * thigh + d * d) / (2 * d)
    h = math.sqrt(max(0.0, shin * shin - a * a))
    mx, my = ankle[0] + a * dx / d, ankle[1] + a * dy / d
    knees = [(mx + k * h * -dy / d, my + k * h * dx / d) for k in (1, -1)]
    return min(knees, key=lambda k: -outward * k[0]), knees


def bone(p0, q0, p1, q1):
    """The rigid motion taking segment p0-q0 onto p1-q1 (equal lengths)."""
    turn = math.atan2(q1[1] - p1[1], q1[0] - p1[0]) - math.atan2(q0[1] - p0[1], q0[0] - p0[0])
    c, s = math.cos(turn), math.sin(turn)
    return lambda p: (p1[0] + (p[0] - p0[0]) * c - (p[1] - p0[1]) * s, p1[1] + (p[0] - p0[0]) * s + (p[1] - p0[1]) * c)


def pose(pic, joints, drops, knees_out, torso_turn=0.0, shift=0.0):
    """Points (x, y up, palette index) of the drawing with each hip lowered by drops[side] units,
    the pelvis moved sideways by shift and the torso turned by torso_turn (radians, + leans left)."""
    legs = {}
    for side, sign in (('left', -1), ('right', 1)):
        j = joints[side]
        hip0, knee0, ankle = j['hip'], j['knee'], j['ankle']
        hip1 = (hip0[0] + shift, hip0[1] - drops[side])
        knee1, both = ik(hip1, ankle, math.dist(hip0, knee0), math.dist(knee0, ankle), sign * knees_out)
        if drops[side] < 0.5:                   # a leg barely bent keeps its knee where it was drawn
            knee1 = min(both, key=lambda k: math.dist(k, knee0))
        legs[side] = dict(thigh=bone(hip0, knee0, hip1, knee1), shin=bone(knee0, ankle, knee1, ankle),
                          hip=hip0[1], knee=knee0[1], ankle=ankle[1])
    # The pelvis: midway between the lowered hips; the torso rides on it (and turns with any tilt).
    l, r = joints['left']['hip'], joints['right']['hip']
    mid0 = ((l[0] + r[0]) / 2, l[1])
    # The torso keeps itself mostly upright over a tilted pelvis (a quarter of the tilt shows,
    # toward the lower hip).
    tilt = 0.25 * math.atan2(drops['left'] - drops['right'], r[0] - l[0]) + torso_turn
    mid1 = (mid0[0] + shift, mid0[1] - (drops['left'] + drops['right']) / 2)
    c, s = math.cos(tilt), math.sin(tilt)
    torso = lambda p: (mid1[0] + (p[0] - mid0[0]) * c - (p[1] - mid0[1]) * s, mid1[1] + (p[0] - mid0[0]) * s + (p[1] - mid0[1]) * c)
    out = []
    for r_ in range(pic.height):
        for x in range(pic.width):
            idx = pic.texels[r_][x]
            if idx < 0:
                continue
            for su in SUB:
                for sv in SUB:
                    p = (x - pic.left + su, pic.top - r_ - sv)
                    leg = legs['left' if p[0] < joints['split'] else 'right']
                    # Feet -> shin -> thigh -> torso, each blended into the next over a few rows.
                    w_shin = smooth((p[1] - (leg['ankle'] - 1.5)) / 3)
                    w_thigh = smooth((p[1] - (leg['knee'] - 1)) / 2)          # a knee bends, it doesn't bow
                    w_torso = smooth((p[1] - (leg['hip'] - 2)) / 4)
                    if (x, r_) in joints['carried_texels']:
                        w_torso = 1.0
                    q = p
                    q = lerp(q, leg['shin'](p), w_shin)
                    q = lerp(q, leg['thigh'](p), w_thigh)
                    q = lerp(q, torso(p), w_torso)
                    out.append((q[0], q[1], idx, w_torso))
    return out


def lerp(a, b, w):
    return (a[0] + (b[0] - a[0]) * w, a[1] + (b[1] - a[1]) * w)


# The buckle (--animate): a short transition on the same hand-placed joints,
# no landing yet. Support failing: one knee goes suddenly and keeps going
# (accelerating), the other follows late and less, the pelvis drops and
# slides toward the failing side, and the torso holds upright until the
# support is gone, then tips that way. Beside it, the same rig in a
# deliberate squat (both knees, smooth, the torso held) to judge whether the
# timing alone says "fails" rather than "crouches".
def failing(t, scale=1.0):
    """Set for the zombie's legs (23 units hip to ankle); scale is the rig's leg length over that."""
    give = max(0.0, t - 1) / 6
    left = 8 * min(1.0, give) ** 2 + 3 * smooth((t - 7) / 5)     # sudden, accelerating, then still going
    right = 3.5 * smooth((t - 4) / 6)                              # the other leg follows late, less
    shift = -3 * smooth((t - 2) / 6)                               # pelvis slides toward the failing side
    torso = 0.55 * smooth((t - 6) / 7) ** 1.5                      # upright, then tips as support goes
    return dict(left=left * scale, right=right * scale), shift * scale, torso


def leg_scale(joints):
    return (joints['left']['hip'][1] - joints['left']['ankle'][1]) / 23


def squatting(t):
    d = 6 * smooth(t / 12)
    return dict(left=d, right=d), 0.0, 0.0


def animate(pic, joints, pal, out):
    cases = [('Support fails (left knee gives)', lambda t: failing(t, leg_scale(joints))), ('Deliberate squat (same rig)', squatting)]
    steps, sub = 16, 3
    cell, pw, ch, floor, scale = 80, 170, 64, 58, 4
    width = cell + pw
    colours = dict(left=(240, 200, 60), right=(200, 140, 40), shift=(120, 220, 120), torso=(80, 210, 230))
    frames = []

    def line(canvas, a, b, colour):
        n = int(max(abs(b[0] - a[0]), abs(b[1] - a[1]))) + 1
        for i in range(n + 1):
            x, y = int(round(a[0] + (b[0] - a[0]) * i / n)), int(round(a[1] + (b[1] - a[1]) * i / n))
            if 0 <= y < len(canvas) and 0 <= x < len(canvas[0]):
                canvas[y][x] = colour
    for step in range(steps * sub + 1):
        t = step / sub
        canvas = [[(24, 24, 28)] * width for _ in range(ch * len(cases))]
        for c, (_, control) in enumerate(cases):
            y0 = c * ch
            for y in range(y0 + floor, y0 + ch):
                for x in range(cell):
                    canvas[y][x] = (52, 48, 44)
            drops, shift, torso = control(t)
            for x, y, idx, w in sorted(pose(pic, joints, drops, 1, torso, shift), key=lambda p: p[3]):
                cx, cy = cell // 2 + int(math.floor(x)), y0 + floor - int(math.ceil(y))
                if 0 <= cx < cell and y0 <= cy < y0 + ch:
                    canvas[cy][cx] = tuple(pal[idx * 3:idx * 3 + 3])
            # Controls over the whole run, a cursor at now: drops (0-12 units), shift (0 to -4), torso (0-0.6 rad).
            px0, ph = cell + 6, ch - 12
            X = lambda tt: px0 + (pw - 12) * tt / steps
            Y = lambda v: y0 + 6 + ph * (1 - max(0.0, min(1.0, v)))
            line(canvas, (px0, Y(0)), (px0 + pw - 12, Y(0)), (60, 60, 66))
            for i in range(steps * sub):
                a, b = i / sub, (i + 1) / sub
                (da, sa, ta), (db, sb, tb) = control(a), control(b)
                line(canvas, (X(a), Y(da['left'] / 12)), (X(b), Y(db['left'] / 12)), colours['left'])
                line(canvas, (X(a), Y(da['right'] / 12)), (X(b), Y(db['right'] / 12)), colours['right'])
                line(canvas, (X(a), Y(-sa / 4)), (X(b), Y(-sb / 4)), colours['shift'])
                line(canvas, (X(a), Y(ta / 0.6)), (X(b), Y(tb / 0.6)), colours['torso'])
            line(canvas, (X(t), y0 + 3), (X(t), y0 + ch - 3), (150, 150, 150))
        big = []
        for row in canvas:
            wide = []
            for px in row:
                wide.extend(list(px) * scale)
            for _ in range(scale):
                big.append(wide)
        frames.append(base64.b64encode(png(width * scale, ch * len(cases) * scale, big)).decode())
    swatch = lambda c: f'<span style="display:inline-block;width:12px;height:3px;background:rgb{c};margin:0 4px 3px 12px"></span>'
    legend = (f'{swatch(colours["left"])}left hip drop {swatch(colours["right"])}right hip drop (0-12 units)'
              f'{swatch(colours["shift"])}pelvis shift left (0-4) {swatch(colours["torso"])}torso tip left (0-0.6 rad)')
    labels = ''.join(f'<div style="height:{ch * scale}px;display:flex;align-items:center">{html.escape(n)}</div>' for n, _ in cases)
    page = f"""<!doctype html><meta charset="utf-8"><title>Buckle</title>
<style>body{{background:#151517;color:#ddd;font:14px system-ui;margin:24px}}button{{margin:12px 8px 0 0;padding:6px 12px}}</style>
<h2>The buckle: support failing vs a deliberate squat, same rig, same joints</h2>
<p>Pain drawing, joints placed by hand, feet planted, no landing. The plots show the controls; the grey line is now.</p><p>{legend}</p>
<div style="display:flex;gap:16px"><div>{labels}</div><img id="f" style="image-rendering:pixelated"></div>
<div><button id="p">Pause</button><button id="s">Slow motion</button><span id="c"></span></div>
<script>const F={frames!r};let i=0,run=true,slow=false;const img=document.getElementById('f'),c=document.getElementById('c');
function show(){{img.src='data:image/png;base64,'+F[i];c.textContent='tic '+(i/{sub}).toFixed(1);}}
function tick(){{if(run){{i=(i+1)%F.length;show();}}setTimeout(tick,(slow?4:1)*1000/35/{sub}+(i==F.length-1?1000:0)+(i==0?400:0));}}
document.getElementById('p').onclick=e=>{{run=!run;e.target.textContent=run?'Pause':'Play';}};
document.getElementById('s').onclick=e=>{{slow=!slow;e.target.textContent=slow?'Normal speed':'Slow motion';}};
document.addEventListener('keydown',e=>{{if(e.key=='ArrowRight'){{run=false;i=(i+1)%F.length;show();}}if(e.key=='ArrowLeft'){{run=false;i=(i+F.length-1)%F.length;show();}}}});
show();tick();</script>"""
    open(out, 'w').write(page)
    print('wrote', out, len(frames), 'frames')


# The fall (--fall): two hits carried down to the floor, beside vanilla.
# - Legs swept, on the pain drawing's rig: the buckle above, support failing
#   underneath first; once it's gone the pelvis drops toward the floor,
#   gathering speed, the torso goes over and the legs straighten and swing
#   out the other way, all about the pelvis (tipping the whole body over a
#   planted foot swung the other leg into the air). Mid-fall, where a fast
#   drop hides a change of drawing, the game's lying drawing takes over -
#   mirrored, as this fall goes left and the drawings lie right - at the
#   moment and offset where the silhouettes overlap most, by measurement.
# - Chest blast: vanilla's own death drawings already are one (the hit, going
#   back, thrown off the feet, down), so it keeps them, unmirrored, and the
#   force varies them: the recoil on the hit drawing (joints placed by hand
#   on it too), how fast it goes back, and the throw - a ballistic arc whose
#   height and backward travel grow with the force - before it lands.
# Lying drawings are set down on the floor (the art floats a few units, as
# in vanilla) and flatten briefly as the body lands.
MECHANICS = [dict(name='Legs swept', side=1, controls=failing, support_ends=11, hold=14,
                  buckle='left knee gives, pelvis drops and slides')]

def down(pic, joints, t, m):
    """The pose at t: the mechanic's own controls, and once support is gone the pelvis dropping toward
    the floor, the torso going over and the legs swinging out the other way, all about the pelvis."""
    drops, shift, torso = m['controls'](min(t, m['hold']), leg_scale(joints))
    u = min(1.0, max(0.0, (t - m['support_ends']) / 7) ** 1.7)       # gathering speed like a falling body
    if u <= 0:
        return pose(pic, joints, drops, 1, torso, shift), 0.0
    drops = {k: v * (1 - u) for k, v in drops.items()}
    side = m['side']
    pts = pose(pic, joints, drops, 1, torso + (side * 1.4 - torso) * u, shift)
    l, r = joints['left']['hip'], joints['right']['hip']
    pelvis = ((l[0] + r[0]) / 2 + shift, l[1] - (drops['left'] + drops['right']) / 2)
    legs = side * 1.3 * u
    c, s_ = math.cos(legs), math.sin(legs)
    out = []
    for x, y, i, w in pts:
        lx = pelvis[0] + (x - pelvis[0]) * c - (y - pelvis[1]) * s_
        ly = pelvis[1] + (x - pelvis[0]) * s_ + (y - pelvis[1]) * c
        out.append((x + (lx - x) * (1 - w), y + (ly - y) * (1 - w), i, w))
    # The pelvis comes down to the floor; nothing goes through it.
    sink = (pelvis[1] - 6) * u
    out = [(x, y - sink, i, w) for x, y, i, w in out]
    low = min(y for _, y, _, _ in out)
    return [(x, y - min(0.0, low), i, w) for x, y, i, w in out], u


def drawn(pic, flip, dx=0.0, flatten=0.0, lift=0.0):
    """A drawing's points (mirrored left to right when flip), moved dx, set down on the floor by its
    lowest solid row (at least 4 texels, not a drop of blood), pressed toward it by flatten."""
    solid = [r for r in range(pic.height) if sum(t >= 0 for t in pic.texels[r]) >= 4]
    bottom = pic.top - solid[-1] - 1 if solid else 0
    sign = -1 if flip else 1
    return [(sign * (x - pic.left + 0.5) + dx, (pic.top - r - 0.5 - bottom) * (1 - flatten) + lift, pic.texels[r][x], 0)
            for r in range(pic.height) for x in range(pic.width) if pic.texels[r][x] >= 0]


def cells(points):
    return {(int(math.floor(x)), int(math.ceil(y))) for x, y, *_ in points}


def handoff(pic, joints, lying, m):
    """When (mid-fall) and where (sideways offset) the lying drawing overlaps the falling rig most."""
    flip = m['side'] > 0
    target = {dx: cells(drawn(lying, flip, dx)) for dx in range(-10, 11)}
    best = (0.0, None, 0)
    for step in range(m['support_ends'] * 2, (m['support_ends'] + 12) * 2 + 1):
        t = step / 2
        pts, u = down(pic, joints, t, m)
        if not 0.4 <= u <= 0.8:                 # mid-fall, where a fast drop hides the change of drawing
            continue
        mine = cells(pts)
        for dx, theirs in target.items():
            iou = len(mine & theirs) / len(mine | theirs)
            if iou > best[0]:
                best = (iou, t, dx)
    return best


def legs_column(pic, joints, lying, rest_frames, m, tics):
    """The rigged fall of a mechanic, handed to the lying drawing; returns (t -> points, phase), its end, a note.
    The lying drawing takes over at the height the rig's body is at and finishes the fall itself,
    going on at the rig's speed under gravity, before it settles on the floor; then any drawings
    after it, the last the corpse."""
    iou, land, dx = handoff(pic, joints, lying, m)
    flip = m['side'] > 0
    middle = lambda pts: sum(y for _, y, _, _ in pts) / len(pts)
    now, before = down(pic, joints, land, m)[0], down(pic, joints, land - 0.5, m)[0]
    height = max(0.0, middle(now) - middle(drawn(lying, flip, dx)))
    top = lambda pts: max(y for _, y, _, _ in pts)
    # Units per tic, downward: the body coming down, at least as fast as a body that has been
    # falling a couple of tics (the rig's top hardly drops while the torso swings over).
    speed = max(2.5, (top(before) - top(now)) / 0.5)
    gravity = 1.0
    drop = (-speed + math.sqrt(speed * speed + 2 * gravity * height)) / gravity
    floor_at = land + drop
    rest = floor_at + tics * len(rest_frames)

    def at(t):
        if t < land:
            return down(pic, joints, t, m)[0], (m['buckle'] if t < m['support_ends'] else 'support gone: pelvis drops, torso goes over')
        if t < floor_at:
            into = t - land
            return drawn(lying, flip, dx, lift=max(0.0, height - speed * into - 0.5 * gravity * into * into)), 'lying drawing, still falling'
        if t < floor_at + tics:
            into = t - floor_at
            return drawn(lying, flip, dx, 0.12 * math.sin(math.pi * min(1.0, into / 4))), ('lands, settles' if into < 4 else 'lying')
        k = min(len(rest_frames) - 1, int((t - floor_at) // tics))
        return drawn(rest_frames[k], flip, dx), ('corpse' if k == len(rest_frames) - 1 else 'lying')
    return at, rest, f'hand-off at tic {land} ({height:.0f} units up, falling {speed:.1f} units/tic), on the floor at tic {floor_at:.1f}, overlap {iou:.2f}'


def floats(pic):
    """How far a drawing's lowest solid row (at least 4 texels) is above its origin."""
    solid = [r for r in range(pic.height) if sum(t >= 0 for t in pic.texels[r]) >= 4]
    return max(0, pic.top - solid[-1] - 1) if solid else 0


DEATH_FRAMES = dict(POSS='HIJKL', SPOS='HIJKL', CPOS='HIJKLMN')   # the death states' frames (info.c)


def death_frames(lumps, monster):
    """The death drawings from the hit (H) on, and which one lies: the first under 60% of the hit's height."""
    frames = [Picture(lumps[monster + letter + '0']) for letter in DEATH_FRAMES[monster]]
    lying = next(i for i, p in enumerate(frames) if i and p.top - floats(p) < 0.6 * frames[0].top)
    return frames, lying


def chest_column(lumps, pal, monster, force, tics):
    """Vanilla's drawings, varied by the force of the blast; returns (t -> points, phase), its end, a note.
    After the recoil on the hit drawing it goes on through the drawings to the lying one; the last of
    them is thrown in an arc only when its art floats off the floor (the zombie's), not when it
    stands on its feet (the chaingunner toppling)."""
    frames, lying = death_frames(lumps, monster)
    H, joints = rig(lumps, pal, monster + 'H0')
    hit = tics * (1.1 - 0.5 * force)                   # harder hits leave the hit drawing sooner
    back = tics * (1.0 - 0.4 * force)
    lift0, up, gravity = 4.0, 1 + 3 * force, 1.0       # thrown off its feet: units, units per tic, Doom's gravity
    flight = (up + math.sqrt(up * up + 2 * gravity * lift0)) / gravity
    travel = 0.5 + 1.5 * force                         # thrown back (to the right, the way the drawings fall)
    plan, t0 = [], hit                                 # (start, end, drawing, thrown)
    for i in range(1, lying):
        thrown = i >= 2 and i == lying - 1 and floats(frames[i]) >= 4
        plan.append((t0, t0 + (flight if thrown else back), frames[i], thrown))
        t0 += flight if thrown else back
    land = t0
    rest = land + tics * (len(frames) - lying)

    def at(t):
        if t < hit:
            torso = -(0.35 * force * (1 - math.exp(-t / 1.2)) + 0.1 * force * pulse(t, 2))   # shoulders snap back
            shift = force * smooth(t / 3)                                                      # hips pushed the other way
            return pose(H, joints, dict(left=0.0, right=0.0), 1, torso, shift), 'hit: shoulders recoil, legs hold'
        dx = 0.0
        for start, end, pic, thrown in plan:
            if t < end:
                into = t - start
                if thrown:
                    return drawn(pic, False, dx + travel * into, lift=lift0 + up * into - 0.5 * gravity * into * into), 'thrown off its feet'
                return drawn(pic, False, dx + travel * 0.5 * into), 'going back'
            dx += travel * (end - start) * (1.0 if thrown else 0.5)
        into = t - land
        if into < tics:
            return drawn(frames[lying], False, dx, (0.08 + 0.08 * force) * math.sin(math.pi * min(1.0, into / 4))), ('lands, settles' if into < 4 else 'lying')
        k = min(len(frames) - 1, lying + int(into // tics))
        return drawn(frames[k], False, dx), ('corpse' if k == len(frames) - 1 else 'lying')
    thrown = any(p[3] for p in plan)
    return at, rest, (f'thrown {up:.1f} units/tic up, {flight:.1f} tics in the air' if thrown else f'{len(plan)} drawings to the floor, {back:.1f} tics each')


def fall(lumps, pal, monster, out, tics=5):
    pic, joints = rig(lumps, pal, monster + 'G1')
    frames, lying = death_frames(lumps, monster)
    columns = [('Legs swept',) + legs_column(pic, joints, frames[lying], frames[lying + 1:], MECHANICS[0], tics),
               ('Chest blast, force 0.6',) + chest_column(lumps, pal, monster, 0.6, tics),
               ('Chest blast, force 1.0',) + chest_column(lumps, pal, monster, 1.0, tics)]
    for name, _, end, note in columns:
        print(f'{name}: {note}, still at tic {end:.1f}')
    steps, sub = int(max(end for _, _, end, _ in columns) + 10), 3
    vanilla = frames
    cell, ch, floor, scale = (128, 72, 66, 4) if monster == 'CPOS' else (96, 64, 58, 5)
    count = 1 + len(columns)
    frames, phases = [], []
    for step in range(steps * sub + 1):
        t = step / sub
        canvas = [[(24, 24, 28)] * (cell * count) for _ in range(ch)]
        for y in range(floor, ch):
            canvas[y] = [(52, 48, 44)] * (cell * count)
        v = vanilla[min(len(vanilla) - 1, int(t // tics))]
        layers = [[(x - v.left + 0.5, v.top - r - 0.5, v.texels[r][x], 0) for r in range(v.height)
                   for x in range(v.width) if v.texels[r][x] >= 0]]
        words = []
        for name, at, _, _ in columns:
            pts, word = at(t)
            layers.append(pts)
            words.append(f'{name}: {word}')
        phases.append(' | '.join(words))
        for c, points in enumerate(layers):
            for x, y, idx, w in sorted(points, key=lambda q: q[3]):
                cx, cy = c * cell + cell // 2 + int(math.floor(x)), floor - int(math.ceil(y))
                if c * cell <= cx < (c + 1) * cell and 0 <= cy < ch:
                    canvas[cy][cx] = tuple(pal[idx * 3:idx * 3 + 3])
        big = []
        for row in canvas:
            wide = []
            for px in row:
                wide.extend(list(px) * scale)
            for _ in range(scale):
                big.append(wide)
        frames.append(base64.b64encode(png(cell * count * scale, ch * scale, big)).decode())
    heads = ''.join(f'<div>{html.escape(n)}</div>' for n in ['Vanilla'] + [c[0] for c in columns])
    notes = '; '.join(f'{html.escape(n)}: {html.escape(note)}' for n, _, _, note in columns)
    page = f"""<!doctype html><meta charset="utf-8"><title>Two falls</title>
<style>body{{background:#151517;color:#ddd;font:14px system-ui;margin:24px}}button{{margin:12px 8px 0 0;padding:6px 12px}}
.t{{display:flex}}.t div{{width:{cell * scale}px;text-align:center}}</style>
<h2>Legs swept vs chest blast: {monster}</h2>
<p>Legs swept: the pain drawing on hand-placed joints buckles at the left knee, the pelvis drops, the torso follows, and the mirrored lying drawing takes over mid-fall. Chest blast: vanilla's own drawings, unmirrored, varied by force: recoil on the hit drawing (joints by hand), how fast it goes back, and a thrown arc before it lands and settles. ({notes})</p>
<div class="t">{heads}</div><img id="f" style="image-rendering:pixelated">
<div><button id="p">Pause</button><button id="s">Slow motion</button><span id="c"></span></div>
<script>const F={frames!r};const P={phases!r};let i=0,run=true,slow=false;const img=document.getElementById('f'),c=document.getElementById('c');
function show(){{img.src='data:image/png;base64,'+F[i];c.textContent='tic '+(i/{sub}).toFixed(1)+' \\u2014 '+P[i];}}
function tick(){{if(run){{i=(i+1)%F.length;show();}}setTimeout(tick,(slow?4:1)*1000/35/{sub}+(i==F.length-1?1000:0)+(i==0?400:0));}}
document.getElementById('p').onclick=e=>{{run=!run;e.target.textContent=run?'Pause':'Play';}};
document.getElementById('s').onclick=e=>{{slow=!slow;e.target.textContent=slow?'Normal speed':'Slow motion';}};
document.addEventListener('keydown',e=>{{if(e.key=='ArrowRight'){{run=false;i=(i+1)%F.length;show();}}if(e.key=='ArrowLeft'){{run=false;i=(i+F.length-1)%F.length;show();}}}});
show();tick();</script>"""
    open(out, 'w').write(page)
    print('wrote', out, len(frames), 'frames')


def main():
    args = sys.argv[1:]
    monster = 'POSS'
    if '--monster' in args:
        k = args.index('--monster')
        monster = args[k + 1]
        del args[k:k + 2]
    if len(args) not in (2, 3) or (len(args) == 3 and args[2] not in ('--animate', '--fall')) or monster + 'G1' not in RIGS:
        raise SystemExit(__doc__)
    lumps = read_wad(args[0])
    pal = lumps['PLAYPAL'][:768]
    pic, joints = rig(lumps, pal, monster + 'G1')
    if len(args) == 3:
        (animate(pic, joints, pal, args[1]) if args[2] == '--animate' else fall(lumps, pal, monster, args[1]))
        return
    hit = rig(lumps, pal, monster + 'H0')
    variants = [('Original', None),
                ('Knees out, 6 down', (dict(left=6, right=6), 1)),
                ('Knees out, 10 down', (dict(left=10, right=10), 1)),
                ('Knees in, 8 down', (dict(left=8, right=8), -1)),
                ('Left leg gives, 11 / 3', (dict(left=11, right=3), 1)),
                ('Pain drawing, joints', (pic, joints)),
                ('Hit drawing, joints', hit)]
    cell, ch, floor, scale = 80, 70, 64, 6
    canvas = [[(24, 24, 28)] * (cell * len(variants)) for _ in range(ch)]
    for c, (_, v) in enumerate(variants):
        for y in range(floor, ch):
            for x in range(c * cell, (c + 1) * cell):
                canvas[y][x] = (52, 48, 44)
        x0 = c * cell + cell // 2
        marked = isinstance(v, tuple) and isinstance(v[0], Picture)
        src, js = v if marked else (pic, joints)
        if v is None or marked:
            pts = [(x - src.left + 0.5, src.top - r - 0.5, src.texels[r][x], (x, r) in js['carried_texels']) for r in range(src.height)
                   for x in range(src.width) if src.texels[r][x] >= 0]
        else:
            pts = pose(pic, joints, v[0], v[1])
        # Torso drawn last, over the legs; carried texels tinted on the marked drawings.
        for x, y, idx, w in sorted(pts, key=lambda p: p[3]):
            cx, cy = x0 + int(math.floor(x)), floor - int(math.ceil(y))
            if c * cell <= cx < (c + 1) * cell and 0 <= cy < ch:
                rgb = tuple(pal[idx * 3:idx * 3 + 3])
                canvas[cy][cx] = (min(255, rgb[0] // 2 + 40), min(255, rgb[1] // 2 + 40), min(255, rgb[2] // 2 + 140)) if marked and w else rgb
        if marked:
            split = x0 + int(math.floor(js['split']))
            for y in range(floor - 30, floor):
                if y % 2 == 0:
                    canvas[y][split] = (255, 60, 200)
            for side in ('left', 'right'):
                for name in ('hip', 'knee', 'ankle'):
                    jx, jy = js[side][name]
                    cx, cy = x0 + int(math.floor(jx)), floor - int(math.ceil(jy))
                    for dx, dy in ((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1)):
                        if 0 <= cy + dy < ch:
                            canvas[cy + dy][cx + dx] = (60, 255, 120)
    big = []
    for row in canvas:
        wide = []
        for px in row:
            wide.extend(list(px) * scale)
        for _ in range(scale):
            big.append(wide)
    open(args[1], 'wb').write(png(len(canvas[0]) * scale, ch * scale, big))
    print('wrote', args[1], '-', ', '.join(name for name, _ in variants))


if __name__ == '__main__':
    main()
