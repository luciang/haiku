/*
	$Id: RegionConstruction.h,v 1.1 2003/08/06 06:46:06 jackburton Exp $
	
	This file defines a class for performing one test of BRegion
	functionality.
	
	*/


#ifndef RegionConstruction_H
#define RegionConstruction_H


#include "RegionTestcase.h"

	
class RegionConstruction :
	public RegionTestcase {
	
private:


protected:
	virtual void testOneRegion(BRegion *);
	virtual void testTwoRegions(BRegion *, BRegion *);

public:
	static Test *suite(void);
	RegionConstruction(std::string name = "");
	virtual ~RegionConstruction();
	};
	
#endif
