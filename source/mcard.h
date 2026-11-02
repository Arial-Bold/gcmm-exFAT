/****************************************************************************
* mcard support prototypes
****************************************************************************/
#ifndef _MCARDSUP_
#define _MCARDSUP_

#include <gccore.h>
#include <stdbool.h>

void GCIMakeHeader();
void ExtractGCIHeader();
int MountCard(int cslot);
u16 FreeBlocks(s32 chn);
int CardGetDirectory (int slot);
void CardListFiles ();
int CardReadFileHeader (int slot, int id);
int CardReadFile (int slot, int id);
int CardWriteFile (int slot);
bool CardDeleteSave(int slot, int id);
/* a quick look at every save in CardList, the card mounted once */
bool CardReadSummaries(int slot, void (*done)(int id, bool ok, void *ctx), void *ctx);
void MC_FormatMode(s32 slot);
void WaitCardError(char *src, int error);

/* what CardWriteFile does when the save is already on the card */
enum { REPLACE_ASK, REPLACE_YES, REPLACE_NO };
extern int card_replace;
static int OFFSET = 0;
#endif
