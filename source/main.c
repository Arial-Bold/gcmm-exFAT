/****************************************************************************
* GCMM exFAT - GameCube Memory Manager
* based on libOGC Memory Card Backup by askot
* delete support + saves information made by dsbomb
* Gui original design by dsbomb
* Gui adds and user interaction by justb
* Banner/Icon display updates by Dronesplitter
* Updates, raw mode and devices by suloku
* exFAT support and the System look in this fork
*****************************************************************************/
#include <gccore.h>
#include <ogcsys.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <malloc.h>
#include <unistd.h>
#include <sys/dir.h>
#include <sys/time.h>
#include <ogc/lwp_watchdog.h>
#include <fat.h>            /* libdvm: FAT, FAT32 and exFAT */
#include <sdcard/gcsd.h>

#include "mcard.h"
#include "raw.h"
#include "sdsupp.h"
#include "freetype.h"
#include "bitmap.h"
#include "ui.h"
#include "saves.h"
#include "aram/sidestep.h"
#include "net/storage.h"
#include "dolload.h"
#include "sound.h"
#include <dirent.h>
#include <strings.h>
#include "net/update.h"

#define PSOSDLOADID 0x7c6000a6
//Comment FLASHIDCHECK to allow writing any image to any mc. This will corrupt official cards.
#define FLASHIDCHECK

#define SDGECKOA_PATH "sda"
#define SDGECKOB_PATH "sdb"
#define SD2SP2_PATH "sdc"
#define GCLOADER_PATH "gcl"
char fatpath[4];

const char appversion[] = "v1.1";
int mode;
int cancel;
int doall;
u32 retraceCount;

extern u8 filelist[1024][1024];
extern bool offsetchanged;
extern u8 currFolder[260];
extern int folderCount;
extern int displaypath;

s32 MEM_CARD = CARD_SLOTB;
extern syssramex *sramex;
extern syssramex* __SYS_LockSramEx();
extern u32 __SYS_UnlockSramEx(u32 write);
extern u8 imageserial[12];

#define NOFAT_MSG "There's no SD card. Choose one in Card tools."

static void updatePAD(u32 retrace)
{
	retraceCount = retrace;
	PAD_ScanPads();
}

/* Definitions in sdsupp.h
#define DEV_NUM 	0
#define DEV_GCSDA 	1
#define DEV_GCSDB 	2
#define DEV_GCSDC 	3
#define DEV_GCODE 	4
*/
u8 DEVICES [DEV_TOTAL+1];
bool have_sd;
u8 CUR_DEVICE; //Current device index

static bool probe(DISC_INTERFACE *io)
{
	bool ok = io->startup(io);
	io->shutdown(io);
	return ok;
}

static void detect_devices(){
	int i;
	for (i=0;i<=DEV_TOTAL;i++) DEVICES[i]=0;

	if (probe(&__io_gcsda)) { DEVICES[DEV_GCSDA] = DEV_AVAIL; DEVICES[DEV_NUM]++; }
	if (probe(&__io_gcsdb)) { DEVICES[DEV_GCSDB] = DEV_AVAIL; DEVICES[DEV_NUM]++; }
	if (probe(&__io_gcsd2)) { DEVICES[DEV_GCSDC] = DEV_AVAIL; DEVICES[DEV_NUM]++; }
	if (probe(&__io_gcode)) { DEVICES[DEV_GCODE] = DEV_AVAIL; DEVICES[DEV_NUM]++; }
}

static DISC_INTERFACE *device_io(int device)
{
	switch (device) {
	case DEV_GCSDA: return &__io_gcsda;
	case DEV_GCSDB: return &__io_gcsdb;
	case DEV_GCSDC: return &__io_gcsd2;
	case DEV_GCODE: return &__io_gcode;
	default: return NULL;
	}
}

