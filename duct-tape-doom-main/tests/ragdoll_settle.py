"""How ragdolls come to rest (death_ragdoll.py's doll, run the way the game runs
it in ragdoll.cpp): how long after the torso lands until the body is still
enough to bake, how much the legs still move in that time (knee travel, and
how often a knee hinge flips), and how curled it ends up (legs: hip to ankle
over the leg's length; spine: neck to hips over its straight length; a ball
of mush is short on both).

Usage: python3 -I tests/ragdoll_settle.py doom2.wad [--seeds 8] [--monsters POSSG1:POSSA5,SPOSG1:SPOSA5,TROOG1:TROOA5]
"""
import argparse
import math
import os
import random
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import death_ragdoll as R  # noqa: E402
from death_lab import read_wad  # noqa: E402


def settle(rest, fall, seed):
    """One fall as the game runs it; its landing and rest tics and the measures above."""
    rng = random.Random(seed)
    doll = R.Doll(rest)
    doll.flips = 0
    side = rng.uniform(-0.4, 0.4)
    for n in fall['hit']:
        jitter = tuple(v * (0.8 + 0.4 * rng.random()) for v in fall['push'])
        doll.push(n, R.add(jitter, (side, 0.0, 0.0)))
    yaw = rng.uniform(-0.1, 0.1)
    for n in doll.p:
        r = R.sub(doll.p[n], doll.p['spine'])
        doll.push(n, R.scale((-r[1], r[0], 0.0), yaw))
    front = doll.front()
    for s in ('l', 'r'):
        out = -1 if s == 'l' else 1
        doll.push('knee_' + s, R.add(R.scale(front, 0.2 + 0.9 * rng.random()), (out * 0.4 * rng.random(), 0, 0)))
    late = rng.random() * 6
    shift = {g: rng.uniform(-2, 2) for g in ('legs', 'trunk', 'neck')}
    # The game takes out the push's net drift: the doll only turns and folds the body.
    mean = [0.0, 0.0]
    for n in doll.p:
        v = R.sub(doll.p[n], doll.prev[n])
        mean[0] += v[0] / len(doll.p)
        mean[1] += v[1] / len(doll.p)
    for n in doll.p:
        doll.prev[n] = R.add(doll.prev[n], (mean[0], mean[1], 0.0))
    landed_at = rest_at = None
    still = 0
    travel = 0.0
    flips_at_landing = 0
    for t in range(175):
        muscle = {g: R.fade(t - shift[g], *fall[g]) for g in ('legs', 'trunk', 'neck')}
        muscle['legs'] = R.fade(t - late - shift['legs'], *fall['legs'])
        knees = (doll.p['knee_l'], doll.p['knee_r'])
        doll.tic(muscle)
        if t >= 2:
            doll.steer()
        if getattr(doll, 'landed', False):
            if landed_at is None:
                landed_at, flips_at_landing = t, doll.flips
            travel += math.dist(knees[0], doll.p['knee_l']) + math.dist(knees[1], doll.p['knee_r'])
        energy = sum(R.dot(R.sub(doll.p[k], doll.prev[k]), R.sub(doll.p[k], doll.prev[k])) for k in doll.p)
        still = still + 1 if getattr(doll, 'landed', False) and energy < 0.002 else 0
        if still >= 8 or (landed_at is not None and t - landed_at >= 40):
            rest_at = t
            break
    p = doll.p
    legs = []
    for s in ('l', 'r'):
        full = doll.len[('hip_' + s, 'knee_' + s)] + doll.len[('knee_' + s, 'ank_' + s)]
        legs.append(math.dist(p['hip_' + s], p['ank_' + s]) / full)
    hips = R.mid(p['hip_l'], p['hip_r'])
    straight = math.dist(rest['neck'], R.mid(rest['hip_l'], rest['hip_r']))
    return dict(landed=landed_at, rest=rest_at, travel=travel, flips=doll.flips - flips_at_landing,
                legs=sum(legs) / 2, spine=math.dist(p['neck'], hips) / straight, up=doll.front()[2] > 0.5 or doll.front()[1] < -0.5)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('wad')
    ap.add_argument('--seeds', type=int, default=8)
    ap.add_argument('--monsters', default='POSSG1:POSSA5,SPOSG1:SPOSA5,TROOG1:TROOA5')
    args = ap.parse_args()
    lumps = read_wad(args.wad)
    pal = lumps['PLAYPAL'][:768]
    total = dict(wait=[], travel=[], flips=[], legs=[], spine=[], timeouts=0, down=0, n=0)
    for spec in args.monsters.split(','):
        frame, back = spec.split(':')
        _, _, _, rest, _ = R.setup(lumps, pal, frame, back, 'tube')
        for title, fall in R.FALLS:
            rows = [settle(rest, fall, seed) for seed in range(1, args.seeds + 1)]
            waits = [r['rest'] - r['landed'] if r['rest'] is not None and r['landed'] is not None else 175 for r in rows]
            print(f"{frame} {title[:24]:24s} rest after landing {sum(waits) / len(waits):5.1f} tics (timeouts {sum(r['rest'] is None for r in rows)}), "
                  f"knee travel {sum(r['travel'] for r in rows) / len(rows):5.1f}, flips {sum(r['flips'] for r in rows) / len(rows):4.1f}, "
                  f"legs {sum(r['legs'] for r in rows) / len(rows):.2f}, spine {sum(r['spine'] for r in rows) / len(rows):.2f}, "
                  f"face down {sum(not r['up'] for r in rows)}")
            total['wait'] += waits
            total['travel'] += [r['travel'] for r in rows]
            total['flips'] += [r['flips'] for r in rows]
            total['legs'] += [r['legs'] for r in rows]
            total['spine'] += [r['spine'] for r in rows]
            total['timeouts'] += sum(r['rest'] is None for r in rows)
            total['down'] += sum(not r['up'] for r in rows)
            total['n'] += len(rows)
    n = total['n']
    curled = sum(1 for a, b in zip(total['legs'], total['spine']) if a < 0.7 and b < 0.95)
    print(f"ALL: rest after landing {sum(total['wait']) / n:.1f} tics, timeouts {total['timeouts']}/{n}, knee travel {sum(total['travel']) / n:.1f}, "
          f"flips {sum(total['flips']) / n:.1f}, legs {sum(total['legs']) / n:.2f}, spine {sum(total['spine']) / n:.2f}, "
          f"curled (legs < 0.7 and spine < 0.95) {curled}/{n}, face down {total['down']}/{n}")


if __name__ == '__main__':
    main()
