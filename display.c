/*
 * See the README file for copyright information and how to reach the author.
 *
 * $Id$
 */

#include "display.h"
#include "tools.h"

#include <vdr/tools.h>

extern "C" {
#include "interface/vmcs_host/vc_tvservice.h"
#include "interface/vmcs_host/vc_dispmanx.h"
}

cRpiDisplay* cRpiDisplay::s_instance = 0;

class cRpiDisplay::cHandles
{
public:

	DISPMANX_DISPLAY_HANDLE_T display;
	DISPMANX_UPDATE_HANDLE_T  update;
};

cRpiDisplay* cRpiDisplay::GetInstance(void)
{
	if (!s_instance)
	{
		TV_DISPLAY_STATE_T tvstate;
		memset(&tvstate, 0, sizeof(TV_DISPLAY_STATE_T));

		if (!vc_tv_get_display_state(&tvstate))
		{
			// HDMI
			if (tvstate.state & (VC_HDMI_HDMI | VC_HDMI_DVI))
				s_instance = new cRpiHDMIDisplay(
						tvstate.display.hdmi.width,
						tvstate.display.hdmi.height,
						tvstate.display.hdmi.frame_rate,
						tvstate.display.hdmi.group,
						tvstate.display.hdmi.mode);
			else
				s_instance = new cRpiCompositeDisplay(
						tvstate.display.sdtv.width,
						tvstate.display.sdtv.height,
						tvstate.display.sdtv.frame_rate);
		}
		else
		{
			ELOG("failed to get display parameters!");
			return false;
		}
	}

	return s_instance;
}

void cRpiDisplay::DropInstance(void)
{
	delete s_instance;
	s_instance = 0;
}

cRpiDisplay::cRpiDisplay(int width, int height, int frameRate,
		cRpiVideoPort::ePort port) :
	m_width(width),
	m_height(height),
	m_frameRate(frameRate),
	m_interlaced(false),
	m_port(port),
	m_handles(new cHandles())
{
	m_handles->display = vc_dispmanx_display_open(
			m_port == cRpiVideoPort::eHDMI ? 0 : 1);

	if (m_handles->display)
	{
		m_handles->update = vc_dispmanx_update_start(0);
		if (m_handles->update == DISPMANX_NO_HANDLE)
			ELOG("failed to start display update!");
	}
	else
		ELOG("failed to open display!");
}

cRpiDisplay::~cRpiDisplay()
{
	if (m_handles->display)
		vc_dispmanx_display_close(m_handles->display);

	delete m_handles;
}

int cRpiDisplay::GetSize(int &width, int &height)
{
	cRpiDisplay* instance = GetInstance();
	if (instance)
	{
		width = instance->m_width;
		height = instance->m_height;
		return 0;
	}
	return -1;
}

int cRpiDisplay::GetSize(int &width, int &height, double &aspect)
{
	cRpiDisplay* instance = GetInstance();
	if (instance)
	{
		width = instance->m_width;
		height = instance->m_height;
		aspect = (double)width / height;
		return 0;
	}
	return -1;
}

int cRpiDisplay::Get(int &width, int &height, int &frameRate, bool &interlaced)
{
	cRpiDisplay* instance = GetInstance();
	if (instance)
	{
		width = instance->m_width;
		height = instance->m_height;
		frameRate = instance->m_frameRate;
		interlaced = instance->m_interlaced;
		return 0;
	}
	return -1;
}

int cRpiDisplay::Set(int width, int height, int frameRate, bool interlaced)
{
	cRpiDisplay* instance = GetInstance();
	if (instance)
		return instance->SetMode(width, height, frameRate, interlaced);

	return -1;
}

cRpiVideoPort::ePort cRpiDisplay::GetVideoPort(void)
{
	cRpiDisplay* instance = GetInstance();
	if (instance)
		return instance->m_port;

	return cRpiVideoPort::eComposite;
}

bool cRpiDisplay::IsProgressive(void)
{
	cRpiDisplay* instance = GetInstance();
	if (instance)
		return !instance->m_interlaced;

	return false;
}

int cRpiDisplay::AddElement(DISPMANX_ELEMENT_HANDLE_T &element,
		int width, int height, int layer)
{
	cRpiDisplay* instance = GetInstance();
	if (instance)
	{
		VC_RECT_T dstRect = { 0, 0, width, height };
		VC_RECT_T srcRect = { 0, 0, width << 16, height << 16 };

		element = vc_dispmanx_element_add(instance->m_handles->update,
				instance->m_handles->display, layer, &dstRect, 0, &srcRect,
				DISPMANX_PROTECTION_NONE, 0, 0, (DISPMANX_TRANSFORM_T)0);

		if (!element)
		{
			ELOG("failed to add display element!");
			return -1;
		}

		vc_dispmanx_update_submit_sync(instance->m_handles->update);
		return 0;
	}
	return -1;
}

int cRpiDisplay::Snapshot(uint8_t* frame, int width, int height)
{
	cRpiDisplay* instance = GetInstance();
	if (instance)
	{
		uint32_t image;
		DISPMANX_RESOURCE_HANDLE_T res = vc_dispmanx_resource_create(
				VC_IMAGE_RGB888, width, height, &image);

		vc_dispmanx_snapshot(instance->m_handles->display, res,
				(DISPMANX_TRANSFORM_T)(DISPMANX_SNAPSHOT_PACK));

		VC_RECT_T rect = { 0, 0, width, height };
		vc_dispmanx_resource_read_data(res, &rect, frame, width * 3);

		vc_dispmanx_resource_delete(res);
		return 0;
	}
	return -1;
}

