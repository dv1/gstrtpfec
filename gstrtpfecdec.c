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



#include <assert.h>
#include <gst/rtp/gstrtpbuffer.h>
#include "gstrtpfecdec.h"



/**** Debugging ****/

GST_DEBUG_CATEGORY_STATIC(rtpfecdec_debug);
#define GST_CAT_DEFAULT rtpfecdec_debug



/**** Typedefs ****/


typedef enum
{
	BUFFER_TYPE_MEDIA,
	BUFFER_TYPE_FEC
}
packet_types;



/**** Constants ****/


enum
{
	PROP_0 = 0, /* GStreamer disallows properties with id 0 -> using dummy enum to prevent 0 */
	PROP_NUM_MEDIA_PACKETS,
	PROP_NUM_FEC_PACKETS
};


enum
{
	DEFAULT_NUM_MEDIA_PACKETS = 9,
	DEFAULT_NUM_FEC_PACKETS = 3
};



/**** Function declarations ****/

/* Handles incoming media and FEC packets, pushing them to the decoder and retrieving recoverd packets */
static GstFlowReturn gst_rtp_fec_dec_handle_incoming_packet(GstRtpFECDec *rtp_fec_dec, GstBuffer *packet, packet_types const packet_type);

/* This function is invoked when the sink pad receives data (media packets) */
static GstFlowReturn gst_rtp_fec_dec_chain_media(GstPad *pad, GstBuffer *packet);
/* This function is invoked when the fec pad receives data (fec packets) */
static GstFlowReturn gst_rtp_fec_dec_chain_fec(GstPad *pad, GstBuffer *packet);

/* Property accessors */
static void gst_rtp_fec_dec_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec);
static void gst_rtp_fec_dec_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec);

/* Called when the pipeline state changes */
static GstStateChangeReturn gst_rtp_fec_dec_change_state(GstElement *element, GstStateChange transition);

/* Finalizer; cleans up states */
static void gst_rtp_fec_dec_finalize(GObject *object);



/**** GStreamer boilerplate ****/

GST_BOILERPLATE(GstRtpFECDec, gst_rtp_fec_dec, GstElement, GST_TYPE_ELEMENT)



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
	GST_PAD_SINK,
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

static void gst_rtp_fec_dec_base_init(gpointer klass)
{
	GstElementClass *element_class = GST_ELEMENT_CLASS(klass);

	gst_element_class_set_details_simple(
		element_class,
		"RTP forward error correction depayloader",
		"Codec/Payloader/Network/RTP",
		"Restores lost RTP packets using forward error correction",
		"Carlos Rafael Giani <dv@pseudoterminal.org>"
	);

	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&sink_template));
	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&src_template));
	gst_element_class_add_pad_template(element_class, gst_static_pad_template_get(&fec_template));
}


static void gst_rtp_fec_dec_class_init(GstRtpFECDecClass *klass)
{
	GObjectClass *object_class;
	GstElementClass *element_class;

	GST_DEBUG_CATEGORY_INIT(rtpfecdec_debug, "rtpfecdec", 0, "RTP FEC depayloader");

	object_class = G_OBJECT_CLASS(klass);
	element_class = GST_ELEMENT_CLASS(klass);

	/* Set functions */
	object_class->finalize = GST_DEBUG_FUNCPTR(gst_rtp_fec_dec_finalize);
	element_class->change_state = GST_DEBUG_FUNCPTR(gst_rtp_fec_dec_change_state);
	object_class->set_property = GST_DEBUG_FUNCPTR(gst_rtp_fec_dec_set_property);
	object_class->get_property = GST_DEBUG_FUNCPTR(gst_rtp_fec_dec_get_property);

	/* Install properties */
	g_object_class_install_property(
		object_class,
		PROP_NUM_MEDIA_PACKETS,
		g_param_spec_uint(
			"num-media-packets",
			"Number of media packets",
			"Number of media packets to expect for FEC packet generation",
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
			"Number of forward error correction packets to expect",
		        1, 24,
			DEFAULT_NUM_FEC_PACKETS,
			G_PARAM_READWRITE | G_PARAM_STATIC_STRINGS
		)
	);
}


