/*****************************************************************************/
// OpenBeOS Translation Kit Test
//
// Version: 
// Author: Michael Wilber
// This is the Test application for BTranslator
//
//
// This application and all source files used in its construction, except 
// where noted, are licensed under the MIT License, and have been written 
// and are:
//
// Copyright (c) 2002 OpenBeOS Project
//
// Permission is hereby granted, free of charge, to any person obtaining a
// copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense, 
// and/or sell copies of the Software, and to permit persons to whom the 
// Software is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included 
// in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS
// OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL 
// THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING 
// FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
// DEALINGS IN THE SOFTWARE.
/*****************************************************************************/
#ifndef TRANSLATOR_TEST_H
#define TRANSLATOR_TEST_H

#include <Translator.h>

/** CppUnit support */
#include <TestCase.h>

class TranslatorTest : public BTestCase {
public:
    TranslatorTest(std::string name = "");
    ~TranslatorTest();
    
	/* cppunit suite function prototype */    
    static CppUnit::Test *Suite();    
    
    //actual tests
	void AcquireReleaseTest();
	void MakeConfigurationViewTest();
	void GetConfigurationMessageTest();	
	
private:
};

#endif // #ifndef TRANSLATOR_TEST_H
