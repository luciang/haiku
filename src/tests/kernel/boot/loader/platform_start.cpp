/*
** Copyright 2003, Axel Dörfler, axeld@pinc-software.de. All rights reserved.
** Distributed under the terms of the OpenBeOS License.
*/


#include <boot/platform.h>


extern "C" int boot(struct stage2_args *args);

int
main(int argc, char **argv)
{
	boot(NULL);
	return 0;
}

