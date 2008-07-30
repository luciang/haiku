/*
 * Copyright 2005, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef RING_BUFFER_H
#define RING_BUFFER_H


#include <OS.h>


struct ring_buffer {
	int32		first;
	int32		in;
	int32		size;
	uint8		buffer[0];
};


#ifdef __cplusplus
extern "C" {
#endif

struct ring_buffer *create_ring_buffer(size_t size);
void delete_ring_buffer(struct ring_buffer *buffer);

void ring_buffer_clear(struct ring_buffer *buffer);
size_t ring_buffer_readable(struct ring_buffer *buffer);
size_t ring_buffer_writable(struct ring_buffer *buffer);
void ring_buffer_flush(struct ring_buffer *buffer, size_t bytes);
size_t ring_buffer_read(struct ring_buffer *buffer, uint8 *data, ssize_t length);
size_t ring_buffer_write(struct ring_buffer *buffer, const uint8 *data, ssize_t length);
ssize_t ring_buffer_user_read(struct ring_buffer *buffer, uint8 *data, ssize_t length);
ssize_t ring_buffer_user_write(struct ring_buffer *buffer, const uint8 *data, ssize_t length);

#ifdef __cplusplus
}
#endif

#endif	/* RING_BUFFER_H */
