/*
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#include <vector>
#include <queue>
#include <algorithm>

#include <ft2build.h>
#include FT_FREETYPE_H

#include <VG/openvg.h>
#include <VG/vgu.h>
#include <EGL/egl.h>
#include <GLES/gl.h>

#include "ovgosd.h"
#include "display.h"
#include "omxdevice.h"
#include "setup.h"
#include "tools.h"

class cOvgFont : public cListObject
{
public:

	static cOvgFont *Get(const char *name)
	{
		if (!s_fonts)
			Init();

		cOvgFont *font;
		for (font = s_fonts->First(); font; font = s_fonts->Next(font))
			if (!strcmp(font->Name(), name))
				return font;

		font = 0;
		bool retry = true;
		while (!font)
		{
			font = new cOvgFont(s_ftLib, name);
			if (vgGetError() == VG_OUT_OF_MEMORY_ERROR)
			{
				delete font;
				font = 0;
				s_fonts->Clear();
				if (!retry)
				{
					ELOG("OVG out of memory - failed to load font!");
					font = new cOvgFont();
					break;
				}
				retry = false;
			}
		}
		s_fonts->Add(font);
		return font;
	}

	static void CleanUp(void)
	{
		delete s_fonts;
		s_fonts = 0;

		if (FT_Done_FreeType(s_ftLib))
			ELOG("failed to deinitialize FreeType library!");
	}

	VGuint GlyphIndex(uint character)
	{
		std::vector<FT_ULong>::iterator it =
				std::find(m_characters.begin(), m_characters.end(), character);

		return it == m_characters.end() ? 0 : it - m_characters.begin();
	}

	VGfloat Escapement(VGuint index)
	{
		return index < m_escapements.size() ? m_escapements[index] : 0;
	}

	VGFont Font(void)
	{
		return m_font;
	}

	const char* Name(void)
	{
		return *m_name;
	}

private:

	#define CHAR_HEIGHT (1 << 14)

	cOvgFont(void) :
		m_font(VG_INVALID_HANDLE),
		m_name("")
	{ }

	cOvgFont(FT_Library lib, const char *name) :
		m_name(name)
	{
		ILOG("loading %s ...", *m_name);

		FT_Face face;
		if (FT_New_Face(lib, name, 0, &face))
			ELOG("failed to open %s!", name);

		m_font = vgCreateFont(face->num_glyphs);
		if (m_font == VG_INVALID_HANDLE)
		{
			FT_Done_Face(face);
			ELOG("failed to allocate new OVG font!");
			return;
		}

		FT_Set_Char_Size(face, 0, CHAR_HEIGHT, 0, 0);

		int glyphId = 0;
		FT_UInt glyphIndex;
		FT_ULong ch = FT_Get_First_Char(face, &glyphIndex);

		while (ch != 0)
		{
			if (FT_Load_Glyph(face, glyphIndex, FT_LOAD_DEFAULT))
				break;

			FT_Outline *ot = &face->glyph->outline;
			VGPath path = ConvertOutline(ot);

			VGfloat origin[] = { 0.0f, 0.0f };
			VGfloat esc[] = {
					(VGfloat)(face->glyph->advance.x) / (VGfloat)CHAR_HEIGHT,
					(VGfloat)(face->glyph->advance.y) / (VGfloat)CHAR_HEIGHT
			};

			vgSetGlyphToPath(m_font, glyphId++, path, VG_FALSE, origin, esc);

			m_characters.push_back(ch);
			m_escapements.push_back(esc[0]);

			if (path != VG_INVALID_HANDLE)
				vgDestroyPath(path);

			ch = FT_Get_Next_Char(face, ch, &glyphIndex);
		}
		FT_Done_Face(face);
	}

	~cOvgFont()
	{
		vgDestroyFont(m_font);
	}

	static void Init(void)
	{
		s_fonts = new cList<cOvgFont>;
		if (FT_Init_FreeType(&s_ftLib))
			ELOG("failed to initialize FreeType library!");
	}

	// convert freetype outline to OpenVG path,
	// based on Raspberry's vgfont library

	VGPath ConvertOutline(FT_Outline *outline)
	{
		if (outline->n_contours == 0)
			return VG_INVALID_HANDLE;

		std::vector<VGubyte> segments;
		std::vector<VGshort> coord;
		segments.reserve(256);
		coord.reserve(1024);

		FT_Vector *points = outline->points;
		const char *tags = outline->tags;
		const short* contour = outline->contours;
		short nCont = outline->n_contours;

		for (short point = 0; nCont != 0; contour++, nCont--)
		{
			short nextContour = *contour + 1;
			bool firstTag = true;
			char lastTag = 0;
			short firstPoint = point;

			for (; point < nextContour; point++)
			{
				char tag = tags[point];
				FT_Vector fpoint = points[point];
				if (firstTag)
				{
					segments.push_back(VG_MOVE_TO);
					firstTag = false;
				}
				else if (tag & 0x1)
				{
					if (lastTag & 0x1)
						segments.push_back(VG_LINE_TO);
					else if (lastTag & 0x2)
						segments.push_back(VG_CUBIC_TO);
					else
						segments.push_back(VG_QUAD_TO);
				}
				else
				{
					if (!(tag & 0x2) && !(lastTag & 0x1))
					{
						segments.push_back(VG_QUAD_TO);
						int coord_size = coord.size();

						VGshort x = (coord[coord_size-2] + fpoint.x) >> 1;
						VGshort y = (coord[coord_size-1] + fpoint.y) >> 1;

						coord.push_back(x);
						coord.push_back(y);
					}
				}
				lastTag = tag;
				coord.push_back(fpoint.x);
				coord.push_back(fpoint.y);
			}
			if (!(lastTag & 0x1))
			{
				if (lastTag & 0x2)
					segments.push_back(VG_CUBIC_TO);
				else
					segments.push_back(VG_QUAD_TO);

				coord.push_back(points[firstPoint].x);
				coord.push_back(points[firstPoint].y);
			}
			segments.push_back(VG_CLOSE_PATH);
		}

		VGPath path = vgCreatePath(VG_PATH_FORMAT_STANDARD,
				VG_PATH_DATATYPE_S_16, 1.0f / (VGfloat)CHAR_HEIGHT, 0.0f, 0, 0,
				VG_PATH_CAPABILITY_APPEND_TO);

		vgAppendPathData(path, segments.size(), &segments[0], &coord[0]);
		return path;
	}

	VGFont m_font;
	cString m_name;

	std::vector<FT_ULong> m_characters;
	std::vector<VGfloat>  m_escapements;

	static FT_Library s_ftLib;
	static cList<cOvgFont> *s_fonts;
};

FT_Library cOvgFont::s_ftLib = 0;
cList<cOvgFont> *cOvgFont::s_fonts = 0;

/* ------------------------------------------------------------------------- */

