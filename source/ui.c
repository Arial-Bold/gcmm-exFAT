/****************************************************************************
* ui.c - see ui.h
*
* Drawn in a 640x480 layout. The projection widens what's visible for 16:9
* (TVs that stretch the picture) and pulls everything in from the edges for
* overscan, so the screens themselves never need to know about either.
*
* The look is the GameCube "System" one: a TV-screen backdrop, teal-black
* panels with a light double frame, frosted rows, the gold selection ring
* and the indigo description bar.
****************************************************************************/
#include <gccore.h>
#include <ogc/lwp_watchdog.h>
#include <malloc.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include "ui.h"
#include "ui_assets.h"

/* ================================================================ settings */

UiSettings ui_set = { UI_RES_AUTO, 0, 1, 0, 4, "", 1 };
static char settings_file[300], settings_old[300];

void ui_settings_path(const char *path, const char *old_path) {
	snprintf(settings_file, sizeof(settings_file), "%s", path);
	snprintf(settings_old, sizeof(settings_old), "%s", old_path ? old_path : "");
}

const char *ui_resolution_name(int r) {
	switch (r) {
	case UI_RES_480I: return "480i";
	case UI_RES_480P: return "480p";
	default: return "Auto";
	}
}

void ui_settings_load(void) {
	if (!settings_file[0]) return;
	FILE *f = fopen(settings_file, "r");
	if (!f && settings_old[0]) f = fopen(settings_old, "r");   /* from an earlier version */
	if (!f) return;
	char line[128];
	while (fgets(line, sizeof(line), f)) {
		char *eq = strchr(line, '=');
		if (!eq) continue;
		*eq = 0;
		int v = atoi(eq + 1);
		char *nl = strpbrk(eq + 1, "\r\n");
		if (nl) *nl = 0;
		if (!strcmp(line, "display.resolution")) ui_set.resolution = (v >= 0 && v < UI_RES_COUNT) ? v : UI_RES_AUTO;
		else if (!strcmp(line, "display.widescreen")) ui_set.widescreen = v ? 1 : 0;
		else if (!strcmp(line, "display.full_width")) ui_set.full_width = v ? 1 : 0;
		else if (!strcmp(line, "display.overscan")) ui_set.overscan = v < 0 ? 0 : v > UI_OVERSCAN_MAX ? UI_OVERSCAN_MAX : v;
		else if (!strcmp(line, "sound.volume")) ui_set.sound_volume = v < 0 ? 0 : v > UI_VOLUME_MAX ? UI_VOLUME_MAX : v;
		else if (!strcmp(line, "intro.play")) ui_set.intro = v ? 1 : 0;
		else if (!strcmp(line, "exit.launcher")) snprintf(ui_set.launcher, sizeof(ui_set.launcher), "%s", eq + 1);
	}
	fclose(f);
	ui_settings_changed();
}

bool ui_settings_save(void) {
	if (!settings_file[0]) return false;
	/* the folder may not exist yet (first run) */
	char dir[300];
	snprintf(dir, sizeof(dir), "%s", settings_file);
	char *slash = strrchr(dir, '/');
	if (slash && slash > strchr(dir, ':') + 1) { *slash = 0; mkdir(dir, 0777); }
	FILE *f = fopen(settings_file, "w");
	if (!f) return false;
	bool ok = fprintf(f, "# GCMM exFAT settings\n") > 0;
	ok = fprintf(f, "display.resolution=%d\n", ui_set.resolution) > 0 && ok;
	ok = fprintf(f, "display.widescreen=%d\n", ui_set.widescreen) > 0 && ok;
	ok = fprintf(f, "display.full_width=%d\n", ui_set.full_width) > 0 && ok;
	ok = fprintf(f, "display.overscan=%d\n", ui_set.overscan) > 0 && ok;
	ok = fprintf(f, "sound.volume=%d\n", ui_set.sound_volume) > 0 && ok;
	ok = fprintf(f, "intro.play=%d\n", ui_set.intro) > 0 && ok;
	ok = fprintf(f, "exit.launcher=%s\n", ui_set.launcher) > 0 && ok;
	ok = fflush(f) == 0 && ok;
	fsync(fileno(f));   /* straight onto the card, not left waiting in a cache */
	ok = fclose(f) == 0 && ok;
	return ok;
}

/* ============================================================ the scene */

#define TXT 96
typedef struct { char label[48]; char value[TXT]; } Pair;
typedef struct { char title[40], detail[40], blocks[16]; int status, checked; bool folder, has_icon; } Row;

static struct {
	int screen;
	char title_info[64];
	char splash[TXT];
	char status1[TXT], status2[TXT];
	char hints[UI_HINTS_MAX][32], saved_hints[UI_HINTS_MAX][32];
	int hint_count, saved_count;
	bool splash_ready;
	float progress;
	/* header */
	char mc_name[32], mc_info[48], mc2_name[32], mc2_info[48], sd_name[32], sd_info[48];
	/* main */
	int main_sel, tools_sel;
	char upd_now[24], upd_new[24], upd_notes[1200];
	char toast[96];
	u32 toast_at;
	unsigned char qr[177 * 177];
	int qr_size;
	char tr_addr[48], tr_state[96];
	struct { char name[48], detail[64]; float progress; } tr_rows[UI_TRANSFER_ROWS];
	/* list */
	char list_title[TXT], list_right[48];
	bool checkable;
	Row rows[UI_LIST_ROWS];
	int row_used, list_sel;
	int scroll_first, scroll_count, scroll_total;
	/* overlay */
	int overlay;
	char overlay_title[48];
	Pair orows[UI_OVERLAY_ROWS];
	int orow_count, osel;
	int oscroll_from, oscroll_first, oscroll_count, oscroll_total;   /* (a scrolling overlay list: see ui_overlay_scroll) */
	int orow_icon[UI_OVERLAY_ROWS];
} sc;

static mutex_t lock = LWP_MUTEX_NULL;
static void L(void) { LWP_MutexLock(lock); }
static void U(void) { LWP_MutexUnlock(lock); }
static void cpy(char *dst, const char *src, size_t n) { snprintf(dst, n, "%s", src ? src : ""); }

static u32 shown_at;   /* when the current screen appeared (ms), for fades */
static u32 now_ms(void) { return ticks_to_millisecs(gettime()); }

void ui_screen(int s) { L(); if (sc.screen != s) shown_at = now_ms(); sc.screen = s; U(); }
void ui_title_info(const char *t) { L(); cpy(sc.title_info, t, sizeof(sc.title_info)); U(); }
void ui_splash_text(const char *t) { L(); cpy(sc.splash, t, sizeof(sc.splash)); U(); }
void ui_status(const char *a, const char *b) { L(); cpy(sc.status1, a, TXT); cpy(sc.status2, b, TXT); U(); }
void ui_progress(float f) { L(); sc.progress = f; U(); }
void ui_hints(const char *const *h, int n) {
	L();
	if (n > UI_HINTS_MAX) n = UI_HINTS_MAX;
	for (int i = 0; i < n; i++) cpy(sc.hints[i], h[i], sizeof(sc.hints[i]));
	sc.hint_count = n;
	U();
}
void ui_hints_push(void) { L(); memcpy(sc.saved_hints, sc.hints, sizeof(sc.hints)); sc.saved_count = sc.hint_count; U(); }
void ui_hints_pop(void) { L(); memcpy(sc.hints, sc.saved_hints, sizeof(sc.hints)); sc.hint_count = sc.saved_count; U(); }
int ui_get_screen(void) { return sc.screen; }
void ui_splash_ready(void) { L(); sc.splash_ready = true; shown_at = now_ms(); U(); }
void ui_main_cards(const char *mn, const char *mi, const char *sn, const char *si) {
	L();
	cpy(sc.mc_name, mn, sizeof(sc.mc_name)); cpy(sc.mc_info, mi, sizeof(sc.mc_info));
	cpy(sc.sd_name, sn, sizeof(sc.sd_name)); cpy(sc.sd_info, si, sizeof(sc.sd_info));
	U();
}
void ui_main_card2(const char *mn, const char *mi) {
	L(); cpy(sc.mc2_name, mn, sizeof(sc.mc2_name)); cpy(sc.mc2_info, mi, sizeof(sc.mc2_info)); U();
}
void ui_update_page(const char *a, const char *b, const char *n) {
	L(); cpy(sc.upd_now, a, sizeof(sc.upd_now)); cpy(sc.upd_new, b, sizeof(sc.upd_new)); cpy(sc.upd_notes, n, sizeof(sc.upd_notes)); U();
}
void ui_toast(const char *m) { L(); cpy(sc.toast, m, sizeof(sc.toast)); sc.toast_at = now_ms(); U(); }
void ui_transfer_page(const unsigned char *qr, int size, const char *addr, const char *state) {
	L();
	if (size > 177) size = 0;
	sc.qr_size = size;
	if (size) memcpy(sc.qr, qr, size * size);
	cpy(sc.tr_addr, addr, sizeof(sc.tr_addr));
	cpy(sc.tr_state, state, sizeof(sc.tr_state));
	U();
}
void ui_transfer_row(int r, const char *n, const char *d, float p) {
	if (r < 0 || r >= UI_TRANSFER_ROWS) return;
	L(); cpy(sc.tr_rows[r].name, n, sizeof(sc.tr_rows[r].name)); cpy(sc.tr_rows[r].detail, d, sizeof(sc.tr_rows[r].detail)); sc.tr_rows[r].progress = p; U();
}
void ui_tools_select(int r) { L(); sc.tools_sel = r; U(); }
void ui_main_select(int r) { L(); sc.main_sel = r; U(); }
void ui_list_header(const char *t, const char *r) { L(); cpy(sc.list_title, t, TXT); cpy(sc.list_right, r, sizeof(sc.list_right)); U(); }
void ui_list_clear(void) { L(); sc.row_used = 0; sc.list_sel = -1; memset(sc.rows, 0, sizeof(sc.rows)); U(); }
void ui_list_checkable(bool on) { L(); sc.checkable = on; U(); }
void ui_list_row(int r, const char *title, const char *detail, const char *blocks, int status, int checked, bool folder) {
	if (r < 0 || r >= UI_LIST_ROWS) return;
	L();
	Row *w = &sc.rows[r];
	cpy(w->title, title, sizeof(w->title)); cpy(w->detail, detail, sizeof(w->detail)); cpy(w->blocks, blocks, sizeof(w->blocks));
	w->status = status; w->checked = checked; w->folder = folder;
	if (r + 1 > sc.row_used) sc.row_used = r + 1;
	U();
}
void ui_list_select(int r) { L(); sc.list_sel = r; U(); }
void ui_list_scroll(int f, int c, int t) { L(); sc.scroll_first = f; sc.scroll_count = c; sc.scroll_total = t; U(); }
void ui_overlay(int o, const char *t) { L(); sc.overlay = o; cpy(sc.overlay_title, t, sizeof(sc.overlay_title)); sc.orow_count = 0; sc.osel = 0; sc.oscroll_total = 0; memset(sc.orow_icon, 0, sizeof(sc.orow_icon)); U(); }
void ui_overlay_row(int r, const char *l, const char *v) {
	if (r < 0 || r >= UI_OVERLAY_ROWS) return;
	L(); cpy(sc.orows[r].label, l, sizeof(sc.orows[r].label)); cpy(sc.orows[r].value, v, TXT); if (!v) sc.orows[r].value[0] = 1; U();
}
void ui_overlay_select(int r) { L(); sc.osel = r; U(); }
void ui_overlay_row_icon(int r, int icon) { if (r >= 0 && r < UI_OVERLAY_ROWS) { L(); sc.orow_icon[r] = icon; U(); } }
void ui_overlay_rows(int n) { L(); sc.orow_count = n; U(); }
void ui_overlay_scroll(int from, int first, int count, int total) { L(); sc.oscroll_from = from; sc.oscroll_first = first; sc.oscroll_count = count; sc.oscroll_total = total; U(); }

