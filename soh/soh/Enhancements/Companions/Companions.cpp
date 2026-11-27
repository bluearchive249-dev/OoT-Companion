// Copyright © 2026 Grimsfrost. All rights reserved.
// Companions: Saria, Ruto, Malon and Zelda travel with Link. Each one is drawn from her own model in the game, follows
// him in a loose group, fights the enemies near him with moves made from the game's own items and effects, reacts to
// what happens, and talks: short speech bubbles over her head (they never stop the game), chats with the others,
// tips about the small keys in a dungeon (from the randomizer's placement on a randomizer file), and a longer talk
// when Link Z-targets her and presses A.
//
// Ruto, Malon and Zelda change with Link's age (young with child Link, grown up with adult Link). Saria doesn't age.
// Everything is in the "Companions" section of Enhancements > Extra Modes. Numbers are tunable at runtime in
// companion_tuning.json ("companion.*" keys, see CompanionTuning.h); lines can be added in companion_lines.json.

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <vector>

#include <imgui.h>
#include <nlohmann/json.hpp>
#include <libultraship/bridge/consolevariablebridge.h>
#include <libultraship/bridge/resourcebridge.h>
#include <ship/Context.h>
#include <ship/window/gui/GuiWindow.h>

#include "soh/ActorDB.h"
#include "soh/cvar_prefixes.h"
#include "soh/Enhancements/custom-message/CustomMessageManager.h"
#include "soh/Enhancements/game-interactor/GameInteractor.h"
#include "soh/Enhancements/randomizer/SeedContext.h"
#include "soh/Enhancements/randomizer/static_data.h"
#include "soh/ShipInit.hpp"
#include "soh/frame_interpolation.h"
#include "CompanionTuning.h"
#include "CompanionRig.h"
#include "CompanionClips.h"
#include "Companions.h"

extern "C" {
#include "z64.h"
#include "macros.h"
#include "src/overlays/actors/ovl_En_Box/z_en_box.h"
#include "functions.h"
#include "variables.h"
#include "src/overlays/actors/ovl_En_Arrow/z_en_arrow.h"
extern PlayState* gPlayState;
}

#define COMP_CVAR(name) CVAR_ENHANCEMENT("Companions." name)
#define COMP_TEXT_ID 0x0207
#define COMP_FPS 20

// ---- Who they are ----

enum CompWho { COMP_SARIA, COMP_RUTO, COMP_MALON, COMP_ZELDA, COMP_COUNT };

// One look and personality: Saria, then young and adult Ruto, Malon and Zelda
enum CompPersona { P_SARIA, P_RUTO_Y, P_RUTO_A, P_MALON_Y, P_MALON_A, P_ZELDA_Y, P_ZELDA_A, P_COUNT };

enum CompMove {
    MOVE_NONE,
    MOVE_SEED,   // a volley of slingshot seeds (Saria)
    MOVE_NUT,    // a Deku Nut flash that stuns (Saria up close, young Zelda)
    MOVE_ICE,    // an ice arrow (adult Ruto: Zora's Fountain's ice)
    MOVE_LIGHT,  // a Light Arrow (adult Zelda)
    MOVE_CUCCO,  // angry cuccos sent at the enemy (Malon)
    MOVE_SPLASH, // a running water splash slam (Ruto)
    MOVE_SONG,   // a song that heals Link a little (Saria's Song)
    MOVE_MILK,   // Lon Lon Milk for Link (Malon)
    MOVE_SHIELD, // Zelda's Lullaby / Nayru's Love: Link can't be hurt for a while (Zelda)
};

struct PersonaDef {
    const char* name;
    u8 r, g, b; // her name's colour in the bubble
    const char* skel;
    f32 scale;
    f32 height; // the top of her head, in her units, for the bubble
    const char* idle;
    const char* move;    // walk or run; nullptr: she has none in the game, her legs are swung by hand
    f32 moveAnimSpeed;   // the move animation's speed per unit of her speed
    const char* swim;    // nullptr: she doesn't swim, she waits on the shore
    const char* cast;    // ranged attack
    const char* melee;   // melee attack
    const char* support; // song, milk, prayer
    const char* happy;
    const char* wave;
    const char* pout;
    const char* sit;
    const char* worried;
    const char* eyes[4];   // open, half, closed, (special)
    u8 eyeSegA, eyeSegB;   // 0: none
    const char* mouths[3]; // neutral, happy/talking, sad
    u8 mouthSeg;           // 0: none
    u32 hideLimbs;         // limbs the vanilla actor never draws (props), as bits
    u8 hairLimb;           // adult Zelda's hair matrices are made at this limb
    u8 legs[4];            // left thigh, left shin, right thigh, right shin (the hand-made walk), 0: none
    u16 voiceAttack, voiceHappy, voiceHurt;
    CompMove mRanged, mClose, mMelee, mSupport;
};

#define OTR(o, n) "__OTR__objects/" o "/" n

static const PersonaDef sDefs[P_COUNT] = {
    // Saria
    { "Saria",
      120,
      230,
      110,
      OTR("object_sa", "gSariaSkel"),
      0.01f,
      5200.0f,
      OTR("object_sa", "gSariaWaitArmsToSideAnim"),
      OTR("object_sa", "gSariaRunAnim"),
      0.22f,
      nullptr,
      OTR("object_sa", "gSariaExtendRightArmAnim"),
      OTR("object_sa", "gSariaExtendRightArmAnim"),
      OTR("object_sa", "gSariaPlayingOcarinaAnim"),
      OTR("object_sa", "gSariaHandsOnFaceAnim"),
      OTR("object_sa", "gSariaWaveAnim"),
      OTR("object_sa", "gSariaStandHandsOnHipsAnim"),
      OTR("object_sa", "gSariaSitting1Anim"),
      OTR("object_sa", "gSariaHandsBehindBackWaitAnim"),
      { OTR("object_sa", "gSariaEyeOpenTex"), OTR("object_sa", "gSariaEyeHalfTex"),
        OTR("object_sa", "gSariaEyeClosedTex"), OTR("object_sa", "gSariaEyeSadTex") },
      0x08,
      0x09,
      { OTR("object_sa", "gSariaMouthClosed2Tex"), OTR("object_sa", "gSariaMouthSmilingOpenTex"),
        OTR("object_sa", "gSariaMouthFrowningTex") },
      0x0A,
      0,
      0,
      { 0, 0, 0, 0 },
      NA_SE_IT_DEKU,
      NA_SE_EV_FAIRY_DASH,
      0,
      MOVE_SEED,
      MOVE_NUT,
      MOVE_NONE,
      MOVE_SONG },
    // Young Ruto
    { "Ruto",
      120,
      190,
      255,
      OTR("object_ru1", "gRutoChildSkel"),
      0.01f,
      4600.0f,
      OTR("object_ru1", "gRutoChildWaitHandsOnHipsAnim"),
      OTR("object_ru1", "gRutoChildWalkAnim"),
      0.35f,
      OTR("object_ru1", "gRutoChildSwimOnBackAnim"),
      OTR("object_ru1", "gRutoChildHoldArmsUpAnim"),
      OTR("object_ru1", "gRutoChildHoldArmsUpAnim"),
      OTR("object_ru1", "gRutoChildBringArmsUpAnim"),
      OTR("object_ru1", "gRutoChildSquirmAnim"),
      OTR("object_ru1", "gRutoChildBringArmsUpAnim"),
      OTR("object_ru1", "gRutoChildTurnAroundAnim"),
      OTR("object_ru1", "gRutoChildSittingAnim"),
      OTR("object_ru1", "gRutoChildWaitHandsBehindBackAnim"),
      { OTR("object_ru1", "gRutoChildEyeOpenTex"), OTR("object_ru1", "gRutoChildEyeHalfTex"),
        OTR("object_ru1", "gRutoChildEyeClosedTex"), OTR("object_ru1", "gRutoChildEyeHalfWithBlushTex") },
      0x08,
      0,
      { OTR("object_ru1", "gRutoChildMouthClosedTex"), OTR("object_ru1", "gRutoChildMouthOpenTex"),
        OTR("object_ru1", "gRutoChildMouthFrownTex") },
      0x09,
      0,
      0,
      { 0, 0, 0, 0 },
      NA_SE_VO_RT_THROW,
      NA_SE_VO_RT_LAUGH_0,
      NA_SE_VO_RT_FALL,
      MOVE_NONE,
      MOVE_NONE,
      MOVE_SPLASH,
      MOVE_NONE },
    // Adult Ruto
    { "Ruto",
      120,
      190,
      255,
      OTR("object_ru2", "gAdultRutoSkel"),
      0.01f,
      6600.0f,
      OTR("object_ru2", "gAdultRutoIdleAnim"),
      nullptr,
      0.0f,
      OTR("object_ru2", "gAdultRutoSwimmingUpAnim"),
      OTR("object_ru2", "gAdultRutoRaisingArmsUpAnim"),
      OTR("object_ru2", "gAdultRutoRaisingArmsUpAnim"),
      OTR("object_ru2", "gAdultRutoRaisingArmsUpAnim"),
      OTR("object_ru2", "gAdultRutoIdleHandsOnHipsAnim"),
      OTR("object_ru2", "gAdultRutoRaisingArmsUpAnim"),
      OTR("object_ru2", "gAdultRutoCrossingArmsAnim"),
      nullptr,
      OTR("object_ru2", "gAdultRutoLookingDownLeftAnim"),
      { OTR("object_ru2", "gAdultRutoEyeOpenTex"), OTR("object_ru2", "gAdultRutoEyeHalfTex"),
        OTR("object_ru2", "gAdultRutoEyeClosedTex"), OTR("object_ru2", "gAdultRutoEyeHalfTex") },
      0x08,
      0x09,
      { nullptr, nullptr, nullptr },
      0,
      0,
      0,
      { 0, 0, 0, 0 },
      NA_SE_VO_RT_THROW,
      NA_SE_VO_RT_LAUGH_0,
      NA_SE_VO_RT_FALL,
      MOVE_ICE,
      MOVE_NONE,
      MOVE_SPLASH,
      MOVE_NONE },
    // Young Malon
    { "Malon",
      255,
      150,
      90,
      OTR("object_ma1", "gMalonChildSkel"),
      0.01f,
      4800.0f,
      OTR("object_ma1", "gMalonChildIdleAnim"),
      nullptr,
      0.0f,
      nullptr,
      OTR("object_ma1", "gMalonChildRaiseHandsAnim"),
      OTR("object_ma1", "gMalonChildRaiseHandsAnim"),
      OTR("object_ma1", "gMalonChildSingAnim"),
      OTR("object_ma1", "gMalonChildSingAnim"),
      OTR("object_ma1", "gMalonChildRaiseHandsAnim"),
      nullptr,
      nullptr,
      nullptr,
      { OTR("object_ma1", "gMalonChildEyeOpenTex"), OTR("object_ma1", "gMalonChildEyeHalfTex"),
        OTR("object_ma1", "gMalonChildEyeClosedTex"), OTR("object_ma1", "gMalonChildEyeClosedTex") },
      0x08,
      0,
      { OTR("object_ma1", "gMalonChildNeutralMouthTex"), OTR("object_ma1", "gMalonChildTalkingMouthTex"),
        OTR("object_ma1", "gMalonChildNeutralMouthTex") },
      0x09,
      (1u << 2) | (1u << 5),
      0,
      { 0, 0, 0, 0 },
      NA_SE_EV_CHICKEN_CRY_A,
      0,
      0,
      MOVE_CUCCO,
      MOVE_NONE,
      MOVE_NONE,
      MOVE_MILK },
    // Adult Malon
    { "Malon",
      255,
      150,
      90,
      OTR("object_ma2", "gMalonAdultSkel"),
      0.01f,
      6200.0f,
      OTR("object_ma2", "gMalonAdultIdleAnim"),
      nullptr,
      0.0f,
      nullptr,
      OTR("object_ma2", "gMalonAdultStandStillAnim"),
      OTR("object_ma2", "gMalonAdultStandStillAnim"),
      OTR("object_ma2", "gMalonAdultSingAnim"),
      OTR("object_ma2", "gMalonAdultSingAnim"),
      OTR("object_ma2", "gMalonAdultSingToIdleAnim"),
      nullptr,
      nullptr,
      OTR("object_ma2", "gMalonAdultStandStillAnim"),
      { OTR("object_ma2", "gMalonAdultEyeOpenTex"), OTR("object_ma2", "gMalonAdultEyeHalfTex"),
        OTR("object_ma2", "gMalonAdultEyeClosedTex"), OTR("object_ma2", "gMalonAdultEyeClosedTex") },
      0x08,
      0,
      { OTR("object_ma2", "gMalonAdultMouthNeutralTex"), OTR("object_ma2", "gMalonAdultMouthHappyTex"),
        OTR("object_ma2", "gMalonAdultMouthSadTex") },
      0x09,
      0,
      0,
      { 3, 4, 6, 7 },
      NA_SE_EV_CHICKEN_CRY_A,
      0,
      0,
      MOVE_CUCCO,
      MOVE_NONE,
      MOVE_NONE,
      MOVE_MILK },
    // Young Zelda
    { "Zelda",
      240,
      200,
      255,
      OTR("object_zl4", "gChildZeldaSkel"),
      0.01f,
      5000.0f,
      OTR("object_zl4", "gChildZeldaAnim_000654"),
      nullptr,
      0.0f,
      nullptr,
      OTR("object_zl4", "gChildZeldaAnim_01BCF0"),
      OTR("object_zl4", "gChildZeldaAnim_01BCF0"),
      OTR("object_zl4", "gChildZeldaAnim_01910C"),
      OTR("object_zl4", "gChildZeldaAnim_01C494"),
      OTR("object_zl4", "gChildZeldaAnim_01C7B0"),
      OTR("object_zl4", "gChildZeldaAnim_013EA0"),
      nullptr,
      OTR("object_zl4", "gChildZeldaAnim_011698"),
      { OTR("object_zl4", "gChildZeldaEyeOpenTex"), OTR("object_zl4", "gChildZeldaEyeBlinkTex"),
        OTR("object_zl4", "gChildZeldaEyeShutTex"), OTR("object_zl4", "gChildZeldaEyeWideTex") },
      0x08,
      0x09,
      { OTR("object_zl4", "gChildZeldaMouthNeutralTex"), OTR("object_zl4", "gChildZeldaMouthHappyTex"),
        OTR("object_zl4", "gChildZeldaMouthWorriedTex") },
      0x0A,
      (1u << 3) | (1u << 4) | (1u << 5) | (1u << 6),
      0,
      { 0, 0, 0, 0 },
      NA_SE_VO_Z0_THROW,
      NA_SE_VO_Z0_SMILE_0,
      NA_SE_VO_Z0_SURPRISE,
      MOVE_NUT,
      MOVE_NUT,
      MOVE_NONE,
      MOVE_SHIELD },
    // Adult Zelda
    { "Zelda",
      240,
      200,
      255,
      OTR("object_zl2", "gZelda2Skel"),
      0.01f,
      6400.0f,
      OTR("object_zl2_anime2", "gZelda2Anime2Anim_009FBC"),
      OTR("object_zl2_anime2", "gZelda2Anime2Anim_009BE4"),
      0.2f,
      nullptr,
      OTR("object_zl2_anime2", "gZelda2Anime2Anim_003FF8"),
      OTR("object_zl2_anime2", "gZelda2Anime2Anim_003FF8"),
      OTR("object_zl2_anime2", "gZelda2Anime2Anim_008050"),
      OTR("object_zl2_anime2", "gZelda2Anime2Anim_008050"),
      OTR("object_zl2_anime2", "gZelda2Anime2Anim_003FF8"),
      nullptr,
      nullptr,
      OTR("object_zl2_anime2", "gZelda2Anime2Anim_007664"),
      { OTR("object_zl2", "gZelda2EyeOpenTex"), OTR("object_zl2", "gZelda2EyeHalfTex"),
        OTR("object_zl2", "gZelda2EyeShutTex"), OTR("object_zl2", "gZelda2EyeHalfTex") },
      0x08,
      0x09,
      { OTR("object_zl2", "gZelda2MouthSeriousTex"), OTR("object_zl2", "gZelda2MouthHappyTex"),
        OTR("object_zl2", "gZelda2MouthOpenTex") },
      0x0A,
      0,
      14,
      { 0, 0, 0, 0 },
      NA_SE_VO_Z1_SURPRISE,
      0,
      NA_SE_VO_Z1_PAIN,
      MOVE_LIGHT,
      MOVE_NONE,
      MOVE_NONE,
      MOVE_SHIELD },
};

static const char* sWhoKey[COMP_COUNT] = { "Saria", "Ruto", "Malon", "Zelda" };
static const char* sPersonaKey[P_COUNT] = { "saria",       "ruto_young",  "ruto_adult", "malon_young",
                                            "malon_adult", "zelda_young", "zelda_adult" };

static CompPersona PersonaFor(int who, bool adult) {
    switch (who) {
        case COMP_SARIA:
            return P_SARIA;
        case COMP_RUTO:
            return adult ? P_RUTO_A : P_RUTO_Y;
        case COMP_MALON:
            return adult ? P_MALON_A : P_MALON_Y;
        default:
            return adult ? P_ZELDA_A : P_ZELDA_Y;
    }
}

// ---- Link's walk on them ----
// Their own skeletons take Link's walk and run: each frame a hidden copy of Link's skeleton plays his walk, and how
// far each of his leg and arm joints has turned from his standing pose is added to the same joint of hers (limbs
// from the game's asset lists, object_*.yml). Order: left thigh, shin, foot; right thigh, shin, foot; left shoulder,
// arm; right shoulder, arm (0: none). Live in companion_tuning.json: "companion.<persona>.walk_map" (10 numbers),
// "companion.<persona>.walk_sign" [legs, arms] (1 or -1), "companion.walk_mix".
static const u8 sLinkLimbs[10] = { 7, 8, 9, 4, 5, 6, 14, 15, 17, 18 };
static const u8 sWalkMap[P_COUNT][10] = {
    { 3, 4, 5, 6, 7, 8, 10, 11, 13, 14 }, // Saria
    { 2, 3, 4, 5, 6, 7, 9, 10, 12, 13 },  // young Ruto
    { 2, 3, 4, 6, 7, 8, 11, 14, 16, 19 }, // adult Ruto
    { 2, 3, 4, 5, 6, 7, 9, 10, 12, 13 },  // young Malon
    { 3, 4, 5, 6, 7, 8, 12, 13, 15, 16 }, // adult Malon
    { 0, 0, 0, 0, 0, 0, 11, 12, 14, 15 }, // young Zelda (unnamed limbs: arms guessed, the dress hides her legs)
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },     // adult Zelda (she has her own run)
};

// The bones Mixamo motion turns (limb numbers from the asset lists): L thigh, shin, foot; R thigh, shin, foot; L upper
// arm, forearm, hand; R upper arm, forearm, hand. Zelda's limbs have no names: hers are found from her skeleton's
// shape when she appears. Live: "companion.<persona>.rig" (12 numbers)
static const CompRigMap sRigMap[P_COUNT] = {
    { 3, 4, 5, 6, 7, 8, 10, 11, 12, 13, 14, 15 }, // Saria
    { 2, 3, 4, 5, 6, 7, 9, 10, 11, 12, 13, 14 },  // young Ruto
    { 2, 3, 4, 6, 7, 8, 11, 14, 15, 16, 19, 20 }, // adult Ruto
    { 2, 3, 4, 5, 6, 7, 9, 10, 11, 12, 13, 14 },  // young Malon
    { 3, 4, 5, 6, 7, 8, 12, 13, 14, 15, 16, 17 }, // adult Malon
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },       // young Zelda (found from her shape)
    { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0 },       // adult Zelda (found from her shape)
};

// Skirt limbs (widened as her legs swing so her knees stay inside): adult Malon's dress, young Malon's dress (its
// middle, which carries the lower part). Live: "companion.<persona>.skirt" [limb, limb]
static const u8 sSkirt[P_COUNT][2] = { { 0, 0 }, { 0, 0 }, { 0, 0 }, { 17, 0 }, { 10, 0 }, { 0, 0 }, { 0, 0 } };

// Her side of a hug: arms out, or the closest the game has
static const char* sHugAnim[P_COUNT] = {
    OTR("object_sa", "gSariaHandsOutAnim"),
    OTR("object_ru1", "gRutoChildHoldArmsUpAnim"),
    OTR("object_ru2", "gAdultRutoRaisingArmsUpAnim"),
    OTR("object_ma1", "gMalonChildRaiseHandsAnim"),
    OTR("object_ma2", "gMalonAdultStandStillAnim"),
    OTR("object_zl4", "gChildZeldaAnim_01910C"),
    OTR("object_zl2_anime2", "gZelda2Anime2Anim_008050"),
};
// Link's side: arms forward (companion_tuning.json "companion.hug_link_anim": 0-3)
static const char* sLinkHugAnims[] = {
    "__OTR__objects/gameplay_keep/gPlayerAnim_link_normal_wait",
    "__OTR__objects/gameplay_keep/gPlayerAnim_link_demo_kakeyori_wait",
    "__OTR__objects/gameplay_keep/gPlayerAnim_link_normal_carry",
    "__OTR__objects/gameplay_keep/gPlayerAnim_link_demo_look_hand_wait",
};
static const char* const LINK_WALK = "__OTR__objects/gameplay_keep/gPlayerAnim_link_normal_walk";
static const char* const LINK_RUN = "__OTR__objects/gameplay_keep/gPlayerAnim_link_normal_run";
static const char* const LINK_WAIT = "__OTR__objects/gameplay_keep/gPlayerAnim_link_normal_wait";

// ---- What they say ----
// Built in, and anything in companion_lines.json added on top: { "saria": { "idle": ["...", ...] }, ... ,
// "banter": [ { "a": "saria", "b": "ruto_young", "lines": ["...", "...", ...] } ] } (lines alternate a, b, a...)

enum LineKind {
    L_ENTER,
    L_IDLE,
    L_LOW_HEALTH,
    L_CHEER,
    L_BOSS,
    L_NIGHT,
    L_ATTACK,
    L_HURT,
    L_KO,
    L_REVIVE,
    L_SUPPORT,
    L_CHAT,
    L_TALK,
    L_KEY_HERE,
    L_KEY_NEAR,
    L_KEY_NONE,
    L_KEY_HELD,
    L_DUNGEON,
    L_WATER,
    L_WAIT,
    L_PROMPT,
    L_HUG,
    L_HUG_SEEN,
    L_HOLD,
    L_LET_GO,
    L_LOOKED_AT,
    L_SONG_START,  // the one who performs, sitting down to play or sing
    L_SONG_LISTEN, // someone listening, partway through
    L_SONG_END,    // the performer, when it's over
    L_SONG_PRAISE, // the listeners, when it's over
    L_SEE_CHEST,   // exploring: what she found
    L_SEE_SIGN,
    L_SEE_POT,
    L_SEE_NPC,
    L_SEE_DOOR,
    L_SEE_SKULL,
    L_SEE_STONE,
    L_SEE_ITEM,
    L_SEE_ANIMAL,
    L_KIND_COUNT
};
static const char* sKindKey[L_KIND_COUNT] = { "enter",  "idle",     "low_health", "cheer",    "boss",     "night",
                                              "attack", "hurt",     "ko",         "revive",   "support",  "chat",
                                              "talk",   "key_here", "key_near",   "key_none", "key_held", "dungeon",
                                              "water",  "wait",     "prompt",     "hug",      "hug_seen", "hold",
                                              "let_go", "looked_at", "song_start", "song_listen", "song_end", "song_praise",
                                              "see_chest", "see_sign", "see_pot", "see_npc", "see_door", "see_skull",
                                              "see_stone", "see_item", "see_animal" };

typedef std::vector<std::string> Lines;
static Lines sLines[P_COUNT][L_KIND_COUNT];
struct Banter {
    int a, b;
    Lines lines;
};
static std::vector<Banter> sBanter;
static std::map<int, std::map<int, Lines>> sSceneLines; // scene -> persona -> lines

static void Add(int p, int k, std::initializer_list<const char*> l) {
    for (const char* s : l) {
        sLines[p][k].push_back(s);
    }
}

static void AddScene(int scene, int p, std::initializer_list<const char*> l) {
    for (const char* s : l) {
        sSceneLines[scene][p].push_back(s);
    }
}

static void AddBanter(int a, int b, std::initializer_list<const char*> l) {
    Banter t;
    t.a = a;
    t.b = b;
    for (const char* s : l) {
        t.lines.push_back(s);
    }
    sBanter.push_back(t);
}

