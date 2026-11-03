/* vcard.c - see vcard.h. The layout and rules follow libogc's card.c. */
#include "vcard.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define SYSAREA 5            /* header, 2 directories, 2 block tables */
#define FAT_MAX 0xFFB        /* block table entries */

static FILE *f;
static uint8_t sys[SYSAREA][VCARD_BLOCK];   /* the system blocks, as on the card */
static int dir_cur, fat_cur;                 /* which copy (0 or 1) is in use */
static int total;                            /* blocks on the card, the system ones too */

/* ---------------------------------------------------------------- bytes */

static uint16_t be16(const uint8_t *p) { return (uint16_t)(p[0] << 8 | p[1]); }
static uint32_t be32(const uint8_t *p) { return (uint32_t)p[0] << 24 | (uint32_t)p[1] << 16 | (uint32_t)p[2] << 8 | p[3]; }
static void put16(uint8_t *p, uint16_t v) { p[0] = v >> 8; p[1] = (uint8_t)v; }
static void put32(uint8_t *p, uint32_t v) { p[0] = v >> 24; p[1] = (uint8_t)(v >> 16); p[2] = (uint8_t)(v >> 8); p[3] = (uint8_t)v; }

/* the card library's checksum: the sum of the 16-bit words, and of their inverses */
static void checksum(const uint8_t *p, int len, uint16_t *c1, uint16_t *c2) {
	uint16_t a = 0, b = 0;
	for (int i = 0; i + 1 < len; i += 2) {
		uint16_t w = be16(p + i);
		a += w;
		b += w ^ 0xFFFF;
	}
	if (a == 0xFFFF) a = 0;
	if (b == 0xFFFF) b = 0;
	*c1 = a;
	*c2 = b;
}

/* a block's place in the system area */
static uint8_t *dirblk(int c) { return sys[1 + c]; }
static uint8_t *fatblk(int c) { return sys[3 + c]; }
#define DIR_UPDATED 0x1FFA
#define DIR_CK 0x1FFC
#define FAT_UPDATED 4
#define FAT_FREE 6
#define FAT_LAST 8
#define FAT_MAP 10

static uint16_t fat_get(const uint8_t *fat, int block) { return be16(fat + FAT_MAP + (block - SYSAREA) * 2); }
static void fat_set(uint8_t *fat, int block, uint16_t v) { put16(fat + FAT_MAP + (block - SYSAREA) * 2, v); }

static bool dir_ok(int c) {
	uint16_t a, b;
	checksum(dirblk(c), DIR_CK, &a, &b);
	return a == be16(dirblk(c) + DIR_CK) && b == be16(dirblk(c) + DIR_CK + 2);
}
static bool fat_ok(int c) {
	uint16_t a, b;
	checksum(fatblk(c) + 4, VCARD_BLOCK - 4, &a, &b);
	if (a != be16(fatblk(c)) || b != be16(fatblk(c) + 2)) return false;
	int free = 0;
	for (int i = SYSAREA; i < total; i++) if (!fat_get(fatblk(c), i)) free++;
	return free == be16(fatblk(c) + FAT_FREE);
}

static bool io_read(long at, void *buf, size_t n) { return !fseek(f, at, SEEK_SET) && fread(buf, 1, n, f) == n; }
static bool io_write(long at, const void *buf, size_t n) { return !fseek(f, at, SEEK_SET) && fwrite(buf, 1, n, f) == n; }

/* -------------------------------------------------------------- opening */

int vcard_blocks_for_size(long bytes) {
	static const int mbits[] = { 4, 8, 16, 32, 64, 128 };
	for (unsigned i = 0; i < sizeof(mbits) / sizeof(mbits[0]); i++)
		if (bytes == (long)mbits[i] * 16 * VCARD_BLOCK) return mbits[i] * 16 - SYSAREA;
	return 0;
}

