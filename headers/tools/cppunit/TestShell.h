#ifndef _beos_test_shell_h_
#define _beos_test_shell_h_

#include <LockerSyncObject.h>
#include <cppunit/Exception.h>
#include <cppunit/Test.h>
#include <cppunit/TestListener.h>
#include <cppunit/TestResult.h>
#include <cppunit/TestResultCollector.h>
#include <TestSuite.h>
#include <map>
#include <set>
#include <string>

class BDirectory;

// Defines SuiteFunction to be a pointer to a function that
// takes no arguments and returns a pointer to a CppUnit::Test
typedef CppUnit::Test* (*SuiteFunction)(void);

// This is just absurd to have to type...
typedef CppUnit::SynchronizedObject::SynchronizationObject SyncObject;

//! BeOS savvy command line interface for the CppUnit testing framework.
/*! This class provides a fully functional command-line testing interface
	built on top of the CppUnit testing library. You add named test suites
	via AddSuite(), and then call Run(), which does all the dirty work. The
	user can get a list of each test installed via AddSuite(), and optionally
	can opt to run only a specified set of them.
*/
class BTestShell {
public:
	BTestShell(const std::string &description = "", SyncObject *syncObject = 0);
	
	// This function is used to add the tests for a given kit (as contained
	// in a BTestSuite object) to the list of available tests. The shell assumes
	// ownership of the BTestSuite object. Each test in the kit is added to
	// the list of tests via a call to AddTest(std::string
	status_t AddSuite(BTestSuite *kit);

	// This function is used to add test suites to the list of available
	// tests. A SuiteFunction is just a function that takes no parameters
	// and returns a pointer to a CppUnit::Test object. Return NULL at
	// your own risk :-). The name given is the name that will be presented
	// when the program is run with "--list" as an argument. Usually the
	// given suite would be a test suite for an entire class, but that's
	// not a requirement.
	void AddTest(const std::string &name, CppUnit::Test* test);

	// This function loads all the test addons it finds in the given
	// directory, returning the number of tests actually loaded.
	int32 LoadSuitesFrom(BDirectory *libDir);

	// This is the function you call after you've added all your test
	// suites with calls to AddSuite(). It runs the test, or displays
	// help, or lists installed tests, or whatever, depending on the
	// command-line arguments passed in.
	int Run(int argc, char *argv[]);
	
	// Verbosity Level enumeration and accessor function
	enum VerbosityLevel { v0, v1, v2, v3 };
	VerbosityLevel Verbosity() const;

	// Returns true if verbosity is high enough that individual tests are
	// allowed to make noise.
	bool BeVerbose() const { return Verbosity() >= v2; };

	static bool GlobalBeVerbose() { return (fGlobalShell ? fGlobalShell->BeVerbose() : true); };

	// Returns a pointer to a global BTestShell object. This function is
	// something of a hack, used to give BTestCase and its subclasses
	// access to verbosity information. Don't rely on it if you don't
	// have to (and always make sure the pointer it returns isn't NULL
	// before you try to use it :-).
	static BTestShell* Shell() { return fGlobalShell; };
	
	// Sets the global BTestShell pointer. The BTestShell class does
	// not assume ownership of the object. 
	static void SetShell(BTestShell *shell) { fGlobalShell = shell; };

protected:	
	VerbosityLevel fVerbosityLevel;
	std::set<std::string> fTestsToRun;
	std::map<std::string, CppUnit::Test*> fTests;
	std::map<std::string, BTestSuite*> fSuites;
	std::set<std::string> fLibDirs;
	CppUnit::TestResult fTestResults;
	CppUnit::TestResultCollector fResultsCollector;
	std::string fDescription;
	static BTestShell* fGlobalShell;
	static const char indent[];
	bool fListTestsAndExit;

	//! Prints a brief description of the program.
	virtual void PrintDescription(int argc, char *argv[]);

	//! Prints out command line argument instructions
	void PrintHelp();
	
	/*! \brief Prints out the list of valid command line arguments.
		Called by PrintHelp().
	*/
	virtual void PrintValidArguments();

	//! Prints out a list of all the currently available tests
	void PrintInstalledTests();
	
	/*! \brief Handles command line arguments; returns true if everything goes
		okay, false if not (or if the program just needs to terminate without
		running any tests). Modifies settings in "settings" as necessary.
	*/
	bool ProcessArguments(int argc, char *argv[]);
	
	//! Processes a single argument, given by the \c arg parameter.
	virtual bool ProcessArgument(std::string arg, int argc, char *argv[]);

	//! Makes any necessary pre-test preparations
	void InitOutput();

	/*! \brief Prints out the test results in the proper format per
		the specified verbosity level.
	*/
	void PrintResults();
	
	/*! \brief Searches all the paths in \c fLibDirs, loading any dynamically
		loadable suites it finds.
	*/
	virtual void LoadDynamicSuites();
	
private:
  //! Prevents the use of the copy constructor.
  BTestShell( const BTestShell &copy );
  
  //! Prevents the use of the copy operator.
  void operator =( const BTestShell &copy );

};	// class BTestShell

#endif // _beos_test_shell_h_