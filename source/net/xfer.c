/* xfer.c - see xfer.h. The server side is adapted from Flashcube's net.c. */
#include "xfer.h"
#include "net.h"
#include "storage.h"
#include "thr.h"
#include "webpage.h"
#include <ctype.h>
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#ifdef GEKKO
#include <network.h>
#define SK_SOCKET(a, b, c) net_socket(a, b, c)
#define SK_BIND net_bind
#define SK_LISTEN net_listen
#define SK_ACCEPT net_accept
#define SK_RECV(s, b, n) net_recv(s, b, n, 0)
#define SK_SEND(s, b, n) net_send(s, b, n, 0)
#define SK_CLOSE net_close
#define SK_SELECT net_select
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <unistd.h>
#define SK_SOCKET(a, b, c) socket(a, b, c)
#define SK_BIND bind
#define SK_LISTEN listen
#define SK_ACCEPT accept
#define SK_RECV(s, b, n) recv(s, b, n, 0)
#define SK_SEND(s, b, n) send(s, b, n, MSG_NOSIGNAL)
#define SK_CLOSE close
#define SK_SELECT select
#endif

extern char fatpath[4];

#define MAX_ITEMS 8
#define MAX_UPLOAD (512L * 1024 * 1024)

static XferItem items[MAX_ITEMS];   /* newest first */
static int nitems, next_id = 1;
static thr_mutex lock;
static thr_t thread;
static volatile bool started, ready, accepting;
static char url[48], msg[96] = "Starting the network\x85";

/* ------------------------------------------------------------- items */

static XferItem *find(int id) {
	for (int i = 0; i < nitems; i++) if (items[i].id == id) return &items[i];
	return NULL;
}

static int add_item(const char *name, uint32_t total) {
	thr_lock(&lock);
	if (nitems == MAX_ITEMS) nitems--;   /* the oldest goes */
	memmove(&items[1], &items[0], sizeof(items[0]) * nitems);
	nitems++;
	XferItem *it = &items[0];
	memset(it, 0, sizeof(*it));
	it->id = next_id++;
	snprintf(it->name, sizeof(it->name), "%s", name);
	it->total = total;
	it->state = XF_SENDING;
	int id = it->id;
	thr_unlock(&lock);
	return id;
}

static void set_item(int id, XferState st, uint32_t got, const char *held, const char *text) {
	thr_lock(&lock);
	XferItem *it = find(id);
	if (it) {
		it->state = st;
		it->got = got;
		if (held) snprintf(it->held, sizeof(it->held), "%s", held);
		if (text) snprintf(it->text, sizeof(it->text), "%s", text);
	}
	thr_unlock(&lock);
}

int xfer_items(XferItem *out, int cap) {
	if (!started) return 0;
	thr_lock(&lock);
	int n = nitems < cap ? nitems : cap;
	memcpy(out, items, sizeof(items[0]) * n);
	thr_unlock(&lock);
	return n;
}

bool xfer_take_waiting(XferItem *out) {
	if (!started) return false;
	bool got = false;
	thr_lock(&lock);
	for (int i = nitems - 1; i >= 0 && !got; i--)   /* the oldest first */
		if (items[i].state == XF_WAITING) {
			items[i].state = XF_WORKING;
			snprintf(items[i].text, sizeof(items[i].text), "Choose where it goes on the GameCube");
			*out = items[i];
			got = true;
		}
	thr_unlock(&lock);
	return got;
}

void xfer_finish(int id, bool ok, const char *text) {
	thr_lock(&lock);
	XferItem *it = find(id);
	if (it) {
		it->state = ok ? XF_DONE : XF_FAILED;
		snprintf(it->text, sizeof(it->text), "%s", text);
		it->held[0] = 0;
	}
	thr_unlock(&lock);
}

void xfer_holding_dir(char *out, int cap) { snprintf(out, cap, "%s:/gcmm-incoming", fatpath); }

/* ------------------------------------------------------------ sockets */

static int wait_fd(int fd, bool wr, int ms) {
	fd_set set;
	FD_ZERO(&set);
	FD_SET(fd, &set);
	struct timeval tv;
	tv.tv_sec = ms / 1000;
	tv.tv_usec = (ms % 1000) * 1000;
	int r = SK_SELECT(fd + 1, wr ? NULL : &set, wr ? &set : NULL, NULL, &tv);
	return r < 0 ? -1 : r > 0;
}

static bool send_all(int fd, const void *data, size_t len) {
	const char *p = data;
	while (len > 0) {
		if (wait_fd(fd, true, 20000) != 1) return false;
		int chunk = len > 8192 ? 8192 : (int)len;
		int n = SK_SEND(fd, p, chunk);
		if (n <= 0) return false;
		p += n;
		len -= (size_t)n;
	}
	return true;
}

