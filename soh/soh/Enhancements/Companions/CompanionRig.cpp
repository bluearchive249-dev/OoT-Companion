// Copyright © 2026 Grimsfrost. All rights reserved.
// Companions: Mixamo motion put on the girls' own skeletons. Each frame her pose (from her own animation) is run
// through her skeleton, and each leg and arm bone is turned, from the hips outward, until it points the way the same
// bone points in the clip, in her own frame (x left, y up, z forward). Only the swing changes: each bone keeps its own
// twist, so her shapes stay hers.

#include "CompanionRig.h"
#include "CompanionClips.h"

#include <cmath>
#include <cstring>

#include "z64.h"
#include "macros.h"

namespace {

struct M3 {
    float m[3][3]; // m[row][col], vectors are columns
};

struct V3 {
    float x, y, z;
};

V3 Mul(const M3& a, const V3& v) {
    return { a.m[0][0] * v.x + a.m[0][1] * v.y + a.m[0][2] * v.z, a.m[1][0] * v.x + a.m[1][1] * v.y + a.m[1][2] * v.z,
             a.m[2][0] * v.x + a.m[2][1] * v.y + a.m[2][2] * v.z };
}

M3 Mul(const M3& a, const M3& b) {
    M3 r;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            r.m[i][j] = a.m[i][0] * b.m[0][j] + a.m[i][1] * b.m[1][j] + a.m[i][2] * b.m[2][j];
        }
    }
    return r;
}

M3 Transpose(const M3& a) {
    M3 r;
    for (int i = 0; i < 3; i++) {
        for (int j = 0; j < 3; j++) {
            r.m[i][j] = a.m[j][i];
        }
    }
    return r;
}

V3 Add(V3 a, V3 b) {
    return { a.x + b.x, a.y + b.y, a.z + b.z };
}
V3 Sub(V3 a, V3 b) {
    return { a.x - b.x, a.y - b.y, a.z - b.z };
}
float Dot(V3 a, V3 b) {
    return a.x * b.x + a.y * b.y + a.z * b.z;
}
V3 Cross(V3 a, V3 b) {
    return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
}
float Len(V3 a) {
    return sqrtf(Dot(a, a));
}
V3 Norm(V3 a) {
    float l = Len(a);
    return (l > 1e-6f) ? V3{ a.x / l, a.y / l, a.z / l } : V3{ 0.0f, -1.0f, 0.0f };
}

const float kBinToRad = 3.14159265f / 32768.0f;

// The game's limb rotation: Rz * Ry * Rx (Matrix_TranslateRotateZYX)
M3 FromRot(const Vec3s& r) {
    float sx = sinf(r.x * kBinToRad), cx = cosf(r.x * kBinToRad);
    float sy = sinf(r.y * kBinToRad), cy = cosf(r.y * kBinToRad);
    float sz = sinf(r.z * kBinToRad), cz = cosf(r.z * kBinToRad);
    M3 m;
    m.m[0][0] = cz * cy;
    m.m[0][1] = cz * sy * sx - sz * cx;
    m.m[0][2] = cz * sy * cx + sz * sx;
    m.m[1][0] = sz * cy;
    m.m[1][1] = sz * sy * sx + cz * cx;
    m.m[1][2] = sz * sy * cx - cz * sx;
    m.m[2][0] = -sy;
    m.m[2][1] = cy * sx;
    m.m[2][2] = cy * cx;
    return m;
}

Vec3s ToRot(const M3& m) {
    float sy = -m.m[2][0];
    sy = (sy > 1.0f) ? 1.0f : ((sy < -1.0f) ? -1.0f : sy);
    float y = asinf(sy);
    float x, z;
    if (fabsf(sy) < 0.9999f) {
        x = atan2f(m.m[2][1], m.m[2][2]);
        z = atan2f(m.m[1][0], m.m[0][0]);
    } else {
        x = atan2f(-m.m[1][2], m.m[1][1]);
        z = 0.0f;
    }
    Vec3s r;
    r.x = (s16)(int)(x / kBinToRad);
    r.y = (s16)(int)(y / kBinToRad);
    r.z = (s16)(int)(z / kBinToRad);
    return r;
}

// The turn taking direction a onto b, by `amount` of the way
M3 Turn(V3 a, V3 b, float amount) {
    a = Norm(a);
    b = Norm(b);
    V3 axis = Cross(a, b);
    float s = Len(axis);
    float c = Dot(a, b);
    float ang = atan2f(s, c) * amount;
    M3 r = { { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 } } };
    if (s < 1e-6f) {
        return r;
    }
    axis = { axis.x / s, axis.y / s, axis.z / s };
    float cs = cosf(ang), sn = sinf(ang), t = 1.0f - cs;
    r.m[0][0] = t * axis.x * axis.x + cs;
    r.m[0][1] = t * axis.x * axis.y - sn * axis.z;
    r.m[0][2] = t * axis.x * axis.z + sn * axis.y;
    r.m[1][0] = t * axis.x * axis.y + sn * axis.z;
    r.m[1][1] = t * axis.y * axis.y + cs;
    r.m[1][2] = t * axis.y * axis.z - sn * axis.x;
    r.m[2][0] = t * axis.x * axis.z - sn * axis.y;
    r.m[2][1] = t * axis.y * axis.z + sn * axis.x;
    r.m[2][2] = t * axis.z * axis.z + cs;
    return r;
}

