/****************************************************************************
* freetype.h - GCMM exFAT's screens and prompts (see freetype.c)
****************************************************************************/
#ifndef _FTFONTS_
#define _FTFONTS_

#define MCDATAOFFSET 64
#define DELETE_MODE 200
#define BACKUP_MODE 300
#define RESTORE_MODE 400

void ShowScreen ();
void ShowAction (char *msg);
void WaitRelease ();
void WaitPrompt (char *msg);
int WaitPromptChoice (char *msg, char *bmsg, char *amsg);
int WaitPromptChoiceAZ (char *msg, char *bmsg, char *amsg);
void writeStatusBar (char *line1, char *line2);

int SelectMode ();
int ChooseDevice (const u8 *devices, int count);
int ChooseCard (const char *title, int exclude, bool always);   /* a memory card, real or virtual: its slot, or -1 */
void NewVirtualCard (void);
const char *DeviceName (int dev);
void UpdateTitleInfo (void);
void UpdateSDInfo (void);

void UpdateMainCards (void);
void MainSummaryRefresh (void);   /* the main screen's line: the saves, and how many aren't backed up */

/* the save lists (save_rows in saves.c) */
typedef struct {
	const char *title;   /* along the top of the list */
	const char *right;   /* at its right */
	const char *verb;    /* "Back up", "Restore", "Delete": tick boxes and the X button; NULL: A chooses a row */
	const char *hint;    /* the bar's first line while nothing is ticked */
	const char *where;   /* its second line: where they go */
} PickSpec;
enum { PICK_BACK, PICK_GO, PICK_OPEN };
int PickList (const PickSpec *ps, int *sel);

#endif
