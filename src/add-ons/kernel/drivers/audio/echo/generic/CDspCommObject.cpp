// ****************************************************************************
//
//  	CDspCommObject.cpp
//
//		Implementation file for EchoGals generic driver DSP interface class.
//
//		Copyright Echo Digital Audio Corporation (c) 1998 - 2002
//		All rights reserved
//		www.echoaudio.com
//		
//		Permission is hereby granted, free of charge, to any person obtaining a
//		copy of this software and associated documentation files (the
//		"Software"), to deal with the Software without restriction, including
//		without limitation the rights to use, copy, modify, merge, publish,
//		distribute, sublicense, and/or sell copies of the Software, and to
//		permit persons to whom the Software is furnished to do so, subject to
//		the following conditions:
//		
//		- Redistributions of source code must retain the above copyright
//		notice, this list of conditions and the following disclaimers.
//		
//		- Redistributions in binary form must reproduce the above copyright
//		notice, this list of conditions and the following disclaimers in the
//		documentation and/or other materials provided with the distribution.
//		
//		- Neither the name of Echo Digital Audio, nor the names of its
//		contributors may be used to endorse or promote products derived from
//		this Software without specific prior written permission.
//
//		THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
//		EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
//		MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
//		IN NO EVENT SHALL THE CONTRIBUTORS OR COPYRIGHT HOLDERS BE LIABLE FOR
//		ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
//		TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
//		SOFTWARE OR THE USE OR OTHER DEALINGS WITH THE SOFTWARE.
//
// ****************************************************************************

#include "CEchoGals.h"

#ifdef DSP_56361
#include "LoaderDSP.c"
#endif


/****************************************************************************

	Construction and destruction

 ****************************************************************************/

//===========================================================================
//
// Overload new & delete so memory for this object is allocated
//	from non-paged memory.
//
//===========================================================================

PVOID CDspCommObject::operator new( size_t Size )
{
	PVOID 		pMemory;
	ECHOSTATUS 	Status;
	
	Status = OsAllocateNonPaged(Size,&pMemory);
	
	if ( (ECHOSTATUS_OK != Status) || (NULL == pMemory ))
	{
		ECHO_DEBUGPRINTF(("CDspCommObject::operator new - memory allocation failed\n"));

		pMemory = NULL;
	}
	else
	{
		memset( pMemory, 0, Size );
	}

	return pMemory;
	
}	// PVOID CDspCommObject::operator new( size_t Size )


VOID  CDspCommObject::operator delete( PVOID pVoid )
{
	if ( ECHOSTATUS_OK != OsFreeNonPaged( pVoid ) )
	{
		ECHO_DEBUGPRINTF( ("CDspCommObject::operator delete memory free "
								 "failed\n") );
	}
}	// VOID  CDspCommObject::operator delete( PVOID pVoid )


//===========================================================================
//
// Constructor
//
//===========================================================================

CDspCommObject::CDspCommObject
(
	PDWORD		pdwDspRegBase,				// Virtual ptr to DSP registers
	PCOsSupport	pOsSupport
)
{
	int	i;

	ASSERT( pOsSupport );
	
	//
	// Init all the basic stuff
	//
	strcpy( m_szCardName, "??????" );
	m_pOsSupport = pOsSupport;				// Ptr to OS Support methods & data
	m_pdwDspRegBase = pdwDspRegBase;		// Virtual addr DSP's register base
	m_bBadBoard = TRUE;						// Set TRUE until DSP loaded
	m_pwDspCode = NULL;						// Current DSP code not loaded
	m_byDigitalMode = DIGITAL_MODE_NONE;
	m_wInputClock = ECHO_CLOCK_INTERNAL;
	m_wOutputClock = ECHO_CLOCK_WORD;
	m_ullLastLoadAttemptTime = 0L - DSP_LOAD_ATTEMPT_PERIOD;	// force first load to go

#ifdef MIDI_SUPPORT	
	m_ullNextMidiWriteTime = 0;
#endif

	//
	// Create the DSP comm page - this is the area of memory read and written by
	// the DSP via bus mastering
	//
	if ( ECHOSTATUS_OK != 
				pOsSupport->OsPageAllocate( sizeof( DspCommPage ) / PAGE_SIZE + 1,
													 (VOID ** )&m_pDspCommPage,
													 &m_dwCommPagePhys ) ||
		  m_pDspCommPage == NULL )
	{
		ECHO_DEBUGPRINTF( ("CDspCommObject::CDspCommObject DSP comm page "
								 "memory allocation failed\n") );
		pOsSupport->EchoErrorMsg(
			"CDspCommObject::CDspCommObject DSP comm page "
			"memory allocation failed",
			"Critical Failure" );
		return;
	}

	//
	// Init the comm page
	//
	m_pDspCommPage->dwCommSize = SWAP( sizeof( DspCommPage ) );
													// Size of DSP comm page
	
	m_pDspCommPage->dwHandshake = 0xffffffff;
	m_pDspCommPage->dwMidiXmitStatus = 0xffffffff;

	for ( i = 0; i < DSP_MAXAUDIOINPUTS; i++ )
		m_pDspCommPage->InLineLevel[ i ] = 0x00;
													// Set line levels so we don't blast
													// any inputs on startup
	memset( m_pDspCommPage->byMonitors,
			  GENERIC_TO_DSP(ECHOGAIN_MUTED),
			  MONITOR_ARRAY_SIZE );			// Mute all monitors

	memset( m_pDspCommPage->byVmixerLevel,
			  GENERIC_TO_DSP(ECHOGAIN_MUTED),
			  VMIXER_ARRAY_SIZE );			// Mute all virtual mixer levels
			  
#ifdef DIGITAL_INPUT_AUTO_MUTE_SUPPORT

	m_fDigitalInAutoMute = TRUE;

#endif

}	// CDspCommObject::CDspCommObject


//===========================================================================
//
// Destructor
//
//===========================================================================

CDspCommObject::~CDspCommObject()
{
	//
	// Reset transport
	//
	CChannelMask cmActive = m_cmActive;
	ResetTransport( &cmActive );
	
	//
	// Meters off
	//
	m_wMeterOnCount = 1;
	SetMetersOn( FALSE );

#ifdef MIDI_SUPPORT

	//
	// MIDI input off
	//
	m_wMidiOnCount	= 1;
	SetMidiOn( FALSE );

#endif // MIDI_SUPPORT

	//
	// Go to sleep
	//
	GoComatose();

	//
	// Free the comm page
	//
	if ( NULL != m_pDspCommPage )
	{
		if ( ECHOSTATUS_OK != m_pOsSupport->OsPageFree(
													sizeof( DspCommPage ) / PAGE_SIZE + 1,
													m_pDspCommPage,
													m_dwCommPagePhys ) )
		{
			ECHO_DEBUGPRINTF(("CDspCommObject::~CDspCommObject "
									"DSP comm page memory free failed\n"));
		}
	}

	ECHO_DEBUGPRINTF( ( "CDspCommObject::~CDspCommObject() is toast!\n" ) );

}	// CDspCommObject::~CDspCommObject()




/****************************************************************************

	Firmware loading functions

 ****************************************************************************/

//===========================================================================
//
// ASIC status check - some cards have one or two ASICs that need to be 
// loaded.  Once that load is complete, this function is called to see if
// the load was successful. 
//
// If this load fails, it does not necessarily mean that the hardware is
// defective - the external box may be disconnected or turned off.
//
// This routine sometimes fails for Layla20; for Layla20, the loop runs 5 times
// and succeeds if it wins on three of the loops.
//
//===========================================================================

BOOL CDspCommObject::CheckAsicStatus()
{
#if NUM_ASIC_TESTS!=1
	DWORD	dwGoodCt = 0;
#endif

	DWORD	dwAsicStatus;
	DWORD	dwReturn;
	int 	iNumTests;

	if ( !m_bHasASIC )
	{
		m_bASICLoaded = TRUE;
		return TRUE;
	}
			
	iNumTests = NUM_ASIC_TESTS;	
		
	for ( int i = 0; i < iNumTests; i++ )
	{
		// Send the vector command
		SendVector( DSP_VC_TEST_ASIC );	

		// Since this is a Layla, the DSP will return a value to
		// indicate whether or not the ASIC is currently loaded
		dwReturn = Read_DSP( &dwAsicStatus);
		if ( ECHOSTATUS_OK != dwReturn )
		{
			ECHO_DEBUGPRINTF(("CDspCommObject::CheckAsicStatus - failed on Read_DSP\n"));
			ECHO_DEBUGBREAK();
			m_bASICLoaded = FALSE;
			return FALSE;
		}

#ifdef ECHO_DEBUG
		if ( dwAsicStatus != ASIC_ALREADY_LOADED &&
			  dwAsicStatus != ASIC_NOT_LOADED )
		{
			ECHO_DEBUGBREAK(); 
		}
#endif
	
		if ( dwAsicStatus == ASIC_ALREADY_LOADED )
		{
		
#if NUM_ASIC_TESTS==1
			m_bASICLoaded = TRUE;
			break;
#else
			//
			// For Layla20
			//
			if ( ++dwGoodCt == 3 )
			{
				m_bASICLoaded = TRUE;
				break;
			}
#endif
		}
		else
		{
			m_bASICLoaded = FALSE;
		}
	}

	return m_bASICLoaded;

}	// BOOL CDspCommObject::CheckAsicStatus()


