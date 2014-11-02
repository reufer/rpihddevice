/*
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#ifndef OVG_OSD_H
#define OVG_OSD_H

#include <vdr/osd.h>

class cOvg;

class cRpiOsdProvider : public cOsdProvider
{

public:

	cRpiOsdProvider();
	~cRpiOsdProvider();

	static void ResetOsd(void);

protected:

	virtual cOsd *CreateOsd(int Left, int Top, uint Level);
	virtual bool ProvidesTrueColor(void) { return true; }
	virtual int StoreImageData(const cImage &Image) { return 0; }
	virtual void DropImageData(int ImageHandle) { }

private:

	cOvg *m_ovg;
	static cRpiOsdProvider *s_instance;
};

#endif

