//----------------------------------------------------------------------
//  This software is part of the OpenBeOS distribution and is covered 
//  by the OpenBeOS license.
//
//  Copyright (c) 2003 Tyler Dauwalder, tyler@dauwalder.net
//---------------------------------------------------------------------
#ifndef _UDF_DISK_STRUCTURES_H
#define _UDF_DISK_STRUCTURES_H

#include <string.h>

#include <ByteOrder.h>
#include <SupportDefs.h>

#include "cpp.h"
#include "UdfDebug.h"

#include "Array.h"

/*! \file DiskStructures.h

	\brief UDF on-disk data structure declarations
	
	UDF is a specialization of the ECMA-167 standard. For the most part,
	ECMA-167 structures are used by UDF with special restrictions. In a
	few instances, UDF introduces its own structures to augment those
	supplied by ECMA-167; those structures are clearly marked.
	
	For UDF info: <a href='http://www.osta.org'>http://www.osta.org</a>
	For ECMA info: <a href='http://www.ecma-international.org'>http://www.ecma-international.org</a>

	For lack of a better place to store this info, the structures that
	are allowed to have length greater than the logical block size are
	as follows (other length restrictions may be found in UDF-2.01 5.1):
	- \c udf_logical_volume_descriptor
	- \c udf_unallocated_space_descriptor
	- \c udf_logical_volume_integrity_descriptor
	- \c udf_space_bitmap_descriptor

	Other links of interest:
	- <a href='http://www.extra.research.philips.com/udf/'>Philips UDF verifier</a>

*/

namespace Udf {

//----------------------------------------------------------------------
// ECMA-167 Part 1
//----------------------------------------------------------------------

/*! \brief Character set specifications

	The character_set_info field shall be set to the ASCII string
	"OSTA Compressed Unicode" (padded right with NULL chars).
	
	See also: ECMA 167 1/7.2.1, UDF-2.01 2.1.2
*/
struct udf_charspec {
public:
	void dump();

	uint8 character_set_type() const { return _character_set_type; } 
	const char* character_set_info() const { return _character_set_info; }
	char* character_set_info() { return _character_set_info; }
	
	void set_character_set_type(uint8 type) { _character_set_type = type; } 
private:
	uint8 _character_set_type;	//!< to be set to 0 to indicate CS0
	char _character_set_info[63];	//!< "OSTA Compressed Unicode"
} __attribute__((packed));


extern const udf_charspec kCS0Charspec;


/*! \brief Date and time stamp 

	See also: ECMA 167 1/7.3, UDF-2.01 2.1.4
*/
class udf_timestamp {
private:
	union type_and_timezone_accessor {
		uint16 type_and_timezone;
		struct {
			uint16 timezone:12,
			       type:4;
		} bits;
	};

public:
	void dump();

	// Get functions
	uint16 type_and_timezone() const { return B_LENDIAN_TO_HOST_INT16(_type_and_timezone); }
	uint8 type() const {
		type_and_timezone_accessor t;
		t.type_and_timezone = type_and_timezone();
		return t.bits.type;
	}
	uint16 timezone() const {
		type_and_timezone_accessor t;
		t.type_and_timezone = type_and_timezone();
		return t.bits.timezone;
	}
	uint16 year() const { return B_LENDIAN_TO_HOST_INT16(_year); }
	uint8 month() const { return _month; }
	uint8 day() const { return _day; }
	uint8 hour() const { return _hour; }
	uint8 minute() const { return _minute; }
	uint8 second() const { return _second; }
	uint8 centisecond() const { return _centisecond; }
	uint8 hundred_microsecond() const { return _hundred_microsecond; }
	uint8 microsecond() const { return _microsecond; }
	
	// Set functions
	void set_type_and_timezone(uint16 type_and_timezone) { _type_and_timezone = B_HOST_TO_LENDIAN_INT16(type_and_timezone); }
	void set_type(uint8 type) {
		type_and_timezone_accessor t;
		t.type_and_timezone = type_and_timezone();
		t.bits.type = type;
		set_type_and_timezone(t.type_and_timezone);
	}
	void set_timezone(uint8 timezone) {
		type_and_timezone_accessor t;
		t.type_and_timezone = type_and_timezone();
		t.bits.timezone = timezone;
		set_type_and_timezone(t.type_and_timezone);
	}
	void set_year(uint16 year) { _year = B_HOST_TO_LENDIAN_INT16(year); }
	void set_month(uint8 month) { _month = month; }
	void set_day(uint8 day) { _day = day; }
	void set_hour(uint8 hour) { _hour = hour; }
	void set_minute(uint8 minute) { _minute = minute; }
	void set_second(uint8 second) { _second = second; }
	void set_centisecond(uint8 centisecond) { _centisecond = centisecond; }
	void set_hundred_microsecond(uint8 hundred_microsecond) { _hundred_microsecond = hundred_microsecond; }
	void set_microsecond(uint8 microsecond) { _microsecond = microsecond; }
private:
	uint16 _type_and_timezone;
	uint16 _year;
	uint8 _month;
	uint8 _day;
	uint8 _hour;
	uint8 _minute;
	uint8 _second;
	uint8 _centisecond;
	uint8 _hundred_microsecond;
	uint8 _microsecond;

} __attribute__((packed));


/*! \brief Identifier used to designate the implementation responsible
	for writing associated data structures on the medium.
	
	See also: ECMA 167 1/7.4, UDF 2.01 2.1.5
*/
struct udf_entity_id {
public:
	void dump();

	// Get functions
	uint8 flags() const { return _flags; }
	const char* identifier() const { return _identifier; }
	char* identifier() { return _identifier; }
	const char* identifier_suffix() const { return _identifier_suffix; }
	char* identifier_suffix() { return _identifier_suffix; }

	// Set functions
	void set_flags(uint8 flags) { _flags = flags; }	
private:
	uint8 _flags;
	char _identifier[23];
	char _identifier_suffix[8];
} __attribute__((packed));


//----------------------------------------------------------------------
// ECMA-167 Part 2
//----------------------------------------------------------------------


/*! \brief Header for volume structure descriptors

	Each descriptor consumes an entire block. All unused trailing
	bytes in the descriptor should be set to 0.
	
	The following descriptors contain no more information than
	that contained in the header:
	
	- BEA01:
	  - type: 0
	  - id: "BEA01"
	  - version: 1

	- TEA01:
	  - type: 0
	  - id: "TEA01"
	  - version: 1
	
	- NSR03:
	  - type: 0
	  - id: "NSR03"
	  - version: 1

	See also: ECMA 167 2/9.1
*/	
struct udf_volume_structure_descriptor_header {
	uint8 type;
	char id[5];
	uint8 version;
	
