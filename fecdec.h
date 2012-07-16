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



#ifndef FECDEC_H
#define FECDEC_H


#include <gst/gst.h>


struct fec_dec_s;
typedef struct fec_dec_s fec_dec;
typedef GstBuffer* (*create_buffer_function)(guint const size_in_bytes, void *data);


fec_dec* fec_dec_create(guint const num_media_packets, guint const num_fec_packets, create_buffer_function const create_buffer, void *create_buffer_data);
void fec_dec_destroy(fec_dec *dec);

void fec_dec_push_media_packet(fec_dec *dec, GstBuffer *packet);
void fec_dec_push_fec_packet(fec_dec *dec, GstBuffer *packet);

gboolean fec_dec_has_recovered_packets(fec_dec *dec);

GstBuffer* fec_dec_pop_recovered_packet(fec_dec *dec);
void fec_dec_flush_recovered_packets(fec_dec *dec);

void fec_dec_set_num_media_packets(fec_dec *dec, guint const num_media_packets);
guint fec_dec_get_num_media_packets(fec_dec *dec);
void fec_dec_set_num_fec_packets(fec_dec *dec, guint const num_fec_packets);
guint fec_dec_get_num_fec_packets(fec_dec *dec);

void fec_dec_reset(fec_dec *dec);


#endif


