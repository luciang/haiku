/* Module manager */

/*
** Copyright 2001, Thomas Kurschel. All rights reserved.
** Distributed under the terms of the NewOS License.
*/


#include <kmodule.h>
#include <lock.h>
#include <Errors.h>
#include <khash.h>
#include <malloc.h>
#include <elf.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

#define MODULE_HASH_SIZE 16

static bool modules_disable_user_addons = false;

#define TRACE_MODULE 0
#if TRACE_MODULE
#	define TRACE(x) dprintf x
#else
#	define TRACE(x) ;
#endif
#define FATAL(x) dprintf x


typedef enum {
	MODULE_QUERIED = 0,
	MODULE_LOADED,
	MODULE_INIT,
	MODULE_READY,
	MODULE_UNINIT,
	MODULE_ERROR
} module_state;


/* Each loaded module image (which can export several modules) is put
 * in a hash (gModuleImagesHash) to be easily found when you search
 * for a specific file name.
 * ToDo: should probably use the VFS to parse the path, and use only the
 * inode number for hashing. Would probably a little bit slower, but would
 * lower the memory foot print quite a lot.
 */

typedef struct module_image {
	struct module_image	*next;
	module_info			**info;		/* the module_info we use */
	char				*path;		/* the full path for the module */
	image_id			image;
	int32				ref_count;	/* how many ref's to this file */
	bool				keep_loaded;
} module_image;

/* Each known module will have this structure which is put in the
 * gModulesHash, and looked up by name.
 */

typedef struct module {
	struct module		*next;
	module_image		*module_image;
	char				*name;
	char				*file;
	int32				ref_count;
	module_info			*info;		/* will only be valid if ref_count > 0 */
	int					offset;		/* this is the offset in the headers */
	module_state		state;		/* state of module */
	bool				keep_loaded;
} module;


typedef struct module_iterator {
	const char			**path_stack;
	int					stack_size;
	int					stack_current;

	char				*prefix;
	DIR					*current_dir;
	int					status;
	int					module_offset;
		/* This is used to keep track of which module_info
		 * within a module we're addressing. */
	module_image		*module_image;
	module_info			**current_header;
	const char			*current_path;
	const char			*current_module_path;
} module_iterator;


/* locking scheme: there is a global lock only; having several locks
 * makes trouble if dependent modules get loaded concurrently ->
 * they have to wait for each other, i.e. we need one lock per module;
 * also we must detect circular references during init and not dead-lock
 */
static recursive_lock gModulesLock;		

/* These are the standard base paths where we start to look for modules
 * to load. Order is important, the last entry here will be searched
 * first.
 * ToDo: these are not yet BeOS compatible (because the current bootfs is very limited)
 */
static const char * const gModulePaths[] = {
	"/boot/addons",
	"/boot/user-addons",
};

#define NUM_MODULE_PATHS (sizeof(gModulePaths) / sizeof(gModulePaths[0]))
#define USER_MODULE_PATHS 1		/* first user path */

/* we store the loaded modules by directory path, and all known modules by module name
 * in a hash table for quick access
 */
static hash_table *gModuleImagesHash;
static hash_table *gModulesHash;


/** calculates hash for a module using its name */

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


/** compares a module to a given name */

static int
module_compare(void *_module, const void *_key)
{
	module *module = (struct module *)_module;
	const char *name = (const char *)_key;
	if (name == NULL)
		return -1;

	return strcmp(module->name, name);
}


/** calculates the hash of a module image using its path */

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


/** compares a module image to a path */

static int
module_image_compare(void *_module, const void *_key)
{
	module_image *image = (module_image *)_module;
	const char *path = (const char *)_key;
	if (path == NULL)
		return -1;

	return strcmp(image->path, path);
}


static inline void
inc_module_ref_count(struct module *module)
{
	module->ref_count++;
}


static inline void
dec_module_ref_count(struct module *module)
{
	module->ref_count--;
}


