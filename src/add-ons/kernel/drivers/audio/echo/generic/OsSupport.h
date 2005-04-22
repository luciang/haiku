// ****************************************************************************
//
//		OsSupport.H
//
//		Wrapper include file for OS-specific header files
//		Set editor tabs to 3 for your viewing pleasure.
//
// ----------------------------------------------------------------------------
//
// This file is part of Echo Digital Audio's generic driver library.
// Copyright Echo Digital Audio Corporation (c) 1998 - 2005
// All rights reserved
// www.echoaudio.com
//
// This library is free software; you can redistribute it and/or
// modify it under the terms of the GNU Lesser General Public
// License as published by the Free Software Foundation; either
// version 2.1 of the License, or (at your option) any later version.
//
// This library is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
// Lesser General Public License for more details.
//
// You should have received a copy of the GNU Lesser General Public
// License along with this library; if not, write to the Free Software
// Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
//
// ****************************************************************************

//	Prevent problems with multiple includes
#ifndef _ECHOOSSUPPORT_
#define _ECHOOSSUPPORT_

//===========================================================================
//
// BeOS
//
//===========================================================================

#ifdef ECHO_BEOS

#include "OsSupportBeOS.h"

#endif

//===========================================================================
//
// WDM driver
//
//===========================================================================

#ifdef ECHO_WDM

//
// WDM driver for Windows Me/2000/XP
//
#include "OsSupportWDM.h"

#endif


//===========================================================================
//
// MacOS 8 and 9
//
//===========================================================================

#ifdef ECHO_OS9

#include "OsSupportMac.h"

#endif


//===========================================================================
//
// Mac OS X
//
//===========================================================================

#ifdef ECHO_OSX

#include "OsSupportOsX.h"

#endif


#endif	// _ECHOOSSUPPORT_
