//------------------------------------------------------------------------------
//	Copyright (c) 2001-2002, OpenBeOS
//
//	Permission is hereby granted, free of charge, to any person obtaining a
//	copy of this software and associated documentation files (the "Software"),
//	to deal in the Software without restriction, including without limitation
//	the rights to use, copy, modify, merge, publish, distribute, sublicense,
//	and/or sell copies of the Software, and to permit persons to whom the
//	Software is furnished to do so, subject to the following conditions:
//
//	The above copyright notice and this permission notice shall be included in
//	all copies or substantial portions of the Software.
//
//	THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
//	IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
//	FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
//	AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
//	LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
//	FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
//	DEALINGS IN THE SOFTWARE.
//
//	File Name:		TPicture.h
//	Author:			Marc Flerackers (mflerackers@androme.be)
//	Description:	TPicture is used to create and play picture data.
//------------------------------------------------------------------------------

#ifndef	_TPICTURE_H
#define	_TPICTURE_H

// Standard Includes -----------------------------------------------------------

// System Includes -------------------------------------------------------------
#include <GraphicsDefs.h>
#include <Point.h>
#include <Rect.h>
#include <DataIO.h>

// Project Includes ------------------------------------------------------------

// Local Includes --------------------------------------------------------------

// Local Defines ---------------------------------------------------------------

// Globals ---------------------------------------------------------------------

// TPicture class --------------------------------------------------------------
class TPicture {
public:
					TPicture();
					TPicture(void *data, int32 size, BList &pictures);
virtual				~TPicture();

		int16		GetOp();
		bool		GetBool();
		int16		GetInt8();
		int16		GetInt16();
		int32		GetInt32();
		int64		GetInt64();
		float		GetFloat();
		BPoint		GetCoord();
		BRect		GetRect();
		rgb_color	GetColor();
		//void		GetString(char *);

		void		*GetData(int32);
		void		GetData(void *data, int32 size);

		void		AddInt8(int8);
		void		AddInt16(int16);
		void		AddInt32(int32);
		void		AddInt64(int64);
		void		AddFloat(float);
		void		AddCoord(BPoint);
		void		AddRect(BRect);
		void		AddColor(rgb_color);
		void		AddString(char *);

		void		AddData(void *data, int32 size);

		//			SwapOp();
		//			SwapInt8();
		//			SwapInt16();
		//			SwapInt32();
		//			SwapInt64();
		//			SwapFloat();
		//			SwapCoord();
		//			SwapRect();
		//			SwapIRect();
		//			SwapColor();
		//			SwapString();

		//			Swap();

		//			CheckPattern();

		void		BeginOp(int32);
		void		EndOp();

		void		EnterStateChange();
		void		ExitStateChange();

		void		EnterFontChange();
		void		ExitFontChange();

		status_t	Play(void **callBackTable, int32 tableEntries,
						void *userData);
		status_t	Rewind();

private:
		BMemoryIO	fData;
		int32		fSize;
		BList		&fPictures;
};
//------------------------------------------------------------------------------

//status_t do_playback(void *, long, BArray<BPicture *> &, void **, long, void *)

#endif // _TPICTURE_H
