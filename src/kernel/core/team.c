/* Team functions */

/*
** Copyright 2001-2002, Travis Geiselbrecht. All rights reserved.
** Distributed under the terms of the NewOS License.
*/

#include <OS.h>
#include <kernel.h>
#include <thread.h>
#include <thread_types.h>
#include <int.h>
#include <khash.h>
#include <malloc.h>
#include <user_runtime.h>
#include <Errors.h>
#include <kerrors.h>
#include <kimage.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <elf.h>
#include <atomic.h>
#include <syscalls.h>
#include <tls.h>


struct team_key {
	team_id id;
};

struct team_arg {
	char *path;
	char **args;
	char **envp;
	unsigned int argc;
	unsigned int envc;
};

// team list
static void *team_hash = NULL;
static team_id next_team_id = 1;
static struct team *kernel_team = NULL;

spinlock team_spinlock = 0;

static struct team *create_team_struct(const char *name, bool kernel);
static void delete_team_struct(struct team *p);
static int team_struct_compare(void *_p, const void *_key);
static uint32 team_struct_hash(void *_p, const void *_key, uint32 range);
static void kfree_strings_array(char **strings, int strc);
static int user_copy_strings_array(char **strings, int strc, char ***kstrings);
static void _dump_team_info(struct team *p);
static int dump_team_info(int argc, char **argv);


static void
_dump_team_info(struct team *p)
{
	dprintf("TEAM: %p\n", p);
	dprintf("id:          0x%lx\n", p->id);
	dprintf("name:        '%s'\n", p->name);
	dprintf("next:        %p\n", p->next);
	dprintf("num_threads: %d\n", p->num_threads);
	dprintf("state:       %d\n", p->state);
	dprintf("pending_signals: 0x%x\n", p->pending_signals);
	dprintf("io_context:  %p\n", p->io_context);
//	dprintf("path:        '%s'\n", p->path);
	dprintf("aspace_id:   0x%lx\n", p->_aspace_id);
	dprintf("aspace:      %p\n", p->aspace);
	dprintf("kaspace:     %p\n", p->kaspace);
	dprintf("main_thread: %p\n", p->main_thread);
	dprintf("thread_list: %p\n", p->thread_list);
}


static int
dump_team_info(int argc, char **argv)
{
	struct team *p;
	int id = -1;
	unsigned long num;
	struct hash_iterator i;

	if (argc < 2) {
		dprintf("team: not enough arguments\n");
		return 0;
	}

	// if the argument looks like a hex number, treat it as such
	if (strlen(argv[1]) > 2 && argv[1][0] == '0' && argv[1][1] == 'x') {
		num = atoul(argv[1]);
		if (num > vm_get_kernel_aspace()->virtual_map.base) {
			// XXX semi-hack
			_dump_team_info((struct team*)num);
			return 0;
		} else {
			id = num;
		}
	}

	// walk through the thread list, trying to match name or id
	hash_open(team_hash, &i);
	while ((p = hash_next(team_hash, &i)) != NULL) {
		if ((p->name && strcmp(argv[1], p->name) == 0) || p->id == id) {
			_dump_team_info(p);
			break;
		}
	}
	hash_close(team_hash, &i, false);
	return 0;
}


int
team_init(kernel_args *ka)
{
	// create the team hash table
	team_hash = hash_init(15, (addr)&kernel_team->next - (addr)kernel_team,
		&team_struct_compare, &team_struct_hash);

	// create the kernel team
	kernel_team = create_team_struct("kernel_team", true);
	if (kernel_team == NULL)
		panic("could not create kernel team!\n");
	kernel_team->state = TEAM_STATE_NORMAL;

	kernel_team->io_context = vfs_new_io_context(NULL);
	if (kernel_team->io_context == NULL)
		panic("could not create io_context for kernel team!\n");

	//XXX should initialize kernel_team->path here. Set it to "/"?

	// stick it in the team hash
	hash_insert(team_hash, kernel_team);

	add_debugger_command("team", &dump_team_info, "list info about a particular team");
	return 0;
}


