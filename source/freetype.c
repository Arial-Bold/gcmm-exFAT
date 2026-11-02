/****************************************************************************
* freetype.c - GCMM exFAT's screens: the main screen (Back up, Restore,
* Card tools), the save lists with their tick boxes, the prompts along the
* bottom and the settings.
*
* (The name is historical: text used to be drawn with FreeType. Everything
* is now described to ui.c, which draws it in the System look.)
****************************************************************************/
#include <gccore.h>
#include <ogcsys.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/statvfs.h>
#include <ogc/lwp_watchdog.h>
#include "bannerload.h"
#include "freetype.h"
#include "gci.h"
#include "mcard.h"
#include "sdsupp.h"
#include "bitmap.h"
#include "raw.h"
#include "ui.h"
#include "saves.h"

/*** Globals ***/
bool offsetchanged = true;
extern const char appversion[];
extern card_stat CardStatus;
extern int cancel;
extern int doall;
extern int mode;
extern s32 MEM_CARD;
extern u8 CUR_DEVICE;
extern bool have_sd;
void pick_card_slot(void);

extern u16 bannerdata[CARD_BANNER_W*CARD_BANNER_H] ATTRIBUTE_ALIGN (32);
extern u8 bannerdataCI[CARD_BANNER_W*CARD_BANNER_H] ATTRIBUTE_ALIGN (32);
extern u8 icondata[8][1024] ATTRIBUTE_ALIGN (32);
extern u16 icondataRGB[8][1024] ATTRIBUTE_ALIGN (32);
extern u16 tlut[9][256] ATTRIBUTE_ALIGN (32);
extern u16 tlutbanner[256] ATTRIBUTE_ALIGN (32);
extern int numicons;
extern int frametable[2*CARD_MAXICONS - 2];
extern int iconindex[2*CARD_MAXICONS - 2];
extern int lastframe;
extern int lasticon;

extern Header cardheader;
extern s32 memsize, sectsize;
extern syssramex *sramex;
extern u8 imageserial[12];

extern u32 retraceCount;
extern syssramex* __SYS_LockSramEx();
extern u32 __SYS_UnlockSramEx(u32 write);

/*** File listings ***/
extern card_dir CardList[];
extern u8 filelist[1024][1024];
extern u32 maxfile;
extern card_direntry gci;
#define MAXFILEBUFFER (1024 * 2048)
extern u8 FileBuffer[MAXFILEBUFFER] ATTRIBUTE_ALIGN (32);
extern u8 CommentBuffer[64] ATTRIBUTE_ALIGN (32);

extern u8 currFolder[260];
extern int folderCount;
extern int displaypath;
extern char fatpath[4];

#define PAGESIZE UI_LIST_ROWS
#define DOT "  \x95  "

/* ============================================================== buttons */

/* Buttons held now, and the ones pressed since the last call. Waits for the
 * next frame first, so loops built on it run once a frame. */
static u32 held_prev;
static u32 pad_held, pad_down;
static void read_pad(void)
{
	VIDEO_WaitVSync();
	pad_held = PAD_ButtonsHeld(0);
	/* the analog stick works as the + Control Pad too */
	s8 sx = PAD_StickX(0), sy = PAD_StickY(0);
	if (sx < -48) pad_held |= PAD_BUTTON_LEFT;
	if (sx > 48) pad_held |= PAD_BUTTON_RIGHT;
	if (sy > 48) pad_held |= PAD_BUTTON_UP;
	if (sy < -48) pad_held |= PAD_BUTTON_DOWN;
	pad_down = pad_held & ~held_prev;
	held_prev = pad_held;
}

/* + Control Pad with repeat: a press, then again every few frames while held */
static int repeat_frames;
static u32 dpad_step(void)
{
	const u32 dirs = PAD_BUTTON_UP | PAD_BUTTON_DOWN | PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT;
	if (pad_down & dirs) { repeat_frames = 0; return pad_down & dirs; }
	if (pad_held & dirs) {
		repeat_frames++;
		if (repeat_frames >= 18 && (repeat_frames - 18) % 5 == 0) return pad_held & dirs;
		return 0;
	}
	repeat_frames = 0;
	return 0;
}

void WaitRelease()
{
	do read_pad(); while (pad_held);
}

void ShowScreen ()
{
	VIDEO_WaitVSync ();
}

void ShowAction (char *msg)
{
	writeStatusBar(msg, "");
}

