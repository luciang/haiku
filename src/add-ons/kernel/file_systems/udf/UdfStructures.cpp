//----------------------------------------------------------------------
//  This software is part of the OpenBeOS distribution and is covered 
//  by the OpenBeOS license.
//
//  Copyright (c) 2003 Tyler Dauwalder, tyler@dauwalder.net
//----------------------------------------------------------------------

/*! \file UdfStructures.cpp

	UDF on-disk data structure definitions
*/

#include "UdfStructures.h"

#include <string.h>

#include "UdfString.h"
#include "Utils.h"

using namespace Udf;

//----------------------------------------------------------------------
// Constants
//----------------------------------------------------------------------

const charspec Udf::kCs0CharacterSet(0, "OSTA Compressed Unicode");
//const charspec kCs0Charspec = { _character_set_type: 0,
//                                _character_set_info: "OSTA Compressed Unicode"
//                                                    "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
//                                                    "\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0\0"
//                              };
                              
// Volume structure descriptor ids 
const char* Udf::kVSDID_BEA 		= "BEA01";
const char* Udf::kVSDID_TEA 		= "TEA01";
const char* Udf::kVSDID_BOOT 		= "BOOT2";
const char* Udf::kVSDID_ISO 		= "CD001";
const char* Udf::kVSDID_ECMA167_2 	= "NSR02";
const char* Udf::kVSDID_ECMA167_3 	= "NSR03";
const char* Udf::kVSDID_ECMA168		= "CDW02";

// entity_ids
const entity_id Udf::kMetadataPartitionMapId(0, "*UDF Metadata Partition");
const entity_id Udf::kSparablePartitionMapId(0, "*UDF Sparable Partition");
const entity_id Udf::kVirtualPartitionMapId(0, "*UDF Virtual Partition");
const entity_id Udf::kImplementationId(0, "*OpenBeOS UDF", implementation_id_suffix(OS_BEOS, BEOS_OPENBEOS));
const entity_id Udf::kPartitionContentsId(0, "+NSR03");