//===========================================================================
//
//	Load ASIC code - done after the DSP is loaded
//
//===========================================================================

BOOL CDspCommObject::LoadASIC
(
	DWORD	dwCmd,
	PBYTE	pCode,
	DWORD	dwSize
)
{
	DWORD i;

	ECHO_DEBUGPRINTF(("CDspCommObject::LoadASIC\n"));

	if ( !m_bHasASIC )
		return TRUE;

#ifdef _DEBUG
	DWORD			dwChecksum = 0;
	ULONGLONG	ullStartTime, ullCurTime;
	m_pOsSupport->OsGetSystemTime( &ullStartTime );
#endif

	// Send the "Here comes the ASIC" command
	if ( ECHOSTATUS_OK != Write_DSP( dwCmd ) )
		return FALSE;

	// Write length of ASIC file in bytes
	if ( ECHOSTATUS_OK != Write_DSP( dwSize ) )
		return FALSE;

	for ( i = 0; i < dwSize; i++ )
	{
#ifdef _DEBUG
		dwChecksum += pCode[i];
#endif	
	
		if ( ECHOSTATUS_OK != Write_DSP( pCode[ i ] ) )
		{
			ECHO_DEBUGPRINTF(("\tfailed on Write_DSP\n"));
			return FALSE;
		}
	}

#ifdef _DEBUG
	m_pOsSupport->OsGetSystemTime( &ullCurTime );
	ECHO_DEBUGPRINTF( ("CDspCommObject::LoadASIC took %ld usec.\n",
							(ULONG) ( ullCurTime - ullStartTime ) ) );
	ECHO_DEBUGPRINTF(("\tChecksum is 0x%lx\n",dwChecksum));		
	ECHO_DEBUGPRINTF(("ASIC load OK\n"));
#endif

	return TRUE;
}	// BOOL CDspCommObject::LoadASIC( DWORD dwCmd, PBYTE pCode, DWORD dwSize )


//===========================================================================
//
// InstallResidentLoader
//
// Install the resident loader for 56361 DSPs;  The resident loader
// is on the EPROM on the board for 56301 DSP.
//
// The resident loader is a tiny little program that is used to load
// the real DSP code.
//
//===========================================================================

#ifdef DSP_56361

ECHOSTATUS CDspCommObject::InstallResidentLoader()
{
	DWORD			dwAddress;
	DWORD			dwIndex;
	int			iNum;
	int			i;
	DWORD			dwReturn;
	PWORD			pCode;

	ECHO_DEBUGPRINTF( ("CDspCommObject::InstallResidentLoader\n") );
	
	//
	// 56361 cards only!
	//
	if (DEVICE_ID_56361 != m_pOsSupport->GetDeviceId() )
		return ECHOSTATUS_OK;

	//
	// Look to see if the resident loader is present.  If the resident loader
	// is already installed, host flag 5 will be on.
	//
	DWORD dwStatus;
	dwStatus = GetDspRegister( CHI32_STATUS_REG );
	if ( 0 != (dwStatus & CHI32_STATUS_REG_HF5 ) )
	{
		ECHO_DEBUGPRINTF(("\tResident loader already installed; status is 0x%lx\n",
								dwStatus));
		return ECHOSTATUS_OK;
	}
	//
	// Set DSP format bits for 24 bit mode
	//
	SetDspRegister( CHI32_CONTROL_REG,
						 GetDspRegister( CHI32_CONTROL_REG ) | 0x900 );

	//---------------------------------------------------------------------------
	//
	// Loader
	//
	// The DSP code is an array of 16 bit words.  The array is divided up into
	// sections.  The first word of each section is the size in words, followed
	// by the section type.
	//
	// Since DSP addresses and data are 24 bits wide, they each take up two
	// 16 bit words in the array.
	//
	// This is a lot like the other loader loop, but it's not a loop,
	// you don't write the memory type, and you don't write a zero at the end.
	//
	//---------------------------------------------------------------------------

	pCode = pwLoaderDSP;
	//
	// Skip the header section; the first word in the array is the size of 
	//	the first section, so the first real section of code is pointed to 
	//	by pCode[0].
	//
	dwIndex = pCode[ 0 ];
	//
	// Skip the section size, LRS block type, and DSP memory type
	//
	dwIndex += 3;
	//	
	// Get the number of DSP words to write
	//
	iNum = pCode[ dwIndex++ ];
	//
	// Get the DSP address for this block; 24 bits, so build from two words
	//
	dwAddress = ( pCode[ dwIndex ] << 16 ) + pCode[ dwIndex + 1 ];
	dwIndex += 2;
	//	
	// Write the count to the DSP
	//
	dwReturn = Write_DSP( (DWORD) iNum );
	if ( dwReturn != 0 )
	{
		ECHO_DEBUGPRINTF( ("CDspCommObject::InstallResidentLoader: Failed to "
								 "write word count!\n") );
		return ECHOSTATUS_DSP_DEAD;
	}

	// Write the DSP address
	dwReturn = Write_DSP( dwAddress );
	if ( dwReturn != 0 )
	{
		ECHO_DEBUGPRINTF( ("CDspCommObject::InstallResidentLoader: Failed to "
								 "write DSP address!\n") );
		return ECHOSTATUS_DSP_DEAD;
	}


	// Write out this block of code to the DSP
	for ( i = 0; i < iNum; i++) // 
	{
		DWORD	dwData;

		dwData = ( pCode[ dwIndex ] << 16 ) + pCode[ dwIndex + 1 ];
		dwReturn = Write_DSP( dwData );
		if ( dwReturn != 0 )
		{
			ECHO_DEBUGPRINTF( ("CDspCommObject::InstallResidentLoader: Failed to "
									 "write DSP code\n") );
			return ECHOSTATUS_DSP_DEAD;
		}

		dwIndex+=2;
	}
	
	//
	// Wait for flag 5 to come up
	//
	BOOL			fSuccess;
	ULONGLONG 	ullCurTime,ullTimeout;

	m_pOsSupport->OsGetSystemTime( &ullCurTime );
	ullTimeout = ullCurTime + 10000L;		// 10m.s.
	fSuccess = FALSE;
	do
	{
		m_pOsSupport->OsSnooze(50);	// Give the DSP some time;
														// no need to hog the CPU
		
		dwStatus = GetDspRegister( CHI32_STATUS_REG );
		if (0 != (dwStatus & CHI32_STATUS_REG_HF5))
		{
			fSuccess = TRUE;
			break;
		}
		
		m_pOsSupport->OsGetSystemTime( &ullCurTime );

	} while (ullCurTime < ullTimeout);
	
	if (FALSE == fSuccess)
	{
		ECHO_DEBUGPRINTF(("\tResident loader failed to set HF5\n"));
		return ECHOSTATUS_DSP_DEAD;
	}
		
	ECHO_DEBUGPRINTF(("\tResident loader successfully installed\n"));

	return ECHOSTATUS_OK;
	
}	// ECHOSTATUS CDspCommObject::InstallResidentLoader()

#endif // DSP_56361


//===========================================================================
//
// LoadDSP
//
// This loads the DSP code.
//
//===========================================================================