/* ------------------------------------------------------- the rows' icons */

/* RGBA8 textures, written by the memory card code and drawn by the thread */
static u8 icon_tex[UI_LIST_ROWS][32 * 32 * 4] __attribute__((aligned(32)));

static void bgr_to_rgba8(const u8 *bgr, int w, int h, u8 *out) {
	u8 *p = out;
	for (int ty = 0; ty < h; ty += 4)
		for (int tx = 0; tx < w; tx += 4) {
			for (int y = 0; y < 4; y++)
				for (int x = 0; x < 4; x++) {
					const u8 *s = bgr + ((ty + y) * w + tx + x) * 3;
					*p++ = 255; *p++ = s[2];   /* alpha, red */
				}
			for (int y = 0; y < 4; y++)
				for (int x = 0; x < 4; x++) {
					const u8 *s = bgr + ((ty + y) * w + tx + x) * 3;
					*p++ = s[1]; *p++ = s[0];  /* green, blue */
				}
		}
	DCFlushRange(out, w * h * 4);
}

void ui_list_icon(int r, const u8 *bgr) {
	if (r < 0 || r >= UI_LIST_ROWS) return;
	L();
	if (bgr) { bgr_to_rgba8(bgr, 32, 32, icon_tex[r]); sc.rows[r].has_icon = true; }
	else sc.rows[r].has_icon = false;
	U();
}

/* ================================================================== video */

#define FIFO_SIZE (256 * 1024)
static void *fifo;
static u32 *xfb[2];
static int fb;
static GXRModeObj mode;
static GXRModeObj *rmode = &mode;
static volatile bool video_change, running;
static lwp_t thread = LWP_THREAD_NULL;

GXRModeObj *ui_video_mode(void) { return rmode; }

/* The GPU draws 640 pixels across. Full width stretches that to 704, the
 * whole width of the TV picture (as games do); Standard leaves it at 640. */
static void pick_mode(void) {
	bool pal = VIDEO_GetCurrentTvMode() == VI_PAL;
	GXRModeObj *base;
	if (ui_set.resolution == UI_RES_480P && VIDEO_HaveComponentCable())
		base = pal ? &TVEurgb60Hz480Prog : &TVNtsc480Prog;
	else if (ui_set.resolution == UI_RES_480I)
		base = pal ? &TVPal528IntDf : &TVNtsc480IntDf;
	else if (VIDEO_HaveComponentCable())
		base = pal ? &TVEurgb60Hz480Prog : &TVNtsc480Prog;
	else
		base = VIDEO_GetPreferredMode(NULL);
	mode = *base;
	if (ui_set.full_width) {
		mode.viWidth = 704;
		mode.viXOrigin = (720 - 704) / 2;
	} else {
		mode.viWidth = 640;
		mode.viXOrigin = (720 - 640) / 2;
	}
}

static void setup_gx(void) {
	f32 yscale = GX_GetYScaleFactor(rmode->efbHeight, rmode->xfbHeight);
	u32 xfb_h = GX_SetDispCopyYScale(yscale);
	GX_SetViewport(0, 0, rmode->fbWidth, rmode->efbHeight, 0, 1);
	GX_SetScissor(0, 0, rmode->fbWidth, rmode->efbHeight);
	GX_SetDispCopySrc(0, 0, rmode->fbWidth, rmode->efbHeight);
	GX_SetDispCopyDst(rmode->fbWidth, xfb_h);
	GX_SetCopyFilter(rmode->aa, rmode->sample_pattern, GX_TRUE, rmode->vfilter);
	GX_SetFieldMode(rmode->field_rendering, (rmode->viHeight == 2 * rmode->xfbHeight) ? GX_ENABLE : GX_DISABLE);
	GX_SetPixelFmt(rmode->aa ? GX_PF_RGB565_Z16 : GX_PF_RGB8_Z24, GX_ZC_LINEAR);
	GX_SetDispCopyGamma(GX_GM_1_0);
}

static void configure_vi(void) {
	VIDEO_Configure(rmode);
	VIDEO_ClearFrameBuffer(rmode, xfb[0], COLOR_BLACK);
	VIDEO_ClearFrameBuffer(rmode, xfb[1], COLOR_BLACK);
	VIDEO_SetNextFramebuffer(xfb[0]);
	VIDEO_SetBlack(FALSE);
	VIDEO_Flush();
	VIDEO_WaitVSync();
	if (rmode->viTVMode & VI_NON_INTERLACE) VIDEO_WaitVSync();
	fb = 1;
}

void ui_settings_changed(void) { video_change = true; }

/* ============================================================== drawing */

typedef struct { u8 r, g, b, a; } Color;
static inline Color rgba(u8 r, u8 g, u8 b, u8 a) { return (Color){ r, g, b, a }; }
static inline Color alpha(Color c, float k) { if (k < 0) k = 0; if (k > 1) k = 1; c.a = (u8)(c.a * k); return c; }
static inline float clamp01(float t) { return t < 0 ? 0 : t > 1 ? 1 : t; }
static Color mix(Color a, Color b, float t) {
	t = clamp01(t);
	return rgba(a.r + (b.r - a.r) * t, a.g + (b.g - a.g) * t, a.b + (b.b - a.b) * t, a.a + (b.a - a.a) * t);
}

static const Color TEXT = { 238, 241, 246, 255 }, SOFT = { 201, 210, 238, 255 }, TITLE = { 115, 146, 255, 255 };
static const Color GOLD = { 232, 207, 74, 255 }, GOLD_HI = { 255, 244, 150, 255 }, DIM = { 124, 134, 146, 255 };

/* the visible part of the layout */
static float vl, vt, vr, vb;
static void view_bounds(void) {
	float w = ui_set.widescreen ? 640.0f * 4.0f / 3.0f : 640.0f, h = 480.0f;
	float k = 1.0f - ui_set.overscan / 100.0f;
	w /= k; h /= k;
	vl = 320 - w / 2; vr = 320 + w / 2; vt = 240 - h / 2; vb = 240 + h / 2;
}
static float pixel_w(void) { return (vr - vl) / rmode->fbWidth; }
static float pixel_h(void) { return (vb - vt) / rmode->efbHeight; }

enum { M_NONE, M_COLOR, M_TEX, M_ALPHA };
static int cur_mode = M_NONE;
static void mode_color(void) {
	if (cur_mode == M_COLOR) return;
	cur_mode = M_COLOR;
	GX_SetNumTexGens(0);
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORDNULL, GX_TEXMAP_NULL, GX_COLOR0A0);
	GX_SetTevOp(GX_TEVSTAGE0, GX_PASSCLR);
}
static void mode_tex(GXTexObj *t, bool alpha_only) {
	GX_LoadTexObj(t, GX_TEXMAP0);
	int m = alpha_only ? M_ALPHA : M_TEX;
	if (cur_mode == m) return;
	cur_mode = m;
	GX_SetNumTexGens(1);
	GX_SetTexCoordGen(GX_TEXCOORD0, GX_TG_MTX2x4, GX_TG_TEX0, GX_IDENTITY);
	GX_SetTevOrder(GX_TEVSTAGE0, GX_TEXCOORD0, GX_TEXMAP0, GX_COLOR0A0);
	if (alpha_only) {
		/* text: the vertex colour, with the font's coverage as alpha */
		GX_SetTevColorIn(GX_TEVSTAGE0, GX_CC_ZERO, GX_CC_ZERO, GX_CC_ZERO, GX_CC_RASC);
		GX_SetTevColorOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
		GX_SetTevAlphaIn(GX_TEVSTAGE0, GX_CA_ZERO, GX_CA_TEXA, GX_CA_RASA, GX_CA_ZERO);
		GX_SetTevAlphaOp(GX_TEVSTAGE0, GX_TEV_ADD, GX_TB_ZERO, GX_CS_SCALE_1, GX_TRUE, GX_TEVPREV);
	} else {
		GX_SetTevOp(GX_TEVSTAGE0, GX_MODULATE);
	}
}

static inline void vtx(float x, float y, Color c, float s, float t) {
	GX_Position2f32(x, y);
	GX_Color4u8(c.r, c.g, c.b, c.a);
	GX_TexCoord2f32(s, t);
}

static void rect_v(float x, float y, float w, float h, Color top, Color bot) {
	if (!(w > 0) || !(h > 0)) return;
	mode_color();
	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
	vtx(x, y, top, 0, 0); vtx(x + w, y, top, 0, 0); vtx(x + w, y + h, bot, 0, 0); vtx(x, y + h, bot, 0, 0);
	GX_End();
}
static void rect_h(float x, float y, float w, float h, Color l, Color r) {
	if (!(w > 0) || !(h > 0)) return;
	mode_color();
	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
	vtx(x, y, l, 0, 0); vtx(x + w, y, r, 0, 0); vtx(x + w, y + h, r, 0, 0); vtx(x, y + h, l, 0, 0);
	GX_End();
}
static void rect(float x, float y, float w, float h, Color c) { rect_v(x, y, w, h, c, c); }
static void tri(float x0, float y0, float x1, float y1, float x2, float y2, Color c) {
	mode_color();
	GX_Begin(GX_TRIANGLES, GX_VTXFMT0, 3);
	vtx(x0, y0, c, 0, 0); vtx(x1, y1, c, 0, 0); vtx(x2, y2, c, 0, 0);
	GX_End();
}
static void circle(float cx, float cy, float r, Color c) {
	mode_color();
	GX_Begin(GX_TRIANGLEFAN, GX_VTXFMT0, 26);
	vtx(cx, cy, c, 0, 0);
	for (int i = 0; i <= 24; i++) {
		float a = 6.2831853f * i / 24;
		vtx(cx + cosf(a) * r, cy + sinf(a) * r, c, 0, 0);
	}
	GX_End();
}

