/*
 * Copyright 2002-2007 Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT license.
 */
#ifndef SUPPORT_H
#define SUPPORT_H


#include <DiskDeviceDefs.h>
#include <HashMap.h>
#include <HashString.h>
#include <Slider.h>
#include <String.h>


class BPartition;


const char* string_for_size(off_t size, char *string);

void dump_partition_info(const BPartition* partition);

bool is_valid_partitionable_space(size_t size);

enum {
	GO_CANCELED	= 0,
	GO_SUCCESS
};

class SpaceIDMap : public HashMap<HashString, partition_id> {
public:
								SpaceIDMap();
	virtual						~SpaceIDMap();

			partition_id		SpaceIDFor(partition_id parentID,
									off_t spaceOffset);

private:
			partition_id		fNextSpaceID;
};

class SizeSlider : public BSlider {
public:
								SizeSlider(const char* name, const char* label,
        							BMessage* message, int32 minValue,
        							int32 maxValue);
	virtual						~SizeSlider();

	virtual const char*			UpdateText() const;

private:
			off_t				fOffset;
			off_t				fSize;
	mutable	BString				fStatusLabel;
};

#endif // SUPPORT_H
