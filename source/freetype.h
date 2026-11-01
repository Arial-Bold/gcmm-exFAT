/****************************************************************************
* freetype.h - GCMM's screens and prompts (see freetype.c)
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
const char *DeviceName (int dev);
void UpdateTitleInfo (void);
void UpdateSDInfo (void);

void ListBegin (const char *mode_name);
int ShowSelector (int saveinfo);

#endif