void writeStatusBar(char *line1, char *line2)
{
	ui_status(line1, line2);
}

static void WaitButtonA ()
{
	for (;;) { read_pad(); if (pad_down & PAD_BUTTON_A) break; }
	WaitRelease();
}

/* Returns 0 = B; 1 = A */
static int WaitButtonAB ()
{
	for (;;) {
		read_pad();
		if (pad_down & PAD_BUTTON_A) { WaitRelease(); return 1; }
		if (pad_down & PAD_BUTTON_B) { WaitRelease(); return 0; }
	}
}

/* Returns 0 = Z; 1 = A */
static int WaitButtonAZ ()
{
	for (;;) {
		read_pad();
		if (pad_down & PAD_BUTTON_A) { WaitRelease(); return 1; }
		if (pad_down & PAD_TRIGGER_Z) { WaitRelease(); return 0; }
	}
}

void WaitPrompt (char *msg)
{
	writeStatusBar(msg, "A: OK");
	WaitButtonA ();
	writeStatusBar("", "");
}

/* Returns 1 if A was pressed and 0 if B was pressed */
int WaitPromptChoice (char *msg, char *bmsg, char *amsg)
{
	char txt[96];
	int ret;
	snprintf(txt, sizeof(txt), "A: %s" DOT "B: %s", amsg, bmsg);
	writeStatusBar(msg, txt);
	ret = WaitButtonAB ();
	writeStatusBar("", "");
	return ret;
}

/* Returns 1 if A was pressed and 0 if Z was pressed */
int WaitPromptChoiceAZ (char *msg, char *bmsg, char *amsg)
{
	char txt[96];
	int ret;
	snprintf(txt, sizeof(txt), "A: %s" DOT "Z: %s", amsg, bmsg);
	writeStatusBar(msg, txt);
	ret = WaitButtonAZ ();
	writeStatusBar("", "");
	return ret;
}

/* ========================================================== main screen */

const char *DeviceName(int dev)
{
	switch (dev) {
	case DEV_GCSDA: return "SD Gecko in Slot A";
	case DEV_GCSDB: return "SD Gecko in Slot B";
	case DEV_GCSDC: return "SD2SP2";
	case DEV_GCODE: return "GC Loader";
	default: return "No device";
	}
}

static const char *DeviceShortName(int dev)
{
	switch (dev) {
	case DEV_GCSDA: return "SD Gecko A";
	case DEV_GCSDB: return "SD Gecko B";
	case DEV_GCSDC: return "SD2SP2";
	case DEV_GCODE: return "GC Loader";
	default: return "";
	}
}

void UpdateMainCards(void);

void UpdateTitleInfo(void)
{
	char txt[64];
	snprintf(txt, sizeof(txt), "GCMM exFAT %s", appversion);
	ui_title_info(txt);
	UpdateMainCards();
}

static char sd_free[32];

/* Free space on the SD card. Call after anything that writes to it. */
void UpdateSDInfo(void)
{
	sd_free[0] = 0;
	if (!have_sd) return;
	char root[8];
	struct statvfs st;
	snprintf(root, sizeof(root), "%s:/", fatpath);
	if (statvfs(root, &st) == 0) {
		unsigned long long b = (unsigned long long)st.f_bavail * st.f_frsize;
		if (b >= 1000000000ULL) snprintf(sd_free, sizeof(sd_free), "%.1f GB free", b / 1e9);
		else snprintf(sd_free, sizeof(sd_free), "%llu MB free", b / 1000000ULL);
	}
}

/* What's in a memory card slot, for the header (only looks: doesn't mount
 * the card). Returns 1 for a memory card, 0 for an empty slot, -1 for
 * something else in it. */
static int slot_info(s32 slot, char *info, int n)
{
	s32 size = 0, sect = 0;
	s32 r = CARD_ProbeEx(slot, &size, &sect);
	if ((r == CARD_ERROR_READY || r == CARD_ERROR_BUSY) && size > 0) { snprintf(info, n, "%d blocks", size * 16 - 5); return 1; }
	if (r == CARD_ERROR_BUSY) { snprintf(info, n, "Checking\x85"); return 1; }
	if (r == CARD_ERROR_WRONGDEVICE) { snprintf(info, n, "Not a memory card"); return -1; }
	snprintf(info, n, "No card in Slot %c", slot == CARD_SLOTB ? 'B' : 'A');
	return 0;
}

/* The header: each memory card (both, when there's one in each slot), and
 * the SD card */
