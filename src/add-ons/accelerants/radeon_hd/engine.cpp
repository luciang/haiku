/*
 * Copyright 2006-2007, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Axel Dörfler, axeld@pinc-software.de
 */


#include <Debug.h>

#include "accelerant.h"
#include "accelerant_protos.h"


#undef TRACE

//#define TRACE_ENGINE
#ifdef TRACE_ENGINE
#	define TRACE(x) _sPrintf x
#else
#	define TRACE(x) ;
#endif


static engine_token sEngineToken = {1, 0 /*B_2D_ACCELERATION*/, NULL};


//	#pragma mark - engine management


status_t
radeon_acquire_engine(uint32 capabilities, uint32 maxWait, sync_token *syncToken,
	engine_token **_engineToken)
{
	TRACE(("radeon_acquire_engine()\n"));
	*_engineToken = &sEngineToken;

	if (acquire_lock(&gInfo->shared_info->engine_lock) != B_OK)
		return B_ERROR;

	return B_OK;
}


status_t
radeon_release_engine(engine_token *engineToken, sync_token *syncToken)
{
	TRACE(("radeon_release_engine()\n"));
	if (syncToken != NULL)
		syncToken->engine_id = engineToken->engine_id;

	release_lock(&gInfo->shared_info->engine_lock);
	return B_OK;
}