static const char *device_mount_name(int device)
{
	switch (device) {
	case DEV_GCSDA: return SDGECKOA_PATH;
	case DEV_GCSDB: return SDGECKOB_PATH;
	case DEV_GCSDC: return SD2SP2_PATH;
	case DEV_GCODE: return GCLOADER_PATH;
	default: return "";
	}
}

/* Mounts the SD card in the device (FAT, FAT32 or exFAT) */
static bool initFAT(int device)
{
	char msg[128];
	DISC_INTERFACE *io = device_io(device);
	ShowAction("Opening the SD card\x85");
	if (!io || DEVICES[device] != DEV_AVAIL){
		sprintf (msg, "Couldn't open the device (%d, %d).", device, DEVICES[device]);
		WaitPrompt(msg);
		return false;
	}
	io->startup(io);
	if (!io->isInserted(io))
	{
		sprintf(msg, "There's no SD card in the %s.", DeviceName(device));
		WaitPrompt(msg);
		return false;
	}
	if (!fatMountSimple (device_mount_name(device), io))
	{
		sprintf(msg, "Couldn't read the SD card in the %s. Is it FAT32 or exFAT?", DeviceName(device));
		WaitPrompt(msg);
		return false;
	}
	DEVICES[device] = DEV_MOUNTED;
	sprintf(fatpath, "%s", device_mount_name(device));
	ShowAction("");
	return true;
}

void deinitFAT()
{
	//First unmount all the devs...
	if (fatpath[0]) fatUnmount (fatpath);
	fatpath[0] = '\0';
	have_sd = 0;
	CUR_DEVICE = 0;
	//...and then shut them down
	for (int d = DEV_GCSDA; d <= DEV_GCODE; d++) {
		if (DEVICES[d] == DEV_MOUNTED) {
			DISC_INTERFACE *io = device_io(d);
			io->shutdown(io);
			DEVICES[d] = DEV_AVAIL;
		}
	}
}

u8 skip_selector = 1;
int device_select()
{
	u8 list[4];
	int count = 0, i;

	//If there are no devices skip
	if (!DEVICES[DEV_NUM]){
		ui_screen(UI_MAIN);
		WaitPrompt (NOFAT_MSG);
		return 0;
	}
	//Only one device was detected, so mount it, but only on first boot
	if (DEVICES[DEV_NUM] == 1 && skip_selector){
		skip_selector = 0;
		for (i=1;i<=DEV_TOTAL;i++)
			if (DEVICES[i]) return i;
	}
	skip_selector = 0;

	static const u8 order[4] = { DEV_GCSDC, DEV_GCSDA, DEV_GCSDB, DEV_GCODE };
	for (i = 0; i < 4; i++)
		if (DEVICES[order[i]]) list[count++] = order[i];
	return ChooseDevice(list, count);
}

/* The settings are kept in one place, MCBACKUP/gcmm.ini on the SD card,
 * however GCMM exFAT was started (a loader doesn't always say where the .dol
 * is, so a file beside it could be missed next time). An older settings.ini
 * beside the .dol is read if there's no gcmm.ini yet. */
static void find_settings(int argc, char *argv[])
{
	char path[300], old[300];
	path[0] = old[0] = 0;
	if (!have_sd) { ui_settings_path("", ""); return; }
	if (argc > 0 && argv && argv[0]) {
		const char *p = strstr(argv[0], ":/");
		const char *slash = strrchr(argv[0], '/');
		if (p && slash && slash > p + 1) {
			char dir[256];
			int n = (int)(slash - (p + 2));
			if (n > 0 && n < (int)sizeof(dir)) {
				memcpy(dir, p + 2, n);
				dir[n] = 0;
				snprintf(old, sizeof(old), "%s:/%s/settings.ini", fatpath, dir);
			}
		}
	}
	snprintf(path, sizeof(path), "%s:/%s/gcmm.ini", fatpath, MCSAVES);
	ui_settings_path(path, old);
}