//! crc 010041 table, as generated by crc_table.cpp
const uint16 Udf::kCrcTable[256] = {
    0x0000, 0x1021, 0x2042, 0x3063, 0x4084, 0x50a5, 0x60c6, 0x70e7,
    0x8108, 0x9129, 0xa14a, 0xb16b, 0xc18c, 0xd1ad, 0xe1ce, 0xf1ef,
    0x1231, 0x0210, 0x3273, 0x2252, 0x52b5, 0x4294, 0x72f7, 0x62d6,
    0x9339, 0x8318, 0xb37b, 0xa35a, 0xd3bd, 0xc39c, 0xf3ff, 0xe3de,
    0x2462, 0x3443, 0x0420, 0x1401, 0x64e6, 0x74c7, 0x44a4, 0x5485,
    0xa56a, 0xb54b, 0x8528, 0x9509, 0xe5ee, 0xf5cf, 0xc5ac, 0xd58d,
    0x3653, 0x2672, 0x1611, 0x0630, 0x76d7, 0x66f6, 0x5695, 0x46b4,
    0xb75b, 0xa77a, 0x9719, 0x8738, 0xf7df, 0xe7fe, 0xd79d, 0xc7bc,
    0x48c4, 0x58e5, 0x6886, 0x78a7, 0x0840, 0x1861, 0x2802, 0x3823,
    0xc9cc, 0xd9ed, 0xe98e, 0xf9af, 0x8948, 0x9969, 0xa90a, 0xb92b,
    0x5af5, 0x4ad4, 0x7ab7, 0x6a96, 0x1a71, 0x0a50, 0x3a33, 0x2a12,
    0xdbfd, 0xcbdc, 0xfbbf, 0xeb9e, 0x9b79, 0x8b58, 0xbb3b, 0xab1a,
    0x6ca6, 0x7c87, 0x4ce4, 0x5cc5, 0x2c22, 0x3c03, 0x0c60, 0x1c41,
    0xedae, 0xfd8f, 0xcdec, 0xddcd, 0xad2a, 0xbd0b, 0x8d68, 0x9d49,
    0x7e97, 0x6eb6, 0x5ed5, 0x4ef4, 0x3e13, 0x2e32, 0x1e51, 0x0e70,
    0xff9f, 0xefbe, 0xdfdd, 0xcffc, 0xbf1b, 0xaf3a, 0x9f59, 0x8f78,
    0x9188, 0x81a9, 0xb1ca, 0xa1eb, 0xd10c, 0xc12d, 0xf14e, 0xe16f,
    0x1080, 0x00a1, 0x30c2, 0x20e3, 0x5004, 0x4025, 0x7046, 0x6067,
    0x83b9, 0x9398, 0xa3fb, 0xb3da, 0xc33d, 0xd31c, 0xe37f, 0xf35e,
    0x02b1, 0x1290, 0x22f3, 0x32d2, 0x4235, 0x5214, 0x6277, 0x7256,
    0xb5ea, 0xa5cb, 0x95a8, 0x8589, 0xf56e, 0xe54f, 0xd52c, 0xc50d,
    0x34e2, 0x24c3, 0x14a0, 0x0481, 0x7466, 0x6447, 0x5424, 0x4405,
    0xa7db, 0xb7fa, 0x8799, 0x97b8, 0xe75f, 0xf77e, 0xc71d, 0xd73c,
    0x26d3, 0x36f2, 0x0691, 0x16b0, 0x6657, 0x7676, 0x4615, 0x5634,
    0xd94c, 0xc96d, 0xf90e, 0xe92f, 0x99c8, 0x89e9, 0xb98a, 0xa9ab,
    0x5844, 0x4865, 0x7806, 0x6827, 0x18c0, 0x08e1, 0x3882, 0x28a3,
    0xcb7d, 0xdb5c, 0xeb3f, 0xfb1e, 0x8bf9, 0x9bd8, 0xabbb, 0xbb9a,
    0x4a75, 0x5a54, 0x6a37, 0x7a16, 0x0af1, 0x1ad0, 0x2ab3, 0x3a92,
    0xfd2e, 0xed0f, 0xdd6c, 0xcd4d, 0xbdaa, 0xad8b, 0x9de8, 0x8dc9,
    0x7c26, 0x6c07, 0x5c64, 0x4c45, 0x3ca2, 0x2c83, 0x1ce0, 0x0cc1,
    0xef1f, 0xff3e, 0xcf5d, 0xdf7c, 0xaf9b, 0xbfba, 0x8fd9, 0x9ff8,
    0x6e17, 0x7e36, 0x4e55, 0x5e74, 0x2e93, 0x3eb2, 0x0ed1, 0x1ef0
};

const uint32 Udf::kLogicalVolumeDescriptorBaseSize = sizeof(logical_volume_descriptor)
                                                     - (UDF_MAX_PARTITION_MAPS
                                                        * UDF_MAX_PARTITION_MAP_SIZE);


//----------------------------------------------------------------------
// Helper functions
//----------------------------------------------------------------------

const char *Udf::tag_id_to_string(tag_id id)
{
	switch (id) {
		case TAGID_UNDEFINED:
			return "undefined";

		case TAGID_PRIMARY_VOLUME_DESCRIPTOR:
			return "primary volume descriptor";			
		case TAGID_ANCHOR_VOLUME_DESCRIPTOR_POINTER:
			return "anchor volume descriptor pointer";
		case TAGID_VOLUME_DESCRIPTOR_POINTER:
			return "volume descriptor pointer";
		case TAGID_IMPLEMENTATION_USE_VOLUME_DESCRIPTOR:
			return "implementation use volume descriptor";
		case TAGID_PARTITION_DESCRIPTOR:
			return "partition descriptor";
		case TAGID_LOGICAL_VOLUME_DESCRIPTOR:
			return "logical volume descriptor";
		case TAGID_UNALLOCATED_SPACE_DESCRIPTOR:
			return "unallocated space descriptor";
		case TAGID_TERMINATING_DESCRIPTOR:
			return "terminating descriptor";
		case TAGID_LOGICAL_VOLUME_INTEGRITY_DESCRIPTOR:
			return "logical volume integrity descriptor";

		case TAGID_FILE_SET_DESCRIPTOR:
			return "file set descriptor";
		case TAGID_FILE_IDENTIFIER_DESCRIPTOR:
			return "file identifier descriptor";
		case TAGID_ALLOCATION_EXTENT_DESCRIPTOR:
			return "allocation extent descriptor";
		case TAGID_INDIRECT_ENTRY:
			return "indirect entry";
		case TAGID_TERMINAL_ENTRY:
			return "terminal entry";
		case TAGID_FILE_ENTRY:
			return "file entry";
		case TAGID_EXTENDED_ATTRIBUTE_HEADER_DESCRIPTOR:
			return "extended attribute header descriptor";
		case TAGID_UNALLOCATED_SPACE_ENTRY:
			return "unallocated space entry";
		case TAGID_SPACE_BITMAP_DESCRIPTOR:
			return "space bitmap descriptor";
		case TAGID_PARTITION_INTEGRITY_ENTRY:
			return "partition integrity entry";
		case TAGID_EXTENDED_FILE_ENTRY:
			return "extended file entry";

		default:
			if (TAGID_CUSTOM_START <= id && id <= TAGID_CUSTOM_END)
				return "custom";
			return "reserved";	
	}
}