	bool id_matches(const char *id);
} __attribute__((packed));


// Volume structure descriptor ids 
extern const char* kVSDID_BEA;
extern const char* kVSDID_TEA;
extern const char* kVSDID_BOOT;
extern const char* kVSDID_ISO;
extern const char* kVSDID_ECMA167_2;
extern const char* kVSDID_ECMA167_3;
extern const char* kVSDID_ECMA168;


//----------------------------------------------------------------------
// ECMA-167 Part 3
//----------------------------------------------------------------------


/*! \brief Location and length of a contiguous chunk of data

	See also: ECMA 167 3/7.1
*/
struct udf_extent_address {
public:
	void dump();

	uint32 length() { return B_LENDIAN_TO_HOST_INT32(_length); }	
	uint32 location() { return B_LENDIAN_TO_HOST_INT32(_location); }
	
	void set_length(int32 length) { _length = B_HOST_TO_LENDIAN_INT32(length); }
	void set_location(int32 location) { _location = B_HOST_TO_LENDIAN_INT32(location); }
private:
	uint32 _length;
	uint32 _location;
} __attribute__((packed));


/*! \brief Location of a logical block within a logical volume.

	See also: ECMA 167 4/7.1
*/
struct udf_logical_block_address {
public:
	void dump();

	uint32 block() const { return B_LENDIAN_TO_HOST_INT32(_block); }
	uint16 partition() const { return B_LENDIAN_TO_HOST_INT16(_partition); }
	
	void set_block(uint32 block) { _block = B_HOST_TO_LENDIAN_INT32(block); }
	void set_partition(uint16 partition) { _partition = B_HOST_TO_LENDIAN_INT16(partition); }
	
private:
	uint32 _block;	//!< Block location relative to start of corresponding partition
	uint16 _partition;	//!< Numeric partition id within logical volume
} __attribute__((packed));


/*! \brief Allocation descriptor.

	See also: ECMA 167 4/14.14.1
*/
struct udf_short_address {
public:
	void dump();

	uint32 length() const { return B_LENDIAN_TO_HOST_INT32(_length); }
	uint32 block() const { return _location.block(); }
	uint16 partition() const { return _location.partition(); }

	void set_length(uint32 length) { _length = B_HOST_TO_LENDIAN_INT32(length); }
	void set_block(uint32 block) { _location.set_block(block); }
	void set_partition(uint16 partition) { _location.set_partition(partition); }
private:
	uint32 _length;
	udf_logical_block_address _location;
} __attribute__((packed));


/*! \brief Allocation descriptor w/ 6 byte implementation use field.

	See also: ECMA 167 4/14.14.2
*/
struct udf_long_address {
public:
	void dump();

	uint32 length() const { return B_LENDIAN_TO_HOST_INT32(_length); }
	
	uint32 block() const { return _location.block(); }
	uint16 partition() const { return _location.partition(); }
	
	const array<uint8, 6>& implementation_use() const { return _implementation_use; }
	array<uint8, 6>& implementation_use() { return _implementation_use; }

	void set_length(uint32 length) { _length = B_HOST_TO_LENDIAN_INT32(length); }
	void set_block(uint32 block) { _location.set_block(block); }
	void set_partition(uint16 partition) { _location.set_partition(partition); }

private:
	uint32 _length;
	udf_logical_block_address _location;
	array<uint8, 6> _implementation_use;
} __attribute__((packed));

/*! \brief Common tag found at the beginning of most udf descriptor structures.

	For error checking, \c udf_tag structures have:
	- The disk location of the tag redundantly stored in the tag itself
	- A checksum value for the tag
	- A CRC value and length

	See also: ECMA 167 1/7.2, UDF 2.01 2.2.1, UDF 2.01 2.3.1
*/
struct udf_tag {
public:
	void dump();	

	status_t init_check(uint32 diskBlock);

	uint16 id() { return B_LENDIAN_TO_HOST_INT16(_id); }
	uint16 version() { return B_LENDIAN_TO_HOST_INT16(_version); }
	uint8 checksum() { return _checksum; }
	uint16 serial_number() { return B_LENDIAN_TO_HOST_INT16(_serial_number); }
	uint16 crc() { return B_LENDIAN_TO_HOST_INT16(_crc); }
	uint16 crc_length() { return B_LENDIAN_TO_HOST_INT16(_crc_length); }
	uint32 location() { return B_LENDIAN_TO_HOST_INT32(_location); }

	void set_id(uint16 id) { _id = B_HOST_TO_LENDIAN_INT16(id); }
	void set_version(uint16 version) { _version = B_HOST_TO_LENDIAN_INT16(version); }
	void set_checksum(uint8 checksum) { _checksum = checksum; }
	void set_serial_number(uint16 serial_number) { _serial_number = B_HOST_TO_LENDIAN_INT16(serial_number); }
	void set_crc(uint16 crc) { _crc = B_HOST_TO_LENDIAN_INT16(crc); }
	void set_crc_length(uint16 crc_length) { _crc_length = B_HOST_TO_LENDIAN_INT16(crc_length); }
	void set_location(uint32 location) { _location = B_HOST_TO_LENDIAN_INT32(location); }
	
private:
	uint16 _id;
	uint16 _version;
	uint8 _checksum;			//!< Sum modulo 256 of bytes 0-3 and 5-15 of this struct.
	uint8 _reserved;			//!< Set to #00.
	uint16 _serial_number;
	uint16 _crc;				//!< May be 0 if \c crc_length field is 0.
	/*! \brief Length of the data chunk used to calculate CRC.
	
		If 0, no CRC was calculated, and the \c crc field must be 0.
		
		According to UDF-2.01 2.3.1.2, the CRC shall be calculated for all descriptors
		unless otherwise noted, and this field shall be set to:
		
		<code>(descriptor length) - (descriptor tag length)</code>
	*/
	uint16 _crc_length;
	/*! \brief Address of this tag within its partition (for error checking).
	
		For virtually addressed structures (i.e. those accessed thru a VAT), this
		shall be the virtual address, not the physical or logical address.
	*/
	uint32 _location;		
	
} __attribute__((packed));


/*! \c udf_tag::id values
*/
enum udf_tag_id {
	TAGID_UNDEFINED	= 0,

	// ECMA 167, PART 3
	TAGID_PRIMARY_VOLUME_DESCRIPTOR,
	TAGID_ANCHOR_VOLUME_DESCRIPTOR_POINTER,
	TAGID_VOLUME_DESCRIPTOR_POINTER,
	TAGID_IMPLEMENTATION_USE_VOLUME_DESCRIPTOR,
	TAGID_PARTITION_DESCRIPTOR,
	TAGID_LOGICAL_VOLUME_DESCRIPTOR,
	TAGID_UNALLOCATED_SPACE_DESCRIPTOR,
	TAGID_TERMINATING_DESCRIPTOR,
	TAGID_LOGICAL_VOLUME_INTEGRITY_DESCRIPTOR,
	