// %d: how many small keys Link holds. %s: where a key is (a check's name, on a randomizer file)
static void BuildLines() {
    // Saria: his oldest friend, warm and teasing, a forest girl who's never far from her ocarina
    Add(P_SARIA, L_ENTER,
        { "Hee hee, where are we off to now, Link?", "Stay close, okay? I don't want to lose you.",
          "It's so different out here from the forest...", "Do you hear that? Even the wind sounds new here." });
    Add(P_SARIA, L_IDLE,
        { "Link? Are you daydreaming again?", "If we're resting, I'll play you something.",
          "You always did stand still like that when you were thinking.",
          "Hmm hmm hmm... sorry, that song's stuck in my head." });
    Add(P_SARIA, L_LOW_HEALTH,
        { "Link, you're hurt! Let me help!", "Please be careful... I can't lose you.",
          "Breathe, Link. I'm right here." });
    Add(P_SARIA, L_CHEER,
        { "We did it!", "Hee hee, they didn't stand a chance!", "That's my Link!",
          "Gone! The forest would be proud." });
    Add(P_SARIA, L_BOSS,
        { "Link... you were amazing. I knew you could do it!", "It's over... I was so scared, but you never were." });
    Add(P_SARIA, L_NIGHT,
        { "The stars are out... it's a little scary without the trees around us.",
          "At night the forest sings. Out here it's so quiet." });
    Add(P_SARIA, L_ATTACK, { "Take that!", "Seeds away!", "Over here!", "Leave him alone!" });
    Add(P_SARIA, L_HURT, { "Ow!", "Hey!", "That stings!" });
    Add(P_SARIA, L_KO, { "I'm... okay... just need a minute...", "Ooh... my head..." });
    Add(P_SARIA, L_REVIVE, { "I'm back! Did you miss me?", "Okay! I'm alright now!" });
    Add(P_SARIA, L_SUPPORT, { "Listen... this song will help you.", "Let the forest's song heal you, Link." });
    Add(P_SARIA, L_CHAT,
        { "Do you remember the first time you tried to climb the Deku Tree? You got stuck for an hour.",
          "Mido would turn green if he saw me out here! Well, greener.",
          "I wonder if the Kokiri miss us. I bet they do.",
          "Whenever you feel lonely, just play my song. I'll always hear it.",
          "You know... I'm really glad you let me come along.",
          "The world is so big, Link. And you're not alone in it." });
    Add(P_SARIA, L_TALK,
        { "We'll always be friends, Link. Wherever you go, I'm with you.^...Even if it's somewhere "
          "without any trees. Hee hee!",
          "Don't worry about me, okay? I'm tougher than I look.^Fairies don't follow everyone, and "
          "neither do I!",
          "I used to think the forest was the whole world.^Now I think the whole world could be "
          "a forest, if you let it grow." });
    Add(P_SARIA, L_KEY_HERE,
        { "Link, I can feel it... there's a key right here in this room!",
          "Wait! There's a key close by. I'm sure of it!" });
    Add(P_SARIA, L_KEY_NEAR,
        { "The forest spirits are whispering... a key is waiting at the %s.",
          "I have a feeling about the %s. Let's check there!" });
    Add(P_SARIA, L_KEY_NONE,
        { "I don't sense any more keys hiding here. You found them all, Link!", "Hmm... no more keys here, I think." });
    Add(P_SARIA, L_KEY_HELD, { "You're carrying %d small key(s) for this place. Don't lose them!" });
    Add(P_SARIA, L_DUNGEON, { "It's so dark in here... stay close to me.", "Something evil is here. I can feel it." });
    Add(P_SARIA, L_WATER, { "I'll wait right here! Kokiri don't swim very well..." });
    Add(P_SARIA, L_WAIT, { "Link? Wait for me!", "Hey, slow down!" });

    // Young Ruto: a proud, bossy princess who has very much decided Link is her fiancé
    Add(P_RUTO_Y, L_ENTER,
        { "Hmph. I suppose this place will do.", "Lead the way, my fiancé. I'll allow it.",
          "This place is so DRY. How do you people live like this?" });
    Add(P_RUTO_Y, L_IDLE,
        { "Are you going to stand there all day? A princess has places to be!",
          "If you're not going to move, at least carry me.",
          "Hmph. I'm not waiting for you. I just happen to be standing here." });
    Add(P_RUTO_Y, L_LOW_HEALTH,
        { "You can't fall now! Who would I marry?!", "Pull yourself together, Link!",
          "Don't you dare get hurt! That's an order!" });
    Add(P_RUTO_Y, L_CHEER,
        { "Ha! Did you see that? I helped!", "That's what you get for bothering a princess!",
          "Not bad, my fiancé. Not bad at all." });
    Add(P_RUTO_Y, L_BOSS, { "You did it! I knew I chose well!", "Hmph... I suppose you're a little bit heroic." });
    Add(P_RUTO_Y, L_NIGHT, { "It's dark. Not that I'm scared! Zoras are never scared. Stay close anyway." });
    Add(P_RUTO_Y, L_ATTACK, { "Hiyaaah!", "Splash!", "Out of my way!", "How dare you!" });
    Add(P_RUTO_Y, L_HURT, { "Eek!", "How DARE you!", "Ow! That's royal skin!" });
    Add(P_RUTO_Y, L_KO, { "I'm... not... crying...", "Carry me... that's an order..." });
    Add(P_RUTO_Y, L_REVIVE, { "Hmph! As if that could stop me!", "I'm fine! Stop looking at me like that!" });
    Add(P_RUTO_Y, L_CHAT,
        { "Daddy would faint if he knew I was out here. That's half the fun.",
          "When we're married, you'll live in Zora's Domain. I've already picked our room.",
          "Lord Jabu-Jabu was always nice to me... until he ate me. Twice.",
          "You're lucky, you know. Most Hylians never get to see a Zora princess this close.",
          "Don't tell anyone, but... it's kind of nice not being alone." });
    Add(P_RUTO_Y, L_TALK,
        { "What? You want to talk to me? Well... I suppose that's allowed.^You did accept my "
          "Zora's Sapphire, after all. You know what that means, right?",
          "When I'm queen, I'll make a rule that everyone has to swim every day.^...You'll be "
          "excused. Maybe." });
    Add(P_RUTO_Y, L_KEY_HERE, { "Look! There's a key in this room! Even a Hylian could find it now." });
    Add(P_RUTO_Y, L_KEY_NEAR,
        { "A princess knows these things: there's a key at the %s. Go get it!",
          "Try the %s. Don't ask me how I know. Just go!" });
    Add(P_RUTO_Y, L_KEY_NONE, { "I think you've taken every key there is. Greedy!" });
    Add(P_RUTO_Y, L_KEY_HELD, { "You have %d key(s). Try not to drop them like you dropped me." });
    Add(P_RUTO_Y, L_DUNGEON, { "Ugh, it smells in here.", "Stay in front of me. That's what fiancés are for." });
    Add(P_RUTO_Y, L_WATER, { "Finally, some water! Try to keep up, Hylian!" });
    Add(P_RUTO_Y, L_WAIT, { "Hey! Don't you leave me behind!", "Wait for your princess!" });

    // Adult Ruto: grown, proud, a sage now, still fond of him and not letting go of the engagement joke
    Add(P_RUTO_A, L_ENTER,
        { "Lead on, Link. The Sage of Water walks with you.", "So this is where your road goes now." });
    Add(P_RUTO_A, L_IDLE,
        { "Taking a moment? Good. Even heroes need still water.",
          "You still stand around exactly like you did when you were little." });
    Add(P_RUTO_A, L_LOW_HEALTH, { "Link! Fall back, I'll cover you!", "Don't make me drag you out of here!" });
    Add(P_RUTO_A, L_CHEER, { "Ha! Like ice on a summer day.", "That's how a Zora fights!" });
    Add(P_RUTO_A, L_BOSS, { "Well done, Link. I always knew you'd grow into it." });
    Add(P_RUTO_A, L_NIGHT, { "The night reminds me of the Domain under the ice. Quiet. Cold. Waiting." });
    Add(P_RUTO_A, L_ATTACK, { "Freeze!", "Feel the cold of the Fountain!", "Back!" });
    Add(P_RUTO_A, L_HURT, { "Hngh!", "Careless...", "You'll pay for that!" });
    Add(P_RUTO_A, L_KO, { "Just... a moment... to catch my breath..." });
    Add(P_RUTO_A, L_REVIVE, { "I'm back on my feet. A Zora doesn't stay down." });
    Add(P_RUTO_A, L_CHAT,
        { "You remember the promise, don't you? I haven't forgotten. Not that I'd hold you to it. "
          "Much.",
          "My people are still frozen under that ice... every step we take is for them.",
          "Seven years, and you came back. I never stopped hoping you would.",
          "Being a sage is lonely work. Walking with you makes it less so." });
    Add(P_RUTO_A, L_TALK,
        { "Link... I gave you my most precious treasure once. I'd do it again.^The Zora are proud "
          "people, but I'm not too proud to say I'm glad you're here." });
    Add(P_RUTO_A, L_KEY_HERE, { "There's a key somewhere in this very room. I can see it from here." });
    Add(P_RUTO_A, L_KEY_NEAR, { "If I were a key, I'd hide at the %s. Shall we look?" });
    Add(P_RUTO_A, L_KEY_NONE, { "No more keys here, I think. You've been thorough." });
    Add(P_RUTO_A, L_KEY_HELD, { "You're holding %d key(s). Use them wisely." });
    Add(P_RUTO_A, L_DUNGEON, { "Stay sharp. Places like this were built to drown the careless." });
    Add(P_RUTO_A, L_WATER, { "Ahh... now we're in my element." });
    Add(P_RUTO_A, L_WAIT, { "Link, wait!" });

    // Young Malon: the ranch girl, cheerful, sings to the horses, loves Epona
    Add(P_MALON_Y, L_ENTER,
        { "Wow! I've never been this far from the ranch!", "Dad would never believe where I am right now!",
          "Fairy boy! Where are we going next?" });
    Add(P_MALON_Y, L_IDLE,
        { "Fairy boy? Are you lost?", "La la la... sorry, I always sing when it's quiet.",
          "Do you wanna rest? The cows always rest after lunch." });
    Add(P_MALON_Y, L_LOW_HEALTH, { "Fairy boy, you're hurt! Drink some milk!", "Hang on! I've got milk!" });
    Add(P_MALON_Y, L_CHEER,
        { "Yay! Get lost, you meanie!", "Hee hee, my cuccos did that!", "Wow, fairy boy, you're strong!" });
    Add(P_MALON_Y, L_BOSS, { "You beat it! Wait 'til I tell Epona!" });
    Add(P_MALON_Y, L_NIGHT, { "At night Dad snores so loud the cows can't sleep. It's so quiet out here..." });
    Add(P_MALON_Y, L_ATTACK, { "Go, cuccos!", "Get 'em!", "Cuccooo!" });
    Add(P_MALON_Y, L_HURT, { "Ouch!", "Hey, that's mean!" });
    Add(P_MALON_Y, L_KO, { "Ow... I wanna go home..." });
    Add(P_MALON_Y, L_REVIVE, { "I'm okay! Ranch girls are tough!" });
    Add(P_MALON_Y, L_SUPPORT, { "Here, fairy boy! Fresh Lon Lon Milk!", "Drink up! It's good for you!" });
    Add(P_MALON_Y, L_CHAT,
        { "Epona's gonna be the fastest horse in Hyrule. You'll see!",
          "Ingo's always grumpy. I think he needs a cucco hug.",
          "Did you know cows give better milk when you sing to them? It's true!",
          "My mom taught me Epona's Song. I sing it every day.",
          "Being out here with you is like a big adventure! Is this what you do every day?" });
    Add(P_MALON_Y, L_TALK,
        { "You know, fairy boy, you're the first boy who ever listened to me sing.^That makes you "
          "a friend of the ranch forever!" });
    Add(P_MALON_Y, L_KEY_HERE, { "Fairy boy, look around! I bet there's a key in this room!" });
    Add(P_MALON_Y, L_KEY_NEAR, { "I think I saw a key over by the %s! Let's go look!" });
    Add(P_MALON_Y, L_KEY_NONE, { "No more keys, I think! You found them all!" });
    Add(P_MALON_Y, L_KEY_HELD, { "You've got %d key(s)! Keep 'em safe like eggs!" });
    Add(P_MALON_Y, L_DUNGEON, { "It's spooky in here... can I hold your hand?" });
    Add(P_MALON_Y, L_WATER, { "I can't swim that good! I'll wait here, okay?" });
    Add(P_MALON_Y, L_WAIT, { "Fairy boy! Wait up!" });

    // Adult Malon: kind and steady, carrying the ranch through hard years
    Add(P_MALON_A, L_ENTER,
        { "It feels good to get away from the ranch for a while.", "So this is what's out past the fields." });
    Add(P_MALON_A, L_IDLE,
        { "Take your time. The ranch taught me patience.", "Mm... it's nice just standing here with you." });
    Add(P_MALON_A, L_LOW_HEALTH, { "Link, you're hurt! Here, drink this!", "Hold on, I've got milk!" });
    Add(P_MALON_A, L_CHEER, { "That'll teach 'em!", "Good job, Link!", "My cuccos never miss." });
    Add(P_MALON_A, L_BOSS, { "You did it, Link. You really did it." });
    Add(P_MALON_A, L_NIGHT, { "Nights like this I'd sit with Epona and sing. Seems so long ago now." });
    Add(P_MALON_A, L_ATTACK, { "Go, cuccos!", "Shoo!", "Leave him alone!" });
    Add(P_MALON_A, L_HURT, { "Ah!", "Ouch!" });
    Add(P_MALON_A, L_KO, { "I'll... be fine... go on..." });
    Add(P_MALON_A, L_REVIVE, { "I'm up! Takes more than that to stop a ranch girl." });
    Add(P_MALON_A, L_SUPPORT, { "Here, Link, Lon Lon Milk. The best in Hyrule.", "Drink up. You need it." });
    Add(P_MALON_A, L_CHAT,
        { "Ingo's not so bad, you know. He's just lost.", "Dad came back, did I tell you? The ranch feels alive again.",
          "I still sing Epona's Song every morning. I like to think she hears it, wherever she is.",
          "When this is all over... will you come visit the ranch sometimes?" });
    Add(P_MALON_A, L_TALK,
        { "You left us so suddenly, seven years ago. I kept singing anyway.^And here you are. "
          "Funny how the song brought you back." });
    Add(P_MALON_A, L_KEY_HERE, { "Link, there's a key around here. Look sharp!" });
    Add(P_MALON_A, L_KEY_NEAR, { "Try the %s. Call it a ranch girl's hunch." });
    Add(P_MALON_A, L_KEY_NONE, { "I think we've cleaned this place out of keys." });
    Add(P_MALON_A, L_KEY_HELD, { "You've got %d key(s) for this place." });
    Add(P_MALON_A, L_DUNGEON, { "I don't like it in here. Let's be quick." });
    Add(P_MALON_A, L_WATER, { "I'll wait for you here, Link!" });
    Add(P_MALON_A, L_WAIT, { "Link, wait for me!" });

    // Young Zelda: the princess who dreamed of him, curious, clever, sees more than she says
    Add(P_ZELDA_Y, L_ENTER,
        { "I've only ever seen this place from my window...",
          "Impa would scold me terribly for coming here. Let's not tell her." });
    Add(P_ZELDA_Y, L_IDLE, { "Is something the matter, Link?", "You're thinking about my dream, aren't you?" });
    Add(P_ZELDA_Y, L_LOW_HEALTH, { "Link! Let me protect you!", "Please be careful! Hyrule needs you!" });
    Add(P_ZELDA_Y, L_CHEER, { "Well done, Link!", "Just as I hoped.", "Impa taught me that trick!" });
    Add(P_ZELDA_Y, L_BOSS, { "You did it! My dream was right about you." });
    Add(P_ZELDA_Y, L_NIGHT, { "At night I used to watch the stars from the castle and wonder who else was looking." });
    Add(P_ZELDA_Y, L_ATTACK, { "Now!", "Look away!", "Be gone!" });
    Add(P_ZELDA_Y, L_HURT, { "Ah!", "Oh!" });
    Add(P_ZELDA_Y, L_KO, { "I'm... alright... keep going..." });
    Add(P_ZELDA_Y, L_REVIVE, { "I'm fine now. A princess doesn't stay down." });
    Add(P_ZELDA_Y, L_SUPPORT, { "The goddesses will protect you, Link.", "Listen to my lullaby... you're safe now." });
    Add(P_ZELDA_Y, L_CHAT,
        { "In my dream, a boy came out of the forest with a fairy. I knew it was you.",
          "That man from the desert... Ganondorf. I don't trust him at all.",
          "Father never listens to me. You do. That means a lot.",
          "I've never gone this long without a lesson! This is wonderful." });
    Add(P_ZELDA_Y, L_TALK,
        { "Link, may I tell you a secret?^...I was terribly lonely in the castle. Then you came "
          "over the wall.^I'm glad it was you." });
    Add(P_ZELDA_Y, L_KEY_HERE, { "There's a key in this room, Link. I'm certain of it." });
    Add(P_ZELDA_Y, L_KEY_NEAR, { "I had a vision... a key, at the %s." });
    Add(P_ZELDA_Y, L_KEY_NONE, { "I don't think any keys remain in this place." });
    Add(P_ZELDA_Y, L_KEY_HELD, { "You have %d key(s) for this place, Link." });
    Add(P_ZELDA_Y, L_DUNGEON, { "This place feels... wrong. Stay close." });
    Add(P_ZELDA_Y, L_WATER, { "I'll wait for you here!" });
    Add(P_ZELDA_Y, L_WAIT, { "Link! Please, wait!" });

    // Adult Zelda: the princess after seven years as Sheik, calm, wise, and gentle with him
    Add(P_ZELDA_A, L_ENTER,
        { "Hyrule has changed so much... but it's still ours to save.", "Let's go, Link. Together." });
    Add(P_ZELDA_A, L_IDLE,
        { "Rest a moment, Link. I'll keep watch.", "You were always braver than you looked. I'm glad you still are." });
    Add(P_ZELDA_A, L_LOW_HEALTH, { "Link! Let Nayru shield you!", "Stay behind me!" });
    Add(P_ZELDA_A, L_CHEER, { "Well fought.", "The light is with us.", "Nicely done, Link." });
    Add(P_ZELDA_A, L_BOSS, { "Another evil sealed. Thank you, Link." });
    Add(P_ZELDA_A, L_NIGHT, { "For seven years I hid in the dark. I don't fear the night anymore." });
    Add(P_ZELDA_A, L_ATTACK, { "Light, guide my arrow!", "Begone!", "Now!" });
    Add(P_ZELDA_A, L_HURT, { "Ngh!", "Careless of me..." });
    Add(P_ZELDA_A, L_KO, { "A moment... I just need a moment..." });
    Add(P_ZELDA_A, L_REVIVE, { "I'm with you again." });
    Add(P_ZELDA_A, L_SUPPORT, { "Nayru, protect him!", "Let the goddess's love shield you." });
    Add(P_ZELDA_A, L_CHAT,
        { "When I was Sheik, I followed you everywhere. It's nicer walking beside you.",
          "I'm sorry, Link. For the seven years. For all of it.", "Impa says I should rest. She never could make me.",
          "Whatever happens, remember you weren't alone in this." });
    Add(P_ZELDA_A, L_TALK,
        { "When we were children, I asked you to save Hyrule. I put that weight on you.^You "
          "carried it anyway. Let me carry some of it now." });
    Add(P_ZELDA_A, L_KEY_HERE, { "There's a key in this room, Link. Look carefully." });
    Add(P_ZELDA_A, L_KEY_NEAR, { "The Triforce of Wisdom whispers of the %s." });
    Add(P_ZELDA_A, L_KEY_NONE, { "No keys remain here. We may move on." });
    Add(P_ZELDA_A, L_KEY_HELD, { "You hold %d key(s) for this place." });
    Add(P_ZELDA_A, L_DUNGEON, { "Ganondorf's power lingers here. Be watchful." });
    Add(P_ZELDA_A, L_WATER, { "Go on, Link. I'll meet you on the other side." });
    Add(P_ZELDA_A, L_WAIT, { "Link, wait for me." });

    // Talking to her (the question before Talk / Hug), hugs, and seeing someone else get one
    Add(P_SARIA, L_PROMPT, { "Hee hee, what is it, Link?", "Hm? Do you need something?" });
    Add(P_SARIA, L_HUG,
        { "Hee hee! You give the best hugs, Link.", "Aww... I missed this.", "Just like when we were little!" });
    Add(P_SARIA, L_HUG_SEEN, { "Hee hee, that's sweet!", "Don't forget about me, Link!" });
    Add(P_RUTO_Y, L_PROMPT, { "What is it? Speak, my fiancé.", "Yes? A princess is listening." });
    Add(P_RUTO_Y, L_HUG,
        { "W-what are you doing?! ...Okay. You can hold on a little longer.", "Finally, some proper affection!",
          "Hmph. I suppose I deserve this." });
    Add(P_RUTO_Y, L_HUG_SEEN, { "HEY! He's MY fiancé!", "Excuse me?! Unhand him!", "...Me next. That's an order." });
    Add(P_RUTO_A, L_PROMPT, { "Yes, Link?", "Something on your mind?" });
    Add(P_RUTO_A, L_HUG,
        { "Oh! ...You've gotten taller, haven't you.", "I'll take that as a yes to the engagement.",
          "Mm. Warm, for a Hylian." });
    Add(P_RUTO_A, L_HUG_SEEN, { "Ahem. I'm standing right here.", "Some of us are engaged to him, you know." });
    Add(P_MALON_Y, L_PROMPT, { "Fairy boy? What's up?", "Hi! Wanna talk?" });
    Add(P_MALON_Y, L_HUG,
        { "Yay! Hugs! Epona likes hugs too!", "Hee hee, you're squishy!",
          "Dad says hugs make the cows happy. I think it works on people too!" });
    Add(P_MALON_Y, L_HUG_SEEN, { "Aww! Can I have one too?", "Hee hee! Group hug next!" });
    Add(P_MALON_A, L_PROMPT, { "Yes, Link?", "What is it?" });
    Add(P_MALON_A, L_HUG,
        { "Oh... Link. Thank you. I needed that.", "You smell like hay and adventure.",
          "Seven years I waited for this, you know." });
    Add(P_MALON_A, L_HUG_SEEN, { "Aww, that's sweet.", "Save one for me, Link!" });
    Add(P_ZELDA_Y, L_PROMPT, { "Yes, Link? What is it?", "Did you wish to speak with me?" });
    Add(P_ZELDA_Y, L_HUG,
        { "Oh! Link... Impa would faint. Let's not tell her.", "...Thank you. I feel braver now.",
          "A princess shouldn't... oh, never mind. Just a little longer." });
    Add(P_ZELDA_Y, L_HUG_SEEN, { "How sweet...", "Link is very kind to everyone, isn't he?" });
    Add(P_ZELDA_A, L_PROMPT, { "Yes, Link?", "What is it?" });
    Add(P_ZELDA_A, L_HUG,
        { "Link... thank you. For everything.", "Seven years... and you're finally here.",
          "I'll remember this, whatever comes next." });
    Add(P_ZELDA_A, L_HUG_SEEN, { "They both deserve that.", "How lovely." });

    // Hand in hand, letting go, and being looked at in first person
    Add(P_SARIA, L_HOLD, { "Hee hee, just like when we'd run through the woods!", "Don't let go, okay?" });
    Add(P_SARIA, L_LET_GO, { "Okay! I'll be right here.", "Aww... alright." });
    Add(P_SARIA, L_LOOKED_AT,
        { "Yes, Link? What is it?", "Hee hee, why are you staring?", "Do I have a leaf in my hair?" });
    Add(P_RUTO_Y, L_HOLD, { "Finally! A proper escort for a princess.", "Hold tight. And don't you dare let go." });
    Add(P_RUTO_Y, L_LET_GO, { "Hmph! Fine!", "...I wasn't enjoying it anyway." });
    Add(P_RUTO_Y, L_LOOKED_AT,
        { "What? Is there something on my face?", "Like what you see, fiancé?",
          "Stop staring, it's rude! ...Okay, a little longer." });
    Add(P_RUTO_A, L_HOLD, { "Lead the way, Link.", "Mm. Your hands are warm." });
    Add(P_RUTO_A, L_LET_GO, { "Very well.", "Until next time, then." });
    Add(P_RUTO_A, L_LOOKED_AT, { "Yes, Link? What is it?", "Admiring the Sage of Water, are we?" });
    Add(P_MALON_Y, L_HOLD, { "Yay! Let's skip!", "Hee hee, your hand's all sweaty, fairy boy!" });
    Add(P_MALON_Y, L_LET_GO, { "Okay!", "Aww, already?" });
    Add(P_MALON_Y, L_LOOKED_AT, { "Fairy boy? Whatcha looking at?", "Hee hee! Hi!", "Is there a cucco on my head?" });
    Add(P_MALON_A, L_HOLD, { "Oh... alright. Lead the way.", "Like walking Epona back to the stable." });
    Add(P_MALON_A, L_LET_GO, { "Okay, Link.", "Thanks for the walk." });
    Add(P_MALON_A, L_LOOKED_AT,
        { "Yes, Link? What is it?", "Something wrong?", "Hm? Do I have hay in my hair again?" });
    Add(P_ZELDA_Y, L_HOLD, { "Oh! ...Alright. Just don't tell Impa.", "Your hand is so warm." });
    Add(P_ZELDA_Y, L_LET_GO, { "Of course.", "Thank you, Link." });
    Add(P_ZELDA_Y, L_LOOKED_AT,
        { "Yes, Link? What is it?", "Is something the matter?", "You're looking at me like in my dream..." });
    Add(P_ZELDA_A, L_HOLD, { "Together, then.", "I won't let go this time." });
    Add(P_ZELDA_A, L_LET_GO, { "Alright.", "I'm still with you." });
    Add(P_ZELDA_A, L_LOOKED_AT, { "Yes, Link? What is it?", "Is something on your mind?" });

    // Resting together: a song
    Add(P_SARIA, L_SONG_START, { "Let's rest a while. Sit down, I'll play our song for you!",
                                 "You look tired, Link. Here... listen to the forest's song." });
    Add(P_MALON_Y, L_SONG_START, { "Wanna hear Epona's Song? My mom taught it to me!",
                                   "Sit down, fairy boy! I'll sing for everybody!" });
    Add(P_MALON_A, L_SONG_START, { "Sit a spell, fairy boy. I'll sing you something.",
                                   "Epona always calms down when I sing this. Maybe you will too." });
    Add(P_ZELDA_Y, L_SONG_START, { "Impa hums this to me when I can't sleep... let me hum it for you.",
                                   "Let's rest. This is a song only the royal family knows." });
    Add(P_ZELDA_A, L_SONG_START, { "Rest, Link. This is the lullaby of the royal family.",
                                   "Impa sang this to me long ago. Now it's my turn." });
    Add(P_SARIA, L_SONG_LISTEN, { "Mm... I could listen to this all day.", "It makes me think of home." });
    Add(P_RUTO_Y, L_SONG_LISTEN, { "I'm NOT swaying! ...Okay, maybe a little.",
                                   "Hmph. Zora music is better. ...This is nice though." });
    Add(P_RUTO_A, L_SONG_LISTEN, { "It reminds me of the waters of Zora's Domain... calm and clear.",
                                   "Shh, Link. I'm listening." });
    Add(P_MALON_Y, L_SONG_LISTEN, { "Ooh, I know this one!", "La la laaa..." });
    Add(P_MALON_A, L_SONG_LISTEN, { "That's lovely... I might have to learn it.", "Mm, I could fall asleep..." });
    Add(P_ZELDA_Y, L_SONG_LISTEN, { "What a beautiful melody...", "I wish the whole kingdom could hear this." });
    Add(P_ZELDA_A, L_SONG_LISTEN, { "It's been so long since I could just... listen.", "Peaceful..." });
    Add(P_SARIA, L_SONG_END, { "Hee hee! Did you like it?", "Whenever you miss me, just play it too, okay?" });
    Add(P_MALON_Y, L_SONG_END, { "Ta-daa! Was I good?", "Hee hee! Epona likes that part best!" });
    Add(P_MALON_A, L_SONG_END, { "Well? Not bad for a farm girl, huh?", "Thanks for listening, fairy boy." });
    Add(P_ZELDA_Y, L_SONG_END, { "Remember this melody, Link. It may help you one day.", "Did it help you rest?" });
    Add(P_ZELDA_A, L_SONG_END, { "Thank you for listening, Link.", "Shall we go on? I feel ready now." });
    Add(P_SARIA, L_SONG_PRAISE, { "That was beautiful!", "Yay! Again sometime!" });
    Add(P_RUTO_Y, L_SONG_PRAISE, { "Hmph... that was... acceptable. Fine, it was lovely!",
                                   "Not bad! Not as good as me, but not bad!" });
    Add(P_RUTO_A, L_SONG_PRAISE, { "Even the Zoras would applaud that.", "Lovely. Truly." });
    Add(P_MALON_Y, L_SONG_PRAISE, { "Yay! Again, again!", "That was so pretty!" });
    Add(P_MALON_A, L_SONG_PRAISE, { "That was lovely!", "You've got a gift, you know that?" });
    Add(P_ZELDA_Y, L_SONG_PRAISE, { "How wonderful!", "Bravo!" });
    Add(P_ZELDA_A, L_SONG_PRAISE, { "Beautiful... thank you.", "Wonderful." });
    // Exploring: what she found
    Add(P_SARIA, L_SEE_CHEST, { "Link! A treasure chest! Come look!", "Ooh, I wonder what's inside this one..." });
    Add(P_RUTO_Y, L_SEE_CHEST, { "Treasure! Open it, Link! ...And if it's shiny, it's mine.",
                                 "A chest! Quick, before somebody else gets it!" });
    Add(P_RUTO_A, L_SEE_CHEST, { "There's a chest over here, Link.", "A chest! Maybe something useful for once." });
    Add(P_MALON_Y, L_SEE_CHEST, { "Fairy boy! A box! A treasure box!", "Is it full of rupees? Open it, open it!" });
    Add(P_MALON_A, L_SEE_CHEST, { "Hey, there's a chest over here!", "Found a chest! Want me to keep watch while you open it?" });
    Add(P_ZELDA_Y, L_SEE_CHEST, { "Link, there's a chest here. Someone hid it on purpose...",
                                  "A treasure chest! How exciting!" });
    Add(P_ZELDA_A, L_SEE_CHEST, { "There's a chest here, Link.", "This chest was placed with care. It may hold something important." });

    Add(P_SARIA, L_SEE_SIGN, { "There's a sign here! I'm not great at reading big-people words...",
                               "A sign! What does it say, Link?" });
    Add(P_RUTO_Y, L_SEE_SIGN, { "A sign. Boring! ...Read it to me.", "Hmph. Who writes on wood anyway?" });
    Add(P_RUTO_A, L_SEE_SIGN, { "A sign. Read it, Link, it might matter.", "Someone left directions here." });
    Add(P_MALON_Y, L_SEE_SIGN, { "There's a sign! Does it say where the cows are?", "Ooh, a sign! I can read some words!" });
    Add(P_MALON_A, L_SEE_SIGN, { "A sign here, if you wanna read it.", "This sign's seen better days." });
    Add(P_ZELDA_Y, L_SEE_SIGN, { "There's writing here! Let me see...", "Signs can tell you a lot, if you read them carefully." });
    Add(P_ZELDA_A, L_SEE_SIGN, { "There's a sign here. Words can be clues, Link.", "Let me read this... interesting." });

    Add(P_SARIA, L_SEE_POT, { "Pots! Mido always breaks these...", "There might be something inside this." });
    Add(P_RUTO_Y, L_SEE_POT, { "Smash it, Link! Smash it! It's fun!", "There's probably rupees in this pot." });
    Add(P_RUTO_A, L_SEE_POT, { "Go on, break it. You always do.", "Pots and crates... Hylians keep everything in pots." });
    Add(P_MALON_Y, L_SEE_POT, { "Dad keeps stuff in crates like these!", "Ooh, what's in here?" });
    Add(P_MALON_A, L_SEE_POT, { "Might be something in this one.", "Somebody's storage. Don't make too big a mess, okay?" });
    Add(P_ZELDA_Y, L_SEE_POT, { "Do you really break everyone's pots, Link?", "Something might be hidden in here." });
    Add(P_ZELDA_A, L_SEE_POT, { "There may be supplies in these.", "Go ahead. I won't tell anyone." });

    Add(P_SARIA, L_SEE_NPC, { "Hi there! I'm Saria!", "Ooh, someone new! Hello!" });
    Add(P_RUTO_Y, L_SEE_NPC, { "Hello, commoner. I'm a princess, you know.", "Who are YOU supposed to be?" });
    Add(P_RUTO_A, L_SEE_NPC, { "Hello. I am Ruto, Sage of Water.", "Mm. Friendly enough." });
    Add(P_MALON_Y, L_SEE_NPC, { "Hi! I'm Malon! Have you been to Lon Lon Ranch?", "Hello! Do you like horses?" });
    Add(P_MALON_A, L_SEE_NPC, { "Howdy! Come by the ranch sometime!", "Hello there!" });
    Add(P_ZELDA_Y, L_SEE_NPC, { "Good day to you.", "Hello! Please don't tell anyone you saw me here..." });
    Add(P_ZELDA_A, L_SEE_NPC, { "Hello. Stay safe.", "Good day." });

    Add(P_SARIA, L_SEE_DOOR, { "Where do you think this door goes?", "A door! Let's peek!" });
    Add(P_RUTO_Y, L_SEE_DOOR, { "A door. Open it for me, Link.", "What's behind here? Go look!" });
    Add(P_RUTO_A, L_SEE_DOOR, { "This way, maybe?", "There's a door here, Link." });
    Add(P_MALON_Y, L_SEE_DOOR, { "Ooh, a door! Knock knock!", "What's in there?" });
    Add(P_MALON_A, L_SEE_DOOR, { "There's a door over here.", "Wonder where this leads." });
    Add(P_ZELDA_Y, L_SEE_DOOR, { "Another way through... let's remember it.", "I wonder what's on the other side." });
    Add(P_ZELDA_A, L_SEE_DOOR, { "A door. Shall we see where it leads?", "There's another way through here, Link." });

    Add(P_SARIA, L_SEE_SKULL, { "Link! A Gold Skulltula, up there! Hear it ticking?", "Something golden is crawling over there!" });
    Add(P_RUTO_Y, L_SEE_SKULL, { "EW! A gold bug! Get it, Link!", "That spider's GOLD! I want it!" });
    Add(P_RUTO_A, L_SEE_SKULL, { "A Gold Skulltula, over there.", "There's a Gold Skulltula! Don't let it get away." });
    Add(P_MALON_Y, L_SEE_SKULL, { "A shiny spider! Ooh, look!", "Fairy boy! A gold spider, right there!" });
    Add(P_MALON_A, L_SEE_SKULL, { "Gold Skulltula! There, Link!", "I heard ticking... there, a gold one!" });
    Add(P_ZELDA_Y, L_SEE_SKULL, { "A Gold Skulltula! People say they were cursed...", "Link, look! A golden spider!" });
    Add(P_ZELDA_A, L_SEE_SKULL, { "A Gold Skulltula. There's a family in Kakariko who would thank you.",
                                  "Over there, Link. A Gold Skulltula." });

    Add(P_SARIA, L_SEE_STONE, { "A Gossip Stone! They tell the best secrets.", "Hi, Gossip Stone! ...It's shy." });
    Add(P_RUTO_Y, L_SEE_STONE, { "This rock is staring at me.", "A talking rock? Hmph, I've seen weirder." });
    Add(P_RUTO_A, L_SEE_STONE, { "A Gossip Stone. It might know something.", "These stones hear everything, you know." });
    Add(P_MALON_Y, L_SEE_STONE, { "Is this rock alive?!", "Hello, rock! ...It didn't say anything." });
    Add(P_MALON_A, L_SEE_STONE, { "One of those stones that whispers.", "Maybe it knows something." });
    Add(P_ZELDA_Y, L_SEE_STONE, { "A Gossip Stone... the Sheikah made these.", "They say these stones hold secrets." });
    Add(P_ZELDA_A, L_SEE_STONE, { "A Gossip Stone. The Sheikah placed these long ago.", "They say the Mask of Truth lets you hear what they know." });

    Add(P_SARIA, L_SEE_ITEM, { "Link, there's something on the ground here!", "Over here! Something for you!" });
    Add(P_RUTO_Y, L_SEE_ITEM, { "Something shiny! Mine! ...Okay, yours.", "Link! Pick this up!" });
    Add(P_RUTO_A, L_SEE_ITEM, { "Something over here, Link.", "Don't leave this lying around." });
    Add(P_MALON_Y, L_SEE_ITEM, { "Ooh! Fairy boy, look what I found!", "Something sparkly! Over here!" });
    Add(P_MALON_A, L_SEE_ITEM, { "Hey, something here for you.", "Don't miss this one!" });
    Add(P_ZELDA_Y, L_SEE_ITEM, { "Link, you dropped... no, someone left this.", "There's something here!" });
    Add(P_ZELDA_A, L_SEE_ITEM, { "Something for you here, Link.", "Take this. You'll need it." });

    Add(P_SARIA, L_SEE_ANIMAL, { "Aww, hello there little one!", "Look, Link! Isn't it cute?" });
    Add(P_RUTO_Y, L_SEE_ANIMAL, { "Keep that thing away from me!", "What IS that? It's got feathers!" });
    Add(P_RUTO_A, L_SEE_ANIMAL, { "Land animals are so... fluffy.", "Hm. It's cute. For a land creature." });
    Add(P_MALON_Y, L_SEE_ANIMAL, { "Hiii! Who's a good friend? You are!", "Aww! I love animals!", "Come here, little one!" });
    Add(P_MALON_A, L_SEE_ANIMAL, { "Hey there, sweetie. Easy now.", "Aww. Looks well fed. Somebody loves you." });
    Add(P_ZELDA_Y, L_SEE_ANIMAL, { "Oh! Hello there!", "How sweet..." });
    Add(P_ZELDA_A, L_SEE_ANIMAL, { "Hello, little one.", "Even now, life goes on. That's something." });

    // Places
    AddScene(SCENE_KOKIRI_FOREST, P_SARIA, { "Home... it looks smaller now, doesn't it?" });
    AddScene(SCENE_KOKIRI_FOREST, P_RUTO_Y, { "So THIS is where you grew up? Everything's so tiny!" });
    AddScene(SCENE_KOKIRI_FOREST, P_ZELDA_Y, { "The forest of the Kokiri... from my dream!" });
    AddScene(SCENE_LOST_WOODS, P_SARIA, { "Follow my song, Link. I know these woods by heart." });
    AddScene(SCENE_LOST_WOODS, P_MALON_Y, { "Don't let go of me! I don't wanna turn into a Skull Kid!" });
    AddScene(SCENE_SACRED_FOREST_MEADOW, P_SARIA, { "My secret place... I'm glad I get to share it with you." });
    AddScene(SCENE_HYRULE_FIELD, P_MALON_Y, { "Look how big the field is! Epona would love it!" });
    AddScene(SCENE_HYRULE_FIELD, P_MALON_A, { "Wide open field... perfect for a ride." });
    AddScene(SCENE_HYRULE_FIELD, P_SARIA, { "So much sky! Where do the trees go?" });
    AddScene(SCENE_LON_LON_RANCH, P_MALON_Y, { "Welcome to Lon Lon Ranch! Wanna say hi to Epona?" });
    AddScene(SCENE_LON_LON_RANCH, P_MALON_A, { "Home. It's good to be back, even for a little while." });
    AddScene(SCENE_ZORAS_DOMAIN, P_RUTO_Y, { "Welcome to MY domain! Bow, everyone, your princess is home!" });
    AddScene(SCENE_ZORAS_DOMAIN, P_RUTO_A, { "My home... frozen. I will bring it back. I swear it." });
    AddScene(SCENE_ZORAS_FOUNTAIN, P_RUTO_Y, { "Lord Jabu-Jabu! ...Please don't eat me again." });
    AddScene(SCENE_MARKET_DAY, P_ZELDA_Y, { "So this is what the market feels like up close! It's so loud!" });
    AddScene(SCENE_MARKET_DAY, P_MALON_Y, { "Dad delivers milk here! Well, when he's awake." });
    AddScene(SCENE_HYRULE_CASTLE, P_ZELDA_Y,
             { "Shh! If the guards see me, I'll be sent straight back to my "
               "lessons." });
    AddScene(SCENE_TEMPLE_OF_TIME, P_ZELDA_A, { "This is where it all began... and where it will end." });
    AddScene(SCENE_TEMPLE_OF_TIME, P_ZELDA_Y, { "The Door of Time... the Spiritual Stones will open it." });
    AddScene(SCENE_KAKARIKO_VILLAGE, P_ZELDA_Y, { "Impa built this village. She's very proud of it." });
    AddScene(SCENE_KAKARIKO_VILLAGE, P_MALON_Y, { "Look at all the cuccos! I wonder if they're friendly." });
    AddScene(SCENE_DEATH_MOUNTAIN_TRAIL, P_RUTO_Y, { "It's so HOT! My skin is drying out!" });
    AddScene(SCENE_DEATH_MOUNTAIN_CRATER, P_RUTO_A, { "This heat... a Zora wasn't made for this." });
    AddScene(SCENE_GERUDO_VALLEY, P_ZELDA_A, { "Ganondorf's homeland. Stay on your guard." });
    AddScene(SCENE_JABU_JABU, P_RUTO_Y, { "Lord Jabu-Jabu's belly... ugh, I've been here enough for one lifetime." });
    AddScene(SCENE_FOREST_TEMPLE, P_SARIA, { "The Forest Temple... I can feel the evil in it. Be careful, Link." });
    AddScene(SCENE_WATER_TEMPLE, P_RUTO_A, { "The Water Temple. My temple. Follow me, I know every current." });

    // Banter, the first speaker first
    AddBanter(P_SARIA, P_RUTO_Y,
              { "So, Ruto, how did you meet Link?", "He's my fiancé. Didn't he tell you?",
                "Your... fiancé? Hee hee! Link, is that true?", "Of course it's true! He took my Zora's Sapphire!" });
    AddBanter(P_RUTO_Y, P_SARIA,
              { "Why do you wear so much green?",
                "It's the color of the forest! Why are you "
                "so... shiny?",
                "Because I'm a PRINCESS.", "Hee hee, okay, Princess Shiny." });
    AddBanter(P_MALON_Y, P_SARIA,
              { "Do you have horses in the forest?", "No, but we have fairies!", "Ooh! Can a fairy pull a cart?",
                "...I've never asked one." });
    AddBanter(P_ZELDA_Y, P_SARIA,
              { "You're Link's friend from the forest, aren't you?",
                "That's right! I'm Saria. You're the princess from his dream?",
                "His dream? I thought it was MY dream...", "Maybe you both dreamed it! That's kind of nice." });
    AddBanter(P_ZELDA_Y, P_MALON_Y,
              { "Malon, you deliver milk to the castle, don't you?", "My dad does! Do you drink it?",
                "Every morning. It's the best thing about the castle.",
                "Hee hee! I'll tell the cows a princess likes them!" });
    AddBanter(P_RUTO_Y, P_ZELDA_Y,
              { "So you're the Hylian princess.", "And you must be Princess Ruto.",
                "Just so you know, Link is spoken for.", "...I'll keep that in mind." });
    AddBanter(P_MALON_Y, P_RUTO_Y,
              { "Your fins are so pretty!", "...Really? I mean, of course they are.",
                "Can you swim faster than a horse runs?", "Faster than ten horses!" });
    AddBanter(P_SARIA, P_RUTO_A,
              { "Ruto, you got so tall!", "And you haven't changed at all, Saria.",
                "Kokiri never grow up. I don't mind, really.", "Hm. I envy that a little, some days." });
    AddBanter(P_SARIA, P_ZELDA_A,
              { "We're both sages now, Zelda. Isn't that strange?",
                "Strange, and a little frightening. I'm glad we're not alone in it.", "We have each other. And Link.",
                "And Link." });
    AddBanter(P_MALON_A, P_ZELDA_A,
              { "Princess... do you think the ranch will be safe?",
                "As long as people like you keep it alive, Malon, yes.", "Then I'll keep singing.",
                "Please do. Hyrule needs songs." });
    AddBanter(P_RUTO_A, P_ZELDA_A,
              { "Princess, I still remember you were the one who told him about my sapphire.",
                "I may have mentioned it was needed.", "Needed! He took my ENGAGEMENT RING because of you!",
                "...I'm sure he'll make it up to you." });
    AddBanter(P_MALON_A, P_RUTO_A,
              { "You've got such a strong voice, Ruto. Ever tried singing?",
                "Zoras sing underwater. It carries for miles.", "Teach me sometime?",
                "If you can hold your breath long enough." });
    AddBanter(P_SARIA, P_MALON_A,
              { "Malon, you sing too, right?", "Every day! Epona's Song mostly.",
                "Let's sing together sometime! Ocarina and voice!", "That sounds lovely, Saria." });
}

