/*
 * Copyright 2002-2008, Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Copyright 2001, Thomas Kurschel. All rights reserved.
 * Distributed under the terms of the NewOS License.
 */

/*!	Manages kernel add-ons and their exported modules. */


#include <kmodule.h>

#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include <FindDirectory.h>

#include <boot_device.h>
#include <elf.h>
#include <lock.h>
#include <vfs.h>
#include <boot/elf.h>
#include <fs/KPath.h>
#include <safemode.h>
#include <util/AutoLock.h>
#include <util/khash.h>


//#define TRACE_MODULE
#ifdef TRACE_MODULE
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif
#define FATAL(x) dprintf x


#define MODULE_HASH_SIZE 16

/*! The modules referenced by this structure are built-in
	modules that can't be loaded from disk.
*/
extern module_info gDeviceManagerModule;
extern module_info gDeviceRootModule;
extern module_info gDeviceForDriversModule;
extern module_info gFrameBufferConsoleModule;

// file systems
extern module_info gRootFileSystem;
extern module_info gDeviceFileSystem;
extern module_info gPipeFileSystem;

static module_info *sBuiltInModules[] = {
	&gDeviceManagerModule,
	&gDeviceRootModule,
	&gDeviceForDriversModule,
	&gFrameBufferConsoleModule,

	&gRootFileSystem,
	&gDeviceFileSystem,
	&gPipeFileSystem,
	NULL
};

enum module_state {
	MODULE_QUERIED = 0,
	MODULE_LOADED,
	MODULE_INIT,
	MODULE_READY,
	MODULE_UNINIT,
	MODULE_ERROR
};


/* Each loaded module image (which can export several modules) is put
 * in a hash (gModuleImagesHash) to be easily found when you search
 * for a specific file name.
 * ToDo: Could use only the inode number for hashing. Would probably be
 * a little bit slower, but would lower the memory foot print quite a lot.
 */

struct module_image {
	struct module_image	*next;
	module_info			**info;		/* the module_info we use */
	module_dependency	*dependencies;
	char				*path;		/* the full path for the module */
	image_id			image;
	int32				ref_count;	/* how many ref's to this file */
	bool				keep_loaded;
};

/* Each known module will have this structure which is put in the
 * gModulesHash, and looked up by name.
 */

struct module {
	struct module		*next;
	::module_image		*module_image;
	char				*name;
	char				*file;
	int32				ref_count;
	module_info			*info;		/* will only be valid if ref_count > 0 */
	int32				offset;		/* this is the offset in the headers */
	module_state		state;		/* state of module */
	uint32				flags;
};

#define B_BUILT_IN_MODULE	2

typedef struct module_path {
	const char			*name;
	uint32				base_length;
} module_path;

typedef struct module_iterator {
	module_path			*stack;
	int32				stack_size;
	int32				stack_current;

	char				*prefix;
	size_t				prefix_length;
	DIR					*current_dir;
	status_t			status;
	int32				module_offset;
		/* This is used to keep track of which module_info
		 * within a module we're addressing. */
	::module_image		*module_image;
	module_info			**current_header;
	const char			*current_path;
	uint32				path_base_length;
	const char			*current_module_path;
	bool				builtin_modules;
	bool				loaded_modules;
} module_iterator;


static bool sDisableUserAddOns = false;

/* locking scheme: there is a global lock only; having several locks
 * makes trouble if dependent modules get loaded concurrently ->
 * they have to wait for each other, i.e. we need one lock per module;
 * also we must detect circular references during init and not dead-lock
 */
static recursive_lock sModulesLock;		

/* These are the standard base paths where we start to look for modules
 * to load. Order is important, the last entry here will be searched
 * first.
 */
static const directory_which kModulePaths[] = {
	B_BEOS_ADDONS_DIRECTORY,
	B_COMMON_ADDONS_DIRECTORY,
	B_USER_ADDONS_DIRECTORY,
};

static const uint32 kNumModulePaths = sizeof(kModulePaths)
	/ sizeof(kModulePaths[0]);
static const uint32 kFirstNonSystemModulePath = 1;

/* We store the loaded modules by directory path, and all known modules
 * by module name in a hash table for quick access
 */
static hash_table *sModuleImagesHash;
static hash_table *sModulesHash;


/*!	Calculates hash for a module using its name */
static uint32
module_hash(void *_module, const void *_key, uint32 range)
{
	module *module = (struct module *)_module;
	const char *name = (const char *)_key;

	if (module != NULL)
		return hash_hash_string(module->name) % range;
	
	if (name != NULL)
		return hash_hash_string(name) % range;

	return 0;
}


