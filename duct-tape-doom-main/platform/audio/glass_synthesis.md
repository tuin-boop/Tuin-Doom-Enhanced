# Synthesizing a breaking window

The target is the impact and falling-glass sequence in RangoAnimations’
[“window breaking”](https://freesound.org/people/RangoAnimations/sounds/613879/).
The public preview is a measurement reference. Its audio is not copied into
the game; every game take is generated from a seed at startup.

## What the reference changes

Increasing treble alone cannot reproduce this sequence. Its strongest
initial crash gives way to repeated, overlapping contacts, including a
substantial later wave. The earlier synth put too much energy into its first
100 milliseconds, then left isolated, comparatively dull pings.

Measurements below use the public preview, averaged to mono and aligned to
the first sample above 1.5% of peak. That removes approximately 257 ms of
leading silence; the game must respond immediately to the shot. These are
properties of the preview, not claims about an uncompressed original.

| Measurement, after onset | Reference | Previous big-window take | New big-window take |
| --- | ---: | ---: | ---: |
| Time to 50% of signal energy | 0.312 s | 0.086 s | 0.419 s |
| Time to 90% of signal energy | 0.904 s | 0.513 s | 0.945 s |
| Time to 99% of signal energy | 1.169 s | 0.746 s | 1.229 s |
| Energy remaining after 0.8 s | 21.3% | 0.45% | 17.9% |

Both generated examples use seed 713 and were measured after unsigned 8-bit
conversion and an offline reproduction of the game’s held-sample playback
and open-path output filtering. Those steps matter: a
float synth preview can hide problems introduced by the actual game path.
The reference’s renewed wave around 0.85 s is evidence for further contacts,
not simply a longer exponential decay or extra reverberation. The exact
objects and floor material in the recording cannot be recovered from these
measurements, so the model below is an approximation.

The new take retains substantial late activity and comes much closer to the
reference’s time to 90% and 99% of energy. Its first half still develops more
slowly. These numbers describe one reproducible take, not every seeded
variant or a guarantee of perceptual equivalence. Across four tested large
Shatter variants, the native 5.5–10 kHz energy share is now approximately
11–13%, compared with 12% in the reference.

The preview also contains about 12.3% of its energy above 5.512 kHz and 3.0%
above 8 kHz. An 11.025 kHz sample cannot retain that original upper band.
Holding an 11 kHz sample during playback produces spectral images, but those
images do not recreate the missing collision detail. Glass therefore uses
22.05 kHz while retaining DOOM’s unsigned 8-bit storage. The other generated
effects and ambience retain their existing rates. Only about 0.25% of this
reference’s energy is above 10 kHz, so 22.05 kHz is a useful bandwidth budget.

## A crash made from objects and contacts

1. **Pane failure.** A brief broad response supplies the initial body.
   Shatter builds a short cluster of fracture packets; Fracture preserves a
   branching crack front. Neither needs a long blanket of noise.
2. **Pieces catching the frame.** Early contacts overlap the failure and
   connect it to the first floor impacts, avoiding an artificial pause.
3. **Falling and rebounding.** A representative collection of large and small
   fragments receives release times, heights, initial vertical speeds and
   restitution values. Further contacts follow those objects’ trajectories.
4. **Edges settling.** Additional quiet, closely spaced taps approximate a
   flat irregular piece tipping across its edges. Sparse small pieces finish
   the sequence.

For a released piece at height `h`, upward speed `u`, and gravity
`g = 35²` map units/s²:

```
impact speed v = sqrt(u² + 2gh)
first flight    = (u + v) / g
next speed      = e v
next flight     = 2 e v / g
```

Here `e` is that piece’s restitution. Rebound height, spacing and strength
decrease together. A piece falling from 110 units with zero initial vertical
speed first lands at approximately 0.424 s; with `e = 0.5`, its next landing
is around 0.848 s. This naturally produces the later activity missing from
the previous fixed, short bounce intervals. Edge taps add irregularity;
perfectly periodic bouncing balls would be the wrong model for flat shards.

These trajectories synthesize an offline sound take. They represent the
break rather than following the individual rendered debris objects live.
Room reflections, distance and acoustic obstruction remain the game mixer’s
responsibility.

## The sound of one contact

Each fragment receives an inharmonic family of fixed frequencies, modal
gains and damping. Size affects its register. The same piece keeps those
frequencies across later hits; contact position changes which modes are
excited. Short, damped responses overlap, producing a changing crowd of
objects rather than unrelated random notes.

A short contact also needs a broad transient. The model combines a compact
half-sine force pulse, an approximate acceleration transient and a very
short surface packet with the modal response. Randomness is confined to
these individual contacts. It is not a continuous hiss used to fill the
whole tail. The balance is critical: excess direct transient becomes spray;
excess ringing becomes bells. Frequencies and decay times are expressed in
Hz and seconds, with modes beyond the render rate’s useful bandwidth
omitted, rather than scaling every pitch with the sample rate.
The modal filters also compensate for their digital frequency warping:
using the analog `Q = π f τ` unchanged near Nyquist would make the highest
notes ring several times longer than the intended decay, adding an unwanted
bell-like tail.

The motivation comes from research on changing modes during
[fracture](https://www.cs.cornell.edu/projects/FractureSound/files/fractureSound_comp.pdf)
and on the role of short
[acceleration transients in rigid impacts](https://www.cs.cornell.edu/projects/Sound/impact/PrecomputedAccelerationNoise2012.pdf).
This implementation is a small procedural approximation; it does not solve
the fracture mechanics or acoustic radiation of a real pane. The separation
of breaking, surface contacts and scattered bounces is also consistent with
[Keller’s environmental sound models](https://www.sfu.ca/sonic-studio-webdav/srs/EcoModelsComposition/EcoMethods.html).

## Verification and audition

`glass-audio-synthesis` checks deterministic variation, finite output and
guarded short buffers at 8, 11.025, 22.05 and 44.1 kHz; immediate break
activity; continuing late impacts; quiet endings; and the actual startup
bank’s rates and lengths. It also checks that the rates of footsteps,
ambience and lamp strikes stay unchanged.

`doom-sfx-render` uses each effect’s native game rate by default. Render both
break styles dry first, then with the room mixer and pistol. Compare at
matched loudness: more volume is not evidence of a better glass model.
Envelope and spectral measurements establish that the missing temporal and
frequency structure is present; listening in the game remains the test of
whether its character fits DOOM.