//----------------------------------------------------------------------
// volume_structure_descriptor_header
//----------------------------------------------------------------------

volume_structure_descriptor_header::volume_structure_descriptor_header(uint8 type, const char *_id, uint8 version)
	: type(type)
	, version(version)
{
	memcpy(id, _id, 5);
}


/*! \brief Returns true if the given \a id matches the header's id.
*/
bool
volume_structure_descriptor_header::id_matches(const char *id)
{
	return strncmp(this->id, id, 5) == 0;
}


//----------------------------------------------------------------------
// charspec
//----------------------------------------------------------------------

charspec::charspec(uint8 type, const char *info)
{
	set_character_set_type(type);
	set_character_set_info(info);
}

void
charspec::dump() const
{
	DUMP_INIT("charspec");
	PRINT(("character_set_type: %d\n", character_set_type()));
	PRINT(("character_set_info: `%s'\n", character_set_info()));
}

void
charspec::set_character_set_info(const char *info)
{
	memset(_character_set_info, 0, 63);
	if (info)
		strncpy(_character_set_info, info, 63);
}	

//----------------------------------------------------------------------
// timestamp
//----------------------------------------------------------------------

#if !USER
static
int
get_month_length(int month, int year)
{
	if (0 <= month && month < 12 && year >= 1970) {
		const int monthLengths[12] = { 31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31 };
		int result = monthLengths[month];
		if (month == 1 && ((year - 1968) % 4 == 0))
			result++;
		return result;
	} else {
		DEBUG_INIT_ETC(NULL, ("month: %d, year: %d", month, year));
		PRINT(("Invalid month or year! Returning 0\n"));
		return 0;
	}
}
#endif

timestamp::timestamp(time_t time)
{
#if USER
	// Is it me, or is localtime() broken?
	tm *local = localtime(&time);
	if (local) {
		set_microsecond(0);
		set_hundred_microsecond(0);
		set_centisecond(0);
		set_second(local->tm_sec);
		set_minute(local->tm_min);
		set_hour(local->tm_hour);
		set_day(local->tm_mday);
		set_month(local->tm_mon+1);
		set_year(local->tm_year+1900);
		set_type(1);
		set_timezone(local->tm_gmtoff / 60);
	} else {
		_clear();
	}
#else	// no localtime() in the R5 kernel...
	// real_time_clock() is returning the time offset by -16 hours.
	// Considering I'm -8 hours from GMT, this doesn't really make
	// sense. For the moment I'm offsetting it manually here, but
	// I'm not sure what the freaking deal is, and unfortunately,
	// localtime() appears to be broken...
	time += 16 * 60 * 60;

	set_microsecond(0);
	set_hundred_microsecond(0);
	set_centisecond(0);
	set_second(time % 60);
	time = time / 60;	// convert to minutes
	set_minute(time % 60);
	time = time / 60;	// convert to hours
	set_hour(time % 24);
	time = time / 24;	// convert to days
	
	// From here we start at time == 0 and count up
	// by days until we figure out what the day, month,
	// and year are.
	int year = 0;
	int month = 0;
	time_t clock = 0;
	for (clock = 0;
	       clock + get_month_length(month, year+1970) < time; 
	         clock += get_month_length(month, year+1970))
	{		
		month++;
		if (month == 12) {
			year++;
			month = 0;
		}
	}
	int day = time - clock;
	set_day(day);
	set_month(month+1);
	set_year(year+1970);
	set_type(1);
	set_timezone(-2047); // -2047 == no timezone specified
#endif
}

