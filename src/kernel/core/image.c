/*
 * Copyright 2003-2005, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
 * Distributed under the terms of the MIT License.
 */

/* User Runtime Loader support in the kernel */


#include <KernelExport.h>
#include <kernel.h>
#include <kimage.h>
#include <lock.h>
#include <thread.h>
#include <team.h>
#include <user_debugger.h>

#include <malloc.h>
#include <string.h>


//#define TRACE_IMAGE
#ifdef TRACE_IMAGE
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif


struct image {
	struct image	*next;
	struct image	*prev;
	image_info		info;
};


static image_id sNextImageID = 1;
static mutex sImageMutex;


/** Registers an image with the specified team.
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

	// notify the debugger
	_info->id = id;
	user_debug_image_created(_info);
		// ToDo: Is this too early? Not even the loader knows about the
		// image (ID) yet. We should probably introduce another syscall, the
		// loader can invoke just before calling the image's init function.

	TRACE(("register_image(team = %p, image id = %ld, image = %p\n", team, id, image));
	return id;
}


/** Unregisters an image from the specified team.
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


/** Counts the registered images from the specified team.
 *	The team lock must be hold when you call this function.
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


/** Removes all images from the specified team. Must only be called
 *	with the team lock hold or a team that has already been removed
 *	from the list (in thread_exit()).
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


#ifdef DEBUG
static int
dump_images_list(int argc, char **argv)
{
	struct team *team = thread_get_current_thread()->team;
	struct image *image = NULL;

	dprintf("Registered images of team 0x%lx\n", team->id);
	dprintf("    ID text       size    data       size    name\n");

	mutex_lock(&sImageMutex);

	while ((image = list_get_next_item(&team->image_list, image)) != NULL) {
		image_info *info = &image->info;

		dprintf("%6ld %p %-7ld %p %-7ld %s\n", info->id, info->text, info->text_size,
			info->data, info->data_size, info->name);
	}

	mutex_unlock(&sImageMutex);

	return 0;
}
#endif


status_t
image_init(void)
{
#ifdef DEBUG
	add_debugger_command("team_images", &dump_images_list, "Dump all registered images from the current team");
#endif

	return mutex_init(&sImageMutex, "image");
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

