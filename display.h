/*
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
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

	cRpiDisplay(int id, int width, int height, int frameRate, bool interlaced,
			bool fixedMode);
	virtual ~cRpiDisplay();

	int Update(const cVideoFrameFormat *videoFormat);

	virtual int SetMode(int width, int height, int frameRate,
			cScanMode::eMode scanMode) {
		return 0;
	}

	static int SetHvsSyncUpdate(cScanMode::eMode scanMode);

	int m_id;
	int m_width;
	int m_height;
	int m_frameRate;
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

	cRpiHDMIDisplay(int id, int width, int height, int frameRate, bool interlaced,
			int group, int mode);
	virtual ~cRpiHDMIDisplay();

private:

	virtual int SetMode(int width, int height, int frameRate,
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

	cRpiDefaultDisplay(int id, int width, int height);

};

#endif
