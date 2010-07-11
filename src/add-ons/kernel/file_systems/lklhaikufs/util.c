/*
 * Copyright 2010 Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *			Lucian Adrian Grijincu, lucian.grijincu@gmail.com
 */

#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include "util.h"

char *
pathJoin(const char * dirName, const char * fileName)
{
	int len = strlen(dirName) + sizeof('/') + strlen(fileName) + sizeof('\0');
	char * path = malloc(len);
	if (path != NULL)
		snprintf(path, len, "%s/%s", dirName, fileName);

	return path;
}
