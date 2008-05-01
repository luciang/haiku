/*
 * Copyright 2003-2007, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/*! User Runtime Loader support in the kernel */


#include <KernelExport.h>

#include <kernel.h>
#include <kimage.h>
#include <kscheduler.h>
#include <lock.h>
#include <team.h>
#include <thread.h>
#include <thread_types.h>
#include <user_debugger.h>

#include <stdlib.h>
#include <string.h>


//#define TRACE_IMAGE
#ifdef TRACE_IMAGE
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif

#define ADD_DEBUGGER_COMMANDS

struct image {
	struct image	*next;
	struct image	*prev;
	image_info		info;
};


static image_id sNextImageID = 1;
static mutex sImageMutex;


/*!	Registers an image with the specified team.
*/
image_id
register_image(struct team *team, image_info *_info, size_t size)
{
	image_id id = atomic_add(&sNextImageID, 1);
	struct image *image;

	image = malloc(sizeof(struct image));
	if (image == NULL)
		return B_NO_MEMORY;

	memcpy(&image->info, _info, sizeof(image_info));

	mutex_lock(&sImageMutex);

	image->info.id = id;
	list_add_item(&team->image_list, image);

	mutex_unlock(&sImageMutex);

	TRACE(("register_image(team = %p, image id = %ld, image = %p\n", team, id, image));
	return id;
}


/*!	Unregisters an image from the specified team.
*/
status_t
unregister_image(struct team *team, image_id id)
{
	status_t status = B_ENTRY_NOT_FOUND;
	struct image *image = NULL;

	mutex_lock(&sImageMutex);

	while ((image = list_get_next_item(&team->image_list, image)) != NULL) {
		if (image->info.id == id) {
			list_remove_link(image);
			status = B_OK;
			break;
		}
	}

	mutex_unlock(&sImageMutex);

	if (status == B_OK) {
		// notify the debugger
		user_debug_image_deleted(&image->info);

		free(image);
	}

	return status;
}


/*!	Counts the registered images from the specified team.
	The team lock must be hold when you call this function.
*/
int32
count_images(struct team *team)
{
	struct image *image = NULL;
	int32 count = 0;

	while ((image = list_get_next_item(&team->image_list, image)) != NULL) {
		count++;
	}

	return count;
}


/*!	Removes all images from the specified team. Must only be called
	with the team lock hold or a team that has already been removed
	from the list (in thread_exit()).
*/
status_t
remove_images(struct team *team)
{
	struct image *image;

	ASSERT(team != NULL);

	while ((image = list_remove_head_item(&team->image_list)) != NULL) {
		free(image);
	}
	return B_OK;
}


/*!	Unlike in BeOS, this function only returns images that belong to the
	current team.
	TODO: we might want to rethink that one day...
*/
status_t
_get_image_info(image_id id, image_info *info, size_t size)
{
	status_t status = B_ENTRY_NOT_FOUND;
	struct team *team = thread_get_current_thread()->team;
	struct image *image = NULL;

	mutex_lock(&sImageMutex);

	while ((image = list_get_next_item(&team->image_list, image)) != NULL) {
		if (image->info.id == id) {
			memcpy(info, &image->info, size);
			status = B_OK;
			break;
		}
	}

	mutex_unlock(&sImageMutex);

	return status;
}


status_t
_get_next_image_info(team_id teamID, int32 *cookie, image_info *info, size_t size)
{
	status_t status = B_ENTRY_NOT_FOUND;
	struct team *team;
	cpu_status state;

	mutex_lock(&sImageMutex);

	state = disable_interrupts();
	GRAB_TEAM_LOCK();

	if (teamID == B_CURRENT_TEAM)
		team = thread_get_current_thread()->team;
	else if (teamID == B_SYSTEM_TEAM)
		team = team_get_kernel_team();
	else
		team = team_get_team_struct_locked(teamID);

	if (team) {
		struct image *image = NULL;
		int32 count = 0;

		while ((image = list_get_next_item(&team->image_list, image)) != NULL) {
			if (count == *cookie) {
				memcpy(info, &image->info, size);
				status = B_OK;
				(*cookie)++;
				break;
			}
			count++;
		}
	} else
		status = B_BAD_TEAM_ID;

	RELEASE_TEAM_LOCK();
	restore_interrupts(state);

	mutex_unlock(&sImageMutex);

	return status;
}


#ifdef ADD_DEBUGGER_COMMANDS
static int
dump_images_list(int argc, char **argv)
{
	struct image *image = NULL;
	struct team *team;

	if (argc > 1) {
		team_id id = strtol(argv[1], NULL, 0);
		team = team_get_team_struct_locked(id);
		if (team == NULL) {
			kprintf("No team with ID %ld found\n", id);
			return 1;
		}
	} else
		team = thread_get_current_thread()->team;

	kprintf("Registered images of team %ld\n", team->id);
	kprintf("    ID text       size    data       size    name\n");

	while ((image = list_get_next_item(&team->image_list, image)) != NULL) {
		image_info *info = &image->info;

		kprintf("%6ld %p %-7ld %p %-7ld %s\n", info->id, info->text, info->text_size,
			info->data, info->data_size, info->name);
	}

	return 0;
}
#endif


