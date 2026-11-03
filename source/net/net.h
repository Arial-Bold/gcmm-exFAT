/* net.h - starts the network (the Broadband Adapter, or a WIZnet adapter such
 * as the ETH2GC, through libogc2) on a thread of its own, asking the router
 * for an address. Used by Update from GitHub and Network transfer. */
#pragma once
#include <stdbool.h>

typedef enum { NET_OFF, NET_STARTING, NET_UP, NET_FAILED } NetState;

void net_start(void);            /* once; again after a failure */
NetState net_state(void);
const char *net_gateway(void);   /* the router, when up */
const char *net_ip(void);        /* the GameCube's address, when up */
const char *net_message(void);   /* a problem, for the screen */
