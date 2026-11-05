# Modding DOAX3 Recomp

Mods replace the game's own files: models, textures, portraits, voice lines, anything in the game's data. The recomp
loads them from the `mods/` folder when the game starts. Your extracted game files are never changed, so removing a
mod puts the original back.

## What works today

| Goal | Status | How |
|---|---|---|
| A custom **face** or **hair** from your own model (Blender, any glTF) | **Works** (checked in game) | `model-export`, edit in Blender, `model-import` ([recipe](#recipe-a-custom-face-and-hair)) |
| New **textures** for a face, hair or costume | **Works** (checked in game) | `model-import --textures` |
| New **portraits**: the character select picture, profile card, icons | **Works** (checked in game) | `portraits-unpack` / `portraits-pack` ([recipe](#recipe-portraits-and-the-profile-card)) |
| New **voice lines** (391 for Kasumi; each girl has her own count) | **Works** (checked in game) | `voice-unpack` / `voice-pack` |
| New **short voice sounds** (shouts, laughs, reactions) | **Works** (checked in game; keep these banks at or below the original size) | `reactions-unpack` / `reactions-pack` |
| Give a girl another girl's face, hair and skin | **Works** (checked in game) | `swap-look` |
| Replace any other game file | **Works** | Put the file in `mods/<mod>/romfs/<game path>` |
| New subtitles | **Not yet**: the voice text table's format isn't decoded | |
| New costume bodies, new materials or extra submeshes | **Not yet**: the importer reuses a model's existing submeshes and materials | |
| Adding a 12th girl (a new slot) | **Not planned**: replace an existing girl instead | |

## Using mods

- Mods live in `mods/` in the data folder (the repo root when you run from `build/`, next to `extracted/`).
- Each mod is a folder: `mods/<ModName>/romfs/...`. The tools write it for you.
- A mod folder whose name starts with `_` is **off** (rename `MyMod` to `_MyMod` to turn it off).
- `LARECOMP_NO_MODS=1` turns every mod off for one run (to check whether a problem comes from a mod).
- Mods load in name order. When two mods replace the same file, the later name wins, and the log says so.
- Changes take effect the next time the game starts.
- The log (`doax3.log`) lists what was loaded (`[mods] ...` lines). If the game crashes with mods loaded, its message
  says so: turn the mods off one by one to find the cause.

A mod file's path below `romfs/` is the path the game's own index uses, for example
`mods/MyCharacter/romfs/00/character/KAS/hair/hair_00/KAS_HAIR_00_TMC.tmc`. The first folder is the package: `00`–`06`
are the game and its update, `00DLC...` are the DLC. Find any path with `doaxmod.py list <text>`.

## The mod tool

Run it from the repo root with the project's Python:

```
.venv/Scripts/python -I tools/mods/doaxmod.py <command> ...
```

| Command | What it does |
|---|---|
| `girls` | The girls' codes (KAS = Kasumi, MAR = Marie Rose, ...) and how many files each has |
| `girl-files GIRL [--all]` | Every file that makes up a girl: face, hair, body skin, costumes, animation, voice, UI images |
| `list [TEXT]` | Game file paths containing TEXT, with sizes |
| `extract PATTERN [--out DIR]` | Unpack game files (wildcards allowed) to `DIR/<path>` |
| `model-export PATH DIR` | A model (its `_TMC.tmc` game path) as `DIR/<name>.gltf` + `.bin` + `tex_NN.png`, and `model.txt` |
| `model-import PATH MOD [--gltf FILE] [--textures DIR] [options]` | The model rebuilt from your `.gltf` / `.glb` and/or `tex_NN` images, into mod MOD |
| `portraits-unpack GIRL DIR [--all]` | Her character select picture, profile card, icons and name plates as `DIR/<texture>.png` |
| `portraits-pack GIRL DIR MOD [--keep-alpha]` | Those textures from your images (any size or format), in every language version |
| `kslt-unpack PATTERN DIR`, `kslt-pack PATTERN DIR MOD` | The same for any UI texture pack (menus, logos, ...) |
| `voice-unpack GIRL DIR` / `voice-pack GIRL DIR MOD` | Her voice lines as WAVs / back from your recordings |
| `reactions-unpack GIRL DIR` / `reactions-pack GIRL DIR MOD` | Her short voice sounds |
| `swap-look TARGET SOURCE MOD` | TARGET gets SOURCE's face, hairstyles and body skin |
| `copy SRC DST MOD` | Put game file SRC into the mod in place of game file DST |
| `tmc-info FILE\|PATH` | The chunk tree of a model (for format work) |
| `check MOD` | Check a mod: every file is one of the game's, voice banks are valid, sound banks aren't too big |

Images and audio can be in any common format; the tool converts and resizes them with ffmpeg (part of the MSYS2
setup). Exporting the game's own BC7 textures needs `texture2ddecoder` in the venv
(`.venv/Scripts/python -m pip install texture2ddecoder`); without it those textures are listed but not exported.

## Recipe: a custom face and hair

You need [Blender](https://www.blender.org/) (free) and your character's head model: face and hair meshes with their
textures, in any format Blender opens. Examples below use Kasumi (`KAS`) and her default hair (`hair_00`).

### 1. Export the originals

```
.venv/Scripts/python -I tools/mods/doaxmod.py girl-files KAS
.venv/Scripts/python -I tools/mods/doaxmod.py model-export 00/character/KAS/face/face_00/KAS_FACE_00_TMC.tmc mod_work/kas_face
.venv/Scripts/python -I tools/mods/doaxmod.py model-export 00/character/KAS/hair/hair_00/KAS_HAIR_00_TMC.tmc mod_work/kas_hair
```

Each folder gets a `.gltf` (meshes and skeleton), the textures as `tex_NN.png`, and `model.txt`. `model.txt` lists:

- every **submesh**: a mesh named `<object>#<number>`, its vertex count, and the textures its material uses;
- every **texture**: its size, format and likely role (colour, normal map or mask);
- the **bones**.

The parts of a face:

| Mesh | What it is |
|---|---|
| `WGT_face#0` | the face and head skin (the brows are painted on) |
| `WGT_face#1` | the lips |
| `WGT_faceparts#2` | the eyeballs (sclera and iris in one texture) |
| `WGT_faceparts#0` | an iris layer over the eyeballs |
| `WGT_faceparts#5`, `#1` | the upper lashes, the lower lash line |
| `WGT_faceparts#6` | dark discs over the eyes (their shading) |
| `WGT_faceparts#4`, `#3` | the teeth and gums, the tongue |
| `*T_SHADOW_MODEL#0` | the shadow the face casts |
| `*_sweat_cheek_l/r#0` | sweat effects |

`WGT_face#0`, `#1` and the eyes, teeth and tongue share one colour texture (Kasumi's texture 4, 512×1024): a face
that replaces one of them has to give them all a place in a new texture (see "Lessons" below).

The parts of a hair:

| Mesh | What it is |
|---|---|
| `WGT_hair#0`–`#45` | strands and cards, each with a material |
| `WGT_zdmodel#0`, `#1` | the hair's depth (base) layer |

- **Depth layer:** give `WGT_zdmodel` your hair too, in the submesh that uses your hair's colour texture. Don't hide it.
  - It writes the hair's depth for the game's depth of field. Hidden, the background blurs over the hair's soft edges:
    blue fringes, palm leaves showing through, and hair that looks half missing.
  - A hang once blamed on replacing it was a timing race in the game's own main/worker handshake, unrelated to models.
    It's fixed in the game code (`WORKER_KICK`, `docs/CHANGES.md`).
- **Other hairstyles:** `hair_01` to `hair_05` differ. `hair_01` has `WGT_hair_a` and `WGT_hair_b`; `hair_02`, `_04`
  and `_05` add wet versions (`WGT_wet_asobi`, `WGT_wet_asobi1`) and are built in the face model's space (head at
  y 0.504). Some of their submeshes use textures from another file (numbers past their own).

### 2. Put your model in place (Blender)

1. **File > Import > glTF 2.0** and open `mod_work/kas_hair/KAS_HAIR_00.gltf` (do the face the same way). You see
   the original hair and its armature.
   - Everything is in the model's own space: the face model's has the head bone at y 0.504; the hair model's has the
     head bone at the origin (the game puts the hair on the head).
   - **Re-export** anything exported before November 2026. Older exports put the eyes, the face and the hair's
     depth layer in the wrong places and gave the face's vertices the wrong bones (see "Facts learned the hard way").
2. Import your own hair. Move, rotate and scale it until it sits where the original hair is.
3. **Apply its transforms:** select it, then **Ctrl+A > All Transforms**.
4. **Name each mesh object after the submesh it replaces**, for example `WGT_hair#0`.
   - It takes that submesh's **material and textures**, so pick a submesh whose material fits (hair on a hair
     material, skin on `WGT_face#0`).
   - Several objects with the same name (e.g. `WGT_hair#0` and `WGT_hair#0.001`) are joined into that submesh.
   - Anything after the number is ignored.
5. **Bones are optional.** You have three choices:
   - leave your mesh unrigged, and the tool copies bone weights from the nearest original vertex;
   - parent it to the imported armature with **Automatic Weights**, which keeps the game's bone names;
   - use `--rigid` when importing, which fixes everything to the head (no hair physics).
6. Delete the original meshes you don't want, or keep them all and use `--only-given` (see step 4).
7. **File > Export > glTF 2.0**:
   - Format: *glTF Separate* or *glb*.
   - Include: *Selected Objects* (your meshes, and the armature if you rigged them).
   - Mesh: *Apply Modifiers*, *UVs*, *Normals* and *Tangents* on.
   - Leave *+Y Up* on.

### 3. Textures

Your submesh uses the textures listed for it in `model.txt`; for `WGT_hair#0` that's `[0, 1, 2]`:

- **Colour:** the first one is the colour texture. Save yours as `tex_00.png` in a folder; any size works, and it's
  scaled to the original's size.
- **Normal map:** the one labelled "normal map". Replace it with your own model's normal map, or with a flat one
  (one solid colour, RGB 128, 128, 255); the original is laid out for the original's UVs.
- **Masks:** purple "mask" textures control the shading (hair highlights). Keep them, or make your own laid out for
  your UVs.
- **Sharing:** textures are shared by every submesh whose material uses them. Replacing one changes all those
  submeshes.

### 4. Build the mod

```
.venv/Scripts/python -I tools/mods/doaxmod.py model-import 00/character/KAS/hair/hair_00/KAS_HAIR_00_TMC.tmc MyCharacter --gltf mod_work/my_hair/hair.gltf --textures mod_work/my_hair --only-given
.venv/Scripts/python -I tools/mods/doaxmod.py check MyCharacter
```

| Option | Effect |
|---|---|
| `--only-given` | Every other submesh of the objects your glTF fills is hidden (it keeps one invisible triangle: the game crashes on empty submeshes). Without it, submeshes you don't give stay as they were. |
| `--rigid` | Every vertex on the object's main bone: the hair follows the head, with no strand physics. Good for short or stiff hair, and the safest start. |
| `--copy-weights` | Copy bone weights from the original vertices nearest each point (the 4 nearest, blended by distance) even for rigged meshes. It's also the default for unrigged meshes or meshes with other bones. Long hair then swings with the strand bones nearest to it; faces get the jaw, lip and eyelid bones for expressions. |
| `--hold-from-colour` | Each vertex's colour (red, 0–1; the glTF's `COLOR_0`) is how much of it is held to the head bone. Paint hair roots red, so they stay on the scalp while the ends swing. |
| `--hide A#0,B#1` | These submeshes are hidden (one invisible triangle each), for example the original lips when yours are part of your face, or `WGT_zdmodel#0,WGT_zdmodel#1`. |
| `--material-colour M:ROW=R,G,B,A` | Sets one row of a material's colours (`MtrCol`). Row 8 of a hair material is its highlight colour: Kasumi's is warm (0.9, 0.75, 0.65), which reddens dark hair; Momiji's is 1.25, 1.3, 1.35. Repeatable. |
| `--textures DIR` | Every `tex_NN.<image>` in DIR replaces texture NN (same size and format; exported textures you didn't change are skipped). It works without `--gltf` too. |
| `--keep-renpack` | The face's `RENPACK` (render data laid out for the original face mesh) is kept. By default it's emptied when `WGT_face` is replaced, since its data no longer matches. |

Do the face the same way. For a face:

- Replace `WGT_face#0` (and `#1`).
- Keep `WGT_faceparts` (the original eyes, teeth and brows) and shape your face around them, or replace those too.
- Give `*T_SHADOW_MODEL#0` a copy of your face, so the shadow matches.

### 5. Test

Start the game and look at the girl, for example with the scripted route `tools/route.sh relax 160 4500`. Frame 4500
is a close-up of Kasumi at the beach; screenshots go to `build/frames/`.

**Limits:**

- **No new materials or submeshes:** use the existing ones, as in step 2.
- **Vertices:** at most 65,535 per vertex buffer.
- **Weights:** at most 12 bone weights per vertex; more are trimmed to the strongest.
- **Expressions:** with copied weights, facial expressions follow the original's bones near each point. A face of very
  different proportions moves less well.
- **Hair colour:** the beach sun is warm and gives dark hair a red-brown sheen. Momiji's black hair measures about
  red/blue 2 in its lit parts. To get dark hair:
  - darken your hair texture and tint it a little blue;
  - use a black-haired girl's hair material (Momiji's: `--material-colour` rows 1, 2, 8, 9, 10);
  - use a mask at her levels (red 20, green 0, blue 22). Kasumi's 3–4 times stronger mask gives a copper sheen.
- **Texture size:** put fine hair on the girl's biggest hair texture (`model.txt` lists them). On Momiji's 256×512, a fine
  hair's strands averaged away and half of it vanished; her 1024×512 keeps them. Use `--alpha-coverage 0.3` so distant mips
  keep the same coverage.
- **Alpha cutoff:** material row 11's third value looks like the hair's alpha cutoff. Momiji's main hair materials have
  0.5; the other girls checked have 0.3. Setting 0.3 with `--material-colour M:11=*,*,0.3,*` (`*` keeps a
  value) was tried together with the bigger texture, so its own effect wasn't measured.
- **Thin strands:** the game cuts hair at an alpha threshold, without smoothing. Fine, 1-pixel strands then come out
  as hard lines, and their shadows on the skin look like barcodes. Blur the alpha across the strands only, and
  within each texture column. Blurred both ways or past a column's edge, cards show as hard rectangles.

## Lessons: fitting a whole head from another model

These come from fitting a complete head, with face, hair, body colour and portraits, onto a girl's rig with scripts in
`mod_work/` (ignored by git, as your mods and working files are). The mod and its scripts aren't part of this project,
since mods built from other people's characters can't be shared here.

Face parts differ by girl. Momiji's face is one object, `WGT_face`, with 8 parts: `#0` lashes (both lids), `#1` iris,
`#2` eye-shading discs, `#3` eyeballs, `#4` lips, `#5` skin, `#6` tongue, `#7` teeth. Kasumi's splits these between
`WGT_face` and `WGT_faceparts`. The bones and portrait names are the same for both (`OPT_Face_*`, `chara_<girl>`).

**Fit**

- **One scale:** one uniform scale, from the eyes to the girl's eye bones, usually brings the mouth within 1-3 mm.
- **Weights, part for part:** the game's expressions are authored for the girl's own weights, so give each skin vertex the
  weights of the same part of her face.
  - Warp your face onto hers with landmarks: each eye's opening, the lip and brow bones, the nose, chin and forehead. Then
    take the weights from the nearest point on her skin and lips. Copying from the nearest point without the warp makes
    a mouth look puffy and pulled, and mapping your own rig's bones by name stops the lip corners and cheeks moving as a
    unit.
  - Keep upper and lower lip vertices on their own side: they touch at rest, so nearest alone can pick the wrong one.
  - Around the eyes, look up from the front. A vertex in the opening (the lid's inward fold) takes its lid edge's weights;
    with nearest, the fold finds her inner rim, where weights fall away, and the lid stops halfway.
  - Lid weights aren't 1. Momiji's peak at about 0.85 and fade over 1 cm. At 1.0 up to a sharp edge the lid moves as a
    rigid flap with a crease. Scale your lid's weight by the two openings' angles, so the same bone turn closes it.
- **Eyes:** the eyeball must stay inside the lid's radius, because the game's lid turns about the eye's pivot at its own
  radius. Momiji's front is at 14.3 mm from the pivot. An eyeball at 17.9 mm came through lid skin at 15-16 mm.
- **Lashes:** the girl's own lash band, warped onto your eyes, holds up best. Real strands thinner than a pixel break into
  black specks, as there's no antialiasing. Place the band by looking from the front, in case your lid stands further out.

**Colour**

- **Shading mask first:** a face's mask texture (Momiji's texture 8) marks skin from lips and eyes, and rises to the body's
  values down the neck (red 245 at the neck ring, 240 on the body, 129 averaged over the face). A flat average shades the
  whole face unlike the body, and no recolouring fixes it. Bring her mask onto your layout, and the alpha of her colour
  texture too: it's also a shading value.
- **Body skin:** recolour `bodybase/<GIRL>_BODYBASE_TEXCHL.texchl` (a 0x40-byte header, then a G1T) by your face skin over
  hers, in linear light.
- **Measure across the seam:** comparing brightness bands across the whole figure reported a 0.5% match while the seam was
  plainly visible. Compare against the same frame with `LARECOMP_NO_MODS=1`.
- **The neck seam:** cut the neck along her neck ring (it's tilted), shape the last 1.5 cm to the ring's radius and blend it
  to the ring's weights. Paint the last 3 cm to her neck colour times the body's gain, and fill the texture past your
  triangles with the same colour, or it bleeds in through the mips as a pale line. Fade the normals to hers over the last
  2 cm: hers meet her body's, and a difference there is a lighting jump.

**Hair**

- **Depth layer:** hair must also go in `WGT_zdmodel`, which writes depth for depth of field. Hidden, the background blurs
  over the hair.
- **Texture:** use the style's biggest colour texture, and check the material's alpha cutoff (Momiji's main hair is 0.5, the
  rest 0.3). Together with a hidden depth layer, these can lose half the hair.
- **Clearance:** keep hair a few mm off the skin and at least 1.2 cm outside the body. This fixes the resting shape only:
  moving arms can still pass through hair.
- **Physics chains:** the game simulates long hair as chains of particles. `tmc.move_bones` moves bones and physics data
  together, so chains can follow your strands, keeping their segment lengths. Keep them behind the strands: moving them
  toward the body sinks hair through it. `tmc.chain_swing` sets each node's stored value (ACSCLS node record +0x80). Higher
  swings more; 0.9 at the root whips the hair. Share each back vertex across chains by distance, so hair stretches instead
  of splitting.
- **Bang bones:** short physics bones should only carry what's within about 5 cm. A long lock on one whips around.
- **Draw order:** sort whole cards inner first. The hair is one mesh drawn in face order with depth writes, so a faint outer
  card drawn first hides those behind it, and skin shows through.
- **Known limit:** whether chains collide with a custom body shape is unknown, and `<GIRL>_YWRK_PHYD.phyd` isn't decoded. Long
  hair that starts on the scalp can go into the body when she bends right over.

### How the face's expressions work (`KAS_FACE_RIG_MPM`)

- **The bones:** the face model has 44 `OPT_Face_` bones (`OPT_Face_Root` and 43 under it: jaw, lips, eyelids,
  brows, cheeks, nose, tongue, eyes). Skin vertices move with them by their weights.
- **The rig file** (`00/character/KAS/animation/KAS_FACE_RIG_MPM.mpm`) is a `char_dat` container of 6 chunks:
  - a header naming the root, `OPT_Face_Root`, with 44 bones;
  - a hierarchy of 44 entries in the face model's node order (the root last), each its parent and children (the jaw
    lists the lower lip and tongue bones);
  - about 246 byte-coded curve records (5–6 per bone), which move the bones. Their encoding isn't decoded yet.
- **Per girl:** every girl has her own rig, and they differ (Ayane's has 242 records).
- **For a custom face:** keep the original's skeleton and rig, fit your face's features to her bones, and copy the
  weights from her nearest vertices. Ayane's face on Kasumi's rig worked the same way.

## Recipe: portraits and the profile card

```
.venv/Scripts/python -I tools/mods/doaxmod.py portraits-unpack KAS mod_work/kas_portraits
```

`textures.txt` in the folder lists every image with its size:

| Texture | Size | Shown |
|---|---|---|
| `chara_kas` | 1024×1024 | the full-body picture on the character select screen |
| `status_kas` | 1024×512 | the profile card: name, nationality, birthday, measurements, hobby, food, colour (it's an image, so edit the text in any image editor) |
| `chara_icon_kas`, `chara_icon_kas_00`–`_03` | 256×300 | the round portrait icons |
| `mission_chara_kas` | 256×256 | a smaller portrait |
| `status_name_kas`, `chara_button_kas`, `r_coin_kas`, ... | various | name plates and buttons |

Edit the PNGs or replace them with your own images. Keep the names; the size doesn't matter, they're scaled to fit.
Then build the mod:

```
.venv/Scripts/python -I tools/mods/doaxmod.py portraits-pack KAS mod_work/kas_portraits MyCharacter --keep-alpha
```

- `--keep-alpha` applies each original's transparency: round icons stay round and the select picture keeps its
  cut-out, even from a plain photo.
- Leave it off if your images have their own transparency.
- Each texture is replaced in every language version of its pack.
- Checked in game: the character select picture, the profile card and the icon in the select row change.

## Recipe: new voice lines

```
.venv/Scripts/python -I tools/mods/doaxmod.py voice-unpack KAS mod_work/kas_voice
```

1. Listen to `mod_work/kas_voice/*.wav` to find which number is which line.
2. Name your recordings by line number (`012.wav`, `013_hello.mp3`, ...: the number first) in a folder. Lines you
   don't provide keep the original voice.
3. Build the mod: `doaxmod.py voice-pack KAS mod_work/my_voice MyCharacter`, then `check MyCharacter`.
4. Do the same for her short sounds with `reactions-unpack` / `reactions-pack`.

**Size limits:**

- **Short-sound banks (`.xws`) must not grow.** The game keeps them in memory: banks 1.8x and 2.1x their size hung it
  on a loading screen. Keep the new sounds about as long as the originals; the tool warns when a bank grows.
- **Dialogue lines can be longer.** That bank is streamed: 1.6x its size played in full.

The subtitles stay the original's.

## What a girl is made of

`doaxmod.py girl-files <GIRL>` lists all of it. For Kasumi (`KAS`):

| Part | Files | Tool |
|---|---|---|
| Face | `00/character/KAS/face/face_00/KAS_FACE_00_TMC.tmc` + `_TMCL.tmcl` | `model-export` / `model-import` |
| Hairstyles | `00/character/KAS/hair/hair_NN/KAS_HAIR_NN_TMC.tmc` + `_TMCL.tmcl` | same |
| Body skin texture | `00/character/KAS/bodybase/KAS_BODYBASE_TEXCH.texch` + `_TEXCHL.texchl` | not yet |
| Costumes | `<package>/character/KAS/costume/<design>_<kind>_<n>_<variant>/` (TMC, TMCL, YWRK physics) | textures: `model-import --textures` |
| Animation, physics, data | `00/character/KAS/animation/*`, `KAS_DAT.bin`, `KAS_YWRK_PHYD.phyd` | (keep the girl's) |
| Voice | `00/sound/char/SND_CHAR_KAS_JP_XHS.xhs`, `..._XWS.xws`, `..._CASINO_JP_XWS.xws` | `voice-*`, `reactions-*` |
| Portraits, profile, icons | KSLT texture packs in `00/ui/` | `portraits-*` |
| Subtitles | `00/voice_msg/VOICE_MSG_TABEL_KAS.bin` | not decoded |

## Instructing a Claude agent

The tools above cover most of a custom character. An agent is useful for the rest:

- turning your files into what the tools take (e.g. driving Blender from a script);
- fitting a face to the original eyes and teeth;
- the parts not done yet: subtitles, body skin, new materials, a better BC7 encoder.

Give it this prompt in a Claude Code session opened in this repo, with the brackets filled in:

```
Read docs/MODDING.md, especially the "Agent brief" section, and follow it.

Goal: replace [Kasumi] in DOAX3 Recomp with my custom character [name].
My files: [path to the head model: .blend / .fbx / .glb with textures], [portrait images], [voice recordings, named by
line number, or "none yet"].
First milestone: [her face and default hair (hair_00) in game]; then [portraits], [voice].
Put everything in the mod mods/[MyCharacter]. Never change extracted/. Before each milestone, tell me the plan and
what you will test; afterwards, show me screenshots from the game.
```

### Agent brief

You are building a custom-character mod for DOAX3 Recomp: a static recompilation of DEAD OR ALIVE Xtreme 3 Scarlet
(Switch, update 1.0.6, all DLC) for Windows.

**Rules**

- Never modify `extracted/` (the user's game files) or the game's code. Output goes to `mods/<ModName>/romfs/`
  through the tool.
- Working files go in `mod_work/`. Keep test output small and delete it when done (screenshots are 2-4 MB each).
- One mod folder, which the user can turn off by renaming it with a `_` prefix. Say plainly what didn't work.
- Python: `.venv/Scripts/python -I`. MSYS2 tools (ffmpeg, libopus) are in `C:\msys64\ucrt64\bin`. Ask the user before
  installing anything else (Blender, if it isn't installed; numpy; ...). Blender can run headless
  (`blender --background --python script.py`) to convert FBX/OBJ/blend to glTF.

**Use the tools; read their code for the formats** (`tools/mods/`):

- `doaxmod.py`: every command above.
- `tmc.py`: the model format. Its docstring documents TMC/TMCL; it holds the glTF export and import, weight copy and
  the BC1 / BC7 (mode 6) encoders.
- `kslt.py`: UI textures (linear BC3 / RGBA8).
- `ktpkg.py`: packages.
- `ktss.py`, `kwb.py`: voice.
- `runtime/mods.c`: the loader.

**Facts learned the hard way**

- **Linear textures:** model textures are G1T files in the TMCL, BC1 (0x59) or BC7 (0x5f), stored **linear** with all
  mip levels. UI textures (KSLT) are linear BC3. No swizzling anywhere.
- **Node names:** a `NodeObj`'s name starts at +0x50 (`MOT01_Head`, `OPT_Face_jaw`, `OPT_acs_hair_a1_bone12`), and
  every name in the game's models is unique. The tools read it from +0x60 until November 2026, which cut long names
  short and made it look as if hair repeated its bone names.
- **Bone palettes:** a vertex's bone numbers index its object's palette, not the node list. The palette is in the
  object's own `NodeObj`, in a block whose offset follows the name: object number, palette size, node, then the
  palette at +0x60 (the face 41 bones, its parts 24). The tool maps node numbers to and from it.
- **Spaces:** each object's vertices are in its node's space: the face's eyes relative to the head, the face skin
  offset by (0, −0.046, −0.022), the hair's depth layer in the face model's space. Export applies the node's world
  matrix; import undoes it.
- **No empty submeshes:** an empty submesh (0 indices) crashes the game. Hide one with a zero-area triangle, as
  `--only-given` does.
- **Winding:** triangles are counter-clockwise front faces, as in glTF.
- **Stride:** a vertex declaration's stride is at +0x44 of its GeoDecl entry; the first field isn't the stride.

**Testing in the game**

- Nothing to build for a mod: the runtime reads `mods/` at start.
- `tools/route.sh relax 160 3900,4500` takes a fresh save to Kasumi's arrival. Frame 3900 is the character select;
  frame 4500 a close-up of her face. `tools/route.sh select` idles on the character select. The routes move `save/`
  aside and put it back afterwards.
- `LARECOMP_SHOT_SIZE=480x270` makes screenshots small. `LARECOMP_AUDIO_WAV=<file>` records the game's audio.
- `doax3.log` has `[mods]` lines; a crash with mods loaded says so. `LARECOMP_NO_MODS=1` runs without mods.
- Always do the **round trip** first: export, re-import unchanged, check in game. A broken round trip means a tool
  bug, not your model.

## Formats reference

- Models (TMC, TMCL, G1T textures): `tools/mods/tmc.py` (docstring).
- UI textures (KSLT): `tools/mods/kslt.py`.
- Packages: `tools/mods/ktpkg.py`.
- Voice lines (G1L, KTSS, Opus in Nintendo's packet framing): `tools/mods/ktss.py`.
- Short sounds (XWSFILE, KWBN, Nintendo DSP ADPCM): `tools/mods/kwb.py`.
