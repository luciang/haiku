/* net_stack.h
 * definitions needed by all network stack modules
 */

#ifndef OBOS_NET_STACK_H
#define OBOS_NET_STACK_H

#include <drivers/module.h>

#include "memory_pool.h"

#ifdef __cplusplus
extern "C" {
#endif

// Networking buffer(s) definition
typedef struct net_buffer net_buffer;
typedef struct net_buffer_queue net_buffer_queue;
typedef void (*buffer_chunk_free_func)(void * arg1, ...); // buffer chunk free callback prototype

// Networking timers definitions
typedef struct net_timer net_timer;
typedef void (*net_timer_func)(net_timer *timer, void *cookie);	// timer callback prototype

// Generic lockers support

typedef int benaphore;
#define create_benaphore(a, b)
#define delete_benaphore(a)
#define lock_benaphore(a)
#define unlock_benaphore(a)

#include "net_interface.h"


// Network stack main module definition

struct net_stack_module_info {
	module_info	module;

	status_t (*start)(void);
	status_t (*stop)(void);
	
	/*
	 * Socket layer
	 */
	 
	/*
	 * Data-Link layer
	 */
	
	status_t (*register_interface)(ifnet_t *ifnet);
	status_t (*unregister_interface)(ifnet_t *ifnet);

	/*
	 * Buffer(s) support
	 */

	net_buffer *	(*new_buffer)(void);
	status_t 		(*delete_buffer)(net_buffer *buffer, bool interrupt_safe);

	net_buffer *	(*duplicate_buffer)(net_buffer *from);
	net_buffer *	(*clone_buffer)(net_buffer *from);
	net_buffer *	(*split_buffer)(net_buffer *from, uint32 offset);


	// specials offsets for add_to_buffer()	
	#define BUFFER_BEGIN 0
	#define BUFFER_END 	 0xFFFFFFFF
	status_t		(*add_to_buffer)(net_buffer *buffer, uint32 offset, const void *data, uint32 bytes, buffer_chunk_free_func freethis);
	status_t		(*remove_from_buffer)(net_buffer *buffer, uint32 offset, uint32 bytes);

	status_t 		(*attach_buffer_free_element)(net_buffer *buffer, void *arg1, void *arg2, buffer_chunk_free_func freethis);

	uint32 			(*read_buffer)(net_buffer *buffer, uint32 offset, void *data, uint32 bytes);
	uint32 			(*write_buffer)(net_buffer *buffer, uint32 offset, const void *data, uint32 bytes);
	
	void			(*dump_buffer)(net_buffer *buffer);

	// Buffer queues support
	net_buffer_queue * (*new_buffer_queue)(size_t max_bytes);
	status_t 		(*delete_buffer_queue)(net_buffer_queue *queue);

	status_t		(*empty_buffer_queue)(net_buffer_queue *queue);
	status_t 		(*enqueue_buffer)(net_buffer_queue *queue, net_buffer *buffer);
	size_t  		(*dequeue_buffer)(net_buffer_queue *queue, net_buffer **buffer, bigtime_t timeout, bool peek); 

	// Timers support
	
	net_timer *	(*new_timer)(void);
	status_t	(*delete_timer)(net_timer *timer);
	status_t	(*start_timer)(net_timer *timer, net_timer_func func, void *cookie, bigtime_t period);
	status_t	(*cancel_timer)(net_timer *timer);
	status_t	(*timer_appointment)(net_timer *timer, bigtime_t *period, bigtime_t *when);
	
	// Lockers
	
	// Memory Pools support
	memory_pool *	(*new_pool)(size_t node_size, uint32 node_count);
	status_t		(*delete_pool)(memory_pool *pool);
	void *			(*new_pool_node)(memory_pool *pool);
	status_t		(*delete_pool_node)(memory_pool *pool, void *node);
	status_t		(*for_each_pool_node)(memory_pool *pool, pool_iterate_func iterate, void *cookie);
	
};

#define NET_STACK_MODULE_NAME	"network/stack/v1"

#ifdef __cplusplus
}
#endif

#endif /* OBOS_NET_STACK_H */