class cOvgString
{
public:

	cOvgString(const unsigned int *symbols, cOvgFont *font) :
		m_width(0.0f), m_font(font)
	{
		for (int i = 0; symbols[i]; i++)
		{
			VGuint glyphId = font->GlyphIndex(symbols[i]);
			m_glyphIds.push_back(glyphId);
			m_width += font->Escapement(glyphId);
		}
	}

	~cOvgString() { }

	      VGFont   Font(void)         { return  m_font->Font();    }
	      VGint    Length(void)       { return  m_glyphIds.size(); }
	      VGfloat  Width(void)        { return  m_width;           }
	const VGuint  *GlyphIds(void)     { return &m_glyphIds[0];     }

private:

	std::vector<VGuint> m_glyphIds;
	VGfloat m_width;
	cOvgFont *m_font;
};

/* ------------------------------------------------------------------------- */

class cOvgPaintBox
{
public:

	static void Draw(VGPath path, tColor color)
	{
		SetPaint(color);
		vgDrawPath(path, VG_FILL_PATH);
	}

	static void Draw(cOvgString *string, tColor color)
	{
		SetPaint(color);
		vgDrawGlyphs(string->Font(), string->Length(), string->GlyphIds(),
				NULL, NULL, VG_FILL_PATH, VG_TRUE);
	}

	static VGPath Rect(void)
	{
		if (!s_initialized)
			SetUp();
		return s_rect;
	}

	static VGPath Ellipse(int quadrants)
	{
		if (!s_initialized)
			SetUp();
		return s_ellipse[(quadrants < -4 || quadrants > 8) ? 4 : quadrants + 4];
	}

	static VGPath Slope(int type)
	{
		if (!s_initialized)
			SetUp();
		return s_slope[(type < 0 || type > 7) ? 0 : type];
	}

	static void CleanUp(void)
	{
		vgDestroyPaint(s_paint);
		vgDestroyPath(s_rect);

		for (int i = 0; i < 8; i++)
			vgDestroyPath(s_slope[i]);

		for (int i = 0; i < 13; i++)
			vgDestroyPath(s_ellipse[i]);

		s_initialized = false;
	}

private:

	static void SetPaint(tColor color)
	{
		if (!s_initialized)
			SetUp();

		vgSetColor(s_paint, (color << 8) + (color >> 24));
	}

