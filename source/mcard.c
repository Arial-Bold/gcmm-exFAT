/****************************************************************************
* Memory Card
*
* Supporting functions.
*
* MAXFILEBUFFER is set to 2MB as it's the largest file I've seen.
*
* CARDBACKUP FILE
*
*   0	- Copy of CardDir
*  64   - Copy of CardStatus
* 192   - Memory Card File Data
****************************************************************************/
#include <gccore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ogc/libversion.h>
#if (_V_MAJOR_ <= 2) && (_V_MINOR_ <= 2)
#ifndef CARD_SetStatusEx
#define CARD_SetStatusEx(chn,fileno,entry) __card_setstatusex(chn,fileno,(struct card_direntry *)(entry))
#endif
#ifndef CARD_GetStatusEx
#define CARD_GetStatusEx(chn,fileno,entry) __card_getstatusex(chn,fileno,(struct card_direntry *)(entry))
#endif
#include "card.h"
extern s32 __card_setstatusex(s32 chn,s32 fileno,struct card_direntry *entry);
extern s32 __card_getstatusex(s32 chn,s32 fileno,struct card_direntry *entry);
s32 CARD_GetFreeBlocks(s32 chn, u16* freeblocks);
s32 CARD_GetSerialNo(s32 chn,u32 *serial1,u32 *serial2);
#else
#include <ogc/card.h>
#endif

#include "mcard.h"
#include "vcard.h"
#include <dirent.h>
#include <strings.h>
#include <sys/stat.h>
#include <ogc/lwp_watchdog.h>
#include "gci.h"
#include "freetype.h"

/*** Memory Card Work Area ***/
static u8 SysArea[CARD_WORKAREA] ATTRIBUTE_ALIGN (32);

/*** Memory File Buffer ***/
#define MAXFILEBUFFER (1024 * 2048)	/*** 2MB Buffer ***/
u8 FileBuffer[MAXFILEBUFFER] ATTRIBUTE_ALIGN (32);
int card_replace = REPLACE_ASK;   /* what CardWriteFile does with a save already on the card */
u8 CommentBuffer[64] ATTRIBUTE_ALIGN (32);

u16 tlut[9][256] ATTRIBUTE_ALIGN (32);
u16 tlutbanner[256] ATTRIBUTE_ALIGN (32);
u8 icondata[8][1024] ATTRIBUTE_ALIGN (32);
u16 icondataRGB[8][1024] ATTRIBUTE_ALIGN (32);
/*** This array holds the 16-bit banner data for the current save
	 Needs decoding by bannerloadRGB function before we can show it ***/
u16 bannerdata[CARD_BANNER_W*CARD_BANNER_H] ATTRIBUTE_ALIGN (32);
/*** This array holds the 8-bit banner data for the current save
	 Needs decoding by bannerloadCI function before we can show it ***/
u8 bannerdataCI[CARD_BANNER_W*CARD_BANNER_H] ATTRIBUTE_ALIGN (32);
int numicons;
int frametable[2*CARD_MAXICONS - 2];
int iconindex[2*CARD_MAXICONS - 2];
int lastframe;
int lasticon;
/*** This matrix will serve as our array of filenames for each file on the card
     We add 10 to filenamelen since we add on game company info***/
u8 filelist[1024][1024];
u8 currFolder[260];
int folderCount;
int displaypath;
int maxfile;
extern int cancel;
extern int doall;
extern bool offsetchanged;
extern int mode;

/*** Card lib ***/
card_dir CardList[CARD_MAXFILES];	/*** Directory listing ***/
static card_dir CardDir;
static card_file CardFile;
static card_stat CardStatus;
static int cardcount = 0;
static u8 permission;
s32 memsize, sectsize;

card_direntry gci;

/****************************************************************************
 * Virtual memory cards: the card images a Launcher keeps on the SD card
 * (swiss/saves/MemoryCardA.USA.raw and so on). Slots from VSLOT_BASE on are
 * these; the card functions below hand them to vcard.c.
 ***************************************************************************/
extern char fatpath[4];
extern syssram* __SYS_LockSram();
extern u32 __SYS_UnlockSram(u32 write);
extern syssramex* __SYS_LockSramEx();
extern u32 __SYS_UnlockSramEx(u32 write);

static VirtualCard vcards[VCARD_MAX];
static int nvcards;

bool SlotIsVirtual(int slot) { return slot >= VSLOT_BASE; }
const VirtualCard *VirtualCardGet(int slot)
{
	int i = slot - VSLOT_BASE;
	return (i >= 0 && i < nvcards) ? &vcards[i] : NULL;
}
int VirtualCardCount(void) { return nvcards; }

static int cmp_vcard(const void *a, const void *b)
{
	const VirtualCard *x = a, *y = b;
	if (x->letter != y->letter) return x->letter - y->letter;
	return strcmp(x->region, y->region);
}

/* What's in swiss/saves: MemoryCardA.USA.raw, MemoryCardB.JPN.raw ... */
int VirtualCardsScan(void)
{
	char dir[64];
	nvcards = 0;
	snprintf(dir, sizeof(dir), "%s:/swiss/saves", fatpath);
	DIR *d = opendir(dir);
	if (!d) return 0;
	struct dirent *e;
	while ((e = readdir(d)) && nvcards < VCARD_MAX) {
		const char *n = e->d_name;
		size_t l = strlen(n);
		if (l < 18 || strncasecmp(n, "MemoryCard", 10) || n[11] != '.' || strcasecmp(n + l - 4, ".raw")) continue;
		char letter = n[10] & ~0x20;
		if (letter != 'A' && letter != 'B') continue;
		VirtualCard *v = &vcards[nvcards];
		memset(v, 0, sizeof(*v));
		v->letter = letter;
		snprintf(v->region, sizeof(v->region), "%.*s", (int)(l - 16) < 7 ? (int)(l - 16) : 7, n + 12);
		snprintf(v->path, sizeof(v->path), "%s/%s", dir, n);
		struct stat st;
		long size = stat(v->path, &st) == 0 ? (long)st.st_size : 0;
		if (vcard_open(v->path) == VCARD_OK) {
			v->formatted = true;
			v->blocks = vcard_blocks();
			v->saves = vcard_count();
			v->free = vcard_free();
		} else {
			v->blocks = vcard_blocks_for_size(size);
			if (!v->blocks) v->blocks = 2043;
		}
		vcard_close();
		nvcards++;
	}
	closedir(d);
	qsort(vcards, nvcards, sizeof(vcards[0]), cmp_vcard);
	return nvcards;
}