void
timestamp::dump() const
{
	DUMP_INIT("timestamp");
	PRINT(("type:                %d\n", type()));
	PRINT(("timezone:            %d\n", timezone()));
	PRINT(("year:                %d\n", year()));
	PRINT(("month:               %d\n", month()));
	PRINT(("day:                 %d\n", day()));
	PRINT(("hour:                %d\n", hour()));
	PRINT(("minute:              %d\n", minute()));
	PRINT(("second:              %d\n", second()));
	PRINT(("centisecond:         %d\n", centisecond()));
	PRINT(("hundred_microsecond: %d\n", hundred_microsecond()));
	PRINT(("microsecond:         %d\n", microsecond()));
}

void
timestamp::_clear()
{
	set_microsecond(0);
	set_hundred_microsecond(0);
	set_centisecond(0);
	set_second(0);
	set_minute(0);
	set_hour(0);
	set_day(0);
	set_month(0);
	set_year(0);
	set_type(0);
	set_timezone(0);
}

//----------------------------------------------------------------------
// implementation_id_suffix
//----------------------------------------------------------------------

implementation_id_suffix::implementation_id_suffix(uint8 os_class,
                                                   uint8 os_identifier)
	: _os_class(os_class)
	, _os_identifier(os_identifier)
{
	memset(_implementation_use.data, 0, _implementation_use.size());
}                                                   

//----------------------------------------------------------------------
// domain_id_suffix
//----------------------------------------------------------------------

domain_id_suffix::domain_id_suffix(uint16 udfRevision, uint8 domainFlags)
	: _udf_revision(udfRevision)
	, _domain_flags(domainFlags)
{
	memset(_reserved.data, 0, _reserved.size());
}                                                   

//----------------------------------------------------------------------
// entity_id
//----------------------------------------------------------------------

entity_id::entity_id(uint8 flags, char *identifier, uint8 *identifier_suffix)
	: _flags(flags)
{
	memset(_identifier, 0, kIdentifierLength);
	if (identifier)
		strncpy(_identifier, identifier, kIdentifierLength);
	if (identifier_suffix)
		memcpy(_identifier_suffix.data, identifier_suffix, kIdentifierSuffixLength);
	else
		memset(_identifier_suffix.data, 0, kIdentifierSuffixLength);
}

entity_id::entity_id(uint8 flags, char *identifier,
	                 const implementation_id_suffix &suffix)
	: _flags(flags)
{
	memset(_identifier, 0, kIdentifierLength);
	if (identifier)
		strncpy(_identifier, identifier, kIdentifierLength);
	memcpy(_identifier_suffix.data, &suffix, kIdentifierSuffixLength);
}	                 

entity_id::entity_id(uint8 flags, char *identifier,
	                 const domain_id_suffix &suffix)
	: _flags(flags)
{
	memset(_identifier, 0, kIdentifierLength);
	if (identifier)
		strncpy(_identifier, identifier, kIdentifierLength);
	memcpy(_identifier_suffix.data, &suffix, kIdentifierSuffixLength);
}	                 

void
entity_id::dump() const
{
	DUMP_INIT("entity_id");
	PRINT(("flags:             %d\n", flags()));
	PRINT(("identifier:        `%.23s'\n", identifier()));
	PRINT(("identifier_suffix:\n"));
	DUMP(identifier_suffix());
}

bool
entity_id::matches(const entity_id &id) const
{
	bool result = true;
	for (int i = 0; i < entity_id::kIdentifierLength; i++) {
		if (identifier()[i] != id.identifier()[i]) {
			result = false;
			break;
		}
	}
	return result;
}

//----------------------------------------------------------------------
// extent_address
//----------------------------------------------------------------------

extent_address::extent_address(uint32 location, uint32 length)
{
	set_location(location);
	set_length(length);
}

void
extent_address::dump() const
{
	DUMP_INIT("extent_address");
	PRINT(("length:   %ld\n", length()));
	PRINT(("location: %ld\n", location()));
}

void
logical_block_address::dump() const
{
	DUMP_INIT("logical_block_address");
	PRINT(("block:     %ld\n", block()));
	PRINT(("partition: %d\n", partition()));
}

long_address::long_address(uint16 partition, uint32 block, uint32 length,
	                       uint8 type)
{
	set_partition(partition);
	set_block(block);
	set_length(length);
	set_type(type);
}
	                       