ECHOSTATUS CDspCommObject::LoadDSP
(
	PWORD	pCode					// Ptr to DSP object code
)
{
	DWORD			dwAddress;
	DWORD			dwIndex;
	int			iNum;
	int			i;
	DWORD			dwReturn;
	ULONGLONG	ullTimeout, ullCurTime;
	ECHOSTATUS	Status;

	ECHO_DEBUGPRINTF(("CDspCommObject::LoadDSP\n"));
	if ( m_pwDspCode == pCode )
	{
		ECHO_DEBUGPRINTF( ("\tDSP is already loaded!\n") );
		return ECHOSTATUS_FIRMWARE_LOADED;
	}
	m_bBadBoard = TRUE;		// Set TRUE until DSP loaded
	m_pwDspCode = NULL;		// Current DSP code not loaded
	m_bASICLoaded = FALSE;	// Loading the DSP code will reset the ASIC
	
	ECHO_DEBUGPRINTF(("CDspCommObject::LoadDSP  Set m_bBadBoard to TRUE\n"));
	
	//
	//	If this board requires a resident loader, install it.
	//
#ifdef DSP_56361
	InstallResidentLoader();
#endif

	// Send software reset command	
	if ( ECHOSTATUS_OK != SendVector( DSP_VC_RESET ) )
	{
		m_pOsSupport->EchoErrorMsg(
			"CDspCommObject::LoadDsp SendVector DSP_VC_RESET failed",
			"Critical Failure" );
		return ECHOSTATUS_DSP_DEAD;
	}

	// Delay 10us
	m_pOsSupport->OsSnooze( 10L );

	// Wait 10ms for HF3 to indicate that software reset is complete	
	m_pOsSupport->OsGetSystemTime( &ullCurTime );
	ullTimeout = ullCurTime + 10000L;		// 10m.s.

	while ( 1 ) 
	{
		if ( GetDspRegister( CHI32_STATUS_REG ) & CHI32_STATUS_REG_HF3 )
			break;
		m_pOsSupport->OsGetSystemTime( &ullCurTime );
		if ( ullCurTime > ullTimeout)
		{
			ECHO_DEBUGPRINTF( ("CDspCommObject::LoadDSP Timeout waiting for "
									 "CHI32_STATUS_REG_HF3\n") );
			m_pOsSupport->EchoErrorMsg(
				"CDspCommObject::LoadDSP SendVector DSP_VC_RESET failed",
				"Critical Failure" );
			return ECHOSTATUS_DSP_TIMEOUT;
		}
	}

	// Set DSP format bits for 24 bit mode now that soft reset is done
	SetDspRegister( CHI32_CONTROL_REG,
						 GetDspRegister( CHI32_CONTROL_REG ) | (DWORD) 0x900 );

	//---------------------------------------------------------------------------
	// Main loader loop
	//---------------------------------------------------------------------------

	dwIndex = pCode[ 0 ];

	for (;;)
	{
		int	iBlockType;
		int	iMemType;

		// Total Block Size
		dwIndex++;
		
		// Block Type
		iBlockType = pCode[ dwIndex ];
		if ( iBlockType == 4 )  // We're finished
			break;

		dwIndex++;

		// Memory Type  P=0,X=1,Y=2
		iMemType = pCode[ dwIndex ]; 
		dwIndex++;
		
		// Block Code Size
		iNum = pCode[ dwIndex ];
		dwIndex++;
		if ( iNum == 0 )			// We're finished
			break;
	
 		// Start Address
		dwAddress = ( (DWORD) pCode[ dwIndex ] << 16 ) + pCode[ dwIndex + 1 ];
//		ECHO_DEBUGPRINTF( ("\tdwAddress %lX\n", dwAddress) );
		dwIndex += 2;
		
		dwReturn = Write_DSP( (DWORD)iNum );
		if ( dwReturn != 0 )
		{
			ECHO_DEBUGPRINTF(("LoadDSP - failed to write number of DSP words\n"));
			return ECHOSTATUS_DSP_DEAD;
		}

		dwReturn = Write_DSP( dwAddress );
		if ( dwReturn != 0 )
		{
			ECHO_DEBUGPRINTF(("LoadDSP - failed to write DSP address\n"));
			return ECHOSTATUS_DSP_DEAD;
		}

		dwReturn = Write_DSP( (DWORD)iMemType );
		if ( dwReturn != 0 )
		{
			ECHO_DEBUGPRINTF(("LoadDSP - failed to write DSP memory type\n"));
			return ECHOSTATUS_DSP_DEAD;
		}

		// Code
		for ( i = 0; i < iNum; i++ )
		{
			DWORD	dwData;

			dwData = ( (DWORD) pCode[ dwIndex ] << 16 ) + pCode[ dwIndex + 1 ];
			dwReturn = Write_DSP( dwData );
			if ( dwReturn != 0 )
			{
				ECHO_DEBUGPRINTF(("LoadDSP - failed to write DSP data\n"));
				return ECHOSTATUS_DSP_DEAD;
			}
	
			dwIndex += 2;
		}
//		ECHO_DEBUGPRINTF( ("\tEnd Code Block\n") );
	}
	dwReturn = Write_DSP( 0 );					// We're done!!!
	if ( dwReturn != 0 )
	{
		ECHO_DEBUGPRINTF(("LoadDSP: Failed to write final zero\n"));
		return ECHOSTATUS_DSP_DEAD;
	}
		

	// Delay 10us
	m_pOsSupport->OsSnooze( 10L );

	m_pOsSupport->OsGetSystemTime( &ullCurTime );
	ullTimeout  = ullCurTime + 500000L;		// 1/2 sec. timeout

	while ( ullCurTime <= ullTimeout) 
	{
		//
		// Wait for flag 4 - indicates that the DSP loaded OK
		//
		if ( GetDspRegister( CHI32_STATUS_REG ) & CHI32_STATUS_REG_HF4 )
		{
			SetDspRegister( CHI32_CONTROL_REG,
								 GetDspRegister( CHI32_CONTROL_REG ) & ~0x1b00 );

			dwReturn = Write_DSP( DSP_FNC_SET_COMMPAGE_ADDR );
			if ( dwReturn != 0 )
			{
				ECHO_DEBUGPRINTF(("LoadDSP - Failed to write DSP_FNC_SET_COMMPAGE_ADDR\n"));
				return ECHOSTATUS_DSP_DEAD;
			}
				
			dwReturn = Write_DSP( m_dwCommPagePhys );
			if ( dwReturn != 0 )
			{
				ECHO_DEBUGPRINTF(("LoadDSP - Failed to write comm page address\n"));
				return ECHOSTATUS_DSP_DEAD;
			}

			//
			// Get the serial number via slave mode.
			// This is triggered by the SET_COMMPAGE_ADDR command.
			//	We don't actually use the serial number but we have to get
			//	it as part of the DSP init vodoo.
			//
			Status = ReadSn();
			if ( ECHOSTATUS_OK != Status )
			{
				ECHO_DEBUGPRINTF(("LoadDSP - Failed to read serial number\n"));
				return Status;
			}

			m_pwDspCode = pCode;			// Show which DSP code loaded
			m_bBadBoard = FALSE;			// DSP OK
			
			ECHO_DEBUGPRINTF(("CDspCommObject::LoadDSP  Set m_bBadBoard to FALSE\n"));			
		
			return ECHOSTATUS_OK;
		}
		
		m_pOsSupport->OsGetSystemTime( &ullCurTime );
	}
	
	ECHO_DEBUGPRINTF( ("LoadDSP: DSP load timed out waiting for HF4\n") );	
	
	return ECHOSTATUS_DSP_TIMEOUT;

}	// DWORD	CDspCommObject::LoadDSP


//===========================================================================
//
// LoadFirmware takes care of loading the DSP and any ASIC code.
//
//===========================================================================

ECHOSTATUS CDspCommObject::LoadFirmware()
{
	ECHOSTATUS	dwReturn;
	ULONGLONG	ullRightNow;

	// Try to load the DSP
	if ( NULL == m_pwDspCodeToLoad || NULL == m_pDspCommPage )
	{
		ECHO_DEBUGBREAK();
		return ECHOSTATUS_NO_MEM;
	}
	
	//
	// Even if the external box is off, an application may still try
	// to repeatedly open the driver, causing multiple load attempts and 
	// making the machine spend lots of time in the kernel.  If the ASIC is not
	// loaded, this code will gate the loading attempts so it doesn't happen
	// more than once per second.
	//
	m_pOsSupport->OsGetSystemTime(&ullRightNow);
	if ( 	(FALSE == m_bASICLoaded) &&
			(DSP_LOAD_ATTEMPT_PERIOD > (ullRightNow - m_ullLastLoadAttemptTime)) )
		return ECHOSTATUS_ASIC_NOT_LOADED;
	
	//
	// Update the timestamp
	//
	m_ullLastLoadAttemptTime = ullRightNow;

	//
	// See if the ASIC is present and working - only if the DSP is already loaded
	//	
	if (NULL != m_pwDspCode)
	{
		dwReturn = CheckAsicStatus();
		if (TRUE == dwReturn)
			return ECHOSTATUS_OK;
		
		//
		// ASIC check failed; force the DSP to reload
		//	
		m_pwDspCode = NULL;
	}

	//
	// Try and load the DSP
	//
	dwReturn = LoadDSP( m_pwDspCodeToLoad );
	if ( 	(ECHOSTATUS_OK != dwReturn) && 
			(ECHOSTATUS_FIRMWARE_LOADED != dwReturn) )
	{
		return dwReturn;
	}
	
	ECHO_DEBUGPRINTF(("DSP load OK\n"));

	//
	// Load the ASIC if the DSP load succeeded; LoadASIC will
	// always return TRUE for cards that don't have an ASIC.
	//
	dwReturn = LoadASIC();
	if ( FALSE == dwReturn )
	{
		dwReturn = ECHOSTATUS_ASIC_NOT_LOADED;
	}
	else
	{
		//
		// ASIC load was successful
		//
		RestoreDspSettings();

		dwReturn = ECHOSTATUS_OK;
	}
	
	return dwReturn;
	
}	// BOOL CDspCommObject::LoadFirmware()