#define RR_SEG 4
#define RR_PTS (4 * (RR_SEG + 1))
static int rr_points(float x, float y, float w, float h, float r, float *px, float *py) {
	if (r > w / 2) r = w / 2;
	if (r > h / 2) r = h / 2;
	if (r < 0) r = 0;
	const float cx[4] = { x + w - r, x + w - r, x + r, x + r }, cy[4] = { y + r, y + h - r, y + h - r, y + r };
	int n = 0;
	for (int c = 0; c < 4; c++) {
		float a0 = (-90.0f + 90.0f * c) * 3.14159265f / 180.0f;
		for (int i = 0; i <= RR_SEG; i++) {
			float a = a0 + 3.14159265f * 0.5f * i / RR_SEG;
			px[n] = cx[c] + cosf(a) * r; py[n] = cy[c] + sinf(a) * r; n++;
		}
	}
	return n;
}
static void rrect_v(float x, float y, float w, float h, float r, Color top, Color bot) {
	if (!(w > 0) || !(h > 0)) return;
	float px[RR_PTS], py[RR_PTS];
	int n = rr_points(x, y, w, h, r, px, py);
	mode_color();
	GX_Begin(GX_TRIANGLEFAN, GX_VTXFMT0, n + 2);
	vtx(x + w / 2, y + h / 2, mix(top, bot, 0.5f), 0, 0);
	for (int i = 0; i <= n; i++) { int k = i % n; vtx(px[k], py[k], mix(top, bot, (py[k] - y) / h), 0, 0); }
	GX_End();
}
static void outline(float x, float y, float w, float h, float r, float t, Color c) {
	if (!(w > 0) || !(h > 0)) return;
	float ux = pixel_w() * 1.02f, uy = pixel_h() * 1.02f;   /* never thinner than a pixel */
	float tx = t < ux ? ux : t, ty = t < uy ? uy : t;
	if (2 * tx >= w || 2 * ty >= h) { rrect_v(x, y, w, h, r, c, c); return; }
	float ox[RR_PTS], oy[RR_PTS], ix[RR_PTS], iy[RR_PTS];
	int n = rr_points(x, y, w, h, r, ox, oy);
	float ri = r - (tx > ty ? tx : ty);
	rr_points(x + tx, y + ty, w - 2 * tx, h - 2 * ty, ri > 0 ? ri : 0, ix, iy);
	mode_color();
	GX_Begin(GX_TRIANGLESTRIP, GX_VTXFMT0, (n + 1) * 2);
	for (int i = 0; i <= n; i++) { int k = i % n; vtx(ox[k], oy[k], c, 0, 0); vtx(ix[k], iy[k], c, 0, 0); }
	GX_End();
}
static void shadow(float x, float y, float w, float h, float r, float spread, Color c) {
	for (int i = 4; i >= 1; i--) {
		float s = spread * i / 4;
		Color k = alpha(c, 0.28f);
		rrect_v(x - s, y - s, w + 2 * s, h + 2 * s, r + s, k, k);
	}
}

/* ---------------------------------------------------------- textures */

static GXTexObj tex_font_small, tex_font_large, tex_logo_title, tex_logo_splash, tex_card_mc, tex_card_sd, tex_card_file, tex_icon[UI_LIST_ROWS];
static GXTexObj tex_card_cheese, tex_intro_card, tex_intro_words;
static bool cheese;   /* the easter egg */

static void tex_init(GXTexObj *t, const void *data, int w, int h, u8 fmt) {
	DCFlushRange((void *)data, GX_GetTexBufferSize(w, h, fmt, GX_FALSE, 0));
	GX_InitTexObj(t, (void *)data, w, h, fmt, GX_CLAMP, GX_CLAMP, GX_FALSE);
	GX_InitTexObjLOD(t, GX_LINEAR, GX_LINEAR, 0, 0, 0, GX_FALSE, GX_FALSE, GX_ANISO_1);
}

static void image(GXTexObj *t, float x, float y, float w, float h, float tw, float th, Color c) {
	mode_tex(t, false);
	float s1 = w > 0 ? 1.0f : 0, t1 = 1.0f;
	(void)tw; (void)th;
	GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
	vtx(x, y, c, 0, 0); vtx(x + w, y, c, s1, 0); vtx(x + w, y + h, c, s1, t1); vtx(x, y + h, c, 0, t1);
	GX_End();
}
/* ---------------------------------------------------------------- text */

static const UiFont *font_for(float size, GXTexObj **t) {
	if (size <= 16) { *t = &tex_font_small; return &font_small; }
	*t = &tex_font_large; return &font_large;
}

static float text_width(const char *s, float size) {
	GXTexObj *t;
	const UiFont *f = font_for(size, &t);
	float k = size / f->px, w = 0;
	for (; *s; s++) w += f->glyphs[(u8)*s].adv16 / 16.0f * k;
	return w;
}

/* Text, vertically centred on y. align: 0 left, 1 centre, 2 right. max_w > 0
 * shortens it with an ellipsis to fit. A soft dark drop sits under it. */
static void text(float x, float y, const char *s, float size, Color c, int align, float max_w) {
	if (!s || !*s) return;
	char buf[160];
	snprintf(buf, sizeof(buf), "%s", s);
	if (max_w > 0 && text_width(buf, size) > max_w) {
		size_t n = strlen(buf);
		while (n > 0) {
			buf[--n] = 0;
			char tmp[164];
			snprintf(tmp, sizeof(tmp), "%s\x85", buf);
			if (text_width(tmp, size) <= max_w) { snprintf(buf, sizeof(buf), "%s", tmp); break; }
		}
	}
	GXTexObj *t;
	const UiFont *f = font_for(size, &t);
	float k = size / f->px;
	float w = text_width(buf, size);
	float px = align == 1 ? x - w / 2 : align == 2 ? x - w : x;
	/* the baseline, so that capitals sit centred on y */
	float base = y + (f->ascent - f->descent) * 0.5f * k * 0.9f;
	mode_tex(t, true);
	for (int pass = 0; pass < 2; pass++) {
		Color col = pass ? c : rgba(0, 0, 0, (u8)(c.a * 0.55f));
		float off = pass ? 0 : 1.0f;
		float pen = px;
		for (const char *p = buf; *p; p++) {
			const UiGlyph *g = &f->glyphs[(u8)*p];
			if (g->w && g->h && *p != ' ') {
				float gx = pen + g->left * k + off, gy = base + g->top * k + off;
				float s0 = (float)g->x / f->w, t0 = (float)g->y / f->h, s1 = (float)(g->x + g->w) / f->w, t1 = (float)(g->y + g->h) / f->h;
				GX_Begin(GX_QUADS, GX_VTXFMT0, 4);
				vtx(gx, gy, col, s0, t0); vtx(gx + g->w * k, gy, col, s1, t0);
				vtx(gx + g->w * k, gy + g->h * k, col, s1, t1); vtx(gx, gy + g->h * k, col, s0, t1);
				GX_End();
			}
			pen += g->adv16 / 16.0f * k;
		}
	}
}

/* --------------------------------------------------------- pieces of the look */

static void panel(float x, float y, float w, float h) {
	shadow(x, y + 6, w, h, 6, 8, rgba(0, 0, 0, 120));
	outline(x - 3, y - 3, w + 6, h + 6, 6, 2, rgba(200, 208, 220, 140));
	outline(x - 1, y - 1, w + 2, h + 2, 5, 1, rgba(10, 13, 16, 255));
	rrect_v(x, y, w, h, 4, rgba(26, 46, 50, 240), rgba(14, 22, 26, 247));
	float h1 = h * 0.35f;
	rect_v(x, y + 3, w, h1, rgba(30, 54, 57, 0), rgba(40, 72, 74, 150));
	rect_v(x, y + 3 + h1, w, h * 0.4f, rgba(40, 72, 74, 150), rgba(30, 54, 57, 0));
	float e = fminf(14, fminf(w, h) * 0.3f);
	rect_h(x, y + 3, e, h - 6, rgba(0, 0, 0, 70), rgba(0, 0, 0, 0));
	rect_h(x + w - e, y + 3, e, h - 6, rgba(0, 0, 0, 0), rgba(0, 0, 0, 70));
	rect(x + 4, y + 1, w - 8, 1, rgba(255, 255, 255, 46));
	outline(x, y, w, h, 4, 1, rgba(255, 255, 255, 30));
}

static void frost(float x, float y, float w, float h) {
	rect_v(x, y, w, h, rgba(76, 94, 114, 205), rgba(40, 54, 70, 215));
	rect(x + 1, y + 1, w - 2, 1, rgba(255, 255, 255, 40));
	outline(x, y, w, h, 2, 1, rgba(215, 225, 240, 153));
}

static void gold_ring(float x, float y, float w, float h) {
	outline(x - 3, y - 3, w + 6, h + 6, 0, 3, GOLD);
	outline(x, y, w, h, 0, 1, rgba(106, 90, 16, 200));
}

static void desc_bar(float x, float y, float w, float h) {
	outline(x - 1, y - 1, w + 2, h + 2, 4, 1, rgba(11, 11, 32, 255));
	rrect_v(x, y, w, h, 3, rgba(42, 34, 140, 255), rgba(59, 44, 182, 255));
	outline(x, y, w, h, 3, 1, rgba(175, 175, 255, 153));
}

static void rule(float x, float y, float w) { rect(x, y, w, 1, rgba(200, 210, 230, 70)); }

/* ------------------------------------------------------- the background
 * As Flashcube's: near black, a 24px grid of 2px lines lined up on the
 * middle of the screen, and a soft dark vignette towards the edges. */

static void mesh(int cols, int rows, const float *xy, const Color *c) {
	mode_color();
	for (int r = 0; r < rows; r++) {
		GX_Begin(GX_TRIANGLESTRIP, GX_VTXFMT0, (cols + 1) * 2);
		for (int k = 0; k <= cols; k++)
			for (int d = 0; d < 2; d++) {
				int i = (r + d) * (cols + 1) + k;
				vtx(xy[i * 2], xy[i * 2 + 1], c[i], 0, 0);
			}
		GX_End();
	}
}