void
long_address::dump() const
{
	DUMP_INIT("long_address");
	PRINT(("length:   %ld\n", length()));
	PRINT(("block:    %ld\n", block()));
	PRINT(("partition: %d\n", partition()));
	PRINT(("implementation_use:\n"));
	DUMP(implementation_use());
}

//----------------------------------------------------------------------
// descriptor_tag 
//----------------------------------------------------------------------

void
descriptor_tag::dump() const
{
	DUMP_INIT("descriptor_tag");
	PRINT(("id:            %d (%s)\n", id(), tag_id_to_string(tag_id(id()))));
	PRINT(("version:       %d\n", version()));
	PRINT(("checksum:      %d\n", checksum()));
	PRINT(("serial_number: %d\n", serial_number()));
	PRINT(("crc:           %d\n", crc()));
	PRINT(("crc_length:    %d\n", crc_length()));
	PRINT(("location:      %ld\n", location()));
}


/*! \brief Calculates the tag's CRC, verifies the tag's checksum, and
	verifies the tag's location on the medium.
	
	Note that this function makes the assumption that the descriptor_tag
	is the first data member in a larger descriptor structure, the remainder
	of which immediately follows the descriptor_tag itself in memory. This
	is generally a safe assumption, as long as the entire descriptor (and
	not the its tag) is read in before init_check() is called. If this is
	not the case, it's best to call this function with a \a calculateCrc
	value of false, to keep from trying to calculate a crc value on invalid
	and possibly unowned memory.
	
	\param block The block location of this descriptor as taken from the
	             corresponding allocation descriptor. If the address specifies
	             a block in a partition, the partition block is the desired
	             location, not the mapped physical disk block.
	\param calculateCrc Whether or not to perform the crc calculation
	                    on the descriptor data following the tag.                
*/
status_t 
descriptor_tag::init_check(uint32 block, bool calculateCrc)
{
	DEBUG_INIT_ETC("descriptor_tag", ("location: %ld, calculateCrc: %s",
	               block, bool_to_string(calculateCrc)));
	PRINT(("location   (paramater)    == %ld\n", block));
	PRINT(("location   (in structure) == %ld\n", location()));
	if (calculateCrc) {
		PRINT(("crc        (calculated)   == %d\n",
		       Udf::calculate_crc(reinterpret_cast<uint8*>(this)+sizeof(descriptor_tag),
		       crc_length())))
	} else {
		PRINT(("crc        (calculated)   == (not calculated)\n"));
	}
	PRINT(("crc        (in structure) == %d\n", crc()));
	PRINT(("crc_length (in structure) == %d\n", crc_length()));
	// location
	status_t error = (block == location()) ? B_OK : B_NO_INIT;
	// checksum
	if (!error) {
		uint32 sum = 0;
		for (int i = 0; i <= 3; i++)
			sum += reinterpret_cast<uint8*>(this)[i];
		for (int i = 5; i <= 15; i++)
			sum += reinterpret_cast<uint8*>(this)[i];
		error = sum % 256 == checksum() ? B_OK : B_NO_INIT;
	}
	// crc
	if (!error && calculateCrc) {
		uint16 _crc = Udf::calculate_crc(reinterpret_cast<uint8*>(this)
		               + sizeof(descriptor_tag), crc_length());
		error = _crc == crc() ? B_OK : B_NO_INIT;
	}	
	RETURN(error);	
}

//----------------------------------------------------------------------
// primary_volume_descriptor
//----------------------------------------------------------------------