static int recv_some(int fd, void *buf, int cap, int ms) {
	if (wait_fd(fd, false, ms) != 1) return -1;
	return SK_RECV(fd, buf, cap);
}

static void send_head(int fd, int code, const char *status, const char *type, long len, const char *extra) {
	char h[640];
	int n = snprintf(h, sizeof(h), "HTTP/1.0 %d %s\r\nContent-Type: %s\r\n%sConnection: close\r\nCache-Control: no-cache\r\n",
	                 code, status, type, extra ? extra : "");
	if (len >= 0) n += snprintf(h + n, sizeof(h) - n, "Content-Length: %ld\r\n", len);
	n += snprintf(h + n, sizeof(h) - n, "\r\n");
	send_all(fd, h, (size_t)n);
}

static void send_text(int fd, int code, const char *status, const char *type, const char *body, size_t len) {
	send_head(fd, code, status, type, (long)len, NULL);
	send_all(fd, body, len);
}

static void send_json(int fd, int code, const char *status, const char *body) {
	send_text(fd, code, status, "application/json", body, strlen(body));
}

/* ------------------------------------------------------------- helpers */

static void url_decode(char *s) {
	char *o = s;
	for (; *s; s++) {
		if (*s == '%' && isxdigit((unsigned char)s[1]) && isxdigit((unsigned char)s[2])) {
			char hex[3] = { s[1], s[2], 0 };
			*o++ = (char)strtol(hex, NULL, 16);
			s += 2;
		} else if (*s == '+') *o++ = ' ';
		else *o++ = *s;
	}
	*o = 0;
}

/* the value of key in a query string ("name=a.zip&x=1"), decoded */
static void query_value(const char *query, const char *key, char *out, size_t cap) {
	out[0] = 0;
	size_t kl = strlen(key);
	for (const char *p = query; p && *p; p = strchr(p, '&') ? strchr(p, '&') + 1 : NULL) {
		if (!strncmp(p, key, kl) && p[kl] == '=') {
			snprintf(out, cap, "%s", p + kl + 1);
			char *amp = strchr(out, '&');
			if (amp) *amp = 0;
			url_decode(out);
			return;
		}
	}
}

/* A file name that's safe on the card: no folders, no odd characters */
static void clean_name(const char *in, char *out, size_t cap) {
	const char *slash = strrchr(in, '/'), *bslash = strrchr(in, '\\');
	if (slash) in = slash + 1;
	if (bslash && bslash + 1 > in) in = bslash + 1;
	size_t k = 0;
	for (const char *p = in; *p && k + 1 < cap; p++) {
		unsigned char c = (unsigned char)*p;
		out[k++] = (isalnum(c) || strchr(" -_.()[]+!,'", c)) && c < 0x80 ? (char)c : '_';
	}
	out[k] = 0;
	while (k > 0 && (out[k - 1] == '.' || out[k - 1] == ' ')) out[--k] = 0;
	if (!k || out[0] == '.') snprintf(out, cap, "file");
}

static void json_escape(const char *in, char *out, size_t cap) {
	size_t k = 0;
	for (; *in && k + 7 < cap; in++) {
		unsigned char c = (unsigned char)*in;
		if (c == '"' || c == '\\') { out[k++] = '\\'; out[k++] = (char)c; }
		else if (c < 0x20 || c >= 0x7f) k += snprintf(out + k, cap - k, "\\u%04x", c);
		else out[k++] = (char)c;
	}
	out[k] = 0;
}

/* ------------------------------------------------------------- routes */

