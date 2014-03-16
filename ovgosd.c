/*
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#include <VG/openvg.h>
#include <VG/vgu.h>
#include <EGL/egl.h>
#include <GLES/gl.h>

#include "ovgosd.h"
#include "display.h"
#include "omxdevice.h"
#include "setup.h"
#include "tools.h"

class cOvgOsd : public cOsd
{

public:

	cOvgOsd(int Left, int Top, uint Level, cOvg *ovg);
	virtual ~cOvgOsd();

	virtual void Flush(void);
	virtual eOsdError SetAreas(const tArea *Areas, int NumAreas);

protected:

	virtual void SetActive(bool On);

private:

	cOvg *m_ovg;

};

/* ------------------------------------------------------------------------- */

class cOvg : public cThread
{
public:

	cOvg() :
		cThread(),
		m_do(0),
		m_done(0),
		m_width(0),
		m_height(0),
		m_aspect(0),
		m_pixmap(0),
		m_d(0),
		m_x(0),
		m_y(0),
		m_w(0),
		m_h(0),
		m_clear(false)
	{
		cRpiSetup::GetDisplaySize(m_width, m_height, m_aspect);

		m_do = new cCondWait();
		m_done = new cCondWait();

		Start();
	}

	~cOvg()
	{
		Cancel(-1);
		Clear();
		while (Active())
			cCondWait::SleepMs(50);

		delete m_do;
		delete m_done;
	}

	void GetDisplaySize(int &width, int &height, double &aspect)
	{
		width = m_width;
		height = m_height;
		aspect = m_aspect;
	}

	void DrawPixmap(int x, int y, int w, int h, int d, const uint8_t *data)
	{
		Lock();
		m_pixmap = data;
		m_d = d;
		m_x = x;
		m_y = y;
		m_w = w;
		m_h = h;
		m_do->Signal();
		m_done->Wait();
		Unlock();
	}

	void Clear()
	{
		Lock();
		m_clear = true;
		m_do->Signal();
		m_done->Wait();
		Unlock();
	}

protected:

