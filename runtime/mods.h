// Mods (docs/MODDING.md): mods/<name>/romfs/<path> in the data folder replaces the game file <path>, named as the
// game's package index names it ("00/character/KAS/face/face_00/KAS_FACE_00_TMC.tmc",
// "00/sound/char/SND_CHAR_KAS_JP_XHS.xhs", "00DLC00020000000/character/MAR/..."). A file packed in a package's
// data_*.lnk is served from the mod by a changed copy of that package's index and archive table (in memory; the
// game files are never changed); a loose file (movies, sound banks) is served from the mod in its place.
// Folders whose name starts with "_" or "." are off; LARECOMP_NO_MODS=1 turns every mod off.
#pragma once
#include <stdint.h>

#include "romfs_build.h"

#ifdef __cplusplus
extern "C" {
#endif

// The replacement for the file rel ("00/data_00.lnk") of the RomFS image laid out from dir (host: its file there,
// host_size bytes), or NULL to keep it. Called by romfs_layout.
const RomfsSource *mods_source(const char *dir, const char *rel, const char *host, uint64_t host_size);
// The mods in use and the files they replace (for the log and the crash explanation)
int mods_count(void);
int mods_files(void);

#ifdef __cplusplus
}
#endif
