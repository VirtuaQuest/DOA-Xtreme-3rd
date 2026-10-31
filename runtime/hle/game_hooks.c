// Game hooks: the places where the shared runtime (input, renderer, file system, frame pacing) asks the game-specific
// code something. In LAS Recomp these were Link's Awakening features (warp, mirror mode, widescreen camera, pots, pets,
// save editor, extra flags); for DOAX3 they answer "nothing special" until a DOAX3 counterpart is written.
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "../hle.h"

// ---- game state
int game_in_play(void) { return 0; }  // (not located yet: the menu's save editor and LED health stay off)
bool world_on_screen(void) { return false; }
const char *game_level_name(void) { return ""; }
uint64_t game_camera_zone(void) { return 0; }
int cheats_link_health(int *max) { if (max) *max = 0; return -1; }

// ---- version, for the shader caches' tag (runtime/nvn/shader_prep.c)
const char *larecomp_version(void) { return "doax3-dev"; }

// ---- input (runtime/hle/input.c)
bool mirror_input(void) { return false; }
uint64_t pets_menu_input(uint64_t b, int32_t st[4]) { (void)st; return b; }
uint64_t tracker_map_input(uint64_t b, int32_t st[4]) { (void)st; return b; }
void input_note_rstick(int32_t ry) { (void)ry; }
int32_t g_stick_lx, g_stick_ly;
uint64_t g_stick_freeze_until;
int32_t g_test_lx, g_test_ly;
uint64_t g_test_stick_until;
uint64_t g_test_buttons, g_test_buttons_until;
char g_window_shot_request[256];

// ---- file system (runtime/services/fs.c)
void romfs_file_read_hook(const char *path) { (void)path; }
void save_editor_file_read(const char *path, uint64_t addr, int64_t off, int64_t size) {
    (void)path, (void)addr, (void)off, (void)size;
}
void flags_ext_save_read(const char *path, uint64_t addr, int64_t off, int64_t size) {
    (void)path, (void)addr, (void)off, (void)size;
}
bool flags_ext_save_written(const char *path, uint64_t addr, int64_t off, int64_t size) {
    (void)path, (void)addr, (void)off, (void)size;
    return false;
}
void save_editor_frame(uint64_t frame) { (void)frame; }

// ---- renderer (runtime/nvn/vulkan): per-draw tints and the widescreen camera
void escape_bottle_tint(int kind, const float *ui, uint32_t ui_size, float tint[8]) {
    (void)kind, (void)ui, (void)ui_size, (void)tint;
}
bool hint_sheet_icon_draw(const float *ui, uint32_t ui_size) { (void)ui, (void)ui_size; return false; }
bool mirror_draw(bool composite) { (void)composite; return false; }
bool mirror_scene_size(uint32_t sw, uint32_t sh, uint32_t w, uint32_t h) { (void)sw, (void)sh, (void)w, (void)h; return false; }
void scene_size_log(bool composite, uint32_t sw, uint32_t sh, uint32_t w, uint32_t h) {
    (void)composite, (void)sw, (void)sh, (void)w, (void)h;
}
bool pets_hide_item_cursor(void) { return false; }
bool pot_glow_active(void) { return false; }
bool pot_level_active(void) { return false; }
void pot_gold_tint(float tint[8]) { (void)tint; }
bool pot_instance_gold(const float *m) { (void)m; return false; }
bool pot_tint(const float *m, float tint[8], uint64_t mesh) { (void)m, (void)tint, (void)mesh; return false; }
bool widescreen_allowed(void) { return false; }
void widescreen_backdrop_seen(uint64_t hash) { (void)hash; }
float widescreen_effect_stretch(uint64_t fs_hash) { (void)fs_hash; return 1.0f; }
void widescreen_frame_end(uint64_t frame) { (void)frame; }
void widescreen_present_crop(uint64_t frame, double shape, float crop[4], double *vx, double *vy, double *vw, double *vh) {
    (void)frame, (void)shape, (void)vx, (void)vy, (void)vw, (void)vh;
    crop[0] = crop[1] = 0, crop[2] = crop[3] = 1;  // the whole frame, the viewport as it is
}
void widescreen_scene_ubo(const float *u, uint64_t size) { (void)u, (void)size; }
void widescreen_set_factor(float f) { (void)f; }
bool widescreen_ui_pane_partial(const float *u, uint64_t size) { (void)u, (void)size; return false; }
