// gensyscalls.cpp

// Don't include arch_gensyscalls.h. It's only needed when creating the
// syscall vector.
#define DONT_INCLUDE_ARCH_GENSYSCALLS_H 1

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <vector>

#include "gensyscalls.h"
#include "gensyscalls_common.h"

using std::vector;

// usage
const char *kUsage =
"Usage: gensyscalls [ -c <calls> ] [ -d <dispatcher> ] [ -n <numbers> ]\n"
"                   [ -t <table> ] [ -s <strace> ]\n"
"\n"
"The command is able to generate several syscalls related source files.\n"
"\n"
"  <calls>                - Output: The assembly source file implementing the\n"
"                           actual syscalls.\n"
"  <dispatcher>           - Output: The C source file to be included by the\n"
"                           syscall dispatcher source file.\n"
"  <numbers>              - Output: The C/assembly include files defining the\n"
"                           syscall numbers.\n"
"  <table>                - Output: A C source file containing an array with\n"
"                           infos about the syscalls\n"
"  <strace>               - Output: A C source file for strace support.\n"
;

// print_usage
static
void
print_usage(bool error)
{
	fprintf((error ? stderr : stdout), kUsage);
}


// Type

// constructor
Type::Type(const char* name, int size, int usedSize,
	const char* alignmentTypeName)
	: fName(name),
	  fSize(size),
	  fUsedSize(usedSize),
	  fAlignmentType(alignmentTypeName)
{
}


// Parameter

// constructor
Parameter::Parameter(const char* typeName, const char* parameterName,
	int size, int usedSize, int offset, const char* alignmentTypeName)
	: Type(typeName, size, usedSize, alignmentTypeName),
	  fParameterName(parameterName),
	  fOffset(offset)
{
}


// Syscall

// ParameterVector
struct Syscall::ParameterVector : public vector<Parameter*> {
};

// constructor
Syscall::Syscall(const char* name, const char* kernelName)
	: fName(name),
	  fKernelName(kernelName),
	  fReturnType(NULL),
	  fParameters(new ParameterVector)
{
}

// destructor
Syscall::~Syscall()
{
	delete fReturnType;
	
	int count = CountParameters();
	for (int i = 0; i < count; i++)
		delete ParameterAt(i);
	delete fParameters;
}

// ParameterAt
int
Syscall::CountParameters() const
{
	return fParameters->size();
}

// ParameterAt
Parameter*
Syscall::ParameterAt(int index) const
{
	if (index < 0 || index >= CountParameters())
		return NULL;

	return (*fParameters)[index];
}

// LastParameter
Parameter*
Syscall::LastParameter() const
{
	return ParameterAt(CountParameters() - 1);
}

// SetReturnType
Type*
Syscall::SetReturnType(const char* name, int size, int usedSize,
	const char* alignmentTypeName)
{
	delete fReturnType;
	return fReturnType = new Type(name, size, usedSize, alignmentTypeName);
}

// AddParameter
Parameter*
Syscall::AddParameter(const char* typeName, const char* parameterName,
	int size, int usedSize, int offset, const char* alignmentTypeName)
{
	Parameter* parameter = new Parameter(typeName, parameterName, size,
		usedSize, offset, alignmentTypeName);
	fParameters->push_back(parameter);
	return parameter;
}


// SyscallVector

struct SyscallVector::_SyscallVector : public vector<Syscall*> {
};


// constructor
SyscallVector::SyscallVector()
	: fSyscalls(new _SyscallVector)
{
}

// destructor
SyscallVector::~SyscallVector()
{
	int count = CountSyscalls();
	for (int i = 0; i < count; i++)
		delete SyscallAt(i);
	delete fSyscalls;
}

// Create
SyscallVector*
SyscallVector::Create()
{
	return new SyscallVector;
}

// CountSyscalls
int
SyscallVector::CountSyscalls() const
{
	return fSyscalls->size();
}

// SyscallAt
Syscall*
SyscallVector::SyscallAt(int index) const
{
	if (index < 0 || index >= CountSyscalls())
		return NULL;

	return (*fSyscalls)[index];
}

// CreateSyscall
Syscall*
SyscallVector::CreateSyscall(const char* name, const char* kernelName)
{
	Syscall* syscall = new Syscall(name, kernelName);
	fSyscalls->push_back(syscall);
	return syscall;
}


// #pragma mark -

// Main
class Main {
public:

