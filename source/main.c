/****************************************************************************
* GCMM - GameCube Memory Manager
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
#include "aram/sidestep.h"

#define PSOSDLOADID 0x7c6000a6
//Comment FLASHIDCHECK to allow writing any image to any mc. This will corrupt official cards.
#define FLASHIDCHECK

#define SDGECKOA_PATH "sda"
#define SDGECKOB_PATH "sdb"
#define SD2SP2_PATH "sdc"
#define GCLOADER_PATH "gcl"
char fatpath[4];

const char appversion[] = "v1.5.2";
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
extern u8 imageserial[12];

#define NOFAT_MSG "No SD card found. Press R on the main screen to choose a device."

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

/* settings.ini lives beside GCMM's own .dol when that folder is on the SD
 * card in use, or in MCBACKUP otherwise */
static void find_settings(int argc, char *argv[])
{
	char path[300];
	path[0] = 0;
	if (!have_sd) { ui_settings_path(""); return; }
	if (argc > 0 && argv && argv[0]) {
		const char *p = strstr(argv[0], ":/");
		const char *slash = strrchr(argv[0], '/');
		if (p && slash && slash > p + 1) {
			char dir[256];
			int n = (int)(slash - (p + 2));
			if (n > 0 && n < (int)sizeof(dir)) {
				memcpy(dir, p + 2, n);
				dir[n] = 0;
				snprintf(path, sizeof(path), "%s:/%s", fatpath, dir);
				if (isdir_sd(path) == 1) snprintf(path, sizeof(path), "%s:/%s/settings.ini", fatpath, dir);
				else path[0] = 0;
			}
		}
	}
	if (!path[0]) snprintf(path, sizeof(path), "%s:/%s/gcmm.ini", fatpath, MCSAVES);
	ui_settings_path(path);
}

/* After choosing a device: mount it and pick the memory card slot */
static void use_device(void)
{
	if (CUR_DEVICE) have_sd = initFAT(CUR_DEVICE);
	//The memory card goes in the other slot to an SD Gecko
	if (CUR_DEVICE == DEV_GCSDB) MEM_CARD = CARD_SLOTA;
	else if (CUR_DEVICE == DEV_GCSDA) MEM_CARD = CARD_SLOTB;
	UpdateTitleInfo();
	UpdateSDInfo();
}

static char slot_letter(void) { return MEM_CARD ? 'B' : 'A'; }

/****************************************************************************
* Back up saves from the memory card to the SD card
****************************************************************************/
void SD_BackupMode ()
{
	int memitems;
	int selected = 0;
	int bytestodo;
	char buffer[256], text[64];

	displaypath = 0;
	ListBegin("Back up");
	writeStatusBar("Reading the memory card\x85", "");
	/*** Get the directory listing from the memory card ***/
	memitems = CardGetDirectory (MEM_CARD);

	/*** If it's a blank card, get out of here ***/
	if (!memitems)
	{
		WaitPrompt ("There are no saves on this memory card.");
		return;
	}

	writeStatusBar("Choose a save to back up.", "A: Back up  \x95  R: Back up all  \x95  B: Back");
	selected = ShowSelector (1);
	if (cancel)
	{
		return;
	}
	else if(doall)
	{
		doall = WaitPromptChoice("Back up every save on this memory card?", "No", "Yes");
		if (doall)
		{
			for ( selected = 0; selected < memitems; selected++ ) {
				sprintf(buffer, "[%d/%d] Reading from Memory Card %c\x85", selected+1, memitems, slot_letter());
				ShowAction(buffer);
				bytestodo = CardReadFile(MEM_CARD, selected);
				if (bytestodo)
				{
					sprintf(buffer, "[%d/%d] Saving to the SD card\x85", selected+1, memitems);
					ShowAction(buffer);
					if (!SDSaveMCImage())
					{
						strncpy(text, (char*)filelist[selected], 32);
						text[32]='\0';
						sprintf(buffer, "Couldn't back up %s. Carry on?", text);
						doall = WaitPromptChoice(buffer, "Yes", "No");
						if (doall)
						{
							WaitPrompt ("Stopped backing up.");
							return;
						}
					}
				}
				else
				{
					WaitPrompt ("Couldn't read the save from the memory card.");
					return;
				}
			}
			WaitPrompt("Every save is backed up.");
		}
		return;
	}

	/*** Back up one save ***/
	sprintf(buffer, "Reading from Memory Card %c\x85", slot_letter());
	ShowAction (buffer);
	bytestodo = CardReadFile (MEM_CARD, selected);
	if (bytestodo)
	{
		ShowAction ("Saving to the SD card\x85");
		if (SDSaveMCImage())
			WaitPrompt ("Backed up.");
		else
			WaitPrompt ("Couldn't back up the save.");
	}
	else
	{
		WaitPrompt ("Couldn't read the save from the memory card.");
	}
}

