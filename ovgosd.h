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

protected:

	virtual cOsd *CreateOsd(int Left, int Top, uint Level);
	virtual bool ProvidesTrueColor(void) { return true; }
	virtual int StoreImageData(const cImage &Image) { return 0; }
	virtual void DropImageData(int ImageHandle) {}

private:

	cOvg *m_ovg;
};


class cOvgOsd : public cOsd
{

public:

	cOvgOsd(int Left, int Top, uint Level, cOvg *ovg);
	virtual ~cOvgOsd();

	virtual void Flush(void);

private:

	cOvg *m_ovg;

};

#endif

