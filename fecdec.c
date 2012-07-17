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
#include <string.h>
#include <openfec/lib_common/of_openfec_api.h>
#include <gst/rtp/gstrtpbuffer.h>
#include "fecdec.h"


#define RTP_FEC_HEADER_SIZE 12


struct fec_dec_s
{
	guint num_media_packets;
	guint num_fec_packets;

	guint max_packet_size;

	create_buffer_function create_buffer;
	void *create_buffer_data;

	guint cur_snbase, blacklisted_snbase;
	gboolean has_snbase;

	GQueue *media_packets;
	GQueue *fec_packets;
	GQueue *recovered_packets;

	GHashTable *media_packet_set;
	GHashTable *fec_packet_set;

	guint32 received_media_packet_mask;
	guint num_received_media_packets;
	guint num_received_fec_packets;
};



static void fec_dec_clear_packet(gpointer data, gpointer user_data);
static void fec_dec_cleanup(fec_dec *dec);
static void fec_dec_check_state(fec_dec *dec);
static gboolean fec_dec_all_media_packets_present(fec_dec *dec);
static gboolean fec_dec_can_recover_packets(fec_dec *dec);
static void fec_dec_recover_packets(fec_dec *dec);



static void custom_hash_table_add(GHashTable *hash_table, gpointer key)
{
	g_hash_table_replace(hash_table, key, key);
}



fec_dec* fec_dec_create(guint const num_media_packets, guint const num_fec_packets, create_buffer_function const create_buffer, void *create_buffer_data)
{
	fec_dec *dec = malloc(sizeof(fec_dec));

	dec->num_media_packets = num_media_packets;
	dec->num_fec_packets = num_fec_packets;
	dec->max_packet_size = 0;
	dec->create_buffer = create_buffer;
	dec->create_buffer_data = create_buffer_data;
	dec->cur_snbase = 0;
	dec->blacklisted_snbase = 0;
	dec->has_snbase = FALSE;
	dec->media_packets = g_queue_new();
	dec->fec_packets = g_queue_new();
	dec->recovered_packets = g_queue_new();
	dec->media_packet_set = g_hash_table_new(g_direct_hash, g_direct_equal);
	dec->fec_packet_set = g_hash_table_new(g_direct_hash, g_direct_equal);
	dec->received_media_packet_mask = 0;
	dec->num_received_media_packets = 0;
	dec->num_received_fec_packets = 0;

	return dec;
}


void fec_dec_destroy(fec_dec *dec)
{
	fec_dec_reset(dec);
	g_queue_free(dec->media_packets);
	g_queue_free(dec->fec_packets);
	g_queue_free(dec->recovered_packets);
	g_hash_table_destroy(dec->media_packet_set);
	g_hash_table_destroy(dec->fec_packet_set);
	free(dec);
}


static void fec_dec_clear_packet(gpointer data, gpointer user_data)
{
	GstBuffer *buffer;
	user_data = user_data; /* shut up compiler warning about unused arguments */
	buffer = data;
	gst_buffer_unref(buffer);
}


static void fec_dec_cleanup(fec_dec *dec)
{
	/*
	NOT clearing recovered_packets here
	cleanup is called right after recovery was completed,
	the user is then supposed to call fec_dec_pop_recovered_packet() to get
	all recovered packets.

	Also, blacklisted_snbase is used to drop any FEC packets that may come up with the current snbase.
	*/
	dec->blacklisted_snbase = dec->cur_snbase;
	dec->has_snbase = FALSE;
	dec->received_media_packet_mask = 0;
	dec->num_received_media_packets = 0;
	dec->num_received_fec_packets = 0;
	dec->max_packet_size = 0;
	g_queue_foreach(dec->media_packets, fec_dec_clear_packet, NULL);
	g_queue_foreach(dec->fec_packets, fec_dec_clear_packet, NULL);
	g_queue_clear(dec->media_packets);
	g_queue_clear(dec->fec_packets);
	g_hash_table_remove_all(dec->media_packet_set);
	g_hash_table_remove_all(dec->fec_packet_set);
}


static gboolean fec_dec_all_media_packets_present(fec_dec *dec)
{
	return (dec->received_media_packet_mask == ((1ul << (dec->num_media_packets)) - 1));
}