/****************************************************************************
* Restore saves from the SD card to the memory card
****************************************************************************/
static void go_up_folder(void)
{
	char* pos = strrchr( (char*)currFolder, '/' );
	if (pos) *pos = 0;
}

void SD_RestoreMode ()
{
	int files;
	int selected;
	char buffer[256], text[64];

	displaypath = 1;
	ListBegin("Restore");
	writeStatusBar("Reading the SD card\x85", "");
	sprintf((char*)currFolder, MCSAVES);
	files = SDGetFileList (1);

	if (!files)
	{
		WaitPrompt ("There are no saves in MCBACKUP on the SD card.");
		return;
	}

	for (;;)
	{
		writeStatusBar("Choose a save to put on the memory card.", "A: Restore  \x95  R: Restore all  \x95  B: Back");
		selected = ShowSelector (1);

		if (cancel)
		{
			if (strcmp((char*)currFolder, MCSAVES) == 0)
				return;
			go_up_folder();
			files = SDGetFileList (1);
			cancel = 0;
			offsetchanged = true;
			continue;
		}
		else if (doall)
		{
			doall = WaitPromptChoice("Restore every save in this folder to the memory card?", "Yes", "No");
			if (!doall)
			{
				for ( selected = folderCount; selected < files; selected++ ) {
					sprintf(buffer, "[%d/%d] Reading from the SD card\x85", selected+1, files);
					ShowAction(buffer);
					if (SDLoadMCImage ((char*)filelist[selected]))
					{
						sprintf(buffer, "[%d/%d] Saving to Memory Card %c\x85", selected+1, files, slot_letter());
						ShowAction(buffer);
						if (!CardWriteFile (MEM_CARD))
						{
							strncpy(text, (char*)filelist[selected], 32);
							text[32]='\0';
							sprintf(buffer, "Couldn't restore %s. Carry on?", text);
							doall = WaitPromptChoice(buffer, "Yes", "No");
							if (doall)
							{
								WaitPrompt ("Stopped restoring.");
								return;
							}
						}
					}
					else
					{
						WaitPrompt ("Couldn't read the save from the SD card.");
						return;
					}
				}
				WaitPrompt("Every save is restored.");
			}
			return;
		}

		//Is it a folder?
		char folder[1024];
		sprintf (folder, "%s:/%s/%s",fatpath, currFolder, (char*)filelist[selected]);
		if(isdir_sd(folder) == 1)
		{
			sprintf(folder, "%s/%s", (char*)currFolder, (char*)filelist[selected]);
			snprintf((char*)currFolder, sizeof(currFolder), "%s", folder);
			files = SDGetFileList (1);
			if (!files)
			{
				WaitPrompt("This folder is empty.");
				go_up_folder();
				files = SDGetFileList (1);
			}
			offsetchanged = true;
			continue;
		}

		ShowAction ("Reading from the SD card\x85");
		if (SDLoadMCImage ((char*)filelist[selected]))
		{
			ShowAction ("Saving to the memory card\x85");
			if (CardWriteFile (MEM_CARD))
				WaitPrompt ("Restored.");
			else
				WaitPrompt ("Couldn't restore the save.");
		}
		else
		{
			WaitPrompt ("Couldn't read the save from the SD card.");
		}
		return;
	}
}