//===========================================================================
//
// This function is used to read back the serial number from the DSP;
// this is triggered by the SET_COMMPAGE_ADDR command.
//
// Only some early Echogals products have serial numbers in the ROM;
// the serial number is not used, but you still need to do this as
// part of the DSP load process.
//
//===========================================================================

ECHOSTATUS CDspCommObject::ReadSn()
{
	int			j;
	DWORD			dwSn[ 6 ];
	ECHOSTATUS	Status;

	ECHO_DEBUGPRINTF( ("CDspCommObject::ReadSn\n") );
	for ( j = 0; j < 5; j++ )
	{
		Status = Read_DSP( &dwSn[ j ] );
		if ( Status != 0 )
		{
			ECHO_DEBUGPRINTF( ("\tFailed to read serial number word %d\n",
									 j) );
			return ECHOSTATUS_DSP_DEAD;
		}
	}
	ECHO_DEBUGPRINTF( ("\tRead serial number %08lx %08lx %08lx %08lx %08lx\n",
							 dwSn[0], dwSn[1], dwSn[2], dwSn[3], dwSn[4]) );
	return ECHOSTATUS_OK;
	
}	// DWORD	CDspCommObject::ReadSn




//===========================================================================
//
//	This is called after LoadFirmware to restore old gains, meters on, 
// monitors, etc.
//
//===========================================================================

void CDspCommObject::RestoreDspSettings()
{
	ECHO_DEBUGPRINTF(("RestoreDspSettings\n"));

	if ( !CheckAsicStatus() )
		return;

	m_pDspCommPage->dwHandshake = 0xffffffff;		
	
#ifdef MIDI_SUPPORT
	m_ullNextMidiWriteTime = 0;
#endif
	
	SetSampleRate();
	if ( 0 != m_wMeterOnCount )
	{
		SendVector( DSP_VC_METERS_ON );
	}
	if ( !WaitForHandshake() )
		return;

	SetInputClock( m_wInputClock );
	SetOutputClock( m_wOutputClock );
		
	if ( !WaitForHandshake() )
		return;
	UpdateAudioOutLineLevel();

	if ( !WaitForHandshake() )
		return;
	UpdateAudioInLineLevel();

	if ( HasVmixer() )
	{
		if ( !WaitForHandshake() )
			return;
		UpdateVmixerLevel();
	}
	
	if ( !WaitForHandshake() )
		return;

	ClearHandshake();
	SendVector( DSP_VC_UPDATE_FLAGS );
	
	ECHO_DEBUGPRINTF(("RestoreDspSettings done\n"));	
	
}	// void CDspCommObject::RestoreDspSettings()




/****************************************************************************

	DSP utilities

 ****************************************************************************/

//===========================================================================
//
// Write_DSP writes a 32-bit value to the DSP; this is used almost 
// exclusively for loading the DSP.
//
//===========================================================================

ECHOSTATUS CDspCommObject::Write_DSP
(
	DWORD dwData				// 32 bit value to write to DSP data register
)
{
	DWORD 		dwStatus;
	ULONGLONG 	ullCurTime, ullTimeout;

//	ECHO_DEBUGPRINTF(("Write_DSP\n"));
	
	m_pOsSupport->OsGetSystemTime( &ullCurTime );
	ullTimeout = ullCurTime + 10000000L;		// 10 sec.
	while ( ullTimeout >= ullCurTime ) 
	{
		dwStatus = GetDspRegister( CHI32_STATUS_REG );
		if ( ( dwStatus & CHI32_STATUS_HOST_WRITE_EMPTY ) != 0 )
		{
			SetDspRegister( CHI32_DATA_REG, dwData );
//			ECHO_DEBUGPRINTF(("Write DSP: 0x%x", dwData));
			return ECHOSTATUS_OK;
		}
		m_pOsSupport->OsGetSystemTime( &ullCurTime );
	}

	m_bBadBoard = TRUE;		// Set TRUE until DSP re-loaded
	
	ECHO_DEBUGPRINTF(("CDspCommObject::Write_DSP  Set m_bBadBoard to TRUE\n"));
		
	return ECHOSTATUS_DSP_TIMEOUT;
	
}	// ECHOSTATUS CDspCommObject::Write_DSP


//===========================================================================
//
// Read_DSP reads a 32-bit value from the DSP; this is used almost 
// exclusively for loading the DSP and checking the status of the ASIC.
//
//===========================================================================

ECHOSTATUS CDspCommObject::Read_DSP
(
	DWORD *pdwData				// Ptr to 32 bit value read from DSP data register
)
{
	DWORD 		dwStatus;
	ULONGLONG	ullCurTime, ullTimeout;

//	ECHO_DEBUGPRINTF(("Read_DSP\n"));
	m_pOsSupport->OsGetSystemTime( &ullCurTime );

	ullTimeout = ullCurTime + READ_DSP_TIMEOUT;	
	while ( ullTimeout >= ullCurTime )
	{
		dwStatus = GetDspRegister( CHI32_STATUS_REG );
		if ( ( dwStatus & CHI32_STATUS_HOST_READ_FULL ) != 0 )
		{
			*pdwData = GetDspRegister( CHI32_DATA_REG );
//			ECHO_DEBUGPRINTF(("Read DSP: 0x%x\n", *pdwData));
			return ECHOSTATUS_OK;
		}
		m_pOsSupport->OsGetSystemTime( &ullCurTime );
	}

	m_bBadBoard = TRUE;		// Set TRUE until DSP re-loaded
	
	ECHO_DEBUGPRINTF(("CDspCommObject::Read_DSP  Set m_bBadBoard to TRUE\n"));	
	
	return ECHOSTATUS_DSP_TIMEOUT;
}	// ECHOSTATUS CDspCommObject::Read_DSP


//===========================================================================
//
// Much of the interaction between the DSP and the driver is done via vector
// commands; SendVector writes a vector command to the DSP.  Typically,
// this causes the DSP to read or write fields in the comm page.
//
// Returns ECHOSTATUS_OK if sent OK.
//
//===========================================================================

ECHOSTATUS CDspCommObject::SendVector
(
	DWORD dwCommand				// 32 bit command to send to DSP vector register
)
{
	ULONGLONG	ullTimeout;
	ULONGLONG	ullCurTime;

//
// Turn this on if you want to see debug prints for every vector command
//
#if 0
//#ifdef ECHO_DEBUG
	char *	pszCmd;
	switch ( dwCommand )
	{
		case DSP_VC_ACK_INT :
			pszCmd = "DSP_VC_ACK_INT";
			break;
		case DSP_VC_SET_VMIXER_GAIN :
			pszCmd = "DSP_VC_SET_VMIXER_GAIN";
			break;
		case DSP_VC_START_TRANSFER :
			pszCmd = "DSP_VC_START_TRANSFER";
			break;
		case DSP_VC_METERS_ON :
			pszCmd = "DSP_VC_METERS_ON";
			break;
		case DSP_VC_METERS_OFF :
			pszCmd = "DSP_VC_METERS_OFF";
			break;
		case DSP_VC_UPDATE_OUTVOL :
			pszCmd = "DSP_VC_UPDATE_OUTVOL";
			break;
		case DSP_VC_UPDATE_INGAIN :
			pszCmd = "DSP_VC_UPDATE_INGAIN";
			break;
		case DSP_VC_ADD_AUDIO_BUFFER :
			pszCmd = "DSP_VC_ADD_AUDIO_BUFFER";
			break;
		case DSP_VC_TEST_ASIC :
			pszCmd = "DSP_VC_TEST_ASIC";
			break;
		case DSP_VC_UPDATE_CLOCKS :
			pszCmd = "DSP_VC_UPDATE_CLOCKS";
			break;
		case DSP_VC_SET_LAYLA_SAMPLE_RATE :
			if ( GetCardType() == LAYLA )
				pszCmd = "DSP_VC_SET_LAYLA_RATE";
			else if ( GetCardType() == GINA || GetCardType() == DARLA )
				pszCmd = "DSP_VC_SET_GD_AUDIO_STATE";
			else
				pszCmd = "DSP_VC_WRITE_CONTROL_REG";
			break;
		case DSP_VC_MIDI_WRITE :
			pszCmd = "DSP_VC_MIDI_WRITE";
			break;
		case DSP_VC_STOP_TRANSFER :
			pszCmd = "DSP_VC_STOP_TRANSFER";
			break;
		case DSP_VC_UPDATE_FLAGS :
			pszCmd = "DSP_VC_UPDATE_FLAGS";
			break;
		case DSP_VC_RESET :
			pszCmd = "DSP_VC_RESET";
			break;
		default :
			pszCmd = "?????";
			break;
	}

	ECHO_DEBUGPRINTF( ("SendVector: %s dwCommand %s (0x%x)\n",
								GetCardName(),
								pszCmd,
								dwCommand) );				
#endif

	m_pOsSupport->OsGetSystemTime( &ullCurTime );
	ullTimeout = ullCurTime + 100000L;		// 100m.s.

	//
	// Wait for the "vector busy" bit to be off
	//
	while ( ullCurTime <= ullTimeout) 
	{
		if ( 0 == ( GetDspRegister( CHI32_VECTOR_REG ) & CHI32_VECTOR_BUSY ) )
		{
			SetDspRegister( CHI32_VECTOR_REG, dwCommand );
			return ECHOSTATUS_OK;
		}
		m_pOsSupport->OsGetSystemTime( &ullCurTime );
	}

	ECHO_DEBUGPRINTF( ("\tPunked out on SendVector\n") );
	ECHO_DEBUGBREAK();
	return ECHOSTATUS_DSP_TIMEOUT;
	
}	// ECHOSTATUS CDspCommObject::SendVector