const int kMaxLimbs = 32;

struct Pose {
    int count;
    int parent[kMaxLimbs]; // skeleton index, -1 for the root
    V3 offset[kMaxLimbs];  // jointPos
    M3 local[kMaxLimbs];
    M3 rot[kMaxLimbs]; // in her model's frame
    V3 pos[kMaxLimbs];
};

StandardLimb* Limb(SkelAnime* skel, int i) {
    return (StandardLimb*)SEGMENTED_TO_VIRTUAL(((void**)skel->skeleton)[i]);
}

void Parents(SkelAnime* skel, int i, int parent, Pose* p) {
    if ((i < 0) || (i >= p->count) || (i == LIMB_DONE)) {
        return;
    }
    StandardLimb* l = Limb(skel, i);
    p->parent[i] = parent;
    if (l->child != LIMB_DONE) {
        Parents(skel, l->child, i, p);
    }
    if (l->sibling != LIMB_DONE) {
        Parents(skel, l->sibling, parent, p);
    }
}

bool Build(SkelAnime* skel, Pose* p) {
    p->count = skel->limbCount;
    if ((p->count <= 0) || (p->count > kMaxLimbs) || (skel->skeleton == nullptr) || (skel->jointTable == nullptr)) {
        return false;
    }
    for (int i = 0; i < p->count; i++) {
        p->parent[i] = -1;
    }
    Parents(skel, 0, -1, p);
    for (int i = 0; i < p->count; i++) {
        StandardLimb* l = Limb(skel, i);
        p->offset[i] = { (float)l->jointPos.x, (float)l->jointPos.y, (float)l->jointPos.z };
        p->local[i] = FromRot(skel->jointTable[i + 1]);
    }
    // (the root sits where her animation puts it)
    p->offset[0] = { (float)skel->jointTable[0].x, (float)skel->jointTable[0].y, (float)skel->jointTable[0].z };
    return true;
}

// Parents always come before their children in the game's limb order
void Forward(Pose* p) {
    for (int i = 0; i < p->count; i++) {
        int par = p->parent[i];
        if (par < 0) {
            p->rot[i] = p->local[i];
            p->pos[i] = p->offset[i];
        } else {
            p->rot[i] = Mul(p->rot[par], p->local[i]);
            p->pos[i] = Add(p->pos[par], Mul(p->rot[par], p->offset[i]));
        }
    }
}

V3 ClipDir(const CompClipMix* mix, int bone) {
    V3 sum = { 0, 0, 0 };
    for (int k = 0; k < mix->count; k++) {
        const CompClip& c = sClips[mix->clip[k]];
        float f = mix->frame[k];
        int a = (int)floorf(f);
        float t = f - (float)a;
        int n = c.frames;
        a = c.loops ? (((a % n) + n) % n) : ((a < 0) ? 0 : ((a >= n) ? n - 1 : a));
        int b = c.loops ? ((a + 1) % n) : ((a + 1 >= n) ? n - 1 : a + 1);
        const int16_t* da = c.rows[a][bone];
        const int16_t* db = c.rows[b][bone];
        V3 d = { da[0] + (db[0] - da[0]) * t, da[1] + (db[1] - da[1]) * t, da[2] + (db[2] - da[2]) * t };
        d = Norm(d);
        sum = Add(sum, { d.x * mix->weight[k], d.y * mix->weight[k], d.z * mix->weight[k] });
    }
    return Norm(sum);
}

float ClipHips(const CompClipMix* mix) {
    float sum = 0.0f, w = 0.0f;
    for (int k = 0; k < mix->count; k++) {
        const CompClip& c = sClips[mix->clip[k]];
        int n = c.frames;
        int a = (int)floorf(mix->frame[k]);
        a = c.loops ? (((a % n) + n) % n) : ((a < 0) ? 0 : ((a >= n) ? n - 1 : a));
        sum += c.hipsY[a] / 10000.0f * mix->weight[k];
        w += mix->weight[k];
    }
    return (w > 0.0f) ? sum / w : 0.0f;
}

} // namespace

float CompRig_LegLength(SkelAnime* skel, const CompRigMap* map) {
    Pose p;
    if (!Build(skel, &p) || (map->lThigh == 0) || (map->lShin == 0) || (map->lFoot == 0)) {
        return 0.0f;
    }
    Forward(&p);
    int a = map->lThigh - 1, b = map->lShin - 1, c = map->lFoot - 1;
    if ((a >= p.count) || (b >= p.count) || (c >= p.count)) {
        return 0.0f;
    }
    return Len(Sub(p.pos[b], p.pos[a])) + Len(Sub(p.pos[c], p.pos[b]));
}

