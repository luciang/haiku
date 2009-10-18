/*
 * Copyright 2008-2009 Haiku Inc. All rights reserved.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Pieter Panman
 */
#ifndef DEVICE_H
#define DEVICE_H


#include <map>
#include <vector>

#include <String.h>
#include <StringItem.h>

extern "C" {
#include "dm_wrapper.h"
}


typedef enum {
	BUS_ISA = 1,
	BUS_PCI,
	BUS_SCSI,
	BUS_NONE
} BusType;


struct Attribute {
			Attribute(BString name, BString value)
				{ fName = name; fValue = value; }
	BString	fName;
	BString	fValue;
};


typedef std::map<BString, BString>::const_iterator AttributeMapIterator;
typedef std::map<BString, BString> AttributeMap;
typedef std::pair<BString, BString> AttributePair;
typedef std::vector<Attribute> Attributes;


typedef enum {
	CAT_NONE = 0,
	CAT_BUS = 6,
	CAT_COMPUTER = 0x12
} Category;


extern const char* kCategoryString[];


class Device : public BStringItem {
public:
							Device(Device* physicalParent,
								BusType busType=BUS_NONE, 
								Category category=CAT_NONE, 
								const BString& name = "unknown",
								const BString& manufacturer = "unknown",
								const BString& driverUsed = "unknown",
								const BString& devPathsPublished = "unknown");
	virtual					~Device();

	virtual BString			GetName()
								{ return fAttributeMap["Device name"]; }
	virtual BString			GetManufacturer()
								{ return fAttributeMap["Manufacturer"]; }
	virtual BString			GetDriverUsed()
								{ return fAttributeMap["Driver used"]; }
	virtual BString			GetDevPathsPublished()
								{ return fAttributeMap["Device paths"]; }
	virtual Category		GetCategory() const
								{ return fCategory; }
	virtual Device*			GetPhysicalParent() const
								{ return fPhysicalParent; }
	virtual BusType			GetBusType() const
								{ return fBusType; }

	virtual Attributes		GetBasicAttributes();
	virtual Attributes		GetBusAttributes();
	virtual Attributes		GetAllAttributes();

	virtual BString			GetBasicStrings();
	virtual BString			GetBusStrings();
	virtual BString			GetAllStrings();
	
	virtual BString			GetBusTabName()
								{ return "Bus Information"; }

	virtual Attribute		GetAttribute(const BString& name)
								{ return Attribute(name.String(),
									 fAttributeMap[name]); }

	virtual void 			SetAttribute(const BString& name,
								const BString& value);

	virtual void			InitFromAttributes() { return; }

protected:
			AttributeMap	fAttributeMap;
			BusType			fBusType;
			Category		fCategory;
			Device*			fPhysicalParent;
};

#endif /* DEVICE_H */

