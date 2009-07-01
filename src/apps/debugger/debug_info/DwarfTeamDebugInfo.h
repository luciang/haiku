/*
 * Copyright 2009, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */
#ifndef DWARF_TEAM_DEBUG_INFO_H
#define DWARF_TEAM_DEBUG_INFO_H

#include "SpecificTeamDebugInfo.h"


class Architecture;
class DwarfManager;
class ImageInfo;


class DwarfTeamDebugInfo : public SpecificTeamDebugInfo {
public:
								DwarfTeamDebugInfo(Architecture* architecture);
	virtual						~DwarfTeamDebugInfo();

			status_t			Init();

	virtual	status_t			CreateImageDebugInfo(const ImageInfo& imageInfo,
									LocatableFile* imageFile,
									SpecificImageDebugInfo*& _imageDebugInfo);

private:
			Architecture*		fArchitecture;
			DwarfManager*		fManager;
};


#endif	// DWARF_TEAM_DEBUG_INFO_H