/* "Virtual card A (USA)", or "Memory Card A" for a real one */
void CardSlotName(int slot, char *out, int cap)
{
	const VirtualCard *v = VirtualCardGet(slot);
	if (v) snprintf(out, cap, "Virtual card %c (%s)", v->letter, v->region);
	else snprintf(out, cap, "Memory Card %c", slot ? 'B' : 'A');
}

/* Says why a virtual card couldn't be opened */
static void v_error(int slot, int r)
{
	char name[48], msg[200];
	CardSlotName(slot, name, sizeof(name));
	if (r == VCARD_ERR_UNFORMATTED)
		snprintf(msg, sizeof(msg), "%s isn't formatted yet. A game formats it the first time it's used, or use Format the card in Card tools.", name);
	else if (r == VCARD_ERR_BROKEN)
		snprintf(msg, sizeof(msg), "%s is damaged: both copies of its directory or block table are bad.", name);
	else
		snprintf(msg, sizeof(msg), "Couldn't read %s on the SD card.", name);
	WaitPrompt(msg);
}

static int v_open(int slot, bool say)
{
	const VirtualCard *v = VirtualCardGet(slot);
	int r = v ? vcard_open(v->path) : VCARD_ERR_IO;
	if (r != VCARD_OK && say) v_error(slot, r);
	return r;
}

/* the serial number games such as F-Zero GX and Phantasy Star Online tie their saves to */
static s32 card_serial(s32 chn, u32 *s1, u32 *s2)
{
	if (SlotIsVirtual(chn)) { vcard_serial(s1, s2); return 0; }
	return CARD_GetSerialNo(chn, s1, s2);
}

/* Formats a virtual card as this console formats a card in that slot */
static int v_format(const char *path, char letter, const char *region, int blocks)
{
	VCardFormat fm;
	memset(&fm, 0, sizeof(fm));
	syssram *sram = __SYS_LockSram();
	fm.counter_bias = sram->counter_bias;
	fm.language = sram->lang;
	__SYS_UnlockSram(0);
	syssramex *sramex = __SYS_LockSramEx();
	memcpy(fm.flash_id, sramex->flash_id[letter == 'B' ? 1 : 0], 12);
	__SYS_UnlockSramEx(0);
	fm.vi_dtv = *(vu16 *)0xCC00206E;   /* the VI's DTV status, as the card library stores it */
	fm.time = gettime();
	fm.encoding = !strcasecmp(region, "JPN") ? 1 : 0;
	return vcard_format(path, blocks, &fm);
}

/* A new virtual card (formatted), replacing any there. Returns its slot, or -1. */
int VirtualCardMake(char letter, const char *region, int blocks)
{
	char dir[64], path[128];
	snprintf(dir, sizeof(dir), "%s:/swiss", fatpath);
	mkdir(dir, 0777);
	snprintf(dir, sizeof(dir), "%s:/swiss/saves", fatpath);
	mkdir(dir, 0777);
	snprintf(path, sizeof(path), "%s/MemoryCard%c.%s.raw", dir, letter, region);
	if (v_format(path, letter, region, blocks) != VCARD_OK) return -1;
	VirtualCardsScan();
	for (int i = 0; i < nvcards; i++)
		if (vcards[i].letter == letter && !strcasecmp(vcards[i].region, region)) return VSLOT_BASE + i;
	return -1;
}

/* Is the card there? A real one in its slot, or a virtual one that's formatted */
bool CardSlotPresent(int slot)
{
	const VirtualCard *v = VirtualCardGet(slot);
	if (v) return v->formatted;
	if (SlotIsVirtual(slot)) return false;
	return CARD_Probe(slot) > 0;
}

//The following code is made by Ralf at GSCentral forums (gscentral.org)
//http://board.gscentral.org/retro-hacking/53093.htm#post188949
s32 FZEROGX_MakeSaveGameValid(s32 chn);
s32 PSO_MakeSaveGameValid(s32 chn);

/*---------------------------------------------------------------------------------
	This function is called if a card is physically removed
---------------------------------------------------------------------------------*/
void card_removed(s32 chn,s32 result)
{
	if (chn == CARD_SLOTA){
		//printf("Card was removed from slot A");
	}
	else{
		//printf("Card was removed from slot B");
	}
	CARD_Unmount(chn);
}

/****************************************************************************
 * MountCard
 *
 * Mounts the memory card in the given slot.
 * CARD_Mount is called for a maximum of 10 tries
 * Returns the result of the last attempted CARD_Mount command.
 ***************************************************************************/
int MountCard(int cslot)
{
	s32 ret = -1;
	int tries = 0;
	int isMounted;

	if (SlotIsVirtual(cslot)) return v_open(cslot, false) == VCARD_OK ? 0 : CARD_ERROR_BROKEN;

	/*** Start the memory card system afresh. libogc2 then waits about 300 ms
	before it trusts a slot again (any sooner and the card counts as missing:
	"no card"), so the reset happens once, and the card gets time to answer
	before it's mounted. ***/
	EXI_ProbeReset();
	CARD_Init (NULL, NULL);
	//Ensure we start in show all files mode
	CARD_SetCompany(NULL);
	CARD_SetGamecode(NULL);
	for (int i = 0; i < 90 && CARD_ProbeEx(cslot, NULL, NULL) == CARD_ERROR_BUSY; i++)
		VIDEO_WaitVSync ();	/* up to 1.5 seconds */

	// Mount the card, try several times as they are tricky
	while ( (tries < 10) && (ret < 0))
	{
		ret = CARD_Mount (cslot, SysArea, card_removed);
		if (ret >= 0) break;

		for (int i = 0; i < 6; i++) VIDEO_WaitVSync ();	/* a tenth of a second between tries */
		tries++;
	}
			/*** Make sure the card is really mounted ***/
	isMounted = CARD_ProbeEx(cslot, &memsize, &sectsize);
	if (memsize > 0 && sectsize > 0)//then we really mounted de card
	{
		return isMounted;
	}
	/*** If this point is reached, something went wrong ***/
	CARD_Unmount(cslot);
	return ret;
}

