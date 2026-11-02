/****************************************************************************
 * bitmap.c
 *
 * Keeps the last banner and icon decoded by bannerload.c (rows of blue,
 * green, red bytes), for the save lists to pick up.
 ****************************************************************************/
#include <gccore.h>
#include <string.h>
#include "bitmap.h"

u8 decoded_banner[96 * 32 * 3];
u8 decoded_icon[32 * 32 * 3];

void ShowBanner(u8 *banner)
{
	memcpy(decoded_banner, banner, sizeof(decoded_banner));
}

void ShowIcon(u8 *icon)
{
	memcpy(decoded_icon, icon, sizeof(decoded_icon));
}
