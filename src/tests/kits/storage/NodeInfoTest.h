// NodeInfoTest.h

#ifndef NODE_INFO_TEST_H
#define NODE_INFO_TEST_H

#include <StorageDefs.h>
#include <SupportDefs.h>
#include <BasicTest.h>

class BApplication;
class BBitmap;
class CppUnit::Test;

class NodeInfoTest : public BasicTest
{
public:
	static CppUnit::Test* Suite();
	
	// This function called before *each* test added in Suite()
	void setUp();
	
	// This function called after *each* test added in Suite()
	void tearDown();

	//------------------------------------------------------------
	// Test functions
	//------------------------------------------------------------
	void InitTest1();
	void InitTest2();
	void TypeTest();
	void IconTest();
	void PreferredAppTest();
	void AppHintTest();
	void TrackerIconTest();

private:
	BApplication	*fApplication;
	BBitmap			*fIconM1;
	BBitmap			*fIconM2;
	BBitmap			*fIconL1;
	BBitmap			*fIconL2;
};

#endif	// NODE_INFO_TEST_H