static float bd_k = 1;   /* how much of the backdrop shows (the opening fades it in) */
static void backdrop(void) {
	float fx = vl, fy = vt, fw = vr - vl, fh = vb - vt;
	rect(fx, fy, fw, fh, rgba(6, 6, 8, (u8)(255 * bd_k)));
	Color line = rgba(0x25, 0x25, 0x2b, (u8)(255 * bd_k));
	float cx = 320, cy = 240;
	float x0 = cx - 24 * ceilf((cx - fx) / 24), y0 = cy - 24 * ceilf((cy - fy) / 24);
	for (float x = x0; x < fx + fw; x += 24) rect(x, fy, 2, fh, line);
	for (float y = y0; y < fy + fh; y += 24) rect(fx, y, fw, 2, line);
	enum { C = 12, R = 10 };
	static float xy[(C + 1) * (R + 1) * 2];
	static Color col[(C + 1) * (R + 1)];
	for (int j = 0; j <= R; j++)
		for (int i = 0; i <= C; i++) {
			int k = j * (C + 1) + i;
			float x = fx + fw * i / C, y = fy + fh * j / R;
			xy[k * 2] = x; xy[k * 2 + 1] = y;
			float dx = (x - (fx + fw / 2)) / (fw / 2 * 1.414f), dy = (y - (fy + fh / 2)) / (fh / 2 * 1.414f);
			float d = sqrtf(dx * dx + dy * dy);
			float v = d < 0.4f ? 0 : (d - 0.4f) / 0.6f * 0.6f;
			col[k] = rgba(0, 0, 0, (u8)(clamp01(v) * 255 * bd_k));
		}
	mesh(C, R, xy, col);
}

/* =============================================================== screens */

/* the header: the logo, and the memory card and SD card at the right */
static float chip(float right, float y, GXTexObj *pic, float pw, float ph, const char *name, const char *info, bool on) {
	float w = 40 + fmaxf(text_width(name, 11), text_width(info, 10));
	float most = sc.mc2_name[0] ? 150 : 200;   /* three chips: a little narrower */
	if (w > most) w = most;
	float x = right - w;
	rrect_v(x, y, w, 32, 6, rgba(10, 14, 20, 170), rgba(10, 14, 20, 170));
	outline(x, y, w, 32, 6, 1, rgba(200, 210, 230, 70));
	image(pic, x + 8, y + 16 - ph / 2, pw, ph, 0, 0, rgba(255, 255, 255, on ? 255 : 110));
	text(x + 30, y + 10, name, 11, on ? TEXT : DIM, 0, w - 36);
	text(x + 30, y + 23, info, 10, SOFT, 0, w - 36);
	return w;
}

static void header(void) {
	image(&tex_logo_title, 22, 32 - LOGO_TITLE_H / 2, LOGO_TITLE_W, LOGO_TITLE_H, 0, 0, rgba(255, 255, 255, 255));   /* centred on the chips */
	/* from the right: the SD card, then the memory cards (B, then A to its left) */
	float x = 618;
	x -= chip(x, 16, &tex_card_sd, 14, 18, sc.sd_name[0] ? sc.sd_name : "No SD card", sc.sd_info, sc.sd_name[0] != 0) + 8;
	if (sc.mc2_name[0]) x -= chip(x, 16, &tex_card_mc, 14, 18, sc.mc2_name, sc.mc2_info, true) + 8;
	chip(x, 16, &tex_card_mc, 14, 18, sc.mc_name, sc.mc_info, sc.mc_info[0] != 0 && strncmp(sc.mc_info, "No ", 3));
}

/* the description bar along the bottom, with a progress bar at its right when there is one */
static void status_bar(void) {
	desc_bar(22, 374, 596, 52);
	float tw = 572;
	if (sc.progress >= 0) {
		float bx = 418, bw = 184, by = 397;
		rrect_v(bx, by, bw, 8, 3, rgba(0, 0, 0, 140), rgba(0, 0, 0, 140));
		float f = clamp01(sc.progress);
		if (f > 0) rrect_v(bx, by, bw * f, 8, 3, rgba(255, 232, 138, 255), rgba(216, 176, 48, 255));
		tw = 370;
	}
	text(38, 391, sc.status1, 13, TEXT, 0, tw);
	text(38, 410, sc.status2, 11, rgba(200, 205, 240, 255), 0, tw);
}

/* ------------------------------------------------- the controls (hints)
 * As CubeVerse draws them: a dark pill for each button (A green, B red, Z
 * purple), the Control Stick as a little stick, three dots, then what it does. */
#define HINT_Y 452

static void hint_stick(float x, float cy, float d) {
	float r = d / 2, cx = x + r;
	circle(cx, cy + 1, r + 1, rgba(0, 0, 0, 150));
	circle(cx, cy, r, rgba(0x4a, 0x4a, 0x52, 255));
	circle(cx, cy, r * 0.78f, rgba(0x2a, 0x2a, 0x30, 255));
	float nr = d * 0.30f;
	circle(cx, cy + 1, nr, rgba(0, 0, 0, 128));
	for (int i = 4; i >= 0; i--)
		circle(cx, cy, nr * (i + 1) / 5.0f, (i & 1) ? rgba(0x9a, 0x9b, 0xa6, 255) : rgba(0xd6, 0xd7, 0xde, 255));
}

static float hint_one(const char *h, float x, float cy, bool draw) {
	char key[8] = "";
	const char *colon = strchr(h, ':');
	const char *label = colon ? colon + 1 : h;
	if (colon && colon - h < 7) { memcpy(key, h, colon - h); key[colon - h] = 0; }
	float w = 0;
	if (!strcmp(key, "S")) {
		if (draw) hint_stick(x, cy, 20);
		w = 20;
	} else if (key[0]) {
		const char *t = !strcmp(key, "ST") ? "START" : key;
		Color tc = rgba(0xd8, 0xda, 0xe6, 255), ring = rgba(255, 255, 255, 36);
		bool glow = false;
		if (!strcmp(key, "A")) { tc = rgba(0x5f, 0xe3, 0x9a, 255); ring = rgba(95, 227, 154, 97); glow = true; }
		if (!strcmp(key, "B")) { tc = rgba(0xff, 0x5c, 0x7c, 255); ring = rgba(255, 92, 124, 97); glow = true; }
		if (!strcmp(key, "Z")) { tc = rgba(0xb9, 0xa2, 0xff, 255); ring = rgba(185, 162, 255, 97); glow = true; }
		float pw = text_width(t, 11) + 12;
		if (pw < 20) pw = 20;
		if (draw) {
			float y = cy - 10;
			if (glow) rrect_v(x - 2, y - 2, pw + 4, 24, 12, alpha(ring, 0.45f), alpha(ring, 0.45f));
			rrect_v(x, y, pw, 20, 10, rgba(0x3c, 0x3c, 0x46, 255), rgba(0x08, 0x08, 0x0a, 255));
			outline(x, y, pw, 20, 10, 1, ring);
			text(x + pw / 2, cy, t, 11, tc, 1, 0);
		}
		w = pw;
	}
	if (key[0]) {
		if (draw) for (int i = 0; i < 3; i++) circle(x + w + 4 + i * 4, cy + 1, 1.1f, rgba(0xc9, 0xcb, 0xd6, 255));
		w += 16;
	}
	if (draw) text(x + w, cy, label, 12, rgba(0xdf, 0xe4, 0xff, 255), 0, 0);
	return w + text_width(label, 12);
}

static void hints_row(void) {
	if (sc.hint_count <= 0) return;
	float total = 0, gap = 30;
	for (int i = 0; i < sc.hint_count; i++) total += hint_one(sc.hints[i], 0, 0, false);
	if (total + gap * (sc.hint_count - 1) > 580) gap = fmaxf(10, (580 - total) / (sc.hint_count - 1));
	float x = 320 - (total + gap * (sc.hint_count - 1)) / 2;
	for (int i = 0; i < sc.hint_count; i++) x += hint_one(sc.hints[i], x, HINT_Y, true) + gap;
}

/* the main screen's pictures */
static void card_pair(float cx, float cy, bool to_sd) {
	GXTexObj *a = to_sd ? &tex_card_mc : &tex_card_sd, *b = to_sd ? &tex_card_sd : &tex_card_mc;
	image(a, cx - 64, cy - 30, 44, 58, 0, 0, rgba(255, 255, 255, 255));
	image(b, cx + 20, cy - 30, 44, 58, 0, 0, rgba(255, 255, 255, 255));
	tri(cx + 12, cy - 1, cx + 2, cy - 9, cx + 2, cy + 7, GOLD);
	rect(cx - 12, cy - 4, 15, 6, GOLD);
}
/* a gear: eight tapered teeth pointing out from the middle, a thin dark
 * edge so it reads against any panel, and a hole in the middle */
static void gear_shape(float cx, float cy, float r, Color c) {
	for (int i = 0; i < 8; i++) {
		float a = i * 3.14159265f / 4 + 3.14159265f / 8;
		float ri = r * 0.70f, ro = r, wi = 0.30f, wo = 0.19f;
		float x0 = cx + cosf(a - wi) * ri, y0 = cy + sinf(a - wi) * ri;
		float x1 = cx + cosf(a + wi) * ri, y1 = cy + sinf(a + wi) * ri;
		float x2 = cx + cosf(a + wo) * ro, y2 = cy + sinf(a + wo) * ro;
		float x3 = cx + cosf(a - wo) * ro, y3 = cy + sinf(a - wo) * ro;
		tri(x0, y0, x1, y1, x2, y2, c);
		tri(x0, y0, x2, y2, x3, y3, c);
	}
	circle(cx, cy, r * 0.76f, c);
}
static void gear(float cx, float cy, float r, Color c, Color hole) {
	gear_shape(cx, cy + 1, r + 1.2f, rgba(10, 12, 16, 200));   /* the dark edge, a touch lower */
	gear_shape(cx, cy, r, c);
	circle(cx, cy, r * 0.62f, rgba(c.r * 0.88f, c.g * 0.88f, c.b * 0.88f, 255));   /* a slight step on the face */
	circle(cx, cy, r * 0.30f, hole);
}

static void main_screen(void) {
	text(320, 86, "What would you like to do?", 17, TEXT, 1, 0);
	static const char *names[3] = { "Back up", "Restore", "Card tools" };
	static const char *desc[3][2] = { { "Copy saves from the memory", "card to the SD card" },
	                                  { "Put saves from the SD card", "back on the memory card" },
	                                  { "Whole-card backups,", "format, delete saves" } };
	for (int i = 0; i < 3; i++) {
		float x = 34 + i * 194, y = 116, w = 180, h = 236, cx = x + w / 2, cy = y + 82;
		panel(x, y, w, h);
		if (i < 2) card_pair(cx, cy, i == 0);
		else {
			image(&tex_card_mc, cx - 40, cy - 32, 44, 58, 0, 0, rgba(255, 255, 255, 255));
			gear(cx + 18, cy + 12, 18, rgba(214, 218, 226, 255), rgba(36, 46, 56, 255));
		}
		text(cx, y + 150, names[i], 20, TEXT, 1, w - 16);
		text(cx, y + 174, desc[i][0], 11, SOFT, 1, w - 16);
		text(cx, y + 190, desc[i][1], 11, SOFT, 1, w - 16);
		if (sc.main_sel == i && sc.overlay == UI_OVERLAY_NONE) gold_ring(x, y, w, h);
	}
}

