/****************************************************************************
* saves.c - see saves.h
****************************************************************************/
#include <gccore.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <dirent.h>
#include <ogc/libversion.h>
#include "card.h"
#include "gci.h"
#include "mcard.h"
#include "sdsupp.h"
#include "bannerload.h"
#include "bitmap.h"
#include "freetype.h"
#include "ui.h"
#include "saves.h"

SaveRow save_rows[SAVES_MAX];
int save_count;

/* from mcard.c and sdsupp.c */
extern card_dir CardList[];
extern u8 filelist[1024][1024];
extern u32 maxfile;
extern card_direntry gci;
extern u8 CommentBuffer[64];
extern u8 icondata[8][1024];
extern u16 icondataRGB[8][1024];
extern u16 tlut[9][256];
extern int numicons;
extern int iconindex[2 * CARD_MAXICONS - 2];
extern u8 currFolder[260];
extern char fatpath[4];

void saves_clear(void)
{
	for (int i = 0; i < SAVES_MAX; i++) free(save_rows[i].icon);
	memset(save_rows, 0, sizeof(save_rows));
	save_count = 0;
}

/* ------------------------------------------------------------ the pieces */

static void line_of(char *out, const u8 *src)
{
	int j;
	for (j = 0; j < 32; j++) {
		char c = (char)src[j];
		if (c == 0 || c == 10) break;
		out[j] = (c < 32) ? ' ' : c;
	}
	out[j] = 0;
	while (j > 0 && out[j - 1] == ' ') out[--j] = 0;
}

static void make_key(char *out, size_t n, const u8 *company, const u8 *gamecode, const u8 *filename)
{
	char name[CARD_FILENAMELEN + 1];
	memcpy(name, filename, CARD_FILENAMELEN);
	name[CARD_FILENAMELEN] = 0;
	snprintf(out, n, "%.2s-%.4s-%s", (const char *)company, (const char *)gamecode, name);
}

/* the first frame of the icon just read (into decoded_icon) */
static bool decode_icon(void)
{
	if (numicons <= 0) return false;
	int k = iconindex[0];
	if (k < 0 || k >= CARD_MAXICONS) return false;
	switch (SDCARD_GetIconFmt(gci.icon_fmt, k)) {
	case 1: iconloadCI(icondata[k], tlut[8]); return true;   /* shared palette */
	case 3: iconloadCI(icondata[k], tlut[k]); return true;
	case 2: iconloadRGB(icondataRGB[k]); return true;
	}
	return false;
}

/* the comment, date, size and icon of the save just read into gci */
static void take_look(SaveRow *r)
{
	char l1[33], l2[33];
	line_of(l1, CommentBuffer);
	line_of(l2, CommentBuffer + 32);
	if (l1[0]) { snprintf(r->title, sizeof(r->title), "%s", l1); snprintf(r->detail, sizeof(r->detail), "%s", l2); }
	r->modified = gci.last_modified;
	r->blocks = gci.length;
	if (decode_icon()) {
		if (!r->icon) r->icon = malloc(32 * 32 * 3);
		if (r->icon) { memcpy(r->icon, decoded_icon, 32 * 32 * 3); r->has_icon = true; }
	}
	r->looked = true;
}

/* ------------------------------------------------------- the memory card */

static void card_done(int id, bool ok, void *ctx)
{
	(void)ctx;
	if (id < save_count && ok) take_look(&save_rows[id]);
	char l2[48];
	snprintf(l2, sizeof(l2), "%d of %d", id + 1, save_count);
	ui_status("Reading the memory card\x85", l2);
	ui_progress(save_count ? (float)(id + 1) / save_count : 1);
}

/* the backups in MCBACKUP (top level): their names, for matching */
static char (*backups)[256];
static int nbackups;

static void list_backups(void)
{
	free(backups);
	backups = NULL;
	nbackups = 0;
	char dir[300];
	snprintf(dir, sizeof(dir), "%s:/%s", fatpath, MCSAVES);
	DIR *d = opendir(dir);
	if (!d) return;
	int cap = 0;
	struct dirent *de;
	while ((de = readdir(d)) != NULL) {
		size_t n = strlen(de->d_name);
		if (n < 5 || strcasecmp(de->d_name + n - 4, ".gci")) continue;
		if (nbackups == cap) {
			cap = cap ? cap * 2 : 64;
			void *b = realloc(backups, sizeof(*backups) * cap);
			if (!b) break;
			backups = b;
		}
		snprintf(backups[nbackups++], 256, "%s", de->d_name);
	}
	closedir(d);
}

/* the newest backup of the save with this key (its date), 0 if there's none */
static bool newest_backup(const char *key, u32 *when)
{
	size_t kl = strlen(key);
	bool found = false;
	*when = 0;
	for (int i = 0; i < nbackups; i++) {
		if (strncmp(backups[i], key, kl) || backups[i][kl] != '_') continue;
		found = true;
		char path[600];
		snprintf(path, sizeof(path), "%s:/%s/%s", fatpath, MCSAVES, backups[i]);
		FILE *f = fopen(path, "rb");
		if (!f) continue;
		card_direntry h;
		if (fread(&h, 1, sizeof(h), f) == sizeof(h) && h.last_modified > *when) *when = h.last_modified;
		fclose(f);
	}
	return found;
}

