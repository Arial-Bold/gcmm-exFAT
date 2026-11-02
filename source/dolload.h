/* dolload.h - starting another .dol from the SD card (the Launcher, or a new
 * version after an update). From Flashcube. */
#pragma once
#include <stdbool.h>
#include <stddef.h>

/* Checks a .dol file without running it. Fills err on failure. */
bool dol_check(const char *path, char *err, size_t errlen);
/* Loads and starts the .dol. Only returns if something went wrong before the
 * point of no return (err says what). */
bool dol_launch(const char *path, char *err, size_t errlen);
/* main.c: stops drawing, the sounds and the SD card just before the jump */
void dol_before_jump(void);
