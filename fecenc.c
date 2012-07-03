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
#include <openfec/lib_common/of_openfec_api.h>
#include <gst/rtp/gstrtpbuffer.h>
#include "fecenc.h"


/* TODO: currently, this code assumes all incoming media packets are of same size */


#define RTP_FEC_HEADER_SIZE 12


struct fec_enc_s
{
	guint num_media_packets;
	guint num_fec_packets;
	guint payload_type;
	guint seqnum_offset;
	guint current_fec_seqnum;
	guint max_packet_size;
	guint cur_num_media_packets;

	GQueue *media_packets;
	GQueue *fec_packets;
};



static void fec_enc_calculate_fec_packets(fec_enc *enc);
static void fec_enc_clear_packet(gpointer data, gpointer user_data);


fec_enc* fec_enc_create(guint const num_media_packets, guint const num_fec_packets, guint const payload_type, guint const seqnum_offset)
{
	fec_enc *enc = malloc(sizeof(fec_enc));

	enc->num_media_packets = num_media_packets;
	enc->num_fec_packets = num_fec_packets;
	enc->payload_type = payload_type;
	enc->seqnum_offset = seqnum_offset;
	enc->current_fec_seqnum = seqnum_offset;
	enc->media_packets = g_queue_new();
	enc->fec_packets = g_queue_new();
	enc->max_packet_size = 0;
	enc->cur_num_media_packets = 0;

	return enc;
}


static void fec_enc_clear_packet(gpointer data, gpointer user_data)
{
	GstBuffer *buffer;
	user_data = user_data; /* shut up compiler warning about unused arguments */
	buffer = data;
	gst_buffer_unref(buffer);
}

void fec_enc_destroy(fec_enc *enc)
{
	fec_enc_reset(enc);
	g_queue_free(enc->media_packets);
	g_queue_free(enc->fec_packets);
	GST_DEBUG("Destroyed FEC encoder %p", enc);
	free(enc);
}


void fec_enc_push_media_packet(fec_enc *enc, GstBuffer *packet)
{
	if (fec_enc_has_fec_packets(enc))
	{
		GST_DEBUG("Not pushing media packet - FEC packets are still present in the FEC queue");
		return;
	}

	if (!fec_enc_is_media_packet_list_full(enc))
	{
		g_queue_push_tail(enc->media_packets, gst_buffer_ref(packet));
		enc->max_packet_size = MAX(enc->max_packet_size, GST_BUFFER_SIZE(packet));
		++enc->cur_num_media_packets;
		GST_DEBUG("Pushed media packet to queue, which now contains %u packets", enc->cur_num_media_packets);
	}

	if (fec_enc_is_media_packet_list_full(enc))
	{
		GST_DEBUG("Media packet queue full, calculating FEC packets");
		fec_enc_calculate_fec_packets(enc);
		enc->max_packet_size = 0;
		while (!g_queue_is_empty(enc->media_packets))
		{
			GstBuffer *packet = g_queue_pop_head(enc->media_packets);
			gst_buffer_unref(packet);
		}
		enc->cur_num_media_packets = 0;
	}
}


GstBuffer* fec_enc_pop_fec_packet(fec_enc *enc)
{
	return g_queue_pop_head(enc->fec_packets);
}


void fec_enc_set_payload_type(fec_enc *enc, guint const payload_type)
{
	enc->payload_type = payload_type;
}


guint fec_enc_get_payload_type(fec_enc *enc)
{
	return enc->payload_type;
}


void fec_enc_set_num_media_packets(fec_enc *enc, guint const num_media_packets)
{
	fec_enc_reset(enc);
	enc->num_media_packets = num_media_packets;
}


guint fec_enc_get_num_media_packets(fec_enc *enc)
{
	return enc->num_media_packets;
}


void fec_enc_set_num_fec_packets(fec_enc *enc, guint const num_fec_packets)
{
	fec_enc_reset(enc);
	enc->num_fec_packets = num_fec_packets;
}


guint fec_enc_get_num_fec_packets(fec_enc *enc)
{
	return enc->num_fec_packets;
}


gboolean fec_enc_is_media_packet_list_full(fec_enc *enc)
{
	return enc->cur_num_media_packets >= enc->num_media_packets;
}


gboolean fec_enc_has_fec_packets(fec_enc *enc)
{
	return !g_queue_is_empty(enc->fec_packets);
}


void fec_enc_reset(fec_enc *enc)
{
	g_queue_foreach(enc->media_packets, fec_enc_clear_packet, NULL);
	g_queue_foreach(enc->fec_packets, fec_enc_clear_packet, NULL);
	g_queue_clear(enc->media_packets);
	g_queue_clear(enc->fec_packets);
	enc->max_packet_size = 0;
	enc->cur_num_media_packets = 0;
}