/* ------------------------------------------------------------------------- */

#define HDMI_MAX_MODES 64

class cRpiHDMIDisplay::ModeList {
public:

	TV_SUPPORTED_MODE_NEW_T modes[HDMI_MAX_MODES];
	int nModes;
};

cRpiHDMIDisplay::cRpiHDMIDisplay(int width, int height, int frameRate,
		int group, int mode) :
	cRpiDisplay(width, height, frameRate, cRpiVideoPort::eHDMI),
	m_modes(new cRpiHDMIDisplay::ModeList()),
	m_group(group),
	m_mode(mode),
	m_startGroup(group),
	m_startMode(mode),
	m_modified(false)
{

	m_modes->nModes = vc_tv_hdmi_get_supported_modes_new(HDMI_RES_GROUP_CEA,
			m_modes->modes, HDMI_MAX_MODES, NULL, NULL);

	m_modes->nModes += vc_tv_hdmi_get_supported_modes_new(HDMI_RES_GROUP_DMT,
			&m_modes->modes[m_modes->nModes], HDMI_MAX_MODES - m_modes->nModes,
			NULL, NULL);

	if (m_modes->nModes)
	{
		DLOG("supported HDMI modes:");
		for (int i = 0; i < m_modes->nModes; i++)
		{
			DLOG("%s[%02d]: %4dx%4d@%2d%s | %s | %3d.%03dMHz%s%s",
				m_modes->modes[i].group == HDMI_RES_GROUP_CEA ? "CEA" :
				m_modes->modes[i].group == HDMI_RES_GROUP_DMT ? "DMT" : "---",
				m_modes->modes[i].code,
				m_modes->modes[i].width,
				m_modes->modes[i].height,
				m_modes->modes[i].frame_rate,
				m_modes->modes[i].scan_mode ? "i" : "p",
				m_modes->modes[i].aspect_ratio == HDMI_ASPECT_4_3   ? " 4:3 " :
				m_modes->modes[i].aspect_ratio == HDMI_ASPECT_14_9  ? "14:9 " :
				m_modes->modes[i].aspect_ratio == HDMI_ASPECT_16_9  ? "16:9 " :
				m_modes->modes[i].aspect_ratio == HDMI_ASPECT_5_4   ? " 5:4 " :
				m_modes->modes[i].aspect_ratio == HDMI_ASPECT_16_10 ? "16:10" :
				m_modes->modes[i].aspect_ratio == HDMI_ASPECT_15_9  ? "15:9 " :
				m_modes->modes[i].aspect_ratio == HDMI_ASPECT_21_9  ? "21:9 " :
					"unknown aspect ratio",
				m_modes->modes[i].pixel_freq / 1000000,
				m_modes->modes[i].pixel_freq % 1000000 / 1000,
				m_modes->modes[i].native ? " (native)" : "",
				m_modes->modes[i].code == m_mode &&
					m_modes->modes[i].group == m_group ? " (current)" : "");

			// update initial parameters
			if (m_modes->modes[i].code == m_mode &&
					m_modes->modes[i].group == m_group)
			{
				m_width = m_modes->modes[i].width;
				m_height = m_modes->modes[i].height;
				m_frameRate = m_modes->modes[i].frame_rate;
				m_interlaced = m_modes->modes[i].scan_mode;
			}
		}
	}
	else
		ELOG("failed to read HDMI EDID information!");

}

cRpiHDMIDisplay::~cRpiHDMIDisplay()
{
	// if mode has been changed, set back to previous state
	if (m_modified)
		SetMode(m_startGroup, m_startMode);

	delete m_modes;
}

int cRpiHDMIDisplay::SetMode(int width, int height, int frameRate,
		bool interlaced)
{
	for (int i = 0; i < m_modes->nModes; i++)
	{
		if (m_modes->modes[i].width == width &&
				m_modes->modes[i].height == height &&
				m_modes->modes[i].frame_rate == frameRate &&
				m_modes->modes[i].scan_mode == interlaced)
		{
			DLOG("setting HDMI mode to %dx%d@%2d%s", width, height,
					frameRate, interlaced ? "i" : "p");

			m_width = width;
			m_height = height;
			m_frameRate = frameRate;
			m_interlaced = interlaced;
			return SetMode(m_modes->modes[i].group, m_modes->modes[i].code);
		}
	}

	DLOG("failed to set HDMI mode to %dx%d@%2d%s",
		width, height, frameRate, interlaced ? "i" : "p");
	return -1;
}

int cRpiHDMIDisplay::SetMode(int group, int mode)
{
	int ret = 0;
	if (group != m_group || mode != m_mode)
	{
		DBG("cHDMI::SetMode(%s, %d)",
			group == HDMI_RES_GROUP_DMT ? "DMT" :
			group == HDMI_RES_GROUP_CEA ? "CEA" : "unknown", mode);

		ret = vc_tv_hdmi_power_on_explicit_new(HDMI_MODE_HDMI,
				(HDMI_RES_GROUP_T)group, mode);
		if (ret)
			ELOG("failed to set HDMI mode!");
		else
		{
			m_group = group;
			m_mode = mode;
			m_modified = true;
		}
	}
	return ret;
}

/* ------------------------------------------------------------------------- */

cRpiCompositeDisplay::cRpiCompositeDisplay(int width, int height,
		int frameRate) :
	cRpiDisplay(width, height, frameRate, cRpiVideoPort::eComposite)
{

}
