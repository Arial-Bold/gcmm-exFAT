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

/* Virtual memory cards (card images on the SD card, in swiss/saves): slot
 * numbers from VSLOT_BASE on, which every card function here takes too */
#define VSLOT_BASE 2
#define VCARD_MAX 12
typedef struct {
	char path[300];
	char letter;          /* the slot it stands in for: 'A' or 'B' */
	char region[8];       /* USA, JPN, EUR, KOR, ALL */
	int blocks, free, saves;
	bool formatted;
} VirtualCard;
int  VirtualCardsScan(void);                 /* looks on the SD card again; returns how many */
int  VirtualCardCount(void);
const VirtualCard *VirtualCardGet(int slot); /* NULL for a real slot */
bool SlotIsVirtual(int slot);
bool CardSlotPresent(int slot);              /* a card in the slot, or a formatted virtual card */
void CardSlotName(int slot, char *out, int cap);   /* "Memory Card A", "Virtual card A (USA)" */
int  VirtualCardMake(char letter, const char *region, int blocks);   /* the new card's slot, or -1 */

/* what CardWriteFile does when the save is already on the card */
enum { REPLACE_ASK, REPLACE_YES, REPLACE_NO };
extern int card_replace;
static int OFFSET = 0;
#endif