//===========================================================================
//
//	Some vector commands involve the DSP reading or writing data to and
// from the comm page; if you send one of these commands to the DSP,
// it will complete the command and then write a non-zero value to
// the dwHandshake field in the comm page.  This function waits for the 
// handshake to show up.
//
//===========================================================================

BOOL CDspCommObject::WaitForHandshake()
{
	int	i;

	//
	// Wait up to three milliseconds for the handshake from the DSP 
	//
	for ( i = 0; i < HANDSHAKE_TIMEOUT; i++ )
	{
		// Look for the handshake value
		if ( 0 != GetHandshakeFlag() )
			break;

		// Give the DSP time to access the comm page
		m_pOsSupport->OsSnooze( 2 );
	}

	if ( HANDSHAKE_TIMEOUT == i )
	{
		ECHO_DEBUGPRINTF( ("CDspCommObject::WaitForHandshake: Timeout waiting "
								 "for DSP\n") );
		ECHO_DEBUGBREAK();
		return FALSE;
	}

	return TRUE;
	
}		// DWORD	CDspCommObject::WaitForHandshake()




/****************************************************************************

	Transport methods

 ****************************************************************************/

//===========================================================================
//
// StartTransport starts transport for a set of pipes
//
//===========================================================================

ECHOSTATUS CDspCommObject::StartTransport
(
	PCChannelMask	pChannelMask,			// Pipes to start
	PCChannelMask	pCyclicMask				// Which pipes are cyclic buffers
)
{
	ECHO_DEBUGPRINTF( ("StartTransport\n") );

	//
	// Wait for the previous command to complete
	//
	if ( !WaitForHandshake() )
		return ECHOSTATUS_DSP_DEAD;

	//
	// Write the appropriate fields in the comm page
	//
	m_pDspCommPage->cmdStart += *pChannelMask;
	m_pDspCommPage->cmdCyclicBuffer += *pCyclicMask;
	if ( !m_pDspCommPage->cmdStart.IsEmpty() )
	{
		//
		// Clear the handshake and send the vector command
		//
		ClearHandshake();
		SendVector( DSP_VC_START_TRANSFER );

		//
		// Wait for transport to start
		//
		if ( !WaitForHandshake() )
			return ECHOSTATUS_DSP_DEAD;

		//
		// Keep track of which pipes are transporting
		//
		m_cmActive += *pChannelMask;
		m_pDspCommPage->cmdStart.Clear();

		return ECHOSTATUS_OK;
	}		// if this monkey is being started

	ECHO_DEBUGPRINTF( ("CDspCommObject::StartTransport: No pipes to start!\n") );
	return ECHOSTATUS_INVALID_CHANNEL;
	
}	// ECHOSTATUS CDspCommObject::StartTransport


//===========================================================================
//
// StopTransport pauses transport for a set of pipes
//
//===========================================================================

ECHOSTATUS CDspCommObject::StopTransport
(
	PCChannelMask	pChannelMask
)
{
	ECHO_DEBUGPRINTF(("StopTransport\n"));

	//
	// Wait for the last command to finish
	//
	if ( !WaitForHandshake() )
		return ECHOSTATUS_DSP_DEAD;

	//
	// Write to the comm page
	//
	m_pDspCommPage->cmdStop += *pChannelMask;
	m_pDspCommPage->cmdReset.Clear();
	if ( !m_pDspCommPage->cmdStop.IsEmpty() )
	{
		//
		// Clear the handshake and send the vector command
		//
		ClearHandshake();
		SendVector( DSP_VC_STOP_TRANSFER );

		//
		// Wait for transport to stop
		//
		if ( !WaitForHandshake() )
			return ECHOSTATUS_DSP_DEAD;

		//
		// Keep track of which pipes are transporting
		//
		m_cmActive -= *pChannelMask;
		m_pDspCommPage->cmdStop.Clear();
		m_pDspCommPage->cmdReset.Clear();

		return ECHOSTATUS_OK;
	}		// if this monkey is being started

	ECHO_DEBUGPRINTF( ("CDspCommObject::StopTransport: No pipes to stop!\n") );
	return ECHOSTATUS_OK;

}	// ECHOSTATUS CDspCommObject::StopTransport


//===========================================================================
//
// ResetTransport resets transport for a set of pipes
//
//===========================================================================

ECHOSTATUS CDspCommObject::ResetTransport
(
	PCChannelMask	pChannelMask
)
{
	ECHO_DEBUGPRINTF(("ResetTransport\n"));

	//
	// Wait for the last command to finish
	//
	if ( !WaitForHandshake() )
		return ECHOSTATUS_DSP_DEAD;

	//
	// Write to the comm page
	//
	m_pDspCommPage->cmdStop += *pChannelMask;
	m_pDspCommPage->cmdReset += *pChannelMask;
	if ( !m_pDspCommPage->cmdReset.IsEmpty() )
	{
		//
		// Clear the handshake and send the vector command
		//
		ClearHandshake();
		SendVector( DSP_VC_STOP_TRANSFER );

		//
		// Wait for transport to stop
		//
		if ( !WaitForHandshake() )
			return ECHOSTATUS_DSP_DEAD;

		//
		// Keep track of which pipes are transporting
		//
		m_cmActive -= *pChannelMask;
		m_pDspCommPage->cmdStop.Clear();
		m_pDspCommPage->cmdReset.Clear();

		return ECHOSTATUS_OK;
	}		// if this monkey is being started

	ECHO_DEBUGPRINTF( ("CDspCommObject::ResetTransport: No pipes to reset!\n") );
	return ECHOSTATUS_OK;

}	// ECHOSTATUS CDspCommObject::ResetTransport


//===========================================================================
//
//	Calling AddBuffer tells the DSP to increment its internal buffer
// count for a given pipe.
//
//===========================================================================

ECHOSTATUS CDspCommObject::AddBuffer( WORD wPipeIndex )
{
	//
	// Parameter check
	//
	if ( wPipeIndex >= GetNumPipes())
	{
		ECHO_DEBUGPRINTF( ("CDspCommObject::AddBuffer: Invalid pipe %d\n",
								 wPipeIndex) );
		return ECHOSTATUS_INVALID_CHANNEL;
	}

	//
	// Wait for the last command
	//
	if ( !WaitForHandshake() )
		return ECHOSTATUS_DSP_DEAD;
		
	//
	// Write to the comm page
	//
	m_pDspCommPage->cmdAddBuffer.Clear();
	m_pDspCommPage->cmdAddBuffer.SetIndexInMask( wPipeIndex );

	//
	// Clear the handshake and send the vector command
	//
	ClearHandshake();
	return SendVector( DSP_VC_ADD_AUDIO_BUFFER );	
	
}	// ECHOSTATUS CDspCommObject::AddOutBuffer( WORD wPipeIndex )


//===========================================================================
//
// This tells the DSP where to start reading the scatter-gather list
// for a given pipe.
//
//===========================================================================

void CDspCommObject::SetAudioDuckListPhys
(
	WORD	wPipeIndex,			// Pipe index
	DWORD dwNewPhysAdr		// Physical address asserted on the PCI bus
)
{
	if (wPipeIndex < GetNumPipes() )
	{
		m_pDspCommPage->dwDuckListPhys[ wPipeIndex ].PhysAddr = 
																		SWAP( dwNewPhysAdr );
	}
}	// void CDspCommObject::SetAudioDuckListPhys



