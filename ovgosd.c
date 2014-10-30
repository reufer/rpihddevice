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

class cOvgCmd
{
public:

	cOvgCmd() { }
	virtual ~cOvgCmd() { }

	virtual bool Execute(EGLDisplay display, EGLSurface surface,
			int width, int height) = 0;
};

class cOvgFlush : public cOvgCmd
{
public:

	cOvgFlush() : cOvgCmd() { }
	virtual ~cOvgFlush() { }

	virtual bool Execute(EGLDisplay display, EGLSurface surface,
			int width, int height)
	{
		eglSwapBuffers(display, surface);
		return true;
	}
};

class cOvgReset : public cOvgCmd
{
public:

	cOvgReset() : cOvgCmd() { }
	virtual ~cOvgReset() { }

	virtual bool Execute(EGLDisplay display, EGLSurface surface,
			int width, int height)
	{
		return false;
	}
};

class cOvgClear : public cOvgCmd
{
public:

	cOvgClear() : cOvgCmd() { }
	virtual ~cOvgClear() { }

	virtual bool Execute(EGLDisplay display, EGLSurface surface,
			int width, int height)
	{
		float color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
	    vgSetfv(VG_CLEAR_COLOR, 4, color);
	    vgClear(0, 0, width, height);
		eglSwapBuffers(display, surface);

		return true;
	}
};

class cOvgDrawImage : public cOvgCmd
{
public:

	cOvgDrawImage(int x, int y, int w, int h, int stride, const uint8_t *argb) :
		cOvgCmd(),
		m_x(x),
		m_y(y),
		m_w(w),
		m_h(h),
		m_d(stride),
		m_argb(argb) { }

	virtual bool Execute(EGLDisplay display, EGLSurface surface,
			int width, int height)
	{
		vgSeti(VG_MATRIX_MODE, VG_MATRIX_IMAGE_USER_TO_SURFACE);
		vgSeti(VG_IMAGE_MODE, VG_DRAW_IMAGE_NORMAL);
		vgSeti(VG_IMAGE_QUALITY, VG_IMAGE_QUALITY_BETTER);
		vgSeti(VG_BLEND_MODE, VG_BLEND_SRC);

		vgLoadIdentity();
		vgScale(1.0f, -1.0f);
		vgTranslate(m_x, m_y - height);

		VGImage image = vgCreateImage(VG_sARGB_8888, m_w, m_h,
				VG_IMAGE_QUALITY_BETTER);

		vgImageSubData(image, m_argb, m_d, VG_sARGB_8888, 0, 0, m_w, m_h);
		vgDrawImage(image);

		vgDestroyImage(image);
		return true;
	}

protected:

	int m_x;
	int m_y;
	int m_w;
	int m_h;
	int m_d;
	const uint8_t* m_argb;

};

class cOvgDrawBitmap : public cOvgDrawImage
{
public:

	cOvgDrawBitmap(int x, int y, int w, int h, int stride, uint8_t *bitmap) :
		cOvgDrawImage(x, y, w, h, stride, bitmap),
		m_bitmap(bitmap) { }

	virtual ~cOvgDrawBitmap()
	{
		free(m_bitmap);
	}


protected:

	uint8_t *m_bitmap;
};

class cOvgDrawPixmap : public cOvgDrawImage
{
public:

	cOvgDrawPixmap(int x, int y, cPixmapMemory* pixmap) :
		cOvgDrawImage(x + pixmap->ViewPort().X(), y + pixmap->ViewPort().Y(),
				pixmap->ViewPort().Width(), pixmap->ViewPort().Height(),
				pixmap->ViewPort().Width() * sizeof(tColor), pixmap->Data()),
		m_pixmap(pixmap) { }

	virtual ~cOvgDrawPixmap()
	{
		delete m_pixmap;
	}

protected:

	cPixmapMemory* m_pixmap;
};

/* ------------------------------------------------------------------------- */

class cOvg : public cThread
{
public:

	cOvg() :
		cThread()
	{
		Start();
	}

	virtual ~cOvg()
	{
		Cancel(-1);
		DoCmd(new cOvgReset());

		while (Active())
			cCondWait::SleepMs(50);
	}

