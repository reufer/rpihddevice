/*
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#include "display.h"
#include "tools.h"

#include <vdr/tools.h>

#include "interface/vmcs_host/vc_dispmanx.h"

#define ALIGN_UP(value, align) (((value) & (align-1)) ? \
		(((value) + (align-1)) & ~(align-1)) : (value))

DISPMANX_DISPLAY_HANDLE_T cRpiDisplay::s_display = 0;
DISPMANX_UPDATE_HANDLE_T cRpiDisplay::s_update = 0;

int cRpiDisplay::Open(int device)
{
	s_display = vc_dispmanx_display_open(device);
	if (!s_display)
	{
		ELOG("failed to open display!");
		return -1;
	}

	s_update = vc_dispmanx_update_start(0);
	if (s_update == DISPMANX_NO_HANDLE)
	{
		ELOG("failed to start display update!");
		Close();
		return -1;
	}
	return 0;
}

void cRpiDisplay::Close(void)
{
	if (s_display)
		vc_dispmanx_display_close(s_display);

	s_display = 0;
}

int cRpiDisplay::GetSize(int &width, int &height)
{
	if (!s_display)
		return -1;

	DISPMANX_MODEINFO_T info;
	memset(&info, 0, sizeof(info));

	if (vc_dispmanx_display_get_info(s_display, &info))
	{
 		ELOG("failed to get display info!");
 		return -1;
	}

	width = info.width;
	height = info.height;

	return 0;
}

int cRpiDisplay::AddElement(DISPMANX_ELEMENT_HANDLE_T &element,
		int width, int height, int layer)
{
	if (!s_display)
		return -1;

	VC_RECT_T dstRect = { 0, 0, width, height };
 	VC_RECT_T srcRect = { 0, 0, width << 16, height << 16 };

 	element = vc_dispmanx_element_add(s_update, s_display, layer, &dstRect, 0,
 			&srcRect, DISPMANX_PROTECTION_NONE, 0, 0, (DISPMANX_TRANSFORM_T)0);

 	if (!element)
 	{
 		ELOG("failed to add display element!");
 		return -1;
 	}

 	vc_dispmanx_update_submit_sync(s_update);
 	return 0;
}

int cRpiDisplay::Snapshot(uint8_t* frame, int width, int height)
{
	if (!s_display)
		return -1;

	uint32_t image;
	DISPMANX_RESOURCE_HANDLE_T res = vc_dispmanx_resource_create(
			VC_IMAGE_RGB888, width, height, &image);

	vc_dispmanx_snapshot(s_display, res,
			(DISPMANX_TRANSFORM_T)(DISPMANX_SNAPSHOT_PACK));

	VC_RECT_T rect = { 0, 0, width, height };
	vc_dispmanx_resource_read_data(res, &rect, frame, width * 3);

	vc_dispmanx_resource_delete(res);
	return 0;
}