//===========================================================================
//
// Get a mask with active pipes
//
//===========================================================================

void CDspCommObject::GetActivePipes
(
	PCChannelMask	pChannelMask
)
{
	pChannelMask->Clear();
	*pChannelMask += m_cmActive;
}	// void CDspCommObject::GetActivePipes()


//===========================================================================
//
//	Set the audio format for a pipe
//
//===========================================================================

ECHOSTATUS CDspCommObject::SetAudioFormat
(
	WORD 							wPipeIndex,
	PECHOGALS_AUDIOFORMAT	pFormat,
	BOOL							fDitherDigitalInputs
)
{
	WORD wDspFormat = DSP_AUDIOFORM_SS_16LE;

	//
	// Check the pipe number
	//
	if (wPipeIndex >= GetNumPipes() )
	{
		ECHO_DEBUGPRINTF( ("CDspCommObject::SetAudioFormat: Invalid pipe"
								 "%d\n",
								 wPipeIndex) );
		return ECHOSTATUS_INVALID_CHANNEL;
	}

	//
	// Look for super-interleave
	//
	if (pFormat->wDataInterleave > 2)
	{
		wDspFormat = DSP_AUDIOFORM_SUPER_INTERLEAVE_32LE
						 | pFormat->wDataInterleave;
	}
	else
	{
		//
		// For big-endian data, only 32 bit mono->mono samples and 32 bit stereo->stereo
		// are supported
		//
		if (pFormat->byDataAreBigEndian)
		{
			
			switch ( pFormat->wDataInterleave )
			{
				case 1 :
					wDspFormat = DSP_AUDIOFORM_MM_32BE;
					break;
					
#ifdef STEREO_BIG_ENDIAN32_SUPPORT
				case 2 :
					wDspFormat = DSP_AUDIOFORM_SS_32BE;
					break;
#endif

			}
		}
		else
		{
			//
			// Check for 32 bit little-endian mono->mono case
			//
			if ( 	(1 == pFormat->wDataInterleave) &&
					(32 == pFormat->wBitsPerSample) &&
					(0 == pFormat->byMonoToStereo) )
			{
				wDspFormat = DSP_AUDIOFORM_MM_32LE;
			}
			else
			{
				//
				// Handle the other little-endian formats
				//
				switch (pFormat->wBitsPerSample)
				{
					case 8 :
						if (2 == pFormat->wDataInterleave)
							wDspFormat = DSP_AUDIOFORM_SS_8;
						else
							wDspFormat = DSP_AUDIOFORM_MS_8;

						break;
				
					default :		
					case 16 :
					
						//
						// If this is a digital input and 
						// the no dither flag is set, use the no-dither format
						//
						if (	fDitherDigitalInputs &&
								(wPipeIndex >= (m_wNumPipesOut + m_wFirstDigitalBusIn)) )
								
						{
							//
							// 16 bit, no dither
							//
							if (2 == pFormat->wDataInterleave)
								wDspFormat = DSP_AUDIOFORM_SS_16LE_ND;
							else
								wDspFormat = DSP_AUDIOFORM_MS_16LE_ND;
						}
						else
						{
							//
							// 16 bit with dither
							//
							if (2 == pFormat->wDataInterleave)
								wDspFormat = DSP_AUDIOFORM_SS_16LE;
							else
								wDspFormat = DSP_AUDIOFORM_MS_16LE;
						}
						
						break;	
					
					case 32 :
						if (2 == pFormat->wDataInterleave)
							wDspFormat = DSP_AUDIOFORM_SS_32LE;
						else
							wDspFormat = DSP_AUDIOFORM_MS_32LE;
						break;					
				}
				
			} // check other little-endian formats
		
		} // not big endian data
		
	} // not super-interleave
	
	m_pDspCommPage->wAudioFormat[wPipeIndex] = SWAP( wDspFormat );	
	
	return ECHOSTATUS_OK;

}	// ECHOSTATUS CDspCommObject::SetAudioFormat


//===========================================================================
//
//	Get the audio format for a pipe
//
//===========================================================================

ECHOSTATUS CDspCommObject::GetAudioFormat
( 
	WORD 							wPipeIndex,
	PECHOGALS_AUDIOFORMAT	pFormat
)
{
	if (wPipeIndex >= GetNumPipes() )
	{
		ECHO_DEBUGPRINTF( ("CDspCommObject::GetAudioFormat: Invalid pipe %d\n",
								 wPipeIndex) );

		return ECHOSTATUS_INVALID_CHANNEL;
	}

	pFormat->byDataAreBigEndian = 0;	// true for most of the formats
	pFormat->byMonoToStereo = 0;
	
	switch (SWAP(m_pDspCommPage->wAudioFormat[wPipeIndex]))
	{
		case DSP_AUDIOFORM_MS_8 :
			pFormat->wDataInterleave = 1;
			pFormat->wBitsPerSample = 8;
			pFormat->byMonoToStereo = 1;
			break;
			
		case DSP_AUDIOFORM_MS_16LE_ND :
		case DSP_AUDIOFORM_MS_16LE :
			pFormat->wDataInterleave = 1;
			pFormat->wBitsPerSample = 16;
			pFormat->byMonoToStereo = 1;			
			break;
			
		case DSP_AUDIOFORM_SS_8 :
			pFormat->wDataInterleave = 2;
			pFormat->wBitsPerSample = 8;
			break;

		case DSP_AUDIOFORM_SS_16LE_ND :
		case DSP_AUDIOFORM_SS_16LE :
			pFormat->wDataInterleave = 2;
			pFormat->wBitsPerSample = 16;
			break;
		
		case DSP_AUDIOFORM_SS_32LE :
			pFormat->wDataInterleave = 2;
			pFormat->wBitsPerSample = 32;
			break;			
		
		case DSP_AUDIOFORM_MS_32LE :
			pFormat->byMonoToStereo = 1;			
			// fall through

		case DSP_AUDIOFORM_MM_32LE :
			pFormat->wDataInterleave = 1;
			pFormat->wBitsPerSample = 32;
			break;			
			
		case DSP_AUDIOFORM_MM_32BE :
			pFormat->wDataInterleave = 1;
			pFormat->wBitsPerSample = 32;
			pFormat->byDataAreBigEndian = 1;
			break;			
			
		case DSP_AUDIOFORM_SS_32BE :
			pFormat->wDataInterleave = 2;
			pFormat->wBitsPerSample = 32;
			pFormat->byDataAreBigEndian = 1;
			break;			
		
	}
	
	return ECHOSTATUS_OK;
	
}	// void CDspCommObject::GetAudioFormat


/****************************************************************************

	Mixer methods

 ****************************************************************************/

//===========================================================================
//
// SetPipeOutGain - set the gain for a single output pipe
//
//===========================================================================

ECHOSTATUS CDspCommObject::SetPipeOutGain
( 
	WORD 	wPipeOut, 
	WORD	wBusOut,
	int 	iGain,
	BOOL 	fImmediate
)
{
	if ( wPipeOut < m_wNumPipesOut )
	{
		//
		// Wait for the handshake
		//
		if ( !WaitForHandshake() )
			return ECHOSTATUS_DSP_DEAD;
			
		//
		// Save the new value
		//
		iGain = GENERIC_TO_DSP(iGain);
		m_pDspCommPage->OutLineLevel[ wPipeOut ] = (BYTE) iGain;

		ECHO_DEBUGPRINTF( ("CDspCommObject::SetPipeOutGain: Out pipe %d "
								 "= 0x%x\n",
								 wPipeOut,
								 iGain) );

		//
		// If fImmediate is true, then do the gain setting right now.
		// If you want to do a batch of gain settings all at once, it's
		// more efficient to call this several times and then only set
		// fImmediate for the last one; then the DSP picks up all of
		// them at once.
		//								 
		if (fImmediate)
		{
			return UpdateAudioOutLineLevel();
		}

		return ECHOSTATUS_OK;		

	}

	ECHO_DEBUGPRINTF( ("CDspCommObject::SetPipeOutGain: Invalid out pipe "
							 "%d\n",
							 wPipeOut) );
	ECHO_DEBUGBREAK();	 
							 
	return ECHOSTATUS_INVALID_CHANNEL;
	
}	// SetPipeOutGain


//===========================================================================
//
// GetPipeOutGain returns the current gain for an output pipe.  This isn't
// really used as the mixer code in CEchoGals stores logical values for
// these, but it's here for completeness.
//
//===========================================================================

ECHOSTATUS CDspCommObject::GetPipeOutGain
( 
	WORD wPipeOut, 
	WORD wBusOut,
	int &iGain
)
{
	if (wPipeOut < m_wNumPipesOut)
	{
		iGain = (int) (char) m_pDspCommPage->OutLineLevel[ wPipeOut ];
		iGain = DSP_TO_GENERIC(8);
		return ECHOSTATUS_OK;		
	}

	ECHO_DEBUGPRINTF( ("CDspCommObject::GetPipeOutGain: Invalid out pipe "
							 "%d\n",
							 wPipeOut) );
							 
	return ECHOSTATUS_INVALID_CHANNEL;
	
}	// GetPipeOutGain

	