void CompRig_Solve(SkelAnime* skel, const CompRigMap* map, const CompClipMix* mix, float amount, CompRigOut* out) {
    out->mask = 0;
    out->rootDY = 0.0f;
    Pose p;
    if ((mix->count <= 0) || (amount <= 0.0f) || !Build(skel, &p)) {
        return;
    }
    Forward(&p);
    // The bones, hips outward: (start limb, end limb, clip bone)
    const int bones[8][3] = { { map->lThigh, map->lShin, 0 }, { map->lShin, map->lFoot, 1 },
                              { map->rThigh, map->rShin, 2 }, { map->rShin, map->rFoot, 3 },
                              { map->lUpper, map->lFore, 4 }, { map->lFore, map->lHand, 5 },
                              { map->rUpper, map->rFore, 6 }, { map->rFore, map->rHand, 7 } };
    for (auto& bn : bones) {
        int a = bn[0] - 1, b = bn[1] - 1;
        if ((a < 0) || (b < 0) || (a >= p.count) || (b >= p.count)) {
            continue;
        }
        V3 now = Sub(p.pos[b], p.pos[a]);
        if (Len(now) < 1.0f) {
            continue;
        }
        V3 want = ClipDir(mix, bn[2]);
        M3 q = Turn(now, want, amount);
        M3 newRot = Mul(q, p.rot[a]);
        int par = p.parent[a];
        p.local[a] = (par < 0) ? newRot : Mul(Transpose(p.rot[par]), newRot);
        Forward(&p);
        Vec3s r = ToRot(p.local[a]);
        out->rot[a + 1][0] = r.x;
        out->rot[a + 1][1] = r.y;
        out->rot[a + 1][2] = r.z;
        out->mask |= 1u << (a + 1);
    }
    // The hips' bob (the clips stand with the hips at about 1.14 leg lengths)
    float leg = 0.0f;
    if ((map->lThigh != 0) && (map->lShin != 0) && (map->lFoot != 0)) {
        Pose rest;
        if (Build(skel, &rest)) {
            Forward(&rest);
            int a = map->lThigh - 1, b = map->lShin - 1, c = map->lFoot - 1;
            if ((a < rest.count) && (b < rest.count) && (c < rest.count)) {
                leg = Len(Sub(rest.pos[b], rest.pos[a])) + Len(Sub(rest.pos[c], rest.pos[b]));
            }
        }
    }
    out->rootDY = (ClipHips(mix) - 1.14f) * leg * amount;
}

bool CompRig_AutoMap(SkelAnime* skel, CompRigMap* map) {
    Pose p;
    memset(map, 0, sizeof(*map));
    if (!Build(skel, &p)) {
        return false;
    }
    Forward(&p);
    float top = -1e9f, bottom = 1e9f;
    for (int i = 0; i < p.count; i++) {
        top = fmaxf(top, p.pos[i].y);
        bottom = fminf(bottom, p.pos[i].y);
    }
    float h = top - bottom;
    if (h < 1.0f) {
        return false;
    }
    // Chains of three: a limb, its first child, that one's first child
    float bestLeg[2] = { -1, -1 }, bestArm[2] = { -1, -1 };
    for (int a = 0; a < p.count; a++) {
        int b = -1, c = -1;
        for (int i = 0; i < p.count; i++) {
            if ((p.parent[i] == a) && (b < 0)) {
                b = i;
            }
        }
        if (b < 0) {
            continue;
        }
        for (int i = 0; i < p.count; i++) {
            if ((p.parent[i] == b) && (c < 0)) {
                c = i;
            }
        }
        if (c < 0) {
            continue;
        }
        V3 A = p.pos[a], C = p.pos[c];
        float rel = (A.y - bottom) / h;
        int side = (A.x > 0.0f) ? 0 : 1; // (her left is +x)
        float span = Len(Sub(C, A)) / h;
        // A leg: starts in the lower half, goes down a good way
        if ((rel < 0.6f) && (A.y - C.y > 0.25f * h) && (span > 0.25f)) {
            float score = span + fabsf(A.x) / h;
            if (score > bestLeg[side]) {
                bestLeg[side] = score;
                if (side == 0) {
                    map->lThigh = (uint8_t)(a + 1), map->lShin = (uint8_t)(b + 1), map->lFoot = (uint8_t)(c + 1);
                } else {
                    map->rThigh = (uint8_t)(a + 1), map->rShin = (uint8_t)(b + 1), map->rFoot = (uint8_t)(c + 1);
                }
            }
        }
        // An arm: starts high and out to the side
        if ((rel > 0.55f) && (fabsf(A.x) > 0.04f * h) && (span > 0.15f) && (C.y < A.y + 0.1f * h)) {
            float score = span + fabsf(C.x - A.x) / h;
            if (score > bestArm[side]) {
                bestArm[side] = score;
                if (side == 0) {
                    map->lUpper = (uint8_t)(a + 1), map->lFore = (uint8_t)(b + 1), map->lHand = (uint8_t)(c + 1);
                } else {
                    map->rUpper = (uint8_t)(a + 1), map->rFore = (uint8_t)(b + 1), map->rHand = (uint8_t)(c + 1);
                }
            }
        }
    }
    return (map->lThigh != 0) || (map->lUpper != 0);
}