/**	Frees an array of strings in kernel space
 *	Parameters
 *		strings		strings array
 *		strc		number of strings in array
 */

static void
kfree_strings_array(char **strings, int strc)
{
	int cnt = strc;

	if (strings != NULL) {
		for (cnt = 0; cnt < strc; cnt++){
			free(strings[cnt]);
		}
	    free(strings);
	}
}


/**	Copy an array of strings from user space to kernel space
 *	Parameters
 *		strings		userspace strings array
 *		strc		number of strings in array
 *		kstrings	pointer to the kernel copy
 *	Returns < 0 on error and **kstrings = NULL
 */

static int
user_copy_strings_array(char **strings, int strc, char ***kstrings)
{
	char **lstrings;
	int err;
	int cnt;
	char *source;
	char buf[SYS_THREAD_STRING_LENGTH_MAX];

	*kstrings = NULL;

	if ((addr)strings >= KERNEL_BASE && (addr)strings <= KERNEL_TOP)
		return ERR_VM_BAD_USER_MEMORY;

	lstrings = (char **)malloc((strc + 1) * sizeof(char *));
	if (lstrings == NULL)
		return B_NO_MEMORY;

	// scan all strings and copy to kernel space

	for (cnt = 0; cnt < strc; cnt++) {
		err = user_memcpy(&source, &(strings[cnt]), sizeof(char *));
		if(err < 0)
			goto error;

		if ((addr)source >= KERNEL_BASE && (addr)source <= KERNEL_TOP){
			err = ERR_VM_BAD_USER_MEMORY;
			goto error;
		}

		err = user_strncpy(buf, source, SYS_THREAD_STRING_LENGTH_MAX - 1);
		if (err < 0)
			goto error;
		buf[SYS_THREAD_STRING_LENGTH_MAX - 1] = 0;

		lstrings[cnt] = strdup(buf);
		if (lstrings[cnt] == NULL){
			err = ENOMEM;
			goto error;
		}
	}

	lstrings[strc] = NULL;

	*kstrings = lstrings;
	return B_NO_ERROR;

error:
	kfree_strings_array(lstrings, cnt);
	dprintf("user_copy_strings_array failed %d \n", err);
	return err;
}


status_t
wait_for_team(team_id id, status_t *_returnCode)
{
	struct team *team;
	thread_id thread;
	cpu_status state;

	// find main thread and wait for that

	state = disable_interrupts();
	GRAB_TEAM_LOCK();

	team = team_get_team_struct_locked(id);
	if (team && team->main_thread)
		thread = team->main_thread->id;
	else
		thread = ERR_INVALID_HANDLE;

	RELEASE_TEAM_LOCK();
	restore_interrupts(state);

	if (thread < 0)
		return thread;

	return wait_for_thread(thread, _returnCode);
}


struct team *
team_get_team_struct(team_id id)
{
	struct team *p;
	int state;

	state = disable_interrupts();
	GRAB_TEAM_LOCK();

	p = team_get_team_struct_locked(id);

	RELEASE_TEAM_LOCK();
	restore_interrupts(state);

	return p;
}


struct team *
team_get_team_struct_locked(team_id id)
{
	struct team_key key;

	key.id = id;

	return hash_lookup(team_hash, &key);
}


static int
team_struct_compare(void *_p, const void *_key)
{
	struct team *p = _p;
	const struct team_key *key = _key;

	if (p->id == key->id)
		return 0;

	return 1;
}


static uint32
team_struct_hash(void *_p, const void *_key, uint32 range)
{
	struct team *p = _p;
	const struct team_key *key = _key;

	if (p != NULL)
		return p->id % range;

	return key->id % range;
}


void
team_remove_team_from_hash(struct team *team)
{
	hash_remove(team_hash, team);
}