	TAGID_CUSTOM_START = 65280,
	TAGID_CUSTOM_END = 65535,
	
	// ECMA 167, PART 4
	TAGID_FILE_SET_DESCRIPTOR = 256,
	TAGID_FILE_IDENTIFIER_DESCRIPTOR,
	TAGID_ALLOCATION_EXTENT_DESCRIPTOR,
	TAGID_INDIRECT_ENTRY,
	TAGID_TERMINAL_ENTRY,
	TAGID_FILE_ENTRY,
	TAGID_EXTENDED_ATTRIBUTE_HEADER_DESCRIPTOR,
	TAGID_UNALLOCATED_SPACE_ENTRY,
	TAGID_SPACE_BITMAP_DESCRIPTOR,
	TAGID_PARTITION_INTEGRITY_ENTRY,
	TAGID_EXTENDED_FILE_ENTRY,
};

/*! \brief Primary volume descriptor
*/
struct udf_primary_descriptor {
public:
	void dump();	

	// Get functions
	const udf_tag& tag() const { return _tag; }
	udf_tag& tag() { return _tag; }

	uint32 vds_number() const { return B_LENDIAN_TO_HOST_INT32(_vds_number); }
	uint32 primary_volume_descriptor_number() const { return B_LENDIAN_TO_HOST_INT32(_primary_volume_descriptor_number); }

	const array<char, 32>& volume_identifier() const { return _volume_identifier; }
	array<char, 32>& volume_identifier() { return _volume_identifier; }
	
	uint16 volume_sequence_number() const { return B_LENDIAN_TO_HOST_INT16(_volume_sequence_number); }
	uint16 max_volume_sequence_number() const { return B_LENDIAN_TO_HOST_INT16(_max_volume_sequence_number); }
	uint16 interchange_level() const { return B_LENDIAN_TO_HOST_INT16(_interchange_level); }
	uint16 max_interchange_level() const { return B_LENDIAN_TO_HOST_INT16(_max_interchange_level); }
	uint32 character_set_list() const { return B_LENDIAN_TO_HOST_INT32(_character_set_list); }
	uint32 max_character_set_list() const { return B_LENDIAN_TO_HOST_INT32(_max_character_set_list); }

	const array<char, 128>& volume_set_identifier() const { return _volume_set_identifier; }
	array<char, 128>& volume_set_identifier() { return _volume_set_identifier; }
	
	const udf_charspec& descriptor_character_set() const { return _descriptor_character_set; }
	udf_charspec& descriptor_character_set() { return _descriptor_character_set; }

	const udf_charspec& explanatory_character_set() const { return _explanatory_character_set; }
	udf_charspec& explanatory_character_set() { return _explanatory_character_set; }

	const udf_extent_address& volume_abstract() const { return _volume_abstract; }
	udf_extent_address& volume_abstract() { return _volume_abstract; }
	const udf_extent_address& volume_copyright_notice() const { return _volume_copyright_notice; }
	udf_extent_address& volume_copyright_notice() { return _volume_copyright_notice; }

	const udf_entity_id& application_id() const { return _application_id; }
	udf_entity_id& application_id() { return _application_id; }
	
	const udf_timestamp& recording_date_and_time() const { return _recording_date_and_time; }
	udf_timestamp& recording_date_and_time() { return _recording_date_and_time; }

	const udf_entity_id& implementation_id() const { return _implementation_id; }
	udf_entity_id& implementation_id() { return _implementation_id; }

	const array<uint8, 64>& implementation_use() const { return _implementation_use; }
	array<uint8, 64>& implementation_use() { return _implementation_use; }

	uint32 predecessor_volume_descriptor_sequence_location() const
	  { return B_LENDIAN_TO_HOST_INT32(_predecessor_volume_descriptor_sequence_location); }
	uint16 flags() const { return B_LENDIAN_TO_HOST_INT16(_flags); }

	// Set functions
	void set_vds_number(uint32 number)
	  { _vds_number = B_HOST_TO_LENDIAN_INT32(number); }
	void set_primary_volume_descriptor_number(uint32 number)
	  { _primary_volume_descriptor_number = B_HOST_TO_LENDIAN_INT32(number); }
	void set_volume_sequence_number(uint16 number)
	  { _volume_sequence_number = B_HOST_TO_LENDIAN_INT16(number); }
	void set_max_volume_sequence_number(uint16 number)
	  { _max_volume_sequence_number = B_HOST_TO_LENDIAN_INT16(number); }
	void set_interchange_level(uint16 level)
	  { _interchange_level = B_HOST_TO_LENDIAN_INT16(level); }
	void set_max_interchange_level(uint16 level)
	  { _max_interchange_level = B_HOST_TO_LENDIAN_INT16(level); }
	void set_character_set_list(uint32 list)
	  { _character_set_list = B_HOST_TO_LENDIAN_INT32(list); }
	void set_max_character_set_list(uint32 list)
	  { _max_character_set_list = B_HOST_TO_LENDIAN_INT32(list); }
	void set_predecessor_volume_descriptor_sequence_location(uint32 location)
	  { _predecessor_volume_descriptor_sequence_location = B_HOST_TO_LENDIAN_INT32(location); }
	void set_flags(uint16 flags)
	  { _flags = B_HOST_TO_LENDIAN_INT16(flags); }

private:
	udf_tag _tag;
	uint32 _vds_number;
	uint32 _primary_volume_descriptor_number;
	array<char, 32> _volume_identifier;
	uint16 _volume_sequence_number;
	uint16 _max_volume_sequence_number;
	uint16 _interchange_level; //!< to be set to 3 if part of multivolume set, 2 otherwise
	uint16 _max_interchange_level; //!< to be set to 3 unless otherwise directed by user
	uint32 _character_set_list;
	uint32 _max_character_set_list;
	array<char, 128> _volume_set_identifier;

	/*! \brief Identifies the character set for the \c volume_identifier
		and \c volume_set_identifier fields.
		
		To be set to CS0.
	*/
	udf_charspec _descriptor_character_set;	

	/*! \brief Identifies the character set used in the \c volume_abstract
		and \c volume_copyright_notice extents.
		
		To be set to CS0.
	*/
	udf_charspec _explanatory_character_set;
	
	udf_extent_address _volume_abstract;
	udf_extent_address _volume_copyright_notice;
	