	int Run(int argc, char **argv)
	{
		// parse arguments
		const char *syscallsFile = NULL;
		const char *dispatcherFile = NULL;
		const char *numbersFile = NULL;
		const char *tableFile = NULL;
		const char *straceFile = NULL;
		for (int argi = 1; argi < argc; argi++) {
			string arg(argv[argi]);
			if (arg == "-h" || arg == "--help") {
				print_usage(false);
				return 0;
			} else if (arg == "-c") {
				if (argi + 1 >= argc) {
					print_usage(true);
					return 1;
				}
				syscallsFile = argv[++argi];
			} else if (arg == "-d") {
				if (argi + 1 >= argc) {
					print_usage(true);
					return 1;
				}
				dispatcherFile = argv[++argi];
			} else if (arg == "-n") {
				if (argi + 1 >= argc) {
					print_usage(true);
					return 1;
				}
				numbersFile = argv[++argi];
			} else if (arg == "-t") {
				if (argi + 1 >= argc) {
					print_usage(true);
					return 1;
				}
				tableFile = argv[++argi];
			} else if (arg == "-s") {
				if (argi + 1 >= argc) {
					print_usage(true);
					return 1;
				}
				straceFile = argv[++argi];
			} else {
				print_usage(true);
				return 1;
			}
		}
		fSyscallVector = create_syscall_vector();
		fSyscallCount = fSyscallVector->CountSyscalls();
		if (!syscallsFile && !dispatcherFile && !numbersFile && !tableFile
			&& !straceFile) {
			printf("Found %d syscalls.\n", fSyscallCount);
			return 0;
		}
		// generate the output
		if (syscallsFile)
			_WriteSyscallsFile(syscallsFile);
		if (dispatcherFile)
			_WriteDispatcherFile(dispatcherFile);
		if (numbersFile)
			_WriteNumbersFile(numbersFile);
		if (tableFile)
			_WriteTableFile(tableFile);
		if (straceFile)
			_WriteSTraceFile(straceFile);
		return 0;
	}

	void _WriteSyscallsFile(const char *filename)
	{
		// open the syscalls output file
		ofstream file(filename, ofstream::out | ofstream::trunc);
		if (!file.is_open())
			throw IOException(string("Failed to open `") + filename + "'.");
		
		// output the syscalls definitions
		for (int i = 0; i < fSyscallCount; i++) {
			const Syscall* syscall = fSyscallVector->SyscallAt(i);
			int paramCount = syscall->CountParameters();
			int paramSize = 0;
			// XXX: Currently the SYSCALL macros support 4 byte aligned
			// parameters only. This has to change, of course.
			for (int k = 0; k < paramCount; k++) {
				const Parameter* parameter = syscall->ParameterAt(k);
				int size = parameter->UsedSize();
				paramSize += (size + 3) / 4 * 4;
			}
			file << "SYSCALL" << (paramSize / 4) << "("
				<< syscall->Name() << ", " << i << ")" << endl;
		}
	}

	void _WriteDispatcherFile(const char *filename)
	{
		// open the dispatcher output file
		ofstream file(filename, ofstream::out | ofstream::trunc);
		if (!file.is_open())
			throw IOException(string("Failed to open `") + filename + "'.");

		// output the case statements
		for (int i = 0; i < fSyscallCount; i++) {
			const Syscall* syscall = fSyscallVector->SyscallAt(i);
			file << "case " << i << ":" << endl;
			file << "\t";
			if (string(syscall->ReturnType()->TypeName()) != "void")
				file << "*call_ret = ";
			file << syscall->KernelName() << "(";
			int paramCount = syscall->CountParameters();
			if (paramCount > 0) {
				Parameter* parameter = syscall->ParameterAt(0);
				if (parameter->AlignmentTypeName()) {
					file << "(" << parameter->TypeName() << ")*("
						 << parameter->AlignmentTypeName()
						 << "*)args";
				} else {
					file << "*(" << _GetPointerType(parameter->TypeName())
						<< ")args";
				}
				for (int k = 1; k < paramCount; k++) {
					parameter = syscall->ParameterAt(k);
					if (parameter->AlignmentTypeName()) {
						file << ", (" << parameter->TypeName() << ")*("
							<< parameter->AlignmentTypeName()
							<< "*)((char*)args + " << parameter->Offset()
							<< ")";
					} else {
						file << ", *(" << _GetPointerType(parameter->TypeName())
							<< ")((char*)args + " << parameter->Offset()
							<< ")";
					}
				}
			}
			file << ");" << endl;
			file << "\tbreak;" << endl;
		}
	}