struct team *
team_get_kernel_team(void)
{
	return kernel_team;
}


team_id
team_get_kernel_team_id(void)
{
	if (!kernel_team)
		return 0;

	return kernel_team->id;
}


team_id
team_get_current_team_id(void)
{
	return thread_get_current_thread()->team->id;
}


static struct team *
create_team_struct(const char *name, bool kernel)
{
	struct team *team;

	team = (struct team *)malloc(sizeof(struct team));
	if (team == NULL)
		goto error;

	team->id = atomic_add(&next_team_id, 1);
	strncpy(&team->name[0], name, SYS_MAX_OS_NAME_LEN-1);
	team->name[SYS_MAX_OS_NAME_LEN-1] = 0;
	team->num_threads = 0;
	team->io_context = NULL;
	team->_aspace_id = -1;
	team->aspace = NULL;
	team->kaspace = vm_get_kernel_aspace();
	vm_put_aspace(team->kaspace);
	team->thread_list = NULL;
	team->main_thread = NULL;
	team->state = TEAM_STATE_BIRTH;
	team->pending_signals = 0;
	team->death_sem = -1;
	team->user_env_base = 0;
	list_init(&team->image_list);

	if (arch_team_init_team_struct(team, kernel) < 0)
		goto error1;

	return team;

error1:
	free(team);
error:
	return NULL;
}


static void
delete_team_struct(struct team *team)
{
	free(team);
}


static int
get_arguments_data_size(char **args, int argc)
{
	uint32 size = 0, count;

	for (count = 0; count < argc; count++)
		size += strlen(args[count]) + 1;

	return size + (argc + 1) * sizeof(char *) + sizeof(struct uspace_program_args);
}


static int32
team_create_team2(void *args)
{
	int err;
	struct thread *t;
	struct team *team;
	struct team_arg *teamArgs = args;
	char *path;
	addr entry;
	char ustack_name[128];
	uint32 totalSize;
	char **uargs;
	char **uenv;
	char *udest;
	struct uspace_program_args *uspa;
	unsigned int arg_cnt;
	unsigned int env_cnt;

	t = thread_get_current_thread();
	team = t->team;

	dprintf("team_create_team2: entry thread %ld\n", t->id);

	// create an initial primary stack region

	// ToDo: make ENV_SIZE variable?
	// ToDo: when B_BASE_ADDRESS is implemented, we could just allocate the stack from
	//		the bottom of the USER_STACK_REGION.

	totalSize = PAGE_ALIGN(MAIN_THREAD_STACK_SIZE + TLS_SIZE + ENV_SIZE +
		get_arguments_data_size(teamArgs->args, teamArgs->argc));
	t->user_stack_base = USER_STACK_REGION + USER_STACK_REGION_SIZE - totalSize;
		// the exact location at the end of the user stack region

	sprintf(ustack_name, "%s_primary_stack", team->name);
	t->user_stack_region_id = vm_create_anonymous_region(team->_aspace_id, ustack_name, (void **)&t->user_stack_base,
		REGION_ADDR_EXACT_ADDRESS, totalSize, REGION_WIRING_LAZY, LOCK_RW);
	if (t->user_stack_region_id < 0) {
		panic("team_create_team2: could not create default user stack region\n");
		return t->user_stack_region_id;
	}

	// now that the TLS area is allocated, initialize TLS
	arch_thread_init_tls(t);

	uspa = (struct uspace_program_args *)(t->user_stack_base + STACK_SIZE + TLS_SIZE + ENV_SIZE);
	uargs = (char **)(uspa + 1);
	udest = (char  *)(uargs + teamArgs->argc + 1);
//	dprintf("addr: stack base=0x%x uargs = 0x%x  udest=0x%x tot_top_size=%d \n\n",t->user_stack_base,uargs,udest,tot_top_size);

	for (arg_cnt = 0; arg_cnt < teamArgs->argc; arg_cnt++) {
		uargs[arg_cnt] = udest;
		user_strcpy(udest, teamArgs->args[arg_cnt]);
		udest += (strlen(teamArgs->args[arg_cnt]) + 1);
	}
	uargs[arg_cnt] = NULL;

	team->user_env_base = t->user_stack_base + STACK_SIZE + TLS_SIZE;
	uenv  = (char **)team->user_env_base;
	udest = (char *)team->user_env_base + ENV_SIZE - 1;
//	dprintf("team_create_team2: envc: %d, envp: 0x%p\n", teamArgs->envc, (void *)teamArgs->envp);
	for (env_cnt = 0; env_cnt < teamArgs->envc; env_cnt++) {
		udest -= (strlen(teamArgs->envp[env_cnt]) + 1);
		uenv[env_cnt] = udest;
		user_strcpy(udest, teamArgs->envp[env_cnt]);
	}
	uenv[env_cnt] = NULL;

	user_memcpy(uspa->program_name, team->name, sizeof(uspa->program_name));
	user_memcpy(uspa->program_path, teamArgs->path, sizeof(uspa->program_path));
	uspa->argc = arg_cnt;
	uspa->argv = uargs;
	uspa->envc = env_cnt;
	uspa->envp = uenv;

	if (teamArgs->args != NULL)
		kfree_strings_array(teamArgs->args, teamArgs->argc);
	if (teamArgs->envp != NULL)
		kfree_strings_array(teamArgs->envp, teamArgs->envc);

	path = teamArgs->path;
	dprintf("team_create_team2: loading elf binary '%s'\n", path);

	err = elf_load_uspace("/boot/libexec/rld.so", team, 0, &entry);
	if (err < 0){
		// XXX clean up team
		return err;
	}

	// free the args
	free(teamArgs->path);
	free(teamArgs);

	dprintf("team_create_team2: loaded elf. entry = 0x%lx\n", entry);

	team->state = TEAM_STATE_NORMAL;

	// jump to the entry point in user space
	arch_thread_enter_uspace(t, entry, uspa);

	// never gets here
	return 0;
}