u16 FreeBlocks(s32 chn)
{
	s32 err;
	static u8 noinserted = 0;
	u16 freeblocks = 0;
	if (SlotIsVirtual(chn)) {
		if (v_open(chn, true) != VCARD_OK) return 0;
		freeblocks = vcard_free();
		vcard_close();
		return freeblocks;
	}
	/*** Try to mount the card ***/
	err = MountCard(chn);
	if (err < 0)
	{
		//We want to allow browsing saves in sd even with no card
		if (err == CARD_ERROR_NOCARD)
		{
			noinserted = 1;
			if(chn) ShowAction("No card inserted in slot B!");
			else ShowAction("No card inserted in slot A!");
			return 0;
		}else
		{
			WaitCardError("CardMount", err);
			return 0;			/*** Unable to mount the card ***/
		}
	}else{
		err = CARD_GetFreeBlocks(chn, &freeblocks);
		if (err < 0)
		{
			CARD_Unmount(chn);
            //We want to allow browsing saves in sd even with no card
            if (err == CARD_ERROR_NOCARD)
            {
				noinserted = 1;
                if(chn) ShowAction("No card inserted in slot B!");
                else ShowAction("No card inserted in slot A!");
                return 0;
            }else
            {
				WaitCardError("FreeBlocks", err);
				return 0;
            }
		}
	}
	if (noinserted)
	{
		if (mode == 300)//Backup mode
			writeStatusBar("Choose a save to back up.", "A: Back up  \x95  R: Back up all  \x95  B: Back");
		else if (mode == 400)//Restore mode
			writeStatusBar("Choose a save to put on the memory card.", "A: Restore  \x95  R: Restore all  \x95  B: Back");
		else
			writeStatusBar("","");
	}
	CARD_Unmount(chn);
	return freeblocks;
}

/****************************************************************************
* GCIMakeHeader
*
* Create a GCI compatible header from libOGC card_stat
****************************************************************************/
void GCIMakeHeader()
{
	/*** Clear out the cgi header ***/
	memset(&gci, 0xff, sizeof(card_direntry) );

	/*** Populate ***/
	memcpy(&gci.gamecode, &CardStatus.gamecode, 4);
	memcpy(&gci.company, &CardStatus.company, 2);
	gci.banner_fmt = CardStatus.banner_fmt;
	memcpy(&gci.filename, &CardStatus.filename, CARD_FILENAMELEN);
	gci.last_modified = CardStatus.time;
	gci.icon_addr = CardStatus.icon_addr;
	gci.icon_fmt = CardStatus.icon_fmt;
	gci.icon_speed = CardStatus.icon_speed;

	/*** Permission key has to be gotten separately. Make it 0 for normal privileges ***/
	gci.permission = permission;
	/*** Who cares about copy counter ***/
	gci.copy_times = 0;

	/*** Block index does not matter, it won't be restored at the same spot ***/
	gci.block = 32;

	gci.length = (CardStatus.len / 8192);
	gci.comment_addr = CardStatus.comment_addr;

	/*** Copy to head of buffer ***/
	memcpy(FileBuffer, &gci, sizeof(card_direntry));
}

/****************************************************************************
* ExtractGCIHeader
*
* Extract a GCI Header to libOGC card_stat
****************************************************************************/
void ExtractGCIHeader()
{
	/*** Clear out the status ***/
	memset(&CardStatus, 0, sizeof(card_stat));
	memcpy(&gci, FileBuffer, sizeof(card_direntry));

	memcpy(&CardStatus.gamecode, &gci.gamecode, 4);
	memcpy(&CardStatus.company, &gci.company, 2);
	CardStatus.banner_fmt = gci.banner_fmt;
	memcpy(&CardStatus.filename, &gci.filename, CARD_FILENAMELEN);
	CardStatus.time = gci.last_modified;
	CardStatus.icon_addr = gci.icon_addr;
	CardStatus.icon_fmt = gci.icon_fmt;
	CardStatus.icon_speed = gci.icon_speed;
	permission = gci.permission;
	CardStatus.len = gci.length * 8192;
	CardStatus.comment_addr = gci.comment_addr;
}

/****************************************************************************
* CardGetDirectory
*
* Returns number of files found on a card.
****************************************************************************/
int CardGetDirectory (int slot)
{
	int err;
	char company[4];
	char gamecode[6];

	//add null char
	company[2] = gamecode[4] = 0;

	if (SlotIsVirtual(slot)) {
		cardcount = 0;
		if (v_open(slot, true) != VCARD_OK) { maxfile = 0; return 0; }
		int n = vcard_count();
		for (int i = 0; i < n && i < CARD_MAXFILES; i++) {
			const u8 *ent = vcard_entry(i);
			card_dir *cd = &CardList[cardcount];
			memset(cd, 0, sizeof(*cd));
			cd->chn = slot;
			cd->fileno = i;
			cd->filelen = vcard_entry_blocks(i) * 8192;
			cd->permissions = ent[0x34];
			memcpy(cd->filename, ent + 8, CARD_FILENAMELEN);
			memcpy(cd->gamecode, ent, 4);
			memcpy(cd->company, ent + 4, 2);
			cd->showall = true;
			char fname[CARD_FILENAMELEN + 1] = { 0 };
			memcpy(fname, ent + 8, CARD_FILENAMELEN);
			memcpy(company, ent + 4, 2);
			memcpy(gamecode, ent, 4);
			memset(filelist[cardcount], 0, 1024);
			sprintf((char*)filelist[cardcount], "%s-%s-%s", company, gamecode, fname);
			cardcount++;
		}
		vcard_close();
		maxfile = cardcount;
		return cardcount;
	}

	/*** Clear the work area ***/
	memset (SysArea, 0, CARD_WORKAREA);

	/*** Initialise the Card system, show all ***/
	CARD_Init(NULL, NULL);

	/*** Try to mount the card ***/
	err = MountCard(slot);
	if (err < 0)
	{
		WaitCardError("CardMount", err);
		return 0;			/*** Unable to mount the card ***/
	}

	//Ensure we are in show all mode
	CARD_SetCompany(NULL);
	CARD_SetGamecode(NULL);

	/*** Retrieve the directory listing ***/
	cardcount = 0;
	err = CARD_FindFirst (slot, &CardDir, true); //true means we want to showall
	while (err != CARD_ERROR_NOFILE)
	{
		memcpy (&CardList[cardcount], &CardDir, sizeof (card_dir));
		memset (filelist[cardcount], 0, 1024);
		memcpy (company, &CardDir.company, 2);
		memcpy (gamecode, &CardDir.gamecode, 4);
		//This array will store what will show in left window
		sprintf ((char*)filelist[cardcount], "%s-%s-%s", company, gamecode, CardDir.filename);
		cardcount++;
		err = CARD_FindNext (&CardDir);
	}

	/*** Release as soon as possible ***/
	CARD_Unmount (slot);

	maxfile = cardcount;
	return cardcount;
}