int vcard_open(const char *path) {
	vcard_close();
	f = fopen(path, "r+b");
	if (!f) return VCARD_ERR_IO;
	if (!io_read(0, sys, sizeof(sys))) { vcard_close(); return VCARD_ERR_UNFORMATTED; }
	/* the header: its checksum, and a card size that makes sense for the file */
	uint16_t a, b;
	checksum(sys[0], 0x1FC, &a, &b);
	int mbits = be16(sys[0] + 0x22);
	fseek(f, 0, SEEK_END);
	long len = ftell(f);
	if (a != be16(sys[0] + 0x1FC) || b != be16(sys[0] + 0x1FE) || mbits < 4 || mbits > 128 || (mbits & (mbits - 1)) ||
	    len < (long)mbits * 16 * VCARD_BLOCK) {
		vcard_close();
		return VCARD_ERR_UNFORMATTED;
	}
	total = mbits * 16;
	/* the copies in use: good ones, the newer if both are */
	bool d0 = dir_ok(0), d1 = dir_ok(1), f0 = fat_ok(0), f1 = fat_ok(1);
	if ((!d0 && !d1) || (!f0 && !f1)) { vcard_close(); return VCARD_ERR_BROKEN; }
	dir_cur = d0 && d1 ? (be16(dirblk(1) + DIR_UPDATED) > be16(dirblk(0) + DIR_UPDATED)) : d1;
	fat_cur = f0 && f1 ? (be16(fatblk(1) + FAT_UPDATED) > be16(fatblk(0) + FAT_UPDATED)) : f1;
	return VCARD_OK;
}

void vcard_close(void) {
	if (f) fclose(f);
	f = NULL;
	total = 0;
}

int vcard_blocks(void) { return total ? total - SYSAREA : 0; }
int vcard_free(void) { return total ? be16(fatblk(fat_cur) + FAT_FREE) : 0; }

/* the n-th save: the n-th used directory entry */
static int slot_of(int n) {
	for (int i = 0; i < VCARD_ENTRIES; i++)
		if (dirblk(dir_cur)[i * VCARD_ENTRY] != 0xFF && n-- == 0) return i;
	return -1;
}

int vcard_count(void) {
	int n = 0;
	if (!total) return 0;
	for (int i = 0; i < VCARD_ENTRIES; i++) if (dirblk(dir_cur)[i * VCARD_ENTRY] != 0xFF) n++;
	return n;
}

const uint8_t *vcard_entry(int n) {
	int s = slot_of(n);
	return s < 0 ? NULL : dirblk(dir_cur) + s * VCARD_ENTRY;
}

int vcard_entry_blocks(int n) {
	const uint8_t *e = vcard_entry(n);
	return e ? be16(e + 0x38) : 0;
}

int vcard_find(const uint8_t *gamecode4, const uint8_t *company2, const char *filename) {
	int count = vcard_count();
	for (int n = 0; n < count; n++) {
		const uint8_t *e = vcard_entry(n);
		if (!memcmp(e, gamecode4, 4) && !memcmp(e + 4, company2, 2) && !strncmp((const char *)e + 8, filename, 32)) return n;
	}
	return -1;
}

void vcard_serial(uint32_t *s1, uint32_t *s2) {
	const uint8_t *h = sys[0];
	*s1 = be32(h) ^ be32(h + 8) ^ be32(h + 16) ^ be32(h + 24);
	*s2 = be32(h + 4) ^ be32(h + 12) ^ be32(h + 20) ^ be32(h + 28);
}

/* ---------------------------------------------------------- reading saves */

int vcard_read(int n, uint8_t *buf, uint32_t len) {
	const uint8_t *e = vcard_entry(n);
	if (!e) return VCARD_ERR_IO;
	int blocks = be16(e + 0x38), block = be16(e + 0x36);
	uint32_t done = 0;
	for (int i = 0; i < blocks && done < len; i++) {
		if (block < SYSAREA || block >= total) return VCARD_ERR_BROKEN;
		uint32_t take = len - done < VCARD_BLOCK ? len - done : VCARD_BLOCK;
		if (!io_read((long)block * VCARD_BLOCK, buf + done, take)) return VCARD_ERR_IO;
		done += take;
		block = fat_get(fatblk(fat_cur), block);
	}
	return VCARD_OK;
}

/* --------------------------------------------------------- changing it */

/* The directory, then the block table, as changed in the other copies:
 * each written to its spare place with its counter one on, so the card is
 * whole whichever write was the last to finish. */