static gboolean fec_dec_can_recover_packets(fec_dec *dec)
{
	/*
	TODO: this should make an OpenFEC call; the line below assumes Reed-Solomon is used
	*/
	return (dec->num_received_media_packets > 0) && ((dec->num_received_media_packets + dec->num_received_fec_packets) >= dec->num_media_packets);
}


static void* fec_dec_source_packet_cb(void *context, UINT32 size, UINT32 esi)
{
	GstBuffer *packet;
	fec_dec *dec;

	esi = esi; /* shut up compiler warning about unused argument */
	
	dec = context;
	packet = dec->create_buffer(size, dec->create_buffer_data);
	g_queue_push_tail(dec->recovered_packets, packet);

	return GST_BUFFER_DATA(packet);
}


static inline guint32 fec_dec_correct_seqnum(fec_dec *dec, guint16 const seqnum)
{
	guint32 corrected_seqnum = seqnum;
	guint32 snbase = dec->cur_snbase;
	guint32 snend = snbase + dec->num_media_packets;

	if (snend > 65536)
	{
		if (corrected_seqnum < snbase)
			corrected_seqnum += 65536;
	}

	return corrected_seqnum;
}


static void fec_dec_recover_packets(fec_dec *dec)
{
	of_session_t *session;
	of_rs_parameters_t params;
	void **encoding_symbol_tab;
	guint i;
	GList *link;

	assert(dec->has_snbase);
	assert(dec->max_packet_size > 0);

	encoding_symbol_tab = malloc(sizeof(void*) * (dec->num_media_packets + dec->num_fec_packets));
	memset(encoding_symbol_tab, 0, sizeof(void*) * (dec->num_media_packets + dec->num_fec_packets));

	for (link = g_queue_peek_head_link(dec->media_packets); link != NULL; link = link->next)
	{
		GstBuffer *media_packet;
		guint32 seqnum;

		media_packet = link->data;
		seqnum = fec_dec_correct_seqnum(dec, gst_rtp_buffer_get_seq(media_packet));

		encoding_symbol_tab[seqnum - dec->cur_snbase] = GST_BUFFER_DATA(media_packet);
	}

	for (link = g_queue_peek_head_link(dec->fec_packets); link != NULL; link = link->next)
	{
		GstBuffer *packet;
		guint8 *fec_data;
		guint8 esi;

		packet = link->data;
		fec_data = GST_BUFFER_DATA(packet) + gst_rtp_buffer_get_header_len(packet);
		esi = fec_data[12];

		encoding_symbol_tab[esi + dec->num_media_packets] = fec_data + RTP_FEC_HEADER_SIZE + 1;
	}

	params.nb_source_symbols = dec->num_media_packets;
	params.nb_repair_symbols = dec->num_fec_packets;
	params.encoding_symbol_length = dec->max_packet_size;

	of_create_codec_instance(&session, OF_CODEC_REED_SOLOMON_GF_2_8_STABLE, OF_DECODER, 0);
	of_set_fec_parameters(session, (of_parameters_t*)(&params));
	of_set_callback_functions(session, fec_dec_source_packet_cb, NULL, dec);

#if 1
	for (i = 0; i < dec->num_media_packets + dec->num_fec_packets; ++i)
	{
		if (encoding_symbol_tab[i] != 0)
			of_decode_with_new_symbol(session, encoding_symbol_tab[i], i);
	}
#else
	of_set_available_symbols(session, encoding_symbol_tab);
#endif

	if (!of_is_decoding_complete(session))
	{
		of_finish_decoding(session);
	}

	free(encoding_symbol_tab);
	of_release_codec_instance(session);
}


static void fec_dec_check_state(fec_dec *dec)
{
	if (fec_dec_all_media_packets_present(dec))
	{
		GST_DEBUG("All %u media packets received, no recovery operation necessary", dec->num_media_packets);
		fec_dec_cleanup(dec);
	}
	else if (fec_dec_can_recover_packets(dec))
	{
		GST_DEBUG("Recovering %u media packets", dec->num_media_packets - dec->num_received_media_packets);
		fec_dec_recover_packets(dec);
		fec_dec_cleanup(dec);
	}
}