status_t
image_debug_lookup_user_symbol_address(struct team *team, addr_t address,
	addr_t *_baseAddress, const char **_symbolName, const char **_imageName,
	bool *_exactMatch)
{
	// TODO: work together with ELF reader and runtime_loader
	
	struct image *image = NULL;

	while ((image = list_get_next_item(&team->image_list, image)) != NULL) {
		image_info *info = &image->info;

		if ((address < (addr_t)info->text
				|| address >= (addr_t)info->text + info->text_size)
			&& (address < (addr_t)info->data
				|| address >= (addr_t)info->data + info->data_size))
			continue;

		// found image
		*_symbolName = NULL;
		*_imageName = info->name;
		*_baseAddress = (addr_t)info->text;
		*_exactMatch = false;

		return B_OK;
	}

	return B_ENTRY_NOT_FOUND;
}


status_t
image_init(void)
{
#ifdef ADD_DEBUGGER_COMMANDS
	add_debugger_command("team_images", &dump_images_list, "Dump all registered images from the current team");
#endif

	mutex_init(&sImageMutex, "image");
	return B_OK;
}


static void
notify_loading_app(status_t result, bool suspend)
{
	cpu_status state;
	struct team *team;

	state = disable_interrupts();
	GRAB_TEAM_LOCK();

	team = thread_get_current_thread()->team;
	if (team->loading_info) {
		// there's indeed someone waiting
		struct team_loading_info *loadingInfo = team->loading_info;
		team->loading_info = NULL;

		loadingInfo->result = result;
		loadingInfo->done = true;

		// we're done with the team stuff, get the thread lock instead
		RELEASE_TEAM_LOCK();
		GRAB_THREAD_LOCK();

		// wake up the waiting thread
		if (loadingInfo->thread->state == B_THREAD_SUSPENDED)
			scheduler_enqueue_in_run_queue(loadingInfo->thread);

		// suspend ourselves, if desired
		if (suspend) {
			thread_get_current_thread()->next_state = B_THREAD_SUSPENDED;
			scheduler_reschedule();
		}

		RELEASE_THREAD_LOCK();
	} else {
		// no-one is waiting
		RELEASE_TEAM_LOCK();
	}

	restore_interrupts(state);
}


//	#pragma mark -
//	Functions exported for the user space


status_t
_user_unregister_image(image_id id)
{
	return unregister_image(thread_get_current_thread()->team, id);
}


image_id
_user_register_image(image_info *userInfo, size_t size)
{
	image_info info;
	
	if (size != sizeof(image_info))
		return B_BAD_VALUE;

	if (!IS_USER_ADDRESS(userInfo)
		|| user_memcpy(&info, userInfo, size) < B_OK)
		return B_BAD_ADDRESS;

	return register_image(thread_get_current_thread()->team, &info, size);
}


void
_user_image_relocated(image_id id)
{
	image_info info;
	status_t error;

	// get an image info
	error = _get_image_info(id, &info, sizeof(image_info));
	if (error != B_OK) {
		dprintf("_user_image_relocated(%ld): Failed to get image info: %lx\n",
			id, error);
		return;
	}

	// notify the debugger
	user_debug_image_created(&info);

	// If the image is the app image, loading is done. We need to notify the
	// thread who initiated the process and is now waiting for us to be done.
	if (info.type == B_APP_IMAGE)
		notify_loading_app(B_OK, true);
}


void
_user_loading_app_failed(status_t error)
{
	if (error >= B_OK)
		error = B_ERROR;

	notify_loading_app(error, false);

	_user_exit_team(error);
}


status_t
_user_get_image_info(image_id id, image_info *userInfo, size_t size)
{
	image_info info;
	status_t status;

	if (size != sizeof(image_info))
		return B_BAD_VALUE;

	if (!IS_USER_ADDRESS(userInfo))
		return B_BAD_ADDRESS;

	status = _get_image_info(id, &info, size);

	if (user_memcpy(userInfo, &info, size) < B_OK)
		return B_BAD_ADDRESS;

	return status;
}


status_t
_user_get_next_image_info(team_id team, int32 *_cookie, image_info *userInfo, size_t size)
{
	image_info info;
	status_t status;

	if (size != sizeof(image_info))
		return B_BAD_VALUE;

	if (!IS_USER_ADDRESS(userInfo) || !IS_USER_ADDRESS(_cookie))
		return B_BAD_ADDRESS;

	status = _get_next_image_info(team, _cookie, &info, size);

	if (user_memcpy(userInfo, &info, size) < B_OK)
		return B_BAD_ADDRESS;

	return status;
}