	udf_entity_id _application_id;
	udf_timestamp _recording_date_and_time;
	udf_entity_id _implementation_id;
	array<uint8, 64> _implementation_use;
	uint32 _predecessor_volume_descriptor_sequence_location;
	uint16 _flags;
	char _reserved[22];

} __attribute__((packed));


/*! \brief Anchor Volume Descriptor Pointer

	vd recorded at preset locations in the partition, used as a reference
	point to the main vd sequences
	
	According to UDF 2.01, an avdp shall be recorded in at least 2 of
	the 3 following locations, where N is the last recordable sector
	of the partition:
	- 256
	- (N - 256)
	- N	
	
	See also: ECMA 167 3/10.2, UDF-2.01 2.2.3
*/
struct udf_anchor_descriptor {
public:
	void dump();
	
	udf_tag& tag() { return _tag; }
	udf_extent_address& main_vds() { return _main_vds; }
	udf_extent_address& reserve_vds() { return _reserve_vds; }
private:
	udf_tag _tag;
	udf_extent_address _main_vds;	//!< min length of 16 sectors
	udf_extent_address _reserve_vds;	//!< min length of 16 sectors
	char _reserved[480];	
} __attribute__((packed));


/*! \brief Volume Descriptor Pointer

	Used to chain extents of volume descriptor sequences together.
	
	See also: ECMA 167 3/10.3
*/
struct udf_descriptor_pointer {
	udf_tag tag;
	uint32 vds_number;
	udf_extent_address next;
} __attribute__((packed));


/*! \brief Implementation Use Volume Descriptor

	See also: ECMA 167 3/10.4
*/
struct udf_implementation_use_descriptor {
public:
	void dump();

	// Get functions
	const udf_tag& tag() const { return _tag; }
	udf_tag& tag() { return _tag; }
	
	uint32 vds_number() const { return B_LENDIAN_TO_HOST_INT32(_vds_number); }

	const udf_entity_id& implementation_id() const { return _implementation_id; }
	udf_entity_id& implementation_id() { return _implementation_id; }

	const array<uint8, 460>& implementation_use() const { return _implementation_use; }
	array<uint8, 460>& implementation_use() { return _implementation_use; }
	
	// Set functions
	void set_vds_number(uint32 number) { _vds_number = B_HOST_TO_LENDIAN_INT32(number); }
private:
	udf_tag _tag;
	uint32 _vds_number;
	udf_entity_id _implementation_id;
	array<uint8, 460> _implementation_use;
} __attribute__((packed));


/*! \brief Partition Descriptor

	See also: ECMA 167 3/10.5
*/
struct udf_partition_descriptor {
private:
	union partition_flags_accessor {
		uint16 partition_flags;
		struct {
			uint16 allocated:1,
			       reserved:15;
		} bits;
	};

public:
	void dump();
	
	// Get functions
	const udf_tag& tag() const { return _tag; }
	udf_tag& tag() { return _tag; }
	
	uint32 vds_number() const { return B_LENDIAN_TO_HOST_INT32(_vds_number); }
	uint16 partition_flags() const { return B_LENDIAN_TO_HOST_INT16(_partition_flags); }
	bool allocated() const {
		partition_flags_accessor f;
		f.partition_flags = partition_flags();
		return f.bits.allocated;
	}
	uint16 partition_number() const { return B_LENDIAN_TO_HOST_INT16(_partition_number); }
		
	const udf_entity_id& partition_contents() const { return _partition_contents; }
	udf_entity_id& partition_contents() { return _partition_contents; }
	
	const array<uint8, 128>& partition_contents_use() const { return _partition_contents_use; }
	array<uint8, 128>& partition_contents_use() { return _partition_contents_use; }
	
	uint32 access_type() const { return B_LENDIAN_TO_HOST_INT32(_access_type); }
	uint32 start() const { return B_LENDIAN_TO_HOST_INT32(_start); }
	uint32 length() const { return B_LENDIAN_TO_HOST_INT32(_length); }

	const udf_entity_id& implementation_id() const { return _implementation_id; }
	udf_entity_id& implementation_id() { return _implementation_id; }

	const array<uint8, 128>& implementation_use() const { return _implementation_use; }
	array<uint8, 128>& implementation_use() { return _implementation_use; }
	
	// Set functions
	void set_vds_number(uint32 number) { _vds_number = B_HOST_TO_LENDIAN_INT32(number); }
	void set_partition_flags(uint16 flags) { _partition_flags = B_HOST_TO_LENDIAN_INT16(flags); }
	void set_allocated(bool allocated) {
		partition_flags_accessor f;
		f.partition_flags = partition_flags();
		f.bits.allocated = allocated;
		set_partition_flags(f.partition_flags);
	}

	void set_access_type(uint32 type) { _access_type = B_HOST_TO_LENDIAN_INT32(type); }
	void set_start(uint32 start) { _start = B_HOST_TO_LENDIAN_INT32(start); }
	void set_length(uint32 length) { _length = B_HOST_TO_LENDIAN_INT32(length); }

private:
	udf_tag _tag;
	uint32 _vds_number;
	/*! Bit 0: If 0, shall mean volume space has not been allocated. If 1,
	    shall mean volume space has been allocated.
	*/
	uint16 _partition_flags;
	uint16 _partition_number;
	
	/*! - "+NSR03" Volume recorded according to ECMA-167, i.e. UDF
		- "+CD001" Volume recorded according to ECMA-119, i.e. iso9660
		- "+FDC01" Volume recorded according to ECMA-107
		- "+CDW02" Volume recorded according to ECMA-168
	*/		
	udf_entity_id _partition_contents;
	array<uint8, 128> _partition_contents_use;

	/*! See \c partition_access_type enum
	*/
	uint32 _access_type;
	uint32 _start;
	uint32 _length;
	udf_entity_id _implementation_id;
	array<uint8, 128> _implementation_use;
	uint8 _reserved[156];
} __attribute__((packed));


enum partition_access_type {
	PAT_UNSPECIFIED,
	PAT_READ_ONLY,
	PAT_WRITE_ONCE,
	PAT_REWRITABLE,
	PAT_OVERWRITABLE,
};


/*! \brief Logical volume descriptor

	See also: ECMA 167 3/10.6, UDF-2.01 2.2.4
*/
struct udf_logical_descriptor {
	void dump();
	
	// Get functions
	const udf_tag& tag() const { return _tag; }
	udf_tag& tag() { return _tag; }

	uint32 vds_number() const { return B_LENDIAN_TO_HOST_INT32(_vds_number); }

	const udf_charspec& character_set() const { return _character_set; }
	udf_charspec& character_set() { return _character_set; }
	
	const array<char, 128>& logical_volume_identifier() const { return _logical_volume_identifier; }
	array<char, 128>& logical_volume_identifier() { return _logical_volume_identifier; }
	
