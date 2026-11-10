/****************************************************************************
* ui.h - GCMM's screens in the GameCube "System" look, drawn by the graphics
* chip on a thread of their own.
*
* The rest of GCMM describes what should be on screen (which screen, the
* list, the save's details, the status bar) and this draws it every frame,
* so the memory card code can wait and work exactly as before. The screen
* shape settings (resolution, 4:3 or 16:9, picture width, overscan) live
* here too.
****************************************************************************/
#ifndef _GCMM_UI_H_
#define _GCMM_UI_H_

#include <gccore.h>
#include <stdbool.h>

/* ---------------------------------------------------------------- settings */

enum { UI_RES_AUTO, UI_RES_480I, UI_RES_480P, UI_RES_COUNT };
#define UI_OVERSCAN_MAX 10

typedef struct {
	int resolution;   /* UI_RES_* */
	int widescreen;   /* 0 = 4:3, 1 = 16:9 */
	int full_width;   /* 1 = 704 (default), 0 = 640 */
	int overscan;     /* percent, 0..UI_OVERSCAN_MAX */
	int sound_volume; /* menu sounds, 0 (off)..UI_VOLUME_MAX */
	char launcher[64];/* the .dol at the root of the SD card that Exit starts ("" = find one) */
	int intro;        /* 1 = the opening plays at start-up */
} UiSettings;
#define UI_VOLUME_MAX 10

extern UiSettings ui_set;

void ui_settings_path(const char *path, const char *old_path);   /* where the settings are kept (once the SD card is ready), and an older file to read if that one isn't there yet */
void ui_settings_load(void);
bool ui_settings_save(void);   /* false if the SD card wouldn't take them */
void ui_settings_changed(void);             /* call after changing ui_set (the video mode follows) */
const char *ui_resolution_name(int r);

/* --------------------------------------------------------------- lifetime */

void ui_init(void);           /* video, graphics and the drawing thread (call first) */
void ui_shutdown(void);       /* before rebooting or starting another program */
GXRModeObj *ui_video_mode(void);

/* ------------------------------------------------------------------ scene */

enum { UI_SPLASH, UI_MAIN, UI_LIST, UI_TOOLS, UI_UPDATE, UI_SETTINGS, UI_TRANSFER };
/* what a row's chip says */
enum { UI_ST_NONE, UI_ST_NEW, UI_ST_CHANGED, UI_ST_BACKED_UP, UI_ST_ON_CARD, UI_ST_NOT_ON_CARD };
enum { UI_OVERLAY_NONE, UI_OVERLAY_DEVICES, UI_OVERLAY_CARDS };
/* the pictures beside the rows of the cards panel (UI_OVERLAY_CARDS: label = name, value = details) */
enum { UI_ROW_NONE, UI_ROW_CARD, UI_ROW_VIRTUAL, UI_ROW_SD };
void ui_overlay_row_icon(int row, int icon);

#define UI_LIST_ROWS 8
#define UI_OVERLAY_ROWS 9

void ui_screen(int screen);
int  ui_get_screen(void);
void ui_splash_ready(void);   /* the splash screen's logo waits for this (once the settings are read) */
void ui_title_info(const char *text);                  /* the version (shown on the splash screen) */
void ui_splash_text(const char *text);
void ui_status(const char *line1, const char *line2);  /* the description bar along the bottom */
void ui_progress(float f);                              /* a bar in the description bar, 0..1 (< 0: none) */
/* the controls along the very bottom, CubeVerse style: "key:label", where the
 * key is S (the Control Stick), A, B, X, Y, Z, ST (START) or none ("label") */
#define UI_HINTS_MAX 5
void ui_hints(const char *const *hints, int n);
/* a message across the top that fades after a few seconds ("Press B again to exit") */
void ui_toast(const char *msg);

/* The opening: the logo flips over and goes into a slot, then the screen
 * comes up (about 7 seconds; the sound is SFX_INTRO, started alongside).
 * The screen set with ui_screen shows at its end. */
void ui_intro_start(void);
bool ui_intro_playing(void);
void ui_intro_skip(void);   /* straight on to the screen coming up */

/* The easter egg: virtual memory cards made of Swiss cheese (until GCMM exFAT closes) */
void ui_cheese(bool on);
bool ui_cheese_on(void);

/* the Network transfer page: the QR code (size x size modules, 1 = dark;
 * size 0 = none yet), the address, a line about the network, and the files
 * that have come in (newest first) */
#define UI_TRANSFER_ROWS 4
void ui_transfer_page(const unsigned char *qr, int size, const char *address, const char *state);
void ui_transfer_row(int row, const char *name, const char *detail, float progress);   /* progress < 0: done; name "": none */
void ui_hints_push(void);   /* keeps the controls shown now (for a question), */
void ui_hints_pop(void);    /* and brings them back */

/* the Settings page: rows as for the panels (ui_overlay_row, _select, _rows);
 * a value starting with \x02 is information, not a choice (no arrows) */

/* the header: the memory card and the SD card */
void ui_main_cards(const char *mc_name, const char *mc_info, const char *sd_name, const char *sd_info);
void ui_main_card2(const char *mc_name, const char *mc_info);   /* a second memory card ("" for none) */

/* main screen: the three jobs (0 Back up, 1 Restore, 2 Card tools; -1 none chosen) */
void ui_main_select(int tile);

/* the Card tools page: eight tiles, two across (0 back up the whole card,
 * 1 restore a whole card, 2 copy between cards, 3 delete saves, 4 format,
 * 5 new virtual card, 6 network transfer, 7 settings) */
#define UI_TOOLS_COUNT 8
void ui_tools_select(int tile);

/* the Update page: this version, the newest on GitHub ("" while checking)
 * and what's new in it */
void ui_update_page(const char *this_version, const char *newest, const char *notes);

/* list screen: the saves (or folders, or card images) to choose from */
void ui_list_header(const char *title, const char *right);
void ui_list_clear(void);
void ui_list_checkable(bool on);                        /* rows have tick boxes */
/* checked: 0 or 1 (ignored for folders); blocks: "" for none */
void ui_list_row(int row, const char *title, const char *detail, const char *blocks, int status, int checked, bool folder);
void ui_list_icon(int row, const u8 *bgr);              /* 32x32, 3 bytes a pixel (blue, green, red); NULL: none */
void ui_list_select(int row);
void ui_list_scroll(int first, int count, int total);    /* for the scroll bar */

/* panels over the screen */
void ui_overlay(int overlay, const char *title);
void ui_overlay_row(int row, const char *label, const char *value);   /* value NULL: a plain choice */
void ui_overlay_select(int row);
void ui_overlay_rows(int count);
/* a scrolling overlay list: the rows from row `from` down show items first..first+count-1 of total (for the scroll bar) */
void ui_overlay_scroll(int from, int first, int count, int total);

#endif
