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



#include <gst/rtp/gstrtpbuffer.h>
#include "gstrtpfecenc.h"



/**** Debugging ****/

GST_DEBUG_CATEGORY_STATIC(rtpfecenc_debug);
#define GST_CAT_DEFAULT rtpfecenc_debug



/**** Constants ****/


enum
{
	DEFAULT_PT = 99 /* Default payload type for FEC packets */
};


enum
{
	PROP_0 = 0, /* GStreamer disallows properties with id 0 -> using dummy enum to prevent 0 */
	PROP_NUM_MEDIA_PACKETS,
	PROP_NUM_FEC_PACKETS,
	PROP_PAYLOAD_TYPE
};


enum
{
	DEFAULT_NUM_MEDIA_PACKETS = 9,
	DEFAULT_NUM_FEC_PACKETS = 3
};



/**** Function declarations ****/

/* This function is invoked when the sink pad receives data */
static GstFlowReturn gst_rtp_fec_enc_chain(GstPad *pad, GstBuffer *packet);
/* This function is invoked when the sink pad receives caps */
static gboolean gst_rtp_fec_enc_setcaps(GstPad *pad, GstCaps *caps);

/* Property accessors */
static void gst_rtp_fec_enc_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec);
static void gst_rtp_fec_enc_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

/* Called when the pipeline state changes */
static GstStateChangeReturn gst_rtp_fec_enc_change_state(GstElement *element, GstStateChange transition);

/* Finalizer; cleans up states */
static void gst_rtp_fec_enc_finalize(GObject *object);



/**** GStreamer boilerplate ****/

GST_BOILERPLATE(GstRtpFECEnc, gst_rtp_fec_enc, GstElement, GST_TYPE_ELEMENT)



/**** Pads ****/

static GstStaticPadTemplate sink_template = GST_STATIC_PAD_TEMPLATE(
	"sink",
	GST_PAD_SINK,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS("application/x-rtp")
);

static GstStaticPadTemplate src_template = GST_STATIC_PAD_TEMPLATE(
	"src",
	GST_PAD_SRC,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS("application/x-rtp")
);

static GstStaticPadTemplate fec_template = GST_STATIC_PAD_TEMPLATE(
	"fec",
	GST_PAD_SRC,
	GST_PAD_ALWAYS,
	GST_STATIC_CAPS(
		"application/x-rtp,"
		"media = (string) { \"video\", \"audio\", \"application\" }, "
		"payload = (int) [ 96, 127 ], "
		"clock-rate = (int) [ 1, MAX ], "
		"encoding-name = (string) \"parityfec\""
	)
);



/**** Function definition ****/

static void gst_rtp_fec_enc_base_init(gpointer klass)
{
	GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

	gst_element_class_set_details_simple(
		element_class,
		"RTP forward error correction payloader",
		"Codec/Payloader/Network/RTP",
		"Generates forward error correction packets out of incoming media data",
		"Carlos Rafael Giani <dv@pseudoterminal.org>"
	);

	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&sink_template));
	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&src_template));
	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&fec_template));
}


