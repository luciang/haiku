/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */

#include "Image.h"

#include "ImageDebugInfo.h"
#include "LocatableFile.h"
#include "Team.h"


Image::Image(Team* team,const ImageInfo& imageInfo, LocatableFile* imageFile)
	:
	fTeam(team),
	fInfo(imageInfo),
	fImageFile(imageFile),
	fDebugInfo(NULL),
	fDebugInfoState(IMAGE_DEBUG_INFO_NOT_LOADED)
{
	if (fImageFile != NULL)
		fImageFile->AcquireReference();
}


Image::~Image()
{
	if (fDebugInfo != NULL)
		fDebugInfo->ReleaseReference();
	if (fImageFile != NULL)
		fImageFile->ReleaseReference();
}


status_t
Image::Init()
{
	return B_OK;
}


bool
Image::ContainsAddress(target_addr_t address) const
{
	return (address >= fInfo.TextBase()
			&& address < fInfo.TextBase() + fInfo.TextSize())
		|| (address >= fInfo.DataBase()
			&& address < fInfo.DataBase() + fInfo.DataSize());
}


void
Image::SetImageDebugInfo(ImageDebugInfo* debugInfo,
	image_debug_info_state state)
{
	if (debugInfo == fDebugInfo && state == fDebugInfoState)
		return;

	if (fDebugInfo != NULL)
		fDebugInfo->RemoveReference();

	fDebugInfo = debugInfo;
	fDebugInfoState = state;

	if (fDebugInfo != NULL)
		fDebugInfo->AddReference();

	// notify listeners
	fTeam->NotifyImageDebugInfoChanged(this);
}