/****************************************************************************
* CardListFiles
****************************************************************************/
void CardListFiles ()
{
	int i;
	char company[4];
	char gamecode[6];

	//add null char
	company[2] = gamecode[4] = 0;

	for (i = 0; i < cardcount; i++)
	{
		memcpy (company, &CardList[i].company, 2);
		memcpy (gamecode, &CardList[i].gamecode, 4);
		printf ("%s %s %s\n", company, gamecode, CardList[i].filename);
	}
}
/****************************************************************************
* CardReadFileHeader
*
* Retrieve a file header from the previously populated list.
* Reads in banner and icon data now
****************************************************************************/
/* Picks the banner, icons and comment out of the save in FileBuffer (its
 * directory entry in gci): into bannerdata, icondata, tlut... and
 * CommentBuffer, as the save lists and the copying need them. */
static void parse_icon_data(void)
{
	int i;
	u8* offset = FileBuffer + MCDATAOFFSET + gci.icon_addr;

	/*** Get the Banner/Icon Data from the save file ***/
	if ((gci.banner_fmt&CARD_BANNER_MASK) == CARD_BANNER_RGB) {
		//RGB banners are 96*32*2 in size
		memcpy(bannerdata, offset, 6144);
		offset += 6144;
	}
	else if ((gci.banner_fmt&CARD_BANNER_MASK) == CARD_BANNER_CI) {
		memcpy(bannerdataCI, offset, 3072);
		offset += 3072;
		memcpy(tlutbanner, offset, 512);
		offset += 512;
	}

	//Icon data
	int shared_pal = 0;
	lastframe = 0;
	numicons = 0;
	int j =0;
    i=0;
	int current_icon = 0;
	for (current_icon=0;i<CARD_MAXICONS;++current_icon){

		//no need to clear all values since we will only use the ones until lasticon
		frametable[current_icon] = 0;
		iconindex[current_icon] = 0;

		//Animation speed is mandatory to be set even for a single icon
		//When a speed is 0 there are no more icons
		//Some games may have bits set after the "blank icon" both in
		//speed (Baten Kaitos) and format (Wario Ware Inc.) bytes, which are just garbage
		if (!SDCARD_GetIconSpeed(gci.icon_speed,current_icon)){
			break;
		}else
		{//We've got a frame
			lastframe+=SDCARD_GetIconSpeed(gci.icon_speed,current_icon)*4;//Count the total frames
			frametable[current_icon]=lastframe; //Store the end frame of the icon

			if (SDCARD_GetIconFmt(gci.icon_fmt,current_icon) != 0)
			{
				//count the number of real icons
				numicons++;
				iconindex[current_icon]=current_icon; //Map the icon

				//CI with shared palette
				if (SDCARD_GetIconFmt(gci.icon_fmt,current_icon) == 1) {
					memcpy(icondata[current_icon], offset, 1024);
					offset += 1024;
					shared_pal = 1;
				}
				//CI with palette after the icon
				else if (SDCARD_GetIconFmt(gci.icon_fmt,current_icon) == 3)
				{
					memcpy(icondata[current_icon], offset, 1024);
					offset += 1024;
					memcpy(tlut[current_icon], offset, 512);
					offset += 512;
				}
				//RGB 16 bit icon
				else if (SDCARD_GetIconFmt(gci.icon_fmt,current_icon) == 2)
				{
					memcpy(icondataRGB[current_icon], offset, 2048);
					offset += 2048;
				}
			}else
			{       //Get next real icon
                    for(j=current_icon;j<CARD_MAXICONS;++j){

                        if (SDCARD_GetIconFmt(gci.icon_fmt,j) != 0)
                        {
                            iconindex[current_icon]=j; //Map blank frame to next real icon
                            break;
                        }

                    }
			}
		}
	}

    lasticon = current_icon-1;
    //Now get icon indexes for ping-pong style icons
    if (SDCARD_GetIconAnim(gci.banner_fmt) == CARD_ANIM_BOUNCE && current_icon>1) //We need at least 3 icons
    {
        j=current_icon;
        for (i = current_icon-2; 0 < i; --i, ++j)
        {
            lastframe += SDCARD_GetIconSpeed(gci.icon_speed,i)*4;
            frametable[j] = lastframe;
            iconindex[j] = iconindex[i];
        }
        lasticon = j-1;
    }
	//Get the shared palette
	if (shared_pal) memcpy(tlut[8], offset, 512);

	/*** Get the comment (two 32 byte strings) into buffer ***/
	memcpy(CommentBuffer, FileBuffer + MCDATAOFFSET + gci.comment_addr, 64);

}

/* How far into a save its banner, icons and comment reach (from the start
 * of its data), so a quick look need not read all of it. */
static u32 icon_data_end(void)
{
	u32 n = gci.icon_addr;
	if ((gci.banner_fmt & CARD_BANNER_MASK) == CARD_BANNER_RGB) n += 6144;
	else if ((gci.banner_fmt & CARD_BANNER_MASK) == CARD_BANNER_CI) n += 3584;
	int shared = 0;
	for (int k = 0; k < CARD_MAXICONS; k++) {
		if (!SDCARD_GetIconSpeed(gci.icon_speed, k)) break;
		switch (SDCARD_GetIconFmt(gci.icon_fmt, k)) {
		case 1: n += 1024; shared = 1; break;
		case 2: n += 2048; break;
		case 3: n += 1536; break;
		}
	}
	if (shared) n += 512;
	if (gci.comment_addr + 64 > n) n = gci.comment_addr + 64;
	return n;
}

/****************************************************************************
 * CardReadSummaries
 *
 * A quick look at every save in CardList (from CardGetDirectory): its
 * directory entry, icons and comment, reading only the part of each save
 * that holds them, with the card mounted once. After each save, done(id)
 * is called with gci, the icon buffers and CommentBuffer filled in (or
 * with ok false if it couldn't be read). Returns false if the card
 * couldn't be mounted.
 ***************************************************************************/