static void gst_rtp_fec_enc_class_init(GstRtpFECEncClass *klass)
{
	GObjectClass *object_class;
	GstElementClass *element_class;

	GST_DEBUG_CATEGORY_INIT(rtpfecenc_debug, "rtpfecenc", 0, "RTP FEC payloader");

	object_class = G_OBJECT_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);

	/* Set functions */
	object_class->finalize = GST_DEBUG_FUNCPTR(gst_rtp_fec_enc_finalize);
	element_class->change_state = GST_DEBUG_FUNCPTR(gst_rtp_fec_enc_change_state);
	object_class->set_property = GST_DEBUG_FUNCPTR(gst_rtp_fec_enc_set_property);
	object_class->get_property = GST_DEBUG_FUNCPTR(gst_rtp_fec_enc_get_property);

	/* Install properties */
	g_object_class_install_property(
		object_class,
		PROP_NUM_MEDIA_PACKETS,
		g_param_spec_uint(
			"num-media-packets",
			"Number of media packets",
			"Number of media packets to use for FEC packet generation",
		        1, 24,
			DEFAULT_NUM_MEDIA_PACKETS,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_NUM_FEC_PACKETS,
		g_param_spec_uint(
			"num-fec-packets",
			"Number of FEC packets",
			"Number of forward error correction packets to generate",
		        1, 24,
			DEFAULT_NUM_FEC_PACKETS,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
	g_object_class_install_property(
		object_class,
		PROP_PAYLOAD_TYPE,
		g_param_spec_uint(
			"pt",
			"PT",
			"Payload type for FEC packets",
		        0, 127,
			DEFAULT_PT,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
}


static void gst_rtp_fec_enc_init(GstRtpFECEnc *rtp_fec_enc, GstRtpFECEncClass *klass)
{
	GstElement *element;

	klass = klass;
	
	element = GST_ELEMENT(rtp_fec_enc);

	/* Create pads out of the templates defined earlier */
	rtp_fec_enc->sinkpad = gst_pad_new_from_static_template(&sink_template, "sink");
	rtp_fec_enc->srcpad = gst_pad_new_from_static_template(&src_template, "src");
	rtp_fec_enc->fecpad = gst_pad_new_from_static_template(&fec_template, "fec");

	/* Set chain and setcaps functions for the sink pad */
	gst_pad_set_chain_function(rtp_fec_enc->sinkpad, gst_rtp_fec_enc_chain);
	gst_pad_set_setcaps_function(rtp_fec_enc->sinkpad, gst_rtp_fec_enc_setcaps);

	/* Add the pads to the element */
	gst_element_add_pad(element, rtp_fec_enc->sinkpad);
	gst_element_add_pad(element, rtp_fec_enc->srcpad);
	gst_element_add_pad(element, rtp_fec_enc->fecpad);

	/* Finally, create the FEC encoder */
	/* TODO: make seqnum-offset a property */
	rtp_fec_enc->enc = fec_enc_create(DEFAULT_NUM_MEDIA_PACKETS, DEFAULT_NUM_FEC_PACKETS, DEFAULT_PT, g_random_int_range(0, G_MAXUINT16));
}


static GstFlowReturn gst_rtp_fec_enc_chain(GstPad *pad, GstBuffer *packet)
{
	GstRtpFECEnc *rtp_fec_enc;
	GstFlowReturn ret;
	guint16 seqnum;

	rtp_fec_enc = GST_RTP_FEC_ENC(gst_pad_get_parent(pad));

	seqnum = gst_rtp_buffer_get_seq(packet);
	GST_DEBUG_OBJECT(rtp_fec_enc, "received RTP packet, seqnum %u", seqnum);

	/* Push the media packet to the encoder */
	fec_enc_push_media_packet(rtp_fec_enc->enc, packet);

	/*
	If the encoder was able to generate FEC packets after the push call above,
	push the packets into the FEC pad
	*/
	while (fec_enc_has_fec_packets(rtp_fec_enc->enc))
	{
		GstBuffer *fec_packet = fec_enc_pop_fec_packet(rtp_fec_enc->enc);
		seqnum = gst_rtp_buffer_get_seq(fec_packet);
		GST_DEBUG_OBJECT(rtp_fec_enc, "pushing FEC packet, seqnum %u", seqnum);
		gst_buffer_set_caps(fec_packet, GST_PAD_CAPS(rtp_fec_enc->fecpad));
		gst_pad_push(rtp_fec_enc->fecpad, fec_packet);
		/*gst_buffer_unref(fec_packet);*/ /* TODO: can this be removed? */
	}

	/* Finally, push the media packet to the src pad */
	ret = gst_pad_push(rtp_fec_enc->srcpad, packet);

	gst_object_unref(rtp_fec_enc);

	return ret;
}


static gboolean gst_rtp_fec_enc_setcaps(GstPad *pad, GstCaps *caps)
{
	GstRtpFECEnc *rtp_fec_enc;
	GstStructure *str;
	gint clock_rate;
	const gchar *media;
	GstCaps *feccaps;
	gboolean res;

	/*
	Here it is necessary to get the caps structure and copy some values to the FEC pad caps.
	To do that, this function exists. It intercepts the srcpad setcaps call, copies the values
	to the fec pad caps, and then sets the src pad caps.
	*/

	rtp_fec_enc = GST_RTP_FEC_ENC(gst_pad_get_parent(pad));

	/* Retrieve the caps structure */
	str = gst_caps_get_structure(caps, 0);

	/* clock-rate must be present in the caps */
	if (!gst_structure_get_int(str, "clock-rate", &clock_rate))
	{
		GST_WARNING_OBJECT(rtp_fec_enc, "missing clock-rate on caps");
		return FALSE;
	}

	/* media must be present in the caps */
	if (!(media = gst_structure_get_string(str, "media")))
	{
		GST_WARNING_OBJECT(rtp_fec_enc, "missing media on caps");
		return FALSE;
	}

	/* Generate new caps for the fec pad */
	feccaps = gst_caps_new_simple(
		"application/x-rtp",
		"media", G_TYPE_STRING, media,
		"payload", G_TYPE_INT, fec_enc_get_payload_type(rtp_fec_enc->enc), /* Use configured payload type for FEC packets */
		"clock-rate", G_TYPE_INT, clock_rate,
		"encoding-name", G_TYPE_STRING, "parityfec",
		NULL
	);
	/* Set the fec pad caps */
	gst_pad_set_caps(rtp_fec_enc->fecpad, feccaps);
	/* Since gst_pad_set_caps() increases the caps reference count, it needs to be decreased here */
	gst_caps_unref(feccaps);

	/* Finally, do the regular src pad setcaps */
	res = gst_pad_set_caps(rtp_fec_enc->srcpad, caps);

	gst_object_unref(rtp_fec_enc);

	return res;
}


static void gst_rtp_fec_enc_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec)
{
	GstRtpFECEnc *rtp_fec_enc;

	GST_OBJECT_LOCK(object);

	rtp_fec_enc = GST_RTP_FEC_ENC(object);

	switch (prop_id)
	{
		case PROP_NUM_MEDIA_PACKETS:
		{
			guint num_media_packets = g_value_get_uint(value);
			GST_DEBUG_OBJECT(rtp_fec_enc, "Set number of media packets to %u", num_media_packets);
			fec_enc_set_num_media_packets(rtp_fec_enc->enc, num_media_packets);
			break;
		}
		case PROP_NUM_FEC_PACKETS:
		{
			guint num_fec_packets = g_value_get_uint(value);
			GST_DEBUG_OBJECT(rtp_fec_enc, "Set number of FEC packets to %u", num_fec_packets);
			fec_enc_set_num_fec_packets(rtp_fec_enc->enc, num_fec_packets);
			break;
		}
		case PROP_PAYLOAD_TYPE:
		{
			guint payload_type = g_value_get_uint(value);
			GST_DEBUG_OBJECT(rtp_fec_enc, "Set FEC payload type to %u", payload_type);
			fec_enc_set_payload_type(rtp_fec_enc->enc, payload_type);
			break;
		}
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}

	GST_OBJECT_UNLOCK(object);
}


static void gst_rtp_fec_enc_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstRtpFECEnc *rtp_fec_enc;

	GST_OBJECT_LOCK(object);

	rtp_fec_enc = GST_RTP_FEC_ENC(object);

	switch (prop_id)
	{
		case PROP_NUM_MEDIA_PACKETS:
			g_value_set_uint(value, fec_enc_get_num_media_packets(rtp_fec_enc->enc));
			break;
		case PROP_NUM_FEC_PACKETS:
			g_value_set_uint(value, fec_enc_get_num_fec_packets(rtp_fec_enc->enc));
			break;
		case PROP_PAYLOAD_TYPE:
			g_value_set_uint(value, fec_enc_get_payload_type(rtp_fec_enc->enc));
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}

	GST_OBJECT_UNLOCK(object);
}


static GstStateChangeReturn gst_rtp_fec_enc_change_state(GstElement *element, GstStateChange transition)
{
	GstStateChangeReturn ret;
	GstRtpFECEnc *rtp_fec_enc;
	
	rtp_fec_enc = GST_RTP_FEC_ENC(element);
	ret = GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);

	switch (transition)
	{
		case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
			break;
		case GST_STATE_CHANGE_PAUSED_TO_READY:
			fec_enc_reset(rtp_fec_enc->enc);
			break;
		case GST_STATE_CHANGE_READY_TO_NULL:
			break;
		default:
			break;
	}

	return ret;
}


static void gst_rtp_fec_enc_finalize(GObject *object)
{
	GstRtpFECEnc *rtp_fec_enc = GST_RTP_FEC_ENC(object);
	fec_enc_destroy(rtp_fec_enc->enc);
	GST_DEBUG_OBJECT(rtp_fec_enc, "Cleaned up FEC encoder");
	G_OBJECT_CLASS(parent_class)->finalize(object);
}