	static void SetUp(void)
	{
		// paint
		s_paint = vgCreatePaint();
		vgSetPaint(s_paint, VG_FILL_PATH);

		// rectangle
		s_rect = vgCreatePath(VG_PATH_FORMAT_STANDARD, VG_PATH_DATATYPE_F,
				1.0f, 0.0f, 0, 0, VG_PATH_CAPABILITY_ALL);
		vguRect(s_rect, 0.0f, 0.0f, 1.0f, 1.0f);

		// ellipses
		for (int i = 0; i < 13; i++)
			s_ellipse[i] = vgCreatePath(VG_PATH_FORMAT_STANDARD,
				VG_PATH_DATATYPE_F,	1.0f, 0.0f, 0, 0, VG_PATH_CAPABILITY_ALL);

		vguArc(s_ellipse[0], 0.0f, 0.0f, 2.0f, 2.0f,   0,  90, VGU_ARC_OPEN);
		vguArc(s_ellipse[1], 1.0f, 0.0f, 2.0f, 2.0f,  90,  90, VGU_ARC_OPEN);
		vguArc(s_ellipse[2], 1.0f, 1.0f, 2.0f, 2.0f, 180,  90, VGU_ARC_OPEN);
		vguArc(s_ellipse[3], 0.0f, 1.0f, 2.0f, 2.0f, 270,  90, VGU_ARC_OPEN);

		// close path via corner opposed of center of arc for inverted arcs
		VGubyte cornerSeg[] = { VG_LINE_TO_ABS, VG_CLOSE_PATH };
		VGfloat cornerData[][2] = {
				{ 1.0f, 1.0f }, { 0.0f, 1.0f }, { 0.0f, 0.0f }, { 1.0f, 0.0f }
		};
		for (int i = 0; i < 4; i++)
			vgAppendPathData(s_ellipse[i], 2, cornerSeg, cornerData[i]);

		vguEllipse(s_ellipse[4], 0.5f, 0.5f, 1.0f, 1.0f);

		vguArc(s_ellipse[5], 0.0f, 1.0f, 2.0f, 2.0f, 270,   90, VGU_ARC_PIE);
		vguArc(s_ellipse[6], 1.0f, 1.0f, 2.0f, 2.0f, 180,   90, VGU_ARC_PIE);
		vguArc(s_ellipse[7], 1.0f, 0.0f, 2.0f, 2.0f,  90,   90, VGU_ARC_PIE);
		vguArc(s_ellipse[8], 0.0f, 0.0f, 2.0f, 2.0f,   0,   90, VGU_ARC_PIE);

		vguArc(s_ellipse[9],  0.0f, 0.5f, 2.0f, 1.0f, 270, 180, VGU_ARC_PIE);
		vguArc(s_ellipse[10], 0.5f, 1.0f, 1.0f, 2.0f, 180, 180, VGU_ARC_PIE);
		vguArc(s_ellipse[11], 1.0f, 0.5f, 2.0f, 1.0f,  90, 180, VGU_ARC_PIE);
		vguArc(s_ellipse[12], 0.5f, 0.0f, 1.0f, 2.0f,   0, 180, VGU_ARC_PIE);

		// slopes
		VGubyte slopeSeg[] = {
				VG_MOVE_TO_ABS, VG_LINE_TO_ABS, VG_CUBIC_TO_ABS, VG_CLOSE_PATH
		};
		// gradient of the slope: VDR uses 0.5 but 0.6 looks nicer...
		const VGfloat s = 0.6f;
		VGfloat slopeData[] = {
				1.0f, 1.0f, 1.0f, 0.0f, 1.0f - s, 0.0f, s, 1.0f, 0.0f, 1.0f
		};
		VGfloat slopeScale[][2] = {
				{ -1.0f, -1.0f }, { -1.0f,  1.0f }, { 1.0f, -1.0f },
				{  1.0f, -1.0f }, { -1.0f,  1.0f }, { 1.0f,  1.0f },
				{ -1.0f, -1.0f }
		};
		VGfloat slopeTrans[][2] = {
				{ -1.0f, -1.0f }, { -1.0f,  0.0f }, { 0.0f, -1.0f },
				{  0.0f,  0.0f }, { -1.0f, -1.0f }, { 0.0f, -1.0f },
				{ -1.0f,  0.0f }
		};
		VGfloat slopeRot[] = { 0.0f, 0.0f, 0.0f, 90.0f, 90.0f, 90.0f, 90.0f };

		VGfloat backupMatrix[9];
		vgGetMatrix(backupMatrix);

		for (int i = 0; i < 8; i++)
		{
			s_slope[i] = vgCreatePath(VG_PATH_FORMAT_STANDARD,
				VG_PATH_DATATYPE_F, 1.0f, 0.0f, 0, 0, VG_PATH_CAPABILITY_ALL);

			if (!i)
				// draw the basic form...
				vgAppendPathData(s_slope[0], 4, slopeSeg, slopeData);
			else
			{
				// .. and translate the variants
				vgLoadIdentity();
				vgRotate(slopeRot[i - 1]);
				vgScale(slopeScale[i - 1][0], slopeScale[i - 1][1]);
				vgTranslate(slopeTrans[i - 1][0], slopeTrans[i - 1][1]);
				vgTransformPath(s_slope[i], s_slope[0]);
			}
		}

		vgLoadMatrix(backupMatrix);
		s_initialized = true;
	}