	uint32 logical_block_size() const { return B_LENDIAN_TO_HOST_INT32(_logical_block_size); }

	const udf_entity_id& domain_id() const { return _domain_id; }
	udf_entity_id& domain_id() { return _domain_id; }

	const array<uint8, 16>& logical_volume_contents_use() const { return _logical_volume_contents_use; }
	array<uint8, 16>& logical_volume_contents_use() { return _logical_volume_contents_use; }

	const udf_long_address& file_set_address() const { return _file_set_address; }
	udf_long_address& file_set_address() { return _file_set_address; }

	uint32 map_table_length() const { return B_LENDIAN_TO_HOST_INT32(_map_table_length); }
	uint32 partition_map_count() const { return B_LENDIAN_TO_HOST_INT32(_partition_map_count); }

	const udf_entity_id& implementation_id() const { return _implementation_id; }
	udf_entity_id& implementation_id() { return _implementation_id; }

	const array<uint8, 128>& implementation_use() const { return _implementation_use; }
	array<uint8, 128>& implementation_use() { return _implementation_use; }

	const udf_extent_address& integrity_sequence_extent() const { return _integrity_sequence_extent; }
	udf_extent_address& integrity_sequence_extent() { return _integrity_sequence_extent; }

	const uint8* partition_maps() const { return _partition_maps; }
	uint8* partition_maps() { return _partition_maps; }

	// Set functions
	void set_vds_number(uint32 number) { _vds_number = B_HOST_TO_LENDIAN_INT32(number); }
	void set_logical_block_size(uint32 size) { _logical_block_size = B_HOST_TO_LENDIAN_INT32(size); }
		
	void set_map_table_length(uint32 length) { _map_table_length = B_HOST_TO_LENDIAN_INT32(length); }
	void set_partition_map_count(uint32 count) { _partition_map_count = B_HOST_TO_LENDIAN_INT32(count); }

private:
	udf_tag _tag;
	uint32 _vds_number;
	
	/*! \brief Identifies the character set for the
		\c logical_volume_identifier field.
		
		To be set to CS0.
	*/
	udf_charspec _character_set;
	array<char, 128> _logical_volume_identifier;
	uint32 _logical_block_size;
	
	/*! \brief To be set to 0 or "*OSTA UDF Compliant". See UDF specs.
	*/
	udf_entity_id _domain_id;
	
	union {
		/*! \brief For UDF, shall contain a \c udf_long_address which identifies
			the location of the logical volume's first file set.
		*/
		array<uint8, 16> _logical_volume_contents_use;
		udf_long_address _file_set_address;
	};

	uint32 _map_table_length;
	uint32 _partition_map_count;
	udf_entity_id _implementation_id;
	array<uint8, 128> _implementation_use;
	
	/*! \brief Logical volume integrity sequence location.
	
		For re/overwritable media, shall be a min of 8KB in length.
		For WORM media, shall be quite frickin large, as a new volume
		must be added to the set if the extent fills up (since you
		can't chain lvis's I guess).
	*/
	udf_extent_address _integrity_sequence_extent;
	
	/*! \brief Restricted to maps of type 1 for normal maps and
		UDF type 2 for virtual maps or maps on systems not supporting
		defect management.
		
		See UDF-2.01 2.2.8, 2.2.9
	*/
	uint8 _partition_maps[0];
} __attribute__((packed));


/*! \brief Generic partition map

	See also: ECMA-167 3/10.7.1
*/
struct udf_generic_partition_map {
	uint8 type;
	uint8 length;
	uint8 map_data[0];
} __attribute__((packed));


/*! \brief Physical partition map (i.e. ECMA-167 Type 1 partition map)

	See also: ECMA-167 3/10.7.2
*/
struct udf_physical_partition_map {
	uint8 type;
	uint8 length;
	uint16 volume_sequence_number;
	uint16 partition_number;	
} __attribute__((packed));


/* ----UDF Specific---- */
/*! \brief Virtual partition map (i.e. UDF Type 2 partition map)

	Note that this differs from the ECMA-167 type 2 partition map,
	but is of identical size.
	
	See also: UDF-2.01 2.2.8
	
	\todo Handle UDF sparable partition maps as well (UDF-2.01 2.2.9)
*/
struct udf_virtual_partition_map {
	uint8 type;
	uint8 length;
	uint8 reserved1[2];
	
	/*! - flags: 0
	    - identifier: "*UDF Virtual Partition"
	    - identifier_suffix: per UDF-2.01 2.1.5.3
	*/
	udf_entity_id partition_type_id;
	uint16 volume_sequence_number;
	
	/*! corresponding type 1 partition map in same logical volume
	*/
	uint16 partition_number;	
	uint8 reserved2[24];
} __attribute__((packed));


/*! \brief Unallocated space descriptor

	See also: ECMA-167 3/10.8
*/
struct udf_unallocated_space_descriptor {
	void dump();

	// Get functions
	const udf_tag& tag() const { return _tag; }
	udf_tag& tag() { return _tag; }
	uint32 vds_number() const { return B_LENDIAN_TO_HOST_INT32(_vds_number); }
	uint32 allocation_descriptor_count() const { return B_LENDIAN_TO_HOST_INT32(_allocation_descriptor_count); }
	udf_extent_address* allocation_descriptors() { return _allocation_descriptors; }

	// Set functions
	void set_vds_number(uint32 number) { _vds_number = B_HOST_TO_LENDIAN_INT32(number); }
	void set_allocation_descriptor_count(uint32 count) { _allocation_descriptor_count = B_HOST_TO_LENDIAN_INT32(count); }
private:
	udf_tag _tag;
	uint32 _vds_number;
	uint32 _allocation_descriptor_count;
	udf_extent_address _allocation_descriptors[0];
} __attribute__((packed));


/*! \brief Terminating descriptor

	See also: ECMA-167 3/10.9
*/
struct udf_terminating_descriptor {
	void dump();

	// Get functions
	const udf_tag& tag() const { return _tag; }
	udf_tag& tag() { return _tag; }

private:
	udf_tag _tag;
	uint8 _reserved[496];
} __attribute__((packed));


/*! \brief Logical volume integrity descriptor

	See also: ECMA-167 3/10.10
*/
struct udf_logical_volume_integrity_descriptor {
	udf_tag tag;
	udf_timestamp recording_time;
	uint32 integrity_type;
	udf_extent_address next_integrity_extent;
	array<uint8, 32> logical_volume_contents_use;
	uint32 partition_count;
	uint32 implementation_use_length;