static void LoadExtraLines() {
    try {
        std::string found;
        for (const char* name : { "companion_lines.json", "mods/companion_lines.json" }) {
            std::string p = Ship::Context::GetPathRelativeToAppDirectory(name);
            if (std::filesystem::exists(p)) {
                found = p;
                break;
            }
        }
        if (found.empty()) {
            return;
        }
        std::ifstream in(found, std::ios::binary);
        std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        nlohmann::json j = nlohmann::json::parse(text, nullptr, false, true);
        if (!j.is_object()) {
            return;
        }
        for (int p = 0; p < P_COUNT; p++) {
            if (!j.contains(sPersonaKey[p]) || !j[sPersonaKey[p]].is_object()) {
                continue;
            }
            for (int k = 0; k < L_KIND_COUNT; k++) {
                auto& o = j[sPersonaKey[p]];
                if (o.contains(sKindKey[k]) && o[sKindKey[k]].is_array()) {
                    for (auto& s : o[sKindKey[k]]) {
                        if (s.is_string()) {
                            sLines[p][k].push_back(s.get<std::string>());
                        }
                    }
                }
            }
        }
        if (j.contains("banter") && j["banter"].is_array()) {
            for (auto& b : j["banter"]) {
                Banter t;
                t.a = t.b = -1;
                for (int p = 0; p < P_COUNT; p++) {
                    if (b.value("a", "") == sPersonaKey[p]) {
                        t.a = p;
                    }
                    if (b.value("b", "") == sPersonaKey[p]) {
                        t.b = p;
                    }
                }
                if ((t.a < 0) || (t.b < 0) || !b.contains("lines") || !b["lines"].is_array()) {
                    continue;
                }
                for (auto& s : b["lines"]) {
                    if (s.is_string()) {
                        t.lines.push_back(s.get<std::string>());
                    }
                }
                sBanter.push_back(t);
            }
        }
    } catch (...) {}
}

// ---- The actor ----

struct CompModel;

enum CompState { ST_FOLLOW, ST_IDLE, ST_ATTACK, ST_SUPPORT, ST_HURT, ST_KO, ST_EMOTE, ST_TALK, ST_HUG, ST_REST, ST_EXPLORE };

typedef struct {
    Actor actor;
    SkelAnime skel;
    ColliderCylinder body;
    ColliderCylinder hit;
    s16 who;
    s16 persona;
    u8 adult;
    u8 state;
    u8 hidden;
    u8 hitActive;
    u8 swimming;
    u8 talking;
    s16 timer;
    s16 cdRanged, cdClose, cdMelee, cdSupport;
    s16 shotsLeft;
    s16 move;
    s16 eye, blinkTimer, mouth, mouthTimer, eyeOverride;
    s16 hp;
    s16 stillTimer, stuckTimer, waitSaid;
    u8 catchingUp, wandering;
    s8 holdSide; // holding Link's hand: 1 at his right, -1 at his left, 0 not
    s16 lookedCooldown, lookedFrames;
    f32 bestDist;   // closest she's got to him on this catch-up
    s16 noProgress; // frames without getting closer
    f32 holdWalk;
    s16 wanderTimer;
    Vec3f wanderGoal;
    f32 legPhase;
    f32 legAmount;
    Actor* target;
    const char* anim;
    s16 formation;
    // Link's walk (a hidden copy of his skeleton walking, and one standing)
    SkelAnime puppet;
    SkelAnime rest;
    Vec3s puppetJoints[PLAYER_LIMB_BUF_COUNT];
    Vec3s puppetMorph[PLAYER_LIMB_BUF_COUNT];
    Vec3s restJoints[PLAYER_LIMB_BUF_COUNT];
    Vec3s restMorph[PLAYER_LIMB_BUF_COUNT];
    const char* puppetAnim;
    u8 linkWalk;
    f32 walkAmount;
    u8 talkStage;
    u8 restFrames;
    // Mixamo motion on her own skeleton
    CompRigMap rig;
    u8 rigState; // 0 not looked at yet, 1 ready, 2 none
    f32 legLen;  // in her model's units
    f32 mixPhase, mixAmount;
    s16 bowClip;
    f32 bowFrame;
    CompRigOut rigOut;
    f32 hugAmount;
    // The skirt: widened (from its waist) as her legs spread, with a little spring so it lags and settles
    f32 skirtScale, skirtVel;
    uint32_t skirtMask; // skirt limbs safe to widen (nothing but skirt hangs below them)
    u8 skirtChecked;
    // A custom model (Link-rigged): its own copy of Link's skeleton playing Link's animations
    CompModel* model;
    SkelAnime modelBody;
    SkelAnime modelSkel;
    Vec3s bodyJoints[PLAYER_LIMB_BUF_COUNT];
    Vec3s bodyMorph[PLAYER_LIMB_BUF_COUNT];
    const char* bodyAnim;
    // Resting together (a song): where she sits, whether she's there yet, and how far into sitting she is
    Vec3f seat;
    u8 seated;
    s16 seatTimer;
    f32 sitAmount;
    // Exploring: what she's gone to look at
    Actor* poi;
    s8 poiKind;
    u8 explorePhase; // 0 going there, 1 looking
    s16 exploreTimer, exploreStall, exploreCd;
    f32 exploreBest;
} Comp;

static s16 sHugWho = -1;
static bool sLinkSits = false; // Link sitting down with them for a song
static CompModel* FindModel(int persona);

static s16 sCompActorId = -1;
static s16 sCuccoActorId = -1;
static Comp* sComp[COMP_COUNT] = {};

// Bubbles: what each one is saying now, and lines waiting their turn (banter)
struct Bubble {
    std::string text;
    s32 frames;
};
static Bubble sBubble[COMP_COUNT];
struct Pending {
    int who;
    std::string text;
    s32 delay;
};
static std::deque<Pending> sPending;
static s32 sChatTimer = 0;
static s32 sEnterTimer = -1;
static s32 sKeyTipTimer = -1;
static s32 sStillFrames = 0;
static s32 sLowHealthCooldown = 0;
static s32 sCheerCooldown = 0;
static s32 sNightSaid = -1;
static s32 sLastTalker = -1;

static bool On() {
    return CVarGetInteger(COMP_CVAR("Enabled"), 0) != 0;
}

static bool WhoOn(int who) {
    return On() && (CVarGetInteger((std::string(CVAR_ENHANCEMENT("Companions.")) + sWhoKey[who]).c_str(), 1) != 0);
}

static f32 Tune(const char* key, f32 def) {
    return CompTune((std::string("companion.") + key).c_str(), def);
}

static std::string Pick(const Lines& l) {
    if (l.empty()) {
        return "";
    }
    return l[(size_t)(Rand_ZeroOne() * (f32)l.size()) % l.size()];
}

static std::string Format(std::string s, const char* str, int num) {
    size_t at = s.find("%s");
    if (at != std::string::npos) {
        s.replace(at, 2, (str != nullptr) ? str : "");
    }
    at = s.find("%d");
    if (at != std::string::npos) {
        s.replace(at, 2, std::to_string(num));
    }
    return s;
}

static void Say(int who, const std::string& text) {
    if (text.empty() || !CVarGetInteger(COMP_CVAR("Chatter"), 1)) {
        return;
    }
    Comp* c = sComp[who];
    std::string t = text;
    // (a bubble shows one box: the ^ breaks of a long talk become spaces)
    std::replace(t.begin(), t.end(), '^', ' ');
    sBubble[who].text = t;
    sBubble[who].frames = (s32)(Tune("bubble_seconds", 2.5f) * COMP_FPS + (f32)t.size() * 0.9f);
    if (c != nullptr) {
        c->mouthTimer = (s16)std::min<s32>(sBubble[who].frames, 60);
    }
    sLastTalker = who;
}

static void SayKind(int who, int kind, const char* str = nullptr, int num = 0) {
    Comp* c = sComp[who];
    if (c == nullptr) {
        return;
    }
    Say(who, Format(Pick(sLines[c->persona][kind]), str, num));
}

// Only one bubble at a time from the same moment: a random one of those here says it
static int RandomPresent(int except = -1) {
    int here[COMP_COUNT];
    int n = 0;
    for (int i = 0; i < COMP_COUNT; i++) {
        if ((sComp[i] != nullptr) && (i != except) && !sComp[i]->hidden && (sComp[i]->state != ST_KO)) {
            here[n++] = i;
        }
    }
    if (n == 0) {
        return -1;
    }
    return here[(int)(Rand_ZeroOne() * (f32)n) % n];
}

static bool AnyoneTalking() {
    if (!sPending.empty()) {
        return true;
    }
    for (int i = 0; i < COMP_COUNT; i++) {
        if (sBubble[i].frames > 0) {
            return true;
        }
    }
    return false;
}

static void Voice(Actor* a, u16 sfx) {
    if (sfx != 0) {
        Audio_PlayActorSound2(a, sfx);
    }
}

static void SetAnim(Comp* c, const char* anim, f32 speed, u8 mode, f32 morph) {
    if ((anim == nullptr) || ((c->anim == anim) && (mode == ANIMMODE_LOOP))) {
        if (anim != nullptr) {
            c->skel.playSpeed = speed;
        }
        return;
    }
    c->anim = anim;
    Animation_Change(&c->skel, (AnimationHeader*)anim, speed, 0.0f, (f32)Animation_GetLastFrame((void*)anim), mode,
                     morph);
}

static const PersonaDef& Def(Comp* c) {
    return sDefs[c->persona];
}

// ---- Enemies ----

static bool ActorExists(PlayState* play, Actor* a) {
    if (a == nullptr) {
        return false;
    }
    for (int cat : { ACTORCAT_ENEMY, ACTORCAT_BOSS, ACTORCAT_MISC, ACTORCAT_NPC }) {
        for (Actor* e = play->actorCtx.actorLists[cat].head; e != nullptr; e = e->next) {
            if (e == a) {
                return true;
            }
        }
    }
    return false;
}

// Everything in the game's enemy and boss lists, whether or not it can be Z-targeted right now, and the enemies the
// game files elsewhere: Leevers (misc) and Skullwalltulas (with the NPCs; not the gold ones, those are Link's)
static bool ExtraEnemy(Actor* e) {
    if (e->id == ACTOR_EN_REEBA) {
        return true;
    }
    if (e->id == ACTOR_EN_SW) {
        return ((e->params & 0xE000) >> 0xD) == 0;
    }
    return false;
}

static bool Fightable(Actor* e) {
    if ((e->update == nullptr) || (e->draw == nullptr) || (e->colChkInfo.health <= 0)) {
        return false;
    }
    // Enemies and bosses that can fight (the hostile ones; the altar's Spiritual Stones are in the boss list too),
    // and Shabom and Beamos, which the game doesn't mark hostile
    if ((e->category == ACTORCAT_ENEMY) || (e->category == ACTORCAT_BOSS)) {
        return (e->flags & ACTOR_FLAG_HOSTILE) || (e->id == ACTOR_EN_BUBBLE) || (e->id == ACTOR_EN_VM);
    }
    // (a Leever under the sand isn't one to shoot at: only while it's up and hostile)
    return ExtraEnemy(e) && (e->flags & ACTOR_FLAG_HOSTILE);
}

static Actor* FindTarget(PlayState* play, Comp* c) {
    Player* player = GET_PLAYER(play);
    f32 range = Tune("fight_range", 450.0f);
    f32 leash = Tune("fight_leash", 650.0f);
    Actor* best = nullptr;
    f32 bestD = 1e9f;

    for (int cat : { ACTORCAT_ENEMY, ACTORCAT_BOSS, ACTORCAT_MISC, ACTORCAT_NPC }) {
        for (Actor* e = play->actorCtx.actorLists[cat].head; e != nullptr; e = e->next) {
            if (!Fightable(e)) {
                continue;
            }
            f32 d = Actor_WorldDistXYZToActor(&c->actor, e);
            if ((d > range) || (Actor_WorldDistXYZToActor(&player->actor, e) > leash)) {
                continue;
            }
            // (the one Link is fighting first)
            if (e == player->focusActor) {
                d *= 0.5f;
            }
            if (d < bestD) {
                bestD = d;
                best = e;
            }
        }
    }
    return best;
}

