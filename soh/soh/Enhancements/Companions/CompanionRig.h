#pragma once

// Copyright © 2026 Grimsfrost. All rights reserved.
// Companions: Mixamo motion put on the girls' own skeletons in the game (see CompanionRig.cpp)

#include <stdint.h>

struct SkelAnime;

// A girl's leg and arm bones, by limb number (1 = root): each bone runs from its limb to the next limb in the list
// (thigh to shin, shin to foot, upper arm to forearm, forearm to hand). 0: none (that bone keeps its own pose)
struct CompRigMap {
    uint8_t lThigh, lShin, lFoot, rThigh, rShin, rFoot;
    uint8_t lUpper, lFore, lHand, rUpper, rFore, rHand;
};

struct CompRigOut {
    int16_t rot[32][3]; // a limb's new rotation (x, y, z), for the limbs marked
    uint32_t mask;      // which limbs (bit = limb number)
    float rootDY;       // how much to raise (or lower) her root, in her model's units
};

// Guesses a map from the skeleton's shape, for a model whose limbs have no names (legs down from the hips, arms out
// from the chest). Returns false if it finds no legs and no arms
bool CompRig_AutoMap(SkelAnime* skel, CompRigMap* map);

// Each clip's bone directions, blended (weights add up to at most 1; the rest stays her own pose), at frames f
struct CompClipMix {
    int clip[3];
    float frame[3];
    float weight[3];
    int count;
};
void CompRig_Solve(SkelAnime* skel, const CompRigMap* map, const CompClipMix* mix, float amount, CompRigOut* out);

// Bones pointed in given directions (her own frame: x left, y up, z forward), for poses that aren't in a clip
// (a hug). Bone order as in the clips: L thigh, L shin, R thigh, R shin, L upper arm, L forearm, R upper arm, R
// forearm. use[i] false: that bone is left as it is. Adds to `out` (it isn't cleared)
void CompRig_SolveDirs(SkelAnime* skel, const CompRigMap* map, const float dirs[8][3], const bool use[8], float amount,
                       CompRigOut* out);

// One arm (left: true) reaching for a point in her model's frame (a hand to hold), the elbow a little bent. Adds to
// `out`
void CompRig_Reach(SkelAnime* skel, const CompRigMap* map, bool left, float tx, float ty, float tz, float amount,
                   CompRigOut* out);

// Which limbs hang below a limb (bits by limb number), to know whether scaling it moves anything but itself
uint32_t CompRig_Descendants(SkelAnime* skel, int limbNumber);

// A clip bone's direction now (her frame), blended over the mix
void CompRig_ClipBone(const CompClipMix* mix, int bone, float out[3]);

// Her leg length in her model's units (hip to ankle), for step rates and the hips' bob
float CompRig_LegLength(SkelAnime* skel, const CompRigMap* map);
