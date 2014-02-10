/*
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#ifndef DISPLAY_H
#define DISPLAY_H

#include "interface/vmcs_host/vc_dispmanx_types.h"

class cRpiDisplay
{

public:

	static int Open(int device);
	static void Close(void);

	static int GetSize(int &width, int &height);
	static int AddElement(DISPMANX_ELEMENT_HANDLE_T &element,
			int width, int height, int layer);
	static int Snapshot(uint8_t* frame, int width, int height);

private:

	static DISPMANX_DISPLAY_HANDLE_T s_display;
	static DISPMANX_UPDATE_HANDLE_T  s_update;

};

#endif
