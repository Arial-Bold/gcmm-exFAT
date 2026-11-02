#ifndef _BITMAP_H_
#define _BITMAP_H_

#include <gctypes.h>

/* bannerload.c hands each decoded picture here */
void ShowBanner (u8 *banner);
void ShowIcon (u8 *icon);

extern u8 decoded_banner[96 * 32 * 3];   /* the last banner (blue, green, red) */
extern u8 decoded_icon[32 * 32 * 3];     /* the last icon */

#endif