/* Is there a memory card in this slot? (only looks: doesn't mount it) */
static bool slot_has_card(s32 slot, bool or_busy)
{
	s32 r = CARD_ProbeEx(slot, NULL, NULL);
	return r == CARD_ERROR_READY || (or_busy && r == CARD_ERROR_BUSY);
}

/* With an SD2SP2, a GC Loader or no SD card, both memory card slots are
 * free: if the slot in use is empty and the other has a card, use that one.
 * (An SD Gecko takes one slot, so then the card can only be in the other.) */
void pick_card_slot(void)
{
	if (CUR_DEVICE == DEV_GCSDA || CUR_DEVICE == DEV_GCSDB) return;
	s32 other = MEM_CARD == CARD_SLOTA ? CARD_SLOTB : CARD_SLOTA;
	if (slot_has_card(MEM_CARD, true)) return;
	if (slot_has_card(other, false)) MEM_CARD = other;
}

/* After choosing a device: mount it and pick the memory card slot */
static void use_device(void)
{
	if (CUR_DEVICE) have_sd = initFAT(CUR_DEVICE);
	//The memory card goes in the other slot to an SD Gecko
	if (CUR_DEVICE == DEV_GCSDB) MEM_CARD = CARD_SLOTA;
	else if (CUR_DEVICE == DEV_GCSDA) MEM_CARD = CARD_SLOTB;
	else MEM_CARD = CARD_SLOTA;   /* both slots free: start with A (pick_card_slot moves to B if that's where the card is) */
	pick_card_slot();
	UpdateTitleInfo();
	UpdateSDInfo();
}

static char slot_letter(void) { return MEM_CARD ? 'B' : 'A'; }

/****************************************************************************
* Back up saves from the memory card to the SD card
*
* The saves on the card, each marked New, Changed or Backed up against the
* copies in MCBACKUP; the new and changed ones start out ticked. X backs up
* the ticked ones.
****************************************************************************/
static bool card_present(void);

static void slot_line(char *out, size_t n, const char *fmt)
{
	snprintf(out, n, fmt, slot_letter());
}

void SD_BackupMode ()
{
	char buffer[128], where[64];
	int sel = 0;

	if (!card_present()) return;
	ui_screen(UI_LIST);
	ui_list_clear();
	ui_list_header("Back up  \x95  choose the saves", "");
	int n = saves_load_card(MEM_CARD);
	if (!n)
	{
		WaitPrompt ("There are no saves on this memory card.");
		return;
	}
	for (int i = 0; i < n; i++)
		save_rows[i].checked = save_rows[i].status == UI_ST_NEW || save_rows[i].status == UI_ST_CHANGED;

	char right[32];
	snprintf(right, sizeof(right), "%d save%s", n, n == 1 ? "" : "s");
	snprintf(where, sizeof(where), "They go to %s on the SD card.", MCSAVES);
	PickSpec ps = { "Back up  \x95  choose the saves", right, "Back up",
	                "Tick the saves to back up with A, or all of them with Y.", where };
	if (PickList(&ps, &sel) != PICK_GO) return;

	/* the ticked saves, one after another */
	int todo = 0, done = 0, k = 0;
	for (int i = 0; i < n; i++) if (save_rows[i].checked) todo++;
	for (int i = 0; i < n; i++)
	{
		SaveRow *r = &save_rows[i];
		if (!r->checked) continue;
		k++;
		snprintf(buffer, sizeof(buffer), "Backing up %s\x85", r->title);
		char l2[32];
		snprintf(l2, sizeof(l2), "%d of %d", k, todo);
		writeStatusBar(buffer, l2);
		ui_progress((float)(k - 1) / todo);
		if (CardReadFile(MEM_CARD, r->index) && SDSaveMCImage())
		{
			done++;
			r->status = UI_ST_BACKED_UP;
			r->checked = false;
			continue;
		}
		ui_progress(-1);
		snprintf(buffer, sizeof(buffer), "Couldn't back up %s. Carry on with the rest?", r->title);
		if (WaitPromptChoice(buffer, "Stop", "Carry on") != 1) break;
	}
	ui_progress(-1);
	UpdateSDInfo();
	if (done == todo) snprintf(buffer, sizeof(buffer), "Backed up %d save%s.", done, done == 1 ? "" : "s");
	else snprintf(buffer, sizeof(buffer), "Backed up %d of the %d saves.", done, todo);
	WaitPrompt(buffer);
}

