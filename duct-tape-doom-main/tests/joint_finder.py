"""Finding a monster's leg joints in its own drawing, without hand marks.

The procedural falls (platform/scene3d/death_falls.cpp) need each leg's hip,
knee and ankle, where the legs part, and which texels hang below the hips
but move with the torso (a gun). Hand marks exist for six drawings
(death_pose.RIGS); this finds the same from the art and scores itself
against them, then shows what it finds for every other monster. The game
runs the same rule (platform/death_joints.h) where no hand rig exists.

The rule, front view:
- Legs: from the feet up, the lowest row with two separate runs of texels
  starts two tracks, each following the run that overlaps it in the row
  above, until the two meet (the crotch) or one is lost. Legs that touch
  for a few rows and part again above (the imp's knock knees) are split
  where they parted below. Too few rows of two legs, a crotch too low or
  too high on the body, or a lower body mostly outside the legs: no rig.
- Heights: the leg's length runs from the floor (the start row) to the
  crotch; the hip sits 35% of it above the crotch, the ankle 22% above the
  floor, the knee halfway between (the zombie's proportions, which the
  other hand rigs share within a unit or two).
- Across: each leg's middle at the knee's and ankle's height, the hip over
  its top rows. Where something joins a leg's run from outside (a gun), the
  run jumps wider than a leg grows, and the leg keeps its width from the
  inner edge.
- Carried: below the hip's blend, any texel outside the two legs, grown
  into the legs (above the knee, joined to it) through colours clearly
  nearer its own than the shins' (a gun crossing a thigh).

Usage: python3 -I tests/joint_finder.py doom2.wad linuxdoom-1.10/info.c build/joint-finder.html
"""
import base64
import html
import math
import os
import re
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from death_lab import Picture, png, read_wad  # noqa: E402
from death_pose import RIGS, pose, rig  # noqa: E402

HIP, ANKLE = 0.35, 0.22      # of the leg's length (floor to crotch): hip above the crotch, ankle above the floor


def runs(pic, r):
    """Opaque spans of row r, as (first, last) columns."""
    out, start = [], None
    for c in range(pic.width + 1):
        solid = c < pic.width and pic.texels[r][c] >= 0
        if solid and start is None:
            start = c
        elif not solid and start is not None:
            out.append((start, c - 1))
            start = None
    return out


def overlap(a, b):
    return min(a[1], b[1]) - max(a[0], b[0]) + 1


def track(pic, first, start, pair):
    """Two legs followed up from the start row: each the run overlapping it in the row above, until
    the two meet (the crotch) or one is lost. Legs touching for a few rows (knock knees) that
    part again above are split where they parted below; a leg lost after that ends at the touch."""
    left, right = [pair[0]], [pair[1]]
    r, bridged = start - 1, None
    while r >= first:
        spans = runs(pic, r)
        nl = max(spans, key=lambda sp: overlap(sp, left[-1]), default=None)
        nr = max(spans, key=lambda sp: overlap(sp, right[-1]), default=None)
        if nl is None or nr is None or overlap(nl, left[-1]) <= 0 or overlap(nr, right[-1]) <= 0:
            if bridged is not None:
                del left[bridged:], right[bridged:]
            break
        if nl == nr:
            gap = (left[-1][1] + right[-1][0] + 1) // 2
            above = None
            for k in range(1, 7):
                if r - k < first:
                    break
                spans = runs(pic, r - k)
                a = max(spans, key=lambda sp: overlap(sp, left[-1]), default=None)
                b = max(spans, key=lambda sp: overlap(sp, right[-1]), default=None)
                if a and b and a != b and overlap(a, left[-1]) > 0 and overlap(b, right[-1]) > 0:
                    above = k
                    break
            if above is None or not nl[0] < gap <= nl[1]:
                break
            bridged = len(left)
            for k in range(above):
                run = max(runs(pic, r - k), key=lambda sp: overlap(sp, nl))
                left.append((run[0], gap - 1))
                right.append((gap, run[1]))
            r -= above
            continue
        left.append(nl)
        right.append(nr)
        r -= 1
    return left, right


