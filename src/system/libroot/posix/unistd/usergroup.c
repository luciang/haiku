/* 
** Copyright 2002, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
** Distributed under the terms of the OpenBeOS License.
*/


#include <unistd.h>
#include <syscalls.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>


// ToDo: implement the user/group functions for real!


gid_t 
getegid(void)
{
	return 0;
}


uid_t 
geteuid(void)
{
	return 0;
}


gid_t 
getgid(void)
{
	return 0;
}


int 
getgroups(int groupSize, gid_t groupList[])
{
	return 0;
}


uid_t 
getuid(void)
{
	return 0;
}


char *
cuserid(char *s)
{
	if (s != NULL && getlogin_r(s, L_cuserid))
		return s;

	return getlogin();
}


int 
setgid(gid_t gid)
{
	return EPERM;
}


int 
setuid(uid_t uid)
{
	return EPERM;
}


int
setegid(gid_t gid)
{
	return EPERM;
}


int
seteuid(uid_t uid)
{
	return EPERM;
}


char *
getlogin(void)
{
	return "baron";
}


int 
getlogin_r(char *name, size_t nameSize)
{
	strlcpy(name, "baron", nameSize);
	return 0;
}

