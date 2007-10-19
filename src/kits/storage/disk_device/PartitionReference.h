/*
 * Copyright 2007, Ingo Weinhold, ingo_weinhold@gmx.de.
 * Distributed under the terms of the MIT License.
 */
#ifndef _PARTITION_REFERENCE_H
#define _PARTITION_REFERENCE_H

#include <DiskDeviceDefs.h>

#include <Referenceable.h>


namespace BPrivate {


class PartitionReference : public Referenceable {
public:
								PartitionReference(partition_id id = -1,
									uint32 changeCounter = 0);
								~PartitionReference();

			partition_id		PartitionID() const;
			void				SetPartitionID(partition_id id);

			uint32				ChangeCounter() const;
			void				SetChangeCounter(uint32 counter);

private:
			partition_id		fID;
			uint32				fChangeCounter;
};


}	// namespace BPrivate

using BPrivate::PartitionReference;

#endif	// _PARTITION_REFERENCE_H
