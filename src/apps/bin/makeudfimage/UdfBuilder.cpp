//----------------------------------------------------------------------
//  This software is part of the OpenBeOS distribution and is covered 
//  by the OpenBeOS license.
//
//  Copyright (c) 2003 Tyler Dauwalder, tyler@dauwalder.net
//----------------------------------------------------------------------

/*! \file UdfBuilder.cpp

	Main UDF image building class implementation.
*/

#include "UdfBuilder.h"

#include <stdio.h>

#include "UdfDebug.h"
#include "Utils.h"

using Udf::check_size_error;

/*! \brief Creates a new UdfBuilder object.
*/
UdfBuilder::UdfBuilder(const char *outputFile, uint32 blockSize, bool doUdf,
                       bool doIso, const ProgressListener &listener)
	: fInitStatus(B_NO_INIT)
	, fOutputFile(outputFile, B_READ_WRITE | B_CREATE_FILE)
	, fOutputFilename(outputFile)
	, fBlockSize(blockSize)
	, fBlockShift(0)
	, fDoUdf(doUdf)
	, fDoIso(doIso)
	, fListener(listener)
	, fAllocator(blockSize)
{
	DEBUG_INIT_ETC("UdfBuilder", ("blockSize: %ld, doUdf: %s, doIso: %s",
	               blockSize, bool_to_string(doUdf), bool_to_string(doIso)));

	// Check the output file
	status_t error = _OutputFile().InitCheck();
	if (!error) {
		// Check the allocator
		error = _Allocator().InitCheck();
		if (!error) {
			// Check the block size
			if (!error)
				error = Udf::get_block_shift(_BlockSize(), fBlockShift);
			if (!error)
				error = _BlockSize() >= 512 ? B_OK : B_BAD_VALUE;
			if (!error) {
				// Check that at least one type of filesystem has
				// been requested
				error = _DoUdf() || _DoIso() ? B_OK : B_BAD_VALUE;
				if (error) {
					_PrintError("No filesystems requested.");
				}			
			} else {
				_PrintError("Invalid block size: %ld", blockSize);
			}
		} else {
			_PrintError("Error creating block allocator: 0x%lx, `%s'", error,
			            strerror(error));
		}
	} else {
		_PrintError("Error opening output file: 0x%lx, `%s'", error,
		            strerror(error));
	}
	
	if (!error) {
		fInitStatus = B_OK;
	}
}

status_t
UdfBuilder::InitCheck() const
{
	return fInitStatus;
}