// ---- Moves ----

static Vec3f HandPos(Comp* c) {
    Vec3f p = c->actor.world.pos;
    p.y += Def(c).height * c->actor.scale.y * 0.6f;
    p.x += Math_SinS(c->actor.shape.rot.y) * 12.0f;
    p.z += Math_CosS(c->actor.shape.rot.y) * 12.0f;
    return p;
}

static Vec3f AimPos(Actor* t) {
    Vec3f p = t->focus.pos;
    if ((p.x == 0.0f) && (p.y == 0.0f) && (p.z == 0.0f)) {
        p = t->world.pos;
        p.y += 20.0f;
    }
    return p;
}

static void FireArrow(PlayState* play, Comp* c, s16 type) {
    if ((c->target == nullptr) || !ActorExists(play, c->target)) {
        return;
    }
    if ((c->bowClip >= 0) && (type != ARROW_NUT)) {
        c->bowClip = CLIP_BOW_RECOIL;
        c->bowFrame = 0.0f;
    }
    Vec3f from = HandPos(c);
    Vec3f to = AimPos(c->target);
    s16 yaw = Math_Vec3f_Yaw(&from, &to);
    s16 pitch = Math_Vec3f_Pitch(&from, &to);
    // A nut is thrown down at the enemy's feet, where it pops
    if (type == ARROW_NUT) {
        pitch = (s16)(pitch + 0x0800);
    }
    Actor* a = Actor_SpawnAsChild(&play->actorCtx, &c->actor, play, ACTOR_EN_ARROW, from.x, from.y, from.z, pitch, yaw,
                                  0, type);
    if (a == nullptr) {
        return;
    }
    // Shot at once (as Ivan the fairy shoots): the arrow flies while Link counts as having just fired
    GET_PLAYER(play)->unk_A73 = MAX(GET_PLAYER(play)->unk_A73, 4);
    a->parent = nullptr;
}

// Angry cuccos: one of Malon's birds flies at the enemy and pecks it
typedef struct {
    Actor actor;
    SkelAnime skel;
    Vec3s joints[16];
    Vec3s morphs[16];
    ColliderCylinder col;
    Actor* target;
    s16 timer;
    s16 flap;
} CompCucco;

static ColliderCylinderInit sCuccoCol = {
    { COLTYPE_NONE, AT_ON | AT_TYPE_PLAYER, AC_NONE, OC1_NONE, OC2_NONE, COLSHAPE_CYLINDER },
    { ELEMTYPE_UNK2,
      { DMG_SLASH_KOKIRI, 0x00, 0x01 },
      { 0x00000000, 0x00, 0x00 },
      TOUCH_ON | TOUCH_SFX_NORMAL,
      BUMP_NONE,
      OCELEM_NONE },
    { 15, 25, -5, { 0, 0, 0 } },
};

static void Cucco_Init(Actor* thisx, PlayState* play) {
    CompCucco* k = (CompCucco*)thisx;
    SkelAnime_InitFlex(play, &k->skel, (FlexSkeletonHeader*)OTR("object_niw", "gCuccoSkel"),
                       (AnimationHeader*)OTR("object_niw", "gCuccoAnim"), k->joints, k->morphs, 16);
    ActorShape_Init(&thisx->shape, 0.0f, ActorShadow_DrawCircle, 10.0f);
    Actor_SetScale(thisx, 0.01f);
    Collider_InitCylinder(play, &k->col);
    Collider_SetCylinder(play, &k->col, thisx, &sCuccoCol);
    k->timer = 80;
    thisx->room = -1;
    thisx->speedXZ = 0.0f;
    Audio_PlayActorSound2(thisx, NA_SE_EV_CHICKEN_CRY_A);
}

static void Cucco_Destroy(Actor* thisx, PlayState* play) {
    Collider_DestroyCylinder(play, &((CompCucco*)thisx)->col);
}

static void Cucco_Update(Actor* thisx, PlayState* play) {
    CompCucco* k = (CompCucco*)thisx;

    SkelAnime_Update(&k->skel);
    k->flap = (s16)(k->flap + 0x2800);
    if (--k->timer <= 0) {
        Actor_Kill(thisx);
        return;
    }
    if ((k->target != nullptr) && !ActorExists(play, k->target)) {
        k->target = nullptr;
    }
    if (k->target != nullptr) {
        Vec3f to = AimPos(k->target);
        s16 yaw = Math_Vec3f_Yaw(&thisx->world.pos, &to);
        s16 pitch = Math_Vec3f_Pitch(&thisx->world.pos, &to);
        Math_SmoothStepToS(&thisx->world.rot.y, yaw, 2, 0x1800, 0x100);
        Math_SmoothStepToS(&thisx->world.rot.x, pitch, 2, 0x1000, 0x100);
    }
    Math_ApproachF(&thisx->speedXZ, 11.0f, 0.3f, 1.5f);
    thisx->shape.rot.y = thisx->world.rot.y;
    thisx->velocity.x = Math_SinS(thisx->world.rot.y) * Math_CosS(thisx->world.rot.x) * thisx->speedXZ;
    thisx->velocity.z = Math_CosS(thisx->world.rot.y) * Math_CosS(thisx->world.rot.x) * thisx->speedXZ;
    thisx->velocity.y = -Math_SinS(thisx->world.rot.x) * thisx->speedXZ;
    Actor_UpdatePos(thisx);
    if ((k->timer % 20) == 0) {
        Audio_PlayActorSound2(thisx, NA_SE_EV_CHICKEN_CRY_M);
    }
    if (k->col.base.atFlags & AT_HIT) {
        // A peck, and the bird flies off in a huff
        k->col.base.atFlags &= ~AT_HIT;
        Audio_PlayActorSound2(thisx, NA_SE_EV_CHICKEN_CRY_A);
        k->target = nullptr;
        thisx->world.rot.y = (s16)(thisx->world.rot.y + 0x8000);
        thisx->world.rot.x = -0x2000;
        k->timer = MIN(k->timer, 20);
    }
    Collider_UpdateCylinder(thisx, &k->col);
    if (k->target != nullptr) {
        CollisionCheck_SetAT(play, &play->colChkCtx, &k->col.base);
    }
}

static s32 Cucco_OverrideLimbDraw(PlayState* play, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot, void* thisx,
                                  Gfx** gfx) {
    CompCucco* k = (CompCucco*)thisx;
    // (its wings beat)
    if ((limbIndex == 13) || (limbIndex == 15)) {
        rot->y = (s16)(rot->y + (s16)(Math_SinS(k->flap) * 9000.0f) * ((limbIndex == 13) ? 1 : -1));
    }
    return false;
}

static void Cucco_Draw(Actor* thisx, PlayState* play) {
    CompCucco* k = (CompCucco*)thisx;
    func_80034BA0(play, &k->skel, Cucco_OverrideLimbDraw, nullptr, thisx, 255);
}

static void SendCuccos(PlayState* play, Comp* c) {
    int n = (int)Tune("cucco_count", 3.0f);
    for (int i = 0; i < n; i++) {
        s16 yaw = (s16)(c->actor.shape.rot.y + (i - n / 2) * 0x1800);
        Vec3f p = c->actor.world.pos;
        p.x += Math_SinS(yaw) * 30.0f;
        p.z += Math_CosS(yaw) * 30.0f;
        p.y += 30.0f + (f32)i * 8.0f;
        Actor* a = Actor_Spawn(&play->actorCtx, play, sCuccoActorId, p.x, p.y, p.z, -0x1000, yaw, 0, 0);
        if (a != nullptr) {
            ((CompCucco*)a)->target = c->target;
        }
    }
}

static void Heal(PlayState* play, int hearts) {
    gSaveContext.healthAccumulator = (s16)(gSaveContext.healthAccumulator + hearts * 0x10);
}

static void DoSupport(PlayState* play, Comp* c) {
    Player* player = GET_PLAYER(play);
    switch (Def(c).mSupport) {
        case MOVE_SONG:
            Heal(play, (int)Tune("song_hearts", 3.0f));
            Sfx_PlaySfxCentered(NA_SE_EV_FAIRY_DASH);
            break;
        case MOVE_MILK:
            Heal(play, (int)Tune("milk_hearts", 5.0f));
            Sfx_PlaySfxCentered(NA_SE_EV_COW_CRY);
            break;
        case MOVE_SHIELD:
            // (negative: he can't be touched, without the flashing of being hurt)
            player->invincibilityTimer = (s8)(-CLAMP((s32)Tune("shield_frames", 100.0f), 0, 127));
            Sfx_PlaySfxCentered(c->adult ? NA_SE_PL_MAGIC_WIND_NORMAL : NA_SE_EV_FAIRY_DASH);
            break;
        default:
            break;
    }
    SayKind(c->who, L_SUPPORT);
}

// ---- Collision ----

static ColliderCylinderInit sBodyCol = {
    { COLTYPE_HIT0, AT_NONE, AC_ON | AC_TYPE_ENEMY, OC1_ON | OC1_TYPE_1 | OC1_TYPE_2, OC2_TYPE_2, COLSHAPE_CYLINDER },
    { ELEMTYPE_UNK1, { 0x00000000, 0x00, 0x00 }, { 0xFFCFFFFF, 0x00, 0x00 }, TOUCH_NONE, BUMP_ON, OCELEM_ON },
    { 14, 45, 0, { 0, 0, 0 } },
};

static ColliderCylinderInit sHitCol = {
    { COLTYPE_NONE, AT_ON | AT_TYPE_PLAYER, AC_NONE, OC1_NONE, OC2_NONE, COLSHAPE_CYLINDER },
    { ELEMTYPE_UNK2,
      { DMG_SLASH_KOKIRI, 0x00, 0x02 },
      { 0x00000000, 0x00, 0x00 },
      TOUCH_ON | TOUCH_SFX_NORMAL,
      BUMP_NONE,
      OCELEM_NONE },
    { 55, 50, 0, { 0, 0, 0 } },
};

// ---- Behaviour ----

static void Comp_Init(Actor* thisx, PlayState* play) {
    Comp* c = (Comp*)thisx;
    c->who = thisx->params & 3;
    c->adult = (thisx->params >> 2) & 1;
    c->persona = PersonaFor(c->who, c->adult);
    const PersonaDef& d = Def(c);

    // (the skeleton's own limb count, as the game's actors do it: tables made to fit)
    SkelAnime_InitFlex(play, &c->skel, (FlexSkeletonHeader*)d.skel, (AnimationHeader*)d.idle, nullptr, nullptr, 0);
    c->anim = d.idle;
    ActorShape_Init(&thisx->shape, 0.0f, ActorShadow_DrawCircle, 22.0f);
    Actor_SetScale(thisx, d.scale);
    Collider_InitCylinder(play, &c->body);
    Collider_SetCylinder(play, &c->body, thisx, &sBodyCol);
    Collider_InitCylinder(play, &c->hit);
    Collider_SetCylinder(play, &c->hit, thisx, &sHitCol);
    c->hit.info.toucher.dmgFlags = c->adult ? DMG_SLASH_MASTER : DMG_SLASH_KOKIRI;
    thisx->gravity = -1.0f;
    thisx->targetMode = 0; // (Z-targeted only up close: enemies first)
    thisx->textId = COMP_TEXT_ID;
    thisx->room = -1; // (she goes from room to room with him)
    thisx->colChkInfo.mass = MASS_IMMOVABLE;
    c->blinkTimer = 30;
    c->hp = (s16)Tune("hp", 6.0f);
    c->state = ST_FOLLOW;
    c->eyeOverride = -1;
    c->cdRanged = c->cdClose = c->cdMelee = 40;
    c->cdSupport = 200;
    for (int i = 0; i < 10; i++) {
        c->linkWalk |= (sWalkMap[c->persona][i] != 0) ? 1 : 0;
    }
    if (c->linkWalk) {
        FlexSkeletonHeader* link = (FlexSkeletonHeader*)(c->adult ? "__OTR__objects/object_link_boy/gLinkAdultSkel"
                                                                  : "__OTR__objects/object_link_child/gLinkChildSkel");
        SkelAnime_InitLink(play, &c->puppet, link, (LinkAnimationHeader*)LINK_WALK, 9, c->puppetJoints, c->puppetMorph,
                           PLAYER_LIMB_MAX);
        SkelAnime_InitLink(play, &c->rest, link, (LinkAnimationHeader*)LINK_WAIT, 9, c->restJoints, c->restMorph,
                           PLAYER_LIMB_MAX);
        LinkAnimation_PlayLoop(play, &c->puppet, (LinkAnimationHeader*)LINK_WALK);
        LinkAnimation_PlayLoop(play, &c->rest, (LinkAnimationHeader*)LINK_WAIT);
        c->rest.playSpeed = 0.0f;
        c->puppetAnim = LINK_WALK;
    }
    c->bowClip = -1;
    c->model = FindModel(c->persona);
    if (c->model != nullptr) {
        FlexSkeletonHeader* link = (FlexSkeletonHeader*)(c->adult ? "__OTR__objects/object_link_boy/gLinkAdultSkel"
                                                                  : "__OTR__objects/object_link_child/gLinkChildSkel");
        SkelAnime_InitLink(play, &c->modelBody, link, (LinkAnimationHeader*)LINK_WAIT, 9, c->bodyJoints, c->bodyMorph,
                           PLAYER_LIMB_MAX);
        LinkAnimation_PlayLoop(play, &c->modelBody, (LinkAnimationHeader*)LINK_WAIT);
        c->bodyAnim = LINK_WAIT;
    }
    sComp[c->who] = c;
}

static void Comp_Destroy(Actor* thisx, PlayState* play) {
    Comp* c = (Comp*)thisx;
    Collider_DestroyCylinder(play, &c->body);
    Collider_DestroyCylinder(play, &c->hit);
    SkelAnime_Free(&c->skel, play);
    if ((c->who >= 0) && (c->who < COMP_COUNT) && (sComp[c->who] == c)) {
        sComp[c->who] = nullptr;
    }
    if (sHugWho == c->who) {
        sHugWho = -1;
    }
}

// Where she walks: her own place in the group around Link
static Vec3f SlotPos(Comp* c, Player* player) {
    static const s16 sAngles[COMP_COUNT] = { (s16)0x9000, (s16)0x7000, (s16)0xB000, (s16)0x5000 };
    static const f32 sDist[COMP_COUNT] = { 75.0f, 80.0f, 110.0f, 115.0f };
    f32 dist = sDist[c->formation % COMP_COUNT] * Tune("follow_distance", 1.0f);
    s16 yaw = (s16)(player->actor.shape.rot.y + sAngles[c->formation % COMP_COUNT]);
    Vec3f p = player->actor.world.pos;
    p.x += Math_SinS(yaw) * dist;
    p.z += Math_CosS(yaw) * dist;
    return p;
}

static void WarpToLink(PlayState* play, Comp* c, Player* player) {
    Vec3f p = SlotPos(c, player);
    Vec3f top = p;
    CollisionPoly* poly;
    s32 bgId;

    top.y += 60.0f;
    f32 floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &top);
    // (no floor there, or a wall between them: right behind him instead)
    if ((floorY <= BGCHECK_Y_MIN) || (fabsf(floorY - player->actor.world.pos.y) > 80.0f)) {
        p = player->actor.world.pos;
        p.x -= Math_SinS(player->actor.shape.rot.y) * 40.0f;
        p.z -= Math_CosS(player->actor.shape.rot.y) * 40.0f;
        floorY = player->actor.world.pos.y;
    }
    c->actor.world.pos = p;
    c->actor.world.pos.y = floorY;
    c->actor.prevPos = c->actor.world.pos;
    c->actor.velocity.y = 0.0f;
    c->actor.speedXZ = 0.0f;
    c->actor.shape.rot.y = c->actor.world.rot.y = player->actor.shape.rot.y;
    c->stuckTimer = 0;
}

static bool LinkSwimming(Player* player) {
    return (player->stateFlags1 & PLAYER_STATE1_IN_WATER) != 0;
}

static bool LinkBusy(PlayState* play, Player* player) {
    if ((sHugWho >= 0) || sLinkSits) {
        return false; // (a hug, or sitting for a song, holds him still; that's not a cutscene)
    }
    // Only real cutscenes: talking (to them or anyone), opening chests, playing the ocarina and the like keep them
    // in view
    // (the game also marks talking, item gets and doors as "in a cutscene" on Link: those don't count)
    return (play->csCtx.state != CS_STATE_IDLE) ||
           (player->stateFlags1 & (PLAYER_STATE1_LOADING | PLAYER_STATE1_DEAD)) ||
           (play->transitionTrigger != TRANS_TRIGGER_OFF);
}

static void StartAttack(PlayState* play, Comp* c, s16 move) {
    const PersonaDef& d = Def(c);
    c->move = move;
    c->state = ST_ATTACK;
    c->actor.speedXZ = 0.0f;
    switch (move) {
        case MOVE_SEED:
            c->shotsLeft = (s16)Tune("seed_shots", 3.0f);
            c->timer = 12;
            c->cdRanged = (s16)Tune("seed_cooldown", 45.0f);
            SetAnim(c, d.cast, 1.5f, ANIMMODE_ONCE, -4.0f);
            c->bowClip = CLIP_BOW_DRAW;
            c->bowFrame = 0.0f;
            break;
        case MOVE_ICE:
        case MOVE_LIGHT:
            c->shotsLeft = 1;
            c->timer = 18;
            c->cdRanged = (s16)Tune((move == MOVE_LIGHT) ? "light_cooldown" : "ice_cooldown", 80.0f);
            SetAnim(c, d.cast, 1.0f, ANIMMODE_ONCE, -4.0f);
            c->bowClip = CLIP_BOW_DRAW;
            c->bowFrame = 0.0f;
            break;
        case MOVE_CUCCO:
            c->shotsLeft = 1;
            c->timer = 14;
            c->cdRanged = (s16)Tune("cucco_cooldown", 160.0f);
            SetAnim(c, d.cast, 1.0f, ANIMMODE_ONCE, -4.0f);
            break;
        case MOVE_NUT:
            c->shotsLeft = 1;
            c->timer = 10;
            c->cdClose = (s16)Tune("nut_cooldown", 140.0f);
            if (d.mRanged == MOVE_NUT) {
                c->cdRanged = c->cdClose;
            }
            SetAnim(c, d.cast, 1.5f, ANIMMODE_ONCE, -4.0f);
            break;
        case MOVE_SPLASH:
            c->shotsLeft = 1;
            c->timer = 40; // (running in, then the slam)
            c->cdMelee = (s16)Tune("splash_cooldown", 70.0f);
            break;
        default:
            c->state = ST_FOLLOW;
            return;
    }
    if (Rand_ZeroOne() < Tune("attack_shout_chance", 0.2f)) {
        SayKind(c->who, L_ATTACK);
    }
}

static void UpdateAttack(PlayState* play, Comp* c) {
    const PersonaDef& d = Def(c);

    if ((c->target != nullptr) && !ActorExists(play, c->target)) {
        c->target = nullptr;
    }
    if (c->target != nullptr) {
        // (with a bow she stands side-on, her bow arm toward it)
        s16 face = Actor_WorldYawTowardActor(&c->actor, c->target);
        if (c->bowClip >= 0) {
            face = (s16)(face - (s16)(Tune("bow_turn", 70.0f) * (65536.0f / 360.0f)));
        }
        Math_SmoothStepToS(&c->actor.shape.rot.y, face, 3, 0x1800, 0x100);
        c->actor.world.rot.y = Actor_WorldYawTowardActor(&c->actor, c->target);
    }
    if (c->move == MOVE_SPLASH) {
        // Run in, then slam the water down
        if ((c->target != nullptr) && (c->shotsLeft > 0) && (Actor_WorldDistXZToActor(&c->actor, c->target) > 50.0f) &&
            (c->timer > 12)) {
            c->actor.speedXZ = 6.0f;
            SetAnim(c, d.move != nullptr ? d.move : d.idle, 1.6f, ANIMMODE_LOOP, -4.0f);
        } else if (c->shotsLeft > 0) {
            c->actor.speedXZ = 0.0f;
            SetAnim(c, d.melee, 1.4f, ANIMMODE_ONCE, -3.0f);
            c->shotsLeft = 0;
            c->timer = 16;
        } else if (c->timer == 10) {
            Vec3f p = c->actor.world.pos;
            p.x += Math_SinS(c->actor.shape.rot.y) * 25.0f;
            p.z += Math_CosS(c->actor.shape.rot.y) * 25.0f;
            EffectSsGSplash_Spawn(play, &p, nullptr, nullptr, 1, 600);
            EffectSsGRipple_Spawn(play, &p, 200, 600, 0);
            Audio_PlayActorSound2(&c->actor, NA_SE_EV_BOMB_DROP_WATER);
            Voice(&c->actor, d.voiceAttack);
            c->hitActive = 6;
        }
    } else if (c->shotsLeft > 0) {
        if (--c->timer <= 0) {
            switch (c->move) {
                case MOVE_SEED:
                    FireArrow(play, c, ARROW_SEED);
                    c->timer = 7;
                    break;
                case MOVE_NUT:
                    FireArrow(play, c, ARROW_NUT);
                    Voice(&c->actor, d.voiceAttack);
                    break;
                case MOVE_ICE:
                    FireArrow(play, c, ARROW_ICE);
                    Voice(&c->actor, d.voiceAttack);
                    break;
                case MOVE_LIGHT:
                    FireArrow(play, c, ARROW_LIGHT);
                    Voice(&c->actor, d.voiceAttack);
                    break;
                case MOVE_CUCCO:
                    SendCuccos(play, c);
                    break;
                default:
                    break;
            }
            if (--c->shotsLeft <= 0) {
                c->timer = 14; // (the follow-through)
            }
        }
        return;
    }
    if (c->shotsLeft <= 0) {
        if (--c->timer <= 0) {
            c->state = ST_FOLLOW;
        }
    }
}

// The fight: what she does when there's an enemy near Link
static bool ThinkFight(PlayState* play, Comp* c, Player* player) {
    const PersonaDef& d = Def(c);

    if (!CVarGetInteger(COMP_CVAR("Fight"), 1)) {
        return false;
    }
    // Link badly hurt: her help comes first
    if ((d.mSupport != MOVE_NONE) && (c->cdSupport <= 0) &&
        (gSaveContext.health <= (s16)(gSaveContext.healthCapacity * Tune("help_below", 0.34f))) &&
        (gSaveContext.healthAccumulator == 0)) {
        c->state = ST_SUPPORT;
        c->timer = 40;
        c->actor.speedXZ = 0.0f;
        c->cdSupport = (s16)Tune("support_cooldown", 900.0f);
        SetAnim(c, d.support, 1.0f, ANIMMODE_LOOP, -6.0f);
        return true;
    }
    if (((play->gameplayFrames + c->who * 3) % 8) == 0) {
        c->target = FindTarget(play, c);
    } else if ((c->target != nullptr) && (!ActorExists(play, c->target) || !Fightable(c->target))) {
        c->target = nullptr;
    }
    if (c->target == nullptr) {
        return false;
    }
    f32 dist = Actor_WorldDistXZToActor(&c->actor, c->target);
    if ((d.mClose != MOVE_NONE) && (dist < Tune("close_range", 130.0f)) && (c->cdClose <= 0)) {
        StartAttack(play, c, d.mClose);
        return true;
    }
    if ((d.mMelee != MOVE_NONE) && (c->cdMelee <= 0) && (dist < Tune("melee_range", 260.0f)) &&
        ((d.mRanged == MOVE_NONE) || (dist < 140.0f))) {
        StartAttack(play, c, d.mMelee);
        return true;
    }
    if ((d.mRanged != MOVE_NONE) && (c->cdRanged <= 0)) {
        StartAttack(play, c, d.mRanged);
        return true;
    }
    return false;
}

static bool MixamoOn();

// ---- Holding hands ----

static void ToggleHold(PlayState* play, Comp* c, Player* player) {
    if (c->holdSide != 0) {
        c->holdSide = 0;
        SayKind(c->who, L_LET_GO);
        return;
    }
    // His right hand first, then his left (two can hold on)
    bool rightTaken = false, leftTaken = false;
    for (int i = 0; i < COMP_COUNT; i++) {
        if ((sComp[i] != nullptr) && (sComp[i] != c)) {
            rightTaken |= (sComp[i]->holdSide == 1);
            leftTaken |= (sComp[i]->holdSide == -1);
        }
    }
    if (rightTaken && leftTaken) {
        // (both his hands are full: the one who's held longest lets go)
        for (int i = 0; i < COMP_COUNT; i++) {
            if ((sComp[i] != nullptr) && (sComp[i] != c) && (sComp[i]->holdSide == 1)) {
                sComp[i]->holdSide = 0;
            }
        }
        rightTaken = false;
    }
    c->holdSide = rightTaken ? -1 : 1;
    c->catchingUp = 0;
    c->wandering = 0;
    SayKind(c->who, L_HOLD);
    Voice(&c->actor, Def(c).voiceHappy);
}

static Vec3f HoldPos(Comp* c, Player* player) {
    s16 yaw = player->actor.shape.rot.y;
    f32 side = Tune(c->adult ? "hold_distance_adult" : "hold_distance_child", c->adult ? 26.0f : 22.0f);
    Vec3f p = player->actor.world.pos;
    // (at his side, his right is -x of his facing)
    p.x -= Math_CosS(yaw) * side * (f32)c->holdSide;
    p.z += Math_SinS(yaw) * side * (f32)c->holdSide;
    p.x -= Math_SinS(yaw) * Tune("hold_behind", 3.0f);
    p.z -= Math_CosS(yaw) * Tune("hold_behind", 3.0f);
    return p;
}

// Walking hand in hand: right at his side, facing where he faces, at his pace. She lets go when he leaves the
// ground for long, swims, climbs, or gets pulled away from her
static bool FollowHolding(PlayState* play, Comp* c, Player* player) {
    if (c->holdSide == 0) {
        return false;
    }
    Vec3f goal = HoldPos(c, player);
    f32 dist = Math_Vec3f_DistXZ(&c->actor.world.pos, &goal);
    // B (his sword) lets go
    if (CHECK_BTN_ALL(play->state.input[0].press.button, BTN_B) && !(player->stateFlags1 & PLAYER_STATE1_TALKING)) {
        c->holdSide = 0;
        SayKind(c->who, L_LET_GO);
        return false;
    }
    if (LinkSwimming(player) ||
        (player->stateFlags1 & (PLAYER_STATE1_CLIMBING_LADDER | PLAYER_STATE1_HANGING_OFF_LEDGE)) ||
        (dist > Tune("hold_break_distance", 140.0f)) ||
        (fabsf(player->actor.world.pos.y - c->actor.world.pos.y) > 60.0f)) {
        c->holdSide = 0;
        SayKind(c->who, L_LET_GO);
        return false;
    }
    // Moves the gap each frame (and no more), so she keeps step with him
    s16 toGoal = Math_Vec3f_Yaw(&c->actor.world.pos, &goal);
    c->actor.world.rot.y = toGoal;
    c->actor.speedXZ = CLAMP(dist, 0.0f, std::max(player->actor.speedXZ + 3.0f, 8.0f));
    if (dist < 1.0f) {
        c->actor.speedXZ = 0.0f;
    }
    Math_SmoothStepToS(&c->actor.shape.rot.y, player->actor.shape.rot.y, 3, 0x1800, 0x100);
    // (her walk is from his speed, not from these little steps)
    c->holdWalk = player->actor.speedXZ;
    SetAnim(c, Def(c).idle, 1.0f, ANIMMODE_LOOP, -6.0f);
    return true;
}