bool CardReadSummaries(int slot, void (*done)(int id, bool ok, void *ctx), void *ctx)
{
	u32 SectorSize = 8192;
	if (SlotIsVirtual(slot)) {
		if (v_open(slot, true) != VCARD_OK) return false;
		for (int id = 0; id < cardcount; id++)
		{
			memset (CommentBuffer, 0, 64);
			lastframe = numicons = 0;
			const u8 *ent = vcard_entry(id);
			if (!ent) { done(id, false, ctx); continue; }
			memcpy(&gci, ent, sizeof(card_direntry));
			memcpy(FileBuffer, &gci, sizeof(card_direntry));
			u32 want = icon_data_end(), most = vcard_entry_blocks(id) * 8192;
			if (want > most) want = most;
			if (want > MAXFILEBUFFER - MCDATAOFFSET) want = MAXFILEBUFFER - MCDATAOFFSET;
			bool ok = vcard_read(id, FileBuffer + MCDATAOFFSET, want) == VCARD_OK;
			if (ok) parse_icon_data();
			done(id, ok, ctx);
		}
		vcard_close();
		return true;
	}
	int err = MountCard(slot);
	if (err < 0)
	{
		WaitCardError("CardMount", err);
		return false;
	}
	CARD_GetSectorSize (slot, &SectorSize);
	for (int id = 0; id < cardcount; id++)
	{
		char company[4] = { 0 }, gamecode[6] = { 0 };
		memcpy (company, &CardList[id].company, 2);
		memcpy (gamecode, &CardList[id].gamecode, 4);
		CARD_SetCompany((const char*)company);
		CARD_SetGamecode((const char*)gamecode);
		memset (CommentBuffer, 0, 64);
		lastframe = numicons = 0;
		if (CARD_Open (slot, (char *) &CardList[id].filename, &CardFile) < 0)
		{
			done(id, false, ctx);
			continue;
		}
		memset(&gci, 0, sizeof(card_direntry));
		CARD_GetStatusEx(slot, CardFile.filenum, &gci);
		memcpy(FileBuffer, &gci, sizeof(card_direntry));
		u32 want = icon_data_end(), have = 0;
		if (want > (u32)CardFile.len) want = CardFile.len;
		if (want > MAXFILEBUFFER - MCDATAOFFSET) want = MAXFILEBUFFER - MCDATAOFFSET;
		bool ok = true;
		while (have < want)
		{
			if (CARD_Read (&CardFile, FileBuffer + MCDATAOFFSET + have, SectorSize, have) < 0) { ok = false; break; }
			have += SectorSize;
		}
		if (ok) parse_icon_data();
		CARD_Close (&CardFile);
		done(id, ok, ctx);
	}
	CARD_SetCompany(NULL);
	CARD_SetGamecode(NULL);
	CARD_Unmount (slot);
	return true;
}

//TODO: get icon and banner settings
int CardReadFileHeader (int slot, int id)
{
	int bytesdone = 0;
	int err;
	u32 SectorSize;
	char company[4];
	char gamecode[6];
	int filesize;
	u16 check_fmt, check_speed;

	if (id >= cardcount)
	{
		WaitPrompt("Bad id");
		return 0;			/*** Bad id ***/
	}

	/*** Clear the work buffers ***/
	memset (FileBuffer, 0, MAXFILEBUFFER);
	memset (CommentBuffer, 0, 64);
	memset (SysArea, 0, CARD_WORKAREA);
	//add null char
	company[2] = gamecode[4] = 0;

	memcpy (company, &CardList[id].company, 2);
	memcpy (gamecode, &CardList[id].gamecode, 4);

	if (SlotIsVirtual(slot))
	{
		/*** a virtual card: the save straight from its blocks ***/
		if (v_open(slot, true) != VCARD_OK) return 0;
		const u8 *ent = vcard_entry(id);
		filesize = vcard_entry_blocks(id) * 8192;
		if (!ent || filesize + MCDATAOFFSET > MAXFILEBUFFER) { vcard_close(); WaitPrompt("Couldn't read that save."); return 0; }
		memcpy(&gci, ent, sizeof(card_direntry));
		memcpy(FileBuffer, &gci, sizeof(card_direntry));
		if (vcard_read(id, FileBuffer + MCDATAOFFSET, filesize) != VCARD_OK) { vcard_close(); WaitPrompt("Couldn't read that save."); return 0; }
		vcard_close();
		parse_icon_data();
		return filesize + MCDATAOFFSET;
	}

	/*** Mount the card ***/
	err = MountCard(slot);
	if (err < 0)
	{
		WaitCardError("CardMount", err);
		return 0;			/*** Unable to mount the card ***/
	}

	/*** Retrieve sector size ***/
	CARD_GetSectorSize (slot, &SectorSize);

	/*** Initialise for this company & gamecode ***/
	CARD_SetCompany((const char*)company);
	CARD_SetGamecode((const char*)gamecode);

	/*** Open the file ***/
	err = CARD_Open (slot, (char *) &CardList[id].filename, &CardFile);
	if (err < 0)
	{
		CARD_Unmount (slot);
		WaitCardError("CardOpen", err);
		return 0;
	}

#ifdef STATUSOGC
	/*** Get card status info ***/
	CARD_GetStatus (slot, CardFile.filenum, &CardStatus);
	CARD_GetAttributes(slot,CardFile.filenum, &permission);

	GCIMakeHeader();
#else
	//get directory entry (same as gci header, but with all the data)
	memset(&gci,0,sizeof(card_direntry));
	CARD_GetStatusEx(slot,CardFile.filenum,&gci);
	/*** Copy to head of buffer ***/
	memcpy(FileBuffer, &gci, sizeof(card_direntry));
#endif

	/*** Copy the file contents to the buffer ***/
	filesize = CardFile.len;

	while (bytesdone < filesize)
	{
		CARD_Read (&CardFile, FileBuffer + MCDATAOFFSET + bytesdone, SectorSize, bytesdone);
		bytesdone += SectorSize;
	}

	/***
		Get the Banner/Icon Data from the memory card file.
		Very specific if/else setup to minimize data copies.
	***/
	parse_icon_data();

	/*** Close the file ***/
	CARD_Close (&CardFile);

	/*** Unmount the card ***/
	CARD_Unmount (slot);

	return filesize + MCDATAOFFSET;
}

