//
// Copyright 2005, Stephan Aßmus <superstippi@gmx.de>
// Distributed under the terms of the MIT License.
//
// Contains an abstract base class HWInterface that provides the
// basic functionality for frame buffer acces in the DisplayDriverPainter
// implementation.

#ifndef HW_INTERFACE_H
#define HW_INTERFACE_H

#include <Accelerant.h>
#include <GraphicsCard.h>
#include <OS.h>
#include <Region.h>

#include "MultiLocker.h"

class RenderingBuffer;
class RGBColor;
class ServerCursor;
class UpdateQueue;

enum {
	HW_ACC_COPY_REGION					= 0x00000001,
	HW_ACC_FILL_REGION					= 0x00000002,
	HW_ACC_INVERT_REGION				= 0x00000004,
};

class HWInterface : public MultiLocker {
 public:
								HWInterface(bool doubleBuffered = false);
	virtual						~HWInterface();

	// You need to WriteLock
	virtual	status_t			Initialize();
	virtual	status_t			Shutdown() = 0;

	// screen mode stuff
	virtual	status_t			SetMode(const display_mode &mode) = 0;
	virtual	void				GetMode(display_mode *mode) = 0;

	virtual status_t			GetDeviceInfo(accelerant_device_info *info) = 0;
	virtual status_t			GetModeList(display_mode **mode_list,
											uint32 *count) = 0;
	virtual status_t			GetPixelClockLimits(display_mode *mode,
													uint32 *low,
													uint32 *high) = 0;
	virtual status_t			GetTimingConstraints(display_timing_constraints *dtc) = 0;
	virtual status_t			ProposeMode(display_mode *candidate,
											const display_mode *low,
											const display_mode *high) = 0;

	virtual status_t			WaitForRetrace(bigtime_t timeout = B_INFINITE_TIMEOUT) = 0;

	virtual status_t			SetDPMSMode(const uint32 &state) = 0;
	virtual uint32				DPMSMode() = 0;
	virtual uint32				DPMSCapabilities() = 0;

	// query for available hardware accleration and perform it
	// (Initialize() must have been called already)
	virtual	uint32				AvailableHWAcceleration() const
									{ return 0; }

	virtual	void				CopyRegion(const clipping_rect* sortedRectList,
										   uint32 count,
										   int32 xOffset, int32 yOffset) {}
	virtual	void				FillRegion(/*const*/ BRegion& region,
										   const RGBColor& color) {}
	virtual	void				InvertRegion(/*const*/ BRegion& region) {}

	// cursor handling (these do their own Read/Write locking)
	virtual	void				SetCursor(ServerCursor* cursor);
	virtual	void				SetCursorVisible(bool visible);
			bool				IsCursorVisible();
	virtual	void				MoveCursorTo(const float& x,
											 const float& y);
			BPoint				GetCursorPosition();

	// frame buffer access (you need to ReadLock!)
			RenderingBuffer*	DrawingBuffer() const;
	virtual	RenderingBuffer*	FrontBuffer() const = 0;
	virtual	RenderingBuffer*	BackBuffer() const = 0;
	virtual	bool				IsDoubleBuffered() const;

	// Invalidate is planned to be used for scheduling an area for updating
	// you need to WriteLock!
	virtual	status_t			Invalidate(const BRect& frame);
	// while as CopyBackToFront() actually performs the operation
			status_t			CopyBackToFront(const BRect& frame);

	// TODO: Just a quick and primitive way to get single buffered mode working.
	// Later, the implementation should be smarter, right now, it will
	// draw the cursor for almost every drawing operation.
	// It seems to me BeOS hides the cursor (in laymans words) before
	// BView::Draw() is called (if the cursor is within that views clipping region),
	// then, after all drawing commands that triggered have been caried out,
	// it shows the cursor again. This approach would have the adventage of
	// the code not cluttering/slowing down DisplayDriverPainter.
	// For now, we hide the cursor for any drawing operation that has
	// a bounding box containing the cursor (in DisplayDriverPainter) so
	// the cursor hiding is completely transparent from code using DisplayDriverPainter.
	// ---
	// NOTE: Investigate locking for these! The client code should already hold a
	// ReadLock, but maybe these functions should acquire a WriteLock!
			void				HideSoftwareCursor(const BRect& area);
			void				HideSoftwareCursor();
			void				ShowSoftwareCursor();

 protected:
	// implement this in derived classes
	virtual	void				_DrawCursor(BRect area) const;

	// does the actual transfer and handles color space conversion
			void				_CopyToFront(uint8* src, uint32 srcBPR,
											 int32 x, int32 y,
											 int32 right, int32 bottom) const;

			BRect				_CursorFrame() const;
			void				_RestoreCursorArea(const BRect& frame) const;

			// If we draw the cursor somewhere in the drawing buffer,
			// we need to backup its contents before drawing, so that
			// we can restore that area when the cursor needs to be
			// drawn somewhere else.
			struct buffer_clip {
								buffer_clip(int32 width, int32 height)
								{
									bpr = width * 4;
									if (bpr > 0 && height > 0)
										buffer = new uint8[bpr * height];
									else
										buffer = NULL;
									left = 0;
									top = 0;
									right = -1;
									bottom = -1;
								}
								~buffer_clip()
								{
									delete[] buffer;
								}
				uint8*			buffer;
				int32			left;
				int32			top;
				int32			right;
				int32			bottom;
				int32			bpr;
			};

			buffer_clip*		fCursorAreaBackup;
			bool				fSoftwareCursorHidden;

			ServerCursor*		fCursor;
			bool				fCursorVisible;
			BPoint				fCursorLocation;
			bool				fDoubleBuffered;

 private:
			UpdateQueue*		fUpdateExecutor;
};

#endif // HW_INTERFACE_H
