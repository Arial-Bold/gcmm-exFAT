/****************************************************************************
* saves.h - the saves the lists show: on the memory card (with each one's
* icon and comment, and whether it's backed up), or on the SD card (and
* whether it's on the memory card already).
****************************************************************************/
#ifndef _GCMM_SAVES_H_
#define _GCMM_SAVES_H_

#include <gccore.h>
#include <stdbool.h>

#define SAVES_MAX 1024

typedef struct {
	char title[33];     /* the comment's first line (or the file's name) */
	char detail[33];    /* its second line */
	char key[48];       /* company-gamecode-filename, to match copies */
	int index;          /* in CardList (card) or filelist (SD) */
	int blocks;
	u32 modified;       /* seconds since 2000 (0: not known) */
	u8 status;          /* UI_ST_* */
	bool folder, checked, looked, has_icon;
	u8 *icon;           /* 32x32, blue green red (once looked at) */
} SaveRow;

extern SaveRow save_rows[SAVES_MAX];
extern int save_count;

void saves_clear(void);

/* The saves on the memory card, each looked at (icon, comment, date), and
 * marked against the backups in MCBACKUP. Shows its progress in the
 * description bar. Returns the count (0 too if the card couldn't be read). */
int saves_load_card(int slot);
int saves_load_card_vs(int slot, int other);   /* for copying: marked against the other card */

/* The folders and saves in the SD card's current folder (currFolder), marked
 * against what's on the memory card (if there is one). Rows are looked at
 * when they're shown (saves_look). mode: as SDGetFileList (1 saves, 0 card
 * images). */
int saves_load_sd(int mode, int slot);
void saves_look(int first, int count);   /* SD rows: their icons and comments */

/* How many saves on the card have no backup in MCBACKUP at all (-1: no card
 * or no SD card). Mounts the card for its list. */
int saves_count_new(int slot, int *total);

#endif