/* a thick arc: part of a ring from angle a0 to a1 (radians, 0 = right, down is positive) */
static void arc(float cx, float cy, float r, float th, float a0, float a1, Color c) {
	int n = 12;
	for (int i = 0; i < n; i++) {
		float u0 = a0 + (a1 - a0) * i / n, u1 = a0 + (a1 - a0) * (i + 1) / n;
		float ri = r - th / 2, ro = r + th / 2;
		float x0 = cx + cosf(u0) * ri, y0 = cy + sinf(u0) * ri, x1 = cx + cosf(u1) * ri, y1 = cy + sinf(u1) * ri;
		float x2 = cx + cosf(u1) * ro, y2 = cy + sinf(u1) * ro, x3 = cx + cosf(u0) * ro, y3 = cy + sinf(u0) * ro;
		tri(x0, y0, x1, y1, x2, y2, c);
		tri(x0, y0, x2, y2, x3, y3, c);
	}
}
static void line_seg(float x0, float y0, float x1, float y1, float w, Color c) {
	float dx = x1 - x0, dy = y1 - y0, l = sqrtf(dx * dx + dy * dy);
	if (l <= 0) return;
	float nx = -dy / l * w / 2, ny = dx / l * w / 2;
	tri(x0 + nx, y0 + ny, x1 + nx, y1 + ny, x1 - nx, y1 - ny, c);
	tri(x0 + nx, y0 + ny, x1 - nx, y1 - ny, x0 - nx, y0 - ny, c);
}

/* Network transfer's picture: a network cable's plug, to the SD card */
static void net_picture(float cx, float cy) {
	const Color white = rgba(255, 255, 255, 255), lite = rgba(226, 230, 238, 255), dim = rgba(176, 184, 200, 255);
	const Color cable = rgba(90, 160, 230, 255);
	float lx = cx - 18;
	rrect_v(lx - 7, cy - 16, 14, 16, 2, lite, dim);     /* the plug */
	rect(lx - 5, cy - 16, 10, 4, GOLD);                  /* its contacts */
	for (int k = 0; k < 4; k++) rect(lx - 4.5f + k * 2.6f, cy - 16, 1.2f, 4, rgba(150, 120, 30, 255));
	rect(lx - 3, cy - 20, 6, 4, dim);                    /* the latch */
	rect(lx - 4, cy, 8, 4, dim);
	line_seg(lx, cy + 4, lx, cy + 10, 4, cable);         /* the cable, curling away */
	arc(lx + 8, cy + 10, 8, 4, 1.57f, 3.14f, cable);
	tri(cx + 6, cy, cx, cy - 5, cx, cy + 5, GOLD);
	rect(cx - 6, cy - 2, 7, 4, GOLD);
	image(&tex_card_sd, cx + 9, cy - 14, 21, 28, 0, 0, white);
}

/* a virtual memory card (a card image on the SD card): the memory card
 * with a little SD card tucked at its lower right */
static void virtual_card(float x, float y, float w, float h, Color tint) {
	image(cheese ? &tex_card_cheese : &tex_card_mc, x, y, w, h, 0, 0, tint);
	float sw = w * 0.42f, sh = h * 0.42f;
	image(&tex_card_sd, x + w - sw * 0.75f, y + h - sh * 0.95f, sw, sh, 0, 0, tint);
}

/* the Card tools page: a tile for each job, each with a little picture */
static void tool_picture(int t, float cx, float cy) {
	const Color white = rgba(255, 255, 255, 255);
	switch (t) {
	case 0: case 1: {   /* the whole card to the SD card, or back */
		GXTexObj *a = t == 0 ? &tex_card_mc : &tex_card_sd, *b = t == 0 ? &tex_card_sd : &tex_card_mc;
		image(a, cx - 30, cy - 14, 21, 28, 0, 0, white);
		image(b, cx + 9, cy - 14, 21, 28, 0, 0, white);
		tri(cx + 6, cy, cx, cy - 5, cx, cy + 5, GOLD);
		rect(cx - 6, cy - 2, 7, 4, GOLD);
		break;
	}
	case 2:             /* copy between cards: a card to a virtual card */
		image(&tex_card_mc, cx - 30, cy - 14, 21, 28, 0, 0, white);
		virtual_card(cx + 9, cy - 14, 21, 28, white);
		tri(cx + 6, cy, cx, cy - 5, cx, cy + 5, GOLD);
		rect(cx - 6, cy - 2, 7, 4, GOLD);
		break;
	case 3: case 4:     /* delete saves, format: the card with a red mark */
		image(&tex_card_mc, cx - 16, cy - 19, 28, 37, 0, 0, white);
		circle(cx + 12, cy + 10, 10, rgba(10, 12, 16, 230));
		circle(cx + 12, cy + 10, 9, rgba(220, 70, 80, 255));
		if (t == 3) rect(cx + 7, cy + 9, 10, 3, white);              /* minus: take some away */
		else { rect(cx + 11, cy + 4, 3, 8, white); rect(cx + 11, cy + 14, 3, 3, white); }   /* "!": everything goes */
		break;
	case 5:             /* a new virtual card: with a green plus */
		virtual_card(cx - 16, cy - 19, 28, 37, white);
		circle(cx + 12, cy - 10, 10, rgba(10, 12, 16, 230));
		circle(cx + 12, cy - 10, 9, rgba(60, 190, 110, 255));
		rect(cx + 7, cy - 11.5f, 10, 3, white);
		rect(cx + 10.5f, cy - 15, 3, 10, white);
		break;
	case 6:             /* network transfer (see net_picture) */
		net_picture(cx, cy);
		break;
	default:            /* settings */
		gear(cx, cy, 15, rgba(214, 218, 226, 255), rgba(36, 46, 56, 255));
		break;
	}
}

static void tools_screen(void) {
	static const char *names[UI_TOOLS_COUNT] = { "Back up the whole card", "Restore a whole card", "Copy between cards",
	                                             "Delete saves", "Format the card", "New virtual card",
	                                             "Network transfer", "Settings" };
	static const char *desc[UI_TOOLS_COUNT] = { "One file with everything on it", "From a whole-card backup",
	                                            "Card to card, real or virtual", "Choose saves to remove",
	                                            "Erase everything on it", "A blank card on the SD card",
	                                            "From a phone or computer", "Picture, updates, exit" };
	panel(22, 62, 596, 300);
	text(34, 80, "Card tools", 12, TITLE, 0, 380);
	rule(32, 92, 576);
	for (int i = 0; i < UI_TOOLS_COUNT; i++) {
		float x = 34 + (i % 2) * 290, y = 100 + (i / 2) * 63, w = 282, h = 57;
		bool on = i == sc.tools_sel && sc.overlay == UI_OVERLAY_NONE;
		rect_v(x, y, w, h, rgba(76, 94, 114, on ? 150 : 80), rgba(40, 54, 70, on ? 160 : 90));
		rect(x + 1, y + 1, w - 2, 1, rgba(255, 255, 255, 30));
		tool_picture(i, x + 40, y + h / 2);
		text(x + 80, y + 21, names[i], 14, i == 4 ? rgba(255, 170, 170, 255) : TEXT, 0, w - 90);
		text(x + 80, y + 40, desc[i], 11, SOFT, 0, w - 90);
		if (on) gold_ring(x, y, w, h);
	}
}

/* the Network transfer page */
static void transfer_screen(void) {
	panel(22, 62, 596, 300);
	text(34, 80, "Network transfer", 12, TITLE, 0, 380);
	rule(32, 92, 576);
	/* the QR code on white, for a phone's camera */
	float qx = 40, qy = 106, qs = 160;
	rect(qx, qy, qs, qs, rgba(255, 255, 255, 255));
	if (sc.qr_size) {
		float m = (qs - 16) / sc.qr_size;
		mode_color();
		for (int y = 0; y < sc.qr_size; y++)
			for (int x = 0; x < sc.qr_size; x++)
				if (sc.qr[y * sc.qr_size + x]) rect(qx + 8 + x * m, qy + 8 + y * m, m + 0.05f, m + 0.05f, rgba(10, 12, 16, 255));
	} else text(qx + qs / 2, qy + qs / 2, "Starting\x85", 12, rgba(90, 96, 110, 255), 1, 0);
	text(qx + qs / 2, qy + qs + 14, "Scan with your phone", 11, SOFT, 1, 0);
	/* the address, for a computer */
	float x = 222;
	text(x, 114, "Or open this address on a computer:", 11, SOFT, 0, 0);
	text(x, 138, sc.tr_addr[0] ? sc.tr_addr : "\x85", 20, GOLD, 0, 370);
	text(x, 162, sc.tr_state, 11, SOFT, 0, 370);
	rule(x - 6, 178, 600 - x);
	text(x, 194, "Files from your phone or computer", 12, TITLE, 0, 0);
	bool any = false;
	for (int i = 0; i < UI_TRANSFER_ROWS; i++) {
		if (!sc.tr_rows[i].name[0]) continue;
		any = true;
		float y = 206 + i * 38;
		rect_v(x - 6, y, 384, 32, rgba(76, 94, 114, 80), rgba(40, 54, 70, 90));
		image(&tex_card_file, x + 2, y + 6, 16, 20, 0, 0, rgba(255, 255, 255, 255));
		text(x + 28, y + 10, sc.tr_rows[i].name, 12, TEXT, 0, 330);
		text(x + 28, y + 24, sc.tr_rows[i].detail, 10, SOFT, 0, 330);
		float p = sc.tr_rows[i].progress;
		if (p >= 0) {
			rrect_v(x + 250, y + 20, 120, 5, 2, rgba(0, 0, 0, 140), rgba(0, 0, 0, 140));
			if (p > 0) rrect_v(x + 250, y + 20, 120 * clamp01(p), 5, 2, rgba(255, 232, 138, 255), rgba(216, 176, 48, 255));
		}
	}
	if (!any) text(x, 222, "Nothing yet. Choose files on the page, and they'll show here.", 11, DIM, 0, 370);
}

/* a message across the top, fading in and out over about three seconds */
static void toast_draw(void) {
	if (!sc.toast[0]) return;
	float t = (now_ms() - sc.toast_at) / 1000.0f;
	float a = clamp01(t / 0.2f) * clamp01((3.2f - t) / 0.4f);
	if (a <= 0) return;
	float w = text_width(sc.toast, 13) + 40, x = 320 - w / 2, y = 64;
	outline(x - 1, y - 1, w + 2, 34 + 2, 4, 1, rgba(11, 11, 32, (u8)(255 * a)));
	rrect_v(x, y, w, 34, 3, rgba(42, 34, 140, (u8)(240 * a)), rgba(59, 44, 182, (u8)(240 * a)));
	outline(x, y, w, 34, 3, 1, rgba(175, 175, 255, (u8)(153 * a)));
	text(320, y + 17, sc.toast, 13, rgba(0xec, 0xeb, 0xff, (u8)(255 * a)), 1, 0);
}