/****************************************************************************
* Restore saves from the SD card to the memory card
*
* The folders and saves in MCBACKUP, each marked whether it's on the memory
* card already. A opens a folder (B goes back up); X puts the ticked saves
* on the card.
****************************************************************************/
static void go_up_folder(void)
{
	char* pos = strrchr( (char*)currFolder, '/' );
	if (pos) *pos = 0;
}

static void folder_title(char *out, size_t n, const char *what)
{
	snprintf(out, n, "%s  \x95  %s", what, (char *)currFolder);
}

void SD_RestoreMode ()
{
	char buffer[160], title[300], where[64];
	int sel = 0;

	sprintf((char*)currFolder, MCSAVES);
	slot_line(where, sizeof(where), "They go onto Memory Card %c.");
	for (;;)
	{
		ui_screen(UI_LIST);
		ui_list_clear();
		int files = saves_load_sd(1, MEM_CARD);
		if (files <= 0)
		{
			if (strcmp((char*)currFolder, MCSAVES) == 0)
			{
				WaitPrompt ("There are no saves in MCBACKUP on the SD card.");
				return;
			}
			WaitPrompt("This folder is empty.");
			go_up_folder();
			sel = 0;
			continue;
		}
		folder_title(title, sizeof(title), "Restore");
		char right[32];
		snprintf(right, sizeof(right), "%d item%s", files, files == 1 ? "" : "s");
		PickSpec ps = { title, right, "Restore", "Tick the saves to put back with A, or all of them with Y.", where };
		int r = PickList(&ps, &sel);
		if (r == PICK_BACK)
		{
			if (strcmp((char*)currFolder, MCSAVES) == 0) return;
			go_up_folder();
			sel = 0;
			continue;
		}
		if (r == PICK_OPEN)
		{
			/* into the folder */
			char folder[1300];
			snprintf(folder, sizeof(folder), "%s/%s", (char*)currFolder, (char*)filelist[save_rows[sel].index]);
			snprintf((char*)currFolder, sizeof(currFolder), "%s", folder);
			sel = 0;
			continue;
		}

		/* X: put the ticked ones on the card */
		if (!card_present()) continue;
		int todo = 0, there = 0;
		for (int i = 0; i < save_count; i++)
			if (save_rows[i].checked && !save_rows[i].folder) { todo++; if (save_rows[i].status == UI_ST_ON_CARD) there++; }
		card_replace = REPLACE_ASK;
		if (there)
		{
			snprintf(buffer, sizeof(buffer), there == 1 ? "One of these is on the memory card already. Replace it?"
			                                            : "%d of these are on the memory card already. Replace them?", there);
			card_replace = WaitPromptChoice(buffer, "Leave them", "Replace") == 1 ? REPLACE_YES : REPLACE_NO;
		}
		int done = 0, k = 0;
		for (int i = 0; i < save_count; i++)
		{
			SaveRow *s = &save_rows[i];
			if (!s->checked || s->folder) continue;
			k++;
			snprintf(buffer, sizeof(buffer), "Restoring %s\x85", s->title);
			char l2[32];
			snprintf(l2, sizeof(l2), "%d of %d", k, todo);
			writeStatusBar(buffer, l2);
			ui_progress((float)(k - 1) / todo);
			if (s->status == UI_ST_ON_CARD && card_replace == REPLACE_NO) continue;
			if (SDLoadMCImage((char*)filelist[s->index]) && CardWriteFile(MEM_CARD))
			{
				done++;
				s->status = UI_ST_ON_CARD;
				s->checked = false;
				continue;
			}
			ui_progress(-1);
			snprintf(buffer, sizeof(buffer), "Couldn't restore %s. Carry on with the rest?", s->title);
			if (WaitPromptChoice(buffer, "Stop", "Carry on") != 1) break;
		}
		card_replace = REPLACE_ASK;
		ui_progress(-1);
		if (done == todo) snprintf(buffer, sizeof(buffer), "Restored %d save%s.", done, done == 1 ? "" : "s");
		else snprintf(buffer, sizeof(buffer), "Restored %d of the %d saves.", done, todo);
		WaitPrompt(buffer);
		return;
	}
}