/*!	Compares a module to a given name */
static int
module_compare(void *_module, const void *_key)
{
	module *module = (struct module *)_module;
	const char *name = (const char *)_key;
	if (name == NULL)
		return -1;

	return strcmp(module->name, name);
}


/*!	Calculates the hash of a module image using its path */
static uint32
module_image_hash(void *_module, const void *_key, uint32 range)
{
	module_image *image = (module_image *)_module;
	const char *path = (const char *)_key;

	if (image != NULL)
		return hash_hash_string(image->path) % range;

	if (path != NULL)
		return hash_hash_string(path) % range;

	return 0;
}


/*!	Compares a module image to a path */
static int
module_image_compare(void *_module, const void *_key)
{
	module_image *image = (module_image *)_module;
	const char *path = (const char *)_key;
	if (path == NULL)
		return -1;

	return strcmp(image->path, path);
}


/*!	Try to load the module image at the specified location.
	If it could be loaded, it returns B_OK, and stores a pointer
	to the module_image object in "_moduleImage".
*/
static status_t
load_module_image(const char *path, module_image **_moduleImage)
{
	module_image *moduleImage;
	status_t status;
	image_id image;

	TRACE(("load_module_image(path = \"%s\", _image = %p)\n", path, _moduleImage));
	ASSERT(_moduleImage != NULL);

	image = load_kernel_add_on(path);
	if (image < 0) {
		dprintf("load_module_image(%s) failed: %s\n", path, strerror(image));
		return image;
	}

	moduleImage = (module_image *)malloc(sizeof(module_image));
	if (!moduleImage) {
		status = B_NO_MEMORY;
		goto err;
	}

	if (get_image_symbol(image, "modules", B_SYMBOL_TYPE_DATA,
			(void **)&moduleImage->info) != B_OK) {
		TRACE(("load_module_image: Failed to load \"%s\" due to lack of 'modules' symbol\n", path));
		status = B_BAD_TYPE;
		goto err1;
	}

	moduleImage->dependencies = NULL;
	get_image_symbol(image, "module_dependencies", B_SYMBOL_TYPE_DATA,
		(void **)&moduleImage->dependencies);
		// this is allowed to be NULL

	moduleImage->path = strdup(path);
	if (!moduleImage->path) {
		status = B_NO_MEMORY;
		goto err1;
	}

	moduleImage->image = image;
	moduleImage->ref_count = 0;
	moduleImage->keep_loaded = false;

	recursive_lock_lock(&sModulesLock);
	hash_insert(sModuleImagesHash, moduleImage);
	recursive_lock_unlock(&sModulesLock);

	*_moduleImage = moduleImage;
	return B_OK;

err1:
	free(moduleImage);
err:
	unload_kernel_add_on(image);

	return status;
}


static status_t
unload_module_image(module_image *moduleImage, const char *path)
{
	TRACE(("unload_module_image(image = %p, path = %s)\n", moduleImage, path));

	RecursiveLocker locker(sModulesLock);

	if (moduleImage == NULL) {
		// if no image was specified, lookup it up in the hash table
		moduleImage = (module_image *)hash_lookup(sModuleImagesHash, path);
		if (moduleImage == NULL)
			return B_ENTRY_NOT_FOUND;
	}

	if (moduleImage->ref_count != 0) {
		FATAL(("Can't unload %s due to ref_cnt = %ld\n", moduleImage->path,
			moduleImage->ref_count));
		return B_ERROR;
	}

	hash_remove(sModuleImagesHash, moduleImage);
	locker.Unlock();

	unload_kernel_add_on(moduleImage->image);
	free(moduleImage->path);
	free(moduleImage);

	return B_OK;
}


static void
put_module_image(module_image *image)
{
	int32 refCount = atomic_add(&image->ref_count, -1);
	ASSERT(refCount > 0);

	// Don't unload anything when there is no boot device yet
	// (because chances are that we will never be able to access it again)

	if (refCount == 1 && !image->keep_loaded && gBootDevice > 0)
		unload_module_image(image, NULL);
}


static status_t
get_module_image(const char *path, module_image **_image)
{
	struct module_image *image;

	TRACE(("get_module_image(path = \"%s\")\n", path));

	RecursiveLocker _(sModulesLock);

	image = (module_image *)hash_lookup(sModuleImagesHash, path);
	if (image == NULL) {
		status_t status = load_module_image(path, &image);
		if (status < B_OK)
			return status;
	}

	atomic_add(&image->ref_count, 1);
	*_image = image;

	return B_OK;
}


