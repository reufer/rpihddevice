/*
 * rpihddevice - VDR HD output device for Raspberry Pi
 * Copyright (C) 2014, 2015, 2016 Thomas Reufer
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

#ifndef OVG_OSD_H
#define OVG_OSD_H

#include <vdr/osd.h>

class cOvgThread;

class cRpiOsdProvider : public cOsdProvider
{

public:

	cRpiOsdProvider(int layer);
	~cRpiOsdProvider();

	static void ResetOsd(bool cleanup = false);
	static const cImage *GetImageData(int ImageHandle);

protected:

	virtual cOsd *CreateOsd(int Left, int Top, uint Level);
	virtual bool ProvidesTrueColor(void) { return true; }
	virtual int StoreImageData(const cImage &Image);
	virtual void DropImageData(int ImageHandle);

private:

	cOvgThread *m_ovg;
	static cRpiOsdProvider *s_instance;
};

#endif

