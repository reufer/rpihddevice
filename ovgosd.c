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

/* ------------------------------------------------------------------------- */

// glyphs containing kerning cache, based on VDR's implementation

class cOvgGlyph : public cListObject
{
public:

	cOvgGlyph(uint charCode, VGfloat advanceX, VGfloat advanceY) :
		m_charCode(charCode), m_advanceX(advanceX), m_advanceY(advanceY) { }

	virtual ~cOvgGlyph() { }

	uint    CharCode(void) { return m_charCode; }
	VGfloat AdvanceX(void) { return m_advanceX; }
	VGfloat AdvanceY(void) { return m_advanceY; }

	bool GetKerningCache(uint prevSym, VGfloat &kerning)
	{
		for (int i = m_kerningCache.Size(); --i > 0; )
			if (m_kerningCache[i].m_prevSym == prevSym)
			{
				kerning = m_kerningCache[i].m_kerning;
				return true;
			}

		return false;
	}

	void SetKerningCache(uint prevSym, VGfloat kerning)
	{
		m_kerningCache.Append(tKerning(prevSym, kerning));
	}

private:

	struct tKerning
	{
	public:

		tKerning(uint prevSym, VGfloat kerning = 0.0f)
		{
			m_prevSym = prevSym;
			m_kerning = kerning;
		}

		uint m_prevSym;
		VGfloat m_kerning;
	};

	uint m_charCode;

	VGfloat m_advanceX;
	VGfloat m_advanceY;

	cVector<tKerning> m_kerningCache;
};

/* ------------------------------------------------------------------------- */

#define CHAR_HEIGHT (1 << 14)

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
					ELOG("[OpenVG] out of memory - failed to load font!");
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

	cOvgGlyph* Glyph(uint charCode) const
	{
		cOvgGlyph *glyph = 0;
		for (glyph = m_glyphs.First(); glyph; glyph = m_glyphs.Next(glyph))
			if (glyph->CharCode() == charCode)
				return glyph;

		glyph = ConvertChar(charCode);
		if (glyph)
			m_glyphs.Add(glyph);

		return glyph;
	}

	VGfloat Kerning(cOvgGlyph *glyph, uint prevSym) const
	{
		VGfloat kerning = 0.0f;
		if (glyph && prevSym)
		{
			if (!glyph->GetKerningCache(prevSym, kerning))
			{
				FT_Vector delta;
				FT_UInt cur = FT_Get_Char_Index(m_face,	glyph->CharCode());
				FT_UInt prev = FT_Get_Char_Index(m_face, prevSym);
				FT_Get_Kerning(m_face, prev, cur, FT_KERNING_DEFAULT, &delta);

				kerning = (VGfloat)delta.x / CHAR_HEIGHT;
				glyph->SetKerningCache(prevSym, kerning);
			}
		}
		return kerning;
	}

	VGfloat     Height(void)    { return  m_height;    }
	VGfloat     Descender(void) { return  m_descender; }
	VGFont      Font(void)      { return  m_font;      }
	const char* Name(void)      { return *m_name;      }

private:

	cOvgFont(void) :
		m_font(VG_INVALID_HANDLE),
		m_name(""),
		m_height(0.0f),
		m_descender(0.0f),
		m_face(0)
	{ }

	cOvgFont(FT_Library lib, const char *name) :
		m_name(name)
	{
		ILOG("loading %s ...", *m_name);

		if (FT_New_Face(lib, name, 0, &m_face))
			ELOG("failed to open %s!", name);

		m_font = vgCreateFont(m_face->num_glyphs);
		if (m_font == VG_INVALID_HANDLE)
		{
			ELOG("failed to allocate new OVG font!");
			return;
		}

		FT_Set_Char_Size(m_face, 0, CHAR_HEIGHT, 0, 0);
		m_height = (VGfloat)(m_face->size->metrics.height) / CHAR_HEIGHT;
		m_descender = (VGfloat)(abs(m_face->size->metrics.descender)) /
				CHAR_HEIGHT;
#if 0
		FT_UInt glyphIndex;
		FT_ULong ch = FT_Get_First_Char(m_face, &glyphIndex);

		while (ch != 0)
		{
			if (FT_Load_Glyph(m_face, glyphIndex, FT_LOAD_DEFAULT))
				break;

			FT_Outline *ot = &m_face->glyph->outline;
			VGPath path = ConvertOutline(ot);

			VGfloat origin[] = { 0.0f, 0.0f };
			VGfloat esc[] = {
					(VGfloat)(m_face->glyph->advance.x) / CHAR_HEIGHT,
					(VGfloat)(m_face->glyph->advance.y) / CHAR_HEIGHT
			};

			vgSetGlyphToPath(m_font, ch, path, VG_FALSE, origin, esc);

			m_glyphs.Add(new cOvgGlyph(ch, esc[0], esc[1]));

			if (path != VG_INVALID_HANDLE)
				vgDestroyPath(path);

			ch = FT_Get_Next_Char(m_face, ch, &glyphIndex);
		}
#endif
	}

	~cOvgFont()
	{
		vgDestroyFont(m_font);
		FT_Done_Face(m_face);
	}

	static void Init(void)
	{
		s_fonts = new cList<cOvgFont>;
		if (FT_Init_FreeType(&s_ftLib))
			ELOG("failed to initialize FreeType library!");
	}

	cOvgGlyph *ConvertChar(uint charCode) const
	{
		FT_UInt glyphIndex = FT_Get_Char_Index(m_face, charCode);
		if (FT_Load_Glyph(m_face, glyphIndex, FT_LOAD_DEFAULT))
			return 0;

		FT_Outline *ot = &m_face->glyph->outline;
		VGPath path = ConvertOutline(ot);

		VGfloat origin[] = { 0.0f, 0.0f };
		VGfloat esc[] = {
				(VGfloat)(m_face->glyph->advance.x) / CHAR_HEIGHT,
				(VGfloat)(m_face->glyph->advance.y) / CHAR_HEIGHT
		};

		vgSetGlyphToPath(m_font, charCode, path, VG_FALSE, origin, esc);
		if (path != VG_INVALID_HANDLE)
			vgDestroyPath(path);

		return new cOvgGlyph(charCode, esc[0], esc[1]);
	}

	// convert freetype outline to OpenVG path,
	// based on Raspberry Pi's vgfont library

	VGPath ConvertOutline(FT_Outline *outline) const
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
				VG_PATH_DATATYPE_S_16, 1.0f / (VGfloat)CHAR_HEIGHT, 0.0f,
				segments.size(), coord.size(), VG_PATH_CAPABILITY_APPEND_TO);

		if (path != VG_INVALID_HANDLE)
			vgAppendPathData(path, segments.size(), &segments[0], &coord[0]);

		return path;
	}

	VGFont m_font;
	cString m_name;
	VGfloat m_height;
	VGfloat m_descender;

	mutable cList<cOvgGlyph> m_glyphs;

	FT_Face m_face;

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
		m_width(0.0f), m_height(font->Height()), m_descender(font->Descender()),
		m_font(font)
	{
		uint prevSym = 0;
		for (int i = 0; symbols[i]; i++)
			if (cOvgGlyph *g = font->Glyph(symbols[i]))
			{
				VGfloat kerning = 0.0f;
				if (prevSym)
				{
					kerning = m_font->Kerning(g, prevSym);
					m_kerning.push_back(kerning);
				}
				m_width += g->AdvanceX() + kerning;
				m_glyphIds.push_back(symbols[i]);
				prevSym = symbols[i];
			}
	}

	~cOvgString() { }

	      VGFont   Font(void)         { return  m_font->Font();    }
	      VGint    Length(void)       { return  m_glyphIds.size(); }
	      VGfloat  Width(void)        { return  m_width;           }
	      VGfloat  Height(void)       { return  m_height;          }
	      VGfloat  Descender(void)    { return  m_descender;       }
	const VGuint  *GlyphIds(void)     { return &m_glyphIds[0];     }
	const VGfloat *Kerning(void)      { return &m_kerning[0];      }

private:

	std::vector<VGuint> m_glyphIds;
	std::vector<VGfloat> m_kerning;

	VGfloat m_width;
	VGfloat m_height;
	VGfloat m_descender;
	cOvgFont *m_font;
};

/* ------------------------------------------------------------------------- */

class cOvgPaintBox
{
public:

	static void Draw(VGPath path)
	{
		vgDrawPath(path, VG_FILL_PATH);
	}

