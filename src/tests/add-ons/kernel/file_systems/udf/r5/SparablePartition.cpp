#include "SparablePartition.h"

#define B_NOT_IMPLEMENTED B_ERROR

using namespace Udf;

/*! \brief Creates a new SparablePartition object.
*/
SparablePartition::SparablePartition(uint16 number, uint32 start, uint32 length,
                                     uint16 packetLength, uint8 tableCount,
                                     uint32 *tableLocations)
	: fNumber(number)
	, fStart(start)
	, fLength(length)
	, fPacketLength(packetLength)
	, fTableCount(tableCount)
	, fInitStatus(B_NO_INIT)
{
	status_t error = (0 < TableCount() && TableCount() <= kMaxSparingTableCount)
	                 ? B_OK : B_BAD_VALUE;
	if (!error) {
		for (uint8 i = 0; i < TableCount(); i++)
			fTableLocations[i] = tableLocations[i];
	}
	if (!error)
		fInitStatus = B_OK;	
}

/*! \brief Destroys the SparablePartition object.
*/
SparablePartition::~SparablePartition()
{
}

/*! \brief Maps the given logical block to a physical block on disc.

	The sparing tables are first checked to see if the logical block has
	been remapped from a defective location to a non-defective one. If
	not, the given logical block is then simply treated as an offset from
	the	start of the physical partition.
*/
status_t
SparablePartition::MapBlock(uint32 logicalBlock, off_t &physicalBlock)
{
	status_t error = InitCheck();
	if (!error) {
		if (logicalBlock >= fLength)
			error = B_BAD_ADDRESS;
		else {
			// Check for the logical block in the sparing tables. If not
			// found, map directly to physical space.

			//physicalBlock = fStart + logicalBlock;
			//return B_OK;
			error = B_ERROR;
		}
	}
	return error;
}

/*! Returns the initialization status of the object.
*/
status_t
SparablePartition::InitCheck()
{
	return fInitStatus;
}
