"""Procedural death lab: does a hit-driven collapse, built on a monster's own
death frames, read as a body losing support or as an image being distorted?

One rule set (generate) turns a hit (force, height on the body) into
progress through the death drawings, at a pace the hit sets per phase, and
bounded departures from what each drawing shows (torso kicked or folded
about the hip, knees dropped, body leaned). Every moment is drawn from the
drawing for its stage, bent toward the in-between of it and the next plus
the hit's departures: the drawings keep the collapsing anatomy, the model
keeps the motion. The rig cuts each upright drawing at knee and hip; legs
shorten (knees come toward the eye), the torso turns about the hip, legs and
torso blend by position across the hip so the body stays joined, and texels
are carried forward at sub-texel points, never blended. A neutral hit is the
checkpoint: the drawings at their own pace with in-betweens, close to vanilla.
Earlier tries, kept as lessons: dropping whole rows read as a squashed
picture; one drawing carried through the whole fall became a rotating
cutout; legs folded in the picture's plane fanned into streaks; a pure
topple about the feet cartwheeled past horizontal.
Writes an HTML page that plays vanilla beside the generated falls (keep it
out of git: it embeds the WAD's art).

Usage: python3 -I tests/death_lab.py doom2.wad build/death-lab.html [--sprite POSS --frames HIJKL --tics 5 --stand A]
"""
import argparse
import base64
import html
import math
import struct
import zlib


def read_wad(path):
    data = open(path, 'rb').read()
    kind, count, offset = struct.unpack_from('<4sii', data, 0)
    if kind not in (b'IWAD', b'PWAD'):
        raise SystemExit(f'{path}: not a WAD')
    lumps = {}
    for i in range(count):
        pos, size, name = struct.unpack_from('<ii8s', data, offset + 16 * i)
        lumps[name.rstrip(b'\0').decode('ascii', 'replace').upper()] = data[pos:pos + size]
    return lumps


class Picture:
    """A Doom picture: texels[y][x] = palette index or -1, offsets as in the WAD."""
    def __init__(self, raw):
        self.width, self.height, self.left, self.top = struct.unpack_from('<hhhh', raw, 0)
        self.texels = [[-1] * self.width for _ in range(self.height)]
        for x in range(self.width):
            pos = struct.unpack_from('<i', raw, 8 + 4 * x)[0]
            while raw[pos] != 255:
                start, length = raw[pos], raw[pos + 1]
                for k in range(length):
                    if 0 <= start + k < self.height:
                        self.texels[start + k][x] = raw[pos + 3 + k]
                pos += length + 4


class Shape:
    """A frame's silhouette as the body model sees it, in units: rows by
    height above the feet (row r spans top-r-1 .. top-r), the opaque span of
    each, the standing top, and the middle of the body below and above the hip."""
    def __init__(self, pic):
        self.pic = pic
        rows = [r for r in range(pic.height) if any(t >= 0 for t in pic.texels[r])]
        self.first, self.last = rows[0], rows[-1]
        self.height = pic.top - self.first          # top of the opaque body above the feet
        self.hip = 0.45 * self.height

        def middle(lo, hi):
            xs = [x - pic.left + 0.5 for r in range(pic.height) for x in range(pic.width)
                  if pic.texels[r][x] >= 0 and lo <= pic.top - r - 0.5 < hi]
            return sum(xs) / len(xs) if xs else 0.0
        self.lower = middle(-99, self.hip)
        self.upper = middle(self.hip, 999)


def smooth(x):
    x = min(1.0, max(0.0, x))
    return x * x * (3 - 2 * x)


def pulse(t, peak):
    return 0.0 if t <= 0 else t / peak * math.exp(1 - t / peak)