/** Try to load the module image at the specified location.
 *	If it could be loaded, it returns B_OK, and stores a pointer
 *	to the module_image object in "_moduleImage".
 */

static status_t
load_module_image(const char *path, module_image **_moduleImage)
{
	module_image *moduleImage;
	status_t status;

	image_id image = elf_load_kspace(path, "");
	if (image < 0) {
		dprintf("load_module_image failed: %s\n", strerror(image));
		return image;
	}

	moduleImage = (module_image *)malloc(sizeof(module_image));
	if (!moduleImage) {
		status = B_NO_MEMORY;
		goto err;
	}

	moduleImage->info = (module_info **)elf_lookup_symbol(image, "modules");
	if (!moduleImage->info) {
		FATAL(("load_module_image: Failed to load %s due to lack of 'modules' symbol\n", path));
		status = B_BAD_TYPE;
		goto err1;
	}

	moduleImage->path = strdup(path);
	if (!moduleImage->path) {
		status = B_NO_MEMORY;
		goto err1;
	}

	moduleImage->image = image;
	moduleImage->ref_count = 0;
	moduleImage->keep_loaded = false;

	recursive_lock_lock(&gModulesLock);
	hash_insert(gModuleImagesHash, moduleImage);
	recursive_lock_unlock(&gModulesLock);

	*_moduleImage = moduleImage;
	return B_OK;

err1:
	free(moduleImage);
err:
	elf_unload_kspace(path);

	return status;
}


static status_t
unload_module_image(module_image *moduleImage, const char *path)
{
	TRACE(("unload_module_image(image = %p, path = %s)\n", moduleImage, path));

	if (moduleImage == NULL) {
		// if no image was specified, lookup it up in the hash table
		moduleImage = (module_image *)hash_lookup(gModuleImagesHash, path);
		if (moduleImage == NULL)
			return B_ENTRY_NOT_FOUND;
	}

	if (moduleImage->ref_count != 0) {
		FATAL(("Can't unload %s due to ref_cnt = %ld\n", moduleImage->path, moduleImage->ref_count));
		return B_ERROR;
	}

	recursive_lock_lock(&gModulesLock);
	hash_remove(gModuleImagesHash, moduleImage);
	recursive_lock_unlock(&gModulesLock);

	elf_unload_kspace(moduleImage->path);
	free(moduleImage->path);
	free(moduleImage);

	return B_OK;
}


static void
put_module_image(module_image *image)
{
	int32 refCount = atomic_add(&image->ref_count, -1);
	ASSERT(refCount > 0);

	if (refCount == 1 && !image->keep_loaded)
		unload_module_image(image, NULL);
}


static status_t
get_module_image(const char *path, module_image **_image)
{
	struct module_image *image = (module_image *)hash_lookup(gModuleImagesHash, path);
	if (image == NULL) {
		status_t status = load_module_image(path, &image);
		if (status < B_OK)
			return status;
	}

	atomic_add(&image->ref_count, 1);
	*_image = image;

	return B_OK;
}


/** Extract the information from the module_info structure pointed at
 *	by "info" and create the entries required for access to it's details.
 */

static status_t
create_module(module_info *info, const char *file, int offset, module **_module)
{
	module *module;

	if (!info->name)
		return B_BAD_VALUE;

	module = (struct module *)hash_lookup(gModulesHash, info->name);
	if (module) {
		FATAL(("Duplicate module name (%s) detected... ignoring new one\n", info->name));
		return B_FILE_EXISTS;
	}

	if ((module = (struct module *)malloc(sizeof(module))) == NULL)
		return B_NO_MEMORY;

	TRACE(("create_module(%s, %s)\n", info->name, file));

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
	module->offset = offset;
		// record where the module_info can be found in the module_info array
	module->ref_count = 0;

	if (info->flags & B_KEEP_LOADED) {
		TRACE(("module %s wants to be kept loaded\n", module->name));
		module->keep_loaded = true;
	}

	recursive_lock_lock(&gModulesLock);
	hash_insert(gModulesHash, module);
	recursive_lock_unlock(&gModulesLock);

	if (_module)
		*_module = module;

	return B_OK;
}


