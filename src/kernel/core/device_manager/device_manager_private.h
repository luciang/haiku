/*
 * Copyright 2004-2005, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Copyright 2002-04, Thomas Kurschel. All rights reserved.
 *
 * Distributed under the terms of the MIT License.
 */
#ifndef DEVICE_MANAGER_PRIVATE
#define DEVICE_MANAGER_PRIVATE

/*
	Part of Device Manager
	Internal header.
*/

#include <kdevice_manager.h>
#include <util/list.h>

#include <lock.h>

#define GENERATOR_MAX_ID 64
#define UNIVERSAL_SUBDIR "/universal"
#define GENERIC_SUBDIR "/generic"

// driver registration directories
#define PNP_DIR
#define SYSTEM_DRIVER_REGISTRATION "/boot/beos/add-ons/kernel/"PNP_DIR"registration/"
#define COMMON_DRIVER_REGISTRATION "/boot/home/config/add-ons/kernel/"PNP_DIR"registration/"

// module directories
#define SYSTEM_MODULES_DIR "/boot/beos/add-ons/kernel/"
#define COMMON_MODULES_DIR "/boot/home/config/add-ons/kernel/"

#define PNP_BOOT_LINKS "pnp_bootlinks"


// info about ID generator
typedef struct id_generator {
	struct list_link link;
	struct id_generator *prev, *next;
	int ref_count;
	char *name;
	uint32 num_ids;
	uint8 alloc_map[(GENERATOR_MAX_ID + 7) / 8];
} id_generator;


// info about a node attribute
typedef struct device_attr_info {
	struct device_attr_info *prev, *next;
	int32		ref_count;				// reference count
	device_attr	attr;
} device_attr_info;


// I/O resource
typedef struct io_resource_info {
	struct io_resource_info *prev, *next;
	device_node_info	*owner;			// associated node; NULL for temporary allocation
	io_resource			resource;		// info about actual resource
} io_resource_info;


// global lock
// whenever you do something in terms of nodes, grab it
extern benaphore gNodeLock;
// list of nodes (includes removed devices!)
extern device_node_info *gNodeList;

// true, if user addons are disabled via safemode
extern bool disable_useraddons;


// # of allocate_io_resource() calls waiting for a retry
extern int pnp_resource_wait_count;
// semaphore for allocate_io_resource() call retry
extern sem_id pnp_resource_wait_sem;

// list of driver registration directories
extern const char *pnp_registration_dirs[];


// list of I/O memory, I/O ports and ISA DMA channels
extern io_resource_info *io_mem_list, *io_port_list, *isa_dma_list;


// nesting of pnp_load_boot_links calls
int pnp_fs_emulation_nesting;


