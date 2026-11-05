// The game's RomFS image, laid out from a folder of game files (runtime/romfs_build.c). The game reads its data through
// one raw RomFS image; runtime/services/fs.c serves that image from this layout - the header and tables in memory, each
// file's data straight from its file in the player's romfs/ folder - so no second copy of the game data is kept.
#pragma once
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define ROMFS_DATA_START 0x200u
// A file made of pieces instead of one host file (mods: runtime/mods.c). Each piece is `size` bytes at `at` in the
// file: from mem, else from the host file path at off, else zeros. Pieces are in order and cover the file.
typedef struct { uint64_t at, size; const uint8_t *mem; const char *path; uint64_t off; } RomfsPiece;
typedef struct { RomfsPiece *pieces; int n; uint64_t size; } RomfsSource;
typedef struct { uint64_t start, size; char *path; const RomfsSource *src; } RomfsLayoutFile;  // start: offset in the image
typedef struct {
    uint8_t header[ROMFS_DATA_START];  // the image's first bytes (the 0x50-byte header, then zeros)
    uint64_t tables_off, tables_size;  // dir hash, dir entries, file hash and file entries, one blob
    uint8_t *tables;
    RomfsLayoutFile *files;  // sorted by start; the bytes between files read as zeros
    int nfiles;
    uint64_t size;  // the whole image
} RomfsLayout;

// Lays out the RomFS image of the folder `dir`, with the mods' replacements (mods_source). false = failed (why: a
// short message).
bool romfs_layout(const char *dir, RomfsLayout *out, char *why, size_t why_size);
// n bytes of a pieced file at off (within its size). -1: a host file couldn't be read.
long long romfs_source_read(const RomfsSource *s, uint8_t *dst, uint64_t n, uint64_t off);
void romfs_layout_free(RomfsLayout *l);

// Bytes of file data under dir (the image is about this plus its tables).
uint64_t romfs_dir_bytes(const char *dir);

// Copies the folder tree src to dst (hard links where the file system allows: no second copy of 6 GB). progress as
// above. false = failed.
bool romfs_copy_tree(const char *src, const char *dst, void (*progress)(uint64_t done, uint64_t total));

#ifdef __cplusplus
}
#endif
