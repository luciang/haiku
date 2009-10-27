/*
 * Copyright 2008, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Copyright 2008, Axel Dörfler, axeld@pinc-software.de.
 * Copyright 2006, Jérôme Duval. All rights reserved.
 * Distributed under the terms of the MIT License.
 */


#include <pthread.h>
#include "pthread_private.h"

#include <limits.h>
#include <stdlib.h>

#include <thread_defs.h>


int
pthread_attr_init(pthread_attr_t *_attr)
{
	pthread_attr *attr;

	if (_attr == NULL)
		return B_BAD_VALUE;

	attr = (pthread_attr *)malloc(sizeof(pthread_attr));
	if (attr == NULL)
		return B_NO_MEMORY;

	attr->detach_state = PTHREAD_CREATE_JOINABLE;
	attr->sched_priority = B_NORMAL_PRIORITY;
	attr->stack_size = USER_STACK_SIZE;

	*_attr = attr;
	return B_OK;
}


int
pthread_attr_destroy(pthread_attr_t *_attr)
{
	pthread_attr *attr;

	if (_attr == NULL || (attr = *_attr) == NULL)
		return B_BAD_VALUE;

	*_attr = NULL;
	free(attr);

	return B_OK;
}


int
pthread_attr_getdetachstate(const pthread_attr_t *_attr, int *state)
{
	pthread_attr *attr;

	if (_attr == NULL || (attr = *_attr) == NULL || state == NULL)
		return B_BAD_VALUE;

	*state = attr->detach_state;

	return B_OK;
}


int
pthread_attr_setdetachstate(pthread_attr_t *_attr, int state)
{
	pthread_attr *attr;

	if (_attr == NULL || (attr = *_attr) == NULL)
		return B_BAD_VALUE;

	if (state != PTHREAD_CREATE_JOINABLE && state != PTHREAD_CREATE_DETACHED)
		return B_BAD_VALUE;

	attr->detach_state = state;

	return B_OK;
}


int
pthread_attr_getstacksize(const pthread_attr_t *_attr, size_t *stacksize)
{
	pthread_attr *attr;

	if (_attr == NULL || (attr = *_attr) == NULL || stacksize == NULL)
		return B_BAD_VALUE;

	*stacksize = attr->stack_size;

	return 0;
}


int
pthread_attr_setstacksize(pthread_attr_t *_attr, size_t stacksize)
{
	pthread_attr *attr;

	if (_attr == NULL || (attr = *_attr) == NULL)
		return B_BAD_VALUE;

	if (stacksize < PTHREAD_STACK_MIN || stacksize > MAX_USER_STACK_SIZE)
		return B_BAD_VALUE;

	attr->stack_size = stacksize;

	return 0;
}


int
pthread_attr_getscope(const pthread_attr_t *attr, int *contentionScope)
{
	if (attr == NULL || contentionScope == NULL)
		return EINVAL;

	*contentionScope = PTHREAD_SCOPE_SYSTEM;
	return 0;
}


int
pthread_attr_setscope(pthread_attr_t *attr, int contentionScope)
{
	if (attr == NULL)
		return EINVAL;

	if (contentionScope != PTHREAD_SCOPE_SYSTEM)
		return ENOTSUP;

	return 0;
}


int
pthread_attr_setschedparam(pthread_attr_t *attr,
	const struct sched_param *param)
{
	if (attr == NULL || param == NULL)
		return EINVAL;

	(*attr)->sched_priority = param->sched_priority;

	return 0;
}


int
pthread_attr_getschedparam(const pthread_attr_t *attr,
	struct sched_param *param)
{
	if (attr == NULL || param == NULL)
		return EINVAL;

	param->sched_priority = (*attr)->sched_priority;

	return 0;
}