static void gst_rtp_fec_dec_init(GstRtpFECDec *rtp_fec_dec, GstRtpFECDecClass *klass)
{
	GstElement *element;

	klass = klass;
	
	element = GST_ELEMENT(rtp_fec_dec);

	/* Create pads out of the templates defined earlier */
	rtp_fec_dec->sinkpad = gst_pad_new_from_static_template(&sink_template, "sink");
	rtp_fec_dec->srcpad = gst_pad_new_from_static_template(&src_template, "src");
	rtp_fec_dec->fecpad = gst_pad_new_from_static_template(&fec_template, "fec");

	/* Set chain functions for sink and fec pads */
	gst_pad_set_chain_function(rtp_fec_dec->sinkpad, gst_rtp_fec_dec_chain_media);
	gst_pad_set_chain_function(rtp_fec_dec->fecpad, gst_rtp_fec_dec_chain_fec);

	/* Add the pads to the element */
	gst_element_add_pad(element, rtp_fec_dec->sinkpad);
	gst_element_add_pad(element, rtp_fec_dec->srcpad);
	gst_element_add_pad(element, rtp_fec_dec->fecpad);

	/* Initialize the mutex */
	rtp_fec_dec->mutex = g_mutex_new();

	/* Finally, create the FEC decoder */
	rtp_fec_dec->dec = fec_dec_create(DEFAULT_NUM_MEDIA_PACKETS, DEFAULT_NUM_FEC_PACKETS);
}


static GstFlowReturn gst_rtp_fec_dec_handle_incoming_packet(GstRtpFECDec *rtp_fec_dec, GstBuffer *packet, packet_types const packet_type)
{
	switch (packet_type)
	{
		case BUFFER_TYPE_FEC:
			fec_dec_push_fec_packet(rtp_fec_dec->dec, packet);
			/*
			fec_dec_push_fec_packet() refs the packet, and since the packet is not needed by
			anybody else, unref it here
			*/
			gst_buffer_unref(packet);
			break;

		case BUFFER_TYPE_MEDIA:
		{
			GstFlowReturn ret;
			fec_dec_push_media_packet(rtp_fec_dec->dec, packet);
			/*
			unlike with the fec packet, the media packet is not unref'd here,
			instead it is pushed downstream - another element might need it
			*/
			ret = gst_pad_push(rtp_fec_dec->srcpad, packet);
			if (ret != GST_FLOW_OK)
				return ret;

			break;
		}

		default:
			assert(0);
	}

	while (fec_dec_has_recovered_packets(rtp_fec_dec->dec))
	{
		GstFlowReturn ret;
		guint16 seqnum;
		GstBuffer *recovered_packet;

		recovered_packet = fec_dec_pop_recovered_packet(rtp_fec_dec->dec);
		seqnum = gst_rtp_buffer_get_seq(recovered_packet);

		GST_DEBUG_OBJECT(rtp_fec_dec, "pushing recovered RTP media packet, seqnum %u", seqnum);
		ret = gst_pad_push(rtp_fec_dec->srcpad, recovered_packet);

		if (ret != GST_FLOW_OK)
		{
			GST_ERROR_OBJECT(rtp_fec_dec, "Could not push RTP media packet, flushing remaining packets");
			fec_dec_flush_recovered_packets(rtp_fec_dec->dec);
			return ret;
		}
	}

	return GST_FLOW_OK;
}


static GstFlowReturn gst_rtp_fec_dec_chain_media(GstPad *pad, GstBuffer *packet)
{
	GstRtpFECDec *rtp_fec_dec;
	guint16 seqnum;
	GstFlowReturn ret;

	rtp_fec_dec = GST_RTP_FEC_DEC(gst_pad_get_parent(pad));
	g_mutex_lock(rtp_fec_dec->mutex);

	seqnum = gst_rtp_buffer_get_seq(packet);
	GST_DEBUG_OBJECT(rtp_fec_dec, "received RTP media packet, seqnum %u", seqnum);

	ret = gst_rtp_fec_dec_handle_incoming_packet(rtp_fec_dec, packet, BUFFER_TYPE_MEDIA);

	g_mutex_unlock(rtp_fec_dec->mutex);
	gst_object_unref(rtp_fec_dec);

	return ret;
}