// How far from Link she lets herself be before she goes to him (each a little different)
static f32 Leash(Comp* c) {
    return Tune("leash", 230.0f) + (f32)c->formation * Tune("leash_step", 30.0f);
}

// A spot near her to stroll to: on solid ground, near her own height, not too far from Link, and dry unless she swims
// ---- Exploring ----
// While Link lingers, a girl now and then goes to look at something in the room that catches her eye (each has her
// own tastes) and says something about it: a chest, a sign, pots and crates, a person, a door, a Gold Skulltula, a
// Gossip Stone, a heart or rupee on the ground, an animal. Out of reach (up a wall, over a gap) she points from where
// she is. She doesn't open, break or take anything: it's Link's adventure. Anything that needs Link's lens, or a chest
// that hasn't appeared yet, she can't see either.

enum PoiKind { POI_CHEST, POI_SIGN, POI_POT, POI_NPC, POI_DOOR, POI_SKULL, POI_STONE, POI_ITEM, POI_ANIMAL, POI_COUNT };
static const int sPoiLine[POI_COUNT] = { L_SEE_CHEST, L_SEE_SIGN, L_SEE_POT,  L_SEE_NPC,   L_SEE_DOOR,
                                         L_SEE_SKULL, L_SEE_STONE, L_SEE_ITEM, L_SEE_ANIMAL };
// How close she goes, and how sure she is to say something
static const f32 sPoiStop[POI_COUNT] = { 50.0f, 45.0f, 38.0f, 65.0f, 60.0f, 80.0f, 50.0f, 28.0f, 55.0f };
static const f32 sPoiSay[POI_COUNT] = { 0.9f, 0.6f, 0.4f, 0.6f, 0.5f, 1.0f, 0.8f, 0.9f, 0.7f };
// What catches whose eye (chest, sign, pot, person, door, skulltula, stone, item, animal)
static const f32 sPoiInterest[P_COUNT][POI_COUNT] = {
    { 0.6f, 0.4f, 0.3f, 0.7f, 0.4f, 0.8f, 0.9f, 0.6f, 0.8f }, // Saria: people, nature, the Gossip Stones
    { 1.0f, 0.3f, 0.6f, 0.4f, 0.5f, 0.6f, 0.4f, 0.9f, 0.3f }, // young Ruto: treasure and shiny things
    { 0.9f, 0.5f, 0.3f, 0.5f, 0.6f, 0.7f, 0.5f, 0.7f, 0.3f }, // adult Ruto
    { 0.6f, 0.4f, 0.7f, 0.6f, 0.4f, 0.6f, 0.4f, 0.7f, 1.0f }, // young Malon: animals!
    { 0.6f, 0.4f, 0.6f, 0.6f, 0.4f, 0.6f, 0.4f, 0.8f, 1.0f }, // adult Malon
    { 0.5f, 1.0f, 0.3f, 0.6f, 0.7f, 0.6f, 1.0f, 0.5f, 0.5f }, // young Zelda: writing, history, secrets
    { 0.6f, 1.0f, 0.3f, 0.6f, 0.8f, 0.7f, 1.0f, 0.6f, 0.4f }, // adult Zelda
};

static Actor* sPoiSeen[40];
static int sPoiSeenCount = 0;

static bool PoiSeen(Actor* a) {
    for (int i = 0; i < sPoiSeenCount; i++) {
        if (sPoiSeen[i] == a) {
            return true;
        }
    }
    return false;
}

static void MarkPoiSeen(Actor* a) {
    if ((a == nullptr) || PoiSeen(a)) {
        return;
    }
    if (sPoiSeenCount < (int)ARRAY_COUNT(sPoiSeen)) {
        sPoiSeen[sPoiSeenCount++] = a;
    } else {
        memmove(&sPoiSeen[0], &sPoiSeen[1], sizeof(sPoiSeen) - sizeof(sPoiSeen[0]));
        sPoiSeen[ARRAY_COUNT(sPoiSeen) - 1] = a;
    }
}

static int PoiKindOf(PlayState* play, Actor* a) {
    if ((a->update == nullptr) || (a->draw == nullptr) || (a->flags & ACTOR_FLAG_REACT_TO_LENS)) {
        return -1;
    }
    if ((a->id == sCompActorId) || (a->id == sCuccoActorId)) {
        return -1;
    }
    switch (a->id) {
        case ACTOR_EN_BOX: {
            EnBox* box = (EnBox*)a;
            if (Flags_GetTreasure(play, a->params & 0x1F) || (box->alpha == 0)) {
                return -1; // (opened, or not there yet)
            }
            return POI_CHEST;
        }
        case ACTOR_EN_KANBAN:
            return POI_SIGN;
        case ACTOR_OBJ_TSUBO:
        case ACTOR_OBJ_KIBAKO:
        case ACTOR_OBJ_KIBAKO2:
            return POI_POT;
        case ACTOR_EN_SW:
            return (((a->params & 0xE000) >> 0xD) > 0) ? POI_SKULL : -1;
        case ACTOR_EN_GS:
            return POI_STONE;
        case ACTOR_EN_ITEM00:
            return POI_ITEM;
        case ACTOR_EN_NIW:
        case ACTOR_EN_COW:
        case ACTOR_EN_DOG:
            return POI_ANIMAL;
        default:
            break;
    }
    if (a->category == ACTORCAT_NPC) {
        return POI_NPC;
    }
    if (a->category == ACTORCAT_DOOR) {
        return POI_DOOR;
    }
    return -1;
}

static bool PoiExists(PlayState* play, Actor* a) {
    if (a == nullptr) {
        return false;
    }
    for (int cat = 0; cat < ACTORCAT_MAX; cat++) {
        for (Actor* e = play->actorCtx.actorLists[cat].head; e != nullptr; e = e->next) {
            if (e == a) {
                return true;
            }
        }
    }
    return false;
}

// Something she can see from here (no wall between her eyes and it)
static bool CanSee(PlayState* play, Comp* c, Actor* a) {
    Vec3f from = c->actor.world.pos;
    from.y += Def(c).height * c->actor.scale.y * 0.8f;
    Vec3f to = a->world.pos;
    to.y += 15.0f;
    Vec3f hit;
    CollisionPoly* poly;
    s32 bgId;
    return !BgCheck_EntityLineTest1(&play->colCtx, &from, &to, &hit, &poly, true, true, true, true, &bgId);
}

static int ExploringCount() {
    int n = 0;
    for (int i = 0; i < COMP_COUNT; i++) {
        n += ((sComp[i] != nullptr) && (sComp[i]->state == ST_EXPLORE)) ? 1 : 0;
    }
    return n;
}

static bool StartExplore(PlayState* play, Comp* c, Player* player) {
    if ((CVarGetInteger(COMP_CVAR("Explore"), 1) == 0) || (c->exploreCd > 0) || c->swimming ||
        (ExploringCount() >= (int)Tune("explore_max", 2.0f)) || (player->actor.speedXZ > Tune("explore_link_speed", 3.0f))) {
        return false;
    }
    f32 radius = Tune("explore_radius", 380.0f);
    Actor* best = nullptr;
    int bestKind = -1;
    f32 bestScore = Tune("explore_min_interest", 0.12f);
    for (int cat = 0; cat < ACTORCAT_MAX; cat++) {
        if ((cat == ACTORCAT_PLAYER) || (cat == ACTORCAT_EXPLOSIVE) || (cat == ACTORCAT_ITEMACTION)) {
            continue;
        }
        for (Actor* a = play->actorCtx.actorLists[cat].head; a != nullptr; a = a->next) {
            int kind = PoiKindOf(play, a);
            if ((kind < 0) || PoiSeen(a)) {
                continue;
            }
            f32 fromLink = Actor_WorldDistXZToActor(&player->actor, a);
            f32 fromHer = Actor_WorldDistXZToActor(&c->actor, a);
            if ((fromLink > radius) || (fromHer > radius * 1.3f) || (fabsf(a->world.pos.y - c->actor.world.pos.y) > 300.0f)) {
                continue;
            }
            f32 score = sPoiInterest[c->persona][kind] * (0.6f + 0.4f * Rand_ZeroOne()) / (1.0f + fromHer / 250.0f);
            // (a heart when he's hurt: worth pointing out)
            if ((kind == POI_ITEM) && (gSaveContext.health <= gSaveContext.healthCapacity / 2)) {
                score *= 2.0f;
            }
            if ((score > bestScore) && CanSee(play, c, a)) {
                bestScore = score;
                best = a;
                bestKind = kind;
            }
        }
    }
    if (best == nullptr) {
        c->exploreCd = (s16)(Tune("explore_retry_seconds", 4.0f) * COMP_FPS);
        return false;
    }
    MarkPoiSeen(best); // (no one else goes to the same thing)
    c->state = ST_EXPLORE;
    c->poi = best;
    c->poiKind = (s8)bestKind;
    c->explorePhase = 0;
    c->exploreTimer = 0;
    c->exploreStall = 0;
    c->exploreBest = 1e9f;
    c->wandering = 0;
    // (high up or far down: she points from here)
    if (fabsf(best->world.pos.y - c->actor.world.pos.y) > 50.0f) {
        c->explorePhase = 1;
    }
    return true;
}

static void EndExplore(Comp* c) {
    c->state = ST_FOLLOW;
    c->poi = nullptr;
    c->eyeOverride = -1;
    c->actor.speedXZ = 0.0f;
    c->wanderTimer = Rand_S16Offset(40, 80);
    c->exploreCd = (s16)((Tune("explore_min_seconds", 7.0f) + Rand_ZeroOne() * Tune("explore_extra_seconds", 10.0f)) *
                         COMP_FPS);
}

static void UpdateExplore(PlayState* play, Comp* c, Player* player) {
    const PersonaDef& d = Def(c);
    c->exploreTimer++;
    // Link heading off, an enemy, the thing gone, or taking too long: back to him
    if (!PoiExists(play, c->poi) || (Actor_WorldDistXZToActor(&c->actor, &player->actor) > Leash(c) * 1.4f) ||
        (c->exploreTimer > (s16)(Tune("explore_max_seconds", 9.0f) * COMP_FPS))) {
        EndExplore(c);
        return;
    }
    if (((c->exploreTimer % 10) == 0) && (FindTarget(play, c) != nullptr)) {
        EndExplore(c);
        return;
    }
    Actor* a = c->poi;
    f32 dist = Actor_WorldDistXZToActor(&c->actor, a);
    if (c->explorePhase == 0) {
        // Over to it
        bool arrived = dist < sPoiStop[c->poiKind];
        // (not getting any closer: a wall, something in the way)
        if (dist < c->exploreBest - 2.0f) {
            c->exploreBest = dist;
            c->exploreStall = 0;
        } else if (++c->exploreStall > 25) {
            arrived = true;
        }
        // (the ground falls away ahead, or deep water: she stops at the edge)
        s16 yaw = Math_Vec3f_Yaw(&c->actor.world.pos, &a->world.pos);
        Vec3f ahead = c->actor.world.pos;
        ahead.x += Math_SinS(yaw) * 25.0f;
        ahead.z += Math_CosS(yaw) * 25.0f;
        ahead.y += 40.0f;
        CollisionPoly* poly;
        s32 bgId;
        f32 floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &ahead);
        f32 waterY;
        WaterBox* wb;
        if ((floorY <= BGCHECK_Y_MIN) || (floorY < c->actor.world.pos.y - 40.0f) ||
            ((d.swim == nullptr) && WaterBox_GetSurface1(play, &play->colCtx, ahead.x, ahead.z, &waterY, &wb) &&
             (waterY > floorY + 15.0f))) {
            arrived = true;
        }
        if (arrived) {
            c->explorePhase = 1;
            c->exploreTimer = 0;
            c->actor.speedXZ = 0.0f;
        } else {
            c->actor.world.rot.y = yaw;
            Math_SmoothStepToS(&c->actor.shape.rot.y, yaw, 3, 0x1000, 0x100);
            Math_ApproachF(&c->actor.speedXZ, Tune("walk_speed", 2.5f) * Tune("explore_speed", 1.0f), 0.5f, 0.6f);
            bool mixWalk = MixamoOn() && (c->rigState == 1) && (c->rig.lThigh != 0);
            if ((d.move != nullptr) && !mixWalk && !(c->linkWalk && CVarGetInteger(COMP_CVAR("LinkWalk"), 1))) {
                SetAnim(c, d.move, std::max(0.6f, c->actor.speedXZ * d.moveAnimSpeed), ANIMMODE_LOOP, -5.0f);
            } else {
                SetAnim(c, d.idle, 1.0f, ANIMMODE_LOOP, -6.0f);
            }
            return;
        }
    }
    // Looking at it, and saying something
    c->actor.speedXZ = 0.0f;
    s16 yaw = Math_Vec3f_Yaw(&c->actor.world.pos, &a->world.pos);
    Math_SmoothStepToS(&c->actor.shape.rot.y, yaw, 3, 0x1000, 0x100);
    c->actor.world.rot.y = c->actor.shape.rot.y;
    if (c->exploreTimer == 6) {
        if (Rand_ZeroOne() < sPoiSay[c->poiKind] * Tune("explore_talk", 1.0f)) {
            SayKind(c->who, sPoiLine[c->poiKind]);
        }
        const char* anim = (c->poiKind == POI_ANIMAL || c->poiKind == POI_ITEM) ? d.happy
                           : (c->poiKind == POI_NPC)                            ? d.wave
                           : (d.worried != nullptr)                             ? d.worried
                                                                                : d.idle;
        SetAnim(c, (anim != nullptr) ? anim : d.idle, 1.0f, ANIMMODE_LOOP, -6.0f);
    }
    if (c->exploreTimer > (s16)(Tune("explore_look_seconds", 3.0f) * COMP_FPS)) {
        EndExplore(c);
    }
}

static bool PickWanderSpot(PlayState* play, Comp* c, Player* player) {
    for (int tries = 0; tries < 4; tries++) {
        s16 yaw = (s16)(Rand_ZeroOne() * 65535.0f);
        f32 r = Tune("wander_radius", 90.0f) * (0.4f + Rand_ZeroOne() * 0.6f);
        Vec3f p = c->actor.world.pos;
        p.x += Math_SinS(yaw) * r;
        p.z += Math_CosS(yaw) * r;
        Vec3f lp = player->actor.world.pos;
        if (Math_Vec3f_DistXZ(&p, &lp) > Leash(c) * 0.8f) {
            continue;
        }
        Vec3f top = p;
        top.y += 50.0f;
        CollisionPoly* poly;
        s32 bgId;
        f32 floorY = BgCheck_EntityRaycastFloor3(&play->colCtx, &poly, &bgId, &top);
        if ((floorY <= BGCHECK_Y_MIN) || (fabsf(floorY - c->actor.world.pos.y) > 30.0f)) {
            continue;
        }
        f32 waterY;
        WaterBox* wb;
        if ((Def(c).swim == nullptr) && WaterBox_GetSurface1(play, &play->colCtx, p.x, p.z, &waterY, &wb) &&
            (waterY > floorY + 15.0f)) {
            continue;
        }
        c->wanderGoal = p;
        return true;
    }
    return false;
}

static void Follow(PlayState* play, Comp* c, Player* player) {
    const PersonaDef& d = Def(c);
    c->holdWalk = -1.0f;
    if (FollowHolding(play, c, player)) {
        return;
    }
    f32 toLink = Actor_WorldDistXZToActor(&c->actor, &player->actor);
    bool waitAtShore = LinkSwimming(player) && (d.swim == nullptr);
    f32 leash = Leash(c);
    f32 nearDist = Tune("catch_up_distance", 80.0f) + (f32)c->formation * 18.0f;

    c->swimming = (d.swim != nullptr) && (c->actor.bgCheckFlags & BGCHECKFLAG_WATER) && (c->actor.yDistToWater > 30.0f);

    if (waitAtShore) {
        // She can't swim: she waits where the water starts
        if (!c->waitSaid) {
            SayKind(c->who, L_WATER);
            c->waitSaid = 1;
        }
    } else {
        c->waitSaid = 0;
    }
    if ((c->actor.bgCheckFlags & BGCHECKFLAG_WATER) && (d.swim == nullptr) && (c->actor.yDistToWater > 25.0f) &&
        !waitAtShore) {
        // (walked into deep water anyway: back to him)
        WarpToLink(play, c, player);
        return;
    }

    // She stays where she is while he's near (he can walk right up to her), strolls about a little, and only goes
    // to him once he's got far: to a spot on her side of him, not behind him
    if (!waitAtShore && (toLink > leash)) {
        c->catchingUp = 1;
        c->wandering = 0;
    }
    Vec3f goal = c->actor.world.pos;
    f32 want = 0.0f;
    // She runs as fast as Link's top running speed (whatever it is: it's watched as he runs)
    static f32 sLinkTop = 0.0f;
    if ((player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) && !(player->stateFlags1 & PLAYER_STATE1_ON_HORSE)) {
        sLinkTop = std::max(sLinkTop, std::min(player->actor.speedXZ, 20.0f));
    }
    f32 runSpeed = std::max(Tune("run_speed", 6.0f), sLinkTop * Tune("run_match", 1.0f));
    f32 walkSpeed = Tune("walk_speed", 2.5f);

    if (c->catchingUp && !waitAtShore) {
        s16 fromLink = Math_Vec3f_Yaw(&player->actor.world.pos, &c->actor.world.pos);
        goal = player->actor.world.pos;
        goal.x += Math_SinS(fromLink) * nearDist;
        goal.z += Math_CosS(fromLink) * nearDist;
        if (toLink < nearDist + 25.0f) {
            c->catchingUp = 0;
            c->wanderTimer = Rand_S16Offset(40, 80);
        } else if (toLink > leash * 1.3f) {
            want = std::max(runSpeed, player->actor.speedXZ * 1.05f);
        } else {
            want = std::max(walkSpeed * 1.4f, std::min(std::max(player->actor.speedXZ, walkSpeed * 1.4f), runSpeed));
        }
    } else if (!waitAtShore) {
        if (c->wandering) {
            goal = c->wanderGoal;
            if (Math_Vec3f_DistXZ(&c->actor.world.pos, &goal) < 12.0f) {
                c->wandering = 0;
                c->wanderTimer = Rand_S16Offset(50, 120);
            } else {
                want = walkSpeed * Tune("wander_speed", 0.7f);
            }
        } else if (--c->wanderTimer <= 0) {
            // Now and then something catches her eye, or a little walk
            if ((Rand_ZeroOne() < Tune("explore_chance", 0.6f)) && StartExplore(play, c, player)) {
                return;
            }
            if ((Rand_ZeroOne() < Tune("wander_chance", 0.6f)) && PickWanderSpot(play, c, player)) {
                c->wandering = 1;
            }
            c->wanderTimer = Rand_S16Offset(60, 120);
        }
    }
    f32 dist = Math_Vec3f_DistXZ(&c->actor.world.pos, &goal);

    Math_ApproachF(&c->actor.speedXZ, want, 0.5f, 1.2f);
    if ((c->actor.speedXZ > 0.5f) && (dist > 1.0f)) {
        s16 yaw = Math_Vec3f_Yaw(&c->actor.world.pos, &goal);
        Math_SmoothStepToS(&c->actor.world.rot.y, yaw, 3, 0x1800, 0x100);
        c->actor.shape.rot.y = c->actor.world.rot.y;
    } else if ((player->stateFlags1 & PLAYER_STATE1_FIRST_PERSON) && (toLink < 600.0f)) {
        // Standing: she looks where she likes, but stares back when he looks around in first person
        Math_SmoothStepToS(&c->actor.shape.rot.y, c->actor.yawTowardsPlayer, 4, 0x800, 0x80);
        c->actor.world.rot.y = c->actor.shape.rot.y;
    }

    // Stuck behind something while far: she catches up
    f32 moved = Math_Vec3f_DistXZ(&c->actor.world.pos, &c->actor.prevPos);
    if (c->wandering && (moved < 0.3f) && (want > 0.0f) && (c->stuckTimer > 10)) {
        c->wandering = 0; // (a stroll into a wall: given up)
    }
    if ((dist > 60.0f) && (moved < 0.5f) && (want > 0.0f)) {
        c->stuckTimer++;
    } else {
        c->stuckTimer = 0;
    }
    // In the same area she never warps while she can get to him: she runs to him, and only if she gets no closer for
    // a while (a wall, a gap, a ledge she can't climb) does she appear at his side. Or if she fell out of the world
    f32 toLink3 = Actor_WorldDistXYZToActor(&c->actor, &player->actor);
    if (c->catchingUp && !waitAtShore) {
        if ((c->bestDist <= 0.0f) || (toLink3 < c->bestDist - Tune("progress_step", 8.0f))) {
            c->bestDist = toLink3;
            c->noProgress = 0;
        } else {
            c->noProgress++;
        }
    } else {
        c->bestDist = 0.0f;
        c->noProgress = 0;
    }
    bool fellOut = (c->actor.floorHeight <= BGCHECK_Y_MIN + 10.0f) && !(c->actor.bgCheckFlags & BGCHECKFLAG_GROUND) &&
                   (c->actor.world.pos.y < player->actor.world.pos.y - 400.0f);
    if (!waitAtShore && (fellOut || (c->noProgress > (s16)(Tune("warp_after_seconds", 10.0f) * COMP_FPS)))) {
        if ((player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) || LinkSwimming(player)) {
            WarpToLink(play, c, player);
            c->bestDist = 0.0f;
            c->noProgress = 0;
        }
    }

    // Her walk
    if (c->swimming) {
        SetAnim(c, d.swim, 1.0f, ANIMMODE_LOOP, -6.0f);
    } else if (c->actor.speedXZ > 0.8f) {
        if ((d.move != nullptr) && !(c->linkWalk && CVarGetInteger(COMP_CVAR("LinkWalk"), 1)) &&
            !(MixamoOn() && (c->rigState == 1) && (c->rig.lThigh != 0))) {
            SetAnim(c, d.move, std::max(0.6f, c->actor.speedXZ * d.moveAnimSpeed), ANIMMODE_LOOP, -5.0f);
        } else {
            SetAnim(c, d.idle, 1.0f, ANIMMODE_LOOP, -6.0f);
        }
    } else if (c->state == ST_FOLLOW) {
        SetAnim(c, d.idle, 1.0f, ANIMMODE_LOOP, -8.0f);
    }
}

static void Emote(Comp* c, const char* anim, s16 frames, s16 eye) {
    // (hand in hand she keeps walking with him)
    // (nor does one interrupt a hug, a talk, a song or her lying down: a hug left that way never ended)
    if ((anim == nullptr) || (c->holdSide != 0) || (c->state == ST_HUG) || (c->state == ST_TALK) ||
        (c->state == ST_REST) || (c->state == ST_KO)) {
        return;
    }
    c->state = ST_EMOTE;
    c->timer = frames;
    c->eyeOverride = eye;
    c->actor.speedXZ = 0.0f;
    SetAnim(c, anim, 1.0f, ANIMMODE_LOOP, -6.0f);
    if (anim == Def(c).happy) {
        Voice(&c->actor, Def(c).voiceHappy);
    }
}

// ---- Hugs ----

// During a hug the right stick turns the camera freely (the game's free look, switched on just for the hug)
static s8 sHugFreeLookWas = -1;

// The right stick turns the camera while Link is held still (a hug, sitting for a song)
static void HeldCameraOn() {
    if (sHugFreeLookWas < 0) {
        sHugFreeLookWas = (s8)CVarGetInteger(CVAR_SETTING("FreeLook.Enabled"), 0);
        CVarSetInteger(CVAR_SETTING("FreeLook.Enabled"), 1);
    }
}
static void HeldCameraOff(PlayState* play) {
    if (sHugFreeLookWas == 0) {
        CVarSetInteger(CVAR_SETTING("FreeLook.Enabled"), 0);
        if (play != nullptr) {
            play->manualCamera = false;
        }
    }
    sHugFreeLookWas = -1;
}

static void EndHug(PlayState* play, Comp* c) {
    if (sHugWho == c->who) {
        sHugWho = -1;
        Player_SetCsAction(play, nullptr, PLAYER_CSACTION_7); // (he's free again)
        HeldCameraOff(play);
    }
    c->state = ST_FOLLOW;
    c->eyeOverride = -1;
    Emote(c, Def(c).happy, 30, -1);
}

static void StartHug(PlayState* play, Comp* c, Player* player) {
    c->state = ST_HUG;
    c->timer = 0; // (frames into the hug: it lasts until he lets go)
    HeldCameraOn();
    c->actor.speedXZ = 0.0f;
    c->mouthTimer = 0;
    sHugWho = c->who;

    // Face to face, close
    s16 yaw = Math_Vec3f_Yaw(&player->actor.world.pos, &c->actor.world.pos);
    f32 dist = c->adult ? Tune("hug_distance_adult", 13.0f) : Tune("hug_distance_child", 11.0f);
    c->actor.world.pos.x = player->actor.world.pos.x + Math_SinS(yaw) * dist;
    c->actor.world.pos.z = player->actor.world.pos.z + Math_CosS(yaw) * dist;
    c->actor.world.pos.y = player->actor.world.pos.y;
    c->actor.shape.rot.y = c->actor.world.rot.y = (s16)(yaw + 0x8000);
    player->actor.shape.rot.y = player->actor.world.rot.y = player->yaw = yaw;
    Player_SetCsAction(play, nullptr, PLAYER_CSACTION_93);

    SetAnim(c, sHugAnim[c->persona], 1.0f, ANIMMODE_ONCE, -6.0f);
    c->eyeOverride = 2;
    Voice(&c->actor, Def(c).voiceHappy);
    Sfx_PlaySfxCentered(NA_SE_EV_FAIRY_DASH);
    gSaveContext.healthAccumulator = (s16)(gSaveContext.healthAccumulator + (s16)Tune("hug_heal_quarters", 4.0f) * 4);
    SayKind(c->who, L_HUG);

    // Someone else sees it
    for (int i = 0; i < COMP_COUNT; i++) {
        if ((i != c->who) && (sComp[i] != nullptr) && !sComp[i]->hidden && (sComp[i]->state != ST_KO) &&
            (Rand_ZeroOne() < Tune("hug_seen_chance", 0.5f))) {
            std::string line = Pick(sLines[sComp[i]->persona][L_HUG_SEEN]);
            if (!line.empty()) {
                sPending.push_back({ i, line, (s32)(Tune("bubble_seconds", 2.5f) * COMP_FPS) });
            }
            break;
        }
    }
}