/* the Settings page */
static void settings_screen(void) {
	panel(22, 62, 596, 300);
	text(34, 80, "Settings", 12, TITLE, 0, 380);
	rule(32, 92, 576);
	for (int i = 0; i < sc.orow_count; i++) {
		/* three groups: the picture; sounds, the opening and Launcher; then update and exit */
		float x = 34, y = 100 + i * 27 + (i >= 4 ? 6 : 0) + (i >= 7 ? 6 : 0), w = 566, h = 24, cy = y + h / 2;
		bool on = i == sc.osel;
		rect_v(x, y, w, h, rgba(76, 94, 114, on ? 150 : 80), rgba(40, 54, 70, on ? 160 : 90));
		rect(x + 1, y + 1, w - 2, 1, rgba(255, 255, 255, 30));
		text(x + 16, cy, sc.orows[i].label, 13, TEXT, 0, 260);
		const char *v = sc.orows[i].value;
		if (v[0] == 2) text(x + w - 16, cy, v + 1, 12, SOFT, 2, 260);
		else if (v[0] && v[0] != 1) {
			float vx = x + w - 110;
			tri(vx - 66, cy, vx - 59, cy - 6, vx - 59, cy + 6, on ? GOLD : DIM);
			tri(vx + 66, cy, vx + 59, cy - 6, vx + 59, cy + 6, on ? GOLD : DIM);
			text(vx, cy, v, 13, on ? TEXT : SOFT, 1, 110);
		}
		if (on) gold_ring(x, y, w, h);
	}
}

/* the Update page */
static void update_screen(void) {
	panel(22, 62, 596, 300);
	text(34, 80, "Update from GitHub", 12, TITLE, 0, 380);
	rule(32, 92, 576);
	image(&tex_logo_title, 40, 106, LOGO_TITLE_W, LOGO_TITLE_H, 0, 0, rgba(255, 255, 255, 255));
	text(130, 116, "This version", 11, SOFT, 0, 0);
	text(250, 116, sc.upd_now, 13, TEXT, 0, 0);
	text(130, 138, "Newest on GitHub", 11, SOFT, 0, 0);
	text(250, 138, sc.upd_new[0] ? sc.upd_new : "Checking\x85", 13, sc.upd_new[0] ? GOLD : SOFT, 0, 0);
	rule(32, 166, 576);
	if (!sc.upd_notes[0]) return;
	text(40, 182, "What's new", 12, TITLE, 0, 0);
	/* the notes, wrapped */
	const char *p = sc.upd_notes;
	float y = 204;
	for (int line = 0; line < 8 && *p; line++, y += 19) {
		char buf[160];
		int n = 0, last_space = -1;
		while (p[n] && p[n] != '\n' && n < (int)sizeof(buf) - 1) {
			buf[n] = p[n]; buf[n + 1] = 0;
			if (p[n] == ' ') last_space = n;
			if (text_width(buf, 12) > 548) { n = last_space > 0 ? last_space : n; break; }
			n++;
		}
		buf[n] = 0;
		bool bullet = buf[0] == '-' || buf[0] == '*' || buf[0] == '\x7f';
		if (bullet) circle(46, y, 2, GOLD);
		text(bullet ? 56 : 40, y, bullet ? buf + 1 + (buf[1] == ' ') : buf, 12, TEXT, 0, 548);
		p += n;
		while (*p == ' ' || *p == '\n') p++;
	}
}

static void folder_glyph(float x, float y) {
	tri(x, y, x + 7, y, x + 9, y + 3, GOLD);
	rect(x, y, 7, 3, GOLD);
	rect(x, y + 3, 22, 14, GOLD);
}

static void status_chip(float right, float cy, int st) {
	static const char *label[] = { "", "New", "Changed", "Backed up", "On card", "Not on card" };
	if (st <= UI_ST_NONE || st > UI_ST_NOT_ON_CARD) return;
	Color bg, fg;
	switch (st) {
	case UI_ST_NEW: case UI_ST_NOT_ON_CARD: bg = rgba(60, 190, 110, 210); fg = rgba(230, 255, 236, 255); break;
	case UI_ST_CHANGED: bg = rgba(240, 140, 60, 220); fg = rgba(255, 244, 232, 255); break;
	default: bg = rgba(80, 90, 110, 220); fg = rgba(205, 212, 224, 255); break;
	}
	float w = text_width(label[st], 10) + 16;
	rrect_v(right - w, cy - 8, w, 16, 8, bg, bg);
	text(right - w / 2, cy, label[st], 10, fg, 1, 0);
}

static void checkbox(float x, float cy, bool on) {
	rrect_v(x, cy - 8, 16, 16, 3, on ? rgba(240, 244, 250, 255) : rgba(20, 28, 34, 255), on ? rgba(214, 222, 232, 255) : rgba(20, 28, 34, 255));
	outline(x, cy - 8, 16, 16, 3, 1, rgba(215, 225, 240, 160));
	if (on) {
		/* the tick */
		Color b = rgba(42, 146, 216, 255);
		mode_color();
		GX_Begin(GX_QUADS, GX_VTXFMT0, 8);
		vtx(x + 3, cy, b, 0, 0); vtx(x + 5, cy - 2, b, 0, 0); vtx(x + 8, cy + 1, b, 0, 0); vtx(x + 7, cy + 4, b, 0, 0);
		vtx(x + 7, cy + 4, b, 0, 0); vtx(x + 6, cy + 1, b, 0, 0); vtx(x + 12, cy - 5, b, 0, 0); vtx(x + 14, cy - 3, b, 0, 0);
		GX_End();
	}
}

static void list_screen(void) {
	panel(22, 62, 596, 300);
	text(34, 80, sc.list_title, 12, TITLE, 0, 380);
	text(606, 80, sc.list_right, 11, SOFT, 2, 200);
	rule(32, 92, 576);
	bool box = sc.checkable;
	for (int i = 0; i < sc.row_used; i++) {
		Row *w = &sc.rows[i];
		float y = 100 + i * 32, cy = y + 14;
		bool on = i == sc.list_sel && sc.overlay == UI_OVERLAY_NONE;
		rect_v(34, y, 566, 28, rgba(76, 94, 114, on ? 150 : 80), rgba(40, 54, 70, on ? 160 : 90));
		rect(35, y + 1, 564, 1, rgba(255, 255, 255, 30));
		float x = 44;
		if (box) {
			if (!w->folder) checkbox(x, cy, w->checked);
			x += 26;
		}
		if (w->folder) folder_glyph(x + 1, cy - 8);
		else if (w->has_icon) {
			outline(x - 1, y + 1, 26, 26, 2, 1, rgba(255, 255, 255, 60));
			image(&tex_icon[i], x, y + 2, 24, 24, 0, 0, rgba(255, 255, 255, 255));
		} else image(&tex_card_file, x + 4, y + 4, 16, 20, 0, 0, rgba(255, 255, 255, 255));
		x += 34;
		text(x, cy, w->title, 13, w->folder ? GOLD : TEXT, 0, w->detail[0] ? 168 : 360);
		if (w->detail[0]) text(x + 178, cy, w->detail, 11, SOFT, 0, 470 - (x + 178) - 8);
		text(492, cy, w->blocks, 11, SOFT, 2, 80);
		status_chip(590, cy, w->status);
		if (on) gold_ring(34, y, 566, 28);
	}
	/* scroll bar */
	if (sc.scroll_total > sc.scroll_count && sc.scroll_total > 0) {
		float top = 100, h = UI_LIST_ROWS * 32 - 4;
		rect(608, top, 2, h, rgba(4, 8, 10, 200));
		float th = fmaxf(16, h * sc.scroll_count / sc.scroll_total);
		float ty = top + (h - th) * (sc.scroll_total > sc.scroll_count ? (float)sc.scroll_first / (sc.scroll_total - sc.scroll_count) : 0);
		rect(608, ty, 2, th, GOLD);
	}
}

static void splash_screen(void) {
	if (!sc.splash_ready) return;   /* (the screen shape isn't known yet) */
	float k = clamp01((now_ms() - shown_at) / 600.0f);
	k = k * k * (3 - 2 * k);
	float w = LOGO_SPLASH_W, h = LOGO_SPLASH_H;
	image(&tex_logo_splash, 320 - w / 2, 190 - h / 2 + (1 - k) * 8, w, h, 0, 0, rgba(255, 255, 255, (u8)(255 * k)));
	text(320, 404, sc.splash, 11, alpha(SOFT, k), 1, 560);
	text(320, 440, sc.title_info, 10, alpha(DIM, k), 1, 300);
}

/* the cards panel: a row for each memory card, real or virtual, with its
 * picture, its name and a line about it */
static void cards_panel(void) {
	float w = 460, rows = sc.orow_count, h = 56 + rows * 42;
	float x = 320 - w / 2, y = 214 - h / 2;   /* (above the bar along the bottom) */
	panel(x, y, w, h);
	text(x + 14, y + 18, sc.overlay_title, 13, TITLE, 0, w - 28);
	rule(x + 14, y + 30, w - 28);
	for (int i = 0; i < sc.orow_count; i++) {
		float ry = y + 40 + i * 42;
		frost(x + 14, ry, w - 28, 36);
		const Color white = rgba(255, 255, 255, 255);
		switch (sc.orow_icon[i]) {
		case UI_ROW_CARD: image(&tex_card_mc, x + 24, ry + 6, 19, 25, 0, 0, white); break;
		case UI_ROW_VIRTUAL: virtual_card(x + 24, ry + 6, 19, 25, white); break;
		case UI_ROW_SD: image(&tex_card_sd, x + 25, ry + 6, 18, 24, 0, 0, white); break;
		}
		float tx = sc.orow_icon[i] ? x + 54 : x + 26;
		text(tx, ry + 12, sc.orows[i].label, 13, TEXT, 0, w - (tx - x) - 24);
		if (sc.orows[i].value[0] != 1) text(tx, ry + 26, sc.orows[i].value, 10, SOFT, 0, w - (tx - x) - 24);
		if (i == sc.osel) gold_ring(x + 14, ry, w - 28, 36);
	}
}