void UpdateMainCards(void)
{
	char name[2][32], info[2][48], tmp[48];
	int shown = 0;
	bool free_a = CUR_DEVICE != DEV_GCSDA, free_b = CUR_DEVICE != DEV_GCSDB;   /* an SD Gecko takes its slot */
	pick_card_slot();   /* (follows a card moved to the other slot) */
	for (s32 slot = CARD_SLOTA; slot <= CARD_SLOTB; slot++) {
		if (!(slot == CARD_SLOTA ? free_a : free_b)) continue;
		if (slot_info(slot, tmp, sizeof(tmp)) == 1) {
			snprintf(name[shown], sizeof(name[0]), "Memory Card %c", slot == CARD_SLOTB ? 'B' : 'A');
			snprintf(info[shown], sizeof(info[0]), "%s", tmp);
			shown++;
		}
	}
	if (shown == 0) {
		/* no memory card: say what's in the slot in use */
		snprintf(name[0], sizeof(name[0]), "Memory Card %c", MEM_CARD ? 'B' : 'A');
		slot_info(MEM_CARD, info[0], sizeof(info[0]));
		if (free_a && free_b && !strncmp(info[0], "No ", 3)) {
			snprintf(name[0], sizeof(name[0]), "Memory Card");
			snprintf(info[0], sizeof(info[0]), "No card in either slot");
		}
	}
	ui_main_cards(name[0], info[0], have_sd ? DeviceShortName(CUR_DEVICE) : "", have_sd ? sd_free : "");
	ui_main_card2(shown == 2 ? name[1] : "", shown == 2 ? info[1] : "");
}

/* ------------------------------------------------------------- settings */

static void settings_rows(void)
{
	char ov[16];
	if (ui_set.overscan) snprintf(ov, sizeof(ov), "%d%%", ui_set.overscan); else snprintf(ov, sizeof(ov), "Off");
	ui_overlay_row(0, "Resolution", ui_resolution_name(ui_set.resolution));
	ui_overlay_row(1, "Screen", ui_set.widescreen ? "16:9" : "4:3");
	ui_overlay_row(2, "Picture width", ui_set.full_width ? "Full" : "Standard");
	ui_overlay_row(3, "Overscan", ov);
	ui_overlay_row(4, "Done", NULL);
	ui_overlay_rows(5);
}

static void settings_help(int row)
{
	static const char *help[5] = {
		"Auto picks 480p when a component cable is connected.",
		"Choose 16:9 if your TV stretches the picture to fill the screen.",
		"Full fills the width of the TV picture. Standard leaves a border.",
		"Pulls everything in from the edges if your TV cuts them off.",
		"Keep these settings.",
	};
	ui_status((char *)help[row], row == 4 ? "A: Done" DOT "B: Done" : "Left/Right: Change" DOT "B: Done");
}

static void SettingsMenu(void)
{
	UiSettings before = ui_set;
	int row = 0;
	int confirm_old = -1;          /* the resolution to go back to if a new one isn't confirmed */
	u64 confirm_until = 0;
	int last_secs = -1;

	ui_overlay(UI_OVERLAY_SETTINGS, "Settings");
	settings_rows();
	ui_overlay_select(row);
	settings_help(row);
	WaitRelease();

	for (;;) {
		read_pad();
		if (confirm_old >= 0) {
			/* a new resolution: the TV might not show it, so it goes back
			 * on its own unless A is pressed */
			u64 now = ticks_to_millisecs(gettime());
			if (pad_down & PAD_BUTTON_A) {
				confirm_old = -1;
				settings_help(row);
				WaitRelease();
				continue;
			}
			if (now >= confirm_until) {
				ui_set.resolution = confirm_old;
				confirm_old = -1;
				ui_settings_changed();
				settings_rows();
				settings_help(row);
				continue;
			}
			int secs = (int)((confirm_until - now + 999) / 1000);
			if (secs != last_secs) {
				char l2[64];
				last_secs = secs;
				snprintf(l2, sizeof(l2), "Going back to %s in %d\x85", ui_resolution_name(confirm_old), secs);
				ui_status("Press A to keep this picture.", l2);
			}
			continue;
		}

		u32 step = dpad_step();
		if (step & PAD_BUTTON_UP) { row = (row + 4) % 5; ui_overlay_select(row); settings_help(row); }
		else if (step & PAD_BUTTON_DOWN) { row = (row + 1) % 5; ui_overlay_select(row); settings_help(row); }
		else if (step & (PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT)) {
			int d = (step & PAD_BUTTON_RIGHT) ? 1 : -1;
			switch (row) {
			case 0: {
				int old = ui_set.resolution;
				ui_set.resolution = (ui_set.resolution + UI_RES_COUNT + d) % UI_RES_COUNT;
				ui_settings_changed();
				confirm_old = old;
				confirm_until = ticks_to_millisecs(gettime()) + 10000;
				last_secs = -1;
				break;
			}
			case 1: ui_set.widescreen ^= 1; ui_settings_changed(); break;
			case 2: ui_set.full_width ^= 1; ui_settings_changed(); break;
			case 3:
				ui_set.overscan += d;
				if (ui_set.overscan < 0) ui_set.overscan = 0;
				if (ui_set.overscan > UI_OVERSCAN_MAX) ui_set.overscan = UI_OVERSCAN_MAX;
				break;
			}
			settings_rows();
		}
		else if ((pad_down & PAD_BUTTON_B) || ((pad_down & PAD_BUTTON_A) && row == 4)) break;
	}

	ui_overlay(UI_OVERLAY_NONE, "");
	if (memcmp(&before, &ui_set, sizeof(ui_set))) {
		if (!have_sd) WaitPrompt("No SD card: these settings last until GCMM exFAT closes.");
		else if (!ui_settings_save()) WaitPrompt("Couldn't save the settings to the SD card. They last until GCMM exFAT closes.");
	}
	WaitRelease();
}