/*!	Extract the information from the module_info structure pointed at
	by "info" and create the entries required for access to it's details.
*/
static status_t
create_module(module_info *info, const char *file, int offset, module **_module)
{
	module *module;

	TRACE(("create_module(info = %p, file = \"%s\", offset = %d, _module = %p)\n",
		info, file, offset, _module));

	if (!info->name)
		return B_BAD_VALUE;

	module = (struct module *)hash_lookup(sModulesHash, info->name);
	if (module) {
		FATAL(("Duplicate module name (%s) detected... ignoring new one\n", info->name));
		return B_FILE_EXISTS;
	}

	if ((module = (struct module *)malloc(sizeof(struct module))) == NULL)
		return B_NO_MEMORY;

	TRACE(("create_module: name = \"%s\", file = \"%s\"\n", info->name, file));

	module->module_image = NULL;
	module->name = strdup(info->name);
	if (module->name == NULL) {
		free(module);
		return B_NO_MEMORY;
	}

	module->file = strdup(file);
	if (module->file == NULL) {
		free(module->name);
		free(module);
		return B_NO_MEMORY;
	}

	module->state = MODULE_QUERIED;
	module->info = info;
	module->offset = offset;
		// record where the module_info can be found in the module_info array
	module->ref_count = 0;
	module->flags = info->flags;

	recursive_lock_lock(&sModulesLock);
	hash_insert(sModulesHash, module);
	recursive_lock_unlock(&sModulesLock);

	if (_module)
		*_module = module;

	return B_OK;
}


/*!	Loads the file at "path" and scans all modules contained therein.
	Returns B_OK if "searchedName" could be found under those modules,
	B_ENTRY_NOT_FOUND if not.
	Must only be called for files that haven't been scanned yet.
	"searchedName" is allowed to be NULL (if all modules should be scanned)
*/
static status_t
check_module_image(const char *path, const char *searchedName)
{
	module_image *image;
	module_info **info;
	int index = 0, match = B_ENTRY_NOT_FOUND;

	TRACE(("check_module_image(path = \"%s\", searchedName = \"%s\")\n", path,
		searchedName));

	if (get_module_image(path, &image) < B_OK)
		return B_ENTRY_NOT_FOUND;

	for (info = image->info; *info; info++) {
		// try to create a module for every module_info, check if the
		// name matches if it was a new entry
		if (create_module(*info, path, index++, NULL) == B_OK) {
			if (searchedName && !strcmp((*info)->name, searchedName))
				match = B_OK;
		}
	}

	// The module we looked for couldn't be found, so we can unload the
	// loaded module at this point
	if (match != B_OK) {
		TRACE(("check_module_file: unloading module file \"%s\" (not used yet)\n",
			path));
		unload_module_image(image, path);
	}

	// decrement the ref we got in get_module_image
	put_module_image(image);

	return match;
}


/*!	This is only called if we fail to find a module already in our cache...
	saves us some extra checking here :)
*/
static module *
search_module(const char *name)
{
	status_t status = B_ENTRY_NOT_FOUND;
	uint32 i;

	TRACE(("search_module(%s)\n", name));

	for (i = kNumModulePaths; i-- > 0;) {
		if (sDisableUserAddOns && i >= kFirstNonSystemModulePath)
			continue;

		// let the VFS find that module for us

		KPath basePath;
		if (find_directory(kModulePaths[i], gBootDevice, true,
				basePath.LockBuffer(), basePath.BufferSize()) != B_OK)
			continue;

		basePath.UnlockBuffer();
		basePath.Append("kernel");

		KPath path;
		status = vfs_get_module_path(basePath.Path(), name, path.LockBuffer(),
			path.BufferSize());
		if (status == B_OK) {
			path.UnlockBuffer();
			status = check_module_image(path.Path(), name);
			if (status == B_OK)
				break;
		}
	}

	if (status != B_OK)
		return NULL;

	return (module *)hash_lookup(sModulesHash, name);
}


static status_t
put_dependent_modules(struct module *module)
{
	module_image *image = module->module_image;
	module_dependency *dependencies;

	// built-in modules don't have a module_image structure
	if (image == NULL
		|| (dependencies = image->dependencies) == NULL)
		return B_OK;

	for (int32 i = 0; dependencies[i].name != NULL; i++) {
		status_t status = put_module(dependencies[i].name);
		if (status < B_OK)
			return status;
	}

	return B_OK;
}


static status_t
get_dependent_modules(struct module *module)
{
	module_image *image = module->module_image;
	module_dependency *dependencies;

	// built-in modules don't have a module_image structure
	if (image == NULL
		|| (dependencies = image->dependencies) == NULL)
		return B_OK;

	TRACE(("resolving module dependencies...\n"));

	for (int32 i = 0; dependencies[i].name != NULL; i++) {
		status_t status = get_module(dependencies[i].name,
			dependencies[i].info);
		if (status < B_OK) {
			dprintf("loading dependent module %s of %s failed!\n",
				dependencies[i].name, module->name);
			return status;
		}
	}

	return B_OK;
}