/** Loads the file at "path" and scans all modules contained therein.
 *	Returns B_OK if "searchedName" could be found under those modules,
 *	B_ENTRY_NOT_FOUND if not.
 *	Must only be called for files that haven't been scanned yet.
 *	"searchedName" is allowed to be NULL (if all modules should be scanned)
 */

static status_t
check_module_image(const char *path, const char *searchedName)
{
	module_image *image;
	module_info **info;
	int index = 0, match = B_ENTRY_NOT_FOUND;

	ASSERT(hash_lookup(gModuleImagesHash, path) == NULL);

	if (load_module_image(path, &image) < B_OK)
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
		TRACE(("check_module_file: unloading module file %s\n", path));
		unload_module_image(image, path);
	}

	return match;
}

	
/** Recursively scans through the provided path for the specified module
 *	named "searchedName".
 *	If "searchedName" is NULL, all modules will be scanned.
 *	Returns B_OK if the module could be found, B_ENTRY_NOT_FOUND if not,
 *	or some other error occured during scanning.
 */

static status_t
recurse_directory(const char *path, const char *searchedName)
{
	status_t status;
	DIR *dir = opendir(path);
	if (dir == NULL);
		return errno;

	errno = 0;

	// loop until we have a match or we run out of entries
	while (true) {
		struct dirent *dirent;
		struct stat st;
		char *newPath;
		size_t size = 0;

		TRACE(("scanning %s\n", path));

		dirent = readdir(dir);
		if (dirent == NULL) {
			// we tell the upper layer we couldn't find anything in here
			status = errno == 0 ? B_ENTRY_NOT_FOUND : errno;
			goto exit;
		}

		size = strlen(path) + strlen(dirent->d_name) + 2;	
		newPath = (char *)malloc(size);
		if (newPath == NULL) {
			status = B_NO_MEMORY;
			goto exit;
		}

		strlcpy(newPath, path, size);
		strlcat(newPath, "/", size);
			// two slashes wouldn't hurt
		strlcat(newPath, dirent->d_name, size);

		if (stat(newPath, &st) != 0) {
			free(newPath);
			errno = 0;

			// If we couldn't stat the current file, we will just ignore it;
			// it's a problem of the file system, not ours.
			continue;
		}

		if (S_ISREG(st.st_mode)) {
			// if it's a file, check if we already have it in the hash table,
			// because then we know it doesn't contain the module we are
			// searching for (we are here because it couldn't be found in
			// the first place)
			if (hash_lookup(gModuleImagesHash, newPath) != NULL)
				continue;

			status = check_module_image(newPath, searchedName);
		} else if (S_ISDIR(st.st_mode))
			status = recurse_directory(newPath, searchedName);
		else
			status = B_ERROR;

		if (status == B_OK)
			goto exit;

		free(newPath);
	}

exit:
	closedir(dir);
	return status;
}


/** This is only called if we fail to find a module already in our cache...
 *	saves us some extra checking here :)
 */

static module *
search_module(const char *name)
{
	status_t status = B_ENTRY_NOT_FOUND;
	int i;

	TRACE(("search_module(%s)\n", name));

	for (i = 0; i < NUM_MODULE_PATHS; i++) {
		if (modules_disable_user_addons && i >= USER_MODULE_PATHS)
			return NULL;

		if ((status = recurse_directory(gModulePaths[i], name)) == B_OK)
			break;
	}

	if (status != B_OK)
		return NULL;

	return (module *)hash_lookup(gModulesHash, name);
}


/** Initializes a loaded module depending on its state */

