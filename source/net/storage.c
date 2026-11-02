/* storage.c - see storage.h */
#include "storage.h"
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

extern char fatpath[4];
extern bool have_sd;

static char program_rel[256];   /* "apps/gcmm/default.dol", without the device */
static char dir_buf[300], file_buf[300];

void storage_set_program(const char *argv0) {
	program_rel[0] = 0;
	if (!argv0) return;
	const char *p = strstr(argv0, ":/");
	p = p ? p + 2 : (argv0[0] == '/' ? argv0 + 1 : NULL);
	if (p && *p) snprintf(program_rel, sizeof(program_rel), "%s", p);
}

bool storage_ok(void) { return have_sd && fatpath[0]; }

bool storage_is_file(const char *path) { struct stat st; return stat(path, &st) == 0 && S_ISREG(st.st_mode); }
bool storage_is_dir(const char *path) {
	DIR *d = opendir(path);
	if (!d) return false;
	closedir(d);
	return true;
}

const char *storage_program_file(void) {
	if (!storage_ok() || !program_rel[0]) return NULL;
	snprintf(file_buf, sizeof(file_buf), "%s:/%s", fatpath, program_rel);
	return storage_is_file(file_buf) ? file_buf : NULL;
}

const char *storage_app_dir(void) {
	const char *prog = storage_program_file();
	if (prog) {
		snprintf(dir_buf, sizeof(dir_buf), "%s", prog);
		char *slash = strrchr(dir_buf, '/');
		if (slash && slash > strchr(dir_buf, ':') + 1) { *slash = 0; return dir_buf; }
	}
	snprintf(dir_buf, sizeof(dir_buf), "%s:/apps/gcmm", fatpath);
	return dir_buf;
}

void storage_path(char *out, size_t cap, const char *name) { snprintf(out, cap, "%s/%s", storage_app_dir(), name); }

char *storage_read(const char *path, size_t *len) {
	FILE *f = fopen(path, "rb");
	if (!f) return NULL;
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	char *buf = n >= 0 ? malloc((size_t)n + 1) : NULL;
	if (buf && fread(buf, 1, (size_t)n, f) != (size_t)n) { free(buf); buf = NULL; }
	fclose(f);
	if (!buf) return NULL;
	buf[n] = 0;
	if (len) *len = (size_t)n;
	return buf;
}

bool storage_write_atomic(const char *path, const void *data, size_t len) {
	char tmp[320];
	snprintf(tmp, sizeof(tmp), "%s.tmp", path);
	FILE *f = fopen(tmp, "wb");
	if (!f) return false;
	bool ok = fwrite(data, 1, len, f) == len;
	if (fclose(f) != 0) ok = false;
	if (ok) { remove(path); ok = rename(tmp, path) == 0; }
	if (!ok) remove(tmp);
	return ok;
}

char *storage_read_other_cacert(size_t *len) {
	static const char *const where[] = { "apps/gcbrowser", "gcbrowser", "apps/cubeverse", "apps/flashcube", "flashcube" };
	for (unsigned i = 0; i < sizeof(where) / sizeof(where[0]); i++) {
		char p[320];
		snprintf(p, sizeof(p), "%s:/%s/cacert.pem", fatpath, where[i]);
		char *pem = storage_read(p, len);
		if (pem) return pem;
	}
	return NULL;
}