/* ------------------------------------------------------------ main screen */

static int main_tile = 0;
static char main_line[96] = "";
#define EXIT_HOLD_FRAMES 45

/* the line along the bottom of the main screen: how many saves there are,
 * and how many have no backup yet (reads the memory card's list) */
void MainSummaryRefresh(void)
{
	int total = 0;
	int fresh = have_sd ? saves_count_new(MEM_CARD, &total) : -1;
	if (CARD_Probe(MEM_CARD) <= 0) snprintf(main_line, sizeof(main_line), "Put a memory card in Slot %c to begin.", MEM_CARD ? 'B' : 'A');
	else if (!have_sd) snprintf(main_line, sizeof(main_line), "There's no SD card: choose one in Card tools.");
	else if (fresh < 0 || !total) snprintf(main_line, sizeof(main_line), "There are no saves on this memory card.");
	else if (!fresh) snprintf(main_line, sizeof(main_line), "%d save%s on the memory card, all of them backed up.", total, total == 1 ? "" : "s");
	else snprintf(main_line, sizeof(main_line), "%d save%s on the memory card, %d of them not backed up yet.", total, total == 1 ? "" : "s", fresh);
}

static void main_status(void)
{
	ui_action(NULL, 0);
	ui_progress(-1);
	writeStatusBar(main_line, "Pick one with the stick and press A. START: Settings.");
}

/* Card tools: a page of its own (like Back up and Restore) with the jobs
 * that are needed less often. After a job it comes back here; B goes back
 * to the main screen. */
static bool in_tools = false;
static int tools_sel = 0;

static void tools_status(void)
{
	static const char *help[UI_TOOLS_COUNT] = {
		"Copies everything on the memory card to one file on the SD card.",
		"Puts a whole-card copy from the SD card back on the memory card.",
		"Choose saves to delete from the memory card.",
		"Erases everything on the memory card.",
		"Use another SD card adapter.",
		"Picture size and position, kept on the SD card.",
	};
	ui_action(NULL, 0);
	ui_progress(-1);
	writeStatusBar((char *)help[tools_sel], "A: Choose" DOT "B: Back");
}

/* Returns the job to do, or -1 for back to the main screen */
static int CardTools(void)
{
	static const int codes[UI_TOOLS_COUNT] = { 700, 800, 200, 900, 1000, 0 };
	in_tools = true;
	UpdateMainCards();
	ui_screen(UI_TOOLS);
	ui_tools_select(tools_sel);
	tools_status();
	WaitRelease();
	for (;;) {
		read_pad();
		u32 step = dpad_step();
		int sel = tools_sel;
		if (step & PAD_BUTTON_UP) sel = (sel + UI_TOOLS_COUNT - 2) % UI_TOOLS_COUNT;
		else if (step & PAD_BUTTON_DOWN) sel = (sel + 2) % UI_TOOLS_COUNT;
		else if (step & (PAD_BUTTON_LEFT | PAD_BUTTON_RIGHT)) sel ^= 1;
		if (sel != tools_sel) { tools_sel = sel; ui_tools_select(sel); tools_status(); }
		if (pad_down & PAD_BUTTON_B) { in_tools = false; break; }
		if (pad_down & PAD_BUTTON_A) {
			if (codes[tools_sel]) { WaitRelease(); return codes[tools_sel]; }
			SettingsMenu();     /* (a panel over this page) */
			tools_status();
		}
	}
	WaitRelease();
	return -1;
}