# The rig: the first death drawing (the body still upright) cut into shins,
# thighs and torso by height, hinged at the knee and hip (each joint where the
# body crosses that height) and standing on the feet. A pose turns each part
# about its joint in the picture's plane, carrying the parts above it. Around
# each joint the turn blends in over a few rows, so a bend stays connected
# instead of cracking open; every texel is carried forward at nine sub-texel
# points, so nothing tears, and a pixel takes the colour of whichever texel
# lands on it last (texels move, never blend).
class Rig:
    def __init__(self, shape):
        pic, self.shape = shape.pic, shape
        self.knee, self.hip = 0.27 * shape.height, 0.52 * shape.height

        def joint(h, band=2):
            xs = [x - pic.left + 0.5 for r in range(pic.height) for x in range(pic.width)
                  if pic.texels[r][x] >= 0 and abs((pic.top - r - 0.5) - h) <= band]
            return (sum(xs) / len(xs) if xs else 0.0, -h)
        self.foot, self.kneeAt, self.hipAt = joint(1.5, 3), joint(self.knee), joint(self.hip)
        # Opaque texels bottom-up, so the upper body lands over the legs.
        self.texels = [(x - pic.left, r - pic.top, pic.texels[r][x])
                       for r in range(pic.height - 1, -1, -1) for x in range(pic.width) if pic.texels[r][x] >= 0]


def rotate(point, pivot, a):
    x, y = point[0] - pivot[0], point[1] - pivot[1]
    c, s = math.cos(a), math.sin(a)
    return (pivot[0] + x * c - y * s, pivot[1] + x * s + y * c)


SUB = (1 / 6, 1 / 2, 5 / 6)


def posed(rig, drop, lean, tilt, slide):
    """Destination points (x, y, palette index) of the rigged drawing. Seen
    from the front, buckling knees come toward the eye, so the legs shorten
    (drop: the share of the hip height lost); the torso rides down on the hip
    and turns about it in the picture's plane (tilt, radians, + right); then
    the whole body leans over about the feet (lean). Across the hip the legs'
    and torso's positions blend, so the body stays joined."""
    hip = (rig.hipAt[0], -rig.hip * (1 - drop))
    out = []
    for u, v, idx in rig.texels:
        for su in SUB:
            for sv in SUB:
                x, y = u + su, v + sv
                h = -y
                legs = (x, y * (1 - drop))
                body = rotate((x, y + rig.hip * drop), hip, tilt)
                w = smooth((h - (rig.hip - 4)) / 8)
                p = rotate(((1 - w) * legs[0] + w * body[0], (1 - w) * legs[1] + w * body[1]), rig.foot, lean)
                out.append((p[0] + slide, p[1], idx))
    below = max(y for _, y, _ in out)
    if below > 0:                               # nothing sinks into the floor
        out = [(x, y - below, i) for x, y, i in out]
    return out


def extent(points):
    xs = [x for x, _, _ in points]
    return -min(y for _, y, _ in points), sum(xs) / len(xs)


# What each drawing shows of the fall: its height and its lean (radians, +
# right: from the middle of its feet to the middle of its top quarter).
# Lying drawings have no lean to speak of; theirs is taken as lying over.
def lean_of(shape, upright, side):
    pic = shape.pic
    def middle(lo, hi):
        xs = [x - pic.left + 0.5 for r in range(pic.height) for x in range(pic.width)
              if pic.texels[r][x] >= 0 and lo <= pic.top - r - 0.5 < hi]
        return sum(xs) / len(xs) if xs else 0.0
    if not upright:
        return side * 1.2
    span = 0.75 * shape.height
    return math.atan2(middle(span, 999) - middle(-99, 6), span)


# The generator: the collapse's progress through the drawings, and the hit's
# departure from what each drawing shows. Progress runs at the drawings' own
# pace, scaled per phase by the hit (the reaction on the first drawing, the
# collapse up to the first lying one, the settle after): a hard high hit
# snaps through, a low hit holds the first drawing while the knees go, a
# weak hit slumps. On top, bounded bends: the torso kicked back by high hits
# or folded over the wound by weak mid ones, the knees dropped by low hits,
# the whole body leaning with the push. Each moment is drawn from the
# drawing its progress is in, bent toward the in-between of it and the next
# (so the fall moves between drawings), plus the hit's bends. Neutral hits
# have no departures: the drawings at their own pace, with in-betweens.
LIMITS = dict(drop=0.45, lean=0.35, tilt=0.5)


