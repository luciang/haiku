/* 
** Copyright 2004, J�r�me Duval, korli@users.sourceforge.net. All rights reserved.
** Distributed under the terms of the Haiku License.
*/


#include <scheduler.h>

int32 
suggest_thread_priority(uint32 what, int32 period, bigtime_t jitter, bigtime_t length)
{
	return 0;
}

bigtime_t 
estimate_max_scheduling_latency(thread_id th)
{
	if (th == -1)
		th = find_thread(NULL);

	return 0;
}