team_id
team_create_team(const char *path, const char *name, char **args, int argc, char **envp, int envc, int priority)
{
	struct team *team;
	thread_id tid;
	team_id pid;
	int err;
	unsigned int state;
//	int sem_retcode;
	struct team_arg *teamArgs;

	dprintf("team_create_team: entry '%s', name '%s' args = %p argc = %d\n", path, name, args, argc);

	team = create_team_struct(name, false);
	if (team == NULL)
		return ENOMEM;

	pid = team->id;

	state = disable_interrupts();
	GRAB_TEAM_LOCK();
	hash_insert(team_hash, team);
	RELEASE_TEAM_LOCK();
	restore_interrupts(state);

	// copy the args over
	teamArgs = (struct team_arg *)malloc(sizeof(struct team_arg));
	if (teamArgs == NULL){
		err = ENOMEM;
		goto err1;
	}
	teamArgs->path = strdup(path);
	if (teamArgs->path == NULL){
		err = ENOMEM;
		goto err2;
	}
	teamArgs->argc = argc;
	teamArgs->args = args;
	teamArgs->envp = envp;
	teamArgs->envc = envc;

	// create a new io_context for this team
	team->io_context = vfs_new_io_context(thread_get_current_thread()->team->io_context);
	if (!team->io_context) {
		err = ENOMEM;
		goto err3;
	}

	// create an address space for this team
	team->_aspace_id = vm_create_aspace(team->name, USER_BASE, USER_SIZE, false);
	if (team->_aspace_id < 0) {
		err = team->_aspace_id;
		goto err4;
	}
	team->aspace = vm_get_aspace_by_id(team->_aspace_id);

	// create a kernel thread, but under the context of the new team
	tid = spawn_kernel_thread_etc(team_create_team2, name, B_NORMAL_PRIORITY, teamArgs, team->id);
	if (tid < 0) {
		err = tid;
		goto err5;
	}

	resume_thread(tid);

	return pid;

err5:
	vm_put_aspace(team->aspace);
	vm_delete_aspace(team->_aspace_id);
err4:
	vfs_free_io_context(team->io_context);
err3:
	free(teamArgs->path);
err2:
	free(teamArgs);
err1:
	// remove the team structure from the team hash table and delete the team structure
	state = disable_interrupts();
	GRAB_TEAM_LOCK();
	hash_remove(team_hash, team);
	RELEASE_TEAM_LOCK();
	restore_interrupts(state);
	delete_team_struct(team);
//err:
	return err;
}


