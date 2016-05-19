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

#ifndef DISPLAY_H
#define DISPLAY_H

#include "tools.h"

class cRpiDisplay
{

public:

	static cRpiDisplay* GetInstance(void);
	static void DropInstance(void);

	static int GetSize(int &width, int &height);
	static int GetSize(int &width, int &height, double &aspect);

	static bool IsProgressive(void);
	static bool IsFixedMode(void);

	static int GetId(void);

	static int Snapshot(unsigned char* frame, int width, int height);

	static int SetVideoFormat(const cVideoFrameFormat *frameFormat);

protected:

	cRpiDisplay(int id, int width, int height, int frameRate, int aspectRatio,
			bool interlaced, bool fixedMode);
	virtual ~cRpiDisplay();

	int Update(const cVideoFrameFormat *videoFormat);

	virtual int SetMode(int width, int height, int frameRate, int aspectRatio,
			cScanMode::eMode scanMode) {
		return 0;
	}

	static int SetHvsSyncUpdate(cScanMode::eMode scanMode);

	static void GetModeFormat(const cVideoFrameFormat *format,
			int &modeX, int &modeY, int &aspectRatio);

	static const char* AspectRatioStr(int aspectRatio);

	int m_id;
	int m_width;
	int m_height;
	int m_frameRate;
	int m_aspectRatio;
	bool m_interlaced;
	bool m_fixedMode;

	static cRpiDisplay *s_instance;

private:

	cRpiDisplay(const cRpiDisplay&);
	cRpiDisplay& operator= (const cRpiDisplay&);

};

class cRpiHDMIDisplay : public cRpiDisplay
{

public:

	cRpiHDMIDisplay(int id, int width, int height, int frameRate,
			int aspectRatio, bool interlaced, int group, int mode);
	virtual ~cRpiHDMIDisplay();

private:

	virtual int SetMode(int width, int height, int frameRate, int aspectRatio,
			cScanMode::eMode scanMode);
	int SetMode(int group, int mode);

	static void TvServiceCallback(void *data, unsigned int reason,
			unsigned int param1, unsigned int param2);

	class ModeList;
	ModeList *m_modes;

	int m_group;
	int m_mode;

	int m_startGroup;
	int m_startMode;
	bool m_modified;
};

class cRpiDefaultDisplay : public cRpiDisplay
{

public:

	cRpiDefaultDisplay(int id, int width, int height, int aspectRatio);

};

#endif
