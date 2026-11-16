// Body settings from the body.ini files (runtime/hle/body_shape.c; docs/MODDING.md, "Body shape").
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

// The settings that are numbers, 1 meaning as she is (body_shape.c BODY_KEYS: their body.ini names and ranges)
enum {
    BODY_BUST_SIZE, BODY_BUST_JIGGLE, BODY_BUTT_SIZE, BODY_BUTT_JIGGLE, BODY_THIGH_SIZE, BODY_THIGH_JIGGLE,
    BODY_ARM_SIZE, BODY_CALF_SIZE, BODY_WAIST_SIZE, BODY_BUST_WEIGHT, BODY_BUST_CONTACT, BODY_NVALUES
};
// The soft-body regions, as a costume's _YWRK.ywrk presets go: breasts, hips (the bottom), thighs
enum { BODY_REGION_BUST, BODY_REGION_BUTT, BODY_REGION_THIGHS, BODY_NREGIONS };

typedef struct {
    char code[8];              // the girl ("MOM")
    float v[BODY_NVALUES];     // 1: as she is
    char physics[8];           // bust_physics: the girl whose breast physics she takes ("" none)
    float physics_amount;      // bust_physics_amount: 0 her own .. 1 the other girl's
} BodySettings;

// Every girl a body.ini sets (the mods in name order, then the launcher's; a later setting wins); read once, on the
// first call
const BodySettings *body_settings(int *n);
// The girl's code for a code or a name ("mar", "Marie Rose", "Marie"), NULL when it's none
const char *body_girl_code(const char *s);
// A soft-body region's jiggle setting (how lively it is, 1 as she is), and its size (its soft body's lengths scale
// with it: how far its points swing and how big they are, so a bigger part moves and is met as the normal one is)
float body_jiggle(const BodySettings *b, int region);
float body_reach(const BodySettings *b, int region);

#ifdef __cplusplus
}
#endif