static void UpdateHug(PlayState* play, Comp* c, Player* player) {
    c->actor.speedXZ = 0.0f;
    // Link holds her (his arms forward, kept while the hug lasts)
    if (player->csAction == PLAYER_CSACTION_93) {
        const char* a = sLinkHugAnims[CLAMP((int)Tune("hug_link_anim", 0.0f), 0, 3)];
        if (player->skelAnime.animation != (LinkAnimationHeader*)a) {
            LinkAnimation_PlayLoop(play, &player->skelAnime, (LinkAnimationHeader*)a);
        }
    } else if (c->timer > 3) {
        Player_SetCsAction(play, nullptr, PLAYER_CSACTION_93);
    }
    // Sparkles over them
    c->timer = (s16)MIN(c->timer + 1, 30000);
    if ((c->timer % 8) == 0) {
        Vec3f p = c->actor.world.pos;
        p.x = (p.x + player->actor.world.pos.x) * 0.5f + Rand_CenteredFloat(20.0f);
        p.z = (p.z + player->actor.world.pos.z) * 0.5f + Rand_CenteredFloat(20.0f);
        p.y += Def(c).height * c->actor.scale.y + Rand_ZeroFloat(10.0f);
        Vec3f vel = { 0.0f, 0.6f, 0.0f };
        Vec3f accel = { 0.0f, 0.05f, 0.0f };
        EffectSsKiraKira_SpawnSmallYellow(play, &p, &vel, &accel);
    }
    // It lasts until he lets go: A or B (after a moment, so the press that chose Hug doesn't end it)
    u16 press = play->state.input[0].press.button;
    if ((c->timer > 15) && (CHECK_BTN_ANY(press, BTN_A | BTN_B))) {
        EndHug(play, c);
    }
}

// ---- Resting together: a song ----
// Link stands still a while: the girls sit around him and one of them performs. Saria plays Saria's Song on her
// ocarina, Malon sings Epona's Song (the ranch's song, her voice up front), Zelda hums Zelda's Lullaby. The others
// sit and listen, swaying a little. Moving (or a button, or an enemy) ends it.

#define SONG_START_SEQ(playerIdx, fadeTimer, seqId) \
    Audio_QueueSeqCmd(0x00000000 | ((u8)(playerIdx) << 24) | ((u8)(fadeTimer) << 0x10) | (u16)(seqId))


struct SongState {
    s8 performer = -1; // who's performing (-1: no song)
    s8 last = -1;      // who performed last time (the next one is someone else, if there's anyone else)
    u16 seq = 0;
    u16 prevSeq = NA_BGM_DISABLED;
    bool music = false;
    s32 timer = 0;
    s32 frames = 0;
    bool listenSaid = false;
};
static SongState sSong;

static bool RestSongsOn() {
    return CVarGetInteger(COMP_CVAR("RestSongs"), 1) != 0;
}

static u16 SongSeqFor(int persona) {
    switch (persona) {
        case P_SARIA:
            return NA_BGM_SARIA_THEME;
        case P_MALON_Y:
        case P_MALON_A:
            return NA_BGM_LONLON;
        case P_ZELDA_Y:
        case P_ZELDA_A:
            return NA_BGM_ZELDA_THEME;
        default:
            return 0;
    }
}

// Whether she's sitting on the ground now (her legs folded, her hips down)
static bool SitsNow(Comp* c) {
    const PersonaDef& d = Def(c);
    if (c->state == ST_REST) {
        // (Malon stands to sing, like at the ranch)
        bool standsToSing = (sSong.performer == c->who) && ((c->persona == P_MALON_Y) || (c->persona == P_MALON_A));
        return c->seated && !standsToSing;
    }
    if (c->anim == nullptr) {
        return false;
    }
    // (her own sitting animations are on a stump or a ledge: on the ground she folds her legs instead)
    if ((d.sit != nullptr) && (c->anim == d.sit)) {
        return true;
    }
    // Saria plays her ocarina sitting down
    return (c->persona == P_SARIA) && (c->anim == d.support);
}

static bool CanRest(Comp* c) {
    return (c != nullptr) && !c->hidden && !c->swimming &&
           ((c->state == ST_FOLLOW) || (c->state == ST_IDLE) || (c->state == ST_EMOTE) || (c->state == ST_EXPLORE));
}

static void EndSong(PlayState* play, bool finished) {
    if (sSong.performer < 0) {
        return;
    }
    int performer = sSong.performer;
    if (sLinkSits) {
        sLinkSits = false;
        if (play != nullptr) {
            Player_SetCsAction(play, nullptr, PLAYER_CSACTION_7); // (he gets up)
            if (sHugWho < 0) {
                HeldCameraOff(play);
            }
        }
    }
    sSong.last = sSong.performer;
    sSong.performer = -1;
    // The music back as it was (unless something else has changed it since)
    if (sSong.music && (play != nullptr) && ((func_800FA0B4(SEQ_PLAYER_BGM_MAIN) & 0xFF) == (sSong.seq & 0xFF))) {
        if (sSong.prevSeq == NA_BGM_NATURE_AMBIENCE) {
            Audio_PlayNatureAmbienceSequence(play->sequenceCtx.natureAmbienceId);
        } else if (sSong.prevSeq == NA_BGM_DISABLED) {
            Audio_QueueSeqCmd(0x100000FF | ((u8)SEQ_PLAYER_BGM_MAIN << 24)); // (stopped)
        } else {
            SONG_START_SEQ(SEQ_PLAYER_BGM_MAIN, 0, sSong.prevSeq);
        }
    }
    sSong.music = false;
    bool praised = false;
    for (int i = 0; i < COMP_COUNT; i++) {
        Comp* c = sComp[i];
        if ((c == nullptr) || (c->state != ST_REST)) {
            continue;
        }
        c->state = ST_FOLLOW;
        c->seated = 0;
        c->eyeOverride = -1;
        if (finished && (i != performer)) {
            Emote(c, Def(c).happy, 40, -1); // (clapping)
            if (!praised && (Rand_ZeroOne() < 0.7f)) {
                praised = true;
                std::string line = Pick(sLines[c->persona][L_SONG_PRAISE]);
                if (!line.empty()) {
                    sPending.push_back({ i, line, (s32)(1.2f * COMP_FPS) });
                }
            }
        }
    }
    if (finished && (sComp[performer] != nullptr)) {
        SayKind(performer, L_SONG_END);
    }
}

static bool StartSong(PlayState* play, Player* player) {
    if (!RestSongsOn() || (sSong.performer >= 0) || (sHugWho >= 0) ||
        !(player->actor.bgCheckFlags & BGCHECKFLAG_GROUND) || LinkSwimming(player) ||
        (player->stateFlags1 & PLAYER_STATE1_FIRST_PERSON) || (play->msgCtx.msgMode != MSGMODE_NONE)) {
        return false;
    }
    // Who could perform (someone else than last time, if there's anyone else)
    std::vector<int> can, fresh;
    int present = 0;
    for (int i = 0; i < COMP_COUNT; i++) {
        Comp* c = sComp[i];
        if (!CanRest(c)) {
            continue;
        }
        present++;
        // (no song with enemies about)
        if ((c->target != nullptr) || (FindTarget(play, c) != nullptr)) {
            return false;
        }
        if (SongSeqFor(c->persona) != 0) {
            can.push_back(i);
            if (i != sSong.last) {
                fresh.push_back(i);
            }
        }
    }
    if (can.empty()) {
        return false;
    }
    std::vector<int>& pool = fresh.empty() ? can : fresh;
    int who = pool[(size_t)(Rand_ZeroOne() * (f32)pool.size()) % pool.size()];
    Comp* p = sComp[who];

    sSong.performer = (s8)who;
    sSong.seq = SongSeqFor(p->persona);
    sSong.music = false;
    sSong.frames = 0;
    sSong.listenSaid = false;
    sSong.timer = (s32)(Tune("song_seconds", 45.0f) * COMP_FPS);

    // Seats: the performer in front of Link, the others in a half circle around him
    s16 face = player->actor.shape.rot.y;
    f32 front = Tune("song_performer_distance", 75.0f);
    f32 ring = Tune("song_ring_distance", 60.0f);
    static const s16 sAround[] = { 0x2C00, -0x2C00, 0x5000, -0x5000, 0x7000, -0x7000 };
    int k = 0;
    for (int i = 0; i < COMP_COUNT; i++) {
        Comp* c = sComp[i];
        if (!CanRest(c)) {
            continue;
        }
        s16 yaw = face;
        f32 dist = front;
        if (i != who) {
            yaw = (s16)(face + sAround[k % 6]);
            dist = ring + (f32)(k / 6) * 25.0f;
            k++;
        }
        c->seat = player->actor.world.pos;
        c->seat.x += Math_SinS(yaw) * dist;
        c->seat.z += Math_CosS(yaw) * dist;
        c->state = ST_REST;
        c->seated = 0;
        c->seatTimer = 0;
        c->target = nullptr;
        c->wandering = 0;
    }
    SayKind(who, L_SONG_START);
    return true;
}

// Each frame, while there's a song
static void UpdateSong(PlayState* play, Player* player) {
    if (sSong.performer < 0) {
        return;
    }
    Comp* p = sComp[sSong.performer];
    if ((p == nullptr) || (p->state != ST_REST) || !RestSongsOn()) {
        EndSong(play, false);
        return;
    }
    sSong.frames++;
    // Link moves, uses something, or gets busy: it's over
    Input* in = &play->state.input[0];
    u16 press = in->press.button;
    bool stick = (ABS(in->rel.stick_x) > 15) || (ABS(in->rel.stick_y) > 15);
    bool buttons = CHECK_BTN_ANY(press, BTN_A | BTN_B | BTN_Z | BTN_R | BTN_CLEFT | BTN_CRIGHT | BTN_CDOWN);
    if ((sSong.frames > 10) &&
        (stick || buttons || (player->actor.speedXZ > 0.5f) || (play->msgCtx.msgMode != MSGMODE_NONE))) {
        EndSong(play, false);
        return;
    }
    // An enemy turns up
    if ((sSong.frames % 10) == 0) {
        for (int i = 0; i < COMP_COUNT; i++) {
            if ((sComp[i] != nullptr) && (sComp[i]->state == ST_REST) && (FindTarget(play, sComp[i]) != nullptr)) {
                EndSong(play, false);
                return;
            }
        }
    }
    // The music starts once she's in her place (or after a moment)
    if (!sSong.music && (p->seated || (sSong.frames > 3 * COMP_FPS))) {
        sSong.music = true;
        sSong.prevSeq = func_800FA0B4(SEQ_PLAYER_BGM_MAIN);
        SONG_START_SEQ(SEQ_PLAYER_BGM_MAIN, 0, sSong.seq);
        // Link sits down with them, facing her (the right stick turns the camera)
        if ((Tune("link_sits", 1.0f) != 0.0f) && (player->actor.bgCheckFlags & BGCHECKFLAG_GROUND)) {
            sLinkSits = true;
            s16 yaw = Math_Vec3f_Yaw(&player->actor.world.pos, &p->actor.world.pos);
            player->actor.shape.rot.y = player->actor.world.rot.y = player->yaw = yaw;
            Player_SetCsAction(play, nullptr, PLAYER_CSACTION_93);
            HeldCameraOn();
        }
    }
    if (sLinkSits) {
        if (player->csAction != PLAYER_CSACTION_93) {
            Player_SetCsAction(play, nullptr, PLAYER_CSACTION_93);
        } else if (player->skelAnime.animation != (LinkAnimationHeader*)LINK_WAIT) {
            LinkAnimation_PlayLoop(play, &player->skelAnime, (LinkAnimationHeader*)LINK_WAIT);
        }
    }
    if (sSong.music) {
        if (sSong.seq == NA_BGM_LONLON) {
            Audio_UpdateMalonSinging(0.0f, NA_BGM_LONLON); // (her voice, as if he were right by her at the ranch)
        }
        // Halfway, someone listening says something
        if (!sSong.listenSaid && (sSong.timer < (s32)(Tune("song_seconds", 45.0f) * COMP_FPS * 0.55f))) {
            sSong.listenSaid = true;
            std::vector<int> listeners;
            for (int i = 0; i < COMP_COUNT; i++) {
                if ((i != sSong.performer) && (sComp[i] != nullptr) && (sComp[i]->state == ST_REST)) {
                    listeners.push_back(i);
                }
            }
            if (!listeners.empty() && (Rand_ZeroOne() < 0.8f)) {
                SayKind(listeners[(size_t)(Rand_ZeroOne() * (f32)listeners.size()) % listeners.size()],
                        L_SONG_LISTEN);
            }
        }
        if (--sSong.timer <= 0) {
            EndSong(play, true);
        }
    }
}

// Her part: to her seat, then sitting (or standing, singing) through the song
static void UpdateRest(PlayState* play, Comp* c, Player* player) {
    const PersonaDef& d = Def(c);
    bool performer = (sSong.performer == c->who);
    if (sSong.performer < 0) {
        c->state = ST_FOLLOW;
        c->seated = 0;
        return;
    }
    if (!c->seated) {
        f32 dx = c->seat.x - c->actor.world.pos.x;
        f32 dz = c->seat.z - c->actor.world.pos.z;
        f32 dist = sqrtf(dx * dx + dz * dz);
        if ((dist < 8.0f) || (++c->seatTimer > 3 * COMP_FPS)) {
            c->seated = 1;
            c->actor.speedXZ = 0.0f;
        } else {
            c->actor.world.rot.y = Math_Vec3f_Yaw(&c->actor.world.pos, &c->seat);
            Math_SmoothStepToS(&c->actor.shape.rot.y, c->actor.world.rot.y, 3, 0x1000, 0x100);
            c->actor.speedXZ = CLAMP(dist * 0.25f, 1.5f, 5.0f);
            bool mixWalk = MixamoOn() && (c->rigState == 1) && (c->rig.lThigh != 0);
            if ((d.move != nullptr) && !mixWalk && !(c->linkWalk && CVarGetInteger(COMP_CVAR("LinkWalk"), 1))) {
                SetAnim(c, d.move, std::max(0.6f, c->actor.speedXZ * d.moveAnimSpeed), ANIMMODE_LOOP, -5.0f);
            } else {
                SetAnim(c, d.idle, 1.0f, ANIMMODE_LOOP, -6.0f);
            }
            return;
        }
    }
    c->actor.speedXZ = 0.0f;
    // The performer faces Link; the others face her
    s16 yaw = c->actor.yawTowardsPlayer;
    if (!performer && (sComp[sSong.performer] != nullptr)) {
        yaw = Math_Vec3f_Yaw(&c->actor.world.pos, &sComp[sSong.performer]->actor.world.pos);
    }
    Math_SmoothStepToS(&c->actor.shape.rot.y, yaw, 4, 0x800, 0x80);
    c->actor.world.rot.y = c->actor.shape.rot.y;

    if (performer) {
        switch (c->persona) {
            case P_SARIA:
                SetAnim(c, d.support, 1.0f, ANIMMODE_LOOP, -8.0f); // (her ocarina)
                c->eyeOverride = 2;
                break;
            case P_MALON_Y:
            case P_MALON_A:
                SetAnim(c, d.support, 1.0f, ANIMMODE_LOOP, -8.0f); // (singing)
                if (sSong.music) {
                    c->mouthTimer = 12;
                }
                c->eyeOverride = ((sSong.frames / 60) % 3 == 2) ? 2 : -1;
                break;
            default:
                // Zelda hums, eyes closed
                SetAnim(c, d.idle, 1.0f, ANIMMODE_LOOP, -8.0f);
                c->eyeOverride = 2;
                if (sSong.music && ((sSong.frames / 40) % 2 == 0)) {
                    c->mouthTimer = 6;
                }
                break;
        }
    } else {
        // (young Ruto has her own sitting; the others sit with their hands on their knees)
        SetAnim(c, (c->persona == P_RUTO_Y) ? d.sit : d.idle, 1.0f, ANIMMODE_LOOP, -8.0f);
        // Eyes half closed now and then, enjoying it
        c->eyeOverride = (sSong.music && (((sSong.frames + c->who * 37) / 90) % 2 == 1)) ? 1 : -1;
    }
}

static void UpdateFace(Comp* c) {
    if (c->blinkTimer-- <= 0) {
        if (++c->eye > 2) {
            c->eye = 0;
            c->blinkTimer = Rand_S16Offset(30, 60);
        }
    }
    if (c->mouthTimer > 0) {
        c->mouthTimer--;
        c->mouth = ((c->mouthTimer / 3) % 2) ? 1 : 0;
    } else if (c->state == ST_KO || c->state == ST_HURT) {
        c->mouth = 2;
    } else if ((c->state == ST_EMOTE) || (c->state == ST_HUG)) {
        c->mouth = 1;
    } else {
        c->mouth = 0;
    }
}

// ---- Custom models ----
// A girl can be drawn with a Link-rigged model (converted from an OoT Online "zzplayas" model by
// tools/zobj2o2r.py into a mod archive in mods/): under "companions/<persona>[_2|_3]" a skel blob, the display
// lists, textures, eyes and mouths. Drawn on her own copy of Link's skeleton with Link's own animations.

struct CompModel {
    u8 tried, ok, dlCount;
    StandardLimb limbs[21];
    void* limbPtrs[21];
    std::string eyes[8], mouths[4];
};
static std::map<std::string, CompModel> sModels;

static u16 BE16(const u8* p) {
    return (u16)((p[0] << 8) | p[1]);
}
static u32 BE32(const u8* p) {
    return ((u32)p[0] << 24) | ((u32)p[1] << 16) | ((u32)p[2] << 8) | p[3];
}

static std::string ModelBase(int persona) {
    std::string base = std::string("companions/") + sPersonaKey[persona];
    if (persona == P_MALON_A) {
        int v = CVarGetInteger(COMP_CVAR("MalonAdultLook"), 0);
        if (v == 1) {
            base += "_2";
        } else if (v == 2) {
            base += "_3";
        }
    }
    return base;
}

static CompModel* FindModel(int persona) {
    if (!CVarGetInteger(COMP_CVAR("CustomModels"), 1)) {
        return nullptr;
    }
    std::string base = ModelBase(persona);
    CompModel& m = sModels[base];
    if (!m.tried) {
        m.tried = 1;
        std::string skelPath = base + "/skel";
        if (ResourceGetSizeByName(skelPath.c_str()) == 0) {
            return nullptr;
        }
        const u8* b = (const u8*)ResourceGetDataByName(skelPath.c_str());
        if ((b == nullptr) || (b[0] != 'C') || (b[1] != 'M') || (b[2] != 'D') || (b[3] != 'L') || (b[4] != 21)) {
            return nullptr;
        }
        m.dlCount = b[5];
        for (int i = 0; i < 21; i++) {
            const u8* l = b + 8 + i * 12;
            StandardLimb& limb = m.limbs[i];
            limb.jointPos.x = (s16)BE16(l);
            limb.jointPos.y = (s16)BE16(l + 2);
            limb.jointPos.z = (s16)BE16(l + 4);
            limb.child = l[6];
            limb.sibling = l[7];
            u32 dl = BE32(l + 8);
            limb.dList = nullptr;
            if (dl != 0) {
                char name[96];
                snprintf(name, sizeof(name), "%s/dl_%06X", base.c_str(), (unsigned)dl);
                limb.dList = (Gfx*)ResourceGetDataByName(name);
            }
            m.limbPtrs[i] = &limb;
        }
        for (int i = 0; i < 8; i++) {
            m.eyes[i] = "__OTR__" + base + "/eye_" + std::to_string(i);
        }
        for (int i = 0; i < 4; i++) {
            m.mouths[i] = "__OTR__" + base + "/mouth_" + std::to_string(i);
        }
        m.ok = 1;
    }
    return m.ok ? &m : nullptr;
}

static void ModelSkel(Comp* c) {
    // (her model's limbs over the joints Link's animations write)
    c->modelSkel = c->modelBody;
    c->modelSkel.skeleton = c->model->limbPtrs;
    c->modelSkel.limbCount = 22;
}

// ---- Mixamo motion ----

static bool MixamoOn() {
    return CVarGetInteger(COMP_CVAR("Mixamo"), 1) != 0;
}

// Arms around someone in front (x left, y up, z forward): upper arms forward and a little out, forearms wrapping in
static const float sHugDirs[8][3] = { { 0, 0, 0 },
                                      { 0, 0, 0 },
                                      { 0, 0, 0 },
                                      { 0, 0, 0 },
                                      { 0.4f, -0.15f, 0.9f },
                                      { -0.95f, 0.05f, 0.3f },
                                      { -0.4f, -0.15f, 0.9f },
                                      { 0.95f, 0.05f, 0.3f } };
static const bool sHugUse[8] = { false, false, false, false, true, true, true, true };

// Sitting on the ground: knees up, hands on the knees
static const float sSitDirs[8][3] = { { 0.18f, 0.35f, 0.92f },  { 0.05f, -0.55f, 0.83f },  { -0.18f, 0.35f, 0.92f },
                                      { -0.05f, -0.55f, 0.83f }, { 0.25f, -0.75f, 0.6f },   { 0.05f, -0.35f, 0.94f },
                                      { -0.25f, -0.75f, 0.6f },  { -0.05f, -0.35f, 0.94f } };
static const bool sSitUse[8] = { true, true, true, true, true, true, true, true };

// A point in the world, in an actor's model frame (its units, before its scale)
static Vec3f WorldToModel(Actor* a, Vec3f w) {
    f32 dx = w.x - a->world.pos.x, dy = w.y - a->world.pos.y, dz = w.z - a->world.pos.z;
    f32 cs = Math_CosS(a->shape.rot.y), sn = Math_SinS(a->shape.rot.y);
    f32 sc = (a->scale.y > 0.0f) ? a->scale.y : 0.01f;
    Vec3f m;
    m.x = (dx * cs - dz * sn) / sc;
    m.y = dy / sc - a->shape.yOffset;
    m.z = (dx * sn + dz * cs) / sc;
    return m;
}

// Where their hands meet: between them, at hand height
static Vec3f HoldMeetPoint(Comp* c, Player* player) {
    Vec3f m;
    m.x = (c->actor.world.pos.x + player->actor.world.pos.x) * 0.5f +
          Math_SinS(player->actor.shape.rot.y) * Tune("hold_hand_forward", 4.0f);
    m.z = (c->actor.world.pos.z + player->actor.world.pos.z) * 0.5f +
          Math_CosS(player->actor.shape.rot.y) * Tune("hold_hand_forward", 4.0f);
    m.y = player->actor.world.pos.y +
          (LINK_IS_ADULT ? Tune("hold_hand_height_adult", 28.0f) : Tune("hold_hand_height_child", 17.0f));
    return m;
}

// Link's side: his arms posed in his own draw (the game's hook in z_player_lib.c), for hugs and holding hands
extern "C" void (*gCompanionPlayerLimbHook)(PlayState* play, s32 limbIndex, Vec3f* pos, Vec3s* rot, Player* player);
static CompRigOut sLinkOut;
static f32 sLinkHug = 0.0f;
static f32 sLinkSit = 0.0f;
static const CompRigMap sLinkRig = { 7, 8, 9, 4, 5, 6, 14, 15, 16, 17, 18, 19 };

static void LinkLimbHook(PlayState* play, s32 limbIndex, Vec3f* pos, Vec3s* rot, Player* player) {
    if (limbIndex == 1) {
        // (once a frame, his pose final by now)
        sLinkOut.mask = 0;
        sLinkOut.rootDY = 0.0f;
        if (!On() || (player->actor.category != ACTORCAT_PLAYER)) {
            return;
        }
        Math_ApproachF(&sLinkHug, (sHugWho >= 0) ? 1.0f : 0.0f, 0.3f, 0.15f);
        if (sLinkHug > 0.0f) {
            CompRig_SolveDirs(&player->skelAnime, &sLinkRig, sHugDirs, sHugUse, sLinkHug, &sLinkOut);
        }
        // Sitting with them for a song: his hips down near the ground (a child's root height is scaled by 0.64
        // after this, so the drop is worked out before that)
        Math_ApproachF(&sLinkSit, sLinkSits ? 1.0f : 0.0f, 0.25f, 0.08f);
        if (sLinkSit > 0.0f) {
            CompRig_SolveDirs(&player->skelAnime, &sLinkRig, sSitDirs, sSitUse, sLinkSit, &sLinkOut);
            f32 k = LINK_IS_ADULT ? 1.0f : 0.64f;
            f32 leg = CompRig_LegLength(&player->skelAnime, &sLinkRig);
            f32 hipSit = leg * Tune("link_sit_hip", 0.14f);
            f32 hipNow = (f32)player->skelAnime.jointTable[0].y * k;
            sLinkOut.rootDY = (hipSit - hipNow) / k * sLinkSit;
        }
        for (int i = 0; i < COMP_COUNT; i++) {
            Comp* c = sComp[i];
            if ((c != nullptr) && (c->holdSide != 0) && (c->state == ST_FOLLOW) && !c->hidden) {
                Vec3f local = WorldToModel(&player->actor, HoldMeetPoint(c, player));
                // (she's at his right: his right hand)
                CompRig_Reach(&player->skelAnime, &sLinkRig, c->holdSide == -1, local.x, local.y, local.z, 1.0f,
                              &sLinkOut);
            }
        }
    }
    if ((limbIndex > 0) && (limbIndex < 32) && (sLinkOut.mask & (1u << limbIndex))) {
        rot->x = sLinkOut.rot[limbIndex][0];
        rot->y = sLinkOut.rot[limbIndex][1];
        rot->z = sLinkOut.rot[limbIndex][2];
    }
    if ((limbIndex == 1) && (sLinkOut.mask != 0)) {
        pos->y += sLinkOut.rootDY;
    }
}

