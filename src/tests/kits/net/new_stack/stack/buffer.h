/* buffer.h
 * private definitions for network buffers support
 */

#ifndef OBOS_NET_STACK_BUFFER_H
#define OBOS_NET_STACK_BUFFER_H

#include <SupportDefs.h>

#include "net_stack.h"

#ifdef __cplusplus
extern "C" {
#endif

extern status_t 	start_buffers_service();
extern status_t 	stop_buffers_service();

// Network buffer(s)
extern net_buffer * 	new_buffer(void);
extern status_t 		delete_buffer(net_buffer *buffer, bool interrupt_safe);

extern net_buffer *		duplicate_buffer(net_buffer *from);
extern net_buffer *		clone_buffer(net_buffer *from);
extern net_buffer *		split_buffer(net_buffer *from, uint32 offset);

extern	status_t		add_to_buffer(net_buffer *buffer, uint32 offset, const void *data, uint32 bytes, buffer_chunk_free_func freethis);
extern	status_t		remove_from_buffer(net_buffer *buffer, uint32 offset, uint32 bytes);

extern	status_t 		attach_buffer_free_element(net_buffer *buffer, void *arg1, void *arg2, buffer_chunk_free_func freethis);

extern uint32 			read_buffer(net_buffer *buffer, uint32 offset, void *data, uint32 bytes);
extern uint32 			write_buffer(net_buffer *buffer, uint32 offset, const void *data, uint32 bytes);

extern void				dump_buffer(net_buffer *buffer);

// Network buffer(s) queue(s)
extern net_buffer_queue * 	new_buffer_queue(size_t max_bytes);
extern status_t 			delete_buffer_queue(net_buffer_queue *queue);

extern status_t		empty_buffer_queue(net_buffer_queue *queue);
extern status_t 	enqueue_buffer(net_buffer_queue *queue, net_buffer *buffer);
extern size_t  		dequeue_buffer(net_buffer_queue *queue, net_buffer **buffer, bigtime_t timeout, bool peek); 

#ifdef __cplusplus
}
#endif

#endif	// OBOS_NET_STACK_BUFFER_H