	/*! \todo double-check the pointer arithmetic here. */
	uint32* free_space_table() { return (uint32*)(this+80); }
	uint32* size_table() { return (uint32*)(free_space_table()+partition_count*sizeof(uint32)); }
	uint8* implementation_use() { return (uint8*)(size_table()+partition_count*sizeof(uint32)); }	
	
} __attribute__((packed));

/*! \brief Logical volume integrity types
*/
enum {
	LVIT_OPEN = 1,
	LVIT_CLOSED,
};

//----------------------------------------------------------------------
// ECMA-167 Part 4
//----------------------------------------------------------------------



/*! \brief File set descriptor

	Contains all the pertinent info about a file set (i.e. a hierarchy of files)
	
	According to UDF-2.01, only one file set descriptor shall be recorded,
	except on WORM media, where the following rules apply:
	- Multiple file sets are allowed only on WORM media
	- The default file set shall be the one with highest value \c file_set_number field.
	- Only the default file set may be flagged as writeable. All others shall be
	  flagged as "hard write protect".
	- No writeable file set may reference metadata structures which are referenced
	  (directly or indirectly) by any other file set. Writeable file sets may, however,
	  reference actual file data extents that are also referenced by other file sets.
*/
struct udf_file_set_descriptor {
	void dump();

	// Get functions
	const udf_tag& tag() const { return _tag; }
	udf_tag& tag() { return _tag; }

	const udf_timestamp& recording_date_and_time() const { return _recording_date_and_time; }
	udf_timestamp& recording_date_and_time() { return _recording_date_and_time; }

	uint16 interchange_level() const { return B_LENDIAN_TO_HOST_INT16(_interchange_level); }
	uint16 max_interchange_level() const { return B_LENDIAN_TO_HOST_INT16(_max_interchange_level); }
	uint32 character_set_list() const { return B_LENDIAN_TO_HOST_INT32(_character_set_list); }
	uint32 max_character_set_list() const { return B_LENDIAN_TO_HOST_INT32(_max_character_set_list); }
	uint32 file_set_number() const { return B_LENDIAN_TO_HOST_INT32(_file_set_number); }
	uint32 file_set_descriptor_number() const { return B_LENDIAN_TO_HOST_INT32(_file_set_descriptor_number); }

	const udf_charspec& logical_volume_id_character_set() const { return _logical_volume_id_character_set; }
	udf_charspec& logical_volume_id_character_set() { return _logical_volume_id_character_set; }

	const array<char, 128>& logical_volume_id() const { return _logical_volume_id; }
	array<char, 128>& logical_volume_id() { return _logical_volume_id; }

	const array<char, 32>& file_set_id() const { return _file_set_id; }
	array<char, 32>& file_set_id() { return _file_set_id; }

	const array<char, 32>& copyright_file_id() const { return _copyright_file_id; }
	array<char, 32>& copyright_file_id() { return _copyright_file_id; }

	const array<char, 32>& abstract_file_id() const { return _abstract_file_id; }
	array<char, 32>& abstract_file_id() { return _abstract_file_id; }

	const udf_charspec& file_set_charspec() const { return _file_set_charspec; }
	udf_charspec& file_set_charspec() { return _file_set_charspec; }

	const udf_long_address& root_directory_icb() const { return _root_directory_icb; }
	udf_long_address& root_directory_icb() { return _root_directory_icb; }

	const udf_entity_id& domain_id() const { return _domain_id; }
	udf_entity_id& domain_id() { return _domain_id; }

	const udf_long_address& next_extent() const { return _next_extent; }
	udf_long_address& next_extent() { return _next_extent; }

	const udf_long_address& system_stream_directory_icb() const { return _system_stream_directory_icb; }
	udf_long_address& system_stream_directory_icb() { return _system_stream_directory_icb; }

	// Set functions
	void set_interchange_level(uint16 level) { _interchange_level = B_HOST_TO_LENDIAN_INT16(level); }
	void set_max_interchange_level(uint16 level) { _max_interchange_level = B_HOST_TO_LENDIAN_INT16(level); }
	void set_character_set_list(uint32 list) { _character_set_list = B_HOST_TO_LENDIAN_INT32(list); }
	void set_max_character_set_list(uint32 list) { _max_character_set_list = B_HOST_TO_LENDIAN_INT32(list); }
	void set_file_set_number(uint32 number) { _file_set_number = B_HOST_TO_LENDIAN_INT32(number); }
	void set_file_set_descriptor_number(uint32 number) { _file_set_descriptor_number = B_HOST_TO_LENDIAN_INT32(number); }
private:
	udf_tag _tag;
	udf_timestamp _recording_date_and_time;
	uint16 _interchange_level;			//!< To be set to 3 (see UDF-2.01 2.3.2.1)
	uint16 _max_interchange_level;		//!< To be set to 3 (see UDF-2.01 2.3.2.2)
	uint32 _character_set_list;
	uint32 _max_character_set_list;
	uint32 _file_set_number;
	uint32 _file_set_descriptor_number;
	udf_charspec _logical_volume_id_character_set;	//!< To be set to kCSOCharspec
	array<char, 128> _logical_volume_id;
	udf_charspec _file_set_charspec;
	array<char, 32> _file_set_id;
	array<char, 32> _copyright_file_id;
	array<char, 32> _abstract_file_id;
	udf_long_address _root_directory_icb;
	udf_entity_id _domain_id;	
	udf_long_address _next_extent;
	udf_long_address _system_stream_directory_icb;
	uint8 _reserved[32];
} __attribute__((packed));


/*! \brief Partition header descriptor
	
	Contains references to unallocated and freed space data structures.
	
	Note that unallocated space is space ready to be written with no
	preprocessing. Freed space is space needing preprocessing (i.e.
	a special write pass) before use.
	
	Per UDF-2.01 2.3.3, the use of tables or bitmaps shall be consistent,
	i.e. only one type or the other shall be used, not both.
	
	To indicate disuse of a certain field, the fields of the allocation
	descriptor shall all be set to 0.
	
	See also: ECMA-167 4/14.3, UDF-2.01 2.2.3
*/
struct udf_partition_header_descriptor {
	udf_long_address unallocated_space_table;
	udf_long_address unallocated_space_bitmap;
	/*! Unused, per UDF-2.01 2.2.3 */
	udf_long_address partition_integrity_table;
	udf_long_address freed_space_table;
	udf_long_address freed_space_bitmap;
	uint8 reserved[88];
} __attribute__((packed));


/*! \brief File identifier descriptor

	Identifies the name of a file entry, and the location of its corresponding
	ICB.
	
	See also: ECMA-167 4/14.4, UDF-2.01 2.3.4
	
	\todo Check pointer arithmetic
*/
struct udf_file_id_descriptor {
	udf_tag tag;
	/*! According to ECMA-167: 1 <= valid version_number <= 32767, 32768 <= reserved <= 65535.
		
		However, according to UDF-2.01, there shall be exactly one version of
		a file, and it shall be 1.
	 */
	uint16 version_number;
	/*! \todo Check UDF-2.01 2.3.4.2 for some more restrictions. */
	union {
		uint8 all;
		struct { 
			uint8	may_be_hidden:1,
					is_directory:1,
					is_deleted:1,
					is_parent:1,
					is_metadata_stream:1,
					reserved_characteristics:3;
		} characteristics;
	};	
	uint8 id_length;
	udf_long_address icb;
	uint8 implementation_use_length;
	