	void DoCmd(cOvgCmd* cmd)
	{
		Lock();
		m_commands.push(cmd);
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
		if (eglChooseConfig(display, attr, &config, 1, &nConfig)
				== EGL_FALSE)
			ELOG("failed to get EGL frame buffer config!");

		// create an EGL rendering context
		EGLContext context = eglCreateContext(display, config, NULL, NULL);
		if (context == EGL_NO_CONTEXT)
			ELOG("failed to create EGL rendering context!");

		while (Running())
		{
			bool reset = false;

			DISPMANX_DISPLAY_HANDLE_T dDisplay = vc_dispmanx_display_open(
				cRpiDisplay::GetVideoPort() == cRpiVideoPort::eHDMI ? 0 : 1);
			DISPMANX_UPDATE_HANDLE_T update = vc_dispmanx_update_start(0);

			int width, height;
			cRpiDisplay::GetSize(width, height);

			VC_RECT_T dstRect = { 0, 0, width, height };
			VC_RECT_T srcRect = { 0, 0, width << 16, height << 16 };

			DISPMANX_ELEMENT_HANDLE_T element = vc_dispmanx_element_add(
					update, dDisplay, 2 /*layer*/, &dstRect, 0, &srcRect,
					DISPMANX_PROTECTION_NONE, 0, 0, (DISPMANX_TRANSFORM_T)0);
			vc_dispmanx_update_submit_sync(update);

			EGL_DISPMANX_WINDOW_T nativewindow;
			nativewindow.element = element;
			nativewindow.width = width;
			nativewindow.height = height;

			const EGLint windowAttr[] = {
				EGL_RENDER_BUFFER, EGL_SINGLE_BUFFER,
				EGL_NONE
			};

			EGLSurface surface = eglCreateWindowSurface(display, config,
					&nativewindow, windowAttr);
			if (surface == EGL_NO_SURFACE)
				ELOG("failed to create EGL window surface!");

			// connect the context to the surface
			if (eglMakeCurrent(display, surface, surface, context)
					== EGL_FALSE)
				ELOG("failed to connect context to surface!");

			float color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		    vgSetfv(VG_CLEAR_COLOR, 4, color);
		    vgClear(0, 0, width, height);
			eglSwapBuffers(display, surface);

			while (!reset)
			{
				while (!m_commands.empty())
				{
					Lock();
					cOvgCmd* cmd = m_commands.front();
					m_commands.pop();
					Unlock();

					if (cmd)
					{
						if (!cmd->Execute(display, surface, width, height))
							reset = true;

						delete cmd;
					}
				}
				if (!reset)
					cCondWait::SleepMs(10);
			}

			// clear screen
			glClear(GL_COLOR_BUFFER_BIT);
			eglSwapBuffers(display, surface);

			// Release OpenGL resources
			eglMakeCurrent(display,
					EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
			eglDestroySurface(display, surface);

			vc_dispmanx_element_remove(update, element);
			vc_dispmanx_display_close(dDisplay);
		}

		eglDestroyContext(display, context);
		eglTerminate(display);

		DLOG("cOvg() thread ended");
	}

private:

	std::queue<cOvgCmd*> m_commands;
};

/* ------------------------------------------------------------------------- */

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
	m_ovg->DoCmd(new cOvgReset());
}

cOvgOsd::~cOvgOsd()
{
	if (Active())
		m_ovg->DoCmd(new cOvgClear());

	SetActive(false);
}

void cOvgOsd::Flush(void)
{
	if (!Active())
		return;

	if (IsTrueColor())
	{
		LOCK_PIXMAPS;

		while (cPixmapMemory *pm = RenderPixmaps())
			m_ovg->DoCmd(new cOvgDrawPixmap(Left(), Top(), pm));
	}
	else
	{
		for (int i = 0; cBitmap *bitmap = GetBitmap(i); ++i)
		{
			int x1, y1, x2, y2;
			if (bitmap->Dirty(x1, y1, x2, y2))
			{
				int w = x2 - x1 + 1;
				int h = y2 - y1 + 1;
				uint8_t *argb = (uint8_t *) malloc(w * h * sizeof(tColor));

				for (int y = y1; y <= y2; ++y)
					for (int x = x1; x <= x2; ++x)
						((uint32_t *) argb)[x - x1 + (y - y1) * w] =
								bitmap->GetColor(x, y);

				m_ovg->DoCmd(new cOvgDrawBitmap(Left() + bitmap->X0() + x1,
					Top() + bitmap->Y0() + y1, w, h, w * sizeof(tColor), argb));

				bitmap->Clean();
			}
		}
	}
	m_ovg->DoCmd(new cOvgFlush());
}

eOsdError cOvgOsd::SetAreas(const tArea *Areas, int NumAreas)
{
	eOsdError error;
	cBitmap * bitmap;

	if (Active())
		m_ovg->DoCmd(new cOvgClear());

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
			m_ovg->DoCmd(new cOvgClear());
		else
			if (GetBitmap(0))
				Flush();
	}
}