/****************************************************************************
* Back up the whole memory card (a .raw image) to the SD card
****************************************************************************/
void SD_RawBackupMode ()
{
	s32 writen = 0;
	char msg[64];

	displaypath = 0;
	writeStatusBar("Reading the memory card\x85", "");

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
	int files;
	int selected;
	char msg[64];
	s32 writen = 0;
	int i;

	displaypath = 1;
	ListBegin("Restore card");
	writeStatusBar("Reading the SD card\x85", "");
	sprintf((char*)currFolder, MCSAVES);
	files = SDGetFileList (0);

	if (!files)
	{
		WaitPrompt ("There are no card images in MCBACKUP on the SD card.");
		return;
	}

	for (;;)
	{
		writeStatusBar("Choose a card image to put on the memory card.", "A: Restore  \x95  B: Back");
		selected = ShowSelector (0);

		if (cancel)
		{
			if (strcmp((char*)currFolder, MCSAVES) == 0)
				return;
			go_up_folder();
			files = SDGetFileList (0);
			cancel = 0;
			offsetchanged = true;
			continue;
		}
		if (doall || selected < 0)
		{
			doall = 0;   /* R does nothing here */
			continue;
		}

		//Is it a folder?
		char folder[1024];
		sprintf (folder, "%s:/%s/%s",fatpath, currFolder, (char*)filelist[selected]);
		if(isdir_sd(folder) == 1)
		{
			sprintf(folder, "%s/%s", (char*)currFolder, (char*)filelist[selected]);
			snprintf((char*)currFolder, sizeof(currFolder), "%s", folder);
			files = SDGetFileList (0);
			if (!files)
			{
				WaitPrompt("This folder is empty.");
				go_up_folder();
				files = SDGetFileList (0);
			}
			offsetchanged = true;
			continue;
		}

#ifdef FLASHIDCHECK
		//imageserial and sramex.flash_id[MEM_CARD] now hold the two flash IDs
		for (i=0;i<12;i++){
			if (imageserial[i] != sramex->flash_id[MEM_CARD][i]){
				WaitPrompt ("This image was made from a different memory card.");
				return;
			}
		}
#endif
		ShowAction ("Reading from the SD card\x85");
		if (RestoreRawImage(MEM_CARD, (char*)filelist[selected], &writen) == 1)
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
* Leave GCMM: back to the loader that started it, or the Launcher
* (autoexec.dol on the SD card), or restart the GameCube
****************************************************************************/
static void exit_to_launcher(void)
{
	int *psoid = (int *) 0x80001800;
	void (*PSOReload) () = (void (*)()) 0x80001800;

	ShowAction ("Exiting\x85");
	if (psoid[0] == PSOSDLOADID){
		deinitFAT();
		ui_shutdown();
		PSOReload ();
	}
	if (have_sd){
		char exitdol[64];
		sprintf(exitdol, "%s:/autoexec.dol",fatpath);
		FILE *fp = fopen(exitdol, "rb");
		if (fp) {
			fseek(fp, 0, SEEK_END);
			int size = ftell(fp);
			fseek(fp, 0, SEEK_SET);
			if ((size > 0) && (size < (AR_GetSize() - (64*1024)))) {
				u8 *dol = (u8*) memalign(32, size);
				if (dol) {
					fread(dol, 1, size, fp);
					fclose(fp);
					deinitFAT();
					ui_shutdown();
					DOLtoARAM(dol, 0, NULL);
					/* only gets here if the .dol couldn't start */
					SYS_ResetSystem(SYS_HOTRESET, 0, 0);
				}
			}
			fclose(fp);
		}
	}
	deinitFAT();
	ui_shutdown();
	SYS_ResetSystem(SYS_HOTRESET, 0, 0);
}

/****************************************************************************
* Main
****************************************************************************/
int main (int argc, char *argv[])
{
	have_sd = false;
	CUR_DEVICE = 0;

	ui_init();      /* video, and the splash screen */
	VIDEO_SetPostRetraceCallback (updatePAD);
	u64 splash_start = ticks_to_millisecs(gettime());
	ui_splash_text("Looking for SD cards\x85");

	detect_devices();

	/* let the logo settle before moving on */
	while (ticks_to_millisecs(gettime()) - splash_start < 1500) VIDEO_WaitVSync();
	ui_screen(UI_MAIN);
	ui_main_select(-1);
	UpdateTitleInfo();

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
	//No device yet: the device choice (skipped on first boot if there's only one)
	if (CUR_DEVICE == 0) CUR_DEVICE = device_select();

	use_device();
	find_settings(argc, argv);
	ui_settings_load();
	skip_selector = 0;

	for (;;)
	{
		freecardbuf();
		cancel = 0;	//set when the user backs out with B
		doall = 0;
		mode = SelectMode ();

		//SD2SP2 and GC Loader leave both memory card slots free: ask which
		if (mode != 500 && mode != 1000 && (CUR_DEVICE == DEV_GCSDC || CUR_DEVICE == DEV_GCODE)) {
			if (WaitPromptChoice ("Which memory card slot?", "Slot B", "Slot A") == 1)
				MEM_CARD = CARD_SLOTA;
			else
				MEM_CARD = CARD_SLOTB;
		}

		switch (mode)
		{
		case 200 : //Delete saves
			MC_DeleteMode(MEM_CARD);
			break;
		case 300 : //Back up saves
			if (have_sd) { SD_BackupMode(); UpdateSDInfo(); }
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
			break;
		}

		offsetchanged = true;
	}
	return 0;
}