	cOvgPaintBox();
	~cOvgPaintBox();

	static VGPaint s_paint;
	static VGPath  s_rect;
	static VGPath  s_ellipse[13];
	static VGPath  s_slope[8];

	static bool s_initialized;
};

VGPaint cOvgPaintBox::s_paint       =   VG_INVALID_HANDLE;
VGPath  cOvgPaintBox::s_rect        =   VG_INVALID_HANDLE;
VGPath  cOvgPaintBox::s_ellipse[13] = { VG_INVALID_HANDLE };
VGPath  cOvgPaintBox::s_slope[8]    = { VG_INVALID_HANDLE };

bool cOvgPaintBox::s_initialized = false;

/* ------------------------------------------------------------------------- */

class cOvgCmd
{
public:

	cOvgCmd() { }
	virtual ~cOvgCmd() { }

	virtual bool Execute(EGLDisplay display, EGLSurface surface,
			int width, int height) = 0;
};

class cOvgCmdFlush : public cOvgCmd
{
public:

	cOvgCmdFlush() : cOvgCmd() { }

	virtual bool Execute(EGLDisplay display, EGLSurface surface,
			int width, int height)
	{
		eglSwapBuffers(display, surface);
		return true;
	}
};

class cOvgCmdReset : public cOvgCmd
{
public:

	cOvgCmdReset(bool cleanup = false) : cOvgCmd(), m_cleanup(cleanup) { }

	virtual bool Execute(EGLDisplay display, EGLSurface surface,
			int width, int height)
	{
		if (m_cleanup)
		{
			cOvgFont::CleanUp();
			cOvgPaintBox::CleanUp();
		}
		return false;
	}

private:

	bool m_cleanup;
};

class cOvgCmdClear : public cOvgCmd
{
public:

	cOvgCmdClear() : cOvgCmd() { }

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

class cOvgCmdSaveRegion : public cOvgCmd
{
public:

	cOvgCmdSaveRegion(cRect *rect, VGImage *image) :
		cOvgCmd(), m_rect(rect), m_image(image) { }

	virtual bool Execute(EGLDisplay display, EGLSurface surface,
			int width, int height)
	{
		if (m_image && m_rect)
		{
			if (*m_image)
				vgDestroyImage(*m_image);

			*m_image = vgCreateImage(VG_sARGB_8888,
					m_rect->Width(), m_rect->Height(), VG_IMAGE_QUALITY_BETTER);

			vgGetPixels(*m_image, 0, 0, m_rect->X(), height - m_rect->Bottom()
					- 1, m_rect->Width(), m_rect->Height());
		}
		return true;
	}

private:

	cRect *m_rect;
	VGImage *m_image;
};

class cOvgCmdRestoreRegion : public cOvgCmd
{
public:

	cOvgCmdRestoreRegion(cRect *rect, VGImage *image) :
		cOvgCmd(), m_rect(rect), m_image(image) { }

	virtual bool Execute(EGLDisplay display, EGLSurface surface,
			int width, int height)
	{
		if (m_image && m_rect)
			vgSetPixels(m_rect->X(), height - m_rect->Bottom() - 1, *m_image,
					0, 0, m_rect->Width(), m_rect->Height());
		return true;
	}

private:

	cRect *m_rect;
	VGImage *m_image;
};

class cOvgCmdDropRegion : public cOvgCmd
{
public:

	cOvgCmdDropRegion(VGImage *image) : cOvgCmd(), m_image(image) { }

	virtual bool Execute(EGLDisplay display, EGLSurface surface,
			int width, int height)
	{
		if (m_image && *m_image)
			vgDestroyImage(*m_image);
		return true;
	}

private:

	VGImage *m_image;
};

class cOvgCmdDrawPixel : public cOvgCmd
{
public:

	cOvgCmdDrawPixel(int x, int y, tColor color) :
		cOvgCmd(), m_x(x), m_y(y), m_color(color) { }

	virtual bool Execute(EGLDisplay display, EGLSurface surface,
			int width, int height)
	{
		vgWritePixels(&m_color, 0, VG_sARGB_8888, m_x, height - m_y - 1, 1, 1);
		return true;
	}

private:

	int m_x;
	int m_y;
	tColor m_color;
};

class cOvgCmdDrawRectangle : public cOvgCmd
{
public:

	cOvgCmdDrawRectangle(int x, int y, int w, int h, tColor color) :
		cOvgCmd(), m_x(x), m_y(y), m_w(w), m_h(h), m_color(color) { }

	virtual bool Execute(EGLDisplay display, EGLSurface surface,
			int width, int height)
	{
		vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
		vgSeti(VG_BLEND_MODE, VG_BLEND_SRC);

		vgLoadIdentity();
		vgScale(1.0f, -1.0f);
		vgTranslate(m_x, m_y - height);
		vgScale(m_w, m_h);

		cOvgPaintBox::Draw(cOvgPaintBox::Rect(), m_color);
		return true;
	}

private:

	int m_x;
	int m_y;
	int m_w;
	int m_h;
	tColor m_color;
};

class cOvgCmdDrawEllipse : public cOvgCmd
{
public:

	cOvgCmdDrawEllipse(int x, int y, int w, int h, tColor color, int quadrants) :
		cOvgCmd(), m_x(x), m_y(y), m_w(w), m_h(h), m_quadrants(quadrants),
		m_color(color) { }

	virtual bool Execute(EGLDisplay display, EGLSurface surface,
			int width, int height)
	{
		vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
		vgSeti(VG_BLEND_MODE, VG_BLEND_SRC);

		vgLoadIdentity();
		vgScale(1.0f, -1.0f);
		vgTranslate(m_x, m_y - height);
		vgScale(m_w, m_h);

		cOvgPaintBox::Draw(cOvgPaintBox::Ellipse(m_quadrants), m_color);
		return true;
	}

private:

	int m_x;
	int m_y;
	int m_w;
	int m_h;
	int m_quadrants;
	tColor m_color;
};

class cOvgCmdDrawSlope : public cOvgCmd
{
public:

	cOvgCmdDrawSlope(int x, int y, int w, int h, tColor color, int type) :
		cOvgCmd(), m_x(x), m_y(y), m_w(w), m_h(h), m_type(type),
		m_color(color) { }

	virtual bool Execute(EGLDisplay display, EGLSurface surface,
			int width, int height)
	{
		vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
		vgSeti(VG_BLEND_MODE, VG_BLEND_SRC);

		vgLoadIdentity();
		vgScale(1.0f, -1.0f);
		vgTranslate(m_x, m_y - height);
		vgScale(m_w, m_h);

		cOvgPaintBox::Draw(cOvgPaintBox::Slope(m_type), m_color);
		return true;
	}

private:

	int m_x;
	int m_y;
	int m_w;
	int m_h;
	int m_type;
	tColor m_color;
};

class cOvgCmdDrawText : public cOvgCmd
{
public:

	cOvgCmdDrawText(int x, int y, unsigned int *symbols, cString *fontName,
			int fontSize, tColor color, int width, int height, int alignment) :
		cOvgCmd(), m_x(x), m_y(y), m_symbols(symbols), m_fontName(fontName),
		m_fontSize(fontSize), m_color(color), m_width(width), m_height(height),
		m_alignment(alignment) { }

	virtual ~cOvgCmdDrawText()
	{
		free(m_symbols);
		delete m_fontName;
	}

	virtual bool Execute(EGLDisplay display, EGLSurface surface,
			int width, int height)
	{
		vgSeti(VG_MATRIX_MODE, VG_MATRIX_GLYPH_USER_TO_SURFACE);
		vgSeti(VG_BLEND_MODE, VG_BLEND_SRC);

		cOvgFont *font = cOvgFont::Get(*m_fontName);
		cOvgString *string = new cOvgString(m_symbols, font);

		VGfloat strWidth = string->Width() * (VGfloat)m_fontSize;
		VGfloat strHeight = (VGfloat)m_fontSize;
		VGfloat offsetX = 0;
		VGfloat offsetY = 0;

		if (m_width)
		{
			if (m_alignment & taLeft)
			{
				if (m_alignment & taBorder)
					offsetX += max(strHeight / TEXT_ALIGN_BORDER, 1.0f);
			}
			else if (m_alignment & taRight)
			{
				if (strWidth < (VGfloat)m_width)
					offsetX += m_width - strWidth;
				if (m_alignment & taBorder)
					offsetX -= max(strHeight / TEXT_ALIGN_BORDER, 1.0f);
			}
			else
			{
				if (strWidth < (VGfloat)m_width)
					offsetX += (m_width - strWidth) / 2;
			}
		}
		if (m_height)
		{
			if (m_alignment & taTop) { }
			else if (m_alignment & taBottom)
			{
				if (strHeight < (VGfloat)m_height)
					offsetY += m_height - strHeight;
			}
			else
			{
				if (strHeight < (VGfloat)m_height)
					offsetY += (m_height - strHeight) / 2;
			}
		}

		vgLoadIdentity();
		vgTranslate(m_x + offsetX, height - m_y - m_fontSize - offsetY + 2);
		vgScale(m_fontSize, m_fontSize);

		VGfloat origin[2] = { 0.0f, 0.0f };
		vgSetfv(VG_GLYPH_ORIGIN, 2, origin);

		VGint cropArea[4] = {
				m_width ? m_x : 0, m_height ? height - m_y - m_height - 1 : 0,
				m_width ? m_width : width - 1, m_height ? m_height : height - 1
		};
		vgSetiv(VG_SCISSOR_RECTS, 4, cropArea);
		vgSeti(VG_SCISSORING, VG_TRUE);

		cOvgPaintBox::Draw(string, m_color);

		vgSeti(VG_SCISSORING, VG_FALSE);
		delete string;
		return true;
	}

private:

	int m_x;
	int m_y;
	unsigned int *m_symbols;
	cString *m_fontName;
	int m_fontSize;
	tColor m_color;
	int m_width;
	int m_height;
	int m_alignment;
};

class cOvgCmdDrawImage : public cOvgCmd
{
public:

	cOvgCmdDrawImage(int x, int y, int w, int h, const tColor *argb,
			bool overlay, double scaleX, double scaleY) :
		cOvgCmd(), m_x(x), m_y(y), m_w(w), m_h(h), m_argb(argb),
		m_overlay(overlay), m_scaleX(scaleX), m_scaleY(scaleY) { }

	virtual bool Execute(EGLDisplay display, EGLSurface surface,
			int width, int height)
	{
		vgSeti(VG_MATRIX_MODE, VG_MATRIX_IMAGE_USER_TO_SURFACE);
		vgSeti(VG_IMAGE_MODE, VG_DRAW_IMAGE_NORMAL);
		vgSeti(VG_IMAGE_QUALITY, VG_IMAGE_QUALITY_BETTER);
		vgSeti(VG_BLEND_MODE, m_overlay ? VG_BLEND_SRC_OVER : VG_BLEND_SRC);

		vgLoadIdentity();
		vgScale(1.0f, -1.0f);
		vgTranslate(m_x, m_y - height);
		vgScale(m_scaleX, m_scaleY);

		VGImage image = vgCreateImage(VG_sARGB_8888, m_w, m_h,
				VG_IMAGE_QUALITY_BETTER);

		vgImageSubData(image, m_argb, m_w * sizeof(tColor),
				VG_sARGB_8888, 0, 0, m_w, m_h);
		vgDrawImage(image);

		vgDestroyImage(image);
		return true;
	}

protected:

	int m_x;
	int m_y;
	int m_w;
	int m_h;
	const tColor* m_argb;
	bool m_overlay;
	double m_scaleX;
	double m_scaleY;
};

class cOvgCmdDrawBitmap : public cOvgCmdDrawImage
{
public:

	cOvgCmdDrawBitmap(int x, int y, int w, int h, tColor *bitmap,
			bool overlay = false, double scaleX = 1.0f, double scaleY = 1.0f) :
		cOvgCmdDrawImage(x, y, w, h, bitmap, overlay, scaleX, scaleY),
		m_bitmap(bitmap) { }

	virtual ~cOvgCmdDrawBitmap()
	{
		free(m_bitmap);
	}

protected:

	tColor *m_bitmap;
};

class cOvgCmdDrawPixmap : public cOvgCmdDrawImage
{
public:

	cOvgCmdDrawPixmap(int x, int y, cPixmapMemory* pixmap) :
		cOvgCmdDrawImage(x + pixmap->ViewPort().X(), y + pixmap->ViewPort().Y(),
				pixmap->ViewPort().Width(), pixmap->ViewPort().Height(),
				(tColor*)pixmap->Data(), false, 1.0f, 1.0f),
		m_pixmap(pixmap) { }

	virtual ~cOvgCmdDrawPixmap()
	{
		delete m_pixmap;
	}

protected:

	cPixmapMemory* m_pixmap;
};

/* ------------------------------------------------------------------------- */

class cOvgThread : public cThread
{
public:

	cOvgThread() : cThread("ovgthread"), m_wait(new cCondWait())
	{
		Start();
	}

	virtual ~cOvgThread()
	{
		Cancel(-1);
		DoCmd(new cOvgCmdReset(), true);

		while (Active())
			cCondWait::SleepMs(50);

		delete m_wait;
	}

	void DoCmd(cOvgCmd* cmd, bool signal = false)
	{
		Lock();
		m_commands.push(cmd);
		Unlock();

		if (signal)
			m_wait->Signal();
	}

protected:

	virtual void Action(void)
	{
		DLOG("cOvgThread() thread started");

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
				if (m_commands.empty())
					m_wait->Wait(100);
				else
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
			}