def generate(shapes, rigs, upright, leans, tics, hit, steps, sub):
    n = len(shapes)
    lie = next((i for i in range(n) if not upright[i]), n)
    if hit is None:
        force = height = high = 0.0
        scales = (1.0, 1.0, 1.0)
    else:
        force, height = hit
        high = smooth((height - 0.3) / 0.6)
        reaction = 1.0 - 0.45 * force * high + 0.9 * force * (1 - high)
        collapse = 1.0 - 0.3 * force + 0.5 * (1 - force)
        scales = (reaction, collapse, 1.1 - 0.2 * force)
    fold = 0.0 if hit is None else 0.7 * (1 - force) * max(0.0, 1 - abs(height - 0.5) * 2.5)
    p, frames = 0.0, []
    for step in range(steps * sub):
        t = step / sub
        k = min(n - 1, int(p))
        f = 0.0 if k == n - 1 else p - k
        nxt = min(n - 1, k + 1)
        if not upright[k]:
            frames.append((k, 0.0, 0.0, 0.0))
        else:
            # The in-between toward the next drawing.
            want_height = (1 - f) * shapes[k].height + f * shapes[nxt].height
            want_lean = (1 - f) * leans[k] + f * leans[nxt]
            drop = (shapes[k].height - want_height) / max(1.0, rigs[k].hip)
            lean = want_lean - leans[k]
            tilt = 0.0
            if hit is not None:
                stage = 1 - smooth((p - 1.5) / 1.5)                 # hit departures fade as the body lies
                tilt += (0.5 * force * high * pulse(t, 3) - fold * smooth(t / 4)) * stage
                drop += 0.75 * force * (1 - high) * smooth(t / 3) * stage
                lean += 0.12 * force * (1 if high > 0.5 else -0.5) * pulse(t, 5) * stage
            frames.append((k, min(LIMITS['drop'], max(0.0, drop)), max(-LIMITS['lean'], min(LIMITS['lean'], lean)),
                           max(-LIMITS['tilt'], min(LIMITS['tilt'], tilt))))
        phase = 0 if k == 0 else 1 if k < lie else 2
        if k < n - 1:
            p = min(n - 1.0, p + 1 / (tics * scales[phase]) / sub)
    return frames


HITS = [('Neutral (checkpoint)', None),
        ('Chest blast (force 0.9, height 0.8)', (0.9, 0.8)),
        ('Legs swept (force 0.6, height 0.15)', (0.6, 0.15)),
        ('Gut shot (force 0.3, height 0.5)', (0.3, 0.5))]


def png(width, height, rgb_rows):
    raw = b''.join(b'\0' + bytes(row) for row in rgb_rows)
    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body) & 0xffffffff)
    return (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 2, 0, 0, 0))
            + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


# The diagnostic (--diagnose): are the motion rules or the deformation the
# bottleneck? An explicit body model, driven by the hit, sets three controls
# over a fixed duration: pelvis height (share of standing hip height), torso
# angle about the pelvis and lean over the feet (radians, + toward the side
# the lying drawings lie). Rules: a high hit recoils the shoulders at once
# while the legs hold, and support fails late; a low hit fails the support
# at once, the pelvis drops, and the torso follows after a lag. The page
# shows, per hit: the skeleton those controls describe, the sprite drawn
# from them (the nearest drawing at or above the skeleton's height, bent
# within limits), and the controls over time, with what the sprite actually
# shows measured back from its pixels (height, lean) beside what the
# skeleton wanted. Distinct controls with alike sprites = the deformation
# is the bottleneck; alike controls = the rules are.
def body_model(t, hit, duration):
    force, height = hit
    high = smooth((height - 0.3) / 0.6)
    shoulder = force * high
    fails = 1 + 7 * high                       # when the legs stop holding
    lag = 3 * (1 - high)                       # the torso follows the pelvis this much later
    q = smooth((t - fails) / max(1.0, (duration - fails) * 0.85))
    pelvis = 1 - 0.85 * q
    torso = 0.7 * shoulder * (1 - math.exp(-t / 1.5))
    torso += (1.45 - torso) * smooth((t - fails - lag) / max(1.0, duration - fails - lag))
    lean = 0.12 * shoulder * smooth(t / 4) - 0.15 * (1 - high) * force * pulse(t, 5)
    return pelvis, torso, lean


