#include "RegistrarThreadManagerTest.h"

#include <cppunit/Test.h>
#include <cppunit/TestCaller.h>
#include <cppunit/TestSuite.h>
#include <TestApp.h>
#include <TestUtils.h>

#if !TEST_R5
#include <RegistrarThread.h>
#include <RegistrarThreadManager.h>
#endif	// !TEST_R5

#include <stdio.h>

// Suite
CppUnit::Test*
RegistrarThreadManagerTest::Suite() {
	CppUnit::TestSuite *suite = new CppUnit::TestSuite();
	typedef CppUnit::TestCaller<RegistrarThreadManagerTest> TC;
		
	suite->addTest( new TC("RegistrarThreadManager::Shutdown test",
						   &RegistrarThreadManagerTest::ShutdownTest) );

					   
	return suite;
}

#if !TEST_R5
// Base test thread class
class TestThread : public RegistrarThread {
public:
	TestThread(const char *name, int32 priority, BMessenger managerMessenger)
		: RegistrarThread(name, priority, managerMessenger)
	{
	}
	
	void DoSomethingUseless() {
		fIntVal++;
		snooze(1000);
	}
	
private:
	int64 fIntVal;
};

// Test thread that terminates quickly
class TerminatingThread : public TestThread {
public:
	TerminatingThread(const char *name, int32 priority, BMessenger managerMessenger)
		: TestThread(name, priority, managerMessenger)
	{
	}

protected:
	virtual status_t ThreadFunction() {
		DoSomethingUseless();
		fIsFinished = true;
		return B_OK;
	}
};
	
// Test thread that never terminates, but pays attention
// to its fShouldExit member
class WellBehavedInfiniteThread : public TestThread {
public:
	WellBehavedInfiniteThread(const char *name, int32 priority, BMessenger managerMessenger)
		: TestThread(name, priority, managerMessenger)
	{
	}

protected:
	virtual status_t ThreadFunction() {
		while (true) {
			DoSomethingUseless();
			if (fShouldExit)
				break;
		}
		fIsFinished = true;
		return B_OK;
	}
};
	
// Test thread that never terminates and completely ignores
// its fShouldExit member
class NaughtyInfiniteThread : public TestThread {
public:
	NaughtyInfiniteThread(const char *name, int32 priority, BMessenger managerMessenger)
		: TestThread(name, priority, managerMessenger)
	{
	}

protected:
	virtual status_t ThreadFunction() {
		while (true) {
			DoSomethingUseless();
		}
		fIsFinished = true;
		return B_OK;
	}
};
#endif	// !TEST_R5


// setUp
void
RegistrarThreadManagerTest::setUp()
{
	BTestCase::setUp();
#if !TEST_R5
	// Setup our application
	fApplication = new BTestApp("application/x-vnd.obos.RegistrarThreadManagerTest");
	if (fApplication->Init() != B_OK) {
		fprintf(stderr, "Failed to initialize application (perhaps the obos registrar isn't running?).\n");
		delete fApplication;
		fApplication = NULL;
	}
#endif	// !TEST_R5
}
	
// tearDown
void
RegistrarThreadManagerTest::tearDown()
{
#if !TEST_R5
	// Terminate the Application
	if (fApplication) {
		fApplication->Terminate();
		delete fApplication;
		fApplication = NULL;
	}
#endif	// !TEST_R5
	BTestCase::tearDown();
}

void
RegistrarThreadManagerTest::ShutdownTest()
{
#if TEST_R5
	Outputf("(no tests performed for R5 version)\n");
#else
	NextSubTest();
	status_t err = B_OK;
	NextSubTest();
	RegistrarThreadManager manager;
	NextSubTest();
	CHK(fApplication && fApplication->InitCheck() == B_OK);
	NextSubTest();
//	fApplication->AddHandler(&manager);
	NextSubTest();
	BMessenger managerMessenger(NULL, fApplication, &err);
	NextSubTest();
	CHK(err == B_OK && managerMessenger.IsValid());
	NextSubTest();
	
	// Launch a bunch of threads
	const uint termThreads = 2;
	const uint niceThreads = 2;
	const uint evilThreads = 2;
	
	for (uint i = 0; i < termThreads; i++) {
		NextSubTest();
		char name[1024];
		sprintf(name, "terminating #%d", i);
		RegistrarThread *thread = new TerminatingThread(name, B_NORMAL_PRIORITY, managerMessenger);
		CHK(thread != NULL);
		CHK(thread->InitCheck() == B_OK);
		CHK(manager.LaunchThread(thread) == B_OK);
	}

	for (uint i = 0; i < niceThreads; i++) {
		NextSubTest();
		char name[1024];
		sprintf(name, "nice #%d", i);
		RegistrarThread *thread = new WellBehavedInfiniteThread(name, B_NORMAL_PRIORITY, managerMessenger);
		CHK(thread != NULL);
		CHK(thread->InitCheck() == B_OK);
		CHK(manager.LaunchThread(thread) == B_OK);
	}

	for (uint i = 0; i < evilThreads; i++) {
		NextSubTest();
		char name[1024];
		sprintf(name, "evil #%d", i);
		RegistrarThread *thread = new NaughtyInfiniteThread(name, B_NORMAL_PRIORITY, managerMessenger);
		CHK(thread != NULL);
		CHK(thread->InitCheck() == B_OK);
		CHK(manager.LaunchThread(thread) == B_OK);
	}
	
	// Check the number of threads before doing a cleanup
	NextSubTest();	// <= 13
	CHK(manager.ThreadCount() == (termThreads + niceThreads + evilThreads));
	
	// Do the cleanup and check again (the terminating threads
	// should be gone)
	NextSubTest();
	snooze(500000);		// give them time to terminate
	CHK(manager.CleanupThreads() == B_OK);
	CHK(manager.ThreadCount() == (niceThreads + evilThreads));
	
	// Now do a shutdown and check again (the nice infinite threads
	// should be gone)
	NextSubTest();
	CHK(manager.ShutdownThreads() == B_OK);
	snooze(1000000);	// give them time to quit nicely
	CHK(manager.CleanupThreads() == B_OK);
	CHK(manager.ThreadCount() == evilThreads);
	

	// Now finally kill any remaining threads (which should rid us of
	// the naughty infinite threads)
	NextSubTest();
	CHK(manager.KillThreads() == B_OK);
	CHK(manager.ThreadCount() == 0);

#endif	// !TEST_R5
}