void fec_dec_push_media_packet(fec_dec *dec, GstBuffer *packet)
{
	guint32 original_seqnum;
	original_seqnum = gst_rtp_buffer_get_seq(packet);

	if (g_hash_table_lookup_extended(dec->media_packet_set, GINT_TO_POINTER(original_seqnum), NULL, NULL))
	{
		GST_DEBUG("Media packet with seqnum %u is already in queue - discarding duplicate", original_seqnum);
		return;
	}

	if (dec->has_snbase)
	{
		guint32 corrected_seqnum;
		corrected_seqnum = fec_dec_correct_seqnum(dec, original_seqnum);

		GST_DEBUG("Pushing media packet with seqnum %u, current snbase is %u", original_seqnum, dec->cur_snbase);

		if ((corrected_seqnum - (guint32)(dec->cur_snbase)) >= dec->num_media_packets)
		{
			GST_DEBUG("Distance between FEC packets and incoming media packets is too large - purging %u FEC packets and setting has_snbase to FALSE", dec->num_received_fec_packets);

			dec->has_snbase = FALSE;
			dec->blacklisted_snbase = dec->cur_snbase;
			g_queue_foreach(dec->fec_packets, fec_dec_clear_packet, NULL);
			g_queue_clear(dec->fec_packets);
			g_hash_table_remove_all(dec->fec_packet_set);
			dec->num_received_fec_packets = 0;

			GST_DEBUG("Pushing media packet with seqnum %u, no current snbase set", original_seqnum);
			g_queue_push_tail(dec->media_packets, gst_buffer_ref(packet));
			custom_hash_table_add(dec->media_packet_set, GINT_TO_POINTER(original_seqnum));
			++dec->num_received_media_packets;
		}
		else if ((corrected_seqnum >= (guint32)(dec->cur_snbase)) && (corrected_seqnum <= ((guint32)(dec->cur_snbase) + ((guint32)(dec->num_media_packets)) - 1)))
		{
			g_queue_push_tail(dec->media_packets, gst_buffer_ref(packet));
			custom_hash_table_add(dec->media_packet_set, GINT_TO_POINTER(original_seqnum));
			++dec->num_received_media_packets;
			dec->received_media_packet_mask |= (1ul << (corrected_seqnum - dec->cur_snbase));
			dec->max_packet_size = MAX(dec->max_packet_size, GST_BUFFER_SIZE(packet));

			fec_dec_check_state(dec);
		}
		else
		{
			GST_DEBUG("Received media packet with seqnum %u outside bounds [%u, %u] - pushing aborted", original_seqnum, dec->cur_snbase, dec->cur_snbase + dec->num_media_packets - 1);
		}
	}
	else
	{
		GST_DEBUG("Pushing media packet with seqnum %u, no current snbase set", original_seqnum);
		g_queue_push_tail(dec->media_packets, gst_buffer_ref(packet));
		custom_hash_table_add(dec->media_packet_set, GINT_TO_POINTER(original_seqnum));
		++dec->num_received_media_packets;
	}

	if (dec->num_received_media_packets > dec->num_media_packets)
	{
		guint i;

		GST_DEBUG("Too many media packets in queue - deleting the %u oldest packets", dec->num_received_media_packets - dec->num_media_packets);

		for (i = dec->num_media_packets; i < dec->num_received_media_packets; ++i)
		{
			GstBuffer *media_packet;
			guint16 seqnum;

			media_packet = g_queue_pop_head(dec->media_packets);
			seqnum = gst_rtp_buffer_get_seq(packet);
			g_hash_table_remove(dec->media_packet_set, GINT_TO_POINTER(seqnum));

			gst_buffer_unref(media_packet);
		}

		dec->num_received_media_packets = dec->num_media_packets;
	}
}