/****************************************************************************
* Delete saves from the memory card (Card tools)
****************************************************************************/
void MC_DeleteMode (void)
{
	char buffer[128], where[64];
	int sel = 0;

	if (!card_present()) return;
	ui_screen(UI_LIST);
	ui_list_clear();
	ui_list_header("Delete saves  \x95  choose the saves", "");
	int n = saves_load_card(MEM_CARD);
	if (!n)
	{
		WaitPrompt ("There are no saves on this memory card.");
		return;
	}
	char right[32];
	snprintf(right, sizeof(right), "%d save%s", n, n == 1 ? "" : "s");
	slot_line(where, sizeof(where), "They'll be deleted from Memory Card %c.");
	PickSpec ps = { "Delete saves  \x95  choose the saves", right, "Delete", "Tick the saves to delete with A.", where };
	if (PickList(&ps, &sel) != PICK_GO) return;

	int todo = 0;
	for (int i = 0; i < n; i++) if (save_rows[i].checked) todo++;
	snprintf(buffer, sizeof(buffer), "Delete %d save%s from Memory Card %c? This can't be undone.", todo, todo == 1 ? "" : "s", slot_letter());
	if (WaitPromptChoiceAZ(buffer, "Delete", "Cancel") != 0) return;
	int done = 0, k = 0;
	for (int i = 0; i < n; i++)
	{
		if (!save_rows[i].checked) continue;
		k++;
		snprintf(buffer, sizeof(buffer), "Deleting %s\x85", save_rows[i].title);
		writeStatusBar(buffer, "");
		ui_progress((float)(k - 1) / todo);
		if (CardDeleteSave(MEM_CARD, save_rows[i].index)) done++;
	}
	ui_progress(-1);
	snprintf(buffer, sizeof(buffer), "Deleted %d save%s.", done, done == 1 ? "" : "s");
	WaitPrompt(buffer);
}

/****************************************************************************
* Back up the whole memory card (a .raw image) to the SD card
****************************************************************************/
void SD_RawBackupMode ()
{
	s32 writen = 0;
	char msg[64];

	displaypath = 0;
	writeStatusBar("Backing up the whole memory card\x85", "");

	if (BackupRawImage(MEM_CARD, &writen) == 1)
	{
		sprintf(msg, "Backed up the whole card (%d bytes).", writen);
		WaitPrompt(msg);
	}else{
		WaitPrompt("Couldn't back up the card.");
	}
}