	/*! If implementation_use_length is greater than 0, the first 32
		bytes of implementation_use() shall be an udf_entity_id identifying
		the implementation that generated the rest of the data in the
		implementation_use() field.
	*/
	uint8* implementation_use() { return (uint8*)(this+38); }
	char* id() { return (char*)(this+38+implementation_use_length); }	
	
} __attribute__((packed));


/*! \brief Allocation extent descriptor

	See also: ECMA-167 4/14.5
*/
struct udf_allocation_extent_descriptor {
	udf_tag tag;
	uint32 previous_allocation_extent_location;
	uint32 length_of_allocation_descriptors;
	
	/*! \todo Check that this is really how things work: */
	uint8* allocation_descriptors() { return (uint8*)(this+sizeof(udf_allocation_extent_descriptor)); }
} __attribute__((packed));


/*! \brief icb_tag::file_type values

	See also ECMA-167 4/14.6.6
*/
enum udf_icb_file_types {
	ICB_TYPE_UNSPECIFIED = 0,
	ICB_TYPE_UNALLOCATED_SPACE_ENTRY,
	ICB_TYPE_PARTITION_INTEGRITY_ENTRY,
	ICB_TYPE_INDIRECT_ENTRY,
	ICB_TYPE_DIRECTORY,
	ICB_TYPE_REGULAR_FILE,
	ICB_TYPE_BLOCK_SPECIAL_DEVICE,
	ICB_TYPE_CHARACTER_SPECIAL_DEVICE,
	ICB_TYPE_EXTENDED_ATTRIBUTES_FILE,
	ICB_TYPE_FIFO,
	ICB_TYPE_ISSOCK,
	ICB_TYPE_TERMINAL,
	ICB_TYPE_SYMLINK,
	ICB_TYPE_STREAM_DIRECTORY,

	ICB_TYPE_RESERVED_START = 14,
	ICB_TYPE_RESERVED_END = 247,
	
	ICB_TYPE_CUSTOM_START = 248,
	ICB_TYPE_CUSTOM_END = 255,
};


/*! \brief ICB entry tag

	Common tag found in all ICB entries (in addition to, and immediately following,
	the descriptor tag).

	See also: ECMA-167 4/14.6, UDF-2.01 2.3.5
*/
struct udf_icb_entry_tag {
	void dump();

	uint32 prior_recorded_number_of_direct_entries() { return B_LENDIAN_TO_HOST_INT32(_prior_recorded_number_of_direct_entries); }
	uint16 strategy_type() { return B_LENDIAN_TO_HOST_INT16(_strategy_type); }

	array<uint8, 2>& strategy_parameters() { return _strategy_parameters; }
	const array<uint8, 2>& strategy_parameters() const { return _strategy_parameters; }

	uint16 entry_count() { return B_LENDIAN_TO_HOST_INT16(_entry_count); }
	uint8 file_type() { return _file_type; }
	udf_logical_block_address& parent_icb_location() { return _parent_icb_location; }
	const udf_logical_block_address& parent_icb_location() const { return _parent_icb_location; }

	uint16 flags() { return B_LENDIAN_TO_HOST_INT16(_all_flags); }
	
	void set_prior_recorded_number_of_direct_entries(uint32 entries) { _prior_recorded_number_of_direct_entries = B_LENDIAN_TO_HOST_INT32(entries); }
	void set_strategy_type(uint16 type) { _strategy_type = B_HOST_TO_LENDIAN_INT16(type); }

	void set_entry_count(uint16 count) { _entry_count = B_LENDIAN_TO_HOST_INT16(count); }
	void set_file_type(uint8 type) { _file_type = type; }

	void flags(uint16 flags) { _all_flags = B_LENDIAN_TO_HOST_INT16(flags); }
	
private:
	uint32 _prior_recorded_number_of_direct_entries;
	/*! Per UDF-2.01 2.3.5.1, only strategy types 4 and 4096 shall be supported.
	
		\todo Describe strategy types here.
	*/
	uint16 _strategy_type;
	array<uint8, 2> _strategy_parameters;
	uint16 _entry_count;
	uint8 _reserved;
	/*! \brief icb_file_type value identifying the type of this icb entry */
	uint8 _file_type;
	udf_logical_block_address _parent_icb_location;
	union {
		uint16 _all_flags;
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
		} _flags;
	};
} __attribute__((packed));

/*! \brief Header portion of an ICB entry.
*/
struct udf_icb_header {
public:
	void dump();
	
	udf_tag &tag() { return _tag; }
	const udf_tag &tag() const { return _tag; }
	
	udf_icb_entry_tag &icb_tag() { return _icb_tag; }
	const udf_icb_entry_tag &icb_tag() const { return _icb_tag; }
private:
	udf_tag _tag;
	udf_icb_entry_tag _icb_tag;
};

/*! \brief Indirect ICB entry
*/
struct udf_indirect_icb_entry {
	udf_tag tag;
	udf_icb_entry_tag icb_tag;
	udf_long_address indirect_icb;
} __attribute__((packed));


/*! \brief Terminal ICB entry
*/
struct udf_terminal_icb_entry {
	udf_tag tag;
	udf_icb_entry_tag icb_tag;
} __attribute__((packed));


/*! \brief File ICB entry

	See also: ECMA-167 4/14.9

	\todo Check pointer math.
*/
struct udf_file_icb_entry {

	// get functions
	udf_tag& tag() { return _tag; }
	const udf_tag& tag() const { return _tag; }
	
	udf_icb_entry_tag& icb_tag() { return _icb_tag; }
	const udf_icb_entry_tag& icb_tag() const { return _icb_tag; }
	
	uint32 uid() { return B_LENDIAN_TO_HOST_INT32(_uid); }
	uint32 gid() { return B_LENDIAN_TO_HOST_INT32(_gid); }
	uint32 permissions() { return B_LENDIAN_TO_HOST_INT32(_permissions); }
	uint16 file_link_count() { return B_LENDIAN_TO_HOST_INT16(_file_link_count); }
	uint8 record_format() { return _record_format; }
	uint8 record_display_attributes() { return _record_display_attributes; }
	uint8 record_length() { return _record_length; }
	uint64 information_length() { return B_LENDIAN_TO_HOST_INT64(_information_length); }
	uint64 logical_blocks_recorded() { return B_LENDIAN_TO_HOST_INT64(_logical_blocks_recorded); }

