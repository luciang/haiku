/*
 * Copyright 2001-2005, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Pahtz <pahtz@yahoo.com.au>
 *		Axel Dörfler, axeld@pinc-software.de
 */

/** Class for low-overhead port-based messaging */

#include <stdlib.h>
#include <string.h>
#include <new>
#include <Region.h>
#include <Shape.h>

#include <ServerLink.h>
#include <ServerProtocol.h>


namespace BPrivate {

ServerLink::ServerLink()
{
}


ServerLink::~ServerLink()
{
}


status_t
ServerLink::ReadRegion(BRegion *region)
{
	fReceiver->Read(&region->fCount, sizeof(long));
	fReceiver->Read(&region->fBounds, sizeof(clipping_rect));
	if (!region->_SetSize(region->fCount))
		return B_NO_MEMORY;
	return fReceiver->Read(region->fData,
		region->fCount * sizeof(clipping_rect));
}


status_t
ServerLink::AttachRegion(const BRegion &region)
{
	fSender->Attach(&region.fCount, sizeof(long));
	fSender->Attach(&region.fBounds, sizeof(clipping_rect));
	return fSender->Attach(region.fData,
		region.fCount * sizeof(clipping_rect));
}


status_t
ServerLink::ReadShape(BShape *shape)
{
	int32 opCount, ptCount;
	fReceiver->Read(&opCount, sizeof(int32));
	fReceiver->Read(&ptCount, sizeof(int32));
	
	uint32 opList[opCount];
	if (opCount > 0)
		fReceiver->Read(opList, opCount * sizeof(uint32));
	
	BPoint ptList[ptCount];
	if (ptCount > 0)
		fReceiver->Read(ptList, ptCount * sizeof(BPoint));
	
	shape->SetData(opCount, ptCount, opList, ptList);
	return B_OK;
}


status_t
ServerLink::AttachShape(BShape &shape)
{
	int32 opCount, ptCount;
	uint32 *opList;
	BPoint *ptList;
	
	shape.GetData(&opCount, &ptCount, &opList, &ptList);
	
	fSender->Attach(&opCount, sizeof(int32));
	fSender->Attach(&ptCount, sizeof(int32));
	if (opCount > 0)
		fSender->Attach(opList, opCount * sizeof(uint32));
	if (ptCount > 0)
		fSender->Attach(ptList, ptCount * sizeof(BPoint));
	return B_OK;
}


status_t
ServerLink::FlushWithReply(int32 &code)
{
	status_t status = Flush(B_INFINITE_TIMEOUT, true);
	if (status < B_OK)
		return status;

	return GetNextMessage(code);
}

}	// namespace BPrivate
