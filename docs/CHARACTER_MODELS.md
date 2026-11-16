# The girls' models: rigging, placement, physics

How a girl in DEAD OR ALIVE Xtreme 3 Scarlet is put together, for putting another character in her place. This page
is the reference; [MODDING.md](MODDING.md) has the tools and recipes. Everything here was read from the game's own
files (1.0.6) with `tools/mods/` and checked in the game where it says so. Points marked *code* come from reading the
game's code (function names from the decompilation project's notes); *inferred* ones were not checked.

Dump any model's bones yourself:

```
.venv/Scripts/python -I tools/mods/doaxmod.py skeleton <model path or _TMC.tmc file> [--objects] [--filter TEXT] [--json OUT]
```

## Contents

- [Axes, units, the bind pose](#axes-units-the-bind-pose)
- [The parts of a girl](#the-parts-of-a-girl)
- [Bone names](#bone-names)
- [The body skeleton](#the-body-skeleton)
- [Proportions: every girl her own](#proportions-every-girl-her-own)
- [How the face, hair and accessories follow the body](#how-the-face-hair-and-accessories-follow-the-body)
- [Skinning](#skinning)
- [The face rig](#the-face-rig)
- [The hair rig and its physics](#the-hair-rig-and-its-physics)
- [Soft parts of the body](#soft-parts-of-the-body)
- [Animation](#animation)
- [Putting another character in a girl's place: checklist](#putting-another-character-in-a-girls-place-checklist)
- [Lessons from the face rigs](#lessons-from-the-face-rigs)

## Axes, units, the bind pose

- **Units:** metres.
- **Axes:** x is the girl's left, y is up, z is forward (she faces +z). glTF from `model-export` uses the same axes.
- **Origin:** the hips (`MOT00_Hips` is at 0, 0, 0), not the floor. The feet are below it (Kasumi's 0.83 m down).
- **Bind pose:** an A-pose. The upper arms point down and out at 45 degrees, the legs are slightly apart, and the
  face is neutral with the eyes open and the mouth closed.
- **Matrices** in the files (`HieLay` local, `GlblMtx` world, `BnOfsMtx` its inverse) are row vectors, with the
  translation in the last row: p' = p × M.

## The parts of a girl

`doaxmod.py girl-files <GIRL>` lists them. A girl is drawn from:

| Part | File | What it holds |
|---|---|---|
| Costume (the body) | `character/<GIRL>/costume/<OWNER>_<kind>_<n>_<v>/<GIRL>_<OWNER>_..._TMC.tmc` + `TMCL` | the whole body skeleton, the body's skin mesh (`WGT_body`), the costume's meshes, their strings and ties, and physics |
| Face | `character/<GIRL>/face/face_00/<GIRL>_FACE_00_TMC.tmc` | the head's skin, eyes, lashes, mouth inside; its own copies of the neck and head bones; 43 face bones |
| Hair | `character/<GIRL>/hair/hair_NN/<GIRL>_HAIR_NN_TMC.tmc` | the hair meshes; its own copy of the head bone; the hair's bone chains and their physics |
| Body skin texture | `character/<GIRL>/bodybase/<GIRL>_BODYBASE_TEXCH.texch` | the skin colour the costumes' body mesh uses |
| Animation | `character/<GIRL>/animation/<GIRL>_INGAME_MPM.mpm`, `_INGAME_SKTM.sktm`, `_FACE_MPM.mpm`, `_FACE_RIG_MPM.mpm` | her body motions and facial animation (plus a bank shared by all girls) |
| Data | `<GIRL>_DAT.bin`, `<GIRL>_MISC.misc`, `<GIRL>_YWRK_PHYD.phyd` | per-girl data; the soft body's presets (*inferred*) |

- **Every costume exists once per girl who can wear it.** `KAS_AYA_BI_02_A` is Ayane's bikini fitted to Kasumi's
  body, with Kasumi's skeleton. The body you see is the costume model's `WGT_body` mesh, so a new body shape means a
  new `WGT_body` in every costume she can wear.

## Bone names

The prefix says what a node is, and the game treats the kinds differently:

| Prefix | What | The game |
|---|---|---|
| `MOT` + number | the 21 animated body bones (`MOT00_Hips` to `MOT20_RightToe`) | driven by the body animation, by node index (the first 21 nodes, in this order); matched by name between models |
| `OPT_` | helpers: twist and joint helpers (`OPT_AS_*`), fingers (`OPT_Hand_*`), the face's bones (`OPT_Face_*`), soft-body bones (`OPT_ywrk_*`, `OPT_Breast_*`, `OPT_Hips_*`), chains (`OPT_acs_*`) | posed by their parents, by animation (the face) or by physics |
| `WGT_` | a mesh object's node (`WGT_body`, `WGT_face`, `WGT_hair`, `WGT_acs_himo1`, ...): its vertices are in this node's space | not animated |
| `XTR_` | attachment points (`XTR_acs_ankl_l`, `XTR_acs_wrist_r`, `XTR_acs_glasses`, `XTR_acs_ornament_r`) | matched by name between models, like `MOT` |

Every node name in a model is unique. A name starts at +0x50 of its `NodeObj`.

## The body skeleton

From Kasumi's costume `KAS_KAS_BI_03_A` (209 nodes; the count changes with the costume's strings and ties):

```
MOT00_Hips (0, 0, 0)
├─ MOT06_LeftUpLeg ─ MOT07_LeftLeg ─ MOT03_LeftFoot ─ MOT18_LeftToe     (and the mirror: 12, 13, 09, 20)
│    helpers: OPT_AS_L_UpLeg_A/B, OPT_AS_L_Knee, XTR_acs_ankl_l; soft: OPT_ywrk_leg_Left00-05
├─ MOT16_Waist ─ MOT02_Chest
│    ├─ MOT15_Neck ─ MOT01_Head
│    ├─ MOT17_LeftShoulder ─ MOT08_LeftArm ─ MOT04_LeftForeArm ─ MOT05_LeftHand   (and 19, 14, 10, 11)
│    │    helpers: OPT_AS_L_ArmTwist_A/B, OPT_AS_L_Elbow, OPT_AS_L_Wrist_A/B, XTR_acs_wrist_l
│    │    fingers: OPT_Hand_Left_Root ─ Index1-3, Middle1-3, Thumb1-3, Metacarpal04 ─ Ring1-3, Pinky1-3
│    └─ OPT_Breast_Left_base ─ OPT_ywrk_Left00-17            (and Right)
├─ OPT_AS_L_Hip, OPT_AS_R_Hip
├─ OPT_Hips_Left_base ─ OPT_ywrk_hip_Left00-13              (and Right)
└─ the costume's own: WGT_acs_<part> ─ OPT_acs_<part>_boneN chains (strings, ties); WGT_body, WGT_<costume> meshes
```

The face and the hair are separate models (below), so the body skeleton ends at `MOT01_Head`.

## Proportions: every girl her own

Each costume file carries its girl's skeleton. There's no single skeleton scaled per girl. The same bikini on three
girls (bind-pose positions, metres from the hips):

| Bone | Kasumi | Marie Rose | Honoka |
|---|---|---|---|
| `MOT01_Head` | (0, 0.504, −0.018) | (0, 0.464, −0.017) | (0, 0.464, −0.017) |
| `MOT15_Neck` | (0, 0.436, −0.040) | (0, 0.401, −0.037) | (0, 0.401, −0.037) |
| `MOT08_LeftArm` | (0.121, 0.384, −0.027) | (0.112, 0.353, −0.024) | same as Marie |
| `MOT05_LeftHand` | (0.459, 0.046, −0.027) | (0.413, 0.052, −0.024) | same |
| `MOT03_LeftFoot` | (0.290, −0.832, −0.022) | (0.263, −0.751, −0.020) | same |

Marie Rose and Honoka share one body size. Momiji's face model puts her head at (0, 0.520, −0.019).

- **Code:** the body animation drives the 21 `MOT` bones from the motion data, and the bone lengths come from there
  (a skeleton in the motion bank). The costume's own `MOT` bind pose is used for skinning. A per-character table
  scales `MOT` bones by name (`SlotMotion::ApplyBoneScales`), and the face and hair divide that scale back out at the
  head, so the head isn't stretched.
- **So:** a new body must fit the girl's animated skeleton. Use her costume's bind pose as the template, joint for
  joint, or the animation will bend the mesh in the wrong places.

## How the face, hair and accessories follow the body

*Code* (the attached-model class, `Class_7000872200`, shared by the hair, the face and accessories):

- **Matched once, by name.** When the model is built, each node whose name starts with `MOT` or `XTR` is matched by
  name to the body's. `OPT_` and `WGT_` nodes are never matched.
- **Each frame**, a matched node takes the body's world matrix exactly. Every other node is its parent's world
  matrix times its own local matrix from the model's `HieLay`.
- **So the head joint is shared**, and everything else in the face or hair model hangs from it at the offsets the
  model gives. The face model's own `MOT01_Head` bind position doesn't decide where the face appears; the body's head
  does. What matters is where the face's bones and vertices sit *relative to its own head bone*.
- **Practical:** build a replacement head in the target girl's head space. Take her face model's
  `MOT01_Head` / `MOT15_Neck` positions as they are, and fit the new face around her face bones (next section).

## Skinning

- **Influences:** every skinned mesh (body, face, hair) has up to 12 bones per vertex: three sets of four weights
  (half floats) and three sets of four bone numbers (bytes).
- **Palettes:** a vertex's bone numbers index its object's palette, not the node list. The palette is in the
  object's `NodeObj` and has at most 255 entries; the face's has 45, a costume body's 149.
- **Spaces:** an object's vertices are in its `WGT_` node's space. `model-export` applies the node's world matrix
  and `model-import` undoes it.
- **Blending:** linear (matrix) blending. A vertex weighted partly to a bone that turns moves along a straight chord
  between the turned and unturned positions, so it sinks toward the pivot. Keep that in mind wherever skin sits close
  to something it mustn't pass through: lids over eyeballs, lips over teeth.

## The face rig

43 bones under `OPT_Face_Root`, which sits on `MOT01_Head` (Momiji's positions shown; every girl has her own):

| Group | Bones |
|---|---|
| Jaw and mouth | `OPT_Face_jaw` (pivot behind the mouth, at the jaw hinge's height); on it `c_lip_lower`, `l/r_lip_lower`, `c_tongue_a` ─ `c_tongue_b`. On the head: `c_lip_upper`, `l/r_lip_upper`, `l/r_lip_outside` (the corners) |
| Eyes | `l/r_eye` (the eyeball's centre, its pivot), `l/r_eyelid` (at the same point: the upper lid turns about it), `l/r_eye_lower_a/b` (lower lid) |
| Brows and forehead | `c_eyebrow`, `l/r_eyebrow_a/b/c`, `l/r_forehead` |
| Cheeks and nose | `l/r_cheek_a/b/c`, `c_nose`, `l/r_nose_a/b/c/d` |

- **Animation replaces the face bones' transforms** (*code*). The facial animation sets each face bone's whole
  local matrix, by node index: track = node index − the first face bone's index. It doesn't add to the bind pose.
  Two consequences:
  - **Don't move the face bones.** The animation puts them back where they were made. Fit the new face to the
    girl's bones: her eyeballs' centres on her `eye` bones, her mouth around her `jaw` and lip bones.
  - **Keep the node order.** Tracks map to node indices, so face bones must stay in the order the girl's model has
    them.
- **Mirroring** (*code*): the animation can mirror a pose by swapping tracks 7-24 with 25-42. That's 7 middle bones
  plus 18 left/right pairs, all 43.
- **Eyes:** the eyeball meshes are spheres around the `eye` bones (Momiji's radius: 16 mm). An upper lid closes as
  the `eyelid` bone turns about its local x axis, from the bind pose (open) down over the eyeball.
- **The face's parts** (Momiji's 8 submeshes of `WGT_face`, found by rendering each alone):

  | # | Part |
  |---|---|
  | 0 | the upper lids' rims with the lashes |
  | 1 | the irises |
  | 2 | a dark shading layer over each eyeball, which turns with the lid |
  | 3 | the eyeballs |
  | 4 | the lips |
  | 5 | the skin |
  | 6 | the tongue |
  | 7 | the mouth's inside and the teeth |

  Shadow casting (`*T_SHADOW_MODEL`, head and neck only) and two cheek sweat meshes are separate objects. Other girls
  number their parts differently; render them to check.

## The hair rig and its physics

```
MOT01_Head ─ WGT_hair (the hair's meshes)
   ├─ OPT_acs_fronthair_root ─ OPT_acs_fronthairN   (bangs: single bones around the face)
   └─ OPT_acs_Hair_root ─ OPT_acs_pony1_root ─ OPT_acs_pony1_bone4 ─ bone20 ─ bone21 ─ ... ─ bone3   (a chain)
                        ─ OPT_acs_pony2_root ─ ...   (more chains; names and counts differ by hairstyle)
   XTR_acs_ornament_l/r                             (attachment points for hair ornaments)
```

- **Hair models differ in layout**, and every layout works:
  - Kasumi's `hair_00` is built in head space: its `MOT01_Head` is at the origin, with a dozen chains named
    `OPT_acs_hair_a1` to `_i1`.
  - Ayane's, Helena's, Marie Rose's, Leifang's and Momiji's sit at their girl's head height.
  - This fits the code above: the head bone takes the body's head, and the rest is relative to it.
  - Most also have an `OPT_head_dummy` bone (or `OPT_Head_dummy`, `OPT_dummy_head`), extra mesh nodes
    (`WGT_acs_hairfront`, ribbons, a bandana) and `WGT_zdmodel`: a copy of the hair drawn only into depth, for its
    transparency.
- **Physics lives in the model**, in its `ACSCLS` chunk. Hair and costumes have one; the face doesn't. Every model
  that has one is simulated each step (the "accessory physics", at 60 Hz), and the results are written into its
  bones.
- **What's in it** (*code*, `accessory_solver.cpp`), four tables:
  - **Simulated parts:** 0xf0 bytes each, seven kinds of chains and springs. The part's bone is a node index at +0x90.
  - **Colliders:** 0x7f0 bytes each, three kinds. Each has a node index at +0x30 and an offset, rotation and size.
  - **Parameter blocks:** 0x90 bytes each (stiffness and swing).
  - **Matrices.**
- **Bones are referenced by node index** into the same model, not by name. So keep a hair model's node list and
  order as they are; move bones, don't add or remove them.
- **A chain's records** (one per bone, 0xa0 bytes):
  - a matrix whose translation (+0x30) is the bone's place from the previous bone of the chain, in that bone's frame;
  - three triples (0.25, 0.5, 1);
  - two swing values (+0x80; lower holds the chain closer to its rest pose);
  - the node index (+0x88).

  Then come the links, 0x60 bytes each: the rest length (+0x00) and the two bone numbers within the chain (+0x50).
  - **Two chain layouts.** In Momiji's ponytail each bone is the previous one's child, so that translation equals
    the bone's local one. In her `hair_03` every bone hangs from one root, and it doesn't. Writing the local one
    there broke the rest pose, and the hair flew; `tmc.move_bones` handles both (checked against the game's own
    records).
  - **The swing values** are 0.6 at the root and 0.1 down Momiji's ponytail, and 1.0 at the root then 0 along
    `hair_03`'s chains.
- **Colliders** (the shapes the hair stays outside of: head, neck, shoulders) are in the hair's own `ACSCLS`,
  attached to the hair model's copies of the body bones. They are sized for the original girl.
- **Rest pose** (*code*): the simulation starts from the model's bind pose (each node's `HieLay` local). Moving a
  chain's bones in the model moves where the hair rests and how long its links are; `ACSCLS` doesn't keep its own copy.
- **Fitting new hair to a style's chains:** weight the new hair to the style's existing chains, moving the chain
  bones to where the new hair hangs (keep their count and order). A long lock weighted to a short bang bone whips
  around; keep bang bones' influence local.

## Soft parts of the body

- **Bones:** breasts (`OPT_Breast_*_base` with 18 `OPT_ywrk_` points each), hips (14 each) and thighs (6 each).
  `ywrk` is short for *yawaraka*, soft.
- **The game finds them by name** (*code*, `Character::InitSoftBody`), with up to 12 presets from the girl's files,
  probably `<GIRL>_YWRK_PHYD.phyd` (*inferred*). Costumes also have `_YWRK.ywrk` files; no reader for those was found.
- **For a new body:** keep these bones and their names. Weight the soft areas to them as the original body is.

## Animation

- **Body** (*code*): motions are banks of 21-bone poses: per bone a translation, a rotation (Euler angles) and a
  scale. They are evaluated over a skeleton stored in the bank and applied to the costume's first 21 nodes by index.
  Motion ids from 0x2000 up come from the bank shared by all girls (`CMN_..._INGAME_MPM`); lower ones from the
  girl's own (`<GIRL>_INGAME_MPM`). The only retargeting is a per-character root-height factor and the per-bone
  scales above.
- **Face:** `<GIRL>_FACE_MPM.mpm` and `<GIRL>_FACE_RIG_MPM.mpm`, absolute local transforms per face bone (above).
  Which of the two files does what isn't traced yet.
- **`.sktm`:** a relocatable table read by the animation code (file type 6). Its meaning isn't known yet.
- **What a mod can't change yet:** the motions themselves, and the skeleton they're made for. A character with
  different proportions should be fitted to the girl's body and skeleton.

## Putting another character in a girl's place: checklist

1. **Pick the girl with the closest build and hairstyle.** Her skeleton stays: height, arm and leg lengths, head
   size. Her body shapes are in every costume she wears.
2. **Head, in her head space.** Export her face (`model-export`). Scale and place the new head so that:
   - its eyeballs' centres sit on her `eye` bones;
   - its mouth is around her `jaw` and lip bones;
   - its neck meets her neck ring (the face skin's lowest vertices, where the body takes over).
   The face bones stay put.
3. **Face weights.** Give each part of the new face the weights of the same part of hers (an eye's outer corner
   from her eye's outer corner, not from whatever is nearest in space). Keep upper and lower lips apart. Then check
   by posing (see the lessons below).
4. **Hair.** Fit the new hair to one of her styles. Weight it to the style's chains and move the chain bones to
   where the new hair hangs, keeping their count and order. Check in game that the hair stays clear of the face and
   arms; the colliders are sized for her.
5. **Textures and shading:** the face atlas at her texture numbers, her shading mask, and the body skin's colour to
   match the new face (MODDING.md).
6. **Test** with `tools/route.sh` and screenshots: blinks (random: shoot every 4 frames), talking, the shower and
   the evening scenes for hair.

## Lessons from the face rigs

From fitting a custom head onto Momiji (November 2026). The faces were posed outside the game (jaw open, lips, a
blink of 25-50 degrees) and rendered side by side with hers; the numbers are each region's uneven motion between
neighbouring vertices.

- **Measure before fixing.** Uneven motion between neighbours ("jagged") showed up in numbers before it showed up
  in pictures. A 50-degree blink and a 14-degree jaw opening, rendered flat grey from the front and the side, show
  what the game's own small shots don't.
- **Eyelid crease:** a thick lid with its crease just above the edge can't take the original girl's weights as they
  are. Hers fall from about 0.8 at the lid's edge to about 0.3 over 15 degrees; squeezed into the 2-3 degrees of a
  tight crease, the crease's skin sank into the turning lid and crumpled. Smooth the eyelid weight over the crease's
  skin, holding the lid itself rigid.
- **Lashes ride on the lid's edge:** give every vertex of an upper-lash strip the weight of the lid edge nearest it,
  not of any lid skin. Otherwise the strip twists into facets as the lid closes.
- **Not every problem is the weights:** check whether the moving part is the skin, the lashes or another layer
  before changing anything. A grey render made a lash strip look like torn skin.
- **Chord sinking:** a vertex weighted 0.25-0.75 to a bone that turns 50 degrees sinks about 10% toward the pivot.
  Over an eyeball 1.3 mm below, that's enough to cut into it. Keep such layers either with the bone (0.77 and up) or
  off it (0.23 and down).