static void UpdateRig(PlayState* play, Comp* c, Player* player) {
    c->rigOut.mask = 0;
    c->rigOut.rootDY = 0.0f;
    // (sitting on the ground is done on her bones even with Mixamo motion off)
    bool sits = SitsNow(c);
    Math_ApproachF(&c->sitAmount, sits ? 1.0f : 0.0f, 0.25f, 0.08f);
    if ((!MixamoOn() && (c->sitAmount <= 0.0f)) || (c->rigState == 2)) {
        return;
    }
    // With a custom model: Link's skeleton under her model, Link's own walk, the rig only for hugs and hands
    SkelAnime* sk = &c->skel;
    if (c->model != nullptr) {
        ModelSkel(c);
        sk = &c->modelSkel;
        c->rig = sLinkRig;
        c->rigState = 1;
        c->skirtChecked = 1;
        c->skirtMask = 0;
        if (c->legLen <= 0.0f) {
            c->legLen = CompRig_LegLength(sk, &c->rig);
        }
    }
    if (c->rigState == 0) {
        // Her bones: from the list, or from her shape
        c->rig = sRigMap[c->persona];
        if (c->rig.lThigh == 0 && c->rig.lUpper == 0) {
            CompRig_AutoMap(&c->skel, &c->rig);
        }
        c->rigState = 1;
    }
    // (live changes: "companion.<persona>.rig")
    std::string k = std::string("companion.") + sPersonaKey[c->persona] + ".rig";
    uint8_t* m = &c->rig.lThigh;
    const uint8_t* def = &sRigMap[c->persona].lThigh;
    CompRigMap found = c->rig;
    for (int i = 0; (i < 12) && (c->model == nullptr); i++) {
        m[i] = (uint8_t)CompTuneAt(k.c_str(), i, (def[i] != 0) ? def[i] : (&found.lThigh)[i]);
    }
    if (!c->skirtChecked) {
        c->skirtChecked = 1;
        c->skirtMask = 0;
        for (int i = 0; i < 2; i++) {
            int limb = (int)CompTuneAt((std::string("companion.") + sPersonaKey[c->persona] + ".skirt").c_str(), i,
                                       sSkirt[c->persona][i]);
            if ((limb <= 0) || (limb >= 32)) {
                continue;
            }
            // (only if what hangs below it is skirt too: widening it mustn't widen her body)
            uint32_t below = CompRig_Descendants(&c->skel, limb);
            uint32_t allowed = 0;
            for (int j = 0; j < 2; j++) {
                int other = sSkirt[c->persona][j];
                allowed |= (other > 0 && other < 32) ? (1u << other) : 0u;
            }
            if (c->persona == P_MALON_Y) {
                allowed |= 1u << 18; // (young Malon's lower dress hangs from the middle)
            }
            if ((below & ~allowed) == 0) {
                c->skirtMask |= 1u << limb;
            }
        }
    }
    if (c->legLen <= 0.0f) {
        c->legLen = CompRig_LegLength(sk, &c->rig);
        if (c->legLen <= 0.0f) {
            c->legLen = 2200.0f; // (no legs found: about a child's)
        }
    }

    CompClipMix mix = {};
    f32 rate = Tune("mixamo_rate", 1.5f); // (clips are 30 frames a second, the game 20)

    // A bow (Saria's slingshot, adult Ruto's and Zelda's arrows): drawn, held, and the recoil after the shot
    if ((c->bowClip >= 0) && MixamoOn()) {
        const CompClip& bc = sClips[c->bowClip];
        c->bowFrame += rate * Tune("bow_rate", 1.2f);
        if (c->bowFrame >= (f32)(bc.frames - 1)) {
            if (c->bowClip == CLIP_BOW_RECOIL) {
                c->bowClip = (c->state == ST_ATTACK) && (c->shotsLeft > 0) ? CLIP_BOW_DRAW : -1;
                c->bowFrame = 0.0f;
            } else {
                c->bowFrame = (f32)(bc.frames - 1); // (held drawn)
            }
        }
        if (c->state != ST_ATTACK && c->bowClip == CLIP_BOW_DRAW) {
            c->bowClip = -1;
        }
    }
    if ((c->bowClip >= 0) && MixamoOn()) {
        mix.clip[0] = c->bowClip;
        mix.frame[0] = c->bowFrame;
        mix.weight[0] = 1.0f;
        mix.count = 1;
        Math_ApproachF(&c->mixAmount, 1.0f, 0.5f, 0.25f);
    } else {
        // Walking and running (and side steps hand in hand)
        f32 speed = ((c->holdSide != 0) && (c->holdWalk >= 0.0f)) ? c->holdWalk : fabsf(c->actor.speedXZ);
        bool moving = MixamoOn() && (c->model == nullptr) && !c->swimming && (c->state != ST_KO) && (c->state != ST_HUG) &&
                      (speed > 0.8f) && (c->actor.bgCheckFlags & BGCHECKFLAG_GROUND);
        Math_ApproachF(&c->mixAmount, moving ? 1.0f : 0.0f, 0.35f, 0.12f);
        if (c->mixAmount <= 0.0f) {
            c->mixPhase = 0.0f;
        }
        f32 legWorld = c->legLen * c->actor.scale.y;
        f32 run = CLAMP((speed - Tune("run_from", 3.5f)) / Tune("run_blend", 1.5f), 0.0f, 1.0f);
        // Side steps: moving across where she faces
        s16 moveYaw = (c->holdSide != 0) ? player->actor.world.rot.y : c->actor.world.rot.y;
        f32 across = Math_SinS((s16)(moveYaw - c->actor.shape.rot.y));
        f32 ahead = Math_CosS((s16)(moveYaw - c->actor.shape.rot.y));
        f32 side = CLAMP((fabsf(across) - 0.55f) / 0.3f, 0.0f, 1.0f) * (1.0f - run);
        int sideClip = (across > 0.0f) ? CLIP_STRAFE_LEFT : CLIP_STRAFE_RIGHT;
        f32 wWalk = (1.0f - run) * (1.0f - side), wRun = run, wSide = side;
        // One shared step: how much of a stride she covers this frame
        f32 legsPerSecond = sClips[CLIP_WALK].legsPerSecond * wWalk + sClips[CLIP_RUN].legsPerSecond * wRun +
                            sClips[sideClip].legsPerSecond * wSide;
        f32 cycleFrames = (f32)sClips[CLIP_WALK].frames * wWalk + (f32)sClips[CLIP_RUN].frames * wRun +
                          (f32)sClips[sideClip].frames * wSide;
        if ((legsPerSecond > 0.01f) && (legWorld > 0.01f) && (cycleFrames > 0.0f)) {
            // clip frames this game frame = (legs moved) / (legs per clip frame)
            f32 frames = (speed / legWorld) / (legsPerSecond / 30.0f) * Tune("stride_scale", 1.0f);
            c->mixPhase += CLAMP(frames, 0.2f, 4.0f) / cycleFrames;
            c->mixPhase -= floorf(c->mixPhase);
        }
        if (ahead < -0.3f && side < 0.5f) {
            // (backing up: the walk played backwards)
            c->mixPhase -= 2.0f * CLAMP((speed / legWorld) / (legsPerSecond / 30.0f), 0.2f, 4.0f) / cycleFrames;
            c->mixPhase -= floorf(c->mixPhase);
        }
        int n = 0;
        if (wWalk > 0.001f) {
            mix.clip[n] = CLIP_WALK, mix.frame[n] = c->mixPhase * (f32)sClips[CLIP_WALK].frames,
            mix.weight[n++] = wWalk;
        }
        if (wRun > 0.001f) {
            mix.clip[n] = CLIP_RUN, mix.frame[n] = c->mixPhase * (f32)sClips[CLIP_RUN].frames, mix.weight[n++] = wRun;
        }
        if (wSide > 0.001f) {
            mix.clip[n] = sideClip, mix.frame[n] = c->mixPhase * (f32)sClips[sideClip].frames, mix.weight[n++] = wSide;
        }
        mix.count = n;
    }
    std::string pk = std::string(sPersonaKey[c->persona]);
    if (c->mixAmount > 0.0f) {
        // (Malon's steps a little shorter: her skirt is long)
        f32 own = Tune((pk + ".mixamo_mix").c_str(), (c->who == COMP_MALON) ? 0.8f : 1.0f);
        CompRig_Solve(sk, &c->rig, &mix, c->mixAmount * Tune("mixamo_mix", 1.0f) * own, &c->rigOut);
        c->rigOut.rootDY *= Tune("mixamo_bob", 1.0f);
    }
    // Her skirt widens as her legs spread (how far apart the thighs point, front to back), springing a little
    {
        f32 spread = 0.0f;
        if ((c->mixAmount > 0.0f) && (mix.count > 0)) {
            float l[3], r[3];
            CompRig_ClipBone(&mix, 0, l);
            CompRig_ClipBone(&mix, 2, r);
            spread = fabsf(l[2] - r[2]) * c->mixAmount;
        }
        f32 target = 1.0f + Tune((pk + ".skirt_flare").c_str(), Tune("skirt_flare", 0.35f)) * spread;
        f32 k = Tune("skirt_spring", 0.25f), damp = Tune("skirt_damping", 0.6f);
        if (c->skirtScale <= 0.0f) {
            c->skirtScale = 1.0f;
        }
        c->skirtVel = c->skirtVel * damp + (target - c->skirtScale) * k;
        c->skirtScale = CLAMP(c->skirtScale + c->skirtVel, 0.9f, 1.6f);
    }
    // A hug: both arms around him
    Math_ApproachF(&c->hugAmount, (c->state == ST_HUG) ? 1.0f : 0.0f, 0.3f, 0.15f);
    if (c->hugAmount > 0.0f) {
        CompRig_SolveDirs(sk, &c->rig, sHugDirs, sHugUse, c->hugAmount, &c->rigOut);
    }
    // Sitting on the ground: knees up, hips down near the ground, her hands on her knees (unless her arms are busy
    // with an ocarina or a song)
    if (c->sitAmount > 0.0f) {
        const PersonaDef& d = Def(c);
        bool armsFree = (c->model != nullptr) || ((c->anim == d.idle) && (c->persona != P_RUTO_Y)) ||
                        ((c->persona == P_SARIA) && (c->anim == d.sit));
        bool use[8] = { true, true, true, true, armsFree, armsFree, armsFree, armsFree };
        CompRig_SolveDirs(sk, &c->rig, sSitDirs, use, c->sitAmount, &c->rigOut);
        if (c->rigOut.mask != 0) {
            // Her hips (the root) brought down to just above the ground, wherever her animation holds them
            f32 hipNow = (f32)sk->jointTable[0].y + c->actor.shape.yOffset;
            f32 hipSit = c->legLen * Tune((pk + ".sit_hip").c_str(), Tune("sit_hip", 0.14f));
            c->rigOut.rootDY += (hipSit - hipNow) * c->sitAmount;
        }
    }
    // Hand in hand: her arm on his side reaches for his hand
    if ((c->holdSide != 0) && (c->state == ST_FOLLOW)) {
        Vec3f m = HoldMeetPoint(c, player);
        Vec3f local = WorldToModel(&c->actor, m);
        CompRig_Reach(sk, &c->rig, c->holdSide == 1, local.x, local.y, local.z, 1.0f, &c->rigOut);
    }
}

static void Comp_Update(Actor* thisx, PlayState* play) {
    Comp* c = (Comp*)thisx;
    Player* player = GET_PLAYER(play);
    const PersonaDef& d = Def(c);

    // Away (turned off, or Link changed age): gone, and the manager brings the right one
    if (!WhoOn(c->who) || (c->adult != (LINK_IS_ADULT ? 1 : 0))) {
        Actor_Kill(thisx);
        return;
    }
    // In cutscenes she steps out of the way, unseen, and comes back at his side
    if (LinkBusy(play, player)) {
        c->hidden = 1;
        c->actor.speedXZ = 0.0f;
        return;
    }
    if (c->hidden) {
        c->hidden = 0;
        // (back at his side only if the cutscene left her far from him)
        if (Actor_WorldDistXZToActor(thisx, &player->actor) > Leash(c)) {
            WarpToLink(play, c, player);
        }
    }

    if (c->cdRanged > 0) {
        c->cdRanged--;
    }
    if (c->cdClose > 0) {
        c->cdClose--;
    }
    if (c->cdMelee > 0) {
        c->cdMelee--;
    }
    if (c->cdSupport > 0) {
        c->cdSupport--;
    }
    if (c->exploreCd > 0) {
        c->exploreCd--;
    }
    SkelAnime_Update(&c->skel);
    if (c->model != nullptr) {
        // Her model moves with Link's own animations: standing, walking, running
        f32 speed = ((c->holdSide != 0) && (c->holdWalk >= 0.0f)) ? c->holdWalk : fabsf(c->actor.speedXZ);
        const char* want = LINK_WAIT;
        f32 rate = 1.0f;
        if (!c->swimming && (c->state != ST_KO) && (c->state != ST_HUG) && (speed > 0.8f)) {
            want = (speed > Tune("run_above", 4.2f)) ? LINK_RUN : LINK_WALK;
            rate = CLAMP(speed * ((want == LINK_RUN) ? Tune("model_run_rate", 0.18f) : Tune("model_walk_rate", 0.4f)),
                         0.5f, 2.0f);
        }
        if (want != c->bodyAnim) {
            c->bodyAnim = want;
            LinkAnimation_Change(play, &c->modelBody, (LinkAnimationHeader*)want, 1.0f, 0.0f,
                                 (f32)Animation_GetLastFrame((void*)want), ANIMMODE_LOOP, -6.0f);
        }
        c->modelBody.playSpeed = rate;
        LinkAnimation_Update(play, &c->modelBody);
    }
    UpdateFace(c);
    UpdateRig(play, c, player);

    // Hit by an enemy
    if (c->body.base.acFlags & AC_HIT) {
        c->body.base.acFlags &= ~AC_HIT;
        if (c->state == ST_HUG) {
            EndHug(play, c);
        }
        if ((c->state != ST_KO) && (c->state != ST_HURT)) {
            c->hp--;
            Voice(thisx, d.voiceHurt);
            Actor_SetColorFilter(thisx, 0x4000, 200, 0, 8);
            c->actor.speedXZ = -4.0f;
            if (c->hp <= 0) {
                c->state = ST_KO;
                c->timer = (s16)(Tune("ko_seconds", 10.0f) * COMP_FPS);
                SayKind(c->who, L_KO);
            } else {
                c->state = ST_HURT;
                c->timer = 15;
                if (Rand_ZeroOne() < 0.5f) {
                    SayKind(c->who, L_HURT);
                }
            }
        }
    }

    switch (c->state) {
        case ST_KO:
            // Down for a while, then back up
            Math_ApproachF(&c->actor.speedXZ, 0.0f, 0.5f, 1.0f);
            Math_SmoothStepToS(&c->actor.shape.rot.z, 0x3800, 4, 0x800, 0x100);
            SetAnim(c, d.sit != nullptr ? d.sit : d.idle, 0.0f, ANIMMODE_LOOP, -4.0f);
            c->eyeOverride = 2;
            if (--c->timer <= 0) {
                c->state = ST_FOLLOW;
                c->hp = (s16)Tune("hp", 6.0f);
                c->eyeOverride = -1;
                SayKind(c->who, L_REVIVE);
            }
            break;
        case ST_HURT:
            Math_ApproachF(&c->actor.speedXZ, 0.0f, 0.5f, 1.0f);
            if (--c->timer <= 0) {
                c->state = ST_FOLLOW;
            }
            break;
        case ST_ATTACK:
            UpdateAttack(play, c);
            break;
        case ST_SUPPORT:
            if (c->timer == 20) {
                DoSupport(play, c);
            }
            if (--c->timer <= 0) {
                c->state = ST_FOLLOW;
            }
            break;
        case ST_EMOTE:
            if ((--c->timer <= 0) || (Actor_WorldDistXZToActor(thisx, &player->actor) > Leash(c))) {
                c->state = ST_FOLLOW;
                c->eyeOverride = -1;
            }
            break;
        case ST_TALK:
            c->actor.speedXZ = 0.0f;
            Math_SmoothStepToS(&c->actor.shape.rot.y, c->actor.yawTowardsPlayer, 4, 0x800, 0x100);
            // "Talk" or "Hug"
            if ((c->talkStage == 0) && (Message_GetState(&play->msgCtx) == TEXT_STATE_CHOICE) &&
                Message_ShouldAdvance(play)) {
                if (play->msgCtx.choiceIndex == 0) {
                    c->talkStage = 1;
                    Message_ContinueTextbox(play, COMP_TEXT_ID);
                } else if (play->msgCtx.choiceIndex == 1) {
                    Message_CloseTextbox(play);
                    StartHug(play, c, player);
                    break;
                } else {
                    Message_CloseTextbox(play);
                    ToggleHold(play, c, player);
                }
            }
            if ((--c->timer <= 0) && (play->msgCtx.msgMode == MSGMODE_NONE)) {
                c->state = ST_FOLLOW;
                Emote(c, d.happy, 30, -1);
            }
            break;
        case ST_HUG:
            UpdateHug(play, c, player);
            break;
        case ST_REST:
            UpdateRest(play, c, player);
            break;
        case ST_EXPLORE:
            UpdateExplore(play, c, player);
            break;
        default:
            if (!ThinkFight(play, c, player)) {
                Follow(play, c, player);
            }
            break;
    }
    if (c->state != ST_KO) {
        // (listening to a song, a gentle sway)
        s16 sway = 0;
        if ((c->state == ST_REST) && c->seated && sSong.music && (sSong.performer != c->who)) {
            sway = (s16)(Math_SinS((s16)(sSong.frames * 0x280 + c->who * 0x2800)) * Tune("song_sway", 900.0f));
        }
        Math_SmoothStepToS(&c->actor.shape.rot.z, sway, 4, 0x800, 0x40);
    }

    // While there's a fight she can't be Z-targeted (his lock-on goes to the enemies)
    bool fight = false;
    for (int i = 0; i < COMP_COUNT; i++) {
        if ((sComp[i] != nullptr) && (sComp[i]->target != nullptr)) {
            fight = true;
        }
    }
    if (fight || (player->stateFlags1 & PLAYER_STATE1_HOSTILE_LOCK_ON)) {
        thisx->flags &= ~ACTOR_FLAG_ATTENTION_ENABLED;
    } else {
        thisx->flags |= ACTOR_FLAG_ATTENTION_ENABLED;
    }

    // Talk: Z-target her and press A
    if ((c->state != ST_KO) && (c->state != ST_ATTACK) && (c->state != ST_TALK) && (c->state != ST_HUG) &&
        (sHugWho < 0)) {
        if (Actor_ProcessTalkRequest(thisx, play)) {
            c->state = ST_TALK;
            c->talkStage = 0;
            c->timer = 10;
            c->actor.speedXZ = 0.0f;
        } else if (player->focusActor == thisx) {
            Actor_OfferTalk(thisx, play, 120.0f);
        }
    }

    // Link's walk: his hidden skeleton walks or runs at her speed
    if (c->linkWalk && !MixamoOn()) {
        // (hand in hand she walks at his pace)
        f32 moveSpeed = ((c->holdSide != 0) && (c->holdWalk >= 0.0f)) ? c->holdWalk : fabsf(c->actor.speedXZ);
        bool walking = CVarGetInteger(COMP_CVAR("LinkWalk"), 1) && !c->swimming && (c->state != ST_KO) &&
                       (c->state != ST_HUG) && (moveSpeed > 0.8f);
        Math_ApproachF(&c->walkAmount, walking ? 1.0f : 0.0f, 0.35f, 0.12f);
        if (c->walkAmount > 0.0f) {
            const char* want = (moveSpeed > Tune("run_above", 4.2f)) ? LINK_RUN : LINK_WALK;
            if (want != c->puppetAnim) {
                c->puppetAnim = want;
                LinkAnimation_Change(play, &c->puppet, (LinkAnimationHeader*)want, 1.0f, 0.0f,
                                     (f32)Animation_GetLastFrame((void*)want), ANIMMODE_LOOP, -6.0f);
            }
            c->puppet.playSpeed =
                CLAMP(moveSpeed * ((want == LINK_RUN) ? Tune("run_anim_rate", 0.22f) : Tune("walk_anim_rate", 0.45f)),
                      0.5f, 2.0f);
            LinkAnimation_Update(play, &c->puppet);
        }
        // (the standing pose, read once its frame is in)
        if ((c->restFrames < 8) || (play->gameplayFrames % 30 == (u32)c->who)) {
            c->restFrames++;
            LinkAnimation_Update(play, &c->rest);
        }
    }

    if (c->swimming) {
        // Ruto floats at the surface
        thisx->gravity = 0.0f;
        thisx->velocity.y = 0.0f;
        Math_ApproachF(&thisx->world.pos.y, thisx->world.pos.y + thisx->yDistToWater - 25.0f, 0.3f, 4.0f);
        thisx->world.rot.x = 0;
        Actor_MoveXZGravity(thisx);
    } else {
        thisx->gravity = -1.0f;
        Actor_MoveXZGravity(thisx);
    }
    Actor_UpdateBgCheckInfo(play, thisx, 26.0f, 14.0f, 40.0f, 0x1D); // (walls, floor, water)
    Actor_SetFocus(thisx, d.height * d.scale * 0.85f);
    // Feet on the ground: young Ruto's standing animation holds her up off it (companion.<persona>.y_offset, in her
    // model's units)
    thisx->shape.yOffset =
        Tune((std::string(sPersonaKey[c->persona]) + ".y_offset").c_str(), (c->persona == P_RUTO_Y) ? -800.0f : 0.0f);

    Collider_UpdateCylinder(thisx, &c->body);
    if (c->state != ST_KO) {
        CollisionCheck_SetAC(play, &play->colChkCtx, &c->body.base);
        CollisionCheck_SetOC(play, &play->colChkCtx, &c->body.base);
    }
    if (c->hitActive > 0) {
        c->hitActive--;
        Collider_UpdateCylinder(thisx, &c->hit);
        c->hit.dim.pos.x = (s16)(c->hit.dim.pos.x + Math_SinS(thisx->shape.rot.y) * 30.0f);
        c->hit.dim.pos.z = (s16)(c->hit.dim.pos.z + Math_CosS(thisx->shape.rot.y) * 30.0f);
        CollisionCheck_SetAT(play, &play->colChkCtx, &c->hit.base);
    }
}

static s32 Comp_OverrideLimbDraw(PlayState* play, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot, void* thisx,
                                 Gfx** gfx) {
    Comp* c = (Comp*)thisx;
    const PersonaDef& d = Def(c);

    if ((limbIndex < 32) && (d.hideLimbs & (1u << limbIndex))) {
        *dList = nullptr;
    }
    // Saria plays her own ocarina
    if ((c->persona == P_SARIA) && (limbIndex == 15) && (c->anim == d.support)) {
        *dList = (Gfx*)OTR("object_sa", "gSariaRightHandAndOcarinaDL");
    }
    // Mixamo motion: her bones turned to the clip's
    if ((limbIndex < 32) && (c->rigOut.mask & (1u << limbIndex))) {
        rot->x = c->rigOut.rot[limbIndex][0];
        rot->y = c->rigOut.rot[limbIndex][1];
        rot->z = c->rigOut.rot[limbIndex][2];
    }
    if ((limbIndex == 1) && (c->rigOut.mask != 0)) {
        pos->y += c->rigOut.rootDY;
    }
    // The skirt widened from its waist (its own place and turn first, then the scale; nothing else hangs below it)
    if ((limbIndex < 32) && (c->skirtMask & (1u << limbIndex)) && (fabsf(c->skirtScale - 1.0f) > 0.005f)) {
        Matrix_TranslateRotateZYX(pos, rot);
        Matrix_Scale(c->skirtScale, c->skirtScale, c->skirtScale, MTXMODE_APPLY);
        *pos = { 0.0f, 0.0f, 0.0f };
        rot->x = rot->y = rot->z = 0;
    }
    // Link's walk: how far his joint has turned from standing, added to hers (when Mixamo motion is off)
    if (!MixamoOn() && c->linkWalk && (c->walkAmount > 0.01f) && (c->restFrames >= 3)) {
        std::string k = std::string("companion.") + sPersonaKey[c->persona];
        f32 mix = Tune("walk_mix", 1.0f) * c->walkAmount;
        if (limbIndex == 1) {
            // (his bob up and down)
            pos->y += (f32)(c->puppetJoints[0].y - c->restJoints[0].y) * mix * Tune("walk_bob", 0.6f);
        }
        for (int i = 0; i < 10; i++) {
            int limb = (int)CompTuneAt((k + ".walk_map").c_str(), i, sWalkMap[c->persona][i]);
            if ((limb == 0) || (limb != limbIndex)) {
                continue;
            }
            f32 sign = CompTuneAt((k + ".walk_sign").c_str(), (i < 6) ? 0 : 1, 1.0f);
            const Vec3s& a = c->puppetJoints[sLinkLimbs[i]];
            const Vec3s& b = c->restJoints[sLinkLimbs[i]];
            rot->x = (s16)(rot->x + (s16)((f32)(s16)(a.x - b.x) * mix * sign));
            rot->y = (s16)(rot->y + (s16)((f32)(s16)(a.y - b.y) * mix * sign));
            rot->z = (s16)(rot->z + (s16)((f32)(s16)(a.z - b.z) * mix * sign));
        }
    }
    // Adult Zelda's hair hangs from her head in seven pieces (as in the castle escape, at rest)
    if ((d.hairLimb != 0) && (limbIndex == d.hairLimb)) {
        Mtx* mtx = (Mtx*)Graph_Alloc(play->state.gfxCtx, sizeof(Mtx) * 7);
        gSPSegment((*gfx)++, 0x0C, (uintptr_t)mtx);
        Matrix_Push();
        Matrix_Translate(pos->x, pos->y, pos->z, MTXMODE_APPLY);
        Matrix_RotateZYX(rot->x, rot->y, rot->z, MTXMODE_APPLY);
        Matrix_Push();
        Matrix_Translate(174.0f, -317.0f, 0.0f, MTXMODE_APPLY);
        Matrix_ToMtx(&mtx[0], (char*)__FILE__, __LINE__);
        Matrix_Translate(-410.0f, -184.0f, 0.0f, MTXMODE_APPLY);
        Matrix_ToMtx(&mtx[1], (char*)__FILE__, __LINE__);
        Matrix_Translate(-1019.0f, -26.0f, 0.0f, MTXMODE_APPLY);
        Matrix_ToMtx(&mtx[2], (char*)__FILE__, __LINE__);
        Matrix_Pop();
        Matrix_Push();
        Matrix_Translate(40.0f, 264.0f, 386.0f, MTXMODE_APPLY);
        Matrix_ToMtx(&mtx[3], (char*)__FILE__, __LINE__);
        Matrix_Translate(-446.0f, -52.0f, 84.0f, MTXMODE_APPLY);
        Matrix_ToMtx(&mtx[4], (char*)__FILE__, __LINE__);
        Matrix_Pop();
        Matrix_Push();
        Matrix_Translate(40.0f, 264.0f, -386.0f, MTXMODE_APPLY);
        Matrix_ToMtx(&mtx[5], (char*)__FILE__, __LINE__);
        Matrix_Translate(-446.0f, -52.0f, -84.0f, MTXMODE_APPLY);
        Matrix_ToMtx(&mtx[6], (char*)__FILE__, __LINE__);
        Matrix_Pop();
        Matrix_Pop();
    }
    return false;
}

static s32 Comp_ModelOverride(PlayState* play, s32 limbIndex, Gfx** dList, Vec3f* pos, Vec3s* rot, void* thisx) {
    Comp* c = (Comp*)thisx;
    if ((limbIndex < 32) && (c->rigOut.mask & (1u << limbIndex))) {
        rot->x = c->rigOut.rot[limbIndex][0];
        rot->y = c->rigOut.rot[limbIndex][1];
        rot->z = c->rigOut.rot[limbIndex][2];
    }
    if ((limbIndex == 1) && (c->rigOut.mask != 0)) {
        pos->y += c->rigOut.rootDY;
    }
    return false;
}