/****************************************************************************
* SelectMode
* 200 delete, 300 back up, 400 restore, 500 exit, 700 whole-card back up,
* 800 whole-card restore, 900 format, 1000 choose device
****************************************************************************/
int SelectMode ()
{
	int ret = 0, poll = 0, start_frames = 0;
	int last_probe = -99;

	UpdateTitleInfo();
	if (in_tools) {
		/* back from one of the Card tools: to that page again */
		int r = CardTools();
		if (r > 0) return r;
	}
	ui_screen(UI_MAIN);
	ui_main_select(main_tile);
	main_status();
	WaitRelease();

	while (!ret)
	{
		read_pad();
		if (++poll >= 60) {
			poll = 0;
			UpdateMainCards();
			/* a card put in or taken out: the line at the bottom follows */
			int p = CARD_Probe(MEM_CARD) > 0;
			if (p != last_probe) {
				if (last_probe != -99) { MainSummaryRefresh(); main_status(); }
				last_probe = p;
			}
		}

		/* START: a tap opens the settings, holding it exits */
		if (pad_held & PAD_BUTTON_START) {
			start_frames++;
			if (start_frames == 15) writeStatusBar("Keep holding START to exit\x85", "");
			if (start_frames >= EXIT_HOLD_FRAMES) { ret = 500; break; }
			continue;
		} else if (start_frames) {
			int tapped = start_frames < 15;
			start_frames = 0;
			if (tapped) {
				ui_main_select(-1);
				SettingsMenu();
				ui_main_select(main_tile);
			}
			main_status();
			continue;
		}

		u32 step = dpad_step();
		if (step & PAD_BUTTON_LEFT) { main_tile = (main_tile + 2) % 3; ui_main_select(main_tile); }
		if (step & PAD_BUTTON_RIGHT) { main_tile = (main_tile + 1) % 3; ui_main_select(main_tile); }

		if (pad_down & PAD_BUTTON_A) {
			if (main_tile == 0) ret = 300;
			else if (main_tile == 1) ret = 400;
			else {
				int r = CardTools();
				if (r > 0) ret = r;
				else { ui_screen(UI_MAIN); ui_main_select(main_tile); main_status(); }
			}
		}
	}
	ui_main_select(-1);
	if (ret != 500) WaitRelease();
	return ret;
}

/* ------------------------------------------------------- choose a device */

/* The overlay listing the SD card devices. Returns the chosen DEV_ number. */
int ChooseDevice(const u8 *devices, int count)
{
	int sel = 0;
	if (!in_tools) { ui_screen(UI_MAIN); ui_main_select(-1); }   /* (from Card tools: over that page) */
	UpdateMainCards();
	ui_action(NULL, 0);
	ui_overlay(UI_OVERLAY_DEVICES, "Choose the SD card");
	for (int i = 0; i < count && i < UI_OVERLAY_ROWS; i++) ui_overlay_row(i, DeviceName(devices[i]), NULL);
	ui_overlay_rows(count);
	ui_overlay_select(sel);
	writeStatusBar("Which SD card adapter should GCMM exFAT use?", "A: Choose");
	WaitRelease();
	for (;;) {
		read_pad();
		u32 step = dpad_step();
		if (step & PAD_BUTTON_UP) { sel = (sel + count - 1) % count; ui_overlay_select(sel); }
		if (step & PAD_BUTTON_DOWN) { sel = (sel + 1) % count; ui_overlay_select(sel); }
		if (pad_down & PAD_BUTTON_A) break;
	}
	ui_overlay(UI_OVERLAY_NONE, "");
	writeStatusBar("", "");
	WaitRelease();
	return devices[sel];
}

/* ============================================================ save lists */

#define ROWS UI_LIST_ROWS