def find(pic, pal):
    """Joints for a drawing, in death_pose's form (without carried texels), or (None, why)."""
    rows = [r for r in range(pic.height) if any(t >= 0 for t in pic.texels[r])]
    if not rows:
        return None, 'empty'
    bottom, body = rows[-1], pic.top - rows[0]
    # Every row near the feet with two runs at least two wide (the two widest) may start the
    # legs; the start that tracks the longest legs wins (the lowest rows can be two toes of
    # one foot stepping forward).
    best = None
    for start in range(bottom, max(rows[0], bottom - 10) - 1, -1):
        wide = [sp for sp in runs(pic, start) if sp[1] - sp[0] >= 1]
        if len(wide) >= 2:
            pair = sorted(sorted(wide, key=lambda sp: sp[0] - sp[1])[:2])
            left, right = track(pic, rows[0], start, pair)
            if best is None or len(left) > len(best[1]):
                best = (start, left, right)
    if best is None:
        return None, 'no two feet'
    start, left, right = best
    count = len(left)
    floor = pic.top - start - 1.0
    crotch = floor + count
    length = crotch - floor
    if count < 6:
        return None, f'legs only {count} rows'
    if not 0.2 * body <= crotch - floor <= 0.6 * body:
        return None, f'crotch at {length:.0f} of {body} units'
    hip, ankle = crotch + HIP * length, floor + ANKLE * length
    knee = (hip + ankle) / 2

    def middles(track, inner):
        """Each row's leg middle (units). A leg widens a texel or two a row at most; a run that jumps
        wider has something joined to it from outside, and the leg keeps its width from the inner edge."""
        width, out = track[0][1] - track[0][0] + 1, []
        for s in track:
            if s[1] - s[0] + 1 > width + 2:
                s = (s[1] - width + 1, s[1]) if inner == 'right' else (s[0], s[0] + width - 1)
            width = s[1] - s[0] + 1
            out.append(((s[0] + s[1] + 1) / 2 - pic.left, s))
        return out
    mids = {'left': middles(left, 'right'), 'right': middles(right, 'left')}
    joints = dict(split=0.0, carried=[], legs=mids, floor=floor, crotch=crotch)
    for side in ('left', 'right'):
        m = mids[side]
        at = lambda h: m[min(count - 1, max(0, int(h - floor)))][0]
        hu = sum(u for u, _ in m[-3:]) / len(m[-3:])         # the hip over the leg's top rows
        joints[side] = dict(hip=(hu, hip), knee=(at(knee), knee), ankle=(at(ankle), ankle))
    gaps = [((left[i][1] + 1 + right[i][0]) / 2 - pic.left) for i in range(max(0, count - 3), count)]
    joints['split'] = sum(gaps) / len(gaps)
    # Carried: below the hip's blend (hip - 2), what lies outside the two legs.
    carried = set()
    lo = mids['left'][-1][1][0] - 1
    hi = mids['right'][-1][1][1] + 1
    for r in range(pic.height):
        h = pic.top - r - 0.5
        if h >= hip + 2 or h <= ankle or r > start:     # nothing hangs at the feet (a toe claw is no gun)
            continue
        k = start - r
        for c in range(pic.width):
            if pic.texels[r][c] < 0:
                continue
            if k < count:
                inside = any(s[0] <= c <= s[1] for _, s in (mids['left'][k], mids['right'][k]))
            else:
                inside = lo <= c <= hi
            if not inside:
                carried.add((c, r))
    # A gun crossing a thigh: from the texels outside the legs, grow (4-connected, above the knee)
    # into texels whose colour is clearly nearer the carried ones' than any the shins show.
    rgb = lambda c, r: tuple(pal[pic.texels[r][c] * 3:pic.texels[r][c] * 3 + 3])
    shins = {rgb(c, r) for side in ('left', 'right') for k, (_, sp) in enumerate(mids[side])
             if ankle <= floor + k + 0.5 <= knee for c in range(sp[0], sp[1] + 1) for r in [start - k]}
    seeds = {rgb(c, r) for c, r in carried}
    near = lambda colour, group: min(math.dist(colour, g) for g in group) if group else 1e9
    todo = list(carried)
    while todo and shins:
        c, r = todo.pop()
        for dc, dr in ((1, 0), (-1, 0), (0, 1), (0, -1)):
            x, y = c + dc, r + dr
            if not (0 <= x < pic.width and 0 <= y < pic.height) or pic.texels[y][x] < 0 or (x, y) in carried:
                continue
            if not knee < pic.top - y - 0.5 < hip + 2:
                continue
            colour = rgb(x, y)
            if near(colour, seeds) < 0.5 * near(colour, shins):
                carried.add((x, y))
                todo.append((x, y))
    # Over a third of the lower body hanging outside the legs: not a body standing on two legs
    # (the Spider Mastermind's machine legs, the mancubus's arm cannon taken for a leg). The
    # most a real one carries is under 30% (the zombie's rifle, the demon's jaw and hands).
    lower = sum(1 for r in range(pic.height) for c in range(pic.width)
                if pic.texels[r][c] >= 0 and pic.top - r - 0.5 < hip + 2)
    if len(carried) > 0.35 * lower:
        return None, f'{100 * len(carried) // lower}% of the lower body is not legs'
    joints['carried_texels'] = carried
    return joints, f'legs {count} rows, crotch {crotch:.0f}'