/*!	Initializes a loaded module depending on its state */
static inline status_t
init_module(module *module)
{
	switch (module->state) {
		case MODULE_QUERIED:
		case MODULE_LOADED:
		{
			status_t status;
			module->state = MODULE_INIT;

			// resolve dependencies

			status = get_dependent_modules(module);
			if (status < B_OK) {
				module->state = MODULE_LOADED;
				return status;
			}

			// init module

			TRACE(("initializing module %s (at %p)... \n", module->name, module->info->std_ops));
			status = module->info->std_ops(B_MODULE_INIT);
			TRACE(("...done (%s)\n", strerror(status)));

			if (status >= B_OK)
				module->state = MODULE_READY;
			else {
				put_dependent_modules(module);
				module->state = MODULE_LOADED;
			}

			return status;
		}

		case MODULE_READY:
			return B_OK;

		case MODULE_INIT:
			FATAL(("circular reference to %s\n", module->name));
			return B_ERROR;

		case MODULE_UNINIT:
			FATAL(("tried to load module %s which is currently unloading\n", module->name));
			return B_ERROR;

		case MODULE_ERROR:
			FATAL(("cannot load module %s because its earlier unloading failed\n", module->name));
			return B_ERROR;

		default:
			return B_ERROR;
	}
	// never trespasses here
}


/*!	Uninitializes a module depeding on its state */
static inline int
uninit_module(module *module)
{
	TRACE(("uninit_module(%s)\n", module->name));

	switch (module->state) {
		case MODULE_QUERIED:
		case MODULE_LOADED:
			return B_NO_ERROR;

		case MODULE_INIT:
			panic("Trying to unload module %s which is initializing\n", module->name);
			return B_ERROR;

		case MODULE_UNINIT:
			panic("Trying to unload module %s which is un-initializing\n", module->name);
			return B_ERROR;

		case MODULE_READY:
		{
			status_t status;

			module->state = MODULE_UNINIT;

			TRACE(("uninitializing module %s...\n", module->name));
			status = module->info->std_ops(B_MODULE_UNINIT);
			TRACE(("...done (%s)\n", strerror(status)));

			if (status == B_NO_ERROR) {
				module->state = MODULE_LOADED;

				put_dependent_modules(module);
				return B_OK;
			}

			FATAL(("Error unloading module %s (%s)\n", module->name,
				strerror(status)));

			module->state = MODULE_ERROR;
			module->flags |= B_KEEP_LOADED;

			return status;
		}
		default:	
			return B_ERROR;		
	}
	// never trespasses here
}


static const char *
iterator_pop_path_from_stack(module_iterator *iterator, uint32 *_baseLength)
{
	if (iterator->stack_current <= 0)
		return NULL;

	if (_baseLength)
		*_baseLength = iterator->stack[iterator->stack_current - 1].base_length;

	return iterator->stack[--iterator->stack_current].name;
}


static status_t
iterator_push_path_on_stack(module_iterator *iterator, const char *path, uint32 baseLength)
{
	if (iterator->stack_current + 1 > iterator->stack_size) {
		// allocate new space on the stack
		module_path *stack = (module_path *)realloc(iterator->stack,
			(iterator->stack_size + 8) * sizeof(module_path));
		if (stack == NULL)
			return B_NO_MEMORY;

		iterator->stack = stack;
		iterator->stack_size += 8;
	}

	iterator->stack[iterator->stack_current].name = path;
	iterator->stack[iterator->stack_current++].base_length = baseLength;
	return B_OK;
}