/****************************************************************************
* CardReadFile
*
* Retrieve a file from the previously populated list.
* Place in filebuffer space, for collection by SMB write.
****************************************************************************/
int CardReadFile (int slot, int id)
{
	int bytesdone = 0;
	int err;
	u32 SectorSize;
	char company[4];
	char gamecode[6];
	int filesize;

	if (id >= cardcount)
	{
		WaitPrompt("Bad id");
		return 0;			/*** Bad id ***/
	}

	/*** Clear the work buffers ***/
	memset (FileBuffer, 0, MAXFILEBUFFER);
	memset (SysArea, 0, CARD_WORKAREA);
	//add null char
	company[2] = gamecode[4] = 0;

	memcpy (company, &CardList[id].company, 2);
	memcpy (gamecode, &CardList[id].gamecode, 4);

	if (SlotIsVirtual(slot))
	{
		/*** a virtual card: the save straight from its blocks ***/
		if (v_open(slot, true) != VCARD_OK) return 0;
		const u8 *ent = vcard_entry(id);
		filesize = vcard_entry_blocks(id) * 8192;
		if (!ent || filesize + MCDATAOFFSET > MAXFILEBUFFER) { vcard_close(); WaitPrompt("Couldn't read that save."); return 0; }
		memcpy(&gci, ent, sizeof(card_direntry));
		memcpy(FileBuffer, &gci, sizeof(card_direntry));
		if (vcard_read(id, FileBuffer + MCDATAOFFSET, filesize) != VCARD_OK) { vcard_close(); WaitPrompt("Couldn't read that save."); return 0; }
		vcard_close();
		
		return filesize + MCDATAOFFSET;
	}

	/*** Mount the card ***/
	err = MountCard(slot);
	if (err < 0)
	{
		WaitCardError("CardMount", err);
		return 0;			/*** Unable to mount the card ***/
	}

	/*** Retrieve sector size ***/
	CARD_GetSectorSize (slot, &SectorSize);

	/*** Initialise for this company & gamecode ***/
	CARD_SetCompany(company);
	CARD_SetGamecode(gamecode);

	/*** Open the file ***/
	err = CARD_Open (slot, (char *) &CardList[id].filename, &CardFile);
	if (err < 0)
	{
		CARD_Unmount (slot);
		WaitCardError("CardOpen", err);
		return 0;
	}

#ifdef STATUSOGC
	/*** Get card status info ***/
	CARD_GetStatus (slot, CardFile.filenum, &CardStatus);
	CARD_GetAttributes(slot,CardFile.filenum, &permission);

	GCIMakeHeader();
#else
	//get directory entry (same as gci header, but with all the data)
	memset(&gci,0,sizeof(card_direntry));
	CARD_GetStatusEx(slot,CardFile.filenum,&gci);
	/*** Copy to head of buffer ***/
	memcpy(FileBuffer, &gci, sizeof(card_direntry));
#endif

	/*** Copy the file contents to the buffer ***/
	filesize = CardFile.len;

	while (bytesdone < filesize)
	{
		CARD_Read (&CardFile, FileBuffer + MCDATAOFFSET + bytesdone, SectorSize, bytesdone);
		bytesdone += SectorSize;
	}

	/*** Close the file ***/
	CARD_Close (&CardFile);

	/*** Unmount the card ***/
	CARD_Unmount (slot);

	return filesize + MCDATAOFFSET;
}

/****************************************************************************
* CardWriteFile
*
* Relies on *GOOD* data being placed in the FileBuffer prior to calling.
* See ReadSMBImage
****************************************************************************/
int CardWriteFile (int slot)
{
	char company[4];
	char gamecode[6];
	char filename[CARD_FILENAMELEN];
	int err, ret;
	u32 SectorSize;
	int offset;
	int written;
	int filelen;
	char txt[128];

	//add null char
	company[2] = gamecode[4] = 0;

	memset (SysArea, 0, CARD_WORKAREA);
	memset(filename, 0, CARD_FILENAMELEN);
	ExtractGCIHeader();
	memcpy(company, &gci.company, 2);
	memcpy(gamecode, &gci.gamecode, 4);
	memcpy(filename, &gci.filename, CARD_FILENAMELEN);
	filelen = gci.length * 8192;

	if (SlotIsVirtual(slot))
	{
		/*** a virtual card: replace what's there if asked, then write it whole ***/
		char name[48];
		CardSlotName(slot, name, sizeof(name));
		if (v_open(slot, true) != VCARD_OK) return 0;
		int there = vcard_find(gci.gamecode, gci.company, filename);
		if (there >= 0)
		{
			if (card_replace == REPLACE_YES) ret = 0;
			else if (card_replace == REPLACE_NO) ret = 1;
			else {
				sprintf(txt, "%s is already on %s. Replace it?", (char *)filename, name);
				ret = WaitPromptChoice(txt, "Replace", "Cancel");
				if (!ret) {
					sprintf(txt, "Sure? The copy on %s will be lost.", name);
					ret = WaitPromptChoiceAZ(txt, "Replace", "Cancel");
				}
			}
			if (ret) {
				vcard_close();
				if (card_replace == REPLACE_ASK) WaitPrompt("Left the save on the card as it was.");
				return 0;
			}
			if (vcard_delete(there) != VCARD_OK) { vcard_close(); WaitPrompt("Couldn't replace the save on the virtual card."); return 0; }
		}
		FZEROGX_MakeSaveGameValid(slot);
		PSO_MakeSaveGameValid(slot);
		u8 entry[64];
		memcpy(entry, &gci, 64);
		int r = vcard_write(entry, FileBuffer + MCDATAOFFSET + OFFSET, gci.length);
		OFFSET = 0;
		vcard_close();
		if (r == VCARD_ERR_NOSPACE) sprintf(txt, "There isn't room on %s for %s (%d blocks).", name, (char *)filename, gci.length);
		else if (r == VCARD_ERR_NOENTRY) sprintf(txt, "%s already holds 127 saves, as many as a card can.", name);
		else if (r != VCARD_OK) sprintf(txt, "Couldn't write %s to %s.", (char *)filename, name);
		if (r != VCARD_OK) { WaitPrompt(txt); return 0; }
		return 1;
	}

	/*** Mount the card ***/
	err = MountCard(slot);
	if (err < 0)
	{
		WaitCardError("CardMount", err);
		return 0;			/*** Unable to mount the card ***/
	}

	CARD_GetSectorSize (slot, &SectorSize);

	/*** Initialise for this company & gamecode ***/
	CARD_SetCompany(company);
	CARD_SetGamecode(gamecode);
	
	/*** If this file exists, abort ***/
	err = CARD_FindFirst (slot, &CardDir, false);
	while (err != CARD_ERROR_NOFILE)
	{
		if ((memcmp(CardDir.gamecode, &gamecode, 4) == 0) && (memcmp(CardDir.company, &company, 2) == 0) && (strcmp ((char *) CardDir.filename, (char *)filename) == 0))
		{
			/*** Found the file: replace it if that was chosen beforehand, or ask ***/
			if (card_replace == REPLACE_YES) ret = 0;
			else if (card_replace == REPLACE_NO) ret = 1;
			else {
				sprintf(txt, "%s is already on the memory card. Replace it?", (char *)filename);
				ret = WaitPromptChoice(txt, "Replace", "Cancel");
				if (!ret) {
					sprintf(txt, "Sure? The copy on the memory card will be lost.");
					ret = WaitPromptChoiceAZ(txt, "Replace", "Cancel");
				}
			}
			if (!ret){
				{
					err = CARD_Delete(slot, (char *) &filename);
					if (err < 0)
					{
						WaitCardError("MCDel", err);
						CARD_Unmount (slot);
						return 0;
					}
					err = CARD_FindFirst (slot, &CardDir, false);
					continue;
				}
			}

			/*** Not replaced: leave it ***/
			CARD_Unmount (slot);
			if (card_replace == REPLACE_ASK) WaitPrompt("Left the save on the memory card as it was.");
			return 0;
		}

		err = CARD_FindNext (&CardDir);
	}

tryagain:
	/*** Initialise for this company & gamecode ***/
	//Again just in case, as this is very important for propper restoring
	CARD_SetCompany(company);
	CARD_SetGamecode(gamecode);
	
	/*** Now restore the file from backup ***/
	err = CARD_Create (slot, (char *) filename, filelen, &CardFile);
	if (err < 0)
	{
		if (err == CARD_ERROR_EXIST)
		{
			/*** Found the file - prompt user ***/
			sprintf(txt, "File %s(%s%s) already exists. Overwrite?", (char *) filename, gamecode, company);
			ret = WaitPromptChoice(txt, "Overwrite", "Cancel");
			if (!ret){
				sprintf(txt, "Are you -SURE- you want to overwrite %s?", (char *) filename);
				ret = WaitPromptChoiceAZ(txt, "Overwrite", "Cancel");
				if(!ret){
					err = CARD_Delete(slot, (char *) &filename);
					if (err < 0)
					{
						WaitCardError("MCDel", err);
						CARD_Unmount (slot);
						return 0;
					}
					goto tryagain;
				}
			}
		}
		CARD_Unmount (slot);
		WaitCardError("CardCreate", err);
		return 0;
	}

//Thanks to Ralf, validate F-zero and PSO savegames
	FZEROGX_MakeSaveGameValid(slot);
	PSO_MakeSaveGameValid(slot);

	/*** Now write the file data, in sector sized chunks ***/
	offset = 0;
	while (offset < filelen)
	{
		if ((offset + SectorSize) <= filelen)
		{
			written = CARD_Write (&CardFile, FileBuffer + MCDATAOFFSET + offset + OFFSET, SectorSize, offset);
		}
		else
		{
			written = CARD_Write (&CardFile, FileBuffer + MCDATAOFFSET + offset + OFFSET, ((offset + SectorSize) - filelen), offset);
		}

		offset += SectorSize;
	}

	OFFSET = 0;

#ifdef STATUSOGC
	/*** Finally, update the status ***/
	CARD_SetStatus (slot, CardFile.filenum, &CardStatus);
	//For some reason this sets the file to Move->allowed, Copy->not allowed, Public file instead of the actual permission value
	CARD_SetAttributes(slot, CardFile.filenum, &permission);
#else
	CARD_SetStatusEx(slot, CardFile.filenum, &gci);
#endif

	CARD_Close (&CardFile);
	CARD_Unmount (slot);

	return 1;
}

