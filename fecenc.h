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



#ifndef FECENC_H
#define FECENC_H


#include <gst/gst.h>


struct fec_enc_s;
typedef struct fec_enc_s fec_enc;


fec_enc* fec_enc_create(guint const num_media_packets, guint const num_fec_packets, guint const payload_type, guint const seqnum_offset);
void fec_enc_destroy(fec_enc *enc);

void fec_enc_push_media_packet(fec_enc *enc, GstBuffer *packet);

GstBuffer* fec_enc_pop_fec_packet(fec_enc *enc);

void fec_enc_set_payload_type(fec_enc *enc, guint const payload_type);
guint fec_enc_get_payload_type(fec_enc *enc);

void fec_enc_set_num_media_packets(fec_enc *enc, guint const num_media_packets);
guint fec_enc_get_num_media_packets(fec_enc *enc);
void fec_enc_set_num_fec_packets(fec_enc *enc, guint const num_fec_packets);
guint fec_enc_get_num_fec_packets(fec_enc *enc);

gboolean fec_enc_is_media_packet_list_full(fec_enc *enc);
gboolean fec_enc_has_fec_packets(fec_enc *enc);

void fec_enc_reset(fec_enc *enc);


#endif