static status_t
iterator_get_next_module(module_iterator *iterator, char *buffer,
	size_t *_bufferSize)
{
	status_t status;

	TRACE(("iterator_get_next_module() -- start\n"));

	if (iterator->builtin_modules) {
		for (int32 i = iterator->module_offset; sBuiltInModules[i] != NULL; i++) {
			// the module name must fit the prefix
			if (strncmp(sBuiltInModules[i]->name, iterator->prefix,
					iterator->prefix_length))
				continue;

			*_bufferSize = strlcpy(buffer, sBuiltInModules[i]->name,
				*_bufferSize);
			iterator->module_offset = i + 1;
			return B_OK;
		}
		iterator->builtin_modules = false;
	}

	if (iterator->loaded_modules) {
		recursive_lock_lock(&sModulesLock);
		hash_iterator hashIterator;
		hash_open(sModulesHash, &hashIterator);

		struct module *module = (struct module *)hash_next(sModulesHash,
			&hashIterator);
		for (int32 i = 0; module != NULL; i++) {
			if (i >= iterator->module_offset) {
				if (!strncmp(module->name, iterator->prefix,
						iterator->prefix_length)) {
					*_bufferSize = strlcpy(buffer, module->name, *_bufferSize);
					iterator->module_offset = i + 1;

					hash_close(sModulesHash, &hashIterator, false);
					recursive_lock_unlock(&sModulesLock);
					return B_OK;
				}
			}
			module = (struct module *)hash_next(sModulesHash, &hashIterator);
		}

		hash_close(sModulesHash, &hashIterator, false);
		recursive_lock_unlock(&sModulesLock);

		// prevent from falling into modules hash iteration again
		iterator->loaded_modules = false; 
	}
	
nextPath:
	if (iterator->current_dir == NULL) {
		// get next directory path from the stack
		const char *path = iterator_pop_path_from_stack(iterator,
			&iterator->path_base_length);
		if (path == NULL) {
			// we are finished, there are no more entries on the stack
			return B_ENTRY_NOT_FOUND;
		}

		free((void *)iterator->current_path);
		iterator->current_path = path;
		iterator->current_dir = opendir(path);
		TRACE(("open directory at %s -> %p\n", path, iterator->current_dir));

		if (iterator->current_dir == NULL) {
			// we don't throw an error here, but silently go to
			// the next directory on the stack
			goto nextPath;
		}
	}

nextModuleImage:
	if (iterator->current_header == NULL) {
		// get next entry from the current directory

		errno = 0;

		struct dirent *dirent;
		if ((dirent = readdir(iterator->current_dir)) == NULL) {
			closedir(iterator->current_dir);
			iterator->current_dir = NULL;

			if (errno < B_OK)
				return errno;

			goto nextPath;
		}

		// check if the prefix matches
		int32 passedOffset, commonLength;
		passedOffset = strlen(iterator->current_path) + 1;
		commonLength = iterator->path_base_length + iterator->prefix_length
			- passedOffset;

		if (commonLength > 0) {
			// the prefix still reaches into the new path part
			int32 length = strlen(dirent->d_name);
			if (commonLength > length)
				commonLength = length;

			if (strncmp(dirent->d_name, iterator->prefix + passedOffset
					- iterator->path_base_length, commonLength))
				goto nextModuleImage;
		}

		// we're not interested in traversing these again
		if (!strcmp(dirent->d_name, ".")
			|| !strcmp(dirent->d_name, ".."))
			goto nextModuleImage;

		// build absolute path to current file
		KPath path(iterator->current_path);
		if (path.InitCheck() != B_OK)
			return B_NO_MEMORY;

		if (path.Append(dirent->d_name) != B_OK)
			return B_BUFFER_OVERFLOW;

		// find out if it's a directory or a file
		struct stat st;
		if (stat(path.Path(), &st) < 0)
			return errno;

		iterator->current_module_path = strdup(path.Path());
		if (iterator->current_module_path == NULL)
			return B_NO_MEMORY;

		if (S_ISDIR(st.st_mode)) {
			status = iterator_push_path_on_stack(iterator,
				iterator->current_module_path, iterator->path_base_length);
			if (status < B_OK)
				return status;

			iterator->current_module_path = NULL;
			goto nextModuleImage;
		}

		if (!S_ISREG(st.st_mode))
			return B_BAD_TYPE;

		TRACE(("open module at %s\n", path.Path()));

		status = get_module_image(path.Path(), &iterator->module_image);
		if (status < B_OK) {
			free((void *)iterator->current_module_path);
			iterator->current_module_path = NULL;
			goto nextModuleImage;
		}

		iterator->current_header = iterator->module_image->info;
		iterator->module_offset = 0;
	}

	// search the current module image until we've got a match
	while (*iterator->current_header != NULL) {
		module_info *info = *iterator->current_header;

		// ToDo: we might want to create a module here and cache it in the hash table

		iterator->current_header++;
		iterator->module_offset++;

		if (strncmp(info->name, iterator->prefix, iterator->prefix_length))
			continue;

		*_bufferSize = strlcpy(buffer, info->name, *_bufferSize);
		return B_OK;
	}

	// leave this module and get the next one

	iterator->current_header = NULL;
	free((void *)iterator->current_module_path);
	iterator->current_module_path = NULL;

	put_module_image(iterator->module_image);
	iterator->module_image = NULL;

	goto nextModuleImage;
}


static void
register_builtin_modules(struct module_info **info)
{
	for (; *info; info++) {
		(*info)->flags |= B_BUILT_IN_MODULE;
			// this is an internal flag, it doesn't have to be set by modules itself

		if (create_module(*info, "", -1, NULL) != B_OK)
			dprintf("creation of built-in module \"%s\" failed!\n", (*info)->name);
	}
}