static int commit_dir(uint8_t *newdir) {
	int other = dir_cur ^ 1;
	put16(newdir + DIR_UPDATED, be16(dirblk(dir_cur) + DIR_UPDATED) + 1);
	uint16_t a, b;
	checksum(newdir, DIR_CK, &a, &b);
	put16(newdir + DIR_CK, a);
	put16(newdir + DIR_CK + 2, b);
	if (!io_write((long)(1 + other) * VCARD_BLOCK, newdir, VCARD_BLOCK)) return VCARD_ERR_IO;
	memcpy(dirblk(other), newdir, VCARD_BLOCK);
	dir_cur = other;
	return VCARD_OK;
}
static int commit_fat(uint8_t *newfat) {
	int other = fat_cur ^ 1;
	put16(newfat + FAT_UPDATED, be16(fatblk(fat_cur) + FAT_UPDATED) + 1);
	uint16_t a, b;
	checksum(newfat + 4, VCARD_BLOCK - 4, &a, &b);
	put16(newfat, a);
	put16(newfat + 2, b);
	if (!io_write((long)(3 + other) * VCARD_BLOCK, newfat, VCARD_BLOCK)) return VCARD_ERR_IO;
	memcpy(fatblk(other), newfat, VCARD_BLOCK);
	fat_cur = other;
	return VCARD_OK;
}

int vcard_write(const uint8_t *entry, const uint8_t *data, int blocks) {
	if (!total) return VCARD_ERR_IO;
	if (vcard_find(entry, entry + 4, (const char *)entry + 8) >= 0) return VCARD_ERR_EXISTS;
	int slot = -1;
	for (int i = 0; i < VCARD_ENTRIES && slot < 0; i++) if (dirblk(dir_cur)[i * VCARD_ENTRY] == 0xFF) slot = i;
	if (slot < 0) return VCARD_ERR_NOENTRY;
	if (blocks < 1 || blocks > vcard_free()) return VCARD_ERR_NOSPACE;

	/* blocks, chained, from after the last one used (as the card library does) */
	static uint8_t fat[VCARD_BLOCK], dir[VCARD_BLOCK];
	memcpy(fat, fatblk(fat_cur), VCARD_BLOCK);
	int cur = be16(fat + FAT_LAST), first = -1, prev = -1;
	for (int got = 0; got < blocks; got++) {
		int tries = 0;
		do {
			cur++;
			if (cur >= total) cur = SYSAREA;
		} while (fat_get(fat, cur) && ++tries < total);
		if (fat_get(fat, cur)) return VCARD_ERR_NOSPACE;
		if (prev >= 0) fat_set(fat, prev, (uint16_t)cur);
		else first = cur;
		fat_set(fat, cur, 0xFFFF);   /* the end, for now */
		prev = cur;
	}
	put16(fat + FAT_FREE, be16(fat + FAT_FREE) - blocks);
	put16(fat + FAT_LAST, (uint16_t)prev);

	/* the save's data into its blocks, while nothing points at them yet */
	for (int b = first, i = 0; i < blocks; i++, b = fat_get(fat, b))
		if (!io_write((long)b * VCARD_BLOCK, data + (long)i * VCARD_BLOCK, VCARD_BLOCK)) return VCARD_ERR_IO;

	int r = commit_fat(fat);
	if (r) return r;
	memcpy(dir, dirblk(dir_cur), VCARD_BLOCK);
	uint8_t *e = dir + slot * VCARD_ENTRY;
	memcpy(e, entry, VCARD_ENTRY);
	e[6] = 0xFF;
	put16(e + 0x36, (uint16_t)first);
	put16(e + 0x38, (uint16_t)blocks);
	put16(e + 0x3A, 0xFFFF);
	r = commit_dir(dir);
	fflush(f);
	return r;
}