def find_head(pic):
    """The head: from the drawing's top down, the run under the head's middle, until a row
    widens past 1.6 times the head's first rows (the shoulders) within the top 40% of the body.
    Returns (neck height, head middle, head width) in units, or None (no head above shoulders).
    Middle and width are medians over the head's rows, so horns or a raised hand don't widen it."""
    rows = [r for r in range(pic.height) if any(t >= 0 for t in pic.texels[r])]
    top = rows[0]
    body = pic.top - top

    def span(r, around):
        rs = runs(pic, r)
        if not rs:
            return None
        return min(rs, key=lambda a: 0 if a[0] <= around <= a[1] else min(abs(a[0] - around), abs(a[1] - around)))
    first = span(top, pic.width // 2)
    mid, spans = (first[0] + first[1]) // 2, []
    for k in range(int(body * 0.4)):
        sp = span(top + k, mid)
        if not sp:
            break
        spans.append(sp)
        mid = (sp[0] + sp[1]) // 2
    if len(spans) < 6:
        return None
    ref = max(b - a + 1 for a, b in spans[:5])
    for k in range(5, len(spans)):
        a, b = spans[k]
        if b - a + 1 > 1.6 * ref:
            widths = sorted(s[1] - s[0] + 1 for s in spans[:k])
            middles = sorted((s[0] + s[1] + 1) / 2 - pic.left for s in spans[:k])
            return pic.top - (top + k), middles[k // 2], widths[k // 2]
    return None


# -- The game's tables: which drawing is each monster's pain frame and first death frame (front view).
def monsters(info_c):
    text = open(info_c).read()
    states = {}
    for m in re.finditer(r'\{SPR_(\w+),(\d+),(-?\d+),\{\w+\},(\w+),\d+,\d+\},\s*//\s*(S_\w+)', text):
        states[m.group(5)] = (m.group(1), int(m.group(2)) & 0x7fff, int(m.group(3)), m.group(4))
    out = []
    for block in re.finditer(r'//\s*(MT_\w+)\s*\n(.*?)\n\s*\}', text, re.S):
        body = block.group(2)
        field = lambda name: (re.search(r'(\S+),\s*//\s*' + name + r'\b', body) or [None, None])[1]
        pain, death, flags = field('painstate'), field('deathstate'), field('flags') or ''
        if 'MF_COUNTKILL' not in flags or pain not in states or death not in states:
            continue
        chain, s = [], death
        while s in states and s not in chain and len(chain) < 32:
            chain.append(s)
            if states[s][2] < 0:
                break
            s = states[s][3]
        name = lambda st, rot: states[st][0] + chr(ord('A') + states[st][1]) + rot
        out.append(dict(mt=block.group(1), pain=states[pain], death=[states[c] for c in chain],
                        pain_lump=name(pain, '1'), hit_lump=name(death, '0')))
    return out


def lump_of(lumps, name):
    """The front view of a frame, as the game picks it (spriteframes lump[0]): the frame's one
    drawing for all rotations, or rotation 1, alone or paired with its mirror (A1A5-style)."""
    for rot in (name[5], '0' if name[5] == '1' else '1'):
        want = name[4] + rot
        for key in lumps:
            if key[:4] == name[:4] and (key[4:6] == want or (len(key) == 8 and key[6:8] == want)):
                return key
    return None


def score(pic, found, hand):
    """Joint errors (units) and how well the carried texels agree where it matters: below the hand
    rig's hip blend (above it the torso takes every texel anyway)."""
    errors = [math.dist(found[s][j], hand[s][j]) for s in ('left', 'right') for j in ('hip', 'knee', 'ankle')]
    low = lambda texels: {(c, r) for c, r in texels if pic.top - r - 0.5 < hand['left']['hip'][1] + 2}
    a, b = low(found['carried_texels']), low(hand['carried_texels'])
    jaccard = len(a & b) / len(a | b) if a | b else 1.0
    return sum(errors) / 6, max(errors), abs(found['split'] - hand['split']), jaccard


def picture(pic, pal, joints, hand=None, scale=5):
    """The drawing on a floor, carried texels tinted, the finder's joints yellow (hand marks green),
    then the same rig with the left leg given way (11/3 units, scaled to the leg)."""
    cell, ch = pic.width + 24, pic.top + 8
    floor = pic.top + 3
    canvas = [[(24, 24, 28)] * (cell * 2) for _ in range(ch)]
    for y in range(floor, ch):
        canvas[y] = [(52, 48, 44)] * (cell * 2)

    def put(x, y, rgb):
        if 0 <= y < ch and 0 <= x < cell * 2:
            canvas[y][x] = rgb
    x0 = cell // 2
    for r in range(pic.height):
        for c in range(pic.width):
            idx = pic.texels[r][c]
            if idx >= 0:
                rgb = tuple(pal[idx * 3:idx * 3 + 3])
                if joints and (c, r) in joints['carried_texels']:
                    rgb = (rgb[0] // 2 + 40, rgb[1] // 2 + 40, min(255, rgb[2] // 2 + 140))
                put(x0 + c - pic.left, floor - (pic.top - r), rgb)
    for js, colour in ((hand, (60, 255, 120)), (joints, (255, 220, 40))):
        if not js:
            continue
        for side in ('left', 'right'):
            for name in ('hip', 'knee', 'ankle'):
                jx, jy = js[side][name]
                cx, cy = x0 + int(math.floor(jx)), floor - int(math.ceil(jy))
                for dx, dy in ((0, 0), (1, 0), (-1, 0), (0, 1), (0, -1)):
                    put(cx + dx, cy + dy, colour)
    if joints:
        length = joints['crotch'] - joints['floor']
        k = length / 19
        for x, y, idx, w in sorted(pose(pic, joints, dict(left=11 * k, right=3 * k), 1), key=lambda p: p[3]):
            put(cell + x0 + int(math.floor(x)), floor - int(math.ceil(y)), tuple(pal[idx * 3:idx * 3 + 3]))
    big = []
    for row in canvas:
        wide = []
        for px in row:
            wide.extend(list(px) * scale)
        big.extend([wide] * scale)
    return base64.b64encode(png(cell * 2 * scale, ch * scale, big)).decode()


def main():
    if len(sys.argv) != 4:
        raise SystemExit(__doc__)
    lumps = read_wad(sys.argv[1])
    pal = lumps['PLAYPAL'][:768]
    parts = ['<h3>Against the hand marks</h3><table><tr><th>drawing</th><th>joints, mean / worst (units)</th>'
             '<th>split off by</th><th>carried overlap</th><th>found (yellow) and hand (green); left leg given way</th></tr>']
    for name in RIGS:
        pic, hand = rig(lumps, pal, name)
        found, why = find(pic, pal)
        if not found:
            parts.append(f'<tr><td>{name}</td><td colspan=3>no rig: {html.escape(why)}</td><td><img src="data:image/png;base64,{picture(pic, pal, None, hand)}"></td></tr>')
            print(f'{name}: no rig ({why})')
            continue
        mean, worst, split, jac = score(pic, found, hand)
        print(f'{name}: joints {mean:.1f} / {worst:.1f}, split {split:.1f}, carried {jac:.2f} ({why})')
        parts.append(f'<tr><td>{name}</td><td>{mean:.1f} / {worst:.1f}</td><td>{split:.1f}</td><td>{jac:.2f}</td>'
                     f'<td><img src="data:image/png;base64,{picture(pic, pal, found, hand)}"></td></tr>')
    parts.append('</table><h3>The rest of the roster: pain drawing (legs swept) and first death drawing (chest blast)</h3><table>')
    for m in monsters(sys.argv[2]):
        if m['pain_lump'][:4] in ('POSS', 'SPOS', 'CPOS'):
            continue
        cells = [f'<td>{m["mt"]}</td>']
        for key in ('pain_lump', 'hit_lump'):
            lump = lump_of(lumps, m[key])
            if not lump:
                cells.append(f'<td>{m[key]}: not in this WAD</td>')
                continue
            pic = Picture(lumps[lump])
            found, why = find(pic, pal)
            print(f'{m["mt"]} {lump}: {"rig" if found else "no rig"} ({why})')
            cells.append(f'<td>{lump}: {html.escape(why if found else "no rig: " + why)}<br>'
                         f'<img src="data:image/png;base64,{picture(pic, pal, found)}"></td>')
        parts.append('<tr>' + ''.join(cells) + '</tr>')
    parts.append('</table>')
    page = ('<!doctype html><meta charset="utf-8"><title>Joint finder</title><style>body{background:#151517;color:#ddd;'
            'font:14px system-ui;margin:24px}td,th{padding:6px 10px;vertical-align:top;text-align:left}img{image-rendering:pixelated}</style>'
            '<h2>Joint finder</h2><p>Legs tracked up from the feet to the crotch; hip, knee and ankle from the leg\'s length; '
            'blue texels hang below the hips and move with the torso. Right of each drawing: its rig with the left leg given way.</p>'
            + ''.join(parts))
    open(sys.argv[3], 'w').write(page)
    print('wrote', sys.argv[3])


if __name__ == '__main__':
    main()