void
primary_volume_descriptor::dump() const
{
	DUMP_INIT("primary_volume_descriptor");
	
	String string;
	
	PRINT(("tag:\n"));
	DUMP(tag());
	PRINT(("vds_number:                       %ld\n", vds_number()));
	PRINT(("primary_volume_descriptor_number: %ld\n", primary_volume_descriptor_number()));
	string = volume_identifier();
	PRINT(("volume_identifier:                `%s'\n", string.Utf8()));
	PRINT(("volume_sequence_number:           %d\n", volume_sequence_number()));
	PRINT(("max_volume_sequence_number:       %d\n", max_volume_sequence_number()));
	PRINT(("interchange_level:                %d\n", interchange_level()));
	PRINT(("max_interchange_level:            %d\n", max_interchange_level()));
	PRINT(("character_set_list:               %ld\n", character_set_list()));
	PRINT(("max_character_set_list:           %ld\n", max_character_set_list()));
	string = volume_set_identifier();
	PRINT(("volume_set_identifier:            `%s'\n", string.Utf8()));
	PRINT(("descriptor_character_set:\n"));
	DUMP(descriptor_character_set());
	PRINT(("explanatory_character_set:\n"));
	DUMP(explanatory_character_set());
	PRINT(("volume_abstract:\n"));
	DUMP(volume_abstract());
	PRINT(("volume_copyright_notice:\n"));
	DUMP(volume_copyright_notice());
	PRINT(("application_id:\n"));
	DUMP(application_id());
	PRINT(("recording_date_and_time:\n"));
	DUMP(recording_date_and_time());
	PRINT(("implementation_id:\n"));
	DUMP(implementation_id());
	PRINT(("implementation_use:\n"));
	DUMP(implementation_use());
	PRINT(("predecessor_vds_location:         %ld\n",
	       predecessor_volume_descriptor_sequence_location()));
	PRINT(("flags:                            %d\n", flags()));       
}


//----------------------------------------------------------------------
// anchor_volume_descriptor_pointer
//----------------------------------------------------------------------

void
anchor_volume_descriptor::dump() const
{
	DUMP_INIT("anchor_volume_descriptor");
	PRINT(("tag:\n"));
	DUMP(tag());
	PRINT(("main_vds:\n"));
	DUMP(main_vds());
	PRINT(("reserve_vds:\n"));
	DUMP(reserve_vds());
}


//----------------------------------------------------------------------
// implementation_use_descriptor
//----------------------------------------------------------------------

void
implementation_use_descriptor::dump() const
{
	DUMP_INIT("implementation_use_descriptor");
	PRINT(("tag:\n"));
	DUMP(tag());
	PRINT(("vds_number: %ld\n", vds_number()));
	PRINT(("implementation_id:\n"));
	DUMP(implementation_id());
	PRINT(("implementation_use: XXX\n"));
	DUMP(implementation_use());
}

//----------------------------------------------------------------------
// partition_descriptor
//----------------------------------------------------------------------

const uint8 Udf::kMaxPartitionDescriptors = 2;

void
partition_descriptor::dump() const
{
	DUMP_INIT("partition_descriptor");
	PRINT(("tag:\n"));
	DUMP(tag());
	PRINT(("vds_number:                %ld\n", vds_number()));
	PRINT(("partition_flags:           %d\n", partition_flags()));
	PRINT(("partition_flags.allocated: %s\n", allocated() ? "true" : "false"));
	PRINT(("partition_number:          %d\n", partition_number()));
	PRINT(("partition_contents:\n"));
	DUMP(partition_contents());
	PRINT(("partition_contents_use:    XXX\n"));
	DUMP(partition_contents_use());
	PRINT(("access_type:               %ld\n", access_type()));
	PRINT(("start:                     %ld\n", start()));
	PRINT(("length:                    %ld\n", length()));
	PRINT(("implementation_id:\n"));
	DUMP(implementation_id());
	PRINT(("implementation_use:        XXX\n"));
	DUMP(implementation_use());
}

//----------------------------------------------------------------------
// logical_volume_descriptor
//----------------------------------------------------------------------

