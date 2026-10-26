/* Death rigs authored by hand for the procedural falls (scene3d/death_falls.cpp):
   per sprite lump, where the body's joints are, in units right of the
   sprite's origin and above its feet, for each leg's hip, knee and ankle;
   where the legs part (u); and up to three boxes (u0, h0, u1, h1) of texels
   that move with the torso though they hang below the hips (a carried gun),
   each taking all its texels or only those of one colour (grey metal, brown
   wood, or any neutral grey down to black). The ragdoll (scene3d/ragdoll.cpp)
   bends at the chest, so a gun held above the hips is boxed too: its texels
   then ride one rigid gun bone instead of bending with the spine. An entry holds only for the
   original artwork: the lump's size and a hash of its pixels must match
   (FNV-1a over the decoded image's palette index and coverage pairs,
   transparent pixels as two zeros, as sprite_eyes_stock.h), so replaced art
   gets no rig and dies as drawn. Placed by eye on each monster's pain and
   hit drawings, front view; the death lab (tests/death_pose.py) holds the
   same joints. */
#ifndef DOOM_DEATH_RIGS_STOCK_H
#define DOOM_DEATH_RIGS_STOCK_H
#include <algorithm>
#include <cstring>

enum { RIG_ALL, RIG_GREY, RIG_WOOD, RIG_NEUTRAL };
struct doom_rig_box { float u0, h0, u1, h1; int colour; };
struct doom_stock_rig {
    const char *name; int width, height; unsigned hash;
    float split; doom_rig_box carried[3];
    float hip[2][2], knee[2][2], ankle[2][2];   // [left, right][u, h]
    bool guided;    // a ragdoll chest blast may play the shot death on strings (ragdoll.cpp's keys fit it)
};
static const doom_stock_rig doom_stock_rig_table[] = {
    // Zombieman in pain, front: the rifle held above the hips, its muzzle end dipping below them;
    // the rest of it across the belly with its dark outline, and its butt by the right arm (a lone
    // grey texel on the right flank left out).
    {"POSSG1", 39, 55, 0xcc173d70u, 3.5f, {{-21, 21, -3, 29, RIG_ALL}, {-21, 29, 16, 39, RIG_NEUTRAL}, {16, 33, 21, 39, RIG_NEUTRAL}},
     {{-2.5f, 27}, {9.0f, 27}}, {{-2.0f, 15.5f}, {8.75f, 15.5f}}, {{-1.5f, 4}, {9.25f, 4}}, true},
    // Zombieman hit (first death drawing): the rifle slung across the left thigh, its muzzle clear of the leg.
    {"POSSH0", 39, 55, 0x2c7b8171u, 4.3f, {{-19, 12, 20, 31, RIG_GREY}, {-20, 12, -6.5f, 22, RIG_ALL}},
     {{-1.0f, 26}, {9.5f, 26}}, {{-1.5f, 14}, {9.5f, 14}}, {{-2.0f, 4}, {9.5f, 3.5f}}},
    // Shotgun guy in pain: the zombie's legs; the left hand hangs by the hip.
    {"SPOSG1", 31, 55, 0x6fce6888u, 4.0f, {{-13, 22, -7, 31, RIG_ALL}},
     {{-2.5f, 27}, {9.5f, 27}}, {{-2.0f, 15.5f}, {9.5f, 15.5f}}, {{0.0f, 4}, {10.0f, 4}}},
    // Shotgun guy hit: the shotgun across the body, stock down over the left thigh.
    {"SPOSH0", 33, 60, 0x60c0a2f4u, 4.0f, {{-15, 16, -2.5f, 27, RIG_WOOD}, {-3, 23.5f, 6, 29, RIG_GREY}},
     {{-2.5f, 27}, {9.5f, 27}}, {{-2.0f, 15.5f}, {9.5f, 15.5f}}, {{0.0f, 4}, {10.0f, 4}}},
    // Chaingunner in pain: short legs wide apart, the chaingun at the hip over the left thigh.
    {"CPOSG1", 43, 58, 0xb326ea33u, -2.5f, {{-22, 13.75f, -4.5f, 28, RIG_NEUTRAL}},
     {{-10.0f, 17.5f}, {5.0f, 17.5f}}, {{-13.0f, 9.5f}, {6.5f, 9.5f}}, {{-14.0f, 2}, {8.5f, 2}}},
    // Chaingunner hit: the gun and its ammo belt hang over both thighs, all of it with the torso.
    {"CPOSH0", 43, 61, 0x62744442u, 4.0f, {{-22, 12.75f, 24, 30, RIG_ALL}},
     {{-3.0f, 17.5f}, {9.0f, 17.5f}}, {{-1.5f, 9.5f}, {9.5f, 9.5f}}, {{0.5f, 2}, {9.0f, 2}}},
};

// Whether a texel of this colour moves with the torso, for a box's colour rule.
inline bool doom_rig_colour(int colour, const unsigned char *rgb)
{
    int hi = std::max({rgb[0], rgb[1], rgb[2]}), lo = std::min({rgb[0], rgb[1], rgb[2]});
    switch (colour) {
    case RIG_GREY: return hi - lo < 24 && hi > 40;
    case RIG_WOOD: return rgb[0] >= rgb[1] && rgb[1] >= rgb[2] + 5;
    case RIG_NEUTRAL: return hi - lo < 24;
    default: return true;
    }
}

inline const doom_stock_rig *doom_stock_rig_for(const char *name, int w, int h, const unsigned char *pixels)
{
    for (const doom_stock_rig &r : doom_stock_rig_table) {
        if (strcmp(r.name, name) || r.width != w || r.height != h) continue;
        unsigned hash = 2166136261u;
        for (size_t i = 0; i < (size_t)w * h * 2; ++i) hash = (hash ^ pixels[i]) * 16777619u;
        return hash == r.hash ? &r : nullptr;
    }
    return nullptr;
}
#endif