static void overlay_panel(void) {
	if (sc.overlay == UI_OVERLAY_NONE) return;
	rect(vl, vt, vr - vl, vb - vt, rgba(0, 0, 0, 120));
	if (sc.overlay == UI_OVERLAY_CARDS) { cards_panel(); return; }
	float w = 360, rows = sc.orow_count, h = 64 + rows * 34;
	float x = 320 - w / 2, y = 222 - h / 2;
	if (y + h > 364) y = 364 - h;   /* (always clear of the bar along the bottom) */
	panel(x, y, w, h);
	text(x + 14, y + 18, sc.overlay_title, 13, TITLE, 0, w - 28);
	rule(x + 14, y + 30, w - 28);
	bool scrolls = sc.oscroll_total > sc.oscroll_count && sc.oscroll_count > 0;
	float rw = scrolls ? w - 42 : w - 28;   /* (room for the scroll bar) */
	if (scrolls) {   /* the scroll bar, beside the rows that scroll */
		float top = y + 42 + sc.oscroll_from * 34, bh = (sc.orow_count - sc.oscroll_from) * 34 - 8;
		float bx = x + w - 22;
		rrect_v(bx, top, 6, bh, 3, rgba(0, 0, 0, 120), rgba(0, 0, 0, 120));
		float kh = fmaxf(16, bh * sc.oscroll_count / sc.oscroll_total);
		float ky = top + (bh - kh) * sc.oscroll_first / (sc.oscroll_total - sc.oscroll_count);
		rrect_v(bx, ky, 6, kh, 3, rgba(220, 228, 245, 220), rgba(170, 180, 205, 220));
	}
	for (int i = 0; i < sc.orow_count; i++) {
		float ry = y + 42 + i * 34, rwi = i < sc.oscroll_from ? w - 28 : rw;   /* (rows above the scrolling ones: full width) */
		frost(x + 14, ry, rwi, 26);
		if (i == sc.osel) gold_ring(x + 14, ry, rwi, 26);
		text(x + 26, ry + 13, sc.orows[i].label, 12, TEXT, 0, 170);
		if (sc.orows[i].value[0] && sc.orows[i].value[0] != 1) {
			bool on = i == sc.osel;
			if (on) {
				tri(x + 196, ry + 13, x + 202, ry + 8, x + 202, ry + 18, SOFT);
				tri(x + w - 26, ry + 13, x + w - 32, ry + 8, x + w - 32, ry + 18, SOFT);
			}
			text((x + 202 + x + w - 32) / 2, ry + 13, sc.orows[i].value, 12, on ? TEXT : SOFT, 1, 110);
		}
	}
}

/* ================================================================ thread */

static void frame_setup(void) {
	view_bounds();
	Mtx44 proj;
	guOrtho(proj, vt, vb, vl, vr, 0, 1);
	GX_LoadProjectionMtx(proj, GX_ORTHOGRAPHIC);
	Mtx m;
	guMtxIdentity(m);
	GX_LoadPosMtxImm(m, GX_PNMTX0);
	GX_SetCurrentMtx(GX_PNMTX0);
	GX_ClearVtxDesc();
	GX_SetVtxDesc(GX_VA_POS, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_CLR0, GX_DIRECT);
	GX_SetVtxDesc(GX_VA_TEX0, GX_DIRECT);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_POS, GX_POS_XY, GX_F32, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_CLR0, GX_CLR_RGBA, GX_RGBA8, 0);
	GX_SetVtxAttrFmt(GX_VTXFMT0, GX_VA_TEX0, GX_TEX_ST, GX_F32, 0);
	GX_SetNumChans(1);
	GX_SetChanCtrl(GX_COLOR0A0, GX_DISABLE, GX_SRC_REG, GX_SRC_VTX, GX_LIGHTNULL, GX_DF_NONE, GX_AF_NONE);
	GX_SetNumTevStages(1);
	GX_SetZMode(GX_FALSE, GX_ALWAYS, GX_FALSE);
	GX_SetBlendMode(GX_BM_BLEND, GX_BL_SRCALPHA, GX_BL_INVSRCALPHA, GX_LO_CLEAR);
	GX_SetAlphaUpdate(GX_FALSE);
	GX_SetColorUpdate(GX_TRUE);
	GX_SetCullMode(GX_CULL_NONE);
	GX_InvalidateTexAll();
	cur_mode = M_NONE;
}

/* ============================================================ the opening
 * The logo, with MEMORY MANAGER under it, centred on black. The words fold
 * into a thin slot; the slot rises to the middle of the screen (the horizon)
 * as the logo lifts; the logo back-flips end over end, coming closer, and
 * lands below the slot tipped back; it lies down and slides away into the
 * slot. A click and a flash, the slot shrinks away, the grid fades in, then
 * the screen. Timed to SFX_INTRO (tools/sound/gcmm_haze.py). */

static bool intro_on;
static u32 intro_at;

void ui_intro_start(void) { L(); intro_on = true; intro_at = now_ms(); U(); }
bool ui_intro_playing(void) { return intro_on; }
void ui_intro_skip(void) {
	L();
	if (intro_on && now_ms() - intro_at < 6200) intro_at = now_ms() - 6200;
	U();
}
void ui_cheese(bool on) { cheese = on; }
bool ui_cheese_on(void) { return cheese; }

#define IN_SLOT_Y 240.0f    /* the slot, once it's the horizon */
#define IN_SLOT_W 330.0f
#define IN_TOP_Y 140.0f     /* where the logo lifts to */
#define IN_CARD_Y 335.0f    /* where it waits, below the horizon */
#define IN_CY ((480 - (INTRO_CARD_H + 18 + INTRO_WORDS_H)) / 2 + INTRO_CARD_H / 2.0f)   /* the logo and the words, centred */
#define IN_WORDS_Y (IN_CY + INTRO_CARD_H / 2.0f + 18)
static const Color LILAC = { 150, 138, 255, 255 };

static float seg(float t, float a, float b) { return clamp01((t - a) / (b - a)); }
static float ease(float x) { return x * x * (3 - 2 * x); }
static float lerpf(float a, float b, float k) { return a + (b - a) * k; }

/* a point of the card, for drawing it in pieces (and cutting it at the slot) */
typedef struct { float x, y, s, t; Color c; } IVtx;

static void ivtx(const IVtx *v) { vtx(v->x, v->y, v->c, v->s, v->t); }

/* a piece of the card, with anything above clip_y cut away (clip_y < 0: none) */
static void intro_piece(const IVtx *q, float clip_y) {
	IVtx out[6];
	int n = 0;
	if (clip_y < 0) { for (int i = 0; i < 4; i++) out[n++] = q[i]; }
	else {
		for (int i = 0; i < 4; i++) {
			const IVtx *a = &q[i], *b = &q[(i + 1) % 4];
			bool ain = a->y >= clip_y, bin = b->y >= clip_y;
			if (ain) out[n++] = *a;
			if (ain != bin) {
				float k = (clip_y - a->y) / (b->y - a->y);
				IVtx m = { lerpf(a->x, b->x, k), clip_y, lerpf(a->s, b->s, k), lerpf(a->t, b->t, k), mix(a->c, b->c, k) };
				out[n++] = m;
			}
		}
	}
	if (n < 3) return;
	GX_Begin(GX_TRIANGLEFAN, GX_VTXFMT0, n);
	for (int i = 0; i < n; i++) ivtx(&out[i]);
	GX_End();
}

/* the rainbow sheen across the card at a point of it (u, v 0..1), as in the mock-ups */
static Color sheen_at(float u, float v, float pos) {
	float W = INTRO_CARD_W, H = INTRO_CARD_H;
	float d = (u * W + 0.6f * v * H) / (W + 0.6f * H) - pos;
	float band = expf(-(d / 0.1f) * (d / 0.1f));
	float hue = d * 3 + pos;
	hue -= floorf(hue);
	float r = 0.5f + 0.5f * cosf(6.2831853f * hue), g = 0.5f + 0.5f * cosf(6.2831853f * (hue + 1 / 3.0f)),
	      b = 0.5f + 0.5f * cosf(6.2831853f * (hue + 2 / 3.0f));
	return rgba((u8)(r * 255), (u8)(g * 255), (u8)(b * 255), (u8)(band * 0.55f * 255));
}

/* The card, turned about its middle (pitch 0: face on; pi/2: edge on, top
 * away; pi: its back), seen from 560 units away, scaled and centred on cx, cy. */
static void intro_card(float cx, float cy, float pitch, float scale, float shine, float clip_y) {
	enum { NX = 16, NY = 8 };
	float w = INTRO_CARD_W * scale / 2, h = INTRO_CARD_H * scale / 2;
	float cp = cosf(pitch), sp = sinf(pitch), F = 560;
	static float px[(NX + 1) * (NY + 1)], py[(NX + 1) * (NY + 1)];
	for (int j = 0; j <= NY; j++)
		for (int i = 0; i <= NX; i++) {
			float x = (2.0f * i / NX - 1) * w, y = (2.0f * j / NY - 1) * h;
			float k = F / (F + y * sp);
			px[j * (NX + 1) + i] = cx + x * k;
			py[j * (NX + 1) + i] = cy + y * cp * k;
		}
	/* how big it looks: edge on, it's a thin bright line */
	float x0 = px[0], x1 = px[NX], x2 = px[NY * (NX + 1) + NX], x3 = px[NY * (NX + 1)];
	float y0 = py[0], y1 = py[NX], y2 = py[NY * (NX + 1) + NX], y3 = py[NY * (NX + 1)];
	float area = ((x0 * y1 - x1 * y0) + (x1 * y2 - x2 * y1) + (x2 * y3 - x3 * y2) + (x3 * y0 - x0 * y3)) / 2;
	if (fabsf(area) < 40) {
		float top = fminf(y0, y3) - 1, bot = fmaxf(y0, y3) + 1;
		if (clip_y >= 0 && top < clip_y) top = clip_y;
		if (bot > top) rect(fminf(x0, x3), top, fmaxf(x1, x2) - fminf(x0, x3), bot - top, rgba(205, 200, 255, 255));
		return;
	}
	bool back = area < 0;
	float light = 0.6f + 0.4f * fabsf(cp);   /* darker as it turns from the light */
	for (int pass = 0; pass < 2; pass++) {
		/* the face (the back: the card's shape in indigo), then the sheen over it */
		mode_tex(&tex_intro_card, back || pass == 1);
		for (int j = 0; j < NY; j++)
			for (int i = 0; i < NX; i++) {
				IVtx q[4];
				static const int di[4] = { 0, 1, 1, 0 }, dj[4] = { 0, 0, 1, 1 };
				for (int c = 0; c < 4; c++) {
					int ii = i + di[c], jj = j + dj[c], k = jj * (NX + 1) + ii;
					float u = (float)ii / NX, v = (float)jj / NY;
					float tv = back ? 1 - v : v;   /* the back, the right way up */
					Color col;
					if (pass == 1) col = sheen_at(u, tv, shine);
					else if (back) {
						Color g = mix(rgba(70, 60, 180, 255), rgba(30, 24, 110, 255), tv);
						col = rgba((u8)(g.r * light), (u8)(g.g * light), (u8)(g.b * light), 255);
					} else col = rgba((u8)(255 * light), (u8)(255 * light), (u8)(255 * light), 255);
					q[c] = (IVtx){ px[k], py[k], u, tv, col };
				}
				intro_piece(q, clip_y);
			}
	}
}