static inline status_t
init_module(module *module)
{
		
	switch (module->state) {
		case MODULE_QUERIED:
		case MODULE_LOADED:
		{
			status_t status;
			module->state = MODULE_INIT;

			TRACE(("initing module %s... \n", module->name));
			status = module->info->std_ops(B_MODULE_INIT);
			TRACE(("...done (%s)\n", strerror(status)));

			if (!status) 
				module->state = MODULE_READY;
			else
				module->state = MODULE_LOADED;

			return status;
		}

		case MODULE_READY:
			return B_NO_ERROR;

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


/** Uninitializes a module depeding on its state */

static inline int
uninit_module(module *module)
{
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

			TRACE(("uniniting module %s...\n", module->name));
			status = module->info->std_ops(B_MODULE_UNINIT);
			TRACE(("...done (%s)\n", strerror(status)));

			if (status == B_NO_ERROR) {
				module->state = MODULE_LOADED;
				return 0;
			}

			FATAL(("Error unloading module %s (%s)\n", module->name, strerror(status)));

			module->state = MODULE_ERROR;
			module->keep_loaded = true;

			return status;
		}
		default:	
			return B_ERROR;		
	}
	// never trespasses here
}


static const char *
iterator_pop_path_from_stack(module_iterator *iterator)
{
	if (iterator->stack_current > 0)
		return iterator->path_stack[--iterator->stack_current];

	return NULL;
}


static status_t
iterator_push_path_on_stack(module_iterator *iterator, const char *path)
{
	if (iterator->stack_current + 1 > iterator->stack_size) {
		// allocate new space on the stack
		const char **stack = (const char **)malloc((iterator->stack_size + 8) * sizeof(char *));
		if (stack == NULL)
			return B_NO_MEMORY;
		
		if (iterator->path_stack != NULL) {
			memcpy(stack, iterator->path_stack, iterator->stack_current * sizeof(char *));
			free(iterator->path_stack);
		}
		
		iterator->path_stack = stack;
		iterator->stack_size += 8;
	}
	
	iterator->path_stack[iterator->stack_current++] = path;
	return B_OK;
}


static status_t
iterator_get_next_module(module_iterator *iterator, char *buffer, size_t *_bufferSize)
{
	status_t status;

	TRACE(("iterator_get_next_module() -- start\n"));

nextDirectory:
	if (iterator->current_dir == NULL) {
		// get next directory path from the stack
		const char *path = iterator_pop_path_from_stack(iterator);
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
			goto nextDirectory;
		}
	}

nextModuleImage:
	if (iterator->current_header == NULL) {
		// get next entry from the current directory
		char path[SYS_MAX_PATH_LEN];
		struct dirent *dirent;
		struct stat st;

		errno = 0;

		if ((dirent = readdir(iterator->current_dir)) == NULL) {
			closedir(iterator->current_dir);
			iterator->current_dir = NULL;

			if (errno < B_OK)
				return errno;

			goto nextDirectory;
		}

		if (!strcmp(dirent->d_name, ".")
			|| !strcmp(dirent->d_name, ".."))
			goto nextModuleImage;

		// build absolute path to current file
		strlcpy(path, iterator->current_path, sizeof(path));
		strlcat(path, "/", sizeof(path));
		strlcat(path, dirent->d_name, sizeof(path));

		// find out if it's a directory or a file
		if (stat(path, &st) < 0)
			return errno;

		iterator->current_module_path = strdup(path);
		if (iterator->current_module_path == NULL)
			return B_NO_MEMORY;

		if (S_ISDIR(st.st_mode)) {
			status = iterator_push_path_on_stack(iterator, iterator->current_module_path);
			if (status < B_OK)
				return status;

			iterator->current_module_path = NULL;
			goto nextModuleImage;
		}

		if (!S_ISREG(st.st_mode))
			return B_BAD_TYPE;

		TRACE(("open module at %s\n", path));

		status = get_module_image(path, &iterator->module_image);
		if (status < B_OK) {
			free((void *)iterator->current_module_path);
			iterator->current_module_path = NULL;
			goto nextModuleImage;
		}

		iterator->current_header = iterator->module_image->info;
		iterator->module_offset = 0;
	}

	if (*iterator->current_header == NULL) {
		iterator->current_header = NULL;
		free((void *)iterator->current_module_path);
		iterator->current_module_path = NULL;

		put_module_image(iterator->module_image);
		iterator->module_image = NULL;

		goto nextModuleImage;
	}

	// ToDo: we might want to create a module here and cache it in the hash table

	*_bufferSize = strlcpy(buffer, (*iterator->current_header)->name, *_bufferSize);

	iterator->current_header++;
	iterator->module_offset++;

	return B_OK;
}