void WaitCardError(char *src, int error)
{
	char msg[1024], err[256];

	//Error message possibilites
	switch (error)
	{
	case CARD_ERROR_BUSY:
		sprintf(err, "Busy");
		break;
	case CARD_ERROR_WRONGDEVICE:
		sprintf(err, "Wrong device in slot");
		break;
	case CARD_ERROR_NOCARD:
		sprintf(err, "No Card");
		break;
	case CARD_ERROR_NOFILE:
		sprintf(err, "No File");
		break;
	case CARD_ERROR_IOERROR:
		sprintf(err, "Internal EXI I/O error");
		break;
	case CARD_ERROR_BROKEN:
		sprintf(err, "File/Dir entry broken");
		break;
	case CARD_ERROR_EXIST:
		sprintf(err, "File already exists");
		break;
	case CARD_ERROR_NOENT:
		sprintf(err, "No empty blocks");
		break;
	case CARD_ERROR_INSSPACE:
		sprintf(err, "Not enough space");
		break;
	case CARD_ERROR_NOPERM:
		sprintf(err, "No permission");
		break;
	case CARD_ERROR_LIMIT:
		sprintf(err, "Card size limit reached");
		break;
	case CARD_ERROR_NAMETOOLONG:
		sprintf(err, "Filename too long");
		break;
	case CARD_ERROR_ENCODING:
		sprintf(err, "Font encoding mismatch");
		break;
	case CARD_ERROR_CANCELED:
		sprintf(err, "Card operation cancelled");
		break;
	case CARD_ERROR_FATAL_ERROR:
		sprintf(err, "Fatal error");
		break;
	case CARD_ERROR_READY:
		sprintf(err, "Ready");
		break;
	default:
		sprintf(err, "Unknown");
		break;
	}
	//Here we build the full error message
	sprintf(msg, "MemCard Error: %s - %d %s", src,error, err);

	WaitPrompt(msg);
}

/****************************************************************************
 * CardDeleteSave
 *
 * Deletes save id (from CardList) off the card. Returns true if it went.
 ***************************************************************************/
bool CardDeleteSave(int slot, int id)
{
	if (SlotIsVirtual(slot))
	{
		if (v_open(slot, true) != VCARD_OK) return false;
		bool ok = vcard_delete(id) == VCARD_OK;
		vcard_close();
		if (!ok) WaitPrompt("Couldn't delete the save from the virtual card.");
		return ok;
	}
	int err = MountCard(slot);
	if (err < 0)
	{
		WaitCardError("MCDel Mount", err);
		return false;
	}
	CARD_SetCompany((const char *)CardList[id].company);
	CARD_SetGamecode((const char *)CardList[id].gamecode);
	err = CARD_Delete(slot, (char *) &CardList[id].filename);
	CARD_SetCompany(NULL);
	CARD_SetGamecode(NULL);
	CARD_Unmount(slot);
	if (err < 0)
	{
		WaitCardError("MCDel", err);
		return false;
	}
	return true;
}