	void _WriteNumbersFile(const char *filename)
	{
		// open the syscall numbers output file
		ofstream file(filename, ofstream::out | ofstream::trunc);
		if (!file.is_open())
			throw IOException(string("Failed to open `") + filename + "'.");

		// output the defines
		const char *prefix = "_user_";
		size_t prefixLen = strlen(prefix);
		for (int i = 0; i < fSyscallCount; i++) {
			const Syscall* syscall = fSyscallVector->SyscallAt(i);
			string name(syscall->KernelName());

			// drop the leading "_user_" prefix
			if (name.find(prefix) != 0)
				throw Exception(string("Bad kernel name: `") + name + "'.");
			name = string(name, prefixLen);

			// convert to upper case (is there no function for that?)
			string defineName;
			for (int k = 0; k < (int)name.length(); k++)
				defineName += toupper(name[k]);
			file << "#define SYSCALL_" << defineName << " " << i << endl;
		}
	}

	void _WriteTableFile(const char *filename)
	{
		// open the syscall table output file
		ofstream file(filename, ofstream::out | ofstream::trunc);
		if (!file.is_open())
			throw IOException(string("Failed to open `") + filename + "'.");

		// output syscall count macro
		file << "#define SYSCALL_COUNT " << fSyscallCount << endl;
		file << endl;

		// assembler guard
		file << "#ifndef _ASSEMBLER" << endl;
		file << endl;

		// output syscall count
		file << "const int kSyscallCount = SYSCALL_COUNT;" << endl;
		file << endl;

		// syscall infos array preamble
		file << "const syscall_info kSyscallInfos[] = {" << endl;

		// the syscall infos
		for (int i = 0; i < fSyscallCount; i++) {
			const Syscall* syscall = fSyscallVector->SyscallAt(i);

			// get the parameter size
			int paramSize = 0;
			if (Parameter* parameter = syscall->LastParameter())
				paramSize = parameter->Offset() + parameter->UsedSize();

			// output the info for the syscall
			file << "\t{ (void *)" << syscall->KernelName() << ", "
				<< paramSize << " }," << endl;
		}

		// syscall infos array end
		file << "};" << endl;

		// assembler guard end
		file << "#endif	// _ASSEMBLER" << endl;
	}

	void _WriteSTraceFile(const char *filename)
	{
		// open the syscall table output file
		ofstream file(filename, ofstream::out | ofstream::trunc);
		if (!file.is_open())
			throw IOException(string("Failed to open `") + filename + "'.");

		// the file defines a single function get_syscalls
		file << "void" << endl
			<< "GET_SYSCALLS(vector<Syscall*> &syscalls)" << endl
			<< "{" << endl
			<< "\tSyscall *syscall;" << endl
			<< "\tTypeHandler *handler;" << endl
			<< "(void)syscall;" << endl
			<< "(void)handler;" << endl;

		int chunkSize = (fSyscallCount + 19) / 20;
		
		// iterate through the syscalls
		for (int i = 0; i < fSyscallCount; i++) {
			const Syscall* syscall = fSyscallVector->SyscallAt(i);

			if (i % chunkSize == 0) {
				// chunk end
				file << endl;
				if (i > 0)
					file << "#endif" << endl;
				// chunk begin
				file << "#ifdef SYSCALLS_CHUNK_" << (i / chunkSize) << endl;
			}

			// spacing, comment
			file << endl;
			file << "\t// " << syscall->Name() << endl;

			// create the return type handler
			const char* returnType = syscall->ReturnType()->TypeName();
			file << "\thandler = TypeHandlerFactory<"
				<< returnType
				<< ">::Create();" << endl;

			// create the syscall
			file << "\tsyscall = new Syscall(\"" << syscall->Name() << "\", "
				<< "\"" << returnType << "\", "<< "handler);" << endl;
			file << "\tsyscalls.push_back(syscall);" << endl;

			// add the parameters
			int paramCount = syscall->CountParameters();
			for (int k = 0; k < paramCount; k++) {
				const Parameter* parameter = syscall->ParameterAt(k);

				// create the parameter type handler
				file << "\thandler = TypeHandlerFactory<"
					<< parameter->TypeName() << ">::Create();" << endl;

				// add the parameter
				file << "\tsyscall->AddParameter(\""
					<< parameter->ParameterName() << "\", "
					<< parameter->Offset() << ", \"" << parameter->TypeName()
					<< "\", handler);" << endl;
			}
		}

		// last syscall chunk end
		file << "#endif" << endl;

		// function end
		file << "}" << endl;
	}

	static string _GetPointerType(const char *type)
	{
		char *parenthesis = strchr(type, ')');
		if (!parenthesis)
			return string(type) + "*";
		// function pointer type
		return string(type, parenthesis - type) + "*" + parenthesis;
	}

private:
	SyscallVector*	fSyscallVector;
	int				fSyscallCount;
};

// main
int
main(int argc, char **argv)
{
	try {
		return Main().Run(argc, argv);
	} catch (Exception &exception) {
		fprintf(stderr, "%s\n", exception.what());
		return 1;
	}
}