//	#pragma mark -
//	Exported Kernel API (private part)


/** Setup the module structures and data for use - must be called
 *	before any other module call.
 */

status_t
module_init(kernel_args *ka, module_info **sys_module_headers)
{
	recursive_lock_create(&gModulesLock);

	gModulesHash = hash_init(MODULE_HASH_SIZE, 0, module_compare, module_hash);
	if (gModulesHash == NULL)
		return B_NO_MEMORY;

	gModuleImagesHash = hash_init(MODULE_HASH_SIZE, 0, module_image_compare, module_image_hash);
	if (gModuleImagesHash == NULL)
		return B_NO_MEMORY;

/*
	if (sys_module_headers) { 
		if (register_module_image("", "(built-in)", 0, sys_module_headers) == NULL)
			return ENOMEM;
	}
*/	

	return B_OK;
}


#ifdef DEBUG
void
module_test(void)
{
	void *cookie;

	dprintf("module_test() - start!\n");

	cookie = open_module_list(NULL);
	if (cookie == NULL)
		return;

	while (true) {
		char name[SYS_MAX_PATH_LEN];
		size_t size = sizeof(name);

		if (read_next_module_name(cookie, name, &size) < B_OK)
			break;

		dprintf("module: %s\n", name);
	}
	close_module_list(cookie);
}
#endif


//	#pragma mark -
//	Exported Kernel API (public part)


/** This returns a pointer to a structure that can be used to
 *	iterate through a list of all modules available under
 *	a given prefix.
 *	All paths will be searched and the returned list will
 *	contain all modules available under the prefix.
 *	The structure is then used by read_next_module_name(), and
 *	must be freed by calling close_module_list().
 */

void *
open_module_list(const char *prefix)
{
	char path[SYS_MAX_PATH_LEN];
	module_iterator *iterator;
	int i;

	TRACE(("open_module_list(prefix = %s)\n", prefix));

	iterator = (module_iterator *)malloc(sizeof(module_iterator));
	if (!iterator)
		return NULL;

	memset(iterator, 0, sizeof(module_iterator));

	// ToDo: possibly, the prefix don't have to be copied, just referenced
	iterator->prefix = strdup(prefix ? prefix : "");
	if (iterator->prefix == NULL) {
		free(iterator);
		return NULL;
	}

	// put all search paths on the stack
	for (i = 0; i < NUM_MODULE_PATHS; i++) {
		const char *p;

		if (modules_disable_user_addons && i >= USER_MODULE_PATHS)
			break;

		strcpy(path, gModulePaths[i]);
		if (prefix && *prefix) {
			strcat(path, "/");
			strlcat(path, prefix, sizeof(path));
		}

		p = strdup(path);
		if (p == NULL) {
			// ToDo: should we abort the whole operation here?
			continue;
		}
		iterator_push_path_on_stack(iterator, p);
	}

	return (void *)iterator;
}


/** Frees the cookie allocated by open_module_list()
 */

status_t
close_module_list(void *cookie)
{
	module_iterator *iterator = (module_iterator *)cookie;
	const char *path;

	TRACE(("close_module_list()\n"));

	if (iterator == NULL)
		return B_BAD_VALUE;

	// free stack
	while ((path = iterator_pop_path_from_stack(iterator)) != NULL)
		free((void *)path);

	// close what have been left open
	if (iterator->module_image != NULL)
		put_module_image(iterator->module_image);

	if (iterator->current_dir != NULL)
		closedir(iterator->current_dir);

	free(iterator->path_stack);
	free((void *)iterator->current_path);
	free((void *)iterator->current_module_path);

	free(iterator->prefix);
	free(iterator);

	return 0;
}