/* POST /upload?name=...: the file, whole, into the holding folder */
static void post_upload(int fd, const char *query, long length, const char *pre, int pre_len) {
	char raw[200], name[64];
	query_value(query, "name", raw, sizeof(raw));
	clean_name(raw, name, sizeof(name));
	if (!accepting) {
		send_json(fd, 503, "Closed", "{\"text\":\"Open Network transfer in Card tools on the GameCube first.\"}");
		return;
	}
	if (length <= 0 || length > MAX_UPLOAD) {
		send_json(fd, 400, "Bad Request", "{\"text\":\"That file is empty or too big (512 MB at most).\"}");
		return;
	}
	char dir[64], path[340], part[350];
	xfer_holding_dir(dir, sizeof(dir));
	mkdir(dir, 0777);
	int id = add_item(name, (uint32_t)length);
	snprintf(path, sizeof(path), "%s/%d-%s", dir, id, name);
	snprintf(part, sizeof(part), "%s.part", path);
	FILE *f = fopen(part, "wb");
	if (!f) {
		set_item(id, XF_FAILED, 0, NULL, "Couldn't write to the SD card.");
		send_json(fd, 500, "Error", "{\"text\":\"Couldn't write to the SD card.\"}");
		return;
	}
	setvbuf(f, NULL, _IOFBF, 32 * 1024);
	long got = 0;
	bool ok = true;
	if (pre_len > 0) {
		if (pre_len > length) pre_len = (int)length;
		ok = fwrite(pre, 1, (size_t)pre_len, f) == (size_t)pre_len;
		got = pre_len;
	}
	char *buf = malloc(16384);
	ok = ok && buf;
	while (ok && got < length) {
		long want = length - got > 16384 ? 16384 : length - got;
		int n = recv_some(fd, buf, (int)want, 30000);
		if (n <= 0) { ok = false; break; }
		ok = fwrite(buf, 1, (size_t)n, f) == (size_t)n;
		got += n;
		set_item(id, XF_SENDING, (uint32_t)got, NULL, NULL);
	}
	free(buf);
	ok = (fclose(f) == 0) && ok && got == length;
	if (ok) ok = rename(part, path) == 0;
	if (!ok) {
		remove(part);
		set_item(id, XF_FAILED, (uint32_t)got, NULL, "It didn't arrive whole (or the SD card is full).");
		send_json(fd, 500, "Error", "{\"text\":\"It didn't arrive whole (or the SD card is full).\"}");
		return;
	}
	set_item(id, XF_WAITING, (uint32_t)got, path, "Choose where it goes on the GameCube");
	char body[64];
	snprintf(body, sizeof(body), "{\"id\":%d}", id);
	send_json(fd, 200, "OK", body);
}

/* GET /status?id=N: how a file is getting on */
static void get_status(int fd, const char *query) {
	char v[16], text[200], body[320];
	query_value(query, "id", v, sizeof(v));
	static const char *const names[] = { "sending", "waiting", "waiting", "done", "failed" };
	thr_lock(&lock);
	XferItem *it = find(atoi(v));
	const char *st = it ? names[it->state] : "failed";
	json_escape(it ? it->text : "The GameCube has forgotten this file.", text, sizeof(text));
	thr_unlock(&lock);
	snprintf(body, sizeof(body), "{\"state\":\"%s\",\"text\":\"%s\"}", st, text);
	send_json(fd, 200, "OK", body);
}

/* GET /list: the backups in MCBACKUP (and the folders in it, one level down) */
static void list_dir(const char *rel, char **out, size_t *len, size_t *cap, int depth) {
	char dir[300];
	snprintf(dir, sizeof(dir), "%s:/MCBACKUP%s%s", fatpath, rel[0] ? "/" : "", rel);
	DIR *d = opendir(dir);
	if (!d) return;
	struct dirent *e;
	while ((e = readdir(d))) {
		if (e->d_name[0] == '.') continue;
		char full[600], relp[300], esc[400];
		snprintf(full, sizeof(full), "%s/%s", dir, e->d_name);
		snprintf(relp, sizeof(relp), "%s%s%s", rel, rel[0] ? "/" : "", e->d_name);
		struct stat st;
		if (stat(full, &st) != 0) continue;
		if (S_ISDIR(st.st_mode)) {
			if (depth < 1) list_dir(relp, out, len, cap, depth + 1);
			continue;
		}
		json_escape(relp, esc, sizeof(esc));
		if (*len + strlen(esc) + 64 > *cap) {
			size_t nc = *cap * 2;
			char *nb = realloc(*out, nc);
			if (!nb) break;
			*out = nb;
			*cap = nc;
		}
		*len += snprintf(*out + *len, *cap - *len, "%s{\"path\":\"%s\",\"size\":%ld}", *len > 1 ? "," : "", esc, (long)st.st_size);
	}
	closedir(d);
}
static void get_list(int fd) {
	size_t cap = 4096, len = 0;
	char *out = malloc(cap);
	if (!out) { send_json(fd, 500, "Error", "[]"); return; }
	out[len++] = '[';
	list_dir("", &out, &len, &cap, 0);
	out[len++] = ']';
	send_text(fd, 200, "OK", "application/json", out, len);
	free(out);
}

/* GET /file?path=...: a backup, to save on the phone or computer */
static void get_file(int fd, const char *query) {
	char rel[300], path[340];
	query_value(query, "path", rel, sizeof(rel));
	if (!rel[0] || strstr(rel, "..") || rel[0] == '/' || strchr(rel, ':')) { send_text(fd, 400, "Bad Request", "text/plain", "", 0); return; }
	snprintf(path, sizeof(path), "%s:/MCBACKUP/%s", fatpath, rel);
	FILE *f = fopen(path, "rb");
	if (!f) { send_text(fd, 404, "Not Found", "text/plain", "Not found", 9); return; }
	fseek(f, 0, SEEK_END);
	long n = ftell(f);
	fseek(f, 0, SEEK_SET);
	char name[80], extra[160];
	const char *slash = strrchr(rel, '/');
	clean_name(slash ? slash + 1 : rel, name, sizeof(name));
	snprintf(extra, sizeof(extra), "Content-Disposition: attachment; filename=\"%s\"\r\n", name);
	send_head(fd, 200, "OK", "application/octet-stream", n, extra);
	char *buf = malloc(16384);
	long sent = 0;
	while (buf && sent < n) {
		size_t r = fread(buf, 1, 16384, f);
		if (r == 0 || !send_all(fd, buf, r)) break;
		sent += (long)r;
	}
	free(buf);
	fclose(f);
}

