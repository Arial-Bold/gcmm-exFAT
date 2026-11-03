/* vcard.h - virtual memory cards: card images (.raw) on the SD card, as a
 * Launcher keeps them in swiss/saves (MemoryCardA.USA.raw and so on), read
 * and written the way a real memory card is.
 *
 * The image is the card byte for byte: 8 KB blocks; block 0 the header,
 * blocks 1-2 two copies of the directory, blocks 3-4 two copies of the block
 * table, then the saves. The newer of each pair (by its update counter, with
 * a good checksum) is the one in use; a change is written to the other copy
 * with the counter one higher, as the GameCube's own card library does, so a
 * write that stops halfway leaves the card as it was.
 *
 * Directory entries are handled as their 64 raw bytes (the same layout as
 * libogc's card_direntry on the GameCube), so this file also runs on a PC
 * for testing. One card is open at a time. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

#define VCARD_BLOCK 8192
#define VCARD_ENTRY 64
#define VCARD_ENTRIES 127

/* results */
enum {
	VCARD_OK = 0,
	VCARD_ERR_IO = -1,        /* couldn't read or write the file */
	VCARD_ERR_UNFORMATTED = -2,/* blank, or not a card image */
	VCARD_ERR_BROKEN = -3,    /* directory or block table damaged in both copies */
	VCARD_ERR_EXISTS = -4,    /* a save with that game, maker and name is there */
	VCARD_ERR_NOSPACE = -5,   /* not enough free blocks */
	VCARD_ERR_NOENTRY = -6,   /* the directory is full (127 saves) */
};

/* what a new card is made from (the console's own values, as a real format uses) */
typedef struct {
	uint8_t flash_id[12];     /* this console's flash ID for the slot */
	uint32_t counter_bias;    /* from SRAM */
	uint32_t language;        /* from SRAM */
	uint16_t vi_dtv;          /* the VI's DTV status register */
	uint64_t time;            /* now, in console ticks */
	uint16_t encoding;        /* 0 ANSI, 1 Shift-JIS (Japanese) */
} VCardFormat;

int  vcard_open(const char *path);          /* reads the system blocks; VCARD_OK or an error */
void vcard_close(void);
int  vcard_blocks(void);                     /* blocks for saves (59, 251, 1019, 2043) */
int  vcard_free(void);                       /* free blocks */
int  vcard_count(void);                      /* saves on the card */
const uint8_t *vcard_entry(int n);           /* the n-th save's 64-byte directory entry */
int  vcard_entry_blocks(int n);              /* its size in blocks */
int  vcard_find(const uint8_t *gamecode4, const uint8_t *company2, const char *filename);   /* n, or -1 */
int  vcard_read(int n, uint8_t *buf, uint32_t len);   /* the save's data (up to len bytes) */
/* A new save from its directory entry (block and length are filled in) and
 * data (length blocks of it). */
int  vcard_write(const uint8_t *entry, const uint8_t *data, int blocks);
int  vcard_delete(int n);
void vcard_serial(uint32_t *s1, uint32_t *s2); /* as CARD_GetSerialNo gives it */

/* Makes path a newly formatted card of `blocks` save blocks (59, 251, 1019 or 2043) */
int  vcard_format(const char *path, int blocks, const VCardFormat *f);
/* The card size of an image file, in save blocks, from its length; 0 if it isn't one */
int  vcard_blocks_for_size(long bytes);