int
team_kill_team(team_id id)
{
	int state;
	struct team *team;
//	struct thread *t;
	thread_id tid = -1;
	int retval = 0;

	state = disable_interrupts();
	GRAB_TEAM_LOCK();

	team = team_get_team_struct_locked(id);
	if (team != NULL)
		tid = team->main_thread->id;
	else
		retval = ERR_INVALID_HANDLE;

	RELEASE_TEAM_LOCK();
	restore_interrupts(state);

	if (retval < 0)
		return retval;

	// just kill the main thread in the team. The cleanup code there will
	// take care of the team
	return thread_kill_thread(tid);
}


/** Fills the team_info structure with information from the specified
 *	team.
 *	Team lock must be hold when called.
 */

static status_t
fill_team_info(struct team *team, team_info *info, size_t size)
{
	if (size != sizeof(team_info))
		return B_BAD_VALUE;

	// ToDo: Set more informations for team_info
	memset(info, 0, size);

	info->team = team->id;
	info->thread_count = team->num_threads;
	info->image_count = count_images(team);
	//info->area_count = 
	//info->debugger_nub_thread = 
	//info->debugger_nub_port = 
	//info->argc = 
	//info->args[64] = 
	//info->uid = 
	//info->gid = 

	// ToDo: make this to return real argc/argv
	strlcpy(info->args, team->name, sizeof(info->args));
	info->argc = 1;

	return B_OK;
}


status_t
_get_team_info(team_id id, team_info *info, size_t size)
{
	int state;
	status_t rc = B_OK;
	struct team *team;
	
	state = disable_interrupts();
	GRAB_TEAM_LOCK();
	
	team = team_get_team_struct_locked(id);
	if (!team) {
		rc = B_BAD_TEAM_ID;
		goto err;
	}

	rc = fill_team_info(team, info, size);

err:
	RELEASE_TEAM_LOCK();
	restore_interrupts(state);
	
	return rc;
}


status_t
_get_next_team_info(int32 *cookie, team_info *info, size_t size)
{
	status_t status = B_BAD_TEAM_ID;
	struct team *team = NULL;
	int32 slot = *cookie;

	int state = disable_interrupts();
	GRAB_TEAM_LOCK();

	if (slot >= next_team_id)
		goto err;

	// get next valid team
	while ((slot < next_team_id) && !(team = team_get_team_struct_locked(slot)))
		slot++;

	if (team) {
		status = fill_team_info(team, info, size);
		*cookie = ++slot;
	}

err:
	RELEASE_TEAM_LOCK();
	restore_interrupts(state);

	return status;
}