static void handle(int fd) {
	enum { HDR = 8192 };
	char *req = malloc(HDR + 1);
	if (!req) return;
	int len = 0;
	char *end = NULL;
	while (len < HDR) {
		int n = recv_some(fd, req + len, HDR - len, 15000);
		if (n <= 0) break;
		len += n;
		req[len] = 0;
		if ((end = strstr(req, "\r\n\r\n")) != NULL) break;
	}
	if (!end) { free(req); return; }
	*end = 0;
	char *body = end + 4;
	int body_have = len - (int)(body - req);
	char method[8] = "", target[600] = "";
	sscanf(req, "%7s %599s", method, target);
	long clen = -1;
	for (char *h = strstr(req, "\r\n"); h; h = strstr(h + 2, "\r\n"))
		if (!strncasecmp(h + 2, "Content-Length:", 15)) clen = atol(h + 17);
	char *query = strchr(target, '?');
	if (query) *query++ = 0;
	else query = (char *)"";

	if (!strcmp(method, "GET") && (!strcmp(target, "/") || !strcmp(target, "/index.html")))
		send_text(fd, 200, "OK", "text/html; charset=utf-8", WEBPAGE, sizeof(WEBPAGE) - 1);
	else if (!strcmp(method, "POST") && !strcmp(target, "/upload")) post_upload(fd, query, clen, body, body_have);
	else if (!strcmp(method, "GET") && !strcmp(target, "/status")) get_status(fd, query);
	else if (!strcmp(method, "GET") && !strcmp(target, "/list")) get_list(fd);
	else if (!strcmp(method, "GET") && !strcmp(target, "/file")) get_file(fd, query);
	else send_text(fd, 404, "Not Found", "text/plain", "Not found", 9);
	free(req);
}

/* ------------------------------------------------------------ the server */

static int open_server(int port) {
	int s = SK_SOCKET(AF_INET, SOCK_STREAM, IPPROTO_IP);
	if (s < 0) return -1;
	struct sockaddr_in sa;
	memset(&sa, 0, sizeof(sa));
#ifdef GEKKO
	sa.sin_len = sizeof(sa);
#else
	int one = 1;
	setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(one));
#endif
	sa.sin_family = AF_INET;
	sa.sin_port = htons((uint16_t)port);
	sa.sin_addr.s_addr = INADDR_ANY;
	if (SK_BIND(s, (struct sockaddr *)&sa, sizeof(sa)) < 0 || SK_LISTEN(s, 8) < 0) {
		SK_CLOSE(s);
		return -1;
	}
	return s;
}

static void *server_main(void *arg) {
	(void)arg;
	net_start();
	while (net_state() == NET_STARTING) thr_sleep_ms(100);
	if (net_state() != NET_UP) {
		snprintf(msg, sizeof(msg), "%s", net_message()[0] ? net_message() : "No network. Is the network adapter connected?");
		return NULL;
	}
	int port = 80, s = open_server(port);
	if (s < 0) { port = 8080; s = open_server(port); }
	if (s < 0) { snprintf(msg, sizeof(msg), "Couldn't start the transfer server."); return NULL; }
	if (port == 80) snprintf(url, sizeof(url), "http://%s/", net_ip());
	else snprintf(url, sizeof(url), "http://%s:%d/", net_ip(), port);
	snprintf(msg, sizeof(msg), "Ready on your home network");
	ready = true;
	for (;;) {
		struct sockaddr_in from;
		socklen_t fl = sizeof(from);
		int c = SK_ACCEPT(s, (struct sockaddr *)&from, &fl);
		if (c < 0) { thr_sleep_ms(50); continue; }
		handle(c);
		SK_CLOSE(c);
	}
	return NULL;
}

void xfer_open(void) {
	accepting = true;
	if (started) return;
	started = true;
	thr_mutex_init(&lock);
	if (!thr_create(&thread, server_main, NULL, 64 * 1024, 50)) snprintf(msg, sizeof(msg), "Couldn't start the transfer server.");
}
void xfer_close(void) { accepting = false; }
bool xfer_ready(void) { return ready; }
const char *xfer_url(void) { return url; }
const char *xfer_message(void) { return msg; }