//===========================================================================
//
// Set input bus gain - iGain is in units of 0.5 dB
//
//===========================================================================

ECHOSTATUS CDspCommObject::SetBusInGain( WORD wBusIn, int iGain)
{
	if (wBusIn > m_wNumBussesIn)
		return ECHOSTATUS_INVALID_CHANNEL;

	//
	// Wait for the handshake (OK even if ASIC is not loaded)
	//
	if ( !WaitForHandshake() )
		return ECHOSTATUS_DSP_DEAD;
		
	//
	// Adjust the gain value
	//		
	iGain += GL20_INPUT_GAIN_MAGIC_NUMBER;
	
	//
	// Put it in the comm page
	//
	m_pDspCommPage->InLineLevel[wBusIn] = (BYTE) iGain;

	return UpdateAudioInLineLevel();
}	


//===========================================================================
//
// Get the input bus gain in units of 0.5 dB
//
//===========================================================================

ECHOSTATUS CDspCommObject::GetBusInGain( WORD wBusIn, int &iGain)
{
	if (wBusIn > m_wNumBussesIn)
		return ECHOSTATUS_INVALID_CHANNEL;
		
	iGain = m_pDspCommPage->InLineLevel[wBusIn];
	iGain -= GL20_INPUT_GAIN_MAGIC_NUMBER;

	return ECHOSTATUS_OK;
}	


//===========================================================================
//
//	Set the nominal level for an input or output bus
//
// bState TRUE			-10 nominal level
// bState FALSE		+4 nominal level
//
//===========================================================================

ECHOSTATUS CDspCommObject::SetNominalLevel
(
	WORD	wBus,
	BOOL	bState
)
{
	//
	// Check the pipe index
	//
	if (wBus < (m_wNumBussesOut + m_wNumBussesIn))
	{
		//
		// Wait for the handshake (OK even if ASIC is not loaded)
		//
		if ( !WaitForHandshake() )
			return ECHOSTATUS_DSP_DEAD;

		//
		// Set the nominal bit
		//		
		if ( bState )
			m_pDspCommPage->cmdNominalLevel.SetIndexInMask( wBus );
		else
			m_pDspCommPage->cmdNominalLevel.ClearIndexInMask( wBus );

		return UpdateAudioOutLineLevel();
	}

	ECHO_DEBUGPRINTF( ("CDspCommObject::SetNominalOutLineLevel Invalid "
							 "index %d\n",
							 wBus ) );
	return ECHOSTATUS_INVALID_CHANNEL;

}	// ECHOSTATUS CDspCommObject::SetNominalLevel


//===========================================================================
//
//	Get the nominal level for an input or output bus
//
// bState TRUE			-10 nominal level
// bState FALSE		+4 nominal level
//
//===========================================================================

ECHOSTATUS CDspCommObject::GetNominalLevel
(
	WORD	wBus,
	PBYTE pbyState
)
{

	if (wBus < (m_wNumBussesOut + m_wNumBussesIn))
	{
		*pbyState = (BYTE)
			m_pDspCommPage->cmdNominalLevel.TestIndexInMask( wBus );
		return ECHOSTATUS_OK;
	}

	ECHO_DEBUGPRINTF( ("CDspCommObject::GetNominalLevel Invalid "
							 "index %d\n",
							 wBus ) );
	return ECHOSTATUS_INVALID_CHANNEL;
}	// ECHOSTATUS CDspCommObject::GetNominalLevel


//===========================================================================
//
//	Set the monitor level from an input bus to an output bus.
//
//===========================================================================

ECHOSTATUS CDspCommObject::SetAudioMonitor
(
	WORD	wBusOut,	// output bus
	WORD	wBusIn,	// input bus
	int	iGain,
	BOOL 	fImmediate
)
{
	/*
	ECHO_DEBUGPRINTF( ("CDspCommObject::SetAudioMonitor: "
							 "Out %d in %d Gain %d (0x%x)\n",
							 wBusOut, wBusIn, iGain, iGain) );
	*/

	//
	// The monitor array is a one-dimensional array;
	// compute the offset into the array
	//
	WORD	wOffset = ComputeAudioMonitorIndex( wBusOut, wBusIn );

	//
	// Wait for the offset
	//	
	if ( !WaitForHandshake() )
		return ECHOSTATUS_DSP_DEAD;

	//
	// Write the gain value to the comm page
	//
	iGain = GENERIC_TO_DSP(iGain);
	m_pDspCommPage->byMonitors[ wOffset ] = (BYTE) (iGain);
	
	//
	// If fImmediate is set, do the command right now
	//
	if (fImmediate)
	{
		return  UpdateAudioOutLineLevel();
	}
	
	return ECHOSTATUS_OK;

}	// ECHOSTATUS CDspCommObject::SetAudioMonitor


//===========================================================================
//
// SetMetersOn turns the meters on or off.  If meters are turned on, the
// DSP will write the meter and clock detect values to the comm page
// at about 30 Hz.
//
//===========================================================================

ECHOSTATUS CDspCommObject::SetMetersOn
(
	BOOL bOn
)
{
	if ( bOn )
	{
		if ( 0 == m_wMeterOnCount )
		{
			SendVector( DSP_VC_METERS_ON );
		}
		m_wMeterOnCount++;
	}
	else
	{
		int	iDevice;
	
		if ( m_wMeterOnCount == 0 )
			return ECHOSTATUS_OK;

		if ( 0 == --m_wMeterOnCount )
		{
			SendVector( DSP_VC_METERS_OFF );
			
			for ( iDevice = 0; iDevice < DSP_MAXPIPES; iDevice++ )
			{
				m_pDspCommPage->VULevel[ iDevice ]   = GENERIC_TO_DSP(ECHOGAIN_MUTED);
				m_pDspCommPage->PeakMeter[ iDevice ] = GENERIC_TO_DSP(ECHOGAIN_MUTED);
			}
		}
	}
	return ECHOSTATUS_OK;
	
}	// ECHOSTATUS CDspCommObject::SetMetersOn


//===========================================================================
//
// Tell the DSP to read and update output, nominal & monitor levels 
//	in comm page.
//
//===========================================================================

ECHOSTATUS CDspCommObject::UpdateAudioOutLineLevel()
{
	ECHO_DEBUGPRINTF( ( "CDspCommObject::UpdateAudioOutLineLevel:\n" ) );
	
	if (FALSE == m_bASICLoaded)
		return ECHOSTATUS_ASIC_NOT_LOADED;
	
	ClearHandshake();
	return( SendVector( DSP_VC_UPDATE_OUTVOL ) );
}	// ECHOSTATUS CDspCommObject::UpdateAudioOutLineLevel()


//===========================================================================
//
// Tell the DSP to read and update input levels in comm page
//
//===========================================================================

ECHOSTATUS CDspCommObject::UpdateAudioInLineLevel()
{
	//ECHO_DEBUGPRINTF( ( "CDspCommObject::UpdateAudioInLineLevel:\n" ) );
	
	if (FALSE == m_bASICLoaded)
		return ECHOSTATUS_ASIC_NOT_LOADED;

	ClearHandshake();
	return( SendVector( DSP_VC_UPDATE_INGAIN ) );
}		// ECHOSTATUS CDspCommObject::UpdateAudioInLineLevel()


//===========================================================================
//
// Tell the DSP to read and update virtual mixer levels 
//	in comm page.  This method is overridden by cards that actually 
// support a vmixer.
//
//===========================================================================

ECHOSTATUS CDspCommObject::UpdateVmixerLevel()
{
	ECHO_DEBUGPRINTF(("CDspCommObject::UpdateVmixerLevel\n"));
	return ECHOSTATUS_NOT_SUPPORTED;
}	// ECHOSTATUS CDspCommObject::UpdateVmixerLevel()


//===========================================================================
//
// Tell the DSP to change the input clock
//
//===========================================================================

ECHOSTATUS CDspCommObject::SetInputClock(WORD wClock)
{
	//
	// Wait for the last command
	//
	if (!WaitForHandshake())
		return ECHOSTATUS_DSP_DEAD;

	ECHO_DEBUGPRINTF( ("CDspCommObject::SetInputClock:\n") );		
		
	//
	// Write to the comm page
	//
	m_pDspCommPage->wInputClock = SWAP(wClock);
	
	//
	// Clear the handshake and send the command
	//
	ClearHandshake();
	ECHOSTATUS Status = SendVector(DSP_VC_UPDATE_CLOCKS);
	
	return Status;

}	// ECHOSTATUS CDspCommObject::SetInputClock


