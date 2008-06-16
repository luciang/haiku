/****************************************************************************
** libebml : parse EBML files, see http://embl.sourceforge.net/
**
** <file/class description>
**
** Copyright (C) 2002-2005 Steve Lhomme.  All rights reserved.
**
** This file is part of libebml.
**
** This library is free software; you can redistribute it and/or
** modify it under the terms of the GNU Lesser General Public
** License as published by the Free Software Foundation; either
** version 2.1 of the License, or (at your option) any later version.
** 
** This library is distributed in the hope that it will be useful,
** but WITHOUT ANY WARRANTY; without even the implied warranty of
** MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
** Lesser General Public License for more details.
** 
** You should have received a copy of the GNU Lesser General Public
** License along with this library; if not, write to the Free Software
** Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
**
** See http://www.matroska.org/license/lgpl/ for LGPL licensing information.
**
** Contact license@matroska.org if any conditions of this licensing are
** not clear to you.
**
**********************************************************************/

/*!
	\file
	\version \$Id: EbmlUInteger.h 1079 2005-03-03 13:18:14Z robux4 $
	\author Steve Lhomme     <robux4 @ users.sf.net>
	\author Julien Coloos    <suiryc @ users.sf.net>
	\author Moritz Bunkus    <moritz @ bunkus.org>
*/
#ifndef LIBEBML_UINTEGER_H
#define LIBEBML_UINTEGER_H

#include "EbmlTypes.h"
#include "EbmlElement.h"

START_LIBEBML_NAMESPACE

const int DEFAULT_UINT_SIZE = 0; ///< optimal size stored

/*!
    \class EbmlUInteger
    \brief Handle all operations on an unsigned integer EBML element
*/
class EBML_DLL_API EbmlUInteger : public EbmlElement {
	public:
		EbmlUInteger();
		EbmlUInteger(const uint64 DefaultValue);
		EbmlUInteger(const EbmlUInteger & ElementToClone);
	
		EbmlUInteger & operator=(const uint64 NewValue) {Value = NewValue; bValueIsSet = true; return *this;}

		/*!
			Set the default size of the integer (usually 1,2,4 or 8)
		*/
		void SetDefaultSize(const int nDefaultSize = DEFAULT_UINT_SIZE) {Size = nDefaultSize;}

		bool ValidateSize() const {return (Size <= 8);}
		uint32 RenderData(IOCallback & output, bool bForceRender, bool bKeepIntact = false);
		uint64 ReadData(IOCallback & input, ScopeMode ReadFully = SCOPE_ALL_DATA);
		uint64 UpdateSize(bool bKeepIntact = false, bool bForceRender = false);
		
		bool operator<(const EbmlUInteger & EltCmp) const {return Value < EltCmp.Value;}
		
		operator uint8()  const {return uint8(Value); }
		operator uint16() const {return uint16(Value);}
		operator uint32() const {return uint32(Value);}
		operator uint64() const {return Value;}

		void SetDefaultValue(uint64 aValue) {assert(!DefaultIsSet); DefaultValue = aValue; DefaultIsSet = true;}
    
		const uint64 DefaultVal() const {assert(DefaultIsSet); return DefaultValue;}

		bool IsDefaultValue() const {
			return (DefaultISset() && Value == DefaultValue);
		}

	protected:
		uint64 Value; /// The actual value of the element
		uint64 DefaultValue;
};

END_LIBEBML_NAMESPACE

#endif // LIBEBML_UINTEGER_H