			// Release OpenGL resources
			eglMakeCurrent(display,
					EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
			eglDestroySurface(display, surface);

			vc_dispmanx_element_remove(update, element);
			vc_dispmanx_display_close(dDisplay);
		}

		cOvgFont::CleanUp();
		cOvgPaintBox::CleanUp();

		eglDestroyContext(display, context);
		eglTerminate(display);

		DLOG("cOvgThread() thread ended");
	}

private:

	std::queue<cOvgCmd*> m_commands;
	cCondWait *m_wait;

};

/* ------------------------------------------------------------------------- */

class cOvgOsd : public cOsd
{
public:

	cOvgOsd(int Left, int Top, uint Level, cOvgThread *ovg) :
		cOsd(Left, Top, Level),
		m_ovg(ovg), m_savedImage(0)
	{ }

	virtual ~cOvgOsd()
	{
		SetActive(false);
		if (m_savedImage)
			m_ovg->DoCmd(new cOvgCmdDropRegion(&m_savedImage));
	}

	virtual eOsdError SetAreas(const tArea *Areas, int NumAreas)
	{
		if (Active())
			m_ovg->DoCmd(new cOvgCmdClear());

		return cOsd::SetAreas(Areas, NumAreas);
	}

	virtual void SaveRegion(int x1, int y1, int x2, int y2)
	{
		if (!Active())
			return;

		m_savedRect = cRect(x1 + Left(), y1 + Top(), x2 - x1 + 1, y2 - y1 + 1);
		m_ovg->DoCmd(new cOvgCmdSaveRegion(&m_savedRect, &m_savedImage));
	}

	virtual void RestoreRegion(void)
	{
		if (!Active())
			return;

		if (m_savedImage)
			m_ovg->DoCmd(new cOvgCmdRestoreRegion(&m_savedRect, &m_savedImage));
	}

	virtual void DrawPixel(int x, int y, tColor Color)
	{
		if (!Active())
			return;

		m_ovg->DoCmd(new cOvgCmdDrawPixel(x + Left(), y + Top(), Color));
	}

	virtual void DrawBitmap(int x, int y, const cBitmap &Bitmap,
			tColor ColorFg = 0, tColor ColorBg = 0,
			bool ReplacePalette = false, bool Overlay = false)
	{
		if (!Active())
			return;

		bool specialColors = ColorFg || ColorBg;
		tColor *argb = MALLOC(tColor, Bitmap.Width() * Bitmap.Height());
		if (!argb)
			return;

		tColor *p = argb;
		for (int py = 0; py < Bitmap.Height(); py++)
			for (int px = 0; px < Bitmap.Width(); px++)
			{
				tIndex index = *Bitmap.Data(px, py);
				*p++ = (!index && Overlay) ? clrTransparent : (specialColors ?
						(index == 0 ? ColorBg :	index ==1 ? ColorFg :
								Bitmap.Color(index)) : Bitmap.Color(index));
			}

		m_ovg->DoCmd(new cOvgCmdDrawBitmap(Left() + x,	Top() + y,
				Bitmap.Width(), Bitmap.Height(), argb, Overlay));
	}

	virtual void DrawScaledBitmap(int x, int y, const cBitmap &Bitmap,
			double FactorX, double FactorY, bool AntiAlias = false)
	{
		if (!Active())
			return;

		tColor *argb = MALLOC(tColor, Bitmap.Width() * Bitmap.Height());
		if (!argb)
			return;

		tColor *p = argb;
		for (int py = 0; py < Bitmap.Height(); py++)
			for (int px = 0; px < Bitmap.Width(); px++)
				*p++ = Bitmap.Color(*Bitmap.Data(px, py));

		m_ovg->DoCmd(new cOvgCmdDrawBitmap(Left() + x,	Top() + y, Bitmap.Width(),
				Bitmap.Height(), argb, false, FactorX, FactorY));
	}

	virtual void DrawRectangle(int x1, int y1, int x2, int y2, tColor Color)
	{
		if (!Active())
			return;

		m_ovg->DoCmd(new cOvgCmdDrawRectangle(x1 + Left(), y1 + Top(),
				x2 - x1 + 1, y2 - y1 + 1, Color));
	}

	virtual void DrawEllipse(int x1, int y1, int x2, int y2, tColor Color,
			int Quadrants = 0)
	{
		if (!Active())
			return;

		m_ovg->DoCmd(new cOvgCmdDrawEllipse(x1 + Left(), y1 + Top(),
				x2 - x1 + 1, y2 - y1 + 1, Color, Quadrants));
	}