	static void Draw(cOvgString *string)
	{
		vgDrawGlyphs(string->Font(), string->Length(), string->GlyphIds(),
				string->Kerning(), NULL, VG_FILL_PATH, VG_TRUE);
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

	static void SetColor(tColor color)
	{
		if (!s_initialized)
			SetUp();

		vgSetParameteri(s_paint, VG_PAINT_TYPE,	VG_PAINT_TYPE_COLOR);
		vgSetColor(s_paint, (color << 8) + (color >> 24));
		vgSetPaint(s_paint, VG_FILL_PATH);
	}

	static void SetAlpha(int alpha)
	{
		if (!s_initialized)
			SetUp();

		alpha = constrain(alpha, ALPHA_TRANSPARENT, ALPHA_OPAQUE);
		VGfloat values[] = {
				1.0f, 1.0f, 1.0f, alpha / 255.0f, 0.0f, 0.0f, 0.0f, 0.0f };

		vgSetfv(VG_COLOR_TRANSFORM_VALUES, 8, values);
		vgSeti(VG_COLOR_TRANSFORM, alpha == ALPHA_OPAQUE ? VG_FALSE : VG_TRUE);
	}

	static void SetPattern(VGImage image = VG_INVALID_HANDLE)
	{
		if (!s_initialized)
			SetUp();

		vgPaintPattern(s_paint, image);
		if (image == VG_INVALID_HANDLE)
			return;

		vgSetParameteri(s_paint, VG_PAINT_TYPE, VG_PAINT_TYPE_PATTERN);
		vgSetParameteri(s_paint, VG_PAINT_PATTERN_TILING_MODE, VG_TILE_REPEAT);
		vgSetPaint(s_paint, VG_FILL_PATH);
	}

	static void SetScissoring(int x = 0, int y = 0, int w = 0, int h = 0)
	{
		VGint cropArea[4] = { x, y, w, h };
		vgSetiv(VG_SCISSOR_RECTS, 4, cropArea);
		vgSeti(VG_SCISSORING, w && h ? VG_TRUE : VG_FALSE);
	}

private:

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

		vguArc(s_ellipse[0], 0.0f, 1.0f, 2.0f, 2.0f, 270,  90, VGU_ARC_OPEN);
		vguArc(s_ellipse[1], 1.0f, 1.0f, 2.0f, 2.0f, 180,  90, VGU_ARC_OPEN);
		vguArc(s_ellipse[2], 1.0f, 0.0f, 2.0f, 2.0f,  90,  90, VGU_ARC_OPEN);
		vguArc(s_ellipse[3], 0.0f, 0.0f, 2.0f, 2.0f,   0,  90, VGU_ARC_OPEN);

		// close path via corner opposed of center of arc for inverted arcs
		VGubyte cornerSeg[] = { VG_LINE_TO_ABS, VG_CLOSE_PATH };
		VGfloat cornerData[][2] = {
				{ 1.0f, 0.0f }, { 0.0f, 0.0f }, { 0.0f, 1.0f }, { 1.0f, 1.0f }
		};
		for (int i = 0; i < 4; i++)
			vgAppendPathData(s_ellipse[i], 2, cornerSeg, cornerData[i]);

		vguEllipse(s_ellipse[4], 0.5f, 0.5f, 1.0f, 1.0f);

		vguArc(s_ellipse[5],  0.0f, 0.0f, 2.0f, 2.0f,   0,  90, VGU_ARC_PIE);
		vguArc(s_ellipse[6],  1.0f, 0.0f, 2.0f, 2.0f,  90,  90, VGU_ARC_PIE);
		vguArc(s_ellipse[7],  1.0f, 1.0f, 2.0f, 2.0f, 180,  90, VGU_ARC_PIE);
		vguArc(s_ellipse[8],  0.0f, 1.0f, 2.0f, 2.0f, 270,  90, VGU_ARC_PIE);

		vguArc(s_ellipse[9],  0.0f, 0.5f, 2.0f, 1.0f, 270, 180, VGU_ARC_PIE);
		vguArc(s_ellipse[10], 0.5f, 0.0f, 1.0f, 2.0f,   0, 180, VGU_ARC_PIE);
		vguArc(s_ellipse[11], 1.0f, 0.5f, 2.0f, 1.0f,  90, 180, VGU_ARC_PIE);
		vguArc(s_ellipse[12], 0.5f, 1.0f, 1.0f, 2.0f, 180, 180, VGU_ARC_PIE);

		// slopes
		VGubyte slopeSeg[] = {
				VG_MOVE_TO_ABS, VG_LINE_TO_ABS, VG_CUBIC_TO_ABS, VG_CLOSE_PATH
		};
		// gradient of the slope: VDR uses 0.5 but 0.6 looks nicer...
		const VGfloat s = 0.6f;
		VGfloat slopeData[] = {
				1.0f, 0.0f, 1.0f, 1.0f, 1.0f - s, 1.0f, s, 0.0f, 0.0f, 0.0f
		};
		VGfloat slopeScale[][2] = {
				{ -1.0f, -1.0f }, { -1.0f,  1.0f }, { 1.0f, -1.0f },
				{ -1.0f,  1.0f }, {  1.0f, -1.0f }, {-1.0f, -1.0f },
				{  1.0f,  1.0f }
		};
		VGfloat slopeTrans[][2] = {
				{ -1.0f, -1.0f }, { -1.0f,  0.0f }, {  0.0f, -1.0f },
				{ -1.0f, -1.0f }, {  0.0f,  0.0f }, { -1.0f,  0.0f },
				{  0.0f, -1.0f }
		};
		VGfloat slopeRot[] = { 0.0f, 0.0f, 0.0f, 90.0f, 90.0f, 90.0f, 90.0f };

		VGfloat backupMatrix[9];
		vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
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

class cEgl
{
public:

	EGLDisplay display;
	EGLContext context;
	EGLConfig  config;
	EGLint     nConfig;
	EGLSurface surface;
	EGLSurface currentSurface;
	EGL_DISPMANX_WINDOW_T window;

	static const char* errStr(EGLint error)
	{
		return 	error == EGL_SUCCESS             ? "success"             :
				error == EGL_NOT_INITIALIZED     ? "not initialized"     :
				error == EGL_BAD_ACCESS          ? "bad access"          :
				error == EGL_BAD_ALLOC           ? "bad alloc"           :
				error == EGL_BAD_ATTRIBUTE       ? "bad attribute"       :
				error == EGL_BAD_CONTEXT         ? "bad context"         :
				error == EGL_BAD_CONFIG          ? "bad config"          :
				error == EGL_BAD_CURRENT_SURFACE ? "bad current surface" :
				error == EGL_BAD_DISPLAY         ? "bad display"         :
				error == EGL_BAD_SURFACE         ? "bad surface"         :
				error == EGL_BAD_MATCH           ? "bad match"           :
				error == EGL_BAD_PARAMETER       ? "bad parameter"       :
				error == EGL_BAD_NATIVE_PIXMAP   ? "bad native pixmap"   :
				error == EGL_BAD_NATIVE_WINDOW   ? "bad native window"   :
				error == EGL_CONTEXT_LOST        ? "context lost"        :
						"unknown error";
	}
};

/* ------------------------------------------------------------------------- */

struct tOvgImageRef
{
	VGImage image;
	bool used;
};

/* ------------------------------------------------------------------------- */

class cOvgSavedRegion
{
public:

	cOvgSavedRegion() : image(VG_INVALID_HANDLE), rect(cRect()) { }
	VGImage image;
	cRect rect;
};

/* ------------------------------------------------------------------------- */

class cOvgRenderTarget
{
public:

	cOvgRenderTarget(int _width = 0, int _height = 0) :
		surface(EGL_NO_SURFACE),
		image(VG_INVALID_HANDLE),
		width(_width),
		height(_height),
		initialized(false) { }

	virtual ~cOvgRenderTarget() { }

	static bool MakeDefault(cEgl *egl)
	{
		if (eglMakeCurrent(egl->display, egl->surface, egl->surface,
				egl->context) == EGL_FALSE)
		{
			ELOG("[EGL] failed to connect context to surface: %s!",
					cEgl::errStr(eglGetError()));
			return false;
		}
		egl->currentSurface = egl->surface;
		return true;
	}

	virtual bool MakeCurrent(cEgl *egl)
	{
		// if this is a window surface, check for an update after OSD reset
		if (image == VG_INVALID_HANDLE && surface != egl->surface)
		{
			surface = egl->surface;
			width = egl->window.width;
			height = egl->window.height;
		}

		if (egl->currentSurface == surface)
			return true;

		if (eglMakeCurrent(egl->display, surface, surface, egl->context) ==
				EGL_FALSE)
		{
			ELOG("[EGL] failed to connect context to surface: %s!",
					cEgl::errStr(eglGetError()));
			return false;
		}
		egl->currentSurface = surface;
		return true;
	}

	EGLSurface surface;
	VGImage    image;
	int        width;
	int        height;
	bool       initialized;

private:

	cOvgRenderTarget(const cOvgRenderTarget&);
	cOvgRenderTarget& operator= (const cOvgRenderTarget&);
};

/* ------------------------------------------------------------------------- */

class cOvgCmd
{
public:

	cOvgCmd(cOvgRenderTarget *target) : m_target(target) { }
	virtual ~cOvgCmd() { }

	virtual bool Execute(cEgl *egl) = 0;
	virtual const char* Description(void) = 0;

protected:

	cOvgRenderTarget *m_target;

private:

	cOvgCmd(const cOvgCmd&);
	cOvgCmd& operator= (const cOvgCmd&);
};

class cOvgCmdFlush : public cOvgCmd
{
public:

	cOvgCmdFlush(cOvgRenderTarget *target) :
		cOvgCmd(target) { }

	virtual const char* Description(void) { return "Flush"; }

	virtual bool Execute(cEgl *egl)
	{
		if (!m_target->MakeCurrent(egl))
			return false;

		eglSwapBuffers(egl->display, m_target->surface);
		return true;
	}
};

class cOvgCmdReset : public cOvgCmd
{
public:

	cOvgCmdReset(bool cleanup = false) :
		cOvgCmd(0), m_cleanup(cleanup) { }

	virtual const char* Description(void) { return "Reset"; }

	virtual bool Execute(cEgl *egl)
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

class cOvgCmdCreatePixelBuffer : public cOvgCmd
{
public:

	cOvgCmdCreatePixelBuffer(cOvgRenderTarget *target) : cOvgCmd(target) { }

	virtual const char* Description(void) { return "CreatePixelBuffer"; }

	virtual bool Execute(cEgl *egl)
	{
		m_target->image = vgCreateImage(VG_sARGB_8888, m_target->width,
				m_target->height, VG_IMAGE_QUALITY_BETTER);

		if (m_target->image == VG_INVALID_HANDLE)
			ELOG("[OpenVG] failed to allocate %dpx x %dpx pixel buffer!",
					m_target->width, m_target->height);
		else
		{
			m_target->surface = eglCreatePbufferFromClientBuffer(egl->display,
					EGL_OPENVG_IMAGE, (EGLClientBuffer)m_target->image,
					egl->config, NULL);

			if (m_target->surface == EGL_NO_SURFACE)
			{
				ELOG("[EGL] failed to create pixel buffer surface: %s!",
						cEgl::errStr(eglGetError()));
				vgDestroyImage(m_target->image);
				m_target->image = VG_INVALID_HANDLE;
			}
			else
			{
				if (eglSurfaceAttrib(egl->display, m_target->surface,
						EGL_SWAP_BEHAVIOR, EGL_BUFFER_PRESERVED) == EGL_FALSE)
				{
					ELOG("[EGL] failed to set surface attributes!");
					eglDestroySurface(egl->display, m_target->surface);
					vgDestroyImage(m_target->image);
					m_target->image = VG_INVALID_HANDLE;
				}
			}
		}
		m_target->initialized = true;
		return true;
	}
};

class cOvgCmdDestroySurface : public cOvgCmd
{
public:

	cOvgCmdDestroySurface(cOvgRenderTarget *target) : cOvgCmd(target) { }

	virtual const char* Description(void) { return "DestroySurface"; }

	virtual bool Execute(cEgl *egl)
	{
		if (!cOvgRenderTarget::MakeDefault(egl))
			return false;

		// only destroy pixel buffer surfaces
		if (m_target->image != VG_INVALID_HANDLE)
		{
			if (eglDestroySurface(egl->display, m_target->surface) == EGL_FALSE)
				ELOG("[EGL] failed to destroy surface: %s!",
						cEgl::errStr(eglGetError()));

			vgDestroyImage(m_target->image);
		}
		delete m_target;
		return true;
	}
};

class cOvgCmdClear : public cOvgCmd
{
public:

	cOvgCmdClear(cOvgRenderTarget *target, tColor color = clrTransparent) :
		cOvgCmd(target), m_color(color) { }

	virtual const char* Description(void) { return "Clear"; }

	virtual bool Execute(cEgl *egl)
	{
		if (!m_target->MakeCurrent(egl))
			return false;

		VGfloat color[4] = {
				(m_color >> 16 & 0xff) / 255.0f,
				(m_color >>  8 & 0xff) / 255.0f,
				(m_color       & 0xff) / 255.0f,
				(m_color >> 24 & 0xff) / 255.0f
		};

	    vgSetfv(VG_CLEAR_COLOR, 4, color);
	    vgClear(0, 0, m_target->width, m_target->height);
		return true;
	}

private:

	tColor m_color;
};

class cOvgCmdSaveRegion : public cOvgCmd
{
public:

	cOvgCmdSaveRegion(cOvgRenderTarget *target, cOvgSavedRegion *savedRegion,
			int x, int y, int w, int h) : cOvgCmd(target),
			m_x(x), m_y(y), m_w(w), m_h(h), m_savedRegion(savedRegion)
	{ }

	virtual const char* Description(void) { return "SaveRegion"; }

	virtual bool Execute(cEgl *egl)
	{
		if (!m_target->MakeCurrent(egl))
			return false;

		if (m_savedRegion->image != VG_INVALID_HANDLE)
			vgDestroyImage(m_savedRegion->image);

		if (m_w && m_h)
		{
			m_savedRegion->image = vgCreateImage(VG_sARGB_8888,
					m_w, m_h, VG_IMAGE_QUALITY_BETTER);

			if (m_savedRegion->image == VG_INVALID_HANDLE)
			{
				ELOG("failed to allocate image!");
				return false;
			}

			m_savedRegion->rect.Set(m_x, m_y, m_w, m_h);
			vgGetPixels(m_savedRegion->image, 0, 0, m_savedRegion->rect.X(),
					m_target->height - m_savedRegion->rect.Bottom() - 1,
					m_savedRegion->rect.Width(), m_savedRegion->rect.Height());
		}
		return true;
	}

private:

	int m_x;
	int m_y;
	int m_w;
	int m_h;
	cOvgSavedRegion *m_savedRegion;
};

class cOvgCmdRestoreRegion : public cOvgCmd
{
public:

	cOvgCmdRestoreRegion(cOvgRenderTarget *target, cOvgSavedRegion *savedRegion)
		: cOvgCmd(target), m_savedRegion(savedRegion) { }

	virtual const char* Description(void) { return "RestoreRegion"; }

	virtual bool Execute(cEgl *egl)
	{
		if (!m_target->MakeCurrent(egl))
			return false;

		if (m_savedRegion && m_savedRegion->image != VG_INVALID_HANDLE)
			vgSetPixels(m_savedRegion->rect.X(),
					m_target->height - m_savedRegion->rect.Bottom() - 1,
					m_savedRegion->image, 0, 0, m_savedRegion->rect.Width(),
					m_savedRegion->rect.Height());

		return true;
	}

private:

	cOvgSavedRegion *m_savedRegion;
};

class cOvgCmdDropRegion : public cOvgCmd
{
public:

	cOvgCmdDropRegion(cOvgSavedRegion *savedRegion) :
		cOvgCmd(0), m_savedRegion(savedRegion) { }

	virtual const char* Description(void) { return "DropRegion"; }

	virtual bool Execute(cEgl *egl)
	{
		if (m_savedRegion)
		{
			if (m_savedRegion->image != VG_INVALID_HANDLE)
				vgDestroyImage(m_savedRegion->image);

			delete m_savedRegion;
		}
		return true;
	}

private:

	cOvgSavedRegion *m_savedRegion;
};

class cOvgCmdDrawPixel : public cOvgCmd
{
public:

	cOvgCmdDrawPixel(cOvgRenderTarget *target, int x, int y, tColor color,
			bool alphablend) :
		cOvgCmd(target), m_x(x), m_y(y), m_color(color),
		m_alphablend(alphablend) { }

	virtual const char* Description(void) { return "DrawPixel"; }

	virtual bool Execute(cEgl *egl)
	{
		if (!m_target->MakeCurrent(egl))
			return false;

		if (m_alphablend)
		{
			tColor dstPixel;
			vgReadPixels(&dstPixel, 0, VG_sARGB_8888,
					m_x, m_target->height - 1 - m_y, 1, 1);

			m_color = AlphaBlend(m_color, dstPixel);
		}
		vgWritePixels(&m_color, 0, VG_sARGB_8888,
				m_x, m_target->height - 1 - m_y, 1, 1);
		return true;
	}

private:

	int m_x;
	int m_y;
	tColor m_color;
	bool m_alphablend;
};

class cOvgCmdDrawRectangle : public cOvgCmd
{
public:

	cOvgCmdDrawRectangle(cOvgRenderTarget *target,
			int x, int y, int w, int h, tColor color) :
		cOvgCmd(target), m_x(x), m_y(y), m_w(w), m_h(h), m_color(color) { }

	virtual const char* Description(void) { return "DrawRectangle"; }

	virtual bool Execute(cEgl *egl)
	{
		if (!m_target->MakeCurrent(egl))
			return false;

		vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
		vgSeti(VG_BLEND_MODE, VG_BLEND_SRC);

		vgLoadIdentity();
		vgTranslate(m_x, m_target->height - m_h - m_y);
		vgScale(m_w, m_h);

		cOvgPaintBox::SetColor(m_color);
		cOvgPaintBox::Draw(cOvgPaintBox::Rect());
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

	cOvgCmdDrawEllipse(cOvgRenderTarget *target,
			int x, int y, int w, int h, tColor color, int quadrants) :
		cOvgCmd(target), m_x(x), m_y(y), m_w(w), m_h(h), m_quadrants(quadrants),
		m_color(color) { }

	virtual const char* Description(void) { return "DrawEllipse"; }

	virtual bool Execute(cEgl *egl)
	{
		if (!m_target->MakeCurrent(egl))
			return false;

		vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
		vgSeti(VG_BLEND_MODE, VG_BLEND_SRC);

		vgLoadIdentity();
		vgTranslate(m_x, m_target->height - m_h - m_y);
		vgScale(m_w, m_h);

		cOvgPaintBox::SetColor(m_color);
		cOvgPaintBox::Draw(cOvgPaintBox::Ellipse(m_quadrants));
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

	cOvgCmdDrawSlope(cOvgRenderTarget *target,
			int x, int y, int w, int h, tColor color, int type) :
		cOvgCmd(target), m_x(x), m_y(y), m_w(w), m_h(h), m_type(type),
		m_color(color) { }

	virtual const char* Description(void) { return "DrawSlope"; }

	virtual bool Execute(cEgl *egl)
	{
		if (!m_target->MakeCurrent(egl))
			return false;

		vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
		vgSeti(VG_BLEND_MODE, VG_BLEND_SRC);

		vgLoadIdentity();
		vgTranslate(m_x, m_target->height - m_h - m_y);
		vgScale(m_w, m_h);

		cOvgPaintBox::SetColor(m_color);
		cOvgPaintBox::Draw(cOvgPaintBox::Slope(m_type));
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

	cOvgCmdDrawText(cOvgRenderTarget *target,
			int x, int y, unsigned int *symbols, cString *fontName,
			int fontSize, tColor colorFg,	tColor colorBg, int w, int h,
			int alignment) :
		cOvgCmd(target), m_x(x), m_y(y), m_w(w), m_h(h),
		m_symbols(symbols), m_fontName(fontName), m_fontSize(fontSize),
		m_colorFg(colorFg), m_colorBg(colorBg),	m_alignment(alignment) { }

	virtual ~cOvgCmdDrawText()
	{
		free(m_symbols);
		delete m_fontName;
	}

	virtual const char* Description(void) { return "DrawText"; }

	virtual bool Execute(cEgl *egl)
	{
		if (!m_target->MakeCurrent(egl))
			return false;

		cOvgFont *font = cOvgFont::Get(*m_fontName);
		if (!font)
			return false;

		cOvgString *string = new cOvgString(m_symbols, font);

		VGfloat offsetX = 0;
		VGfloat offsetY = 0;
		VGfloat width = string->Width() * (VGfloat)m_fontSize;
		VGfloat height = string->Height() * (VGfloat)m_fontSize;
		VGfloat descender = string->Descender() * (VGfloat)m_fontSize;

		if (m_w)
		{
			if (m_alignment & taLeft)
			{
				if (m_alignment & taBorder)
					offsetX += max(height / TEXT_ALIGN_BORDER, 1.0f);
			}
			else if (m_alignment & taRight)
			{
				if (width < m_w)
					offsetX += m_w - width;
				if (m_alignment & taBorder)
					offsetX -= max(height / TEXT_ALIGN_BORDER, 1.0f);
			}
			else
			{
				if (width < m_w)
					offsetX += (m_w - width) / 2;
			}
		}
		if (m_h)
		{
			if (m_alignment & taTop) { }
			else if (m_alignment & taBottom)
			{
				if (height < m_h)
					offsetY += m_h - height;
			}
			else
			{
				if (height < m_h)
					offsetY += (m_h - height) / 2;
			}
		}

		vgSeti(VG_BLEND_MODE, VG_BLEND_SRC);
		vgSeti(VG_MATRIX_MODE, VG_MATRIX_GLYPH_USER_TO_SURFACE);

		// some magic offset to conform with VDR's text rendering
		offsetY -= 0.06f * m_fontSize;

		vgLoadIdentity();
		vgTranslate(m_x + offsetX,
				m_target->height - m_y - m_fontSize - offsetY + 1);
		vgScale(m_fontSize, m_fontSize);

		VGfloat origin[2] = { 0.0f, 0.0f };
		vgSetfv(VG_GLYPH_ORIGIN, 2, origin);

		cOvgPaintBox::SetScissoring(
				m_w ? m_x : m_x + floor(offsetX),
				m_h ? m_target->height - m_y - m_h : m_target->height - m_y -
						m_fontSize - floor(descender) + 1,
				m_w ? m_w : floor(width) + 1,
				m_h ? m_h : m_fontSize + floor(descender) - 1);

		if (m_colorBg != clrTransparent)
		{
			VGfloat color[4] = {
					(m_colorBg >> 16 & 0xff) / 255.0f,
					(m_colorBg >>  8 & 0xff) / 255.0f,
					(m_colorBg       & 0xff) / 255.0f,
					(m_colorBg >> 24 & 0xff) / 255.0f
			};
		    vgSetfv(VG_CLEAR_COLOR, 4, color);
		    vgClear(0, 0, m_target->width, m_target->height);
		}

		if (string->Length())
		{
			cOvgPaintBox::SetColor(m_colorFg);
			cOvgPaintBox::Draw(string);
		}

		cOvgPaintBox::SetScissoring();
		delete string;
		return true;
	}

private:

	int m_x;
	int m_y;
	int m_w;
	int m_h;
	unsigned int *m_symbols;
	cString *m_fontName;
	int m_fontSize;
	tColor m_colorFg;
	tColor m_colorBg;
	int m_alignment;
};

class cOvgCmdRenderPixels : public cOvgCmd
{
public:

	cOvgCmdRenderPixels(cOvgRenderTarget *target, cOvgRenderTarget *source,
			int dx, int dy, int sx, int sy, int w, int h, int alpha) :
		cOvgCmd(target), m_source(source), m_dx(dx), m_dy(dy),
		m_sx(sx), m_sy(sy), m_w(w), m_h(h), m_alpha(alpha) { }

	virtual const char* Description(void) { return "RenderPixels"; }

	virtual bool Execute(cEgl *egl)
	{
		if (!m_target->MakeCurrent(egl))
			return false;

		cOvgPaintBox::SetAlpha(m_alpha);
		cOvgPaintBox::SetScissoring(m_dx,
				m_target->height - m_dy - m_h, m_w, m_h);

		vgSeti(VG_BLEND_MODE, VG_BLEND_SRC_OVER);
		vgSeti(VG_MATRIX_MODE, VG_MATRIX_IMAGE_USER_TO_SURFACE);
		vgSeti(VG_IMAGE_MODE, VG_DRAW_IMAGE_NORMAL);
		vgSeti(VG_IMAGE_QUALITY, VG_IMAGE_QUALITY_BETTER);

		vgLoadIdentity();
		vgTranslate(m_dx - m_sx,
				m_target->height - m_source->height - m_dy + m_sy);

		vgDrawImage(m_source->image);

		cOvgPaintBox::SetAlpha(255);
		cOvgPaintBox::SetScissoring();
		return true;
	}

private:

	cOvgRenderTarget *m_source;
	int m_dx;
	int m_dy;
	int m_sx;
	int m_sy;
	int m_w;
	int m_h;
	int m_alpha;
};

class cOvgCmdRenderPattern : public cOvgCmd
{
public:

	cOvgCmdRenderPattern(cOvgRenderTarget *target, cOvgRenderTarget *source,
			int dx, int dy, int sx, int sy, int w, int h, int alpha) :
		cOvgCmd(target), m_source(source), m_dx(dx), m_dy(dy),
		m_sx(sx), m_sy(sy), m_w(w), m_h(h), m_alpha(alpha) { }

	virtual const char* Description(void) { return "RenderPattern"; }

	virtual bool Execute(cEgl *egl)
	{
		if (!m_target->MakeCurrent(egl))
			return false;

		int sx = m_dx - m_sx;
		int sy = m_target->height - m_dy - m_source->height  + m_sy;
		int dy = m_target->height - m_dy - m_h;

		while (sx > m_dx)
			sx -= m_source->width;

		while (sy > dy)
			sy -= m_source->height;

		int nx = (m_dx + m_w - sx) / m_source->width;
		if ((m_dx + m_w - sx) % m_source->width) nx++;

		int ny = (dy + m_h - sy) / m_source->height;
		if ((dy + m_h - sy) % m_source->height) ny++;

		cOvgPaintBox::SetAlpha(m_alpha);
		cOvgPaintBox::SetScissoring(m_dx, dy, m_w, m_h);

		VGPath path = vgCreatePath(VG_PATH_FORMAT_STANDARD,
				VG_PATH_DATATYPE_F, 1.0f, 0.0f, 0, 0,
				VG_PATH_CAPABILITY_TRANSFORM_TO);

		vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
		vgLoadIdentity();
		vgScale(m_source->width * nx, m_source->height * ny);
		vgTransformPath(path, cOvgPaintBox::Rect());

		cOvgPaintBox::SetPattern(m_source->image);

		vgSeti(VG_MATRIX_MODE, VG_MATRIX_PATH_USER_TO_SURFACE);
		vgLoadIdentity();
		vgTranslate(sx, sy);

		cOvgPaintBox::Draw(path);

		cOvgPaintBox::SetAlpha(255);
		cOvgPaintBox::SetScissoring();
		cOvgPaintBox::SetPattern();
		vgDestroyPath(path);
		return true;
	}

private:

	cOvgRenderTarget *m_source;
	int m_dx;
	int m_dy;
	int m_sx;
	int m_sy;
	int m_w;
	int m_h;
	int m_alpha;
};

class cOvgCmdCopyPixels : public cOvgCmd
{
public:

	cOvgCmdCopyPixels(cOvgRenderTarget *target, cOvgRenderTarget *source,
			int dx, int dy, int sx, int sy, int w, int h) :
		cOvgCmd(target), m_source(source),
		m_dx(dx), m_dy(dy), m_sx(sx), m_sy(sy), m_w(w), m_h(h) { }

	virtual const char* Description(void) { return "CopyPixels"; }

	virtual bool Execute(cEgl *egl)
	{
		if (!m_target->MakeCurrent(egl))
			return false;

		vgSetPixels(m_dx, m_target->height - m_h - m_dy, m_source->image,
				m_sx, m_source->height - m_h - m_sy, m_w, m_h);
		return true;
	}

private:

	cOvgRenderTarget *m_source;
	int m_dx;
	int m_dy;
	int m_sx;
	int m_sy;
	int m_w;
	int m_h;
};

class cOvgCmdMovePixels : public cOvgCmd
{
public:

	cOvgCmdMovePixels(cOvgRenderTarget *target,
			int dx, int dy, int sx, int sy, int w, int h) :
		cOvgCmd(target),
		m_dx(dx), m_dy(dy), m_sx(sx), m_sy(sy), m_w(w), m_h(h) { }

	virtual const char* Description(void) { return "MovePixels"; }

	virtual bool Execute(cEgl *egl)
	{
		if (!m_target->MakeCurrent(egl))
			return false;

		vgCopyPixels(m_dx, m_target->height - m_h - m_dy,
				m_sx, m_target->height - m_h - m_sy, m_w, m_h);
		return true;
	}

private:

	int m_dx;
	int m_dy;
	int m_sx;
	int m_sy;
	int m_w;
	int m_h;
};

class cOvgCmdStoreImage : public cOvgCmd
{
public:

	cOvgCmdStoreImage(tOvgImageRef *image, int w, int h, tColor *argb) :
		cOvgCmd(0), m_image(image), m_w(w), m_h(h), m_argb(argb) { }

	virtual ~cOvgCmdStoreImage()
	{
		free(m_argb);
	}

	virtual const char* Description(void) { return "StoreImage"; }

	virtual bool Execute(cEgl *egl)
	{
		m_image->image = vgCreateImage(VG_sARGB_8888, m_w, m_h,
				VG_IMAGE_QUALITY_BETTER);

		if (m_image->image != VG_INVALID_HANDLE)
			vgImageSubData(m_image->image, m_argb, m_w * sizeof(tColor),
					VG_sARGB_8888, 0, 0, m_w, m_h);
		else
		{
			m_image->used = false;
			ELOG("[OpenVG] failed to allocate %dpx x %dpx image!", m_w, m_h);
		}
		return true;
	}

private:

	tOvgImageRef *m_image;
	int m_w;
	int m_h;
	tColor *m_argb;
};

class cOvgCmdDropImage : public cOvgCmd
{
public:

	cOvgCmdDropImage(tOvgImageRef *image) :
		cOvgCmd(0), m_image(image) { }

	virtual const char* Description(void) { return "DropImage"; }

	virtual bool Execute(cEgl *egl)
	{
		if (m_image->image != VG_INVALID_HANDLE)
			vgDestroyImage(m_image->image);

		m_image->used = false;
		return true;
	}

private:

	tOvgImageRef *m_image;
};

class cOvgCmdDrawImage : public cOvgCmd
{
public:

	cOvgCmdDrawImage(cOvgRenderTarget *target, VGImage *image, int x, int y) :
		cOvgCmd(target), m_image(image), m_x(x), m_y(y) { }

	virtual const char* Description(void) { return "DrawImage"; }

	virtual bool Execute(cEgl *egl)
	{
		if (!m_target->MakeCurrent(egl))
			return false;

		VGint height = vgGetParameteri(*m_image, VG_IMAGE_HEIGHT);

		vgSeti(VG_MATRIX_MODE, VG_MATRIX_IMAGE_USER_TO_SURFACE);
		vgSeti(VG_IMAGE_MODE, VG_DRAW_IMAGE_NORMAL);
		vgSeti(VG_IMAGE_QUALITY, VG_IMAGE_QUALITY_BETTER);
		vgSeti(VG_BLEND_MODE, VG_BLEND_SRC);

		vgLoadIdentity();
		vgTranslate(m_x, m_target->height - height - m_y);

		vgDrawImage(*m_image);
		return true;
	}

protected:

	VGImage *m_image;
	int m_x;
	int m_y;
};

class cOvgCmdDrawBitmap : public cOvgCmd
{
public:

	cOvgCmdDrawBitmap(cOvgRenderTarget *target,
			int x, int y, int w, int h, tColor *argb,
			bool overlay = false, double scaleX = 1.0f, double scaleY = 1.0f) :
		cOvgCmd(target), m_x(x), m_y(y), m_w(w), m_h(h), m_argb(argb),
		m_overlay(overlay), m_scaleX(scaleX), m_scaleY(scaleY) { }

	virtual ~cOvgCmdDrawBitmap()
	{
		free(m_argb);
	}

	virtual const char* Description(void) { return "DrawBitmap"; }

	virtual bool Execute(cEgl *egl)
	{
		int w = min(m_w, vgGeti(VG_MAX_IMAGE_WIDTH));
		int h = min(m_h, vgGeti(VG_MAX_IMAGE_HEIGHT));

		if (w <= 0 || h <= 0)
			return true;

		if (!m_target->MakeCurrent(egl))
			return false;

		vgSeti(VG_MATRIX_MODE, VG_MATRIX_IMAGE_USER_TO_SURFACE);
		vgSeti(VG_IMAGE_MODE, VG_DRAW_IMAGE_NORMAL);
		vgSeti(VG_IMAGE_QUALITY, VG_IMAGE_QUALITY_BETTER);
		vgSeti(VG_BLEND_MODE, m_overlay ? VG_BLEND_SRC_OVER : VG_BLEND_SRC);

		vgLoadIdentity();
		vgScale(1.0f, -1.0f);
		vgTranslate(m_x, m_y - m_target->height);
		vgScale(m_scaleX, m_scaleY);

		VGImage image = vgCreateImage(VG_sARGB_8888, w, h,
				VG_IMAGE_QUALITY_BETTER);

		if (image == VG_INVALID_HANDLE)
		{
			ELOG("failed to allocate image!");
			return false;
		}

		vgImageSubData(image, m_argb, m_w * sizeof(tColor),
				VG_sARGB_8888, 0, 0, w, h);

		vgDrawImage(image);

		vgDestroyImage(image);
		return true;
	}

protected:

	int m_x;
	int m_y;
	int m_w;
	int m_h;
	tColor *m_argb;
	bool m_overlay;
	double m_scaleX;
	double m_scaleY;
};

/* ------------------------------------------------------------------------- */

#define OVG_MAX_OSDIMAGES 256
#define OVG_CMDQUEUE_SIZE 2048

class cOvgThread : public cThread
{
public:

	cOvgThread(int layer) :	cThread("ovgthread"),
		m_wait(new cCondWait()), m_stalled(false), m_layer(layer)
	{
		for (int i = 0; i < OVG_MAX_OSDIMAGES; i++)
			m_images[i].used = false;

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
		while (m_stalled)
			cCondWait::SleepMs(10);

		Lock();
		m_commands.push(cmd);
		Unlock();

		if (m_commands.size() > OVG_CMDQUEUE_SIZE)
		{
			m_stalled = true;
			ILOG("[OpenVG] command queue stalled!");
		}

		if (signal || m_stalled)
			m_wait->Signal();
	}

	virtual int StoreImageData(const cImage &image)
	{
		if (image.Width() > m_maxImageSize.Width() ||
				image.Height() > m_maxImageSize.Height())
		{
			DLOG("[OpenVG] cannot store image of %dpx x %dpx "
					"(maximum size is %dpx x %dpx) - falling back to "
					"cOsdProvider::StoreImageData()",
					image.Width(), image.Height(),
					m_maxImageSize.Width(), m_maxImageSize.Height());
			return 0;
		}

		int imageHandle = GetFreeImageHandle();
		if (imageHandle)
		{
			tColor *argb = MALLOC(tColor, image.Width() * image.Height());
			if (!argb)
			{
				FreeImageHandle(imageHandle);
				imageHandle = 0;
			}
			else
			{
				memcpy(argb, image.Data(),
						sizeof(tColor) * image.Width() * image.Height());

				tOvgImageRef *imageRef = GetImageRef(imageHandle);
				DoCmd(new cOvgCmdStoreImage(imageRef,
						image.Width(), image.Height(), argb), true);

				cTimeMs timer(5000);
				while (imageRef->used && imageRef->image == VG_INVALID_HANDLE
						&& !timer.TimedOut())
					cCondWait::SleepMs(2);

				if (imageRef->image == VG_INVALID_HANDLE)
				{
					ELOG("failed to store OSD image! (%s)",	timer.TimedOut() ?
							"timed out" : "allocation failed");
					DropImageData(imageHandle);
					imageHandle = 0;
				}
			}
		}
		return imageHandle;
	}

	virtual void DropImageData(int imageHandle)
	{
		DoCmd(new cOvgCmdDropImage(GetImageRef(imageHandle)));
	}

	virtual const cSize &MaxImageSize(void) const
	{
		return m_maxImageSize;
	}

	tOvgImageRef *GetImageRef(int imageHandle)
	{
		int i = -imageHandle - 1;
		if (0 <= i && i < OVG_MAX_OSDIMAGES)
			return &m_images[i];

		return 0;
	}

protected:

	virtual int GetFreeImageHandle(void)
	{
		Lock();
		int imageHandle = 0;
		for (int i = 0; i < OVG_MAX_OSDIMAGES && !imageHandle; i++)
			if (!m_images[i].used)
			{
				m_images[i].used = true;
				m_images[i].image = VG_INVALID_HANDLE;
				imageHandle = -i - 1;
			}
		Unlock();
		return imageHandle;
	}

	virtual void FreeImageHandle(int imageHandle)
	{
		int i = -imageHandle - 1;
		if (i >= 0 && i < OVG_MAX_OSDIMAGES)
			m_images[i].used = false;
	}

	virtual void Action(void)
	{
		DLOG("cOvgThread() thread started");

		cEgl egl;
		egl.display = eglGetDisplay(EGL_DEFAULT_DISPLAY);

		if (egl.display == EGL_NO_DISPLAY)
			ELOG("[EGL] failed to get display connection!");

		if (eglInitialize(egl.display, NULL, NULL) == EGL_FALSE)
			ELOG("[EGL] failed to init display connection!");

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

		// get an appropriate EGL frame buffer configuration
		if (eglChooseConfig(egl.display, attr, &egl.config, 1, &egl.nConfig)
				== EGL_FALSE)
			ELOG("[EGL] failed to get frame buffer config!");

		// create an EGL rendering context
		egl.context = eglCreateContext(egl.display, egl.config, NULL, NULL);
		if (egl.context == EGL_NO_CONTEXT)
			ELOG("[EGL] failed to create rendering context!");

		while (Running())
		{
			DISPMANX_DISPLAY_HANDLE_T display = vc_dispmanx_display_open(
					cRpiDisplay::GetId());
			DISPMANX_UPDATE_HANDLE_T update = vc_dispmanx_update_start(0);

			cRpiDisplay::GetSize(egl.window.width, egl.window.height);

			VC_RECT_T srcRect = { 0, 0,
					egl.window.width << 16, egl.window.height << 16 };
			VC_RECT_T dstRect = { 0, 0, egl.window.width, egl.window.height };

			egl.window.element = vc_dispmanx_element_add(
					update, display, m_layer, &dstRect, 0, &srcRect,
					DISPMANX_PROTECTION_NONE, 0, 0, (DISPMANX_TRANSFORM_T)0);

			vc_dispmanx_update_submit_sync(update);

			// create background surface
			const EGLint attr[] = {
				EGL_RENDER_BUFFER, EGL_BACK_BUFFER,
				EGL_NONE
			};

			egl.surface = eglCreateWindowSurface(egl.display, egl.config,
					&egl.window, attr);
			if (egl.surface == EGL_NO_SURFACE)
				ELOG("[EGL] failed to create window surface: %s!",
						cEgl::errStr(eglGetError()));

			if (eglSurfaceAttrib(egl.display, egl.surface,
					EGL_SWAP_BEHAVIOR, EGL_BUFFER_PRESERVED) == EGL_FALSE)
				ELOG("[EGL] failed to set surface attributes: %s!",
						cEgl::errStr(eglGetError()));

			if (eglMakeCurrent(egl.display, egl.surface, egl.surface,
					egl.context) ==	EGL_FALSE)
				ELOG("failed to connect context to surface: %s!",
						cEgl::errStr(eglGetError()));

			egl.currentSurface = egl.surface;

			m_maxImageSize.Set(vgGeti(VG_MAX_IMAGE_WIDTH),
					vgGeti(VG_MAX_IMAGE_HEIGHT));

			float color[4] = {0.0f, 0.0f, 0.0f, 0.0f};
			vgSetfv(VG_CLEAR_COLOR, 4, color);
			vgClear(0, 0, egl.window.width, egl.window.height);

			bool reset = false;
			while (!reset)
			{
				if (m_commands.empty())
					m_wait->Wait(20);
				else
				{
					Lock();
					cOvgCmd* cmd = m_commands.front();
					m_commands.pop();
					Unlock();

					reset = cmd ? !cmd->Execute(&egl) : true;

					VGErrorCode err = vgGetError();
					if (cmd && err != VG_NO_ERROR)
						ELOG("[OpenVG] %s error: %s",
								cmd->Description(), errStr(err));

					//ELOG("[OpenVG] %s", cmd->Description());
					delete cmd;

					if (m_stalled && m_commands.size() < OVG_CMDQUEUE_SIZE / 2)
						m_stalled = false;
				}
			}

			if (eglDestroySurface(egl.display, egl.surface) == EGL_FALSE)
				ELOG("[EGL] failed to destroy surface: %s!",
						cEgl::errStr(eglGetError()));

			vc_dispmanx_element_remove(update, egl.window.element);
			vc_dispmanx_display_close(display);

			DLOG("cOvgThread() thread reset");
		}

		for (int i = 0; i < OVG_MAX_OSDIMAGES; i++)
			if (m_images[i].used)
				vgDestroyImage(m_images[i].image);

		cOvgFont::CleanUp();
		cOvgPaintBox::CleanUp();
		vgFinish();

		eglDestroyContext(egl.display, egl.context);
		eglTerminate(egl.display);

		DLOG("cOvgThread() thread ended");
	}

private:

	static const char* errStr(VGErrorCode error)
	{
		return
			error == VG_NO_ERROR                       ? "no error"            :
			error == VG_BAD_HANDLE_ERROR               ? "bad handle"          :
			error == VG_ILLEGAL_ARGUMENT_ERROR         ? "illegal argument"    :
			error == VG_OUT_OF_MEMORY_ERROR            ? "out of memory"       :
			error == VG_PATH_CAPABILITY_ERROR          ? "path capability"     :
			error == VG_UNSUPPORTED_IMAGE_FORMAT_ERROR ? "unsup. image format" :
			error == VG_UNSUPPORTED_PATH_FORMAT_ERROR  ? "unsup. path format"  :
			error == VG_IMAGE_IN_USE_ERROR             ? "image in use"        :
			error == VG_NO_CONTEXT_ERROR               ? "no context"          :
						"unknown error";
	}

	std::queue<cOvgCmd*> m_commands;
	cCondWait *m_wait;
	bool m_stalled;
	int m_layer;

	tOvgImageRef m_images[OVG_MAX_OSDIMAGES];

	cSize m_maxImageSize;
};

/* ------------------------------------------------------------------------- */

class cOvgPixmap : public cPixmap
{
public:

	cOvgPixmap(int Layer, cOvgThread *ovg, cOvgRenderTarget *buffer,
			const cRect &ViewPort, const cRect &DrawPort) :
		cPixmap(Layer, ViewPort, DrawPort),
		m_ovg(ovg),
		m_buffer(buffer),
		m_savedRegion(new cOvgSavedRegion()),
		m_dirty(false)
	{ }

	virtual ~cOvgPixmap()
	{
		m_ovg->DoCmd(new cOvgCmdDropRegion(m_savedRegion));
		m_ovg->DoCmd(new cOvgCmdDestroySurface(m_buffer));
	}

	virtual void SetAlpha(int Alpha)
	{
		Alpha = constrain(Alpha, ALPHA_TRANSPARENT, ALPHA_OPAQUE);
		if (Alpha != cPixmap::Alpha())
		{
			cPixmap::SetAlpha(Alpha);
			SetDirty();
		}
	}

	virtual void SetTile(bool Tile)
	{
		cPixmap::SetTile(Tile);
		SetDirty();
	}

	virtual void SetViewPort(const cRect &Rect)
	{
		cPixmap::SetViewPort(Rect);
		SetDirty();
	}

	virtual void SetDrawPortPoint(const cPoint &Point, bool Dirty = true)
	{
		cPixmap::SetDrawPortPoint(Point, Dirty);
		if (Dirty)
			SetDirty();
	}

	virtual void Clear(void)
	{
		LOCK_PIXMAPS;
		m_ovg->DoCmd(new cOvgCmdClear(m_buffer));
		SetDirty();
		MarkDrawPortDirty(DrawPort());
	}

	virtual void Fill(tColor Color)
	{
		LOCK_PIXMAPS;
		m_ovg->DoCmd(new cOvgCmdClear(m_buffer, Color));
		SetDirty();
		MarkDrawPortDirty(DrawPort());
	}

	virtual void DrawImage(const cPoint &Point, const cImage &Image)
	{
		LOCK_PIXMAPS;
		tColor *argb = MALLOC(tColor, Image.Width() * Image.Height());
		if (!argb)
			return;

		memcpy(argb, Image.Data(),
				sizeof(tColor) * Image.Width() * Image.Height());

		m_ovg->DoCmd(new cOvgCmdDrawBitmap(m_buffer, Point.X(), Point.Y(),
				Image.Width(), Image.Height(), argb, false));

		SetDirty();
		MarkDrawPortDirty(cRect(Point, cSize(Image.Width(),
				Image.Height())).Intersected(DrawPort().Size()));
	}

	virtual void DrawImage(const cPoint &Point, int ImageHandle)
	{
		if (ImageHandle < 0 && m_ovg->GetImageRef(ImageHandle))
			m_ovg->DoCmd(new cOvgCmdDrawImage(m_buffer,
					&m_ovg->GetImageRef(ImageHandle)->image,
					Point.X(), Point.Y()));
		else
			if (cRpiOsdProvider::GetImageData(ImageHandle))
				DrawImage(Point, *cRpiOsdProvider::GetImageData(ImageHandle));

		SetDirty();
	}

	virtual void DrawPixel(const cPoint &Point, tColor Color)
	{
		LOCK_PIXMAPS;
		m_ovg->DoCmd(new cOvgCmdDrawPixel(m_buffer, Point.X(), Point.Y(),
				Color, Layer() == 0 && !IS_OPAQUE(Color)));

		SetDirty();
		MarkDrawPortDirty(Point);
	}

	virtual void DrawBitmap(const cPoint &Point, const cBitmap &Bitmap,
			tColor ColorFg = 0, tColor ColorBg = 0, bool Overlay = false)
	{
		LOCK_PIXMAPS;
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
						(index == 0 ? ColorBg :	index == 1 ? ColorFg :
								Bitmap.Color(index)) : Bitmap.Color(index));
			}

		m_ovg->DoCmd(new cOvgCmdDrawBitmap(m_buffer, Point.X(), Point.Y(),
				Bitmap.Width(), Bitmap.Height(), argb, Overlay));

		SetDirty();
		MarkDrawPortDirty(cRect(Point, cSize(Bitmap.Width(),
				Bitmap.Height())).Intersected(DrawPort().Size()));
	}

	virtual void DrawScaledBitmap(const cPoint &Point, const cBitmap &Bitmap,
			double FactorX, double FactorY, bool AntiAlias = false)
	{
		LOCK_PIXMAPS;
		tColor *argb = MALLOC(tColor, Bitmap.Width() * Bitmap.Height());
		if (!argb)
			return;

		tColor *p = argb;
		for (int py = 0; py < Bitmap.Height(); py++)
			for (int px = 0; px < Bitmap.Width(); px++)
				*p++ = Bitmap.Color(*Bitmap.Data(px, py));

		m_ovg->DoCmd(new cOvgCmdDrawBitmap(m_buffer, Point.X(), Point.Y(),
				Bitmap.Width(), Bitmap.Height(), argb, false,
				FactorX, FactorY));

		SetDirty();
		MarkDrawPortDirty(cRect(Point, cSize(
				int(round(Bitmap.Width() * FactorX)) + 1,
				int(round(Bitmap.Height() * FactorY)) + 1)).Intersected(
						DrawPort().Size()));
	}

	virtual void DrawText(const cPoint &Point, const char *s, tColor ColorFg,
			tColor ColorBg, const cFont *Font, int Width = 0, int Height = 0,
			int Alignment = taDefault)
	{
		LOCK_PIXMAPS;
		int len = s ? Utf8StrLen(s) : 0;
		unsigned int *symbols = MALLOC(unsigned int, len + 1);
		if (!symbols)
			return;

		if (len)
			Utf8ToArray(s, symbols, len + 1);
		else
			symbols[0] = 0;

		m_ovg->DoCmd(new cOvgCmdDrawText(m_buffer, Point.X(), Point.Y(),
				symbols, new cString(Font->FontName()), Font->Size(),
				ColorFg, ColorBg, Width, Height, Alignment));

		SetDirty();
		MarkDrawPortDirty(cRect(Point.X(), Point.Y(),
				Width ? Width : DrawPort().Width() - Point.X(),
				Height ? Height : DrawPort().Height() - Point.Y()));
	}

	virtual void DrawRectangle(const cRect &Rect, tColor Color)
	{
		LOCK_PIXMAPS;
		m_ovg->DoCmd(new cOvgCmdDrawRectangle(m_buffer,
				Rect.X(), Rect.Y(),	Rect.Width(), Rect.Height(), Color));

		SetDirty();
		MarkDrawPortDirty(Rect);
	}

	virtual void DrawEllipse(const cRect &Rect, tColor Color, int Quadrants = 0)
	{
		LOCK_PIXMAPS;
		m_ovg->DoCmd(new cOvgCmdDrawEllipse(m_buffer,
				Rect.X(), Rect.Y(),	Rect.Width(), Rect.Height(),
				Color, Quadrants));

		SetDirty();
		MarkDrawPortDirty(Rect);
	}

	virtual void DrawSlope(const cRect &Rect, tColor Color, int Type)
	{
		LOCK_PIXMAPS;
		m_ovg->DoCmd(new cOvgCmdDrawSlope(m_buffer,
				Rect.X(), Rect.Y(),	Rect.Width(), Rect.Height(), Color, Type));

		SetDirty();
		MarkDrawPortDirty(Rect);
	}

	virtual void Render(const cPixmap *Pixmap, const cRect &Source,
			const cPoint &Dest)
	{
		LOCK_PIXMAPS;
		if (Pixmap->Alpha() == ALPHA_TRANSPARENT)
			return;

		if (const cOvgPixmap *pm = dynamic_cast<const cOvgPixmap *>(Pixmap))
		{
			m_ovg->DoCmd(new cOvgCmdRenderPixels(m_buffer, pm->m_buffer,
					Dest.X(), Dest.Y(), Source.X(), Source.Y(),
					Source.Width(), Source.Height(), pm->Alpha()));

			SetDirty();
			MarkDrawPortDirty(DrawPort());
		}
	}

	virtual void Copy(const cPixmap *Pixmap, const cRect &Source,
			const cPoint &Dest)
	{
		LOCK_PIXMAPS;
		if (const cOvgPixmap *pm = dynamic_cast<const cOvgPixmap *>(Pixmap))
		{
			m_ovg->DoCmd(new cOvgCmdCopyPixels(m_buffer, pm->m_buffer,
					Dest.X(), Dest.Y(), Source.X(), Source.Y(),
					Source.Width(), Source.Height()));

			SetDirty();
			MarkDrawPortDirty(DrawPort());
		}
	}

	virtual void Scroll(const cPoint &Dest, const cRect &Source, bool pan)
	{
		LOCK_PIXMAPS;
		cRect s;
		if (&Source == &cRect::Null)
			s = DrawPort().Shifted(-DrawPort().Point());
		else
			s = Source.Intersected(DrawPort().Size());

		if (Dest != s.Point())
		{
			m_ovg->DoCmd(new cOvgCmdMovePixels(m_buffer, Dest.X(), Dest.Y(),
					s.X(), s.Y(), s.Width(), s.Height()));

			if (pan)
				SetDrawPortPoint(DrawPort().Point().Shifted(s.Point() -	Dest),
						false);
			else
				MarkDrawPortDirty(Dest);
			SetDirty();
		}
	}

	virtual void Scroll(const cPoint &Dest, const cRect &Source = cRect::Null)
	{
		Scroll(Dest, Source, false);
	}

	virtual void Pan(const cPoint &Dest, const cRect &Source = cRect::Null)
	{
		Scroll(Dest, Source, true);
	}

	virtual void SaveRegion(const cRect &Source)
	{
		m_ovg->DoCmd(new cOvgCmdSaveRegion(m_buffer, m_savedRegion,
				Source.X(), Source.Y(), Source.Width(), Source.Height()));
	}

	virtual void RestoreRegion(void)
	{
		m_ovg->DoCmd(new cOvgCmdRestoreRegion(m_buffer, m_savedRegion));
		SetDirty();
	}

	virtual void CopyToTarget(cOvgRenderTarget *target, int left, int top)
	{
		LOCK_PIXMAPS;
		cRect d = ViewPort().Shifted(left, top);
		cPoint s = -DrawPort().Point();

		m_ovg->DoCmd(new cOvgCmdCopyPixels(target, m_buffer,
				d.X(), d.Y(), s.X(), s.Y(), d.Width(), d.Height()));

		SetDirty(false);
	}

	virtual void RenderToTarget(cOvgRenderTarget *target, int left, int top)
	{
		LOCK_PIXMAPS;
		cRect d = ViewPort().Shifted(left, top);
		cPoint s = -DrawPort().Point();

		if (Tile())
			m_ovg->DoCmd(new cOvgCmdRenderPattern(target, m_buffer,
					d.X(), d.Y(), s.X(), s.Y(), d.Width(), d.Height(),
					Alpha()));
		else
			m_ovg->DoCmd(new cOvgCmdRenderPixels(target, m_buffer,
					d.X(), d.Y(), s.X(), s.Y(), d.Width(), d.Height(),
					Alpha()));

		SetDirty(false);
	}

	virtual bool IsDirty(void) { return m_dirty; }
	virtual void SetDirty(bool dirty = true) { m_dirty = dirty; }

private:

	cOvgPixmap(const cOvgPixmap&);
	cOvgPixmap& operator= (const cOvgPixmap&);

	cOvgThread       *m_ovg;
	cOvgRenderTarget *m_buffer;
	cOvgSavedRegion  *m_savedRegion;

	bool m_dirty;
};

/* ------------------------------------------------------------------------- */

class cOvgOsd : public cOsd
{
public:

	cOvgOsd(int Left, int Top, uint Level, cOvgThread *ovg) :
		cOsd(Left, Top, Level),
		m_ovg(ovg),
		m_surface(new cOvgRenderTarget())
	{
		cTimeMs timer(10000);
		while (!m_ovg->MaxImageSize().Height() && !timer.TimedOut())
			cCondWait::SleepMs(100);
	}

	virtual ~cOvgOsd()
	{
		SetActive(false);
		m_ovg->DoCmd(new cOvgCmdDestroySurface(m_surface));
	}

	virtual eOsdError SetAreas(const tArea *Areas, int NumAreas)
	{
		cRect r;
		for (int i = 0; i < NumAreas; i++)
			r.Combine(cRect(Areas[i].x1, Areas[i].y1,
					Areas[i].Width(), Areas[i].Height()));

		tArea area = { r.Left(), r.Top(), r.Right(), r.Bottom(), 32 };

		for (int i = 0; i < m_pixmaps.Size(); i++)
			m_pixmaps[i] = NULL;

		return cOsd::SetAreas(&area, 1);
	}

	virtual const cSize &MaxPixmapSize(void) const
	{
		return m_ovg->MaxImageSize();
	}

	virtual cPixmap *CreatePixmap(int Layer, const cRect &ViewPort,
			const cRect &DrawPort = cRect::Null)
	{
		LOCK_PIXMAPS;
		int width = DrawPort.IsEmpty() ? ViewPort.Width() : DrawPort.Width();
		int height = DrawPort.IsEmpty() ? ViewPort.Height() : DrawPort.Height();

#if APIVERSNUM >= 20301
		if (width > m_ovg->MaxImageSize().Width() ||
				height > m_ovg->MaxImageSize().Height())
		{
			ELOG("[OpenVG] cannot allocate pixmap of %dpx x %dpx, "
					"maximum size is %dpx x %dpx!", width, height,
					m_ovg->MaxImageSize().Width(),
					m_ovg->MaxImageSize().Height());

			return NULL;
		}
#else
		if (width > m_ovg->MaxImageSize().Width() ||
				height > m_ovg->MaxImageSize().Height())
		{
			ELOG("[OpenVG] cannot allocate pixmap of %dpx x %dpx, "
					"clipped to %dpx x %dpx!", width, height,
					min(width, m_ovg->MaxImageSize().Width()),
					min(height, m_ovg->MaxImageSize().Height()));

			width = min(width, m_ovg->MaxImageSize().Width());
			height = min(height, m_ovg->MaxImageSize().Height());
		}
#endif
		// create pixel buffer and wait until command has been completed
		cOvgRenderTarget *buffer = new cOvgRenderTarget(width, height);
		m_ovg->DoCmd(new cOvgCmdCreatePixelBuffer(buffer), true);

		cTimeMs timer(10000);
		while (!buffer->initialized && !timer.TimedOut())
			cCondWait::SleepMs(2);

		if (buffer->initialized && buffer->image != VG_INVALID_HANDLE)
		{
			cOvgPixmap *pm = new cOvgPixmap(Layer, m_ovg, buffer,
					ViewPort, DrawPort);

			if (cOsd::AddPixmap(pm))
			{
				for (int i = 0; i < m_pixmaps.Size(); i++)
					if (!m_pixmaps[i])
						return m_pixmaps[i] = pm;

				m_pixmaps.Append(pm);
				return pm;
			}
			delete pm;
		}
		else
		{
			ELOG("[OpenVG] failed to create pixmap! (%s)",
					timer.TimedOut() ? "timed out" : "allocation failed");
			m_ovg->DoCmd(new cOvgCmdDestroySurface(buffer));
		}
		return NULL;
	}

	virtual void DestroyPixmap(cPixmap *Pixmap)
	{
		if (Pixmap)
		{
			LOCK_PIXMAPS;
			for (int i = 1; i < m_pixmaps.Size(); i++)
				if (m_pixmaps[i] == Pixmap)
				{
					if (Pixmap->Layer() >= 0)
						m_pixmaps[0]->SetDirty();

					m_pixmaps[i] = NULL;
					cOsd::DestroyPixmap(Pixmap);
					return;
				}
		}
	}

	virtual void SaveRegion(int x1, int y1, int x2, int y2)
	{
		if (!Active() || !m_pixmaps[0])
			return;

		m_pixmaps[0]->SaveRegion(
				cRect(x1, y1, x2 - x1 + 1, y2 - y1 + 1).Shifted(
					- m_pixmaps[0]->ViewPort().Point()));
	}

	virtual void RestoreRegion(void)
	{
		if (!Active() || !m_pixmaps[0])
			return;

		m_pixmaps[0]->RestoreRegion();
	}

	virtual void DrawPixel(int x, int y, tColor Color)
	{
		if (!m_pixmaps[0])
			return;

		m_pixmaps[0]->DrawPixel(
				cPoint(x, y) - m_pixmaps[0]->ViewPort().Point(), Color);
	}

	virtual void DrawBitmap(int x, int y, const cBitmap &Bitmap,
			tColor ColorFg = 0, tColor ColorBg = 0,
			bool ReplacePalette = false, bool Overlay = false)
	{
		if (!m_pixmaps[0])
			return;

		m_pixmaps[0]->DrawBitmap(
				cPoint(x, y) - m_pixmaps[0]->ViewPort().Point(),
				Bitmap, ColorFg, ColorBg, Overlay);
	}

	virtual void DrawScaledBitmap(int x, int y, const cBitmap &Bitmap,
			double FactorX, double FactorY, bool AntiAlias = false)
	{
		if (!m_pixmaps[0])
			return;

		m_pixmaps[0]->DrawScaledBitmap(
				cPoint(x, y) - m_pixmaps[0]->ViewPort().Point(),
				Bitmap, FactorX, FactorY);
	}

	virtual void DrawImage(const cPoint &Point, int ImageHandle)
	{
		if (!m_pixmaps[0])
			return;

		m_pixmaps[0]->DrawImage(Point - m_pixmaps[0]->ViewPort().Point(),
				ImageHandle);
	}

	virtual void DrawImage(const cPoint &Point, const cImage &Image)
	{
		if (!m_pixmaps[0])
			return;

		m_pixmaps[0]->DrawImage(Point - m_pixmaps[0]->ViewPort().Point(),
				Image);
	}

	virtual void DrawRectangle(int x1, int y1, int x2, int y2, tColor Color)
	{
		if (!m_pixmaps[0])
			return;

		m_pixmaps[0]->DrawRectangle(
				cRect(x1, y1, x2 - x1 + 1, y2 - y1 + 1).Shifted(
				- m_pixmaps[0]->ViewPort().Point()), Color);
	}

	virtual void DrawEllipse(int x1, int y1, int x2, int y2, tColor Color,
			int Quadrants = 0)
	{
		if (!m_pixmaps[0])
			return;

		m_pixmaps[0]->DrawEllipse(
				cRect(x1, y1, x2 - x1 + 1, y2 - y1 + 1).Shifted(
				- m_pixmaps[0]->ViewPort().Point()), Color, Quadrants);
	}

	virtual void DrawSlope(int x1, int y1, int x2, int y2, tColor Color,
			int Type)
	{
		if (!m_pixmaps[0])
			return;

		m_pixmaps[0]->DrawSlope(
				cRect(x1, y1, x2 - x1 + 1, y2 - y1 + 1).Shifted(
				- m_pixmaps[0]->ViewPort().Point()), Color, Type);
	}

	virtual void DrawText(int x, int y, const char *s, tColor ColorFg,
			tColor ColorBg, const cFont *Font, int Width = 0, int Height = 0,
			int Alignment = taDefault)
	{
		if (!m_pixmaps[0])
			return;

		m_pixmaps[0]->DrawText(cPoint(x, y) - m_pixmaps[0]->ViewPort().Point(),
				s, ColorFg, ColorBg, Font, Width, Height, Alignment);
	}

	virtual void Flush(void)
	{
		if (!Active())
			return;

		LOCK_PIXMAPS;

//#define USE_VDRS_RENDERING
#ifdef USE_VDRS_RENDERING
		while (cOvgPixmap *pm =	dynamic_cast<cOvgPixmap *>(RenderPixmaps()))
		{
			pm->CopyToTarget(m_surface, Left(), Top());

#if APIVERSNUM >= 20110
			DestroyPixmap(pm);
#else
			delete pm;
#endif
		}
#else
		bool dirty = false;
		for (int i = 0; i < m_pixmaps.Size() && !dirty; i++)
			if (m_pixmaps[i] &&
					m_pixmaps[i]->Layer() >= 0 && m_pixmaps[i]->IsDirty())
				dirty = true;

		if (!dirty)
			return;

		m_ovg->DoCmd(new cOvgCmdClear(m_surface));

		for (int layer = 0; layer < MAXPIXMAPLAYERS; layer++)
			for (int i = 0; i < m_pixmaps.Size(); i++)
				if (m_pixmaps[i])
					if (m_pixmaps[i]->Layer() == layer)
						m_pixmaps[i]->RenderToTarget(m_surface, Left(), Top());
#endif
		m_ovg->DoCmd(new cOvgCmdFlush(m_surface), true);
		return;
	}

protected:

	virtual void SetActive(bool On)
	{
		if (On != Active())
		{
			cOsd::SetActive(On);
			if (!On)
				Clear();
			else
				if (GetBitmap(0))
					Flush();
		}
	}

	virtual void Clear(void)
	{
		m_ovg->DoCmd(new cOvgCmdClear(m_surface));
		m_ovg->DoCmd(new cOvgCmdFlush(m_surface));
	}

private:

	cOvgThread           *m_ovg;
	cOvgRenderTarget     *m_surface;
	cVector<cOvgPixmap *> m_pixmaps;
};

/* ------------------------------------------------------------------------- */

class cOvgRawOsd : public cOsd
{
public:

	cOvgRawOsd(int Left, int Top, uint Level, cOvgThread *ovg) :
		cOsd(Left, Top, Level),
		m_ovg(ovg),
		m_surface(new cOvgRenderTarget())
	{ }

	virtual ~cOvgRawOsd()
	{
		SetActive(false);
		m_ovg->DoCmd(new cOvgCmdDestroySurface(m_surface));
	}

	virtual void Flush(void)
	{
		if (!Active())
			return;

		if (IsTrueColor())
		{
			LOCK_PIXMAPS;
			while (cPixmapMemory *pm =
					dynamic_cast<cPixmapMemory *>(RenderPixmaps()))
			{
				if (tColor* argb = MALLOC(tColor,
						pm->DrawPort().Width() * pm->DrawPort().Height()))
				{
					memcpy(argb, pm->Data(), sizeof(tColor) *
							pm->DrawPort().Width() * pm->DrawPort().Height());

					m_ovg->DoCmd(new cOvgCmdDrawBitmap(m_surface,
							Left() + pm->ViewPort().Left(),
							Top() + pm->ViewPort().Top(),
							pm->DrawPort().Width(), pm->DrawPort().Height(),
							argb));
				}
#if APIVERSNUM >= 20110
				DestroyPixmap(pm);
#else
				delete pm;
#endif
			}
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

					m_ovg->DoCmd(new cOvgCmdDrawBitmap(m_surface,
							Left() + bitmap->X0() + x1,
							Top() + bitmap->Y0() + y1, w, h, argb));

					bitmap->Clean();
				}
			}
		}
		m_ovg->DoCmd(new cOvgCmdFlush(m_surface), true);
	}

	virtual eOsdError SetAreas(const tArea *Areas, int NumAreas)
	{
		eOsdError error;
		cBitmap *bitmap;

		if (Active())
			Clear();

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
				Clear();
			else
				if (GetBitmap(0))
					Flush();
		}
	}

	virtual void Clear(void)
	{
		m_ovg->DoCmd(new cOvgCmdClear(m_surface));
		m_ovg->DoCmd(new cOvgCmdFlush(m_surface));
	}

private:

	cOvgThread       *m_ovg;
	cOvgRenderTarget *m_surface;

};

/* ------------------------------------------------------------------------- */

cRpiOsdProvider* cRpiOsdProvider::s_instance = 0;

cRpiOsdProvider::cRpiOsdProvider(int layer) :
	cOsdProvider(),
	m_ovg(0)
{
	DLOG("new cOsdProvider()");
	m_ovg = new cOvgThread(layer);
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

int cRpiOsdProvider::StoreImageData(const cImage &Image)
{
	int id = m_ovg->StoreImageData(Image);
	return id ? id : cOsdProvider::StoreImageData(Image);
}

void cRpiOsdProvider::DropImageData(int ImageHandle)
{
	if (ImageHandle < 0)
		m_ovg->DropImageData(ImageHandle);
	else
		cOsdProvider::DropImageData(ImageHandle);
}

const cImage *cRpiOsdProvider::GetImageData(int ImageHandle)
{
	return cOsdProvider::GetImageData(ImageHandle);
}

void cRpiOsdProvider::ResetOsd(bool cleanup)
{
	if (s_instance)
		s_instance->m_ovg->DoCmd(new cOvgCmdReset(cleanup));

	UpdateOsdSize(true);
}
