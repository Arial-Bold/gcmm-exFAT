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
} UiSettings;

extern UiSettings ui_set;

void ui_settings_path(const char *path);   /* where settings.ini is kept (once the SD card is ready) */
void ui_settings_load(void);
void ui_settings_save(void);
void ui_settings_changed(void);             /* call after changing ui_set (the video mode follows) */
const char *ui_resolution_name(int r);

/* --------------------------------------------------------------- lifetime */

void ui_init(void);           /* video, graphics and the drawing thread (call first) */
void ui_shutdown(void);       /* before rebooting or starting another program */
GXRModeObj *ui_video_mode(void);

/* ------------------------------------------------------------------ scene */

enum { UI_SPLASH, UI_MAIN, UI_LIST };
enum { UI_ITEM_FILE, UI_ITEM_FOLDER, UI_ITEM_SAVE };
enum { UI_OVERLAY_NONE, UI_OVERLAY_DEVICES, UI_OVERLAY_SETTINGS };

#define UI_LIST_ROWS 10
#define UI_INFO_ROWS 8
#define UI_OVERLAY_ROWS 6

void ui_screen(int screen);
void ui_title_info(const char *text);                  /* right of the title bar: version and device */
void ui_splash_text(const char *text);
void ui_status(const char *line1, const char *line2);  /* the description bar along the bottom */

/* main screen */
void ui_main_cards(const char *mc_name, const char *mc_info, const char *sd_name, const char *sd_info);
void ui_main_select(int row);                           /* 0: saves on the memory card, 1: saves on the SD card, -1: none */

/* list screen */
void ui_list_header(const char *path, const char *mode);
void ui_list_clear(void);
void ui_list_row(int row, const char *name, int kind);
void ui_list_select(int row);
void ui_list_scroll(int first, int count, int total);    /* for the scroll bar */
void ui_info_clear(void);
void ui_info_title(const char *title);
void ui_info_row(int row, const char *label, const char *value);
void ui_info_folder(bool folder);
void ui_info_banner_rgb(const u8 *bgr);                 /* 96x32, 3 bytes a pixel (blue, green, red) */
void ui_info_icon_rgb(const u8 *bgr);                   /* 32x32, likewise */
void ui_info_no_pictures(void);

/* panels over the screen */
void ui_overlay(int overlay, const char *title);
void ui_overlay_row(int row, const char *label, const char *value);   /* value NULL: a plain choice */
void ui_overlay_select(int row);
void ui_overlay_rows(int count);

#endif
