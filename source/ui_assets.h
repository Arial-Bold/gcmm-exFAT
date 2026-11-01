/* ui_assets.h - made by tools/make_assets.py: do not edit by hand. */
#ifndef UI_ASSETS_H
#define UI_ASSETS_H
#include <gctypes.h>

/* one character of a font atlas: where it is, its size, where it sits
 * against the pen (left, top from the baseline) and how far it moves the
 * pen (in 1/16 pixels) */
typedef struct { u16 x, y; u8 w, h; s8 left, top; u16 adv16; } UiGlyph;
typedef struct { const u8 *tex; u16 w, h; u8 px, ascent, descent; const UiGlyph *glyphs; } UiFont;

extern const UiFont font_small;   /* 14 px */
extern const UiFont font_large;   /* 24 px */

#define LOGO_TITLE_W 120
#define LOGO_TITLE_H 36
extern const u8 logo_title_tex[];   /* RGBA8 */
#define LOGO_SPLASH_W 384
#define LOGO_SPLASH_H 120
extern const u8 logo_splash_tex[];   /* RGBA8 */
#define CARD_MC_W 64
#define CARD_MC_H 84
extern const u8 card_mc_tex[];   /* RGBA8 */
#define CARD_SD_W 64
#define CARD_SD_H 84
extern const u8 card_sd_tex[];   /* RGBA8 */
#define CARD_FILE_W 16
#define CARD_FILE_H 20
extern const u8 card_file_tex[];   /* RGBA8 */

#endif
