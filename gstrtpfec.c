/*
 *  RTP forward error correction encoding plugin for GStreamer
 *
 *  Copyright (C) 2012 Carlos Rafael Giani <dv@pseudoterminal.org>
 *
 *  This library is free software; you can redistribute it and/or
 *  modify it under the terms of the GNU Lesser General Public
 *  License as published by the Free Software Foundation; either
 *  version 2.1 of the License, or (at your option) any later version.
 *
 *  This library is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  Lesser General Public License for more details.
 *
 *  You should have received a copy of the GNU Lesser General Public
 *  License along with this library; if not, write to the Free Software
 *  Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */



#include "gstrtpfec.h"


#define PACKAGE "package"


static gboolean plugin_init(GstPlugin *plugin)
{
	if (!gst_element_register(plugin, "rtpfecenc", GST_RANK_NONE, gst_rtp_fec_enc_get_type())) return FALSE;
	if (!gst_element_register(plugin, "rtpfecdec", GST_RANK_NONE, gst_rtp_fec_dec_get_type())) return FALSE;
	return TRUE;
}

GST_PLUGIN_DEFINE(
	GST_VERSION_MAJOR,
	GST_VERSION_MINOR,
	"rtpfec",
	"RTP forward error correction encoding plugin",
	plugin_init,
	"1.0",
	GST_LICENSE_UNKNOWN,
	"package",
	"http://no-url-yet"
)