void fec_dec_push_fec_packet(fec_dec *dec, GstBuffer *packet)
{
	guint8 *fec_data;
	guint32 snbase;
	guint16 seqnum;
	GList *link;

	fec_data = GST_BUFFER_DATA(packet) + gst_rtp_buffer_get_header_len(packet);
	snbase = (((guint16)(fec_data[0])) << 8) | (((guint16)(fec_data[1])) << 0);
	seqnum = gst_rtp_buffer_get_seq(packet);

	GST_DEBUG("Received FEC packet, snbase %u, index %u, seqnum %u", snbase, (guint)(fec_data[12]), seqnum);

	if (snbase == dec->blacklisted_snbase)
	{
		GST_DEBUG("Ignoring FEC packet since data from this snbase has been restored already (= the packet is not needed)");
		return;
	}

	if (g_hash_table_lookup_extended(dec->fec_packet_set, GINT_TO_POINTER(seqnum), NULL, NULL))
	{
		GST_DEBUG("FEC packet with seqnum %u is already in queue - discarding duplicate", seqnum);
		return;
	}

	if (dec->cur_snbase != snbase)
	{
		GST_DEBUG("snbase changed from %u to %u - purging FEC queue (%u FEC packets and %u media packets present)", dec->cur_snbase, snbase, dec->num_received_fec_packets, dec->num_received_media_packets);
		g_queue_foreach(dec->fec_packets, fec_dec_clear_packet, NULL);
		g_queue_clear(dec->fec_packets);
		g_hash_table_remove_all(dec->fec_packet_set);
		dec->num_received_fec_packets = 0;
	}

	dec->cur_snbase = snbase;
	dec->has_snbase = TRUE;
	dec->received_media_packet_mask = 0;
	dec->num_received_media_packets = 0;
	dec->max_packet_size = 0;
	g_queue_push_tail(dec->fec_packets, gst_buffer_ref(packet));
	custom_hash_table_add(dec->fec_packet_set, GINT_TO_POINTER(seqnum));
	++dec->num_received_fec_packets;

	for (link = g_queue_peek_head_link(dec->media_packets); link != NULL;)
	{
		GstBuffer *media_packet;
		guint32 corrected_seqnum;

		media_packet = link->data;
		seqnum = gst_rtp_buffer_get_seq(media_packet);
		corrected_seqnum = fec_dec_correct_seqnum(dec, seqnum);

		if ((corrected_seqnum < snbase) || (corrected_seqnum > (snbase + dec->num_media_packets - 1)))
		{
			GList *old_link;

			GST_DEBUG("Found media packet with seqnum %u outside bounds [%u, %u] - purging", seqnum, snbase, snbase + dec->num_media_packets - 1);
			g_hash_table_remove(dec->media_packet_set, GINT_TO_POINTER(seqnum));
			gst_buffer_unref(media_packet);

			old_link = link;
			link = link->next;
			g_queue_delete_link(dec->media_packets, old_link);
		}
		else
		{
			dec->max_packet_size = MAX(dec->max_packet_size, GST_BUFFER_SIZE(media_packet));
			dec->received_media_packet_mask |= (1ul << (corrected_seqnum - dec->cur_snbase));
			++dec->num_received_media_packets;
			link = link->next;
		}
	}

	fec_dec_check_state(dec);
}


gboolean fec_dec_has_recovered_packets(fec_dec *dec)
{
	return !g_queue_is_empty(dec->recovered_packets);
}


GstBuffer* fec_dec_pop_recovered_packet(fec_dec *dec)
{
	return g_queue_pop_tail(dec->recovered_packets);
}


void fec_dec_flush_recovered_packets(fec_dec *dec)
{
	g_queue_foreach(dec->recovered_packets, fec_dec_clear_packet, NULL);
	g_queue_clear(dec->recovered_packets);
}


void fec_dec_set_num_media_packets(fec_dec *dec, guint const num_media_packets)
{
	fec_dec_reset(dec);
	dec->num_media_packets = num_media_packets;
}


guint fec_dec_get_num_media_packets(fec_dec *dec)
{
	return dec->num_media_packets;
}


void fec_dec_set_num_fec_packets(fec_dec *dec, guint const num_fec_packets)
{
	fec_dec_reset(dec);
	dec->num_fec_packets = num_fec_packets;
}


guint fec_dec_get_num_fec_packets(fec_dec *dec)
{
	return dec->num_fec_packets;
}


void fec_dec_reset(fec_dec *dec)
{
	fec_dec_cleanup(dec);
	fec_dec_flush_recovered_packets(dec);
}

