/* How sound gets through the map: a graph of its openings. Every two-sided
 * line is an opening between two sectors; sound travels from opening to
 * opening across the sectors they share. One search from the listener finds,
 * for each opening in reach, the way there: how far it is, how much is lost
 * crossing openings (a narrow gap, a closed door, a window's glass) and how
 * dull that leaves it. Sectors are only topology here, so a staircase of
 * twenty sectors is no twenty rooms. Distance and loss stay apart: glass
 * makes a sound quieter and duller, not farther. */
#ifndef DOOM_AUDIO_ACOUSTIC_GRAPH_H
#define DOOM_AUDIO_ACOUSTIC_GRAPH_H

/* What reaches the listener from a point, summed over the openings it can
   come through: loudness after distance (fade) and loss, the lowpass it
   comes through, the direction it arrives from (where its ways enter the
   listener's sector, weighted) and how far the ways are, weighted alike. */
typedef struct { float gain, cutoff, from_x, from_y, distance; } acoustic_heard_t;

/* Searches outward from the listener at (x, y) in sector, as far as reach
   units plus losses. Cheap; call when the listener moves or now and then for
   doors. Builds the openings first on a new level. */
void Acoustic_Listen(float x, float y, int sector, float reach);
/* A point (x, y) in sector, heard through the openings of its sector the last
   search reached; openings to the same neighboring sector count as one way. fade(distance) turns a path's length into loudness. Not
   for the listener's own sector: there the way is straight. Returns 0 when
   nothing gets through. */
int Acoustic_Heard(float x, float y, int sector, float (*fade)(float distance, const void *data),
                   const void *data, acoustic_heard_t *out);
/* The cheapest way to a point (x, y) in sector: its length, loss in dB and
   the lowpass after it. A sound inside a shut door is heard from the door's
   openings, not through itself. Returns 0 when the search did not reach it. */
typedef struct { float length, loss, cutoff; } acoustic_way_t;
int Acoustic_Way(float x, float y, int sector, acoustic_way_t *out);
/* Keeps the search on the console player: run again once they have moved,
   changed sector or a few tics have passed. Every user calls this first. */
void Acoustic_Follow(void);
/* A window pane on line, whole or broken; through a whole one sound is
   much quieter and dull. Again when it breaks. */
void Acoustic_Glass(int line, int whole);
/* Whether window glass is shown at all; without it no pane holds sound back. */
void Acoustic_ShowGlass(int shown);
/* Whether a whole pane is shown on line. */
int Acoustic_GlassWhole(int line);
#endif