static void Comp_Draw(Actor* thisx, PlayState* play) {
    Comp* c = (Comp*)thisx;
    const PersonaDef& d = Def(c);

    if (c->hidden) {
        return;
    }
    s16 eye = (c->eyeOverride >= 0) ? c->eyeOverride : c->eye;
    if (c->model != nullptr) {
        // Her model: Link's eye and mouth layout (open, half, closed; closed, open, half-open mouths)
        static const int sMouth[3] = { 0, 2, 1 };
        OPEN_DISPS(play->state.gfxCtx);
        Gfx_SetupDL_25Opa(play->state.gfxCtx);
        gSPSegment(POLY_OPA_DISP++, 0x08, (uintptr_t)c->model->eyes[CLAMP(eye, 0, 2)].c_str());
        gSPSegment(POLY_OPA_DISP++, 0x09, (uintptr_t)c->model->mouths[sMouth[CLAMP(c->mouth, 0, 2)]].c_str());
        gDPSetEnvColor(POLY_OPA_DISP++, (u8)CompTuneAt("companion.model_env", 0, 255.0f),
                       (u8)CompTuneAt("companion.model_env", 1, 255.0f),
                       (u8)CompTuneAt("companion.model_env", 2, 255.0f), 255);
        CLOSE_DISPS(play->state.gfxCtx);
        ModelSkel(c);
        SkelAnime_DrawFlexOpa(play, c->model->limbPtrs, c->modelBody.jointTable, c->model->dlCount, Comp_ModelOverride,
                              nullptr, thisx);
        return;
    }
    const char* eyeTex = d.eyes[CLAMP(eye, 0, 3)];
    const char* mouthTex = d.mouths[CLAMP(c->mouth, 0, 2)];

    OPEN_DISPS(play->state.gfxCtx);
    if (d.eyeSegA != 0) {
        gSPSegment(POLY_OPA_DISP++, d.eyeSegA, (uintptr_t)eyeTex);
    }
    if (d.eyeSegB != 0) {
        gSPSegment(POLY_OPA_DISP++, d.eyeSegB, (uintptr_t)eyeTex);
    }
    if ((d.mouthSeg != 0) && (mouthTex != nullptr)) {
        gSPSegment(POLY_OPA_DISP++, d.mouthSeg, (uintptr_t)mouthTex);
    }
    // (adult Zelda's skeleton reads its render setup from segment 0x0B)
    gSPSegment(POLY_OPA_DISP++, 0x0B, (uintptr_t)&D_80116280[2]);
    CLOSE_DISPS(play->state.gfxCtx);
    func_80034BA0(play, &c->skel, Comp_OverrideLimbDraw, nullptr, thisx, 255);
}

// ---- Talking to her (A) ----

static void OnOpenText(uint16_t* textId, bool* loadFromMessageTable) {
    PlayState* play = gPlayState;
    if ((play == nullptr) || (*textId != COMP_TEXT_ID)) {
        return;
    }
    // Which of them: the one Link is talking to (the message context only learns it after this text is opened, so
    // it can still name whoever he talked to before)
    Comp* c = nullptr;
    Player* player = GET_PLAYER(play);
    Actor* who[3] = { (player != nullptr) ? player->talkActor : nullptr,
                      (player != nullptr) ? player->focusActor : nullptr, play->msgCtx.talkActor };
    for (int w = 0; (w < 3) && (c == nullptr); w++) {
        for (int i = 0; i < COMP_COUNT; i++) {
            if ((sComp[i] != nullptr) && (who[w] != nullptr) && (&sComp[i]->actor == who[w])) {
                c = sComp[i];
            }
        }
    }
    if (c == nullptr) {
        return;
    }
    const PersonaDef& d = Def(c);
    std::string line;
    bool prompt = (c->talkStage == 0);
    if (prompt) {
        line = Pick(sLines[c->persona][L_PROMPT]);
        if (line.empty()) {
            line = "Yes, Link?";
        }
    } else {
        line = Pick(sLines[c->persona][L_TALK]);
        if (line.empty()) {
            line = Pick(sLines[c->persona][L_CHAT]);
        }
    }
    char head[32];
    snprintf(head, sizeof(head), "%s:%s ", d.name, "%w");
    std::string t = std::string((c->who == COMP_ZELDA)   ? "%p"
                                : (c->who == COMP_RUTO)  ? "%b"
                                : (c->who == COMP_MALON) ? "%y"
                                                         : "%g") +
                    head + line;
    if (prompt) {
        // (one short box, then the two choices)
        t += (c->holdSide != 0) ? "\x1C&%gTalk&Hug&Let go%w" : "\x1C&%gTalk&Hug&Hold hands%w";
    }
    CustomMessage msg = CustomMessage(t, TEXTBOX_TYPE_BLACK, TEXTBOX_POS_BOTTOM);
    msg.AutoFormat();
    msg.LoadIntoFont();
    *loadFromMessageTable = false;
}

// ---- Keys ----

static bool IsKeyScene(s16 scene) {
    switch (scene) {
        case SCENE_FOREST_TEMPLE:
        case SCENE_FIRE_TEMPLE:
        case SCENE_WATER_TEMPLE:
        case SCENE_SPIRIT_TEMPLE:
        case SCENE_SHADOW_TEMPLE:
        case SCENE_BOTTOM_OF_THE_WELL:
        case SCENE_GERUDO_TRAINING_GROUND:
        case SCENE_THIEVES_HIDEOUT:
        case SCENE_INSIDE_GANONS_CASTLE:
        case SCENE_TREASURE_BOX_SHOP:
            return true;
        default:
            return false;
    }
}

static bool IsDungeon(s16 scene) {
    return IsKeyScene(scene) || (scene == SCENE_DEKU_TREE) || (scene == SCENE_DODONGOS_CAVERN) ||
           (scene == SCENE_JABU_JABU) || (scene == SCENE_ICE_CAVERN) || (scene == SCENE_GANONS_TOWER);
}

static bool IsSmallKey(RandomizerGet rg) {
    return ((rg >= RG_FOREST_TEMPLE_SMALL_KEY) && (rg <= RG_TREASURE_GAME_SMALL_KEY)) ||
           ((rg >= RG_FOREST_TEMPLE_KEY_RING) && (rg <= RG_TREASURE_GAME_KEY_RING));
}

// A key tip from one of them: one in this very room, where one waits (randomizer), how many he holds, or none left
static void KeyTip(PlayState* play, int who) {
    Comp* c = sComp[who];
    if (c == nullptr) {
        return;
    }
    s16 scene = play->sceneNum;
    bool inRoom = false;
    std::string where;
    int unfound = 0;

    if (IS_RANDO) {
        auto ctx = Rando::Context::GetInstance();
        if (ctx == nullptr) {
            return;
        }
        for (auto& loc : Rando::StaticData::GetLocationTable()) {
            if ((loc.GetScene() != scene) || (loc.GetRandomizerCheck() == RC_UNKNOWN_CHECK)) {
                continue;
            }
            Rando::ItemLocation* il = ctx->GetItemLocation(loc.GetRandomizerCheck());
            if ((il == nullptr) || !IsSmallKey(il->GetPlacedRandomizerGet()) || il->HasObtained() ||
                (il->GetCheckStatus() >= RCSHOW_COLLECTED)) {
                continue;
            }
            unfound++;
            // In this room, if the check's actor is here
            for (int cat = 0; cat < ACTORCAT_MAX; cat++) {
                for (Actor* a = play->actorCtx.actorLists[cat].head; a != nullptr; a = a->next) {
                    if ((a->id == loc.GetActorID()) && (a->params == loc.GetActorParams())) {
                        inRoom = true;
                    }
                }
            }
            if (where.empty() || (Rand_ZeroOne() < 0.35f)) {
                where = loc.GetShortName();
            }
        }
    } else {
        // Unopened chests holding a small key, and keys lying on the floor, in this room
        for (Actor* a = play->actorCtx.actorLists[ACTORCAT_CHEST].head; a != nullptr; a = a->next) {
            if ((a->id == ACTOR_EN_BOX) && (((a->params >> 5) & 0x7F) == GI_KEY_SMALL) &&
                !Flags_GetTreasure(play, a->params & 0x1F)) {
                inRoom = true;
                unfound++;
            }
        }
        for (Actor* a = play->actorCtx.actorLists[ACTORCAT_MISC].head; a != nullptr; a = a->next) {
            if ((a->id == ACTOR_EN_ITEM00) && ((a->params & 0xFF) == ITEM00_SMALL_KEY)) {
                inRoom = true;
                unfound++;
            }
        }
    }
    s32 held = (gSaveContext.mapIndex < ARRAY_COUNT(gSaveContext.inventory.dungeonKeys))
                   ? MAX(0, gSaveContext.inventory.dungeonKeys[gSaveContext.mapIndex])
                   : 0;

    if (inRoom) {
        SayKind(who, L_KEY_HERE);
        Emote(c, Def(c).wave, 40, 3);
    } else if (!where.empty()) {
        SayKind(who, L_KEY_NEAR, where.c_str());
    } else if ((held > 0) && (Rand_ZeroOne() < 0.6f)) {
        SayKind(who, L_KEY_HELD, nullptr, held);
    } else if (IS_RANDO && (unfound == 0)) {
        SayKind(who, L_KEY_NONE);
    } else if (held > 0) {
        SayKind(who, L_KEY_HELD, nullptr, held);
    }
}

// ---- The group: who's here, and the talk between them ----

static void Spawn(PlayState* play, int who) {
    Player* player = GET_PLAYER(play);
    if (sCompActorId < 0) {
        ActorDBInit i;
        i.name = "Comp_Companion";
        i.desc = "A companion travelling with Link (Saria, Ruto, Malon or Zelda)";
        i.category = ACTORCAT_NPC;
        i.flags = ACTOR_FLAG_ATTENTION_ENABLED | ACTOR_FLAG_FRIENDLY | ACTOR_FLAG_UPDATE_CULLING_DISABLED |
                  ACTOR_FLAG_DRAW_CULLING_DISABLED;
        i.objectId = OBJECT_GAMEPLAY_KEEP;
        i.instanceSize = sizeof(Comp);
        i.init = Comp_Init;
        i.destroy = Comp_Destroy;
        i.update = Comp_Update;
        i.draw = Comp_Draw;
        sCompActorId = (s16)ActorDB::Instance->AddEntry(i).entry.id;

        ActorDBInit k;
        k.name = "Comp_Cucco";
        k.desc = "Malon's angry cucco (Companions)";
        k.category = ACTORCAT_ITEMACTION;
        k.flags = ACTOR_FLAG_UPDATE_CULLING_DISABLED | ACTOR_FLAG_DRAW_CULLING_DISABLED;
        k.objectId = OBJECT_GAMEPLAY_KEEP;
        k.instanceSize = sizeof(CompCucco);
        k.init = Cucco_Init;
        k.destroy = Cucco_Destroy;
        k.update = Cucco_Update;
        k.draw = Cucco_Draw;
        sCuccoActorId = (s16)ActorDB::Instance->AddEntry(k).entry.id;
    }
    s16 params = (s16)(who | ((LINK_IS_ADULT ? 1 : 0) << 2));
    Actor* a = Actor_Spawn(&play->actorCtx, play, sCompActorId, player->actor.world.pos.x, player->actor.world.pos.y,
                           player->actor.world.pos.z, 0, player->actor.shape.rot.y, 0, params);
    if (a != nullptr) {
        Comp* c = (Comp*)a;
        int n = 0;
        for (int i = 0; i < who; i++) {
            if (WhoOn(i)) {
                n++;
            }
        }
        c->formation = (s16)n;
        WarpToLink(play, c, player);
    }
}

static bool CanBeHere(PlayState* play) {
    if ((play == nullptr) || (gSaveContext.fileNum < 0) || (gSaveContext.fileNum > 2) ||
        (gSaveContext.gameMode != GAMEMODE_NORMAL)) {
        return false;
    }
    Player* player = GET_PLAYER(play);
    return (player != nullptr) && (player->actor.update != nullptr);
}

static void StartBanter() {
    std::vector<const Banter*> fits;
    for (auto& b : sBanter) {
        int wa = -1, wb = -1;
        for (int i = 0; i < COMP_COUNT; i++) {
            if ((sComp[i] != nullptr) && !sComp[i]->hidden && (sComp[i]->state != ST_KO)) {
                if (sComp[i]->persona == b.a) {
                    wa = i;
                }
                if (sComp[i]->persona == b.b) {
                    wb = i;
                }
            }
        }
        if ((wa >= 0) && (wb >= 0)) {
            fits.push_back(&b);
        }
    }
    if (fits.empty()) {
        SayKind(RandomPresent(), L_CHAT);
        return;
    }
    const Banter* b = fits[(size_t)(Rand_ZeroOne() * (f32)fits.size()) % fits.size()];
    s32 delay = 0;
    for (size_t i = 0; i < b->lines.size(); i++) {
        int persona = (i % 2 == 0) ? b->a : b->b;
        for (int w = 0; w < COMP_COUNT; w++) {
            if ((sComp[w] != nullptr) && (sComp[w]->persona == persona)) {
                sPending.push_back({ w, b->lines[i], delay });
                delay = (s32)(Tune("bubble_seconds", 2.5f) * COMP_FPS + (f32)b->lines[i].size() * 0.9f) + 6;
            }
        }
    }
}

static void OnFrame() {
    PlayState* play = gPlayState;

    for (int i = 0; i < COMP_COUNT; i++) {
        if (sBubble[i].frames > 0) {
            sBubble[i].frames--;
        }
        if ((sComp[i] == nullptr) || sComp[i]->hidden) {
            sBubble[i].frames = 0;
        }
    }
    if (!On() || !CanBeHere(play)) {
        sPending.clear();
        return;
    }
    Player* player = GET_PLAYER(play);
    // A hug whose girl is gone or doing something else: Link let go (never left held)
    if ((sHugWho >= 0) && ((sComp[sHugWho] == nullptr) || (sComp[sHugWho]->state != ST_HUG))) {
        sHugWho = -1;
        Player_SetCsAction(play, nullptr, PLAYER_CSACTION_7);
        HeldCameraOff(play);
    }
    if (LinkBusy(play, player)) {
        EndSong(play, false);
        return;
    }
    UpdateSong(play, player);
    // Everyone who's on, at Link's side
    for (int i = 0; i < COMP_COUNT; i++) {
        if (WhoOn(i) && (sComp[i] == nullptr)) {
            Spawn(play, i);
        }
    }

    // Lines waiting their turn
    if (!sPending.empty()) {
        Pending& p = sPending.front();
        if (--p.delay <= 0) {
            if (sComp[p.who] != nullptr) {
                Say(p.who, p.text);
            }
            sPending.pop_front();
        }
    }
    if (play->msgCtx.msgMode != MSGMODE_NONE) {
        return;
    }

    // Arriving somewhere
    if (sEnterTimer > 0 && --sEnterTimer == 0) {
        int w = RandomPresent();
        if (w >= 0) {
            auto sc = sSceneLines.find(play->sceneNum);
            std::string line;
            // A line for this place from whoever has one
            if (sc != sSceneLines.end()) {
                std::vector<int> have;
                for (int i = 0; i < COMP_COUNT; i++) {
                    if ((sComp[i] != nullptr) && sc->second.count(sComp[i]->persona)) {
                        have.push_back(i);
                    }
                }
                if (!have.empty()) {
                    w = have[(size_t)(Rand_ZeroOne() * (f32)have.size()) % have.size()];
                    line = Pick(sc->second[sComp[w]->persona]);
                }
            }
            if (line.empty() && (Rand_ZeroOne() < 0.6f)) {
                line = Pick(sLines[sComp[w]->persona][IsDungeon(play->sceneNum) ? L_DUNGEON : L_ENTER]);
            }
            Say(w, line);
        }
    }
    if (sKeyTipTimer > 0 && --sKeyTipTimer == 0) {
        int w = RandomPresent(sLastTalker);
        if (w < 0) {
            w = RandomPresent();
        }
        if (w >= 0) {
            KeyTip(play, w);
        }
    }

    // Link looking at one of them in first person (C-Up): she notices
    for (int i = 0; i < COMP_COUNT; i++) {
        Comp* c = sComp[i];
        if (c == nullptr) {
            continue;
        }
        if (c->lookedCooldown > 0) {
            c->lookedCooldown--;
        }
        Camera* cam = GET_ACTIVE_CAM(play);
        bool looking = false;
        if ((player->stateFlags1 & PLAYER_STATE1_FIRST_PERSON) && (cam != nullptr) && !c->hidden &&
            (c->state != ST_KO)) {
            Vec3f dir = { cam->at.x - cam->eye.x, cam->at.y - cam->eye.y, cam->at.z - cam->eye.z };
            Vec3f head = c->actor.focus.pos;
            Vec3f to = { head.x - cam->eye.x, head.y - cam->eye.y, head.z - cam->eye.z };
            f32 dl = sqrtf(dir.x * dir.x + dir.y * dir.y + dir.z * dir.z);
            f32 tl = sqrtf(to.x * to.x + to.y * to.y + to.z * to.z);
            if ((dl > 0.01f) && (tl > 0.01f) && (tl < Tune("look_distance", 800.0f))) {
                f32 cosA = (dir.x * to.x + dir.y * to.y + dir.z * to.z) / (dl * tl);
                // (within a few degrees, a little wider up close)
                looking = cosA > cosf(fminf(0.35f, Tune("look_angle", 7.0f) * 0.01745f * (1.0f + 120.0f / tl)));
            }
        }
        c->lookedFrames = looking ? (s16)(c->lookedFrames + 1) : 0;
        if ((c->lookedFrames == (s16)Tune("look_frames", 8.0f)) && (c->lookedCooldown == 0)) {
            c->lookedCooldown = (s16)(Tune("look_cooldown_seconds", 6.0f) * COMP_FPS);
            SayKind(i, L_LOOKED_AT);
            if ((c->state == ST_FOLLOW) && (c->holdSide == 0)) {
                Emote(c, Def(c).wave, 30, -1);
            }
            c->actor.shape.rot.y = c->actor.world.rot.y = c->actor.yawTowardsPlayer;
        }
    }

    // Link standing still
    if (player->actor.speedXZ < 0.5f) {
        sStillFrames++;
    } else {
        sStillFrames = 0;
    }
    if ((sStillFrames == (s32)(Tune("idle_seconds", 15.0f) * COMP_FPS)) && !StartSong(play, player)) {
        int w = RandomPresent();
        if (w >= 0) {
            SayKind(w, L_IDLE);
            const PersonaDef& d = Def(sComp[w]);
            Emote(sComp[w], (Rand_ZeroOne() < 0.5f && d.sit != nullptr) ? d.sit : d.worried, 120, -1);
        }
    }

    // Link badly hurt
    if (sLowHealthCooldown > 0) {
        sLowHealthCooldown--;
    }
    if ((sLowHealthCooldown == 0) && (gSaveContext.health <= gSaveContext.healthCapacity / 4) &&
        (gSaveContext.health > 0)) {
        int w = RandomPresent();
        if (w >= 0) {
            SayKind(w, L_LOW_HEALTH);
        }
        sLowHealthCooldown = 60 * COMP_FPS;
    }
    if (sCheerCooldown > 0) {
        sCheerCooldown--;
    }

    // Night falls
    if (IS_NIGHT && (sNightSaid != (s32)gSaveContext.totalDays) && !IsDungeon(play->sceneNum) &&
        (play->sceneNum == SCENE_HYRULE_FIELD || play->sceneNum == SCENE_LON_LON_RANCH ||
         play->sceneNum == SCENE_KAKARIKO_VILLAGE || play->sceneNum == SCENE_LAKE_HYLIA ||
         play->sceneNum == SCENE_GERUDO_VALLEY || play->sceneNum == SCENE_KOKIRI_FOREST)) {
        sNightSaid = (s32)gSaveContext.totalDays;
        int w = RandomPresent();
        if (w >= 0) {
            SayKind(w, L_NIGHT);
        }
    }

    // Now and then, something to say: on her own, or to one of the others
    bool fighting = false;
    for (int i = 0; i < COMP_COUNT; i++) {
        if ((sComp[i] != nullptr) && (sComp[i]->target != nullptr)) {
            fighting = true;
        }
    }
    if (!fighting && !AnyoneTalking() && (--sChatTimer <= 0)) {
        f32 lo = Tune("chatter_min_seconds", 55.0f);
        f32 hi = Tune("chatter_max_seconds", 110.0f);
        sChatTimer = (s32)((lo + Rand_ZeroOne() * (hi - lo)) * COMP_FPS);
        int present = 0;
        for (int i = 0; i < COMP_COUNT; i++) {
            present += (sComp[i] != nullptr) ? 1 : 0;
        }
        if ((present >= 2) && (Rand_ZeroOne() < Tune("banter_chance", 0.55f))) {
            StartBanter();
        } else {
            int w = RandomPresent(sLastTalker);
            if (w < 0) {
                w = RandomPresent();
            }
            if (w >= 0) {
                if (IsKeyScene(play->sceneNum) && (Rand_ZeroOne() < 0.4f)) {
                    KeyTip(play, w);
                } else {
                    SayKind(w, L_CHAT);
                    if (Rand_ZeroOne() < 0.5f) {
                        Emote(sComp[w], Def(sComp[w]).happy, 40, -1);
                    }
                }
            }
        }
    }
}

static void OnSceneInit(int16_t sceneNum) {
    // (a new place has its own music: nothing to give back)
    sSong.performer = -1;
    sSong.music = false;
    sPoiSeenCount = 0;
    if (sLinkSits) {
        sLinkSits = false;
        if (sHugWho < 0) {
            HeldCameraOff(gPlayState);
        }
    }
    for (int i = 0; i < COMP_COUNT; i++) {
        sComp[i] = nullptr; // (the scene's actors are gone)
        sBubble[i].frames = 0;
    }
    sPending.clear();
    sEnterTimer = (s32)(Tune("enter_line_seconds", 3.0f) * COMP_FPS);
    sKeyTipTimer = IsKeyScene(sceneNum) ? (s32)(Tune("key_tip_seconds", 9.0f) * COMP_FPS) : -1;
    if (sChatTimer < 20 * COMP_FPS) {
        sChatTimer = 20 * COMP_FPS;
    }
}

static void OnEnemyDefeat(void* actor) {
    Actor* a = (Actor*)actor;
    if (!On() || (gPlayState == nullptr)) {
        return;
    }
    // Everyone cheers a boss; the odd enemy gets a word now and then
    if (a->category == ACTORCAT_BOSS) {
        int w = RandomPresent();
        if (w >= 0) {
            SayKind(w, L_BOSS);
            Emote(sComp[w], Def(sComp[w]).happy, 60, -1);
        }
        for (int i = 0; i < COMP_COUNT; i++) {
            if ((sComp[i] != nullptr) && (i != w)) {
                Emote(sComp[i], Def(sComp[i]).wave, 60, -1);
            }
        }
        return;
    }
    if ((sCheerCooldown == 0) && (Rand_ZeroOne() < Tune("cheer_chance", 0.3f)) && !AnyoneTalking()) {
        int w = RandomPresent();
        if (w >= 0) {
            SayKind(w, L_CHEER);
            sCheerCooldown = (s32)(Tune("cheer_cooldown_seconds", 20.0f) * COMP_FPS);
        }
    }
}

// ---- The bubbles ----

class CompanionsHud : public Ship::GuiWindow {
  public:
    using GuiWindow::GuiWindow;
    void InitElement() override {
    }
    void DrawElement() override {
    }
    void UpdateElement() override {
    }
    void Draw() override;
};

void CompanionsHud::Draw() {
    PlayState* play = gPlayState;
    if (!On() || (play == nullptr) || (play->pauseCtx.state != 0) || !CVarGetInteger(COMP_CVAR("Chatter"), 1)) {
        return;
    }
    ImGuiViewport* vp = ImGui::GetMainViewport();
    ImDrawList* list = ImGui::GetForegroundDrawList(vp);
    float scale = vp->Size.y / 480.0f * CVarGetFloat(COMP_CVAR("BubbleScale"), 1.0f);
    float font = ImGui::GetFontSize() * 0.8f * fmaxf(scale, 1.0f);

    for (int i = 0; i < COMP_COUNT; i++) {
        Comp* c = sComp[i];
        if ((c == nullptr) || c->hidden || (sBubble[i].frames <= 0) || sBubble[i].text.empty()) {
            continue;
        }
        const PersonaDef& d = Def(c);
        Vec3f top = c->actor.world.pos;
        top.y += d.height * c->actor.scale.y + 18.0f;
        MtxF* m = &play->viewProjectionMtxF;
        float cx = m->xx * top.x + m->xy * top.y + m->xz * top.z + m->xw;
        float cy = m->yx * top.x + m->yy * top.y + m->yz * top.z + m->yw;
        float cw = m->wx * top.x + m->wy * top.y + m->wz * top.z + m->ww;
        if (cw <= 1.0f) {
            continue; // (behind the camera)
        }
        float sx = vp->Pos.x + (cx / cw * 0.5f + 0.5f) * vp->Size.x;
        float sy = vp->Pos.y + (0.5f - cy / cw * 0.5f) * vp->Size.y;
        float wrap = 230.0f * scale;
        int alpha = (sBubble[i].frames < 10) ? sBubble[i].frames * 25 : 250;

        std::string name = d.name;
        ImVec2 nameSz = ImGui::GetFont()->CalcTextSizeA(font * 0.85f, FLT_MAX, 0.0f, name.c_str());
        ImVec2 sz = ImGui::GetFont()->CalcTextSizeA(font, FLT_MAX, wrap, sBubble[i].text.c_str());
        float w = fmaxf(sz.x, nameSz.x) + 16.0f * scale;
        float h = sz.y + nameSz.y + 12.0f * scale;
        float x = CLAMP(sx - w * 0.5f, vp->Pos.x + 4.0f, vp->Pos.x + vp->Size.x - w - 4.0f);
        float y = CLAMP(sy - h - 10.0f * scale, vp->Pos.y + 4.0f, vp->Pos.y + vp->Size.y - h - 4.0f);

        list->AddRectFilled(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(255, 252, 240, alpha * 230 / 255),
                            8.0f * scale);
        list->AddRect(ImVec2(x, y), ImVec2(x + w, y + h), IM_COL32(d.r / 2, d.g / 2, d.b / 2, alpha), 8.0f * scale, 0,
                      1.5f * scale);
        float tx = CLAMP(sx, x + 10.0f * scale, x + w - 10.0f * scale);
        list->AddTriangleFilled(ImVec2(tx - 6.0f * scale, y + h - 1.0f), ImVec2(tx + 6.0f * scale, y + h - 1.0f),
                                ImVec2(tx, y + h + 8.0f * scale), IM_COL32(255, 252, 240, alpha * 230 / 255));
        list->AddText(ImGui::GetFont(), font * 0.85f, ImVec2(x + 8.0f * scale, y + 4.0f * scale),
                      IM_COL32(d.r * 3 / 5, d.g * 3 / 5, d.b * 3 / 5, alpha), name.c_str());
        list->AddText(ImGui::GetFont(), font, ImVec2(x + 8.0f * scale, y + 6.0f * scale + nameSz.y),
                      IM_COL32(40, 30, 30, alpha), sBubble[i].text.c_str(), nullptr, wrap);
    }
}

std::shared_ptr<Ship::GuiWindow> Companions_CreateHudWindow() {
    return std::make_shared<CompanionsHud>(CVAR_WINDOW("CompanionsHud"), "Companions");
}

static void RegisterCompanions() {
    BuildLines();
    LoadExtraLines();
    sChatTimer = 30 * COMP_FPS;
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnGameFrameUpdate>(OnFrame);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnSceneInit>(OnSceneInit);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnEnemyDefeat>(OnEnemyDefeat);
    GameInteractor::Instance->RegisterGameHook<GameInteractor::OnOpenText>(OnOpenText);
    gCompanionPlayerLimbHook = LinkLimbHook;
}

static RegisterShipInitFunc initFunc(RegisterCompanions, {});
