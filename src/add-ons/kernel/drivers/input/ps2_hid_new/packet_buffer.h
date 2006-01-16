/*
 * Copyright 2005, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef PACKET_BUFFER_H
#define PACKET_BUFFER_H


#include <OS.h>


struct packet_buffer;
typedef struct packet_buffer packet_buffer;

#ifdef __cplusplus
extern "C" {
#endif

struct packet_buffer *create_packet_buffer(size_t size);
void delete_packet_buffer(struct packet_buffer *buffer);

void packet_buffer_clear(struct packet_buffer *buffer);
size_t packet_buffer_readable(struct packet_buffer *buffer);
size_t packet_buffer_writable(struct packet_buffer *buffer);
void packet_buffer_flush(struct packet_buffer *buffer, size_t bytes);
size_t packet_buffer_read(struct packet_buffer *buffer, uint8 *data, size_t length);
size_t packet_buffer_write(struct packet_buffer *buffer, const uint8 *data, size_t length);

#ifdef __cplusplus
}
#endif

#endif	/* PACKET_BUFFER_H */