/** Return the next module name from the available list, using
 *	a structure previously created by a call to open_module_list.
 *	Returns B_OK as long as it found another module, B_ENTRY_NOT_FOUND
 *	when done.
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
	recursive_lock_lock(&gModulesLock);

	status = iterator_get_next_module(iterator, buffer, _bufferSize);

	iterator->status = status;
	recursive_lock_unlock(&gModulesLock);

	TRACE(("read_next_module_name: finished with status %s\n", strerror(status)));
	return status;
}


/** Iterates through all loaded modules, and stores its path in "buffer".
 *	ToDo: check if the function in BeOS really does that (could also mean:
 *		iterate through all modules that are currently loaded; have a valid
 *		module_image pointer, which would be hard to test for)
 */

status_t 
get_next_loaded_module_name(uint32 *cookie, char *buffer, size_t *_bufferSize)
{
	hash_iterator *iterator = (hash_iterator *)*cookie;
	module_image *moduleImage;
	status_t status;

	TRACE(("get_next_loaded_module_name()\n"));

	if (cookie == NULL || buffer == NULL || _bufferSize == NULL)
		return B_BAD_VALUE;

	if (iterator == NULL) {
		iterator = hash_open(gModuleImagesHash, NULL);
		if (iterator == NULL)
			return B_NO_MEMORY;

		*(hash_iterator **)cookie = iterator;
	}

	recursive_lock_lock(&gModulesLock);

	moduleImage = hash_next(gModuleImagesHash, iterator);
	if (moduleImage != NULL) {
		strlcpy(buffer, moduleImage->path, *_bufferSize);
		*_bufferSize = strlen(moduleImage->path);
		status = B_OK;
	} else {
		hash_close(gModuleImagesHash, iterator, true);
		status = B_ENTRY_NOT_FOUND;
	}

	recursive_lock_unlock(&gModulesLock);

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

	recursive_lock_lock(&gModulesLock);

	module = (struct module *)hash_lookup(gModulesHash, path);

	// if we don't have it cached yet, search for it
	if (module == NULL) {
		module = search_module(path);
		if (module == NULL) {
			FATAL(("module: Search for %s failed.\n", path));
			goto err;
		}
	}

	/* We now need to find the module_image for the module. This should
	 * be in memory if we have just run search_modules, but may not be
	 * if we are used cached information.
	 * We can't use the module->module_image pointer, because it is not
	 * reliable at this point (it won't be set to NULL when the module_image
	 * is unloaded).
	 */
	if (get_module_image(module->file, &moduleImage) < B_OK)
		goto err;

	// (re)set in-memory data for the loaded module
	module->info = moduleImage->info[module->offset];
	module->module_image = moduleImage;

	// the module image must not be unloaded anymore
	if (module->keep_loaded)
		module->module_image->keep_loaded = true;

	inc_module_ref_count(module);

	// The state will be adjusted by the call to init_module
	// if we have just loaded the file
	if (module->ref_count == 1)
		status = init_module(module);
	else
		status = B_OK;

	recursive_lock_unlock(&gModulesLock);

	if (status == B_OK)
		*_info = module->info;

	return status;

err:
	recursive_lock_unlock(&gModulesLock);
	return B_ENTRY_NOT_FOUND;
}


status_t
put_module(const char *path)
{
	module *module;

	TRACE(("put_module(path = %s)\n", path));

	recursive_lock_lock(&gModulesLock);

	module = (struct module *)hash_lookup(gModulesHash, path);
	if (module == NULL) {
		FATAL(("module: We don't seem to have a reference to module %s\n", path));
		recursive_lock_unlock(&gModulesLock);
		return B_BAD_VALUE;
	}
	dec_module_ref_count(module);

	// ToDo: not sure if this should ever be called for keep_loaded modules...
	if (module->ref_count == 0)
		uninit_module(module);

	put_module_image(module->module_image);

	recursive_lock_unlock(&gModulesLock);
	return B_OK;
}
