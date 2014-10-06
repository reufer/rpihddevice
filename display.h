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

	static cRpiVideoPort::ePort GetVideoPort(void);
	static bool IsProgressive(void);

	static int Snapshot(unsigned char* frame, int width, int height);

	static int SetVideoFormat(int width, int height, int frameRate,
			bool interlaced);

protected:

	cRpiDisplay(int width, int height, int frameRate,
			cRpiVideoPort::ePort port);
	virtual ~cRpiDisplay();

	int Update(int width, int height, int frameRate, bool interlaced);

	virtual int SetMode(int width, int height, int frameRate, bool interlaced) {
		return 0;
	}

	int m_width;
	int m_height;
	int m_frameRate;
	bool m_interlaced;
	cRpiVideoPort::ePort m_port;

	static cRpiDisplay *s_instance;

private:

	cRpiDisplay(const cRpiDisplay&);
	cRpiDisplay& operator= (const cRpiDisplay&);

};

class cRpiHDMIDisplay : public cRpiDisplay
{

public:

	cRpiHDMIDisplay(int width, int height, int frameRate, int group, int mode);
	virtual ~cRpiHDMIDisplay();

private:

	virtual int SetMode(int width, int height, int frameRate, bool interlaced);
	int SetMode(int group, int mode);

	class ModeList;
	ModeList *m_modes;

	int m_group;
	int m_mode;

	int m_startGroup;
	int m_startMode;
	bool m_modified;
};

class cRpiCompositeDisplay : public cRpiDisplay
{

public:

	cRpiCompositeDisplay(int width, int height, int frameRate);

private:

	virtual int SetMode(int width, int height, int frameRate, bool interlaced) {
		return 0;
	}
};

#endif