	udf_timestamp& access_date_and_time() { return _access_date_and_time; }
	const udf_timestamp& access_date_and_time() const { return _access_date_and_time; }

	udf_timestamp& modification_date_and_time() { return _modification_date_and_time; }
	const udf_timestamp& modification_date_and_time() const { return _modification_date_and_time; }

	udf_timestamp& attribute_date_and_time() { return _attribute_date_and_time; }
	const udf_timestamp& attribute_date_and_time() const { return _attribute_date_and_time; }

	uint32 checkpoint() { return B_LENDIAN_TO_HOST_INT32(_checkpoint); }

	udf_long_address& extended_attribute_icb() { return _extended_attribute_icb; }
	const udf_long_address& extended_attribute_icb() const { return _extended_attribute_icb; }

	udf_entity_id& implementation_id() { return _implementation_id; }
	const udf_entity_id& implementation_id() const { return _implementation_id; }

	uint64 unique_id() { return B_LENDIAN_TO_HOST_INT64(_unique_id); }
	uint32 extended_attributes_length() { return B_LENDIAN_TO_HOST_INT32(_extended_attributes_length); }
	uint32 allocation_descriptors_length() { return B_LENDIAN_TO_HOST_INT32(_allocation_descriptors_length); }

	uint8* extended_attributes() { return (uint8*)(this+sizeof(udf_file_icb_entry)); }
	uint8* allocation_descriptors() { return (uint8*)(this+sizeof(udf_file_icb_entry)+extended_attributes_length()); }
	
	// set functions
	void set_uid(uint32 uid) { _uid = B_HOST_TO_LENDIAN_INT32(uid); }
	void set_gid(uint32 gid) { _gid = B_HOST_TO_LENDIAN_INT32(gid); }
	void set_permissions(uint32 permissions) { _permissions = B_HOST_TO_LENDIAN_INT32(permissions); }

	void set_file_link_count(uint16 count) { _file_link_count = B_HOST_TO_LENDIAN_INT16(count); }
	void set_record_format(uint8 format) { _record_format = format; }
	void set_record_display_attributes(uint8 attributes) { _record_display_attributes = attributes; }
	void set_record_length(uint8 length) { _record_length = length; }

	void set_information_length(uint64 length) { _information_length = B_HOST_TO_LENDIAN_INT64(length); }
	void set_logical_blocks_recorded(uint64 blocks) { _logical_blocks_recorded = B_HOST_TO_LENDIAN_INT64(blocks); }

	void set_checkpoint(uint32 checkpoint) { _checkpoint = B_HOST_TO_LENDIAN_INT32(checkpoint); }

	void set_unique_id(uint64 id) { _unique_id = B_HOST_TO_LENDIAN_INT64(id); }

	void set_extended_attributes_length(uint32 length) { _extended_attributes_length = B_HOST_TO_LENDIAN_INT32(length); }
	void set_allocation_descriptors_length(uint32 length) { _allocation_descriptors_length = B_HOST_TO_LENDIAN_INT32(length); }

private:
	udf_tag _tag;
	udf_icb_entry_tag _icb_tag;
	uint32 _uid;
	uint32 _gid;
	/*! \todo List perms in comment and add handy union thingy */
	uint32 _permissions;
	/*! Identifies the number of file identifier descriptors referencing
		this icb.
	*/
	uint16 _file_link_count;
	uint8 _record_format;				//!< To be set to 0 per UDF-2.01 2.3.6.1
	uint8 _record_display_attributes;	//!< To be set to 0 per UDF-2.01 2.3.6.2
	uint8 _record_length;				//!< To be set to 0 per UDF-2.01 2.3.6.3
	uint64 _information_length;
	uint64 _logical_blocks_recorded;		//!< To be 0 for files and dirs with embedded data
	udf_timestamp _access_date_and_time;
	udf_timestamp _modification_date_and_time;
	udf_timestamp _attribute_date_and_time;
	/*! \brief Initially 1, may be incremented upon user request. */
	uint32 _checkpoint;
	udf_long_address _extended_attribute_icb;
	udf_entity_id _implementation_id;
	/*! \brief The unique id identifying this file entry
	
		The id of the root directory of a file set shall be 0.
		
		\todo Detail the system specific requirements for unique ids from UDF-2.01
	*/
	uint64 _unique_id;
	uint32 _extended_attributes_length;
	uint32 _allocation_descriptors_length;
	
} __attribute__((packed));
		

/*! \brief Extended file ICB entry

	See also: ECMA-167 4/14.17
	
	\todo Check pointer math.
*/
struct udf_extended_file_icb_entry {
	udf_tag tag;
	udf_icb_entry_tag icb_tag;
	uint32 uid;
	uint32 gid;
	/*! \todo List perms in comment and add handy union thingy */
	uint32 permissions;
	/*! Identifies the number of file identifier descriptors referencing
		this icb.
	*/
	uint16 file_link_count;
	uint8 record_format;				//!< To be set to 0 per UDF-2.01 2.3.6.1
	uint8 record_display_attributes;	//!< To be set to 0 per UDF-2.01 2.3.6.2
	uint8 record_length;				//!< To be set to 0 per UDF-2.01 2.3.6.3
	uint64 information_length;
	uint64 logical_blocks_recorded;		//!< To be 0 for files and dirs with embedded data
	udf_timestamp access_date_and_time;
	udf_timestamp modification_date_and_time;
	udf_timestamp creation_date_and_time;
	udf_timestamp attribute_date_and_time;
	/*! \brief Initially 1, may be incremented upon user request. */
	uint32 checkpoint;
	uint32 reserved;
	udf_long_address extended_attribute_icb;
	udf_long_address stream_directory_icb;
	udf_entity_id implementation_id;
	/*! \brief The unique id identifying this file entry
	
		The id of the root directory of a file set shall be 0.
		
		\todo Detail the system specific requirements for unique ids from UDF-2.01 3.2.1.1
	*/
	uint64 unique_id;
	uint32 extended_attributes_length;
	uint32 allocation_descriptors_length;
	
	uint8* extended_attributes() { return (uint8*)(this+sizeof(udf_file_icb_entry)); }
	uint8* allocation_descriptors() { return (uint8*)(this+sizeof(udf_file_icb_entry)+extended_attributes_length); }

} __attribute__((packed));
		

};	// namespace Udf

#endif	// _UDF_DISK_STRUCTURES_H