/****************************************************************************
* Restore a whole-card image from the SD card
****************************************************************************/
void SD_RawRestoreMode ()
{
	char msg[128], title[300];
	s32 writen = 0;
	int sel = 0;

	sprintf((char*)currFolder, MCSAVES);
	for (;;)
	{
		ui_screen(UI_LIST);
		ui_list_clear();
		int files = saves_load_sd(0, MEM_CARD);
		if (files <= 0)
		{
			if (strcmp((char*)currFolder, MCSAVES) == 0)
			{
				WaitPrompt ("There are no card images in MCBACKUP on the SD card.");
				return;
			}
			WaitPrompt("This folder is empty.");
			go_up_folder();
			sel = 0;
			continue;
		}
		folder_title(title, sizeof(title), "Restore a whole card");
		snprintf(msg, sizeof(msg), "Everything on Memory Card %c will be replaced.", slot_letter());
		PickSpec ps = { title, "", NULL, "Choose the card image to put on the memory card.", msg };
		int r = PickList(&ps, &sel);
		if (r == PICK_BACK)
		{
			if (strcmp((char*)currFolder, MCSAVES) == 0) return;
			go_up_folder();
			sel = 0;
			continue;
		}
		SaveRow *s = &save_rows[sel];
		if (s->folder)
		{
			char folder[1300];
			snprintf(folder, sizeof(folder), "%s/%s", (char*)currFolder, (char*)filelist[s->index]);
			snprintf((char*)currFolder, sizeof(currFolder), "%s", folder);
			sel = 0;
			continue;
		}

#ifdef FLASHIDCHECK
		/* a card image only goes back on the card it came from */
		SDLoadCardImageHeader((char*)filelist[s->index]);
		getserial(imageserial);
		Card_SerialNo(MEM_CARD);
		sramex = __SYS_LockSramEx();
		__SYS_UnlockSramEx(0);
		for (int i=0;i<12;i++){
			if (imageserial[i] != sramex->flash_id[MEM_CARD][i]){
				WaitPrompt ("This image was made from a different memory card.");
				return;
			}
		}
#endif
		snprintf(msg, sizeof(msg), "Put %s on Memory Card %c? Everything on the card now will be lost.", s->title, slot_letter());
		if (WaitPromptChoiceAZ(msg, "Restore", "Cancel") != 0) continue;
		writeStatusBar("Restoring the whole memory card\x85", "");
		if (RestoreRawImage(MEM_CARD, (char*)filelist[s->index], &writen) == 1)
		{
			sprintf(msg, "Restored the whole card (%d bytes).", writen);
			WaitPrompt(msg);
		}else
		{
			WaitPrompt("Couldn't restore the card.");
		}
		return;
	}
}

/* is there a memory card in the slot? (asks twice: the first look can miss a
 * card that was just put in) */
static bool card_present(void)
{
	char msg[64];
	CARD_Probe(MEM_CARD);
	VIDEO_WaitVSync ();
	if (CARD_Probe(MEM_CARD) > 0) return true;
	sprintf(msg, "Please put a memory card in Slot %c.", slot_letter());
	WaitPrompt(msg);
	return false;
}

/****************************************************************************
* Starting another .dol (the Launcher, or a new version after an update)
****************************************************************************/

/* dolload.c calls this just before it jumps: everything of ours stops */
void dol_before_jump(void)
{
	ui_shutdown();
	sound_shutdown();
	deinitFAT();
}

/* Starts a .dol from the SD card. Only returns if it couldn't (err says why). */
static bool launch_dol(const char *path, char *err, size_t errlen)
{
	return dol_launch(path, err, errlen);
}

bool gcmm_launch_dol(const char *path)
{
	char err[200];
	return launch_dol(path, err, sizeof(err));
}

/* The .dol files at the root of the SD card, sorted, for Settings > Launcher */
static int cmp_name(const void *a, const void *b) { return strcasecmp((const char *)a, (const char *)b); }
int launcher_list(char (*names)[64], int max)
{
	int n = 0;
	if (!have_sd) return 0;
	char root[8];
	snprintf(root, sizeof(root), "%s:/", fatpath);
	DIR *d = opendir(root);
	if (!d) return 0;
	struct dirent *ent;
	while ((ent = readdir(d)) && n < max) {
		size_t l = strlen(ent->d_name);
		if (ent->d_name[0] == '.' || l < 5 || l >= 64 || strcasecmp(ent->d_name + l - 4, ".dol")) continue;
		char path[80];
		snprintf(path, sizeof(path), "%s%s", root, ent->d_name);
		if (!storage_is_file(path)) continue;
		snprintf(names[n++], 64, "%s", ent->d_name);
	}
	closedir(d);
	qsort(names, n, 64, cmp_name);
	return n;
}

