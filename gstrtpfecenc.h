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



#ifndef GSTRTPFECENC_H
#define GSTRTPFECENC_H

#include <gst/gst.h>
#include "fecenc.h"


G_BEGIN_DECLS


typedef struct _GstRtpFECEnc GstRtpFECEnc;
typedef struct _GstRtpFECEncClass GstRtpFECEncClass;

/* standard type-casting and type-checking boilerplate... */
#define GST_TYPE_RTP_FEC_ENC             (gst_rtp_fec_enc_get_type())
#define GST_RTP_FEC_ENC(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_RTP_FEC_ENC, GstRtpFECEnc))
#define GST_RTP_FEC_ENC_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_RTP_FEC_ENC, GstRtpFECEncClass))
#define GST_IS_RTP_FEC_ENC(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_RTP_FEC_ENC))
#define GST_IS_RTP_FEC_ENC_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_RTP_FEC_ENC))

struct _GstRtpFECEnc
{
	GstElement element;

	GstPad
		*sinkpad,
		*srcpad,
		*fecpad;

	/* Actual FEC encoder */
	fec_enc *enc;
};

struct _GstRtpFECEncClass
{
	GstElementClass parent_class;
};

GType gst_rtp_fec_enc_get_type(void);


G_END_DECLS


#endif

