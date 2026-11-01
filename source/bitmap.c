/****************************************************************************
 * bitmap.c
 *
 * Hands a save's banner and icon (decoded by bannerload.c into rows of blue,
 * green, red bytes) to the screen.
 ****************************************************************************/
#include <gccore.h>
#include "bitmap.h"
#include "ui.h"

void ShowBanner(u8 *banner)
{
	ui_info_banner_rgb(banner);   /* 96x32 */
}

void ShowIcon(u8 *icon)
{
	ui_info_icon_rgb(icon);       /* 32x32 */
}