static int chosen(int *blocks)
{
	int n = 0, b = 0;
	for (int i = 0; i < save_count; i++)
		if (save_rows[i].checked && !save_rows[i].folder) { n++; b += save_rows[i].blocks; }
	if (blocks) *blocks = b;
	return n;
}

static void pick_status(const PickSpec *ps)
{
	char l1[96], act[48];
	int blocks, n = ps->verb ? chosen(&blocks) : 0;
	if (!n) {
		ui_action(NULL, 0);
		writeStatusBar((char *)ps->hint, (char *)ps->where);
		return;
	}
	snprintf(l1, sizeof(l1), "%d save%s chosen" DOT "%d block%s", n, n == 1 ? "" : "s", blocks, blocks == 1 ? "" : "s");
	snprintf(act, sizeof(act), "%s %d save%s", ps->verb, n, n == 1 ? "" : "s");
	writeStatusBar(l1, (char *)ps->where);
	ui_action(act, 'X');
}

static void pick_page(int offset)
{
	char blocks[16];
	saves_look(offset, ROWS);   /* (SD rows: looked at as they're shown) */
	ui_list_clear();
	for (int i = 0; i < ROWS; i++) {
		int n = offset + i;
		if (n >= save_count) { ui_list_icon(i, NULL); continue; }
		SaveRow *r = &save_rows[n];
		blocks[0] = 0;
		if (r->blocks > 0 && !r->folder) snprintf(blocks, sizeof(blocks), "%d block%s", r->blocks, r->blocks == 1 ? "" : "s");
		ui_list_row(i, r->title, r->detail, blocks, r->folder ? UI_ST_NONE : r->status, r->checked, r->folder);
		ui_list_icon(i, r->has_icon ? r->icon : NULL);
	}
	ui_list_scroll(offset, ROWS, save_count);
}

/****************************************************************************
* PickList
*
* The list of save_rows, with tick boxes when ps->verb is set: A ticks a save
* (or opens a folder), Y ticks them all (or none), X does the job with the
* ticked ones. Without tick boxes A chooses a row. Returns PICK_GO (X),
* PICK_OPEN (A on a folder, or a row chosen; *sel says which) or PICK_BACK.
****************************************************************************/
int PickList(const PickSpec *ps, int *sel)
{
	int selection = *sel, offset = -1;
	if (selection < 0 || selection >= save_count) selection = 0;
	ui_screen(UI_LIST);
	ui_list_header(ps->title, ps->right);
	ui_list_checkable(ps->verb != NULL);
	ui_progress(-1);
	pick_status(ps);
	WaitRelease();
	for (;;)
	{
		int new_offset = (selection / ROWS) * ROWS;
		if (new_offset != offset) { offset = new_offset; pick_page(offset); }
		ui_list_select(save_count ? selection - offset : -1);

		read_pad();
		if (pad_down & PAD_BUTTON_B) { *sel = selection; WaitRelease(); return PICK_BACK; }
		if ((pad_down & PAD_BUTTON_X) && ps->verb && chosen(NULL)) { *sel = selection; ui_action(NULL, 0); WaitRelease(); return PICK_GO; }
		if ((pad_down & PAD_BUTTON_Y) && ps->verb) {
			/* all of them, or none if they all are */
			bool all = true;
			for (int i = 0; i < save_count; i++) if (!save_rows[i].folder && !save_rows[i].checked) all = false;
			for (int i = 0; i < save_count; i++) if (!save_rows[i].folder) save_rows[i].checked = !all;
			pick_page(offset);
			pick_status(ps);
		}
		if ((pad_down & PAD_BUTTON_A) && save_count) {
			SaveRow *r = &save_rows[selection];
			if (r->folder || !ps->verb) { *sel = selection; WaitRelease(); return PICK_OPEN; }
			r->checked = !r->checked;
			pick_page(offset);
			pick_status(ps);
		}

		u32 step = dpad_step();
		if (!save_count || !step) continue;
		int jump = (step & PAD_BUTTON_DOWN) ? 1 : (step & PAD_BUTTON_UP) ? -1 :
		           (step & PAD_BUTTON_RIGHT) ? ROWS : -ROWS;
		int next = selection + jump;
		if (jump == 1 || jump == -1) {
			/* up and down wrap around */
			if (next >= save_count) next = 0;
			if (next < 0) next = save_count - 1;
		} else {
			/* left and right move a page, stopping at the ends */
			if (next >= save_count) next = save_count - 1;
			if (next < 0) next = 0;
		}
		selection = next;
	}
}