static GstFlowReturn gst_rtp_fec_dec_chain_fec(GstPad *pad, GstBuffer *packet)
{
	GstRtpFECDec *rtp_fec_dec;
	guint16 seqnum;
	GstFlowReturn ret;

	rtp_fec_dec = GST_RTP_FEC_DEC(gst_pad_get_parent(pad));
	g_mutex_lock(rtp_fec_dec->mutex);

	seqnum = gst_rtp_buffer_get_seq(packet);
	GST_DEBUG_OBJECT(rtp_fec_dec, "received RTP FEC packet, seqnum %u", seqnum);

	ret = gst_rtp_fec_dec_handle_incoming_packet(rtp_fec_dec, packet, BUFFER_TYPE_FEC);

	g_mutex_unlock(rtp_fec_dec->mutex);
	gst_object_unref(rtp_fec_dec);

	return ret;
}


static void gst_rtp_fec_dec_set_property(GObject *object, guint prop_id, GValue const *value, GParamSpec *pspec)
{
	GstRtpFECDec *rtp_fec_dec;

	GST_OBJECT_LOCK(object);

	rtp_fec_dec = GST_RTP_FEC_DEC(object);

	switch (prop_id)
	{
		case PROP_NUM_MEDIA_PACKETS:
		{
			guint num_media_packets = g_value_get_uint(value);
			GST_DEBUG_OBJECT(rtp_fec_dec, "Set number of media packets to %u", num_media_packets);
			g_mutex_lock(rtp_fec_dec->mutex);
			fec_dec_set_num_media_packets(rtp_fec_dec->dec, num_media_packets);
			g_mutex_unlock(rtp_fec_dec->mutex);
			break;
		}
		case PROP_NUM_FEC_PACKETS:
		{
			guint num_fec_packets = g_value_get_uint(value);
			GST_DEBUG_OBJECT(rtp_fec_dec, "Set number of FEC packets to %u", num_fec_packets);
			g_mutex_lock(rtp_fec_dec->mutex);
			fec_dec_set_num_fec_packets(rtp_fec_dec->dec, num_fec_packets);
			g_mutex_unlock(rtp_fec_dec->mutex);
			break;
		}
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}

	GST_OBJECT_UNLOCK(object);
}


static void gst_rtp_fec_dec_get_property(GObject *object, guint prop_id, GValue *value, GParamSpec *pspec)
{
	GstRtpFECDec *rtp_fec_dec;

	GST_OBJECT_LOCK(object);

	rtp_fec_dec = GST_RTP_FEC_DEC(object);

	switch (prop_id)
	{
		case PROP_NUM_MEDIA_PACKETS:
			g_value_set_uint(value, fec_dec_get_num_media_packets(rtp_fec_dec->dec));
			break;
		case PROP_NUM_FEC_PACKETS:
			g_value_set_uint(value, fec_dec_get_num_fec_packets(rtp_fec_dec->dec));
			break;
		default:
			G_OBJECT_WARN_INVALID_PROPERTY_ID(object, prop_id, pspec);
			break;
	}

	GST_OBJECT_UNLOCK(object);
}


static GstStateChangeReturn gst_rtp_fec_dec_change_state(GstElement *element, GstStateChange transition)
{
	GstStateChangeReturn ret;
	GstRtpFECDec *rtp_fec_dec;
	
	rtp_fec_dec = GST_RTP_FEC_DEC(element);
	ret = GST_ELEMENT_CLASS(parent_class)->change_state(element, transition);

	switch (transition)
	{
		case GST_STATE_CHANGE_PLAYING_TO_PAUSED:
			break;
		case GST_STATE_CHANGE_PAUSED_TO_READY:
			g_mutex_lock(rtp_fec_dec->mutex);
			fec_dec_reset(rtp_fec_dec->dec);
			g_mutex_unlock(rtp_fec_dec->mutex);
			break;
		case GST_STATE_CHANGE_READY_TO_NULL:
			break;
		default:
			break;
	}

	return ret;
}


static void gst_rtp_fec_dec_finalize(GObject *object)
{
	GstRtpFECDec *rtp_fec_dec = GST_RTP_FEC_DEC(object);
	g_mutex_free(rtp_fec_dec->mutex);
	fec_dec_destroy(rtp_fec_dec->dec);
	GST_DEBUG_OBJECT(rtp_fec_dec, "Cleaned up FEC decoder");
	G_OBJECT_CLASS(parent_class)->finalize(object);
}