void
logical_volume_descriptor::dump() const
{
	DUMP_INIT("logical_volume_descriptor");
	PRINT(("tag:\n"));
	DUMP(tag());
	PRINT(("vds_number:                %ld\n", vds_number()));
	PRINT(("character_set:\n"));
	DUMP(character_set());
	String string(logical_volume_identifier());
	PRINT(("logical_volume_identifier: `%s'\n", string.Utf8()));
	PRINT(("logical_block_size:        %ld\n", logical_block_size()));
	PRINT(("domain_id:\n"));
	DUMP(domain_id());
	PRINT(("logical_volume_contents_use:\n"));
	DUMP(logical_volume_contents_use());
	PRINT(("file_set_address:\n"));
	DUMP(file_set_address());
	PRINT(("map_table_length:          %ld\n", map_table_length()));
	PRINT(("partition_map_count:       %ld\n", partition_map_count()));
	PRINT(("implementation_id:\n"));
	DUMP(implementation_id());
	PRINT(("implementation_use:\n"));
	DUMP(implementation_use());
	PRINT(("integrity_sequence_extent:\n"));
	DUMP(integrity_sequence_extent());
//	PRINT(("partition_maps:\n"));
	const uint8 *maps = partition_maps();
	int offset = 0;
	for (uint i = 0; i < partition_map_count(); i++) {
		PRINT(("partition_map #%d:\n", i));
		uint8 type = maps[offset];
		uint8 length = maps[offset+1];
		PRINT(("  type: %d\n", type));
		PRINT(("  length: %d\n", length));
		switch (type) {
			case 1:
				for (int j = 0; j < length-2; j++) 
					PRINT(("  data[%d]: %d\n", j, maps[offset+2+j]));
				break;
			case 2: {
				PRINT(("  partition_number: %d\n", *reinterpret_cast<const uint16*>(&(maps[offset+38]))));
				PRINT(("  entity_id:\n"));
				const entity_id *id = reinterpret_cast<const entity_id*>(&(maps[offset+4]));
				if (id)	// To kill warning when DEBUG==0
					PDUMP(id);
				break;
			}
		}
		offset += maps[offset+1];
	}
	// \todo dump partition_maps
}


logical_volume_descriptor&
logical_volume_descriptor::operator=(const logical_volume_descriptor &rhs)
{
	_tag = rhs._tag;
	_vds_number = rhs._vds_number;
	_character_set = rhs._character_set;
	_logical_volume_identifier = rhs._logical_volume_identifier;
	_logical_block_size = rhs._logical_block_size;
	_domain_id = rhs._domain_id;
	_logical_volume_contents_use = rhs._logical_volume_contents_use;
	_map_table_length = rhs._map_table_length;
	_partition_map_count = rhs._partition_map_count;
	_implementation_id = rhs._implementation_id;
	_implementation_use = rhs._implementation_use;
	_integrity_sequence_extent = rhs._integrity_sequence_extent;
	// copy the partition maps one by one
	uint8 *lhsMaps = partition_maps();
	const uint8 *rhsMaps = rhs.partition_maps();
	int offset = 0;
	for (uint8 i = 0; i < rhs.partition_map_count(); i++) {
		uint8 length = rhsMaps[offset+1];
		memcpy(&lhsMaps[offset], &rhsMaps[offset], length);
		offset += length;		
	}
	return *this;
}


//----------------------------------------------------------------------
// physical_partition_map 
//----------------------------------------------------------------------

void
physical_partition_map::dump()
{
	DUMP_INIT("physical_partition_map");
	PRINT(("type: %d\n", type()));
	PRINT(("length: %d\n", length()));
	PRINT(("volume_sequence_number: %d\n", volume_sequence_number()));
	PRINT(("partition_number: %d\n", partition_number()));
}

//----------------------------------------------------------------------
// sparable_partition_map 
//----------------------------------------------------------------------

void
sparable_partition_map::dump()
{
	DUMP_INIT("sparable_partition_map");
	PRINT(("type: %d\n", type()));
	PRINT(("length: %d\n", length()));
	PRINT(("partition_type_id:"));
	DUMP(partition_type_id());
	PRINT(("volume_sequence_number: %d\n", volume_sequence_number()));
	PRINT(("partition_number: %d\n", partition_number()));
	PRINT(("sparing_table_count: %d\n", sparing_table_count()));
	PRINT(("sparing_table_size: %ld\n", sparing_table_size()));
	PRINT(("sparing_table_locations:"));
	for (uint8 i = 0; i < sparing_table_count(); i++)
		PRINT(("  %d: %ld\n", i, sparing_table_location(i)));
}

//----------------------------------------------------------------------
// unallocated_space_descriptor
//----------------------------------------------------------------------

void
unallocated_space_descriptor::dump() const
{
	DUMP_INIT("unallocated_space_descriptor");
	PRINT(("tag:\n"));
	DUMP(tag());
	PRINT(("vds_number:                  %ld\n", vds_number()));
	PRINT(("allocation_descriptor_count: %ld\n", allocation_descriptor_count()));
	// \todo dump alloc_descriptors
}


//----------------------------------------------------------------------
// terminating_descriptor
//----------------------------------------------------------------------

void
terminating_descriptor::dump() const
{
	DUMP_INIT("terminating_descriptor");
	PRINT(("tag:\n"));
	DUMP(tag());
}