int saves_load_card(int slot)
{
	saves_clear();
	ui_status("Reading the memory card\x85", "");
	int n = CardGetDirectory(slot);
	if (n <= 0) { ui_progress(-1); return 0; }
	if (n > SAVES_MAX) n = SAVES_MAX;
	save_count = n;
	for (int i = 0; i < n; i++) {
		SaveRow *r = &save_rows[i];
		r->index = i;
		memcpy(r->title, CardList[i].filename, CARD_FILENAMELEN);
		r->title[CARD_FILENAMELEN] = 0;
		make_key(r->key, sizeof(r->key), CardList[i].company, CardList[i].gamecode, (const u8 *)CardList[i].filename);
		r->blocks = (CardList[i].filelen + 8191) / 8192;
	}
	ui_progress(0);
	CardReadSummaries(slot, card_done, NULL);
	ui_progress(-1);
	/* against the backups on the SD card */
	list_backups();
	for (int i = 0; i < n; i++) {
		SaveRow *r = &save_rows[i];
		u32 when;
		if (!newest_backup(r->key, &when)) r->status = UI_ST_NEW;
		else r->status = (r->modified && when && r->modified > when) ? UI_ST_CHANGED : UI_ST_BACKED_UP;
	}
	return n;
}

int saves_count_new(int slot, int *total)
{
	*total = 0;
	if (CARD_Probe(slot) <= 0) return -1;
	int n = CardGetDirectory(slot);
	if (n <= 0) return n < 0 ? -1 : 0;
	*total = n;
	list_backups();
	int fresh = 0;
	for (int i = 0; i < n; i++) {
		char key[48];
		u32 when;
		make_key(key, sizeof(key), CardList[i].company, CardList[i].gamecode, (const u8 *)CardList[i].filename);
		if (!newest_backup(key, &when)) fresh++;
	}
	return fresh;
}

/* ------------------------------------------------------------ the SD card */

static char (*card_keys)[48];
static int ncard_keys;
static bool have_card_list;   /* the memory card was read (it may be empty) */
static int list_mode;

int saves_load_sd(int mode, int slot)
{
	saves_clear();
	list_mode = mode;
	/* what's on the memory card, to say which saves are there already */
	free(card_keys);
	card_keys = NULL;
	ncard_keys = 0;
	have_card_list = false;
	if (mode == 1 && CARD_Probe(slot) > 0) {
		ui_status("Reading the memory card\x85", "");
		int n = CardGetDirectory(slot);
		have_card_list = n >= 0;
		if (n > 0 && (card_keys = malloc(sizeof(*card_keys) * n)) != NULL) {
			for (int i = 0; i < n; i++)
				make_key(card_keys[i], 48, CardList[i].company, CardList[i].gamecode, (const u8 *)CardList[i].filename);
			ncard_keys = n;
		}
	}
	ui_status("Reading the SD card\x85", "");
	int files = SDGetFileList(mode);
	if (files <= 0) return 0;
	if (files > SAVES_MAX) files = SAVES_MAX;
	save_count = files;
	for (int i = 0; i < files; i++) {
		SaveRow *r = &save_rows[i];
		r->index = i;
		snprintf(r->title, sizeof(r->title), "%s", (char *)filelist[i]);
		char path[1300];
		snprintf(path, sizeof(path), "%s:/%s/%s", fatpath, currFolder, (char *)filelist[i]);
		r->folder = isdir_sd(path) == 1;
		r->looked = r->folder || mode != 1;
		if (!r->folder && mode != 1) {
			/* a whole-card image: its size */
			FILE *f = fopen(path, "rb");
			if (f) {
				fseek(f, 0, SEEK_END);
				long sz = ftell(f);
				fclose(f);
				snprintf(r->detail, sizeof(r->detail), "%ld blocks", sz / 8192 - 5 > 0 ? sz / 8192 - 5 : 0);
			}
		}
	}
	return files;
}

void saves_look(int first, int count)
{
	sd_quiet = 1;
	for (int i = first; i < first + count && i < save_count; i++) {
		SaveRow *r = &save_rows[i];
		if (r->looked) continue;
		r->looked = true;
		if (!SDLoadMCImageHeader((char *)filelist[r->index])) { r->status = UI_ST_NONE; continue; }
		take_look(r);
		make_key(r->key, sizeof(r->key), (const u8 *)gci.company, (const u8 *)gci.gamecode, (const u8 *)gci.filename);
		r->status = UI_ST_NOT_ON_CARD;
		for (int k = 0; k < ncard_keys; k++)
			if (!strcmp(card_keys[k], r->key)) { r->status = UI_ST_ON_CARD; break; }
		if (!have_card_list) r->status = UI_ST_NONE;   /* (no memory card to compare with) */
	}
	sd_quiet = 0;
}