/* The Launcher Exit starts: the one chosen in Settings if it's there,
 * otherwise autoexec.dol, boot.dol, ipl.dol or a swiss*.dol at the root.
 * "" if there's none. */
const char *launcher_current(void)
{
	static char names[48][64];
	static const char *const usual[] = { "autoexec.dol", "boot.dol", "ipl.dol" };
	int n = launcher_list(names, 48);
	if (ui_set.launcher[0])
		for (int i = 0; i < n; i++) if (!strcasecmp(names[i], ui_set.launcher)) return names[i];
	for (unsigned u = 0; u < sizeof(usual) / sizeof(usual[0]); u++)
		for (int i = 0; i < n; i++) if (!strcasecmp(names[i], usual[u])) return names[i];
	for (int i = 0; i < n; i++) if (!strncasecmp(names[i], "swiss", 5)) return names[i];
	return "";
}

/****************************************************************************
* Leave GCMM: start the Launcher (Settings > Launcher), or go back to the
* loader that started it, or restart the GameCube. Returns if the Launcher
* couldn't be started (after saying why).
****************************************************************************/
static void exit_to_launcher(void)
{
	int *psoid = (int *) 0x80001800;
	void (*PSOReload) () = (void (*)()) 0x80001800;
	char err[200];

	ShowAction ("Exiting\x85");
	const char *name = launcher_current();
	if (have_sd && name[0]) {
		char path[80];
		snprintf(path, sizeof(path), "%s:/%s", fatpath, name);
		launch_dol(path, err, sizeof(err));
		/* only gets here if it couldn't be started */
		WaitPrompt(err);
		return;
	}
	if (psoid[0] == PSOSDLOADID){
		ui_shutdown();
		sound_shutdown();
		deinitFAT();
		PSOReload ();
	}
	if (WaitPromptChoice("There's no Launcher (.dol) at the root of the SD card. Restart the GameCube?", "Cancel", "Restart") == 0)
		return;
	ui_shutdown();
	sound_shutdown();
	deinitFAT();
	SYS_ResetSystem(SYS_HOTRESET, 0, 0);
}