//----------------------------------------------------------------------
// file_set_descriptor
//----------------------------------------------------------------------

void
file_set_descriptor::dump() const
{
	DUMP_INIT("file_set_descriptor");
	PRINT(("tag:\n"));
	DUMP(tag());
	PRINT(("recording_date_and_time:\n"));
	DUMP(recording_date_and_time());
	PRINT(("interchange_level: %d\n", interchange_level()));
	PRINT(("max_interchange_level: %d\n", max_interchange_level()));
	PRINT(("character_set_list: %ld\n", character_set_list()));
	PRINT(("max_character_set_list: %ld\n", max_character_set_list()));
	PRINT(("file_set_number: %ld\n", file_set_number()));
	PRINT(("file_set_descriptor_number: %ld\n", file_set_descriptor_number()));
	PRINT(("logical_volume_id_character_set:\n"));
	DUMP(logical_volume_id_character_set());
	PRINT(("logical_volume_id:\n"));
	DUMP(logical_volume_id());
	PRINT(("file_set_id_character_set:\n"));
	DUMP(file_set_id_character_set());
	PRINT(("file_set_id:\n"));
	DUMP(file_set_id());
	PRINT(("copyright_file_id:\n"));
	DUMP(copyright_file_id());
	PRINT(("abstract_file_id:\n"));
	DUMP(abstract_file_id());
	PRINT(("root_directory_icb:\n"));
	DUMP(root_directory_icb());
	PRINT(("domain_id:\n"));
	DUMP(domain_id());
	PRINT(("next_extent:\n"));
	DUMP(next_extent());
	PRINT(("system_stream_directory_icb:\n"));
	DUMP(system_stream_directory_icb());
}

void
file_id_descriptor::dump() const
{
	DUMP_INIT("file_id_descriptor");
	PRINT(("tag:\n"));
	DUMP(tag());
	PRINT(("version_number:            %d\n", version_number()));
	PRINT(("may_be_hidden:             %d\n", may_be_hidden()));
	PRINT(("is_directory:              %d\n", is_directory()));
	PRINT(("is_deleted:                %d\n", is_deleted()));
	PRINT(("is_parent:                 %d\n", is_parent()));
	PRINT(("is_metadata_stream:        %d\n", is_metadata_stream()));
	PRINT(("id_length:                 %d\n", id_length()));
	PRINT(("icb:\n"));
	DUMP(icb());
	PRINT(("implementation_use_length: %d\n", is_parent()));
	PRINT(("id: `"));
	for (int i = 0; i < id_length(); i++)
		SIMPLE_PRINT(("%c", id()[i]));
	SIMPLE_PRINT(("'\n"));
}

void
icb_entry_tag::dump() const
{
	DUMP_INIT("icb_entry_tag");
	PRINT(("prior_entries: %ld\n", prior_recorded_number_of_direct_entries()));
	PRINT(("strategy_type: %d\n", strategy_type()));
	PRINT(("strategy_parameters:\n"));
	DUMP(strategy_parameters());
	PRINT(("entry_count: %d\n", entry_count()));
	PRINT(("file_type: %d\n", file_type()));
	PRINT(("parent_icb_location:\n"));
	DUMP(parent_icb_location());
	PRINT(("all_flags: %d\n", flags()));
	
/*
	uint32 prior_recorded_number_of_direct_entries;
	uint16 strategy_type;
	array<uint8, 2> strategy_parameters;
	uint16 entry_count;
	uint8 reserved;
	uint8 file_type;
	logical_block_address parent_icb_location;
	union {
		uint16 all_flags;
		struct {
			uint16	descriptor_flags:3,			
					if_directory_then_sort:1,	//!< To be set to 0 per UDF-2.01 2.3.5.4
					non_relocatable:1,
					archive:1,
					setuid:1,
					setgid:1,
					sticky:1,
					contiguous:1,
					system:1,
					transformed:1,
					multi_version:1,			//!< To be set to 0 per UDF-2.01 2.3.5.4
					is_stream:1,
					reserved_icb_entry_flags:2;
		} flags;
	};

*/

}

void
icb_header::dump() const
{
	DUMP_INIT("icb_header");

	PRINT(("tag:\n"));
	DUMP(tag());
	PRINT(("icb_tag:\n"));
	DUMP(icb_tag());
	
}