//===========================================================================
//
// Tell the DSP to change the output clock - Layla20 only
//
//===========================================================================

ECHOSTATUS CDspCommObject::SetOutputClock(WORD wClock)
{

	return ECHOSTATUS_CLOCK_NOT_SUPPORTED;
	
}	// ECHOSTATUS CDspCommObject::SetOutputClock


//===========================================================================
//
// Fill out an ECHOGALS_METERS struct using the current values in the 
// comm page.  This method is overridden for vmixer cards.
//
//===========================================================================

ECHOSTATUS CDspCommObject::GetAudioMeters
(
	PECHOGALS_METERS	pMeters
)
{
	pMeters->iNumPipesOut = 0;
	pMeters->iNumPipesIn = 0;

	//
	//	Output 
	// 
	DWORD dwCh = 0;
	WORD 	i;

	pMeters->iNumBussesOut = (int) m_wNumBussesOut;
	for (i = 0; i < m_wNumBussesOut; i++)
	{
		pMeters->iBusOutVU[i] = 
			DSP_TO_GENERIC( ((int) (char) m_pDspCommPage->VULevel[ dwCh ]) );

		pMeters->iBusOutPeak[i] = 
			DSP_TO_GENERIC( ((int) (char) m_pDspCommPage->PeakMeter[ dwCh ]) );
		
		dwCh++;
	}

	pMeters->iNumBussesIn = (int) m_wNumBussesIn;	
	for (i = 0; i < m_wNumBussesIn; i++)
	{
		pMeters->iBusInVU[i] = 
			DSP_TO_GENERIC( ((int) (char) m_pDspCommPage->VULevel[ dwCh ]) );
		pMeters->iBusInPeak[i] = 
			DSP_TO_GENERIC( ((int) (char) m_pDspCommPage->PeakMeter[ dwCh ]) );
		
		dwCh++;
	}
	
	return ECHOSTATUS_OK;
	
} // GetAudioMeters


#ifdef DIGITAL_INPUT_AUTO_MUTE_SUPPORT

//===========================================================================
//
// Digital input auto-mute - Gina24, Layla24, and Mona only
//
//===========================================================================

ECHOSTATUS CDspCommObject::GetDigitalInputAutoMute(BOOL &fAutoMute)
{
	fAutoMute = m_fDigitalInAutoMute;	
	
	ECHO_DEBUGPRINTF(("CDspCommObject::GetDigitalInputAutoMute %d\n",fAutoMute));
	
	return ECHOSTATUS_OK;
}

ECHOSTATUS CDspCommObject::SetDigitalInputAutoMute(BOOL fAutoMute)
{
	ECHO_DEBUGPRINTF(("CDspCommObject::SetDigitalInputAutoMute %d\n",fAutoMute));
	
	//
	// Store the flag
	//
	m_fDigitalInAutoMute = fAutoMute;
	
	//
	// Re-set the input clock to the current value - indirectly causes the 
	// auto-mute flag to be sent to the DSP
	//
	SetInputClock(m_wInputClock);
	
	return ECHOSTATUS_OK;
}

#endif // DIGITAL_INPUT_AUTO_MUTE_SUPPORT




/****************************************************************************

	Power management

 ****************************************************************************/

//===========================================================================
//
// Tell the DSP to go into low-power mode
//
//===========================================================================

ECHOSTATUS CDspCommObject::GoComatose()
{
	ECHO_DEBUGPRINTF(("CDspCommObject::GoComatose\n"));

	if (NULL != m_pwDspCode)
	{
		//
		// Make LoadFirmware do a complete reload
		//	
		m_pwDspCode = NULL;
		
		//
		// Put the DSP to sleep
		//
		return SendVector(DSP_VC_GO_COMATOSE);
	}
	
	return ECHOSTATUS_OK;

}	// end of GoComatose



#ifdef MIDI_SUPPORT

/****************************************************************************

	MIDI

 ****************************************************************************/

//===========================================================================
//
// Send a buffer full of MIDI data to the DSP
//
//===========================================================================

ECHOSTATUS CDspCommObject::WriteMidi
(
	PBYTE		pData,						// Ptr to data buffer
	DWORD		dwLength,					// How many bytes to write
	PDWORD	pdwActualCt					// Return how many actually written
)
{
	ECHOSTATUS	Status;
	DWORD			dwLengthTemp, dwShift, dwPacked, i;
#ifdef ECHO_DEBUG
	DWORD			dwOrgLgth = dwLength;
#endif

	//
	// Send all the MIDI data
	//
	*pdwActualCt = 0;
	Status = ECHOSTATUS_OK;
		
	while ( dwLength > 0 )
	{
		//
		// Wait for the last command to complete
		//
		if (FALSE == WaitForHandshake())
		{
			ECHO_DEBUGPRINTF(("CDspCommObject::WriteMidi - Timed out waiting for handshake\n"));
			
			Status = ECHOSTATUS_DSP_DEAD;
			break;
		}
	
		//
		// See if the DSP is ready to go
		//
		if (0 == (GetDspRegister( CHI32_STATUS_REG) & CHI32_STATUS_REG_HF4))
		{
			//
			// MIDI output is full
			//
			break;
		}
	
		//
		// Pack the MIDI data three bytes at a time
		//
		if ( dwLength > 3 )
			dwLengthTemp = 3;
		else
			dwLengthTemp = dwLength;
		
		dwShift = 16;
		dwPacked = 0;
		for ( i = 0; i < dwLengthTemp; i++ )
		{
			dwPacked |= (DWORD) *pData++ << dwShift;
			dwShift -= 8;
		}

		dwPacked |= dwLengthTemp << 24;		
		dwLength -= dwLengthTemp;

		//
		// Write the data to the DSP
		//
		m_pDspCommPage->dwMIDIOutData = SWAP( dwPacked );
		ClearHandshake();
		SendVector( DSP_VC_MIDI_WRITE );
		
		ECHO_DEBUGPRINTF(("CDspCommObject::WriteMidi - wrote 0x%08lx\n",dwPacked));

		//
		// Update the count
		//
		*pdwActualCt += dwLengthTemp;
	
	}			// while( dwLength )

	if ( 0 != *pdwActualCt )
	{
		//
		// Save the current time - used to detect if MIDI out is currently busy
		//
		m_pOsSupport->OsGetSystemTime( &m_ullMidiOutTime );
															// Last time MIDI out occured
	}

	ECHO_DEBUGPRINTF( ("CDspCommObject::WriteMidi: Actual %ld "
							 "Expected %ld OK\n",
							 *pdwActualCt,
							 dwOrgLgth) );
							 
	return Status;
	
}		// ECHOSTATUS CDspCommObject::WriteMidi


//===========================================================================
//
// Called from the interrupt handler - get a MIDI input byte
//
//===========================================================================

ECHOSTATUS CDspCommObject::ReadMidi
(
	WORD 		wIndex,				// Buffer index
	DWORD &	dwData				// Return data
)
{
	if ( wIndex >= DSP_MIDI_BUFFER_SIZE - 1 )
		return ECHOSTATUS_INVALID_INDEX;

	//
	// Get the data
	//	
	dwData = SWAP( m_pDspCommPage->wMidiInData[ wIndex ] );

	//
	// Timestamp for the MIDI input activity indicator
	//
	m_pOsSupport->OsGetSystemTime( &m_ullMidiInTime );

	return ECHOSTATUS_OK;
	
}	// ECHOSTATUS CDspCommObject::ReadMidi


ECHOSTATUS CDspCommObject::SetMidiOn( BOOL bOn )
{
	if ( bOn )
	{
		if ( 0 == m_wMidiOnCount )
		{
			if ( !WaitForHandshake() )
				return ECHOSTATUS_DSP_DEAD;

			m_pDspCommPage->dwFlags |= SWAP( (DWORD) DSP_FLAG_MIDI_INPUT );
			
			ClearHandshake();
			SendVector( DSP_VC_UPDATE_FLAGS );
		}
		m_wMidiOnCount++;
	}
	else
	{
		if ( m_wMidiOnCount == 0 )
			return ECHOSTATUS_OK;

		if ( 0 == --m_wMidiOnCount )
		{
			if ( !WaitForHandshake() )
				return ECHOSTATUS_DSP_DEAD;
				
			m_pDspCommPage->dwFlags &= SWAP( (DWORD) ~DSP_FLAG_MIDI_INPUT );
			
			ClearHandshake();
			SendVector( DSP_VC_UPDATE_FLAGS );
		}
	}

	return ECHOSTATUS_OK;

}	// ECHOSTATUS CDspCommObject::SetMidiOn


#endif // MIDI_SUPPORT



// **** CDspCommObject.cpp ****
