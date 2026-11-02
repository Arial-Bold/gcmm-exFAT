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

UiSettings ui_set = { UI_RES_AUTO, 0, 1, 0 };
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
		if (!strcmp(line, "display.resolution")) ui_set.resolution = (v >= 0 && v < UI_RES_COUNT) ? v : UI_RES_AUTO;
		else if (!strcmp(line, "display.widescreen")) ui_set.widescreen = v ? 1 : 0;
		else if (!strcmp(line, "display.full_width")) ui_set.full_width = v ? 1 : 0;
		else if (!strcmp(line, "display.overscan")) ui_set.overscan = v < 0 ? 0 : v > UI_OVERSCAN_MAX ? UI_OVERSCAN_MAX : v;
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
	char action[48], action_key;
	float progress;
	/* header */
	char mc_name[32], mc_info[48], mc2_name[32], mc2_info[48], sd_name[32], sd_info[48];
	/* main */
	int main_sel, tools_sel;
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
void ui_action(const char *label, char key) { L(); cpy(sc.action, label, sizeof(sc.action)); sc.action_key = key; U(); }
void ui_progress(float f) { L(); sc.progress = f; U(); }
void ui_main_cards(const char *mn, const char *mi, const char *sn, const char *si) {
	L();
	cpy(sc.mc_name, mn, sizeof(sc.mc_name)); cpy(sc.mc_info, mi, sizeof(sc.mc_info));
	cpy(sc.sd_name, sn, sizeof(sc.sd_name)); cpy(sc.sd_info, si, sizeof(sc.sd_info));
	U();
}
void ui_main_card2(const char *mn, const char *mi) {
	L(); cpy(sc.mc2_name, mn, sizeof(sc.mc2_name)); cpy(sc.mc2_info, mi, sizeof(sc.mc2_info)); U();
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
void ui_overlay(int o, const char *t) { L(); sc.overlay = o; cpy(sc.overlay_title, t, sizeof(sc.overlay_title)); sc.orow_count = 0; sc.osel = 0; U(); }
void ui_overlay_row(int r, const char *l, const char *v) {
	if (r < 0 || r >= UI_OVERLAY_ROWS) return;
	L(); cpy(sc.orows[r].label, l, sizeof(sc.orows[r].label)); cpy(sc.orows[r].value, v, TXT); if (!v) sc.orows[r].value[0] = 1; U();
}
void ui_overlay_select(int r) { L(); sc.osel = r; U(); }
void ui_overlay_rows(int n) { L(); sc.orow_count = n; U(); }

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

/* ------------------------------------------------- the TV-screen backdrop
 * CubeVerse's "soft phosphor" screen: a deep blue-black, a cool glow, soft
 * scanlines bowed by the curve of the glass and a dark tube-shaped edge.
 * The meshes are worked out once (and again when the screen shape changes). */

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

static void tube_point(float fx, float fy, float fw, float fh, float u, float v, float k, float *x, float *y) {
	float bu = u * (1 + k * v * v), bv = v * (1 + k * 1.4f * u * u);
	*x = fx + fw * (bu + 1) / 2;
	*y = fy + fh * (bv + 1) / 2;
}

enum { GC = 12, GR = 8, SC = 16, SROWS = 63, VC = 24, VR = 18 };
static float glow_xy[2][(GC + 1) * (GR + 1) * 2];
static Color glow_c[2][(GC + 1) * (GR + 1)];
static float *scan_xy;     /* (SC+1) x up to 4*SROWS rows */
static Color *scan_c;
static int scan_rows;
static float edge_xy[(VC + 1) * (VR + 1) * 2];
static Color edge_c[(VC + 1) * (VR + 1)];
static float made_for[4] = { -1, -1, -1, -1 };

static void backdrop_make(float fx, float fy, float fw, float fh) {
	for (int pass = 0; pass < 2; pass++)
		for (int j = 0; j <= GR; j++)
			for (int i = 0; i <= GC; i++) {
				int k = j * (GC + 1) + i;
				float x = fx + fw * i / GC, y = fy + fh * j / GR;
				glow_xy[pass][k * 2] = x; glow_xy[pass][k * 2 + 1] = y;
				float dx = (x - (fx + fw / 2)) / (fw * (pass ? 0.45f : 0.6f));
				float dy = pass ? (y - (fy + fh * 0.5f)) / (fh * 0.5f) : (y - (fy + fh * 0.3f)) / (fh * 0.7f);
				float v = clamp01(1 - sqrtf(dx * dx + dy * dy) / (pass ? 0.66f : 0.62f));
				glow_c[pass][k] = pass ? rgba(60, 110, 175, (u8)(clamp01(v * 0.13f * 1.7f) * 255)) : rgba(120, 170, 225, (u8)(clamp01(v * 0.17f * 1.7f) * 255));
			}
	/* scanlines: per line clear, full, full (then clear at the next line), 8 units apart */
	static const float pos[3] = { 0, 0.3f, 0.72f };
	const float period = 8;
	int nrows = (int)(fh / period) * 3 + 1;
	free(scan_xy); free(scan_c);
	scan_xy = malloc(sizeof(float) * (SC + 1) * nrows * 2);
	scan_c = malloc(sizeof(Color) * (SC + 1) * nrows);
	scan_rows = (scan_xy && scan_c) ? nrows : 0;
	for (int r = 0; r < scan_rows; r++) {
		int line = r / 3, ph = r % 3;
		float v = (line + pos[ph]) * period / fh * 2 - 1;
		for (int i = 0; i <= SC; i++) {
			int k = r * (SC + 1) + i;
			tube_point(fx, fy, fw, fh, (float)i / SC * 2 - 1, v, 0.06f, &scan_xy[k * 2], &scan_xy[k * 2 + 1]);
			scan_c[k] = rgba(0, 0, 0, ph ? 150 : 0);
		}
	}
	for (int j = 0; j <= VR; j++)
		for (int i = 0; i <= VC; i++) {
			int k = j * (VC + 1) + i;
			float u = (float)i / VC * 2 - 1, v = (float)j / VR * 2 - 1;
			edge_xy[k * 2] = fx + fw * i / VC; edge_xy[k * 2 + 1] = fy + fh * j / VR;
			float au = fabsf(u) * 1.02f, av = fabsf(v) * 1.04f;
			float d = powf(powf(au, 5) + powf(av, 5), 0.2f), d2 = sqrtf(u * u * 0.8f + v * v);
			float t = clamp01((d - 0.62f) / 0.38f), t2 = clamp01((d2 - 0.35f) / 0.9f);
			float a = 1 - (1 - powf(t, 1.6f)) * (1 - 0.55f * t2 * t2);
			edge_c[k] = rgba(0, 0, 0, (u8)(clamp01(a) * 255));
		}
	made_for[0] = fx; made_for[1] = fy; made_for[2] = fw; made_for[3] = fh;
}

static void backdrop(void) {
	float fx = vl, fy = vt, fw = vr - vl, fh = vb - vt;
	if (made_for[0] != fx || made_for[1] != fy || made_for[2] != fw || made_for[3] != fh) backdrop_make(fx, fy, fw, fh);
	rect_v(fx, fy, fw, fh, rgba(0x1c, 0x24, 0x31, 255), rgba(0x0c, 0x10, 0x16, 255));
	mesh(GC, GR, glow_xy[0], glow_c[0]);
	mesh(GC, GR, glow_xy[1], glow_c[1]);
	for (int r0 = 0; r0 < scan_rows - 1; r0 += SROWS) {
		int rows = scan_rows - 1 - r0 < SROWS ? scan_rows - 1 - r0 : SROWS;
		mesh(SC, rows, scan_xy + r0 * (SC + 1) * 2, scan_c + r0 * (SC + 1));
	}
	mesh(VC, VR, edge_xy, edge_c);
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

/* A round button (A, B, X, Y, Z) */
static void button_round(float cx, float cy, const char *l, Color body, Color label) {
	circle(cx, cy, 9, rgba(10, 12, 16, 230));
	circle(cx, cy, 8, body);
	rect_v(cx - 5, cy - 7, 10, 6, rgba(255, 255, 255, 80), rgba(255, 255, 255, 0));
	text(cx, cy, l, 10, label, 1, 0);
}
static void key_button(float cx, float cy, char k) {
	char l[2] = { k, 0 };
	switch (k) {
	case 'A': button_round(cx, cy, l, rgba(60, 190, 110, 255), rgba(255, 255, 255, 255)); break;
	case 'B': button_round(cx, cy, l, rgba(220, 70, 80, 255), rgba(255, 255, 255, 255)); break;
	case 'Z': button_round(cx, cy, l, rgba(140, 120, 240, 255), rgba(255, 255, 255, 255)); break;
	default: button_round(cx, cy, l, rgba(214, 218, 226, 255), rgba(30, 34, 40, 255)); break;
	}
}

/* the description bar along the bottom, with the action's button (or a progress bar) at its right */
static void status_bar(void) {
	desc_bar(22, 374, 596, 52);
	float tw = 572;
	if (sc.progress >= 0) {
		float bx = 418, bw = 184, by = 397;
		rrect_v(bx, by, bw, 8, 3, rgba(0, 0, 0, 140), rgba(0, 0, 0, 140));
		float f = clamp01(sc.progress);
		if (f > 0) rrect_v(bx, by, bw * f, 8, 3, rgba(255, 232, 138, 255), rgba(216, 176, 48, 255));
		tw = 370;
	} else if (sc.action[0]) {
		float bw = text_width(sc.action, 12) + 54, bx = 606 - bw, by = 384, bh = 32;
		rrect_v(bx, by, bw, bh, 4, rgba(72, 184, 240, 255), rgba(42, 146, 216, 255));
		outline(bx, by, bw, bh, 4, 1, rgba(215, 225, 240, 120));
		key_button(bx + 18, by + bh / 2, sc.action_key);
		text(bx + 34, by + bh / 2, sc.action, 12, rgba(255, 255, 255, 255), 0, 0);
		tw = bx - 50;
	}
	text(38, 391, sc.status1, 13, TEXT, 0, tw);
	text(38, 410, sc.status2, 11, rgba(200, 205, 240, 255), 0, tw);
}

/* the main screen's pictures */
static void card_pair(float cx, float cy, bool to_sd) {
	GXTexObj *a = to_sd ? &tex_card_mc : &tex_card_sd, *b = to_sd ? &tex_card_sd : &tex_card_mc;
	image(a, cx - 64, cy - 30, 44, 58, 0, 0, rgba(255, 255, 255, 255));
	image(b, cx + 20, cy - 30, 44, 58, 0, 0, rgba(255, 255, 255, 255));
	tri(cx + 12, cy - 1, cx + 2, cy - 9, cx + 2, cy + 7, GOLD);
	rect(cx - 12, cy - 4, 15, 6, GOLD);
}
static void gear(float cx, float cy, float r, Color c, Color hole) {
	for (int i = 0; i < 8; i++) {
		float a = i * 3.14159265f / 4, x = cx + cosf(a) * r, y = cy + sinf(a) * r;
		rrect_v(x - r * 0.28f, y - r * 0.28f, r * 0.56f, r * 0.56f, 2, c, c);
	}
	circle(cx, cy, r * 0.92f, c);
	circle(cx, cy, r * 0.38f, hole);
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
	case 2: case 3:     /* delete saves, format: the card with a red mark */
		image(&tex_card_mc, cx - 16, cy - 19, 28, 37, 0, 0, white);
		circle(cx + 12, cy + 10, 10, rgba(10, 12, 16, 230));
		circle(cx + 12, cy + 10, 9, rgba(220, 70, 80, 255));
		if (t == 2) rect(cx + 7, cy + 9, 10, 3, white);              /* minus: take some away */
		else { rect(cx + 11, cy + 4, 3, 8, white); rect(cx + 11, cy + 14, 3, 3, white); }   /* "!": everything goes */
		break;
	case 4:             /* another SD card adapter */
		image(&tex_card_sd, cx - 22, cy - 18, 24, 32, 0, 0, rgba(255, 255, 255, 120));
		image(&tex_card_sd, cx - 6, cy - 14, 24, 32, 0, 0, white);
		break;
	default:            /* settings */
		gear(cx, cy, 15, rgba(214, 218, 226, 255), rgba(36, 46, 56, 255));
		break;
	}
}

static void tools_screen(void) {
	static const char *names[UI_TOOLS_COUNT] = { "Back up the whole card", "Restore a whole card", "Delete saves",
	                                             "Format the card", "Choose the SD card", "Settings" };
	static const char *desc[UI_TOOLS_COUNT] = { "One file with everything on it", "From a whole-card backup",
	                                            "Choose saves to remove", "Erase everything on it",
	                                            "Use another adapter", "Picture size and position" };
	panel(22, 62, 596, 300);
	text(34, 80, "Card tools", 12, TITLE, 0, 380);
	rule(32, 92, 576);
	for (int i = 0; i < UI_TOOLS_COUNT; i++) {
		float x = 34 + (i % 2) * 290, y = 104 + (i / 2) * 84, w = 282, h = 74;
		bool on = i == sc.tools_sel && sc.overlay == UI_OVERLAY_NONE;
		rect_v(x, y, w, h, rgba(76, 94, 114, on ? 150 : 80), rgba(40, 54, 70, on ? 160 : 90));
		rect(x + 1, y + 1, w - 2, 1, rgba(255, 255, 255, 30));
		tool_picture(i, x + 40, y + h / 2);
		text(x + 80, y + 28, names[i], 14, i == 3 ? rgba(255, 170, 170, 255) : TEXT, 0, w - 90);
		text(x + 80, y + 48, desc[i], 11, SOFT, 0, w - 90);
		if (on) gold_ring(x, y, w, h);
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
	float k = clamp01((now_ms() - shown_at) / 600.0f);
	k = k * k * (3 - 2 * k);
	float w = LOGO_SPLASH_W, h = LOGO_SPLASH_H;
	image(&tex_logo_splash, 320 - w / 2, 190 - h / 2 + (1 - k) * 8, w, h, 0, 0, rgba(255, 255, 255, (u8)(255 * k)));
	text(320, 404, sc.splash, 11, alpha(SOFT, k), 1, 560);
	text(320, 440, sc.title_info, 10, alpha(DIM, k), 1, 300);
}

static void overlay_panel(void) {
	if (sc.overlay == UI_OVERLAY_NONE) return;
	rect(vl, vt, vr - vl, vb - vt, rgba(0, 0, 0, 120));
	float w = 360, rows = sc.orow_count, h = 64 + rows * 34;
	float x = 320 - w / 2, y = 222 - h / 2;
	panel(x, y, w, h);
	text(x + 14, y + 18, sc.overlay_title, 13, TITLE, 0, w - 28);
	rule(x + 14, y + 30, w - 28);
	for (int i = 0; i < sc.orow_count; i++) {
		float ry = y + 42 + i * 34;
		frost(x + 14, ry, w - 28, 26);
		if (i == sc.osel) gold_ring(x + 14, ry, w - 28, 26);
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

static void draw_frame(void) {
	frame_setup();
	L();
	backdrop();
	if (sc.screen == UI_SPLASH) {
		splash_screen();
	} else {
		header();
		if (sc.screen == UI_LIST) list_screen();
		else if (sc.screen == UI_TOOLS) tools_screen();
		else main_screen();
		status_bar();
		overlay_panel();
	}
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
