// ****************************************************************************
//
//		CChannelMask.h
//
//		Include file for interfacing with the CChannelMask and CChMaskDsp
//		classes.
//		Set editor tabs to 3 for your viewing pleasure.
//
// 	CChannelMask is a handy way to specify a group of pipes simultaneously.
//		It should really be called "CPipeMask", but the class name predates
//		the term "pipe".
//	
//		CChMaskDsp is used in the comm page to specify a group of channels
//		at once; these are read by the DSP and must therefore be kept
//		in little-endian format.
//
//---------------------------------------------------------------------------
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

//	Prevent problems with multiple includes
#ifndef _CHMASKOBJECT_
#define _CHMASKOBJECT_

//
//	Defines
//
typedef unsigned long	CH_MASK;

#define	CH_MASK_SZ		(2)				// Max channel mask array size
#define	CH_MASK_BITS	(sizeof( CH_MASK ) * 8)
													// Max bits per mask entry

#define	ECHO_INVALID_CHANNEL	((WORD)(-1))
													// Marks unused channel #

typedef unsigned short		CH_MASK_DSP;
#define	CH_MASK_DSP_BITS	(sizeof( CH_MASK_DSP ) * 8)
													// Max bits per mask entry

/****************************************************************************

	CChannelMask

 ****************************************************************************/

class CChannelMask
{
protected:

#ifdef ECHO_OS9
	friend class CInOutChannelMask;
#endif

	CH_MASK	m_MaskRegs[ CH_MASK_SZ ];	// One bit per output or input channel

public:

	CChannelMask();
	~CChannelMask() {}

	// Returns TRUE if no bits set
	BOOL IsEmpty();

	// Set the wPipeIndex bit in the mask
	void SetIndexInMask( WORD wPipeIndex );

	// Clear the wPipeIndex bit in the mask
	void ClearIndexInMask( WORD wPipeIndex );

	// Return the next bit set starting with wStartPipeIndex as an index.
	//	If nothing set, returns ECHO_INVALID_CHANNEL.
	//	Use this interface for enumerating thru a channel mask.
	WORD GetIndexFromMask( WORD wStartPipeIndex );
	
	// Test driver channel index in mask.
	//	Return TRUE if set	
	BOOL TestIndexInMask( WORD wPipeIndex );

	// Clear all bits in the mask
	void Clear();

	// Clear bits in this mask that are in SrcMask
	void ClearMask( CChannelMask SrcMask );

	//	Return TRUE if any bits in source mask are set in this mask
	BOOL Test( CChannelMask * pSrcMask );

	//
	//	Return TRUE if the Test Mask contains all of the channels
	//	enabled in this instance.
	//	Use to be sure all channels in this instance exist in
	//	another instance.
	//
	BOOL IsSubsetOf( CChannelMask& TstMask );

	//
	//	Return TRUE if the Test Mask contains at least one of the channels
	//	enabled in this instance.
	//	Use to find out if any channels in this instance exist in
	//	another instance.
	//
	BOOL IsIntersectionOf( CChannelMask& TstMask );

	//
	//	Overload new & delete so memory for this object is allocated
	//	from non-paged memory.
	//
	PVOID operator new( size_t Size );
	VOID  operator delete( PVOID pVoid );

	//
	//	Macintosh compiler likes "class" after friend, PC doesn't care
	//
	friend class CChMaskDsp;

	inline CH_MASK operator []  ( int iNdx )
		{ return SWAP( m_MaskRegs[ iNdx ] ); }

	//	Return TRUE if source mask equals this mask
	friend BOOLEAN operator == ( CONST CChannelMask &LVal,
										  CONST CChannelMask &RVal );

	// Copy mask bits in source to this mask
	CChannelMask& operator = (CONST CChannelMask & RVal);

	// Copy mask bits in source to this mask
	CChannelMask& operator = (CONST CChMaskDsp & RVal);

	// Add mask bits in source to this mask
	VOID operator += (CONST CChannelMask & RVal);

	// Subtract mask bits in source to this mask
	VOID operator -= (CONST CChannelMask & RVal);

	// AND mask bits in source to this mask
	VOID operator &= (CONST CChannelMask & RVal);

	// OR mask bits in source to this mask
	VOID operator |= (CONST CChannelMask & RVal);

protected :

	//
	//	Store an output bit mask and an input bitmask.
	//	We assume here that the # of outputs fits in one mask reg
	//
	void SetMask( CH_MASK OutMask, CH_MASK InMask, int nOutputs );
	void SetOutMask( CH_MASK OutMask, int nOutputs );
	void SetInMask( CH_MASK InMask, int nOutputs );

	//
	//	Retrieve an output bit mask and an input bitmask.
	//	We assume here that the # of outputs fits in one mask reg
	//
	void GetMask( CH_MASK & OutMask, CH_MASK & InMask, int nOutputs );
	CH_MASK GetOutMask( int nOutputs );
	CH_MASK GetInMask( int nOutputs );

};	// class CChannelMask

typedef	CChannelMask *		PCChannelMask;


/****************************************************************************

	CChMaskDsp

 ****************************************************************************/

class CChMaskDsp
{
protected:

	CH_MASK_DSP	m_MaskRegs[ CH_MASK_SZ ];	// One bit per output or input channel

public:

	CChMaskDsp();
	~CChMaskDsp() {}

	// Returns TRUE if no bits set
	BOOL IsEmpty();

	// Set the wPipeIndex bit in the mask
	void SetIndexInMask( WORD wPipeIndex );

	// Clear the wPipeIndex bit in the mask
	void ClearIndexInMask( WORD wPipeIndex );
	
	// Test pipe index in mask.
	//	Return TRUE if set	
	BOOL TestIndexInMask( WORD wPipeIndex );

	// Clear all bits in the mask
	void Clear();

	//
	//	Overload new & delete so memory for this object is allocated
	//	from non-paged memory.
	//
	PVOID operator new( size_t Size );
	VOID  operator delete( PVOID pVoid );

	//
	//	Macintosh compiler likes "class" after friend, PC doesn't care
	//
	friend class CChannelMask;

	inline CH_MASK_DSP operator []  ( int iNdx )
		{ return SWAP( m_MaskRegs[ iNdx ] ); }

	// Copy mask bits in source to this mask
	CChMaskDsp& operator = (CONST CChannelMask & RVal);

	// Add mask bits in source to this mask
	VOID operator += (CONST CChannelMask & RVal);

	// Subtract mask bits in source to this mask
	VOID operator -= (CONST CChannelMask & RVal);

protected :

};	// class CChMaskDsp

typedef	CChMaskDsp *		PCChMaskDsp;

#endif // _CHMASKOBJECT_

//	CChannelMask.h