int
sys_setenv(const char *name, const char *value, int overwrite)
{
	char var[SYS_THREAD_STRING_LENGTH_MAX];
	int state;
	addr env_space;
	char **envp;
	int envc;
	bool var_exists = false;
	int var_pos = 0;
	int name_size;
	int rc = 0;
	int i;
	char *p;

	// ToDo: please put me out of the kernel into libroot.so!

	dprintf("sys_setenv: entry (name=%s, value=%s)\n", name, value);

	if (strlen(name) + strlen(value) + 1 >= SYS_THREAD_STRING_LENGTH_MAX)
		return -1;

	state = disable_interrupts();
	GRAB_TEAM_LOCK();

	strcpy(var, name);
	strncat(var, "=", SYS_THREAD_STRING_LENGTH_MAX-1);
	name_size = strlen(var);
	strncat(var, value, SYS_THREAD_STRING_LENGTH_MAX-1);

	env_space = (addr)thread_get_current_thread()->team->user_env_base;
	envp = (char **)env_space;
	for (envc = 0; envp[envc]; envc++) {
		if (!strncmp(envp[envc], var, name_size)) {
			var_exists = true;
			var_pos = envc;
		}
	}
	if (!var_exists)
		var_pos = envc;

	dprintf("sys_setenv: variable does%s exist\n", var_exists ? "" : " not");
	if ((!var_exists) || (var_exists && overwrite)) {
		// XXX- make a better allocator
		if (var_exists) {
			if (strlen(var) <= strlen(envp[var_pos])) {
				strcpy(envp[var_pos], var);
			} else {
				for (p = (char *)env_space + ENV_SIZE - 1, i = 0; envp[i]; i++)
					if (envp[i] < p)
						p = envp[i];
				p -= (strlen(var) + 1);
				if (p < (char *)env_space + (envc * sizeof(char *))) {
					rc = -1;
				} else {
					envp[var_pos] = p;
					strcpy(envp[var_pos], var);
				}
			}
		}
		else {
			for (p = (char *)env_space + ENV_SIZE - 1, i=0; envp[i]; i++)
				if (envp[i] < p)
					p = envp[i];
			p -= (strlen(var) + 1);
			if (p < (char *)env_space + ((envc + 1) * sizeof(char *))) {
				rc = -1;
			} else {
				envp[envc] = p;
				strcpy(envp[envc], var);
				envp[envc + 1] = NULL;
			}
		}
	}
	dprintf("sys_setenv: variable set.\n");

	RELEASE_TEAM_LOCK();
	restore_interrupts(state);
	
	return rc;
}


int
sys_getenv(const char *name, char **value)
{
	char **envp;
	char *p;
	int state;
	int i;
	int len = strlen(name);
	int rc = -1;

	// ToDo: please put me out of the kernel into libroot.so!
	
	state = disable_interrupts();
	GRAB_TEAM_LOCK();

	envp = (char **)thread_get_current_thread()->team->user_env_base;
	for (i = 0; envp[i]; i++) {
		if (!strncmp(envp[i], name, len)) {
			p = envp[i] + len;
			if (*p == '=') {
				*value = (p + 1);
				rc = 0;
				break;
			}
		}
	}
	
	RELEASE_TEAM_LOCK();
	restore_interrupts(state);
	
	return rc;
}


//	#pragma mark -


status_t
user_wait_for_team(team_id id, status_t *_userReturnCode)
{
	status_t returnCode;
	status_t status;

	if (!CHECK_USER_ADDRESS(_userReturnCode))
		return B_BAD_ADDRESS;

	status = wait_for_team(id, &returnCode);
	if (status >= B_OK) {
		if (user_memcpy(_userReturnCode, &returnCode, sizeof(returnCode)) < B_OK)
			return B_BAD_ADDRESS;
	}

	return status;
}


