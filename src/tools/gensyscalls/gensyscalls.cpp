// gensyscalls.cpp

#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>

#include "arch_config.h"

#include "gensyscalls.h"
#include "gensyscalls_common.h"

extern "C" gensyscall_syscall_info *gensyscall_get_infos(int *count);

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

enum {
	PARAMETER_ALIGNMENT	= sizeof(FUNCTION_CALL_PARAMETER_ALIGNMENT_TYPE)
};

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
		fSyscallInfos = gensyscall_get_infos(&fSyscallCount);
		_UpdateSyscallInfos();
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
			const gensyscall_syscall_info &syscall = fSyscallInfos[i];
			int paramCount = syscall.parameter_count;
			int paramSize = 0;
			gensyscall_parameter_info* parameters = syscall.parameters;
			// XXX: Currently the SYSCALL macros support 4 byte aligned
			// parameters only. This has to change, of course.
			for (int k = 0; k < paramCount; k++) {
				int size = parameters[k].actual_size;
				paramSize += (size + 3) / 4 * 4;
			}
			file << "SYSCALL" << (paramSize / 4) << "("
				<< syscall.name << ", " << i << ")" << endl;
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
			const gensyscall_syscall_info &syscall = fSyscallInfos[i];
			file << "case " << i << ":" << endl;
			file << "\t";
			if (string(syscall.return_type) != "void")
				file << "*call_ret = ";
			file << syscall.kernel_name << "(";
			int paramCount = syscall.parameter_count;
			if (paramCount > 0) {
				gensyscall_parameter_info* parameters = syscall.parameters;
				if (parameters[0].size < PARAMETER_ALIGNMENT) {
					file << "(" << parameters[0].type << ")*("
						 << "FUNCTION_CALL_PARAMETER_ALIGNMENT_TYPE"
						 << "*)args";
				} else {
					file << "*(" << _GetPointerType(parameters[0].type)
						<< ")args";
				}
				for (int k = 1; k < paramCount; k++) {
					if (parameters[k].size < PARAMETER_ALIGNMENT) {
						file << ", (" << parameters[k].type << ")*("
							<< "FUNCTION_CALL_PARAMETER_ALIGNMENT_TYPE"
							<< "*)((char*)args + " << parameters[k].offset
							<< ")";
					} else {
						file << ", *(" << _GetPointerType(parameters[k].type)
							<< ")((char*)args + " << parameters[k].offset
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
			const gensyscall_syscall_info &syscall = fSyscallInfos[i];
			string name(syscall.kernel_name);
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

		// output syscall count
		file << "const int kSyscallCount = " << fSyscallCount << ";" << endl;
		file << endl;

		// syscall infos array preamble
		file << "const syscall_info kSyscallInfos[] = {" << endl;

		// the syscall infos
		for (int i = 0; i < fSyscallCount; i++) {
			const gensyscall_syscall_info &syscall = fSyscallInfos[i];

			// get the parameter size
			int paramSize = 0;
			if (syscall.parameter_count > 0) {
				const gensyscall_parameter_info &lastParam
					= syscall.parameters[syscall.parameter_count - 1];
				paramSize = lastParam.offset + lastParam.actual_size;
			}

			// output the info for the syscall
			file << "\t{ " << syscall.kernel_name << ", "
				<< paramSize << " }," << endl;
		}

		// syscall infos array end
		file << "};" << endl;
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

		int32 chunkSize = (fSyscallCount + 19) / 20;
		
		// iterate through the syscalls
		for (int i = 0; i < fSyscallCount; i++) {
			const gensyscall_syscall_info &syscall = fSyscallInfos[i];

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
			file << "\t// " << syscall.name << endl;

			// create the return type handler
			file << "\thandler = new TypeHandlerImpl<" << syscall.return_type
				<< ">();" << endl;

			// create the syscall
			file << "\tsyscall = new Syscall(\"" << syscall.name << "\", "
				<< "handler);" << endl;
			file << "\tsyscalls.push_back(syscall);" << endl;

			// add the parameters
			for (int32 k = 0; k < syscall.parameter_count; k++) {
				const gensyscall_parameter_info &parameter
					= syscall.parameters[k];

				// create the parameter type handler
				file << "\thandler = new TypeHandlerImpl<"
					<< parameter.type << ">();" << endl;

				// add the parameter
				file << "\tsyscall->AddParameter(\"" << parameter.name << "\", "
					<< parameter.offset << ", handler);" << endl;
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

	void _UpdateSyscallInfos()
	{
		// Since getting the parameter offsets and actual sizes doesn't work
		// as it is now, we overwrite them with values computed using the
		// parameter alignment type.
		for (int i = 0; i < fSyscallCount; i++) {
			gensyscall_syscall_info &syscall = fSyscallInfos[i];
			int paramCount = syscall.parameter_count;
			gensyscall_parameter_info* parameters = syscall.parameters;
			int offset = 0;
			for (int k = 0; k < paramCount; k++) {
				if (parameters[k].size < PARAMETER_ALIGNMENT)
					parameters[k].actual_size = PARAMETER_ALIGNMENT;
				else
					parameters[k].actual_size = parameters[k].size;
				parameters[k].offset = offset;
				offset += parameters[k].actual_size;
			}
		}
	}

private:
	gensyscall_syscall_info	*fSyscallInfos;
	int						fSyscallCount;
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