def silhouette(points):
    """Height, and lean (feet to top quarter, as lean_of), of a set of points (x, y)."""
    top = -min(y for x, y in points)
    feet = [x for x, y in points if y > -6] or [0.0]
    head = [x for x, y in points if y < -0.75 * top] or [0.0]
    return top, math.atan2(sum(head) / len(head) - sum(feet) / len(feet), 0.75 * top)


def skeleton(stand, pelvis, torso, lean):
    hip_h, torso_l = 0.52 * stand.height, 0.48 * stand.height
    feet = (0.0, 0.0)
    hip = rotate((0.0, -pelvis * hip_h), feet, lean)
    knee = ((feet[0] + hip[0]) / 2 - 5 * (1 - pelvis), (feet[1] + hip[1]) / 2)
    world = torso + lean
    shoulder = (hip[0] + 0.75 * torso_l * math.sin(world), hip[1] - 0.75 * torso_l * math.cos(world))
    head = (hip[0] + torso_l * math.sin(world), hip[1] - torso_l * math.cos(world))
    joints = [feet, knee, hip, shoulder, head]
    samples = [(a[0] + (b[0] - a[0]) * i / 12, a[1] + (b[1] - a[1]) * i / 12)
               for a, b in zip(joints, joints[1:]) for i in range(13)]
    below = max(0.0, max(y for _, y in samples))
    return [(x, y - below) for x, y in joints], [(x, y - below) for x, y in samples]


