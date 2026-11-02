/****************************************************************************
* freetype.c - GCMM's screens: the main screen, the save lists, the prompts
* along the bottom and the settings.
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

void UpdateTitleInfo(void)
{
	char txt[64];
	if (CUR_DEVICE && have_sd)
		snprintf(txt, sizeof(txt), "%s" DOT "%s", appversion, DeviceShortName(CUR_DEVICE));
	else
		snprintf(txt, sizeof(txt), "%s" DOT "No SD card", appversion);
	ui_title_info(txt);
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

/* What's in the memory card slot (only looks: doesn't mount the card) */
static void UpdateMainCards(void)
{
	char mc_name[32], mc_info[48];
	s32 size = 0, sect = 0;
	pick_card_slot();   /* (follows a card moved to the other slot) */
	s32 r = CARD_ProbeEx(MEM_CARD, &size, &sect);
	snprintf(mc_name, sizeof(mc_name), "Memory Card %c", MEM_CARD ? 'B' : 'A');
	if ((r == CARD_ERROR_READY || r == CARD_ERROR_BUSY) && size > 0)
		snprintf(mc_info, sizeof(mc_info), "%d blocks", size * 16 - 5);
	else if (r == CARD_ERROR_WRONGDEVICE)
		snprintf(mc_info, sizeof(mc_info), "Not a memory card");
	else if (r == CARD_ERROR_BUSY)
		snprintf(mc_info, sizeof(mc_info), "Checking\x85");
	else
		snprintf(mc_info, sizeof(mc_info), "No card in Slot %c", MEM_CARD ? 'B' : 'A');
	ui_main_cards(mc_name, mc_info, have_sd ? DeviceShortName(CUR_DEVICE) : "", have_sd ? sd_free : "");
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
		if (have_sd) ui_settings_save();
		else WaitPrompt("No SD card: these settings last until GCMM closes.");
	}
	WaitRelease();
}

/* ------------------------------------------------------------ main screen */

static int main_row = 0;
#define EXIT_HOLD_FRAMES 45

static void main_status(void)
{
	writeStatusBar("Back up, restore or delete the saves on your memory card.",
	               "A: Open" DOT "START: Settings" DOT "Hold START: Exit to Launcher");
}

/****************************************************************************
* SelectMode
* 200 delete, 300 back up, 400 restore, 500 exit, 700 raw back up,
* 800 raw restore, 900 format, 1000 choose device
****************************************************************************/
int SelectMode ()
{
	int ret = 0, poll = 0, start_frames = 0;

	UpdateTitleInfo();
	UpdateMainCards();
	ui_screen(UI_MAIN);
	ui_main_select(main_row);
	main_status();
	WaitRelease();

	while (!ret)
	{
		read_pad();
		if (++poll >= 60) { poll = 0; UpdateMainCards(); }

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
				ui_main_select(main_row);
			}
			main_status();
			continue;
		}

		u32 step = dpad_step();
		if (step & (PAD_BUTTON_UP | PAD_BUTTON_DOWN)) { main_row ^= 1; ui_main_select(main_row); }

		if (pad_held & PAD_TRIGGER_L) {
			if (pad_down & PAD_BUTTON_Y) ret = 700;       /* back up the whole card */
			else if (pad_down & PAD_BUTTON_X) ret = 800;  /* restore the whole card */
			else if (pad_down & PAD_TRIGGER_Z) ret = 900; /* format */
		}
		else if (pad_down & PAD_BUTTON_A) ret = main_row ? 400 : 300;
		else if (pad_down & PAD_BUTTON_Y) ret = 300;
		else if (pad_down & PAD_BUTTON_X) ret = 400;
		else if (pad_down & PAD_TRIGGER_Z) ret = 200;
		else if (pad_down & PAD_TRIGGER_R) ret = 1000;
	}
	if (ret == 300) main_row = 0;
	if (ret == 400) main_row = 1;
	ui_main_select(-1);
	if (ret != 500) WaitRelease();
	return ret;
}

