#include "StringSearchTest.h"
#include "cppunit/TestCaller.h"
#include <String.h>
#include <stdio.h>

StringSearchTest::StringSearchTest(std::string name) :
		BTestCase(name)
{
}

 

StringSearchTest::~StringSearchTest()
{
}


void 
StringSearchTest::PerformTest(void)
{
	BString *string1, *string2;
	int32 i;

	//FindFirst(BString&)
	NextSubTest();
	string1 = new BString("last but not least");
	string2 = new BString("st");
	i = string1->FindFirst(*string2);
	CPPUNIT_ASSERT(i == 2);
	delete string1;
	delete string2;

	NextSubTest();
	string1 = new BString;
	string2 = new BString("some text");
	i = string1->FindFirst(*string2);
	CPPUNIT_ASSERT(i == B_ERROR);
	delete string1;
	delete string2;

	//FindFirst(char*)
	NextSubTest();
	string1 = new BString("last but not least");
	i = string1->FindFirst("st");
	CPPUNIT_ASSERT(i == 2);
	delete string1;
	
	NextSubTesT();
	string1 = new BString;
	i = string1->FindFirst("some text");
	CPPUNIT_ASSERT(i == B_ERROR);
	delete string1;

	NextSubTest();
	string1 = new BString("string");
	i = string1->FindFirst((char*)NULL);
	CPPUNIT_ASSERT(i == 0);
	delete string1;

	//FindFirst(BString&, int32)
	NextSubTest();
	string1 = new BString("abc abc abc");
	string2 = new BString("abc");
	i = string1->FindFirst(*string2, 5);
	CPPUNIT_ASSERT(i == 8);
	delete string1;
	delete string2;

	NextSubTest();
	string1 = new BString("abc abc abc");
	string2 = new BString("abc");
	i = string1->FindFirst(*string2, 200);
	CPPUNIT_ASSERT(i == B_ERROR);
	delete string1;
	delete string2;

	NextSubTest();
	string1 = new BString("abc abc abc");
	string2 = new BString("abc");
	i = string1->FindFirst(*string2, -10);
	CPPUNIT_ASSERT(i == B_ERROR);
	delete string1;
	delete string2;

	//FindFirst(const char*, int32)
	NextSubTest();
	string1 = new BString("abc abc abc");
	i = string1->FindFirst("abc", 2);
	CPPUNIT_ASSERT(i == 4);
	delete string1;

	NextSubTest();
	string1 = new BString("abc abc abc");
	i = string1->FindFirst("abc", 200);
	CPPUNIT_ASSERT(i == B_ERROR);
	delete string1;

	NextSubTest();
	string1 = new BString("abc abc abc");
	i = string1->FindFirst("abc", -10);
	CPPUNIT_ASSERT(i == B_ERROR);
	delete string1;

	NextSubTest();
	string1 = new BString("abc abc abc");
	i = string1->FindFirst((char*)NULL, 3);
	CPPUNIT_ASSERT(i == 0);
	delete string1;

	//FindFirst(char)
	NextSubTest();
	string1 = new BString("abcd abcd");
	i = string1->FindFirst('c');
	CPPUNIT_ASSERT(i == 2);
	delete string1;

	NextSubTest();
	string1 = new BString("abcd abcd");
	i = string1->FindFirst('e');
	CPPUNIT_ASSERT(i == B_ERROR);
	delete string1;

	//FindFirst(char, int32)
	NextSubTest();
	string1 = new BString("abc abc abc");
	i = string1->FindFirst("b", 3);
	CPPUNIT_ASSERT(i == 5);
	delete string1;

	NextSubTest();
	string1 = new BString("abcd abcd");
	i = string1->FindFirst('e', 3);
	CPPUNIT_ASSERT(i == B_ERROR);
	delete string1;

	NextSubTest();
	string1 = new BString("abc abc abc");
	i = string1->FindFirst("a", 9);
	CPPUNIT_ASSERT(i == B_ERROR);
	delete string1;
}


CppUnit::Test *StringSearchTest::suite(void)
{	
	typedef CppUnit::TestCaller<StringSearchTest>
		StringSearchTestCaller;
		
	return(new StringSearchTestCaller("BString::Search Test", &StringSearchTest::PerformTest));
}
