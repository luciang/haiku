/*
 * Copyright (c) 2001-2008, Haiku, Inc.
 * Distributed under the terms of the MIT license.
 *
 * Authors:
 *		Adi Oanca <adioanca@myrealbox.com>
 *		Axel Dörfler, axeld@pinc-software.de
 *		Stephan Aßmus, <superstippi@gmx.de>
 */
#ifndef SCREEN_H
#define SCREEN_H


#include <Accelerant.h>
#include <GraphicsDefs.h>
#include <Point.h>


class DrawingEngine;
class HWInterface;

class Screen {
 public:
								Screen(::HWInterface *interface, int32 id);
								Screen();
	virtual						~Screen();

			status_t			Initialize();
			void				Shutdown();

			int32				ID() const { return fID; }
			status_t			GetMonitorInfo(monitor_info& info) const;

			status_t			SetMode(const display_mode& mode,
									bool makeDefault);
			status_t			SetMode(uint16 width, uint16 height,
									uint32 colorspace,
									const display_timing& timing,
									bool makeDefault);
			status_t			SetPreferredMode();
			status_t			SetBestMode(uint16 width, uint16 height,
									uint32 colorspace, float frequency,
									bool strict = true);

			void				GetMode(display_mode* mode) const;
			void				GetMode(uint16 &width, uint16 &height,
									uint32 &colorspace, float &frequency) const;
			BRect				Frame() const;
			color_space			ColorSpace() const;

			bool				IsDefaultMode() const { return fIsDefault; }

	inline	DrawingEngine*		GetDrawingEngine() const
									{ return fDriver; }
	inline	::HWInterface*		HWInterface() const
									{ return fHWInterface; }

 private:
			int32				_FindBestMode(const display_mode* modeList,
									uint32 count, uint16 width, uint16 height,
									uint32 colorspace, float frequency) const;

			int32				fID;
			DrawingEngine*		fDriver;
			::HWInterface*		fHWInterface;
			bool				fIsDefault;
};

#endif	/* SCREEN_H */