team_id
user_team_create_team(const char *upath, const char *uname, char **args, int argc, char **envp, int envc, int priority)
{
	char path[SYS_MAX_PATH_LEN];
	char name[SYS_MAX_OS_NAME_LEN];
	char **kargs;
	char **kenv;
	int rc;

	dprintf("user_team_create_team : argc=%d \n",argc);

	if ((addr)upath >= KERNEL_BASE && (addr)upath <= KERNEL_TOP)
		return ERR_VM_BAD_USER_MEMORY;
	if ((addr)uname >= KERNEL_BASE && (addr)uname <= KERNEL_TOP)
		return ERR_VM_BAD_USER_MEMORY;

	rc = user_copy_strings_array(args, argc, &kargs);
	if (rc < 0)
		goto error;
	
	if (envp == NULL) {
		envp = (char **)thread_get_current_thread()->team->user_env_base;
		for (envc = 0; envp && (envp[envc]); envc++);
	}
	rc = user_copy_strings_array(envp, envc, &kenv);
	if (rc < 0)
		goto error;

	rc = user_strncpy(path, upath, SYS_MAX_PATH_LEN-1);
	if (rc < 0)
		goto error;

	path[SYS_MAX_PATH_LEN-1] = 0;

	rc = user_strncpy(name, uname, SYS_MAX_OS_NAME_LEN-1);
	if (rc < 0)
		goto error;

	name[SYS_MAX_OS_NAME_LEN-1] = 0;

	return team_create_team(path, name, kargs, argc, kenv, envc, priority);
error:
	kfree_strings_array(kargs, argc);
	kfree_strings_array(kenv, envc);
	return rc;
}


status_t
user_get_team_info(team_id id, team_info *info)
{
	team_info kinfo;
	status_t rc = B_OK;
	status_t rc2;
	
	if ((addr)info >= KERNEL_BASE && (addr)info <= KERNEL_TOP)
		return ERR_VM_BAD_USER_MEMORY;
		
	rc = _get_team_info(id, &kinfo, sizeof(team_info));
	if (rc != B_OK)
		return rc;
	
	rc2 = user_memcpy(info, &kinfo, sizeof(team_info));
	if (rc2 < 0)
		return rc2;
	
	return rc;
}


status_t
user_get_next_team_info(int32 *cookie, team_info *info)
{
	int32 kcookie;
	team_info kinfo;
	status_t rc = B_OK;
	status_t rc2;
	
	if ((addr)cookie >= KERNEL_BASE && (addr)cookie <= KERNEL_TOP)
		return ERR_VM_BAD_USER_MEMORY;
	if ((addr)info >= KERNEL_BASE && (addr)info <= KERNEL_TOP)
		return ERR_VM_BAD_USER_MEMORY;
	
	rc2 = user_memcpy(&kcookie, cookie, sizeof(int32));
	if (rc2 < 0)
		return rc2;
	
	rc = _get_next_team_info(&kcookie, &kinfo, sizeof(team_info));
	if (rc != B_OK)
		return rc;
	
	rc2 = user_memcpy(cookie, &kcookie, sizeof(int32));
	if (rc2 < 0)
		return rc2;
	
	rc2 = user_memcpy(info, &kinfo, sizeof(team_info));
	if (rc2 < 0)
		return rc2;
	
	return rc;
}


int
user_getenv(const char *userName, char **_userValue)
{
	char name[SYS_THREAD_STRING_LENGTH_MAX];
	char *value;
	int rc;

	if (!CHECK_USER_ADDRESS(userName)
		|| !CHECK_USER_ADDRESS(_userValue)
		|| user_strlcpy(name, userName, SYS_THREAD_STRING_LENGTH_MAX) < B_OK)
		return B_BAD_ADDRESS;

	rc = sys_getenv(name, &value);
	if (rc < 0)
		return rc;

	if (user_memcpy(_userValue, &value, sizeof(char *)) < B_OK)
		return B_BAD_ADDRESS;

	return rc;
}


int
user_setenv(const char *userName, const char *userValue, int overwrite)
{
	char name[SYS_THREAD_STRING_LENGTH_MAX];
	char value[SYS_THREAD_STRING_LENGTH_MAX];

	if (!CHECK_USER_ADDRESS(userName)
		|| !CHECK_USER_ADDRESS(userValue)
		|| user_strlcpy(name, userName, SYS_THREAD_STRING_LENGTH_MAX) < B_OK
		|| user_strlcpy(value, userValue, SYS_THREAD_STRING_LENGTH_MAX) < B_OK)
		return B_BAD_ADDRESS;

	return sys_setenv(name, value, overwrite);
}

