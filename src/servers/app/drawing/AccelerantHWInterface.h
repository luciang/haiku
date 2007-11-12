/*
 * Copyright 2005-2006, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		Michael Lotz <mmlr@mlotz.ch>
 *		Stephan Aßmus <superstippi@gmx.de>
 */
#ifndef ACCELERANT_HW_INTERFACE_H
#define ACCELERANT_HW_INTERFACE_H


#include "HWInterface.h"

#include <image.h>
#include <video_overlay.h>

class MallocBuffer;
class AccelerantBuffer;


class AccelerantHWInterface : public HWInterface {
public:
								AccelerantHWInterface();
	virtual						~AccelerantHWInterface();

	virtual	status_t			Initialize();
	virtual	status_t			Shutdown();

	virtual	status_t			SetMode(const display_mode &mode);
	virtual	void				GetMode(display_mode *mode);

	virtual status_t			GetDeviceInfo(accelerant_device_info *info);
	virtual status_t			GetFrameBufferConfig(frame_buffer_config& config);

	virtual status_t			GetModeList(display_mode **mode_list,
									uint32 *count);
	virtual status_t			GetPixelClockLimits(display_mode *mode,
									uint32 *low, uint32 *high);
	virtual status_t			GetTimingConstraints(display_timing_constraints *dtc);
	virtual status_t			ProposeMode(display_mode *candidate,
									const display_mode *low,
									const display_mode *high);
	virtual	status_t			GetPreferredMode(display_mode* mode);
	virtual status_t			GetMonitorInfo(monitor_info* info);

	virtual sem_id				RetraceSemaphore();
	virtual status_t			WaitForRetrace(bigtime_t timeout = B_INFINITE_TIMEOUT);

	virtual status_t			SetDPMSMode(const uint32 &state);
	virtual uint32				DPMSMode();
	virtual uint32				DPMSCapabilities();

	virtual status_t			GetAccelerantPath(BString &path);
	virtual status_t			GetDriverPath(BString &path);

	// query for available hardware accleration and perform it
	virtual	uint32				AvailableHWAcceleration() const;

	// overlay support
	virtual overlay_token		AcquireOverlayChannel();
	virtual void				ReleaseOverlayChannel(overlay_token token);

	virtual status_t			GetOverlayRestrictions(const Overlay* overlay,
									overlay_restrictions* restrictions);
	virtual bool				CheckOverlayRestrictions(int32 width,
									int32 height, color_space colorSpace);
	virtual const overlay_buffer* AllocateOverlayBuffer(int32 width,
									int32 height, color_space space);
	virtual void				FreeOverlayBuffer(const overlay_buffer* buffer);

	virtual void				ConfigureOverlay(Overlay* overlay);
	virtual void				HideOverlay(Overlay* overlay);

	// accelerated drawing
	virtual	void				CopyRegion(const clipping_rect* sortedRectList,
										   uint32 count,
										   int32 xOffset, int32 yOffset);
	virtual	void				FillRegion(/*const*/ BRegion& region,
										   const rgb_color& color,
										   bool autoSync);
	virtual	void				InvertRegion(/*const*/ BRegion& region);

	virtual	void				Sync();

	// cursor handling
	virtual	void				SetCursor(ServerCursor* cursor);
	virtual	void				SetCursorVisible(bool visible);
	virtual	void				MoveCursorTo(const float& x,
											 const float& y);

	// frame buffer access
	virtual	RenderingBuffer*	FrontBuffer() const;
	virtual	RenderingBuffer*	BackBuffer() const;
	virtual	bool				IsDoubleBuffered() const;

protected:
	virtual	void				_DrawCursor(BRect area) const;

private:
		int						_OpenGraphicsDevice(int deviceNumber);
		status_t				_OpenAccelerant(int device);
		status_t				_SetupDefaultHooks();
		status_t				_UpdateModeList();
		status_t				_UpdateFrameBufferConfig();
		void					_RegionToRectParams(/*const*/ BRegion* region,
													uint32* count) const;
		uint32					_NativeColor(const rgb_color& color) const;
		status_t				_FindBestMode(const display_mode& compareMode,
									float compareAspectRatio,
									display_mode& modeFound,
									int32 *_diff = NULL) const;
		status_t				_SetFallbackMode(display_mode& mode) const;
		void					_SetSystemPalette();
		void					_SetGrayscalePalette();

		int						fCardFD;
		image_id				fAccelerantImage;
		GetAccelerantHook		fAccelerantHook;
		engine_token			*fEngineToken;
		sync_token				fSyncToken;

		// required hooks - guaranteed to be valid
		acquire_engine			fAccAcquireEngine;
		release_engine			fAccReleaseEngine;
		sync_to_token			fAccSyncToToken;
		accelerant_mode_count	fAccGetModeCount;
		get_mode_list			fAccGetModeList;
		get_frame_buffer_config	fAccGetFrameBufferConfig;
		set_display_mode		fAccSetDisplayMode;
		get_display_mode		fAccGetDisplayMode;
		get_pixel_clock_limits	fAccGetPixelClockLimits;

		// optional accelerant hooks
		get_timing_constraints	fAccGetTimingConstraints;
		propose_display_mode	fAccProposeDisplayMode;
		get_preferred_display_mode fAccGetPreferredDisplayMode;
		get_monitor_info		fAccGetMonitorInfo;
		get_edid_info			fAccGetEDIDInfo;
		fill_rectangle			fAccFillRect;
		invert_rectangle		fAccInvertRect;
		screen_to_screen_blit	fAccScreenBlit;
		set_cursor_shape		fAccSetCursorShape;
		move_cursor				fAccMoveCursor;
		show_cursor				fAccShowCursor;					
		
		// dpms hooks
		dpms_capabilities		fAccDPMSCapabilities;
		dpms_mode				fAccDPMSMode;
		set_dpms_mode			fAccSetDPMSMode;

		// overlay hooks
		overlay_count				fAccOverlayCount;
		overlay_supported_spaces	fAccOverlaySupportedSpaces;
		overlay_supported_features	fAccOverlaySupportedFeatures;
		allocate_overlay_buffer		fAccAllocateOverlayBuffer;
		release_overlay_buffer		fAccReleaseOverlayBuffer;
		get_overlay_constraints		fAccGetOverlayConstraints;
		allocate_overlay			fAccAllocateOverlay;
		release_overlay				fAccReleaseOverlay;
		configure_overlay			fAccConfigureOverlay;

		frame_buffer_config		fFrameBufferConfig;
		int						fModeCount;
		display_mode*			fModeList;

		MallocBuffer*			fBackBuffer;
		AccelerantBuffer*		fFrontBuffer;

		display_mode			fDisplayMode;
		bool					fInitialModeSwitch;

mutable	fill_rect_params*		fRectParams;
mutable	uint32					fRectParamsCount;
mutable	blit_params*			fBlitParams;
mutable	uint32					fBlitParamsCount;
};

#endif // ACCELERANT_HW_INTERFACE_H