/****************************************************************************
* Main
****************************************************************************/
int main (int argc, char *argv[])
{
	have_sd = false;
	CUR_DEVICE = 0;

	ui_init();      /* video; the splash screen waits for the settings (below) */
	VIDEO_SetPostRetraceCallback (updatePAD);
	sound_init();   /* (the sounds are made in the background) */
	storage_set_program(argc > 0 && argv ? argv[0] : NULL);

	detect_devices();

	//Command line (from a .cli file): ask, sdgecko, sdgeckoA, sdgeckoB, gcloader, sd2sp2
	if (argc > 1 && argv[1])
	{
		if (!strcasecmp(argv[1], "ask"))
		{
			skip_selector = 0;
		}
		else if (!strcasecmp(argv[1], "sdgecko"))
		{
			if (DEVICES[DEV_GCSDA] == DEV_AVAIL && !(DEVICES[DEV_GCSDB] == DEV_AVAIL))
				CUR_DEVICE = DEV_GCSDA;
			else if (DEVICES[DEV_GCSDB] == DEV_AVAIL && !(DEVICES[DEV_GCSDA] == DEV_AVAIL))
				CUR_DEVICE = DEV_GCSDB;
		}
		else if (!strcasecmp(argv[1], "sdgeckoA"))
		{
			if (DEVICES[DEV_GCSDA] == DEV_AVAIL) CUR_DEVICE = DEV_GCSDA;
		}
		else if (!strcasecmp(argv[1], "sdgeckoB"))
		{
			if (DEVICES[DEV_GCSDB] == DEV_AVAIL) CUR_DEVICE = DEV_GCSDB;
		}
		else if (!strcasecmp(argv[1], "gcloader"))
		{
			if (DEVICES[DEV_GCODE] == DEV_AVAIL) CUR_DEVICE = DEV_GCODE;
		}
		else if (!strcasecmp(argv[1], "sd2sp2"))
		{
			if (DEVICES[DEV_GCSDC] == DEV_AVAIL) CUR_DEVICE = DEV_GCSDC;
		}
	}
	/* With one SD card adapter, or one given on the command line, the SD card
	 * opens straight away, so the settings (16:9 and so on) are in place
	 * before the splash screen shows */
	if (CUR_DEVICE == 0 && DEVICES[DEV_NUM] == 1 && skip_selector) CUR_DEVICE = device_select();
	if (CUR_DEVICE) {
		use_device();
		find_settings(argc, argv);
		ui_settings_load();
	}
	ui_splash_ready();
	ui_splash_text("Looking for saves\x85");
	u64 splash_start = ticks_to_millisecs(gettime());
	while (ticks_to_millisecs(gettime()) - splash_start < 1500) VIDEO_WaitVSync();   /* let the logo settle */
	ui_screen(UI_MAIN);
	ui_main_select(-1);
	UpdateTitleInfo();

	//Several adapters: choose one (and its settings then)
	if (!have_sd && CUR_DEVICE == 0) {
		CUR_DEVICE = device_select();
		use_device();
		find_settings(argc, argv);
		ui_settings_load();
	}
	skip_selector = 0;
	if (have_sd) update_remove_old();   /* the version an update replaced */
	MainSummaryRefresh();

	for (;;)
	{
		freecardbuf();
		cancel = 0;	//set when the user backs out with B
		doall = 0;
		mode = SelectMode ();

		//SD2SP2 and GC Loader leave both memory card slots free: use the one
		//with a card in it, and only ask when there's a card in each
		if (mode != 500 && mode != 1000 && CUR_DEVICE != DEV_GCSDA && CUR_DEVICE != DEV_GCSDB) {
			if (slot_has_card(CARD_SLOTA, true) && slot_has_card(CARD_SLOTB, true)) {
				if (WaitPromptChoice ("Which memory card slot?", "Slot B", "Slot A") == 1)
					MEM_CARD = CARD_SLOTA;
				else
					MEM_CARD = CARD_SLOTB;
			} else {
				pick_card_slot();
			}
		}

		switch (mode)
		{
		case 200 : //Delete saves
			MC_DeleteMode();
			break;
		case 300 : //Back up saves
			if (have_sd) SD_BackupMode();
			else WaitPrompt(NOFAT_MSG);
			break;
		case 400 : //Restore saves
			if (have_sd) SD_RestoreMode();
			else WaitPrompt(NOFAT_MSG);
			break;
		case 500 : //Exit
			exit_to_launcher();
			break;
		case 700 : //Back up the whole card
			if (have_sd) { SD_RawBackupMode(); UpdateSDInfo(); }
			else WaitPrompt(NOFAT_MSG);
			break;
		case 800 : //Restore the whole card
			if (card_present())
			{
				if (have_sd) SD_RawRestoreMode();
				else WaitPrompt(NOFAT_MSG);
			}
			break;
		case 900 : //Format the card
			if (card_present()) MC_FormatMode(MEM_CARD);
			break;
		case 1000: //Choose a device
			deinitFAT();
			UpdateTitleInfo();
			WaitPrompt("The SD card is closed. You can swap devices now.");
			ShowAction("Looking for SD cards\x85");
			detect_devices();
			CUR_DEVICE = device_select();
			use_device();
			find_settings(argc, argv);
			ui_settings_load();   /* (this SD card's) */
			break;
		}

		offsetchanged = true;
		if (mode != 500) MainSummaryRefresh();   /* (the saves may have changed) */
	}
	return 0;
}
