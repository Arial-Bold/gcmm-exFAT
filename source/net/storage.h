/* storage.h - where GCMM exFAT keeps its own files (for the update code):
 * the folder its .dol was started from, or apps/gcmm, on the SD card in use. */
#pragma once
#include <stdbool.h>
#include <stddef.h>

void  storage_set_program(const char *argv0);   /* the .dol we were started from, if the loader said */
bool  storage_ok(void);                          /* there's an SD card */
const char *storage_app_dir(void);               /* e.g. "sdc:/apps/gcmm" */
const char *storage_program_file(void);          /* the running .dol, or NULL if not known */
void  storage_path(char *out, size_t cap, const char *name);   /* a file in the app's folder */
char *storage_read(const char *path, size_t *len);             /* malloc'd, NUL-terminated; NULL if missing */
bool  storage_write_atomic(const char *path, const void *data, size_t len);
bool  storage_is_file(const char *path);
bool  storage_is_dir(const char *path);
char *storage_read_other_cacert(size_t *len);    /* another app's cacert.pem on the card */
