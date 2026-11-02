/* net.c - see net.h */
#include "net.h"
#include "thr.h"
#include <stdio.h>
#include <string.h>
#ifdef GEKKO
#include <network.h>
#endif

static volatile NetState state = NET_OFF;
static char ip[16], mask[16], gw[16], msg[96] = "";
static thr_t thread;

NetState net_state(void) { return state; }
const char *net_gateway(void) { return gw; }
const char *net_message(void) { return msg; }

static void *net_main(void *arg) {
	(void)arg;
#ifdef GEKKO
	ip[0] = mask[0] = gw[0] = 0;
	s32 r = if_config(ip, mask, gw, true);
	if (r < 0 || !ip[0]) {
		snprintf(msg, sizeof(msg), r < 0 ? "No network (error %d). Is the network adapter connected?" : "Your router didn't give the GameCube an address.", (int)r);
		state = NET_FAILED;
		return NULL;
	}
#endif
	state = NET_UP;
	return NULL;
}

void net_start(void) {
	/* once only: libogc can't set the adapter up a second time, so after a
	 * failure GCMM exFAT has to be started again */
	if (state != NET_OFF) return;
	state = NET_STARTING;
	msg[0] = 0;
	if (!thr_create(&thread, net_main, NULL, 64 * 1024, 50)) {
		state = NET_FAILED;
		snprintf(msg, sizeof(msg), "Couldn't start the network.");
	}
}
