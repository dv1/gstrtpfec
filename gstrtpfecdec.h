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



#ifndef GSTRTPFECDEC_H
#define GSTRTPFECDEC_H

#include <gst/gst.h>
#include "fecdec.h"


G_BEGIN_DECLS


typedef struct _GstRtpFECDec GstRtpFECDec;
typedef struct _GstRtpFECDecClass GstRtpFECDecClass;

/* standard type-casting and type-checking boilerplate... */
#define GST_TYPE_RTP_FEC_DEC             (gst_rtp_fec_dec_get_type())
#define GST_RTP_FEC_DEC(obj)             (G_TYPE_CHECK_INSTANCE_CAST((obj), GST_TYPE_RTP_FEC_DEC, GstRtpFECDec))
#define GST_RTP_FEC_DEC_CLASS(klass)     (G_TYPE_CHECK_CLASS_CAST((klass), GST_TYPE_RTP_FEC_DEC, GstRtpFECDecClass))
#define GST_IS_RTP_FEC_DEC(obj)          (G_TYPE_CHECK_INSTANCE_TYPE((obj), GST_TYPE_RTP_FEC_DEC))
#define GST_IS_RTP_FEC_DEC_CLASS(klass)  (G_TYPE_CHECK_CLASS_TYPE((klass), GST_TYPE_RTP_FEC_DEC))
#define GST_RTP_FEC_DEC_CAST(obj)        ((GstRtpFECDec*)(obj))

struct _GstRtpFECDec
{
	GstElement element;

	GstPad
		*sinkpad,
		*srcpad,
		*fecpad;

	/* Actual FEC decoder */
	fec_dec *dec;

	/*
	Mutex used in the chain functions. The GstObject mutex cannot be used,
	because it is locked by other functions as well, potentially causing deadlocks.
	*/
	GMutex  *mutex;
};

struct _GstRtpFECDecClass
{
	GstElementClass parent_class;
};

GType gst_rtp_fec_dec_get_type(void);


G_END_DECLS


#endif