static status_t
register_preloaded_module_image(struct preloaded_image *image)
{
	module_image *moduleImage;
	struct module_info **info;
	status_t status;
	int32 index = 0;

	TRACE(("register_preloaded_module_image(image = \"%s\")\n", image->name));

	image->is_module = false;

	if (image->id < 0)
		return B_BAD_VALUE;

	moduleImage = (module_image *)malloc(sizeof(module_image));
	if (moduleImage == NULL)
		return B_NO_MEMORY;

	if (get_image_symbol(image->id, "modules", B_SYMBOL_TYPE_DATA,
			(void **)&moduleImage->info) != B_OK) {
		status = B_BAD_TYPE;
		goto error;
	}

	image->is_module = true;

	moduleImage->dependencies = NULL;
	get_image_symbol(image->id, "module_dependencies", B_SYMBOL_TYPE_DATA,
		(void **)&moduleImage->dependencies);
		// this is allowed to be NULL

	// Try to recreate the full module path, so that we don't try to load the
	// image again when asked for a module it does not export (would only be
	// problematic if it had got replaced and the new file actually exports
	// that module). Also helpful for recurse_directory().
	{
		// ToDo: this is kind of a hack to have the full path in the hash
		//	(it always assumes the preloaded add-ons to be in the system directory)
		char path[B_FILE_NAME_LENGTH];
		const char *name, *suffix;
		if (moduleImage->info[0]
			&& (suffix = strstr(name = moduleImage->info[0]->name,
					image->name)) != NULL) {
			// even if strlcpy() is used here, it's by no means safe
			// against buffer overflows
			size_t length = strlcpy(path, "/boot/beos/system/add-ons/kernel/",
				sizeof(path));
			strlcpy(path + length, name, strlen(image->name)
				+ 1 + (suffix - name));

			moduleImage->path = strdup(path);
		} else
			moduleImage->path = strdup(image->name);
	}
	if (moduleImage->path == NULL) {
		status = B_NO_MEMORY;
		goto error;
	}

	moduleImage->image = image->id;
	moduleImage->ref_count = 0;
	moduleImage->keep_loaded = false;

	hash_insert(sModuleImagesHash, moduleImage);

	for (info = moduleImage->info; *info; info++) {
		create_module(*info, moduleImage->path, index++, NULL);
	}

	return B_OK;

error:
	free(moduleImage);
	
	// We don't need this image anymore. We keep it, if it doesn't look like
	// a module at all. It might be an old-style driver.
	if (image->is_module)
		unload_kernel_add_on(image->id);

	return status;
}


static int
dump_modules(int argc, char **argv)
{
	hash_iterator iterator;
	struct module_image *image;
	struct module *module;

	hash_rewind(sModulesHash, &iterator);
	dprintf("-- known modules:\n");

	while ((module = (struct module *)hash_next(sModulesHash, &iterator)) != NULL) {
		dprintf("%p: \"%s\", \"%s\" (%ld), refcount = %ld, state = %d, mimage = %p\n",
			module, module->name, module->file, module->offset, module->ref_count,
			module->state, module->module_image);
	}

	hash_rewind(sModuleImagesHash, &iterator);
	dprintf("\n-- loaded module images:\n");

	while ((image = (struct module_image *)hash_next(sModuleImagesHash, &iterator)) != NULL) {
		dprintf("%p: \"%s\" (image_id = %ld), info = %p, refcount = %ld, %s\n", image,
			image->path, image->image, image->info, image->ref_count,
			image->keep_loaded ? "keep loaded" : "can be unloaded");
	}
	return 0;
}


//	#pragma mark - Exported Kernel API (private part)


/*!	Unloads a module in case it's not in use. This is the counterpart
	to load_module().
*/
status_t
unload_module(const char *path)
{
	struct module_image *moduleImage;

	recursive_lock_lock(&sModulesLock);
	moduleImage = (module_image *)hash_lookup(sModuleImagesHash, path);
	recursive_lock_unlock(&sModulesLock);

	if (moduleImage == NULL)
		return B_ENTRY_NOT_FOUND;

	put_module_image(moduleImage);
	return B_OK;
}


/*!	Unlike get_module(), this function lets you specify the add-on to
	be loaded by path.
	However, you must not use the exported modules without having called
	get_module() on them. When you're done with the NULL terminated
	\a modules array, you have to call unload_module(), no matter if
	you're actually using any of the modules or not - of course, the
	add-on won't be unloaded until the last put_module().
*/
status_t
load_module(const char *path, module_info ***_modules)
{
	module_image *moduleImage;
	status_t status = get_module_image(path, &moduleImage);
	if (status != B_OK)
		return status;

	*_modules = moduleImage->info;
	return B_OK;
}