	virtual void DrawSlope(int x1, int y1, int x2, int y2, tColor Color,
			int Type)
	{
		if (!Active())
			return;

		m_ovg->DoCmd(new cOvgCmdDrawSlope(x1 + Left(), y1 + Top(),
				x2 - x1 + 1, y2 - y1 + 1, Color, Type));
	}

	virtual void DrawText(int x, int y, const char *s, tColor ColorFg,
			tColor ColorBg, const cFont *Font, int Width = 0, int Height = 0,
			int Alignment = taDefault)
	{
		if (!Active())
			return;

		if (Width && ColorBg != clrTransparent)
			m_ovg->DoCmd(new cOvgCmdDrawRectangle(x + Left(), y + Top(),
					Width, Height ? Height : Font->Height(), ColorBg));
		if (!s)
			return;

		int len = Utf8StrLen(s);
		if (len)
		{
			unsigned int *symbols = MALLOC(unsigned int, len + 1);
			if (!symbols)
				return;

			Utf8ToArray(s, symbols, len + 1);
			m_ovg->DoCmd(new cOvgCmdDrawText(x + Left(), y + Top(), symbols,
					new cString(Font->FontName()), Font->Size(), ColorFg,
					Width, Height, Alignment));
		}
	}

	virtual void Flush(void)
	{
		if (!Active())
			return;

		m_ovg->DoCmd(new cOvgCmdFlush(), true);
	}

protected:

	virtual void SetActive(bool On)
	{
		if (On != Active())
		{
			cOsd::SetActive(On);
			if (!On)
				m_ovg->DoCmd(new cOvgCmdClear());
			else
				if (GetBitmap(0))
					Flush();
		}
	}

private:

	cOvgThread *m_ovg;
	cRect m_savedRect;
	VGImage m_savedImage;

};

/* ------------------------------------------------------------------------- */

class cOvgRawOsd : public cOsd
{
public:

	cOvgRawOsd(int Left, int Top, uint Level, cOvgThread *ovg) :
		cOsd(Left, Top, Level),
		m_ovg(ovg)
	{ }

	virtual ~cOvgRawOsd()
	{
		SetActive(false);
	}

	virtual void Flush(void)
	{
		if (!Active())
			return;

		if (IsTrueColor())
		{
			LOCK_PIXMAPS;
			while (cPixmapMemory *pm = RenderPixmaps())
				m_ovg->DoCmd(new cOvgCmdDrawPixmap(Left(), Top(), pm));
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
					tColor *argb = MALLOC(tColor, w * h);
					if (!argb)
						return;

					tColor *p = argb;
					for (int y = y1; y <= y2; ++y)
						for (int x = x1; x <= x2; ++x)
							*p++ = bitmap->GetColor(x, y);

					m_ovg->DoCmd(new cOvgCmdDrawBitmap(Left() + bitmap->X0()
							+ x1, Top() + bitmap->Y0() + y1, w, h, argb));

					bitmap->Clean();
				}
			}
		}
		m_ovg->DoCmd(new cOvgCmdFlush(), true);
	}

	virtual eOsdError SetAreas(const tArea *Areas, int NumAreas)
	{
		eOsdError error;
		cBitmap *bitmap;

		if (Active())
			m_ovg->DoCmd(new cOvgCmdClear());

		error = cOsd::SetAreas(Areas, NumAreas);

		for (int i = 0; (bitmap = GetBitmap(i)) != NULL; i++)
			bitmap->Clean();

		return error;
	}

protected:

	virtual void SetActive(bool On)
	{
		if (On != Active())
		{
			cOsd::SetActive(On);
			if (!On)
				m_ovg->DoCmd(new cOvgCmdClear());
			else
				if (GetBitmap(0))
					Flush();
		}
	}

private:

	cOvgThread *m_ovg;

};

/* ------------------------------------------------------------------------- */

cRpiOsdProvider* cRpiOsdProvider::s_instance = 0;

cRpiOsdProvider::cRpiOsdProvider() :
	cOsdProvider(),
	m_ovg(0)
{
	DLOG("new cOsdProvider()");
	m_ovg = new cOvgThread();
	s_instance = this;
}

cRpiOsdProvider::~cRpiOsdProvider()
{
	DLOG("delete cOsdProvider()");
	s_instance = 0;
	delete m_ovg;
}

cOsd *cRpiOsdProvider::CreateOsd(int Left, int Top, uint Level)
{
	if (cRpiSetup::IsHighLevelOsd())
		return new cOvgOsd(Left, Top, Level, m_ovg);
	else
		return new cOvgRawOsd(Left, Top, Level, m_ovg);
}

void cRpiOsdProvider::ResetOsd(bool cleanup)
{
	if (s_instance)
		s_instance->m_ovg->DoCmd(new cOvgCmdReset(cleanup));

	UpdateOsdSize(true);
}