/* ------------------------------------------------------- choose a device */

/* The overlay listing the SD card devices. Returns the chosen DEV_ number. */
int ChooseDevice(const u8 *devices, int count)
{
	int sel = 0;
	ui_screen(UI_MAIN);
	ui_main_select(-1);
	UpdateMainCards();
	ui_overlay(UI_OVERLAY_DEVICES, "Choose a device");
	for (int i = 0; i < count && i < UI_OVERLAY_ROWS; i++) ui_overlay_row(i, DeviceName(devices[i]), NULL);
	ui_overlay_rows(count);
	ui_overlay_select(sel);
	writeStatusBar("Which SD card should GCMM use?", "A: Choose");
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

static char list_mode[32];
static int page_kind[PAGESIZE];

/* Switches to the list screen for one of the modes (Back up, Restore...) */
void ListBegin(const char *mode_name)
{
	snprintf(list_mode, sizeof(list_mode), "%s", mode_name);
	ui_list_clear();
	ui_info_clear();
	ui_list_scroll(0, 0, 0);
	ui_list_header("", list_mode);
	ui_screen(UI_LIST);
	offsetchanged = true;
}

static int is_folder(int i)
{
	if (!displaypath) return 0;   /* the memory card has no folders */
	char folder[1024];
	snprintf(folder, sizeof(folder), "%s:/%s/%s", fatpath, currFolder, (char*)filelist[i]);
	return isdir_sd(folder) == 1;
}

static void hexline(char *out, size_t n, const u8 *b, int count)
{
	out[0] = 0;
	for (int i = 0; i < count; i++) snprintf(out + i * 2, n - i * 2, "%02X", b[i]);
}

/* The card image chosen for a whole-card restore, beside the card in the slot */
static void showCardInfo(int sel, int folder)
{
	char txt[64], a[32], b[32];
	int i;

	ui_info_clear();
	if (folder) {
		ui_info_title((char*)filelist[sel]);
		ui_info_folder(true);
		ui_info_row(0, "Folder", "Press A to open");
		return;
	}

	/* block 1 of the image goes into cardheader */
	SDLoadCardImageHeader((char*)filelist[sel]);
	u64 serial1[4];
	for (i = 0; i < 4; i++) memcpy(&serial1[i], (u8*)&cardheader + (8*i), sizeof(u64));
	u64 serialA = serial1[0]^serial1[1]^serial1[2]^serial1[3];
	getserial(imageserial);

	ui_info_title("Card image");
	hexline(a, sizeof(a), imageserial, 6);
	hexline(b, sizeof(b), imageserial + 6, 6);
	ui_info_row(0, "Flash ID", a);
	ui_info_row(1, "", b);
	snprintf(txt, sizeof(txt), "%016llX", serialA);
	ui_info_row(2, "Serial", txt);
	snprintf(txt, sizeof(txt), "%d blocks", (cardheader.SizeMb[1]*16)-5);
	ui_info_row(3, "Size", txt);

	/* mounting the card makes the SRAM's copy of its flash ID readable */
	u64 cardserialno = Card_SerialNo(MEM_CARD);
	sramex = __SYS_LockSramEx();
	__SYS_UnlockSramEx(0);

	hexline(a, sizeof(a), sramex->flash_id[MEM_CARD], 6);
	hexline(b, sizeof(b), sramex->flash_id[MEM_CARD] + 6, 6);
	ui_info_row(4, MEM_CARD ? "Card B ID" : "Card A ID", a);
	ui_info_row(5, "", b);
	snprintf(txt, sizeof(txt), "%016llX", cardserialno);
	ui_info_row(6, "Serial", txt);
	snprintf(txt, sizeof(txt), "%d blocks", (memsize*16)-5);
	ui_info_row(7, "Size", txt);
}

static void comment_line(char *out, int from)
{
	int j;
	for (j = 0; j < 32; j++) {
		char c = (char)CommentBuffer[from + j];
		if (c == 0 || c == 10) break;
		out[j] = c;
	}
	out[j] = 0;
	/* trailing spaces */
	while (j > 0 && out[j - 1] == ' ') out[--j] = 0;
}

static void showSaveInfo(int sel, int folder)
{
	char gamecode[5], company[3], txt[96];

	ui_info_clear();
	if (folder) {
		ui_info_title((char*)filelist[sel]);
		ui_info_folder(true);
		ui_info_row(0, "Folder", "Press A to open");
		return;
	}

	if (mode == RESTORE_MODE)
	{
		SDLoadMCImageHeader((char*)filelist[sel]);
		memcpy(company, gci.company, 2);
		memcpy(gamecode, gci.gamecode, 4);
	}
	else
	{
		memcpy(company, CardList[sel].company, 2);
		memcpy(gamecode, CardList[sel].gamecode, 4);
		CardReadFileHeader(MEM_CARD, sel);
	}
	company[2] = gamecode[4] = 0;

	/* the banner (the icon follows from ShowSelector, a frame at a time) */
	if (SDCARD_GetBannerFmt(gci.banner_fmt) == CARD_BANNER_RGB)
		bannerloadRGB(bannerdata);
	else if (SDCARD_GetBannerFmt(gci.banner_fmt) == CARD_BANNER_CI)
		bannerloadCI(bannerdataCI, tlutbanner);

	char comment1[33], comment2[33];
	comment_line(comment1, 0);
	comment_line(comment2, 32);
	if (comment1[0]) ui_info_title(comment1);
	else { snprintf(txt, sizeof(txt), "%s%s", gamecode, company); ui_info_title(txt); }

	int r = 0;
	if (comment2[0]) ui_info_row(r++, "Details", comment2);
	snprintf(txt, sizeof(txt), "%s%s", gamecode, company);
	ui_info_row(r++, "Game ID", txt);

	/* gci.last_modified counts seconds from Jan 1, 2000 */
	u32 t = gci.last_modified + 946684800;
	static const char monthstr[12][4] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun", "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
	/* Fliegel & van Flandern (1968), Communications of the ACM 11(10) */
	int I, J, K, L, N;
	L = t / 86400 + 2509157;
	N = 4 * L / 146097;
	L = L - (146097 * N + 3) / 4;
	I = 4000 * (L + 1) / 1461001;
	L = L - 1461 * I / 4 + 31;
	J = 80 * L / 2447;
	K = L - 2447 * J / 80;
	L = J / 11;
	J = J + 2 - 12 * L;
	I = 100 * (N - 49) + I + L;
	int year = I, month = J, day = K;
	int min = (t / 60) % 60, hour = (t / 3600) % 24;
	if (month < 1 || month > 12) month = 1;
	snprintf(txt, sizeof(txt), "%s %d, %04d  %02d:%02d", monthstr[month-1], day, year, hour, min);
	ui_info_row(r++, "Saved", txt);

	snprintf(txt, sizeof(txt), "%d (%d free)", gci.length, FreeBlocks(MEM_CARD));
	ui_info_row(r++, "Blocks", txt);

	snprintf(txt, sizeof(txt), "%s%s%s",
	         (gci.permission & 4) ? "Public" : "Private",
	         (gci.permission & 8) ? ", no copying" : "",
	         (gci.permission & 16) ? ", no moving" : "");
	ui_info_row(r++, "Access", txt);
	snprintf(txt, sizeof(txt), "%d", gci.copy_times);
	ui_info_row(r++, "Copied", txt);
	ui_info_row(r++, "File", (char*)filelist[sel]);
}

/* the list's rows for the page starting at offset */
static void ShowPage(int offset)
{
	ui_list_clear();
	for (int i = 0; i < PAGESIZE; i++) {
		int n = offset + i;
		if (n >= (int)maxfile) { page_kind[i] = -1; continue; }
		page_kind[i] = is_folder(n) ? UI_ITEM_FOLDER : (displaypath ? UI_ITEM_FILE : UI_ITEM_SAVE);
		ui_list_row(i, (char*)filelist[n], page_kind[i]);
	}
}

static void ShowFiles (int offset, int selection, int saveinfo)
{
	char path[300];
	if (displaypath) snprintf(path, sizeof(path), "%s:/%s/", fatpath, currFolder);
	else snprintf(path, sizeof(path), "Memory Card %c", MEM_CARD ? 'B' : 'A');
	ui_list_header(path, list_mode);

	if (offsetchanged) {
		offsetchanged = false;
		ShowPage(offset);
	}
	ui_list_select(selection - offset);
	ui_list_scroll(offset, PAGESIZE, maxfile);

	int folder = page_kind[selection - offset] == UI_ITEM_FOLDER;
	if (saveinfo) showSaveInfo(selection, folder);
	else showCardInfo(selection, folder);
}

/****************************************************************************
* ShowSelector
*
* Lets the user choose a file to back up or restore. Returns its index, or
* -1 with cancel set (B) or doall set (R).
* saveinfo 1: shows the save's details (saves)
* saveinfo 0: shows the card image's details (whole-card restore)
****************************************************************************/
int ShowSelector (int saveinfo)
{
	int redraw = 1;
	int offset = 0;
	int selection = 0;
	int currframe = 0;
	int i;

	offsetchanged = true;
	WaitRelease();

	for (;;)
	{
		if (redraw)
		{
			if (saveinfo) {
				memset((u8*)bannerdata, 0, CARD_BANNER_W*CARD_BANNER_H*2);
				memset((u8*)bannerdataCI, 0, CARD_BANNER_W*CARD_BANNER_H);
				memset((u8*)icondata, 0, 8*1024);
				memset((u8*)icondataRGB, 0, 8*1024*2);
				memset((u8*)tlut, 0, 9*512);
				memset((u8*)tlutbanner, 0, 512);
			}
			int new_offset = (selection / PAGESIZE) * PAGESIZE;
			if (new_offset != offset) { offset = new_offset; offsetchanged = true; }
			ShowFiles (offset, selection, saveinfo);
			redraw = 0;
		}

		/* the save's animated icon */
		if (lastframe && saveinfo && page_kind[selection - offset] != UI_ITEM_FOLDER)
		{
			currframe = retraceCount % lastframe;
			for (i = 0; i < 2 * CARD_MAXICONS - 2; ++i)
				if (currframe < frametable[i]) break;
			currframe = i;
			int fmt = SDCARD_GetIconFmt(gci.icon_fmt, iconindex[currframe]);
			if (fmt == 1) iconloadCI(icondata[iconindex[currframe]], tlut[8]);           /* shared palette */
			else if (fmt == 3) iconloadCI(icondata[iconindex[currframe]], tlut[iconindex[currframe]]);
			else if (fmt == 2) iconloadRGB(icondataRGB[iconindex[currframe]]);
		}

		read_pad();

		if (pad_down & PAD_BUTTON_B)
		{
			offsetchanged = true;
			cancel = 1;
			lastframe = 0;
			return -1;
		}
		if (pad_down & PAD_TRIGGER_R)
		{
			doall = 1;
			lastframe = 0;
			return -1;
		}
		if (pad_down & PAD_BUTTON_A)
		{
			lastframe = 0;
			return selection;
		}

		u32 step = dpad_step();
		if (!maxfile || !step) continue;
		int jump = (step & PAD_BUTTON_DOWN) ? 1 : (step & PAD_BUTTON_UP) ? -1 :
		           (step & PAD_BUTTON_RIGHT) ? PAGESIZE : -PAGESIZE;
		int next = selection + jump;
		if (jump == 1 || jump == -1) {
			/* up and down wrap around */
			if (next >= (int)maxfile) next = 0;
			if (next < 0) next = maxfile - 1;
		} else {
			/* left and right move a page, stopping at the ends */
			if (next >= (int)maxfile) next = maxfile - 1;
			if (next < 0) next = 0;
		}
		if (next != selection) { selection = next; redraw = 1; }
	}
}