/*! Setup the module structures and data for use - must be called
	before any other module call.
*/
status_t
module_init(kernel_args *args)
{
	struct preloaded_image *image;

	if (recursive_lock_init(&sModulesLock, "modules rlock") < B_OK)
		return B_ERROR;

	sModulesHash = hash_init(MODULE_HASH_SIZE, 0, module_compare, module_hash);
	if (sModulesHash == NULL)
		return B_NO_MEMORY;

	sModuleImagesHash = hash_init(MODULE_HASH_SIZE, 0, module_image_compare,
		module_image_hash);
	if (sModuleImagesHash == NULL)
		return B_NO_MEMORY;

	// register built-in modules

	register_builtin_modules(sBuiltInModules);

	// register preloaded images

	for (image = args->preloaded_images; image != NULL; image = image->next) {
		status_t status = register_preloaded_module_image(image);
		if (status != B_OK) {
			dprintf("Could not register image \"%s\": %s\n", image->name,
				strerror(status));
		}
	}

	sDisableUserAddOns = get_safemode_boolean(B_SAFEMODE_DISABLE_USER_ADD_ONS,
		false);

	add_debugger_command("modules", &dump_modules,
		"list all known & loaded modules");

	return B_OK;
}


//	#pragma mark - Exported Kernel API (public part)


/*! This returns a pointer to a structure that can be used to
	iterate through a list of all modules available under
	a given prefix.
	All paths will be searched and the returned list will
	contain all modules available under the prefix.
	The structure is then used by read_next_module_name(), and
	must be freed by calling close_module_list().
*/
void *
open_module_list(const char *prefix)
{
	module_iterator *iterator;
	uint32 i;

	TRACE(("open_module_list(prefix = %s)\n", prefix));

	if (sModulesHash == NULL) {
		dprintf("open_module_list() called too early!\n");
		return NULL;
	}

	iterator = (module_iterator *)malloc(sizeof(module_iterator));
	if (!iterator)
		return NULL;

	memset(iterator, 0, sizeof(module_iterator));

	iterator->prefix = strdup(prefix != NULL ? prefix : "");
	if (iterator->prefix == NULL) {
		free(iterator);
		return NULL;
	}
	iterator->prefix_length = strlen(iterator->prefix);

	if (gBootDevice > 0) {
		// We do have a boot device to scan

		// first, we'll traverse over the built-in modules
		iterator->builtin_modules = true;
		iterator->loaded_modules = false;

		// put all search paths on the stack
		for (i = 0; i < kNumModulePaths; i++) {
			if (sDisableUserAddOns && i >= kFirstNonSystemModulePath)
				break;

			KPath pathBuffer;
			if (find_directory(kModulePaths[i], gBootDevice, true,
					pathBuffer.LockBuffer(), pathBuffer.BufferSize()) != B_OK)
				continue;

			pathBuffer.UnlockBuffer();
			pathBuffer.Append("kernel");

			// Copy base path onto the iterator stack
			char *path = strdup(pathBuffer.Path());
			if (path == NULL)
				continue;

			size_t length = strlen(path);

			// TODO: it would currently be nicer to use the commented
			// version below, but the iterator won't work if the prefix
			// is inside a module then.
			// It works this way, but should be done better.
#if 0
			// Build path component: base path + '/' + prefix
			size_t length = strlen(sModulePaths[i]);
			char *path = (char *)malloc(length + iterator->prefix_length + 2);
			if (path == NULL) {
				// ToDo: should we abort the whole operation here?
				//	if we do, don't forget to empty the stack
				continue;
			}

			memcpy(path, sModulePaths[i], length);
			path[length] = '/';
			memcpy(path + length + 1, iterator->prefix,
				iterator->prefix_length + 1);
#endif

			iterator_push_path_on_stack(iterator, path, length + 1);
		}
	} else {
		// include loaded modules in case there is no boot device yet
		iterator->builtin_modules = false;
		iterator->loaded_modules = true;
	}

	return (void *)iterator;
}


/*!	Frees the cookie allocated by open_module_list() */
status_t
close_module_list(void *cookie)
{
	module_iterator *iterator = (module_iterator *)cookie;
	const char *path;

	TRACE(("close_module_list()\n"));

	if (iterator == NULL)
		return B_BAD_VALUE;

	// free stack
	while ((path = iterator_pop_path_from_stack(iterator, NULL)) != NULL)
		free((void *)path);

	// close what have been left open
	if (iterator->module_image != NULL)
		put_module_image(iterator->module_image);

	if (iterator->current_dir != NULL)
		closedir(iterator->current_dir);

	free(iterator->stack);
	free((void *)iterator->current_path);
	free((void *)iterator->current_module_path);

	free(iterator->prefix);
	free(iterator);

	return B_OK;
}