void MC_FormatMode(s32 slot)
{
	int erase = 1;
	int err;

	if (SlotIsVirtual(slot))
	{
		const VirtualCard *v = VirtualCardGet(slot);
		char name[48], msg[160];
		if (!v) return;
		CardSlotName(slot, name, sizeof(name));
		snprintf(msg, sizeof(msg), "Format %s? Everything on it will be erased.", name);
		if (WaitPromptChoice(msg, "Cancel", "Format") != 1) return;
		snprintf(msg, sizeof(msg), "Sure? All the saves on %s will be lost.", name);
		if (WaitPromptChoiceAZ(msg, "Format", "Cancel") != 0) return;   /* (Z formats) */
		ShowAction("Formatting\x85");
		char path[300], region[8];
		snprintf(path, sizeof(path), "%s", v->path);
		snprintf(region, sizeof(region), "%s", v->region);
		bool ok = v_format(path, v->letter, region, v->blocks) == VCARD_OK;
		VirtualCardsScan();
		WaitPrompt(ok ? "Formatted. The virtual card is empty and ready." : "Couldn't format the virtual card.");
		return;
	}

	//0 = B wass pressed -> ask again
	if (!slot){
		erase = WaitPromptChoice("Are you sure you want to format memory card in slot A?", "Format", "Cancel");
	}else{
		erase = WaitPromptChoice("Are you sure you want to format memory card in slot B?", "Format", "Cancel");
	}

	if (!erase){
		if (!slot){
			erase = WaitPromptChoiceAZ("All contents of memory card in slot A will be erased!", "Format", "Cancel");
		}else{
			erase = WaitPromptChoiceAZ("All contents of memory card in slot B will be erased!", "Format", "Cancel");
		}

		if (!erase)
		{

			/*** Try to mount the card ***/
			err = MountCard(slot);
			if (err < 0)
			{
				WaitCardError("MCFormat Mount", err);
				return; /*** Unable to mount the card ***/
			}
			ShowAction("Formatting card...");
			/*** Format the card ***/
			CARD_Format(slot);
			usleep(1000*1000);

			/*** Try to mount the card ***/
			err = MountCard(slot);
			if (err < 0)
			{
				WaitCardError("MCFormat Mount", err);
				return; /*** Unable to mount the card ***/
			}else
			{
				WaitPrompt("Format completed successfully");
			}

				CARD_Unmount(slot);
				return;
		}
	}

	WaitPrompt("Format operation cancelled");
	return;
}

//The following code is made by Ralf at GSCentral forums (gscentral.org)
//http://board.gscentral.org/retro-hacking/53093.htm#post188949

// u8 FileBuffer[MAXFILEBUFFER]: .gci file buffer
// MCDATAOFFSET: .gci header size (0x40 bytes)

/*************************************************************/
/* FZEROGX_MakeSaveGameValid                                 */
/* (use just before writing a F-Zero GX system .gci file)    */
/*                                                           */
/* chn: Destination memory card port                         */
/* ret: Error code                                           */
/*************************************************************/

s32 FZEROGX_MakeSaveGameValid(s32 chn)
{
	s32 ret;
	u32 i,j;
	u32 serial1,serial2;
	u16 chksum = 0xFFFF;

	if(strcasecmp(&FileBuffer[0x08],"f_zero.dat")!=0) return CARD_ERROR_READY;		// check for F-Zero GX system file
	if((ret=card_serial(chn,&serial1,&serial2))<0) return ret;			// get encrypted destination memory card serial numbers

	*(u16*)&FileBuffer[0x2066+MCDATAOFFSET] = serial1 >> 16;			// set new serial numbers
	*(u16*)&FileBuffer[0x7580+MCDATAOFFSET] = serial2 >> 16;
	*(u16*)&FileBuffer[0x2060+MCDATAOFFSET] = serial1 & 0xFFFF;
	*(u16*)&FileBuffer[0x2200+MCDATAOFFSET] = serial2 & 0xFFFF;

	for(i=0x02+MCDATAOFFSET;i<0x8000+MCDATAOFFSET;i++) {				// calc 16-bit checksum
		chksum ^= (FileBuffer[i]&0xFF);

		for(j=8;j>0;j--) {
			if (chksum&1) chksum = (chksum>>1)^0x8408;
			else chksum >>= 1;
		}
	}

	*(u16*)&FileBuffer[0x00+MCDATAOFFSET] = ~chksum;				// set new checksum

	return ret;
}

/***********************************************************/
/* PSO_MakeSaveGameValid	                           */
/* (use just before writing a PSO system .gci file)        */
/*                                                         */
/* chn: Destination memory card port                       */
/* ret: Error code                                         */
/***********************************************************/

s32 PSO_MakeSaveGameValid(s32 chn)
{
	s32 ret;
	u32 i,j;
	u32 chksum;
	u32 crc32LUT[256];
	u32 serial1,serial2;
	u32 pso3offset;

	if(strcasecmp(&FileBuffer[0x08],"PSO_SYSTEM")==0) {				// check for PSO1&2 system file
		pso3offset = 0x00;
		goto exit;
	}

	if(strcasecmp(&FileBuffer[0x08],"PSO3_SYSTEM")==0) {				// check for PSO3 system file
		pso3offset = 0x10;							// PSO3 data block size adjustment
		goto exit;
	}

	return CARD_ERROR_READY;							// nothing to do
exit:
	if((ret=card_serial(chn,&serial1,&serial2))<0) return ret;			// get encrypted destination memory card serial numbers

	*(u32*)&FileBuffer[0x2158+MCDATAOFFSET] = serial1;				// set new serial numbers
	*(u32*)&FileBuffer[0x215C+MCDATAOFFSET] = serial2;

	for(i=0;i<256;i++) {								// generate crc32 LUT
	        chksum = i;

        	for(j=8;j>0;j--) {
			if (chksum&1) chksum = (chksum>>1)^0xEDB88320;
             		else chksum >>= 1;
		}

        	crc32LUT[i] = chksum;
	}

	chksum = 0xDEBB20E3;								// PSO initial crc32 value

	for (i=0x204C+MCDATAOFFSET;i<0x2164+pso3offset+MCDATAOFFSET;i++) {		// calc 32-bit checksum
		chksum = ((chksum>>8)&0xFFFFFF)^crc32LUT[(chksum^FileBuffer[i])&0xFF];
	}

	*(u32*)&FileBuffer[0x2048+MCDATAOFFSET] = chksum^0xFFFFFFFF;			// set new checksum

	return ret;
}