/*! \brief Builds the disc image.
*/
status_t
UdfBuilder::Build()
{
	DEBUG_INIT("UdfBuilder");
	status_t error = InitCheck();
	if (error)
		RETURN(error);

	_OutputFile().Seek(0, SEEK_SET);		
	_PrintUpdate(VERBOSITY_LOW, "Output file: `%s'", fOutputFilename.c_str());		

	// Reserve the first 32KB and zero them out.
	if (!error) {
		const int reservedAreaSize = 32 * 1024;
		Udf::extent_address extent(0, reservedAreaSize);
		error = _Allocator().GetExtent(extent);
		if (!error) {
			ssize_t bytes = _OutputFile().Zero(reservedAreaSize);
			error = check_size_error(bytes, reservedAreaSize);
		}
		// Error check
		if (error) {				 
			_PrintError("Error creating reserved area: 0x%lx, `%s'",
			            error, strerror(error));
		}
	}	

	const int vrsBlockSize = 2048;
	
	// Write the iso portion of the vrs
	if (!error && _DoIso()) {
		_PrintUpdate(VERBOSITY_LOW, "Writing iso portion of volume recognition sequence...");
		
		// Error check
		if (error) {				 
			_PrintError("Error writing iso vrs: 0x%lx, `%s'",
			            error, strerror(error));
		}
	}
			
	// Write the udf portion of the vrs
	if (!error && _DoUdf()) {
		_PrintUpdate(VERBOSITY_LOW, "Writing udf portion of volume recognition sequence...");
		Udf::extent_address extent;
		// Bea
		Udf::volume_structure_descriptor_header bea(0, Udf::kVSDID_BEA, 1);
		error = _Allocator().GetNextExtent(vrsBlockSize, true, extent);
		if (!error) {
			ssize_t bytes = _OutputFile().Write(&bea, sizeof(bea));
			error = check_size_error(bytes, sizeof(bea));
			if (!error) {
				bytes = _OutputFile().Zero(vrsBlockSize-sizeof(bea));
				error = check_size_error(bytes, vrsBlockSize-sizeof(bea));
			}
		}				
		// Nsr
		Udf::volume_structure_descriptor_header nsr(0, Udf::kVSDID_ECMA167_3, 1);
		if (!error)
			error = _Allocator().GetNextExtent(vrsBlockSize, true, extent);
		if (!error) {
			ssize_t bytes = _OutputFile().Write(&nsr, sizeof(nsr));
			error = check_size_error(bytes, sizeof(nsr));
			if (!error) {
				bytes = _OutputFile().Zero(vrsBlockSize-sizeof(nsr));
				error = check_size_error(bytes, vrsBlockSize-sizeof(nsr));
			}
		}				
		// Tea
		Udf::volume_structure_descriptor_header tea(0, Udf::kVSDID_TEA, 1);
		if (!error)
			error = _Allocator().GetNextExtent(vrsBlockSize, true, extent);
		if (!error) {
			ssize_t bytes = _OutputFile().Write(&tea, sizeof(tea));
			error = check_size_error(bytes, sizeof(tea));
			if (!error) {
				bytes = _OutputFile().Zero(vrsBlockSize-sizeof(tea));
				error = check_size_error(bytes, vrsBlockSize-sizeof(tea));
			}
		}				
		// Error check
		if (error) {				 
			_PrintError("Error writing udf vrs: 0x%lx, `%s'",
			            error, strerror(error));
		}
	}

	if (!error)
		_PrintUpdate(VERBOSITY_LOW, "Finished.");
	RETURN(error);
}

/*! \brief Uses vsprintf() to output the given format string and arguments
	into the given message string.
	
	va_start() must be called prior to calling this function to obtain the
	\a arguments parameter, but va_end() must *not* be called upon return,
	as this function takes the liberty of doing so for you.
*/
status_t
UdfBuilder::_FormatString(char *message, const char *formatString, va_list arguments) const
{
	status_t error = message && formatString ? B_OK : B_BAD_VALUE;
	if (!error) {
		vsprintf(message, formatString, arguments);
		va_end(arguments);	
	}
	return error;
}

/*! \brief Outputs a printf()-style error message to the listener.
*/
void
UdfBuilder::_PrintError(const char *formatString, ...) const
{
	if (!formatString) {
		DEBUG_INIT_ETC("UdfBuilder", ("formatString: `%s'", formatString));
		PRINT(("ERROR: _PrintError() called with NULL format string!\n"));
		return;
	}
	char message[kMaxUpdateStringLength];
	va_list arguments;
	va_start(arguments, formatString);
	status_t error = _FormatString(message, formatString, arguments);
	if (!error)
		fListener.OnError(message);	
}

/*! \brief Outputs a printf()-style warning message to the listener.
*/
void
UdfBuilder::_PrintWarning(const char *formatString, ...) const
{
	if (!formatString) {
		DEBUG_INIT_ETC("UdfBuilder", ("formatString: `%s'", formatString));
		PRINT(("ERROR: _PrintWarning() called with NULL format string!\n"));
		return;
	}
	char message[kMaxUpdateStringLength];
	va_list arguments;
	va_start(arguments, formatString);
	status_t error = _FormatString(message, formatString, arguments);
	if (!error)
		fListener.OnWarning(message);	
}

/*! \brief Outputs a printf()-style update message to the listener
	at the given verbosity level.
*/
void
UdfBuilder::_PrintUpdate(VerbosityLevel level, const char *formatString, ...) const
{
	if (!formatString) {
		DEBUG_INIT_ETC("UdfBuilder", ("level: %d, formatString: `%s'",
		               level, formatString));
		PRINT(("ERROR: _PrintUpdate() called with NULL format string!\n"));
		return;
	}
	char message[kMaxUpdateStringLength];
	va_list arguments;
	va_start(arguments, formatString);
	status_t error = _FormatString(message, formatString, arguments);
	if (!error)
		fListener.OnUpdate(level, message);	
}