/*!	Return the next module name from the available list, using
	a structure previously created by a call to open_module_list().
	Returns B_OK as long as it found another module, B_ENTRY_NOT_FOUND
	when done.
*/
status_t
read_next_module_name(void *cookie, char *buffer, size_t *_bufferSize)
{
	module_iterator *iterator = (module_iterator *)cookie;
	status_t status;

	TRACE(("read_next_module_name: looking for next module\n"));

	if (iterator == NULL || buffer == NULL || _bufferSize == NULL)
		return B_BAD_VALUE;

	if (iterator->status < B_OK)
		return iterator->status;

	status = iterator->status;
	recursive_lock_lock(&sModulesLock);

	status = iterator_get_next_module(iterator, buffer, _bufferSize);

	iterator->status = status;
	recursive_lock_unlock(&sModulesLock);

	TRACE(("read_next_module_name: finished with status %s\n",
		strerror(status)));
	return status;
}


/*!	Iterates through all loaded modules, and stores its path in "buffer".
	ToDo: check if the function in BeOS really does that (could also mean:
		iterate through all modules that are currently loaded; have a valid
		module_image pointer, which would be hard to test for)
*/
status_t 
get_next_loaded_module_name(uint32 *_cookie, char *buffer, size_t *_bufferSize)
{
	if (sModulesHash == NULL) {
		dprintf("get_next_loaded_module_name() called too early!\n");
		return B_ERROR;
	}

	//TRACE(("get_next_loaded_module_name(\"%s\")\n", buffer));

	if (_cookie == NULL || buffer == NULL || _bufferSize == NULL)
		return B_BAD_VALUE;

	status_t status = B_ENTRY_NOT_FOUND;
	uint32 offset = *_cookie;

	RecursiveLocker _(sModulesLock);

	hash_iterator iterator;
	hash_open(sModulesHash, &iterator);
	struct module *module = (struct module *)hash_next(sModulesHash,
		&iterator);

	for (uint32 i = 0; module != NULL; i++) {
		if (i >= offset) {
			*_bufferSize = strlcpy(buffer, module->name, *_bufferSize);
			*_cookie = i + 1;
			status = B_OK;
			break;
		}
		module = (struct module *)hash_next(sModulesHash, &iterator);
	}

	hash_close(sModulesHash, &iterator, false);

	return status;
}


status_t
get_module(const char *path, module_info **_info)
{
	module_image *moduleImage;
	module *module;
	status_t status;

	TRACE(("get_module(%s)\n", path));

	if (path == NULL)
		return B_BAD_VALUE;

	RecursiveLocker _(sModulesLock);

	module = (struct module *)hash_lookup(sModulesHash, path);

	// if we don't have it cached yet, search for it
	if (module == NULL) {
		module = search_module(path);
		if (module == NULL) {
			FATAL(("module: Search for %s failed.\n", path));
			return B_ENTRY_NOT_FOUND;
		}
	}

	if ((module->flags & B_BUILT_IN_MODULE) == 0) {
		/* We now need to find the module_image for the module. This should
		 * be in memory if we have just run search_module(), but may not be
		 * if we are using cached information.
		 * We can't use the module->module_image pointer, because it is not
		 * reliable at this point (it won't be set to NULL when the module_image
		 * is unloaded).
		 */
		if (get_module_image(module->file, &moduleImage) < B_OK)
			return B_ENTRY_NOT_FOUND;

		// (re)set in-memory data for the loaded module
		module->info = moduleImage->info[module->offset];
		module->module_image = moduleImage;

		// the module image must not be unloaded anymore
		if (module->flags & B_KEEP_LOADED)
			module->module_image->keep_loaded = true;
	}

	// The state will be adjusted by the call to init_module
	// if we have just loaded the file
	if (module->ref_count == 0)
		status = init_module(module);
	else
		status = B_OK;

	if (status == B_OK) {
		if (module->ref_count < 0)
			panic("argl %s", path);
		module->ref_count++;
		*_info = module->info;
	} else if ((module->flags & B_BUILT_IN_MODULE) == 0
		&& (module->flags & B_KEEP_LOADED) == 0)
		put_module_image(module->module_image);

	return status;
}


status_t
put_module(const char *path)
{
	module *module;

	TRACE(("put_module(path = %s)\n", path));

	RecursiveLocker _(sModulesLock);

	module = (struct module *)hash_lookup(sModulesHash, path);
	if (module == NULL) {
		FATAL(("module: We don't seem to have a reference to module %s\n",
			path));
		return B_BAD_VALUE;
	}

	if (module->ref_count == 0)
		panic("module %s has no references.\n", path);

	if ((module->flags & B_KEEP_LOADED) == 0) {
		if (--module->ref_count == 0)
			uninit_module(module);
	} else if ((module->flags & B_BUILT_IN_MODULE) == 0)
		put_module_image(module->module_image);

	return B_OK;
}