static void fec_enc_calculate_fec_packets(fec_enc *enc)
{
	of_session_t *session;
	void **encoding_symbol_tab;
	of_rs_parameters_t params;
	guint32 timestamp;
	guint32 ssrc;
	guint16 snbase;
	guint32 mask;
	guint i;

	assert(enc->num_media_packets == enc->cur_num_media_packets);

	mask = (1ul << (enc->num_media_packets)) - 1;

	params.nb_source_symbols = enc->num_media_packets;
	params.nb_repair_symbols = enc->num_fec_packets;
	params.encoding_symbol_length = enc->max_packet_size;

	of_create_codec_instance(&session, OF_CODEC_REED_SOLOMON_GF_2_8_STABLE, OF_ENCODER, 0);
	of_set_fec_parameters(session, (of_parameters_t*)(&params));

	GST_DEBUG("Created OpenFEC session");

	encoding_symbol_tab = malloc(sizeof(void*) * (enc->num_media_packets + enc->num_fec_packets));

	{
		GstBuffer *first_packet = g_queue_peek_head_link(enc->media_packets)->data;
		ssrc = gst_rtp_buffer_get_ssrc(first_packet);
		timestamp = gst_rtp_buffer_get_timestamp(first_packet);
		snbase = gst_rtp_buffer_get_seq(first_packet);
		GST_DEBUG("Using SSRC %u, timestamp %u, snbase %u for FEC packets", ssrc, timestamp, snbase);
	}

	{
		GList *link;
		guint32 cnt = 0;
		for (link = g_queue_peek_head_link(enc->media_packets); link != NULL; link = link->next)
		{
			GstBuffer *packet = link->data;
			encoding_symbol_tab[cnt++] = GST_BUFFER_DATA(packet);
		}
	}

	for (i = 0; i < enc->num_fec_packets; ++i)
	{
		GstBuffer *fec_packet;
		guint8 *fec_data;

		/* +1 to make room for the FEC packet index byte */
		fec_packet = gst_rtp_buffer_new_allocate(RTP_FEC_HEADER_SIZE + 1 + enc->max_packet_size, 0, 0);

		gst_rtp_buffer_set_version(fec_packet, GST_RTP_VERSION);
		gst_rtp_buffer_set_ssrc(fec_packet, ssrc);
		gst_rtp_buffer_set_seq(fec_packet, enc->current_fec_seqnum++);
		gst_rtp_buffer_set_timestamp(fec_packet, timestamp);
		gst_rtp_buffer_set_payload_type(fec_packet, enc->payload_type);

		/*
		The FEC data is placed directly after the RTP header, and includes the FEC header
		and the payload
		-> skip the RTP header
		*/
		fec_data = GST_BUFFER_DATA(fec_packet) + gst_rtp_buffer_get_header_len(fec_packet);

		/*  FEC header:
		 *   0                   1                   2                   3
		 *   0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
		 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		 *  |      SN base                  |        length recovery        |
		 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		 *  |E| PT recovery |                 mask                          |
		 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		 *  |                          TS recovery                          |
		 *  +-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
		 */

		fec_data[0] = (snbase >> 8) & 0xff;
		fec_data[1] = (snbase >> 0) & 0xff;

		fec_data[2] = (enc->max_packet_size >> 8) & 0xff;
		fec_data[3] = (enc->max_packet_size >> 0) & 0xff;

		fec_data[4] = enc->payload_type & 0x7f;

		fec_data[5] = (mask >> 16) & 0xff;
		fec_data[6] = (mask >> 8) & 0xff;
		fec_data[7] = (mask >> 0) & 0xff;

		fec_data[8] = (timestamp >> 24) & 0xff;
		fec_data[9] = (timestamp >> 16) & 0xff;
		fec_data[10] = (timestamp >> 8) & 0xff;
		fec_data[11] = (timestamp >> 0) & 0xff;

		fec_data[12] = i;

		g_queue_push_tail(enc->fec_packets, fec_packet);

		/*
		The payload OpenFEC shall write to lies beyond the FEC header (12 byte) and the index byte
		*/
		encoding_symbol_tab[i + enc->num_media_packets] = fec_data + RTP_FEC_HEADER_SIZE + 1;
	}

	for (i = 0; i < enc->num_fec_packets; ++i)
	{
		of_build_repair_symbol(session, encoding_symbol_tab, i + enc->num_media_packets);
	}
	GST_DEBUG("Calculated FEC packets");

	free(encoding_symbol_tab);
	of_release_codec_instance(session);

}