#ifdef __cplusplus
extern "C" {
#endif

// attributes.c

device_attr_info *pnp_find_attr_nolock(device_node_handle node, const char *name,
	bool recursive, type_code type);
status_t pnp_get_attr_uint8(device_node_handle node, const char *name, 
	uint8 *value, bool recursive);
status_t pnp_get_attr_uint8_nolock(device_node_handle node, const char *name, 
	uint8 *value, bool recursive);
status_t pnp_get_attr_uint16(device_node_handle node, const char *name, 
	uint16 *value, bool recursive);
status_t pnp_get_attr_uint16_nolock(device_node_handle node, const char *name, 
	uint16 *value, bool recursive);
status_t pnp_get_attr_uint32(device_node_handle node, const char *name, 
	uint32 *value, bool recursive);
status_t pnp_get_attr_uint32_nolock(device_node_handle node, const char *name, 
	uint32 *value, bool recursive);
status_t pnp_get_attr_uint64(device_node_handle node, const char *name, 
	uint64 *value, bool recursive);
status_t pnp_get_attr_uint64_nolock(device_node_handle node, const char *name, 
	uint64 *value, bool recursive);
status_t pnp_get_attr_string(device_node_handle node, const char *name, 
	char **value, bool recursive);
status_t pnp_get_attr_string_nolock(device_node_handle node, const char *name, 
	const char **value, bool recursive);
status_t pnp_get_attr_raw(device_node_handle node, const char *name, 
	void **data, size_t *len, bool recursive);
status_t pnp_get_attr_raw_nolock(device_node_handle node, const char *name, 
	const void **data, size_t *len, bool recursive);
void pnp_free_node_attr(device_attr_info *attr);
status_t pnp_duplicate_node_attr(const device_attr *src, device_attr_info **dest_out);
status_t pnp_get_next_attr(device_node_handle node, device_attr_handle *attr);
status_t pnp_release_attr(device_node_handle node, device_attr_handle attr);
status_t pnp_retrieve_attr(device_attr_handle attr, const device_attr **attr_content);
status_t pnp_write_attr(device_node_handle node, const device_attr *attr);
void pnp_remove_attr_int(device_node_handle node, device_attr_info *attr);
status_t pnp_remove_attr(device_node_handle node, const char *name);


// boot_hack.c
#if 0
void pnp_load_boot_links( void );
void pnp_unload_boot_links( void );
char *pnp_boot_safe_realpath( 
	const char *file_name, char *resolved_path );
int pnp_boot_safe_lstat( const char *file_name, struct stat *info );
DIR *pnp_boot_safe_opendir( const char *dirname );
int pnp_boot_safe_closedir( DIR *dirp );
struct dirent *pnp_boot_safe_readdir( DIR *dirp );
#endif

// driver_loader.c

status_t pnp_load_driver(device_node_handle node, void *user_cookie, 
			driver_module_info **interface, void **cookie);
status_t pnp_unload_driver(device_node_handle node);
void pnp_unblock_load(device_node_info *node);
void pnp_load_driver_automatically(device_node_info *node, bool after_rescan);
void pnp_unload_driver_automatically(device_node_info *node, bool before_rescan);


// id_generator.c

int32 pnp_create_id(const char *name);
status_t pnp_free_id(const char *name, uint32 id);
extern status_t id_generator_init(void);


// io_resources.c

status_t pnp_acquire_io_resources(io_resource *resources, io_resource_handle *handles);
status_t pnp_release_io_resources(const io_resource_handle *handles);
void pnp_assign_io_resources(device_node_info *node, const io_resource_handle *handles);
void pnp_release_node_resources(device_node_info *node);


// nodes.c

status_t pnp_alloc_node(const device_attr *attrs, const io_resource_handle *resources,
	device_node_info **new_node);
void pnp_create_node_links(device_node_info *node, device_node_info *parent);
void pnp_add_node_ref(device_node_info *node);
void pnp_remove_node_ref(device_node_info *node);
void pnp_remove_node_ref_nolock(device_node_info *node);
device_node_handle pnp_find_device(device_node_handle parent, const device_attr *attrs);
device_node_handle pnp_get_parent(device_node_handle node);
void dump_device_node_info(device_node_info *node, int32 level);
extern status_t nodes_init(void);


// notifications.c

status_t pnp_notify_probe_by_module(device_node_info *node, 
	const char *consumer_name);
void pnp_notify_unregistration(device_node_info *notify_list);
void pnp_start_hook_call(device_node_info *node);
void pnp_start_hook_call_nolock(device_node_info *node);
void pnp_finish_hook_call(device_node_info *node);
void pnp_finish_hook_call_nolock(device_node_info *node);


// patterns.c

status_t pnp_expand_pattern(device_node_info *node, const char *pattern,
	char *dest, char *buffer, size_t *term_array, int *num_parts);
status_t pnp_expand_pattern_attr(device_node_info *node, const char *attr_name, 
	char **expanded);


// probe.c
status_t pnp_notify_fixed_consumers(device_node_handle node);
status_t pnp_notify_dynamic_consumers(device_node_info *node);


// registration.c

status_t pnp_register_device(device_node_handle parent, const device_attr *attrs,
	const io_resource_handle *resources, device_node_handle *node);
status_t pnp_unregister_device(device_node_handle node);
void pnp_unregister_node_rec(device_node_info *node, device_node_info **dependency_list);
void pnp_unref_unregistered_nodes(device_node_info *node_list);
void pnp_defer_probing_of_children_nolock(device_node_info *node);
void pnp_defer_probing_of_children(device_node_info *node);
void pnp_probe_waiting_children_nolock(device_node_info *node);
void pnp_probe_waiting_children(device_node_info *node);

// root_node.c

extern void pnp_root_init_root(void);
extern void pnp_root_destroy_root(void);
extern void pnp_root_rescan_root(void);

// scan.c

status_t pnp_rescan(device_node_handle node, uint32 depth);
status_t pnp_rescan_int(device_node_info *node, uint32 depth, 
	bool ignore_fixed_consumers);
status_t pnp_initial_scan(device_node_info *node);

#ifdef __cplusplus
}
#endif

#endif	/* DEVICE_MANAGER_PRIVATE_H */