	virtual void Action(void)
	{
		DLOG("cOvg() thread started");

		EGLDisplay display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
		if (display == EGL_NO_DISPLAY)
			ELOG("failed to get EGL display connection!");

		if (eglInitialize(display, NULL, NULL) == EGL_FALSE)
			ELOG("failed to init EGL display connection!");

		eglBindAPI(EGL_OPENVG_API);

		const EGLint attr[] = {
			EGL_RED_SIZE, 8,
			EGL_GREEN_SIZE, 8,
			EGL_BLUE_SIZE, 8,
			EGL_ALPHA_SIZE, 8,
			EGL_SURFACE_TYPE, EGL_WINDOW_BIT | EGL_PBUFFER_BIT,
			EGL_CONFORMANT, EGL_OPENVG_BIT,
			EGL_NONE
		};

		EGLConfig config;
		EGLint nConfig;

		// get an appropriate EGL frame buffer configuration
		if (eglChooseConfig(display, attr, &config, 1, &nConfig) == EGL_FALSE)
			ELOG("failed to get EGL frame buffer config!");

		// create an EGL rendering context
		EGLContext context = eglCreateContext(display, config, NULL, NULL);
		if (context == EGL_NO_CONTEXT)
			ELOG("failed to create EGL rendering context!");

		cRpiDisplay::Open(0 /* LCD */);
		DISPMANX_ELEMENT_HANDLE_T dispmanElement;
		cRpiDisplay::AddElement(dispmanElement, m_width, m_height, 2);

		EGL_DISPMANX_WINDOW_T nativewindow;
		nativewindow.element = dispmanElement;
		nativewindow.width = m_width;
		nativewindow.height = m_height;

		const EGLint windowAttr[] = {
			EGL_RENDER_BUFFER, EGL_SINGLE_BUFFER,
			EGL_NONE
		};

		EGLSurface surface = eglCreateWindowSurface(display, config,
				&nativewindow, windowAttr);
		if (surface == EGL_NO_SURFACE)
			ELOG("failed to create EGL window surface!");

		// connect the context to the surface
		if (eglMakeCurrent(display, surface, surface, context) == EGL_FALSE)
			ELOG("failed to connect context to surface!");

		float color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	    vgSetfv(VG_CLEAR_COLOR, 4, color);
	    vgClear(0, 0, m_width, m_height);
		eglSwapBuffers(display, surface);

		vgSeti(VG_MATRIX_MODE, VG_MATRIX_IMAGE_USER_TO_SURFACE);
		vgSeti(VG_IMAGE_MODE, VG_DRAW_IMAGE_NORMAL);
		vgSeti(VG_IMAGE_QUALITY, VG_IMAGE_QUALITY_BETTER);
		vgSeti(VG_BLEND_MODE, VG_BLEND_SRC);

		vgLoadIdentity();
		vgScale(1.0f, -1.0f);
		vgTranslate(0.0f, -m_height);

		VGImage image = vgCreateImage(VG_sARGB_8888, m_width, m_height,
				VG_IMAGE_QUALITY_BETTER);

		while (Running())
		{
			m_do->Wait();
			if (m_pixmap)
			{
				vgClearImage(image, m_x, m_y, m_w, m_h);
				vgImageSubData(image, m_pixmap, m_d, VG_sARGB_8888,
						m_x, m_y, m_w, m_h);
				vgDrawImage(image);
				m_pixmap = 0;
			}
			if (m_clear)
			{
				vgClearImage(image, 0, 0, m_width, m_height);
				vgDrawImage(image);
				m_clear = false;
			}
			eglSwapBuffers(display, surface);
			m_done->Signal();
		}

		vgDestroyImage(image);

		// clear screen
		glClear(GL_COLOR_BUFFER_BIT);
		eglSwapBuffers(display, surface);

		// Release OpenGL resources
		eglMakeCurrent(display,
				EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
		eglDestroySurface(display, surface);
		eglDestroyContext(display, context);
		eglTerminate(display);

		cRpiDisplay::Close();

		DLOG("cOvg() thread ended");
	}

private:

	cCondWait* m_do;
	cCondWait* m_done;

	int m_width;
	int m_height;
	double m_aspect;

	const uint8_t *m_pixmap;
	int m_d;
	int m_x;
	int m_y;
	int m_w;
	int m_h;

	bool m_clear;

};

cRpiOsdProvider::cRpiOsdProvider() :
	cOsdProvider(),
	m_ovg(0)
{
	DLOG("new cOsdProvider()");
	m_ovg = new cOvg();
}

cRpiOsdProvider::~cRpiOsdProvider()
{
	DLOG("delete cOsdProvider()");
	delete m_ovg;
}

cOsd *cRpiOsdProvider::CreateOsd(int Left, int Top, uint Level)
{
	return new cOvgOsd(Left, Top, Level, m_ovg);
}

/* ------------------------------------------------------------------------- */

cOvgOsd::cOvgOsd(int Left, int Top, uint Level, cOvg *ovg) :
	cOsd(Left, Top, Level),
	m_ovg(ovg)
{
}

cOvgOsd::~cOvgOsd()
{
	if (Active())
		m_ovg->Clear();

	SetActive(false);
}

void cOvgOsd::Flush(void)
{
	if (!Active())
		return;

	if (IsTrueColor())
	{
		LOCK_PIXMAPS;

		while (cPixmapMemory *pm = RenderPixmaps()) {
			m_ovg->DrawPixmap(
					Left() + pm->ViewPort().X(), Top() + pm->ViewPort().Y(),
					pm->ViewPort().Width(),	pm->ViewPort().Height(),
					pm->ViewPort().Width() * sizeof(tColor), pm->Data());
			delete pm;
		}
		return;
	}

	for (int i = 0; cBitmap *bitmap = GetBitmap(i); ++i)
	{
		int x1, y1, x2, y2;
		if (bitmap->Dirty(x1, y1, x2, y2))
		{
			int w = x2 - x1 + 1;
			int h = y2 - y1 + 1;
			uint8_t *argb = (uint8_t *) malloc(w * h * sizeof(uint32_t));

			for (int y = y1; y <= y2; ++y)
			{
				for (int x = x1; x <= x2; ++x)
				{
					((uint32_t *) argb)[x - x1 + (y - y1) * w] =
					bitmap->GetColor(x, y);
				}
			}
			m_ovg->DrawPixmap(Left() + bitmap->X0() + x1, 
				Top() + bitmap->Y0() + y1, w, h, w * sizeof(tColor), argb);
			bitmap->Clean();
			free(argb);
		}
	}
}

eOsdError cOvgOsd::SetAreas(const tArea *Areas, int NumAreas)
{
	eOsdError error;
	cBitmap * bitmap;

	if (Active())
		m_ovg->Clear();

	error = cOsd::SetAreas(Areas, NumAreas);

	for (int i = 0; (bitmap = GetBitmap(i)) != NULL; i++)
		bitmap->Clean();

	return error;
}

void cOvgOsd::SetActive(bool On)
{
	if (On != Active())
	{
		cOsd::SetActive(On);
		if (!On)
			m_ovg->Clear();
		else
			if (GetBitmap(0))
				Flush();
	}
}