int vcard_delete(int n) {
	int s = slot_of(n);
	if (s < 0 || !total) return VCARD_ERR_IO;
	static uint8_t fat[VCARD_BLOCK], dir[VCARD_BLOCK];
	const uint8_t *e = dirblk(dir_cur) + s * VCARD_ENTRY;
	int blocks = be16(e + 0x38), block = be16(e + 0x36);
	/* out of the directory first, then its blocks are freed */
	memcpy(fat, fatblk(fat_cur), VCARD_BLOCK);
	int freed = 0;
	for (int i = 0; i < blocks && block >= SYSAREA && block < total; i++) {
		int next = fat_get(fat, block);
		if (next) { fat_set(fat, block, 0); freed++; }
		if (next == 0xFFFF) break;
		block = next;
	}
	memcpy(dir, dirblk(dir_cur), VCARD_BLOCK);
	memset(dir + s * VCARD_ENTRY, 0xFF, VCARD_ENTRY);
	int r = commit_dir(dir);
	if (r) return r;
	put16(fat + FAT_FREE, be16(fat + FAT_FREE) + freed);
	r = commit_fat(fat);
	fflush(f);
	return r;
}

/* -------------------------------------------------------------- formatting */

int vcard_format(const char *path, int blocks, const VCardFormat *fm) {
	int mbits = (blocks + SYSAREA) / 16;
	if (mbits < 4 || mbits > 128 || (mbits & (mbits - 1)) || mbits * 16 - SYSAREA != blocks) return VCARD_ERR_IO;
	vcard_close();
	FILE *o = fopen(path, "wb");
	if (!o) return VCARD_ERR_IO;
	static uint8_t blk[VCARD_BLOCK];
	uint16_t a, b;

	/* the header, made as the card library makes it from the console's values */
	memset(blk, 0xFF, VCARD_BLOCK);
	uint64_t rnd = fm->time;
	for (int i = 0; i < 12; i++) {
		rnd = ((rnd * 0x41c64e6dULL) + 0x3039ULL) >> 16;
		blk[i] = (uint8_t)(fm->flash_id[i] + (uint32_t)rnd);
		rnd = ((rnd * 0x41c64e6dULL) + 0x3039ULL) >> 16;
		rnd &= 0x7fff;
	}
	put32(blk + 12, (uint32_t)(fm->time >> 32));
	put32(blk + 16, (uint32_t)fm->time);
	put32(blk + 20, fm->counter_bias);
	put32(blk + 24, fm->language);
	put32(blk + 28, fm->vi_dtv);
	put16(blk + 0x20, 0);
	put16(blk + 0x22, (uint16_t)mbits);
	put16(blk + 0x24, fm->encoding);
	checksum(blk, 0x1FC, &a, &b);
	put16(blk + 0x1FC, a);
	put16(blk + 0x1FE, b);
	bool ok = fwrite(blk, 1, VCARD_BLOCK, o) == VCARD_BLOCK;

	/* two empty directories */
	for (int c = 0; c < 2 && ok; c++) {
		memset(blk, 0xFF, VCARD_BLOCK);
		put16(blk + DIR_UPDATED, (uint16_t)c);
		checksum(blk, DIR_CK, &a, &b);
		put16(blk + DIR_CK, a);
		put16(blk + DIR_CK + 2, b);
		ok = fwrite(blk, 1, VCARD_BLOCK, o) == VCARD_BLOCK;
	}
	/* two empty block tables */
	for (int c = 0; c < 2 && ok; c++) {
		memset(blk, 0, VCARD_BLOCK);
		put16(blk + FAT_UPDATED, (uint16_t)c);
		put16(blk + FAT_FREE, (uint16_t)blocks);
		put16(blk + FAT_LAST, 4);
		checksum(blk + 4, VCARD_BLOCK - 4, &a, &b);
		put16(blk, a);
		put16(blk + 2, b);
		ok = fwrite(blk, 1, VCARD_BLOCK, o) == VCARD_BLOCK;
	}
	/* the save blocks, blank */
	memset(blk, 0, VCARD_BLOCK);
	for (int i = SYSAREA; i < mbits * 16 && ok; i++) ok = fwrite(blk, 1, VCARD_BLOCK, o) == VCARD_BLOCK;
	if (fclose(o) != 0) ok = false;
	if (!ok) { remove(path); return VCARD_ERR_IO; }
	return VCARD_OK;
}