def diagnose(args, shapes, rigs, upright, leans, stand, palette, out):
    cases = [('Chest blast (force 0.9, height 0.8)', (0.9, 0.8)), ('Legs swept (force 0.9, height 0.15)', (0.9, 0.15))]
    duration, steps, sub = 20, 28, 2
    sw, pw, ch, floor, scale = 96, 200, 80, 68, 3
    width = sw * 2 + pw
    rgb = [tuple(palette[i * 3:i * 3 + 3]) for i in range(256)]
    colours = dict(pelvis=(240, 200, 60), torso=(80, 210, 230), height=(235, 235, 235), lean=(230, 90, 200), anchor=(120, 120, 130))
    # Run each case once: controls, intended and expressed measures, and the posed sprite.
    runs = []
    for name, hit in cases:
        rows, anchor = [], 0
        for step in range(steps * sub):
            t = step / sub
            pelvis, torso, lean = body_model(t, hit, duration)
            joints, samples = skeleton(stand, pelvis, torso, lean)
            want_h, want_lean = silhouette(samples)
            # The drawing at or just above the skeleton's height, never going back.
            while anchor + 1 < len(shapes) and shapes[anchor + 1].height >= want_h - 2:
                anchor += 1
            k = anchor
            if upright[k]:
                drop = min(0.6, max(0.0, 1 - pelvis * 0.52 * stand.height / max(1.0, rigs[k].hip)))
                pts = posed(rigs[k], drop, max(-0.35, min(0.35, lean)), max(-0.6, min(0.6, torso - leans[k])), 0.0)
            else:
                pic = shapes[k].pic
                pts = [(x - pic.left + 0.5, r - pic.top + 0.5, pic.texels[r][x]) for r in range(pic.height)
                       for x in range(pic.width) if pic.texels[r][x] >= 0]
            got_h, got_lean = silhouette([(x, y) for x, y, _ in pts])
            rows.append(dict(t=t, pelvis=pelvis, torso=torso, lean=lean, joints=joints, pts=pts, k=k,
                             want_h=want_h / stand.height, got_h=got_h / stand.height, want_lean=want_lean, got_lean=got_lean))
        runs.append(rows)
        first = lambda key, test: next((r['t'] for r in rows if test(r[key])), None)
        print(f'{name}: pelvis below 0.8 at tic {first("pelvis", lambda v: v < 0.8)}, '
              f'torso past 0.5 rad at tic {first("torso", lambda v: v > 0.5)}; '
              f'sprite lean past 0.3 at tic {first("got_lean", lambda v: v > 0.3)}, '
              f'sprite height under 0.8 at tic {first("got_h", lambda v: v < 0.8)}')

    def line(canvas, a, b, colour, dotted=False):
        n = int(max(abs(b[0] - a[0]), abs(b[1] - a[1]))) + 1
        for i in range(n + 1):
            if dotted and i % 3:
                continue
            x, y = int(round(a[0] + (b[0] - a[0]) * i / n)), int(round(a[1] + (b[1] - a[1]) * i / n))
            if 0 <= y < len(canvas) and 0 <= x < len(canvas[0]):
                canvas[y][x] = colour

    frames_png = []
    for step in range(steps * sub):
        canvas = [[(24, 24, 28)] * width for _ in range(ch * len(cases))]
        for c, rows in enumerate(runs):
            y0 = c * ch
            for y in range(y0 + floor, y0 + ch):
                canvas[y] = canvas[y][:sw * 2] + [(24, 24, 28)] * pw
                for x in range(sw * 2):
                    canvas[y][x] = (52, 48, 44)
            r = rows[step]
            # Skeleton.
            js = [(sw // 2 + x, y0 + floor + y) for x, y in r['joints']]
            for a, b in zip(js, js[1:]):
                line(canvas, a, b, (210, 210, 210))
            line(canvas, js[2], js[2], colours['pelvis'])
            # Sprite.
            for x, y, idx in r['pts']:
                cx, cy = sw + sw // 2 + int(math.floor(x)), y0 + floor + int(math.floor(y))
                if sw <= cx < 2 * sw and y0 <= cy < y0 + ch:
                    canvas[cy][cx] = rgb[idx]
            # Plots: controls solid, measured from the sprite dotted.
            px0, ph = sw * 2 + 4, ch - 12
            X = lambda t: px0 + (pw - 8) * t / steps
            Y = lambda v: y0 + 4 + ph * (1 - max(0.0, min(1.0, v)))
            line(canvas, (px0, Y(0)), (px0 + pw - 8, Y(0)), (60, 60, 66))
            for a, b in zip(rows, rows[1:]):
                line(canvas, (X(a['t']), Y(a['pelvis'])), (X(b['t']), Y(b['pelvis'])), colours['pelvis'])
                line(canvas, (X(a['t']), Y(a['torso'] / 1.6)), (X(b['t']), Y(b['torso'] / 1.6)), colours['torso'])
                line(canvas, (X(a['t']), Y(a['want_h'])), (X(b['t']), Y(b['want_h'])), colours['height'])
                line(canvas, (X(a['t']), Y(a['got_h'])), (X(b['t']), Y(b['got_h'])), colours['height'], dotted=True)
                line(canvas, (X(a['t']), Y(a['want_lean'] / 1.6)), (X(b['t']), Y(b['want_lean'] / 1.6)), colours['lean'])
                line(canvas, (X(a['t']), Y(a['got_lean'] / 1.6)), (X(b['t']), Y(b['got_lean'] / 1.6)), colours['lean'], dotted=True)
                line(canvas, (X(a['t']), y0 + ch - 4 - 2 * a['k']), (X(b['t']), y0 + ch - 4 - 2 * b['k']), colours['anchor'])
            line(canvas, (X(r['t']), y0 + 2), (X(r['t']), y0 + ch - 2), (150, 150, 150), dotted=True)
        big = []
        for row in canvas:
            wide = []
            for px in row:
                wide.extend(list(px) * scale)
            for _ in range(scale):
                big.append(wide)
        frames_png.append(base64.b64encode(png(width * scale, ch * len(cases) * scale, big)).decode())
    swatch = lambda c: f'<span style="display:inline-block;width:12px;height:3px;background:rgb{c};margin:0 4px 3px 12px"></span>'
    legend = (f'{swatch(colours["pelvis"])}pelvis height {swatch(colours["torso"])}torso angle (0 to 1.6 rad)'
              f'{swatch(colours["height"])}body height {swatch(colours["lean"])}lean (0 to 1.6 rad)'
              f'{swatch(colours["anchor"])}drawing used (steps up)')
    labels = ''.join(f'<div style="height:{ch * scale}px;display:flex;align-items:center">{html.escape(n)}</div>' for n, _ in cases)
    page = f"""<!doctype html><meta charset="utf-8"><title>Death lab diagnostic</title>
<style>body{{background:#151517;color:#ddd;font:14px system-ui;margin:24px}}button{{margin:12px 8px 0 0;padding:6px 12px}}</style>
<h2>Diagnostic: the controls behind two falls, same duration</h2>
<p>Left to right: the skeleton the body model wants, the sprite drawn from it, and the controls over time. Solid lines are what the model wants; dotted lines are what the sprite actually shows, measured from its pixels.</p>
<p>{legend}</p>
<div style="display:flex;gap:16px"><div>{labels}</div><img id="f" style="image-rendering:pixelated"></div>
<div><button id="p">Pause</button><button id="s">Slow motion</button><span id="c"></span></div>
<script>const F={frames_png!r};let i=0,run=true,slow=false;const img=document.getElementById('f'),c=document.getElementById('c');
function show(){{img.src='data:image/png;base64,'+F[i];c.textContent='tic '+(i/{sub}).toFixed(1);}}
function tick(){{if(run){{i=(i+1)%F.length;show();}}setTimeout(tick,(slow?4:1)*1000/35/{sub}+(i==F.length-1?900:0));}}
document.getElementById('p').onclick=e=>{{run=!run;e.target.textContent=run?'Pause':'Play';}};
document.getElementById('s').onclick=e=>{{slow=!slow;e.target.textContent=slow?'Normal speed':'Slow motion';}};
document.addEventListener('keydown',e=>{{if(e.key=='ArrowRight'){{run=false;i=(i+1)%F.length;show();}}if(e.key=='ArrowLeft'){{run=false;i=(i+F.length-1)%F.length;show();}}}});
show();tick();</script>"""
    open(out, 'w').write(page)
    print(f'wrote {out}: {len(frames_png)} frames')


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('wad')
    ap.add_argument('out')
    ap.add_argument('--sprite', default='POSS')
    ap.add_argument('--frames', default='HIJKL')
    ap.add_argument('--stand', default='A')
    ap.add_argument('--tics', type=int, default=5)
    ap.add_argument('--scale', type=int, default=4)
    ap.add_argument('--diagnose', action='store_true', help='controls-vs-sprite diagnostic for two hits instead')
    args = ap.parse_args()
    lumps = read_wad(args.wad)
    palette = lumps['PLAYPAL'][:768]

    def lump(frame):
        for rot in '01':
            name = f'{args.sprite}{frame}{rot}'
            if name in lumps:
                return Picture(lumps[name])
        for name in lumps:
            if name.startswith(f'{args.sprite}{frame}') and len(name) >= 6:
                return Picture(lumps[name])
        raise SystemExit(f'no lump for {args.sprite}{frame}')
    shapes = [Shape(lump(f)) for f in args.frames]
    stand = Shape(lump(args.stand))
    print(f'standing: height {stand.height} lower {stand.lower:.1f} upper {stand.upper:.1f}')
    for f, sh in zip(args.frames, shapes):
        print(f'{args.sprite}{f}: height {sh.height:3d} lower {sh.lower:+5.1f} upper {sh.upper:+5.1f}  '
              f'(picture {sh.pic.width}x{sh.pic.height} offsets {sh.pic.left},{sh.pic.top})')
    rigs = [Rig(sh) for sh in shapes]
    upright = [sh.height >= 0.6 * stand.height for sh in shapes]
    lie = next((i for i in range(len(shapes)) if not upright[i]), len(shapes) - 1)
    side = 1 if shapes[lie].upper >= shapes[lie].lower else -1      # which way the lying drawings lie
    leans = [lean_of(sh, up, side) for sh, up in zip(shapes, upright)]
    print('leans', ' '.join(f'{l:+.2f}' for l in leans))
    if args.diagnose:
        diagnose(args, shapes, rigs, upright, leans, stand, palette, args.out)
        return

    cw, ch, floor, scale = 96, 80, 68, args.scale
    steps, sub = 34, 2
    runs = [generate(shapes, rigs, upright, leans, args.tics, hit, steps, sub) for _, hit in HITS]
    names = ['Vanilla'] + [name for name, _ in HITS]
    palette_rgb = [tuple(palette[i * 3:i * 3 + 3]) for i in range(256)]

    def put_drawing(canvas, x0, pic, clip):
        for r in range(pic.height):
            for x in range(pic.width):
                idx = pic.texels[r][x]
                if idx >= 0:
                    cx, cy = x0 + x - pic.left, floor - pic.top + r
                    if clip[0] <= cx < clip[1] and 0 <= cy < ch:
                        canvas[cy][cx] = palette_rgb[idx]

    frames_png = []
    for step in range(steps * sub):
        t = step / sub
        canvas = [[(24, 24, 28)] * (cw * len(names)) for _ in range(ch)]
        for y in range(floor, ch):
            canvas[y] = [(52, 48, 44)] * (cw * len(names))
        for c in range(len(names)):
            x0, clip = c * cw + cw // 2, (c * cw, (c + 1) * cw)
            if c == 0:
                put_drawing(canvas, x0, shapes[min(len(shapes) - 1, int(t // args.tics))].pic, clip)
                continue
            k, drop, lean, tilt = runs[c - 1][step]
            if abs(drop) + abs(lean) + abs(tilt) < 1e-3:
                put_drawing(canvas, x0, shapes[k].pic, clip)
                continue
            for x, y, idx in posed(rigs[k], drop, lean, tilt, 0.0):
                cx, cy = x0 + int(math.floor(x)), floor + int(math.floor(y))
                if clip[0] <= cx < clip[1] and 0 <= cy < ch:
                    canvas[cy][cx] = palette_rgb[idx]
        big = []
        for row in canvas:
            wide = []
            for px in row:
                wide.extend(list(px) * scale)
            for _ in range(scale):
                big.append(wide)
        frames_png.append(base64.b64encode(png(len(canvas[0]) * scale, ch * scale, big)).decode())
    titles = ''.join(f'<div style="width:{cw * scale}px">{html.escape(name)}</div>' for name in names)
    page = f'''<!doctype html><meta charset="utf-8"><title>Death lab</title>
<style>body{{background:#151517;color:#ddd;font:14px system-ui;margin:24px}}.t{{display:flex}}.t div{{text-align:center}}
button{{margin:12px 8px 0 0;padding:6px 12px}}</style>
<h2>{html.escape(args.sprite)} death: vanilla vs generated collapses</h2>
<p>Same drawings in every column. One rule set, fed a hit (force, height) or none: progress through the death drawings at a pace the hit sets, each moment drawn from the drawing for that stage, bent within limits (legs shortened, torso turned about the hip, body leaned; texels moved, never blended) toward the in-between of it and the next drawing plus the hit's departures. The neutral column is the checkpoint: it should look like vanilla with in-betweens.</p>
<div class="t">{titles}</div><img id="f" style="image-rendering:pixelated">
<div><button id="p">Pause</button><button id="s">Slow motion</button><span id="c"></span></div>
<script>const F={frames_png!r};let i=0,run=true,slow=false;const img=document.getElementById('f'),c=document.getElementById('c');
function show(){{img.src='data:image/png;base64,'+F[i];c.textContent='tic '+(i/{sub}).toFixed(1);}}
function tick(){{if(run){{i=(i+1)%F.length;show();}}setTimeout(tick,(slow?4:1)*1000/35/{sub}+(i==F.length-1?700:0));}}
document.getElementById('p').onclick=e=>{{run=!run;e.target.textContent=run?'Pause':'Play';}};
document.getElementById('s').onclick=e=>{{slow=!slow;e.target.textContent=slow?'Normal speed':'Slow motion';}};
document.addEventListener('keydown',e=>{{if(e.key=='ArrowRight'){{run=false;i=(i+1)%F.length;show();}}if(e.key=='ArrowLeft'){{run=false;i=(i+F.length-1)%F.length;show();}}}});
show();tick();</script>'''
    open(args.out, 'w').write(page)
    print(f'wrote {args.out}: {len(frames_png)} frames')


if __name__ == '__main__':
    main()
