// TIFFTranslatorTest.h

#ifndef TIFF_TRANSLATOR_TEST_H
#define TIFF_TRANSLATOR_TEST_H

#include <TestCase.h>
#include <TestShell.h>

#define BBT_MIME_STRING  "image/x-be-bitmap"
#define TIFF_MIME_STRING "image/tiff"

namespace CppUnit {
class Test;
}

class TIFFTranslatorTest : public BTestCase {
public:
	static CppUnit::Test* Suite();

	// This function called before *each* test added in Suite()
	void setUp();

	// This function called after *each* test added in Suite()
	void tearDown();

	//------------------------------------------------------------
	// Test functions
	//------------------------------------------------------------
#if !TEST_R5
	void LoadAddOnTest();
#endif
	void IdentifyTest();
	void TranslateTest();
};

#endif	// TIFF_TRANSLATOR_TEST_H
