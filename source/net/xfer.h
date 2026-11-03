/* xfer.h - Network transfer: a small web server on the home network (from
 * Flashcube's Transfer). A phone or computer opens the page, sends files,
 * and downloads backups from MCBACKUP.
 *
 * A file that's sent is saved whole in a holding folder first; then the
 * GameCube asks where it goes (xfer_take_waiting), and says how it went
 * (xfer_finish), which the page shows. Everything network-side runs on a
 * thread of its own; these are called from the main thread. */
#pragma once
#include <stdbool.h>
#include <stdint.h>

typedef enum { XF_SENDING, XF_WAITING, XF_WORKING, XF_DONE, XF_FAILED } XferState;

typedef struct {
	int id;
	char name[64];         /* as it was sent (made safe for the card) */
	char held[300];        /* where it is now, in the holding folder */
	uint32_t got, total;   /* bytes */
	XferState state;
	char text[96];         /* what happened, for the page and the screen */
} XferItem;

void xfer_open(void);                 /* starts the network and the server (once); files are accepted from now */
void xfer_close(void);                /* files are refused until xfer_open again */
bool xfer_ready(void);                /* the server is answering */
const char *xfer_url(void);           /* "http://192.168.1.42/" once ready */
const char *xfer_message(void);       /* the network's state, or a problem */
int  xfer_items(XferItem *out, int cap);   /* the latest files, newest first */
bool xfer_take_waiting(XferItem *out);     /* a file that arrived and needs a home (it becomes XF_WORKING) */
void xfer_finish(int id, bool ok, const char *text);
void xfer_holding_dir(char *out, int cap); /* the holding folder */