/* the slot: a thin dark groove with a lilac edge; lit, it glows */
static void intro_slot(float y, float hw, float a, float lit) {
	if (hw < 1 || a <= 0) return;
	for (int g = 4; g >= 1; g--) {   /* the glow */
		float e = g * (2 + 3 * lit);
		rrect_v(320 - hw - e, y - 3 - e, 2 * hw + 2 * e, 6 + 2 * e, 3 + e, alpha(LILAC, a * (0.05f + 0.12f * lit) / g), alpha(LILAC, a * (0.05f + 0.12f * lit) / g));
	}
	rrect_v(320 - hw, y - 3, 2 * hw, 6, 3, rgba(26, 20, 80, (u8)(255 * a)), rgba(26, 20, 80, (u8)(255 * a)));
	outline(320 - hw, y - 3, 2 * hw, 6, 3, 1, alpha(LILAC, a));
	if (hw > 10) rect(320 - hw + 5, y - 1, 2 * hw - 10, 2, rgba(6, 4, 24, (u8)(255 * a)));
	if (lit > 0 && hw > 8) rect(320 - hw + 4, y - 1, 2 * hw - 8, 2, rgba(230, 225, 255, (u8)(255 * a * lit)));
}

/* the opening, t seconds in, up to the screen coming up (6.2 s) */
static void intro_draw(float t) {
	rect(vl, vt, vr - vl, vb - vt, rgba(0, 0, 0, 255));
	float kg = ease(seg(t, 5.4f, 6.2f));
	if (kg > 0) { bd_k = kg; backdrop(); bd_k = 1; }

	/* MEMORY MANAGER folds into the slot (1.4 - 2.3): flat and thin, lilac */
	float km = seg(t, 1.4f, 2.3f);
	if (km < 1) {
		float e = ease(km);
		float w = lerpf(INTRO_WORDS_W, IN_SLOT_W, e), h = fmaxf(2, lerpf(INTRO_WORDS_H, 6, ease(clamp01(km * 1.3f))));
		Color c = mix(rgba(255, 255, 255, 255), LILAC, e);
		c.a = (u8)(255 * (1 - ease(seg(km, 0.75f, 1))));
		image(&tex_intro_words, 320 - w / 2, IN_WORDS_Y + INTRO_WORDS_H / 2.0f - h / 2, w, h, 0, 0, c);
	}

	/* the slot: there once the words have become it, rising to the horizon as
	 * the logo lifts, lit when the card goes in, then shrinking away */
	float slot_y = lerpf(IN_WORDS_Y + INTRO_WORDS_H / 2.0f, IN_SLOT_Y, ease(seg(t, 2.6f, 3.2f)));
	float shrink = ease(seg(t, 4.95f, 5.4f));
	float lit = t >= 4.65f ? 1 - 0.5f * seg(t, 4.65f, 5.2f) : 0;
	if (shrink < 1) intro_slot(slot_y, IN_SLOT_W / 2 * (1 - shrink), ease(seg(t, 1.95f, 2.3f)), lit);

	/* the logo */
	if (t < 4.65f) {
		float pitch, cy, scale, shine, clip = -1;
		const float PI2 = 6.2831853f;
		if (t < 2.6f) {                         /* at rest; a soft gloss passes over it */
			pitch = 0; cy = IN_CY; scale = 1; shine = -0.5f + seg(t, 0.4f, 1.4f) * 1.9f;
		} else if (t < 3.1f) {                  /* lifts, tips a little (the wind-up) */
			float k = ease(seg(t, 2.6f, 3.1f));
			pitch = -0.3f * k; cy = lerpf(IN_CY, IN_TOP_Y, k); scale = 1; shine = -0.5f;
		} else if (t < 4.1f) {                  /* the back flip, coming closer, dropping below the horizon */
			float k = ease(seg(t, 3.1f, 4.1f));
			pitch = lerpf(-0.3f, PI2 - 0.55f, k);
			cy = lerpf(IN_TOP_Y, IN_CARD_Y, ease(k)); scale = lerpf(1, 1.12f, k) + 0.22f * sinf(k * 3.1415927f); shine = -0.3f + k * 1.5f;
		} else if (t < 4.35f) {                 /* waits below the slot, tipped back */
			float k = seg(t, 4.1f, 4.35f);
			pitch = PI2 - 0.55f; cy = IN_CARD_Y - 3 * sinf(k * 3.1415927f); scale = 1.12f; shine = 1.2f;
		} else {                                /* lies down and slides away into the slot */
			float k = seg(t, 4.35f, 4.65f), e = k * k * (1.6f - 0.6f * k);
			pitch = lerpf(PI2 - 0.55f, PI2 - 1.53f, e);
			cy = lerpf(IN_CARD_Y, IN_SLOT_Y - 2, e); scale = lerpf(1.12f, 0.86f, e); shine = 1.3f;
			clip = IN_SLOT_Y;
		}
		intro_card(320, cy, pitch, scale, shine, clip);
	}

	if (t >= 4.65f && t < 4.9f) rect(vl, vt, vr - vl, vb - vt, rgba(255, 255, 255, (u8)(110 * (1 - seg(t, 4.65f, 4.9f)))));
}

static void draw_frame(void) {
	frame_setup();
	L();
	/* the opening: on its own until 6.2 s, then the screen comes up through the grid */
	float it = intro_on ? (now_ms() - intro_at) / 1000.0f : 99;
	if (it >= 7.0f) intro_on = false;
	if (intro_on && it < 6.2f) { intro_draw(it); U(); return; }
	if (sc.screen == UI_SPLASH && !sc.splash_ready) {   /* starting up: black until the settings are read */
		rect(vl, vt, vr - vl, vb - vt, rgba(0, 0, 0, 255));
		U();
		return;
	}
	backdrop();
	if (sc.screen == UI_SPLASH) {
		splash_screen();
	} else {
		header();
		if (sc.screen == UI_LIST) list_screen();
		else if (sc.screen == UI_TOOLS) tools_screen();
		else if (sc.screen == UI_UPDATE) update_screen();
		else if (sc.screen == UI_SETTINGS) settings_screen();
		else if (sc.screen == UI_TRANSFER) transfer_screen();
		else main_screen();
		overlay_panel();
		status_bar();   /* (over a panel's shade: it describes what's in the panel) */
		hints_row();   /* (over the panel's shade: they're for the panel too) */
		toast_draw();
	}
	if (intro_on) { bd_k = 1 - ease(seg(it, 6.2f, 7.0f)); backdrop(); bd_k = 1; }
	U();
}

static void *video_main(void *arg) {
	(void)arg;
	while (running) {
		if (video_change) {
			video_change = false;
			GXRModeObj old = mode;
			pick_mode();
			if (memcmp(&old, &mode, sizeof(mode))) {
				GX_DrawDone();
				VIDEO_SetBlack(TRUE);
				VIDEO_Flush();
				VIDEO_WaitVSync();
				configure_vi();
				setup_gx();
			}
		}
		draw_frame();
		GX_SetZMode(GX_TRUE, GX_ALWAYS, GX_TRUE);   /* so the copy clears depth too */
		GX_CopyDisp(xfb[fb], GX_TRUE);
		GX_DrawDone();
		VIDEO_SetNextFramebuffer(xfb[fb]);
		VIDEO_Flush();
		VIDEO_WaitVSync();
		fb ^= 1;
	}
	return NULL;
}

void ui_init(void) {
	VIDEO_Init();
	PAD_Init();
	LWP_MutexInit(&lock, false);
	/* framebuffers big enough for any mode (PAL's 576 lines included) */
	size_t fb_bytes = 640 * 576 * 2;
	xfb[0] = MEM_K0_TO_K1(memalign(32, fb_bytes));
	xfb[1] = MEM_K0_TO_K1(memalign(32, fb_bytes));
	pick_mode();
	configure_vi();
	fifo = memalign(32, FIFO_SIZE);
	memset(fifo, 0, FIFO_SIZE);
	GX_Init(fifo, FIFO_SIZE);
	GX_SetCopyClear((GXColor){ 0, 0, 0, 255 }, 0x00FFFFFF);
	setup_gx();
	GX_SetCullMode(GX_CULL_NONE);
	GX_CopyDisp(xfb[0], GX_TRUE);

	tex_init(&tex_font_small, font_small.tex, font_small.w, font_small.h, GX_TF_I8);
	tex_init(&tex_font_large, font_large.tex, font_large.w, font_large.h, GX_TF_I8);
	tex_init(&tex_logo_title, logo_title_tex, LOGO_TITLE_W, LOGO_TITLE_H, GX_TF_RGBA8);
	tex_init(&tex_logo_splash, logo_splash_tex, LOGO_SPLASH_W, LOGO_SPLASH_H, GX_TF_RGBA8);
	tex_init(&tex_card_mc, card_mc_tex, CARD_MC_W, CARD_MC_H, GX_TF_RGBA8);
	tex_init(&tex_card_sd, card_sd_tex, CARD_SD_W, CARD_SD_H, GX_TF_RGBA8);
	tex_init(&tex_card_file, card_file_tex, CARD_FILE_W, CARD_FILE_H, GX_TF_RGBA8);
	tex_init(&tex_card_cheese, card_cheese_tex, CARD_CHEESE_W, CARD_CHEESE_H, GX_TF_RGBA8);
	tex_init(&tex_intro_card, intro_card_tex, INTRO_CARD_W, INTRO_CARD_H, GX_TF_RGBA8);
	tex_init(&tex_intro_words, intro_words_tex, INTRO_WORDS_W, INTRO_WORDS_H, GX_TF_RGBA8);
	for (int i = 0; i < UI_LIST_ROWS; i++) tex_init(&tex_icon[i], icon_tex[i], 32, 32, GX_TF_RGBA8);

	sc.screen = UI_SPLASH;
	sc.main_sel = 0;
	sc.progress = -1;
	shown_at = now_ms();
	running = true;
	LWP_CreateThread(&thread, video_main, NULL, NULL, 64 * 1024, 100);
}

void ui_shutdown(void) {
	if (!running) return;
	running = false;
	LWP_JoinThread(thread, NULL);
	GX_DrawDone();
	VIDEO_SetBlack(TRUE);
	VIDEO_Flush();
	VIDEO_WaitVSync();
}
