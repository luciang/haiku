/*
 * Copyright 2001-2008, Haiku.
 * Distributed under the terms of the MIT License.
 *
 * Authors:
 *		DarkWyrm <bpmagic@columbus.rr.com>
 *		Jérôme Duval, jerome.duval@free.fr
 *		Axel Dörfler, axeld@pinc-software.de
 */
#ifndef APP_SERVER_PROTOCOL_H
#define APP_SERVER_PROTOCOL_H


#include <SupportDefs.h>


// Server port names. The input port is the port which is used to receive
// input messages from the Input Server. The other is the "main" port for
// the server and is utilized mostly by BApplication objects.
#define SERVER_PORT_NAME "haiku app_server"
#if TEST_MODE
#	define SERVER_INPUT_PORT "haiku input port"
#endif

#define AS_REQUEST_COLOR_KEY 0x00010000
	// additional option for AS_VIEW_SET_VIEW_BITMAP

enum {
	// NOTE: all defines have to start with "AS_" to let the "code_to_name"
	// utility work correctly

	AS_GET_DESKTOP,
	AS_REGISTER_INPUT_SERVER = 1,
	AS_EVENT_STREAM_CLOSED,
		// Notification of event stream closing to restart input_server

	// Desktop definitions (through the ServerApp, though)
	AS_GET_WINDOW_LIST,
	AS_GET_WINDOW_INFO,
	AS_MINIMIZE_TEAM,
	AS_BRING_TEAM_TO_FRONT,
	AS_WINDOW_ACTION,

	// Application definitions
	AS_CREATE_APP,
	AS_DELETE_APP,
	AS_QUIT_APP,
	AS_ACTIVATE_APP,
	AS_APP_CRASHED,

	AS_CREATE_WINDOW,
	AS_CREATE_OFFSCREEN_WINDOW,
	AS_DELETE_WINDOW,
	AS_CREATE_BITMAP,
	AS_DELETE_BITMAP,
	AS_GET_BITMAP_OVERLAY_RESTRICTIONS,

	// Cursor commands
	AS_SET_CURSOR,

	AS_SHOW_CURSOR,
	AS_HIDE_CURSOR,
	AS_OBSCURE_CURSOR,
	AS_QUERY_CURSOR_HIDDEN,

	AS_CREATE_CURSOR,
	AS_DELETE_CURSOR,

	AS_BEGIN_RECT_TRACKING,
	AS_END_RECT_TRACKING,

	// Window definitions
	AS_SHOW_WINDOW,
	AS_HIDE_WINDOW,
	AS_MINIMIZE_WINDOW,
	AS_QUIT_WINDOW,
	AS_SEND_BEHIND,
	AS_SET_LOOK,
	AS_SET_FEEL, 
	AS_SET_FLAGS,
	AS_DISABLE_UPDATES,
	AS_ENABLE_UPDATES,
	AS_BEGIN_UPDATE,
	AS_END_UPDATE,
	AS_NEEDS_UPDATE,
	AS_SET_WINDOW_TITLE,
	AS_ADD_TO_SUBSET,
	AS_REMOVE_FROM_SUBSET,
	AS_SET_ALIGNMENT,
	AS_GET_ALIGNMENT,
	AS_GET_WORKSPACES,
	AS_SET_WORKSPACES,
	AS_WINDOW_RESIZE,
	AS_WINDOW_MOVE,
	AS_SET_SIZE_LIMITS,
	AS_ACTIVATE_WINDOW,
	AS_IS_FRONT_WINDOW,

	// BPicture definitions
	AS_CREATE_PICTURE,
	AS_DELETE_PICTURE,
	AS_CLONE_PICTURE,
	AS_DOWNLOAD_PICTURE,

	// Font-related server communications
	AS_SET_SYSTEM_FONT,
	AS_GET_SYSTEM_FONTS,
	AS_GET_SYSTEM_DEFAULT_FONT,

	AS_GET_FONT_LIST_REVISION,
	AS_GET_FAMILY_AND_STYLES,

	AS_GET_FAMILY_AND_STYLE,
	AS_GET_FAMILY_AND_STYLE_IDS,
	AS_GET_FONT_BOUNDING_BOX,
	AS_GET_TUNED_COUNT,
	AS_GET_TUNED_INFO,
	AS_GET_FONT_HEIGHT,
	AS_GET_FONT_FILE_FORMAT,
	AS_GET_EXTRA_FONT_FLAGS,

	AS_GET_STRING_WIDTHS,
	AS_GET_EDGES,
	AS_GET_ESCAPEMENTS,
	AS_GET_ESCAPEMENTS_AS_FLOATS,
	AS_GET_BOUNDINGBOXES_CHARS,
	AS_GET_BOUNDINGBOXES_STRING,
	AS_GET_BOUNDINGBOXES_STRINGS,
	AS_GET_HAS_GLYPHS,
	AS_GET_GLYPH_SHAPES,
	AS_GET_TRUNCATED_STRINGS,

	// Screen methods
	AS_VALID_SCREEN_ID,
	AS_GET_NEXT_SCREEN_ID,
	AS_SCREEN_GET_MODE,
	AS_SCREEN_SET_MODE,
	AS_PROPOSE_MODE,
	AS_GET_MODE_LIST,

	AS_GET_PIXEL_CLOCK_LIMITS,
	AS_GET_TIMING_CONSTRAINTS,

	AS_SCREEN_GET_COLORMAP,
	AS_GET_DESKTOP_COLOR,
	AS_SET_DESKTOP_COLOR,
	AS_GET_SCREEN_ID_FROM_WINDOW,

	AS_READ_BITMAP,

	AS_GET_RETRACE_SEMAPHORE,
	AS_GET_ACCELERANT_INFO,
	AS_GET_MONITOR_INFO,
	AS_GET_FRAME_BUFFER_CONFIG,
	
	AS_SET_DPMS,
	AS_GET_DPMS_STATE,
	AS_GET_DPMS_CAPABILITIES,

	// Misc stuff
	AS_GET_ACCELERANT_PATH,
	AS_GET_DRIVER_PATH,

	// Global function call defs
	AS_SET_UI_COLORS,
	AS_SET_UI_COLOR,
	AS_SET_DECORATOR,
	AS_GET_DECORATOR,
	AS_R5_SET_DECORATOR,
	AS_COUNT_DECORATORS,
	AS_GET_DECORATOR_NAME,

	AS_COUNT_WORKSPACES,
	AS_SET_WORKSPACE_COUNT,
	AS_CURRENT_WORKSPACE,
	AS_ACTIVATE_WORKSPACE,
	AS_GET_SCROLLBAR_INFO,
	AS_SET_SCROLLBAR_INFO,
	AS_GET_MENU_INFO,
	AS_SET_MENU_INFO,
	AS_IDLE_TIME,
	AS_SET_MOUSE_MODE,
	AS_GET_MOUSE_MODE,
	AS_GET_MOUSE,
	AS_SET_DECORATOR_SETTINGS,
	AS_GET_DECORATOR_SETTINGS,
	AS_GET_SHOW_ALL_DRAGGERS,
	AS_SET_SHOW_ALL_DRAGGERS,

	// Graphics calls
	AS_SET_HIGH_COLOR,
	AS_SET_LOW_COLOR,
	AS_SET_VIEW_COLOR,

	AS_STROKE_ARC,
	AS_STROKE_BEZIER,
	AS_STROKE_ELLIPSE,
	AS_STROKE_LINE,
	AS_STROKE_LINEARRAY,
	AS_STROKE_POLYGON,
	AS_STROKE_RECT,
	AS_STROKE_ROUNDRECT,
	AS_STROKE_SHAPE,
	AS_STROKE_TRIANGLE,

	AS_FILL_ARC,
	AS_FILL_BEZIER,
	AS_FILL_ELLIPSE,
	AS_FILL_POLYGON,
	AS_FILL_RECT,
	AS_FILL_REGION,
	AS_FILL_ROUNDRECT,
	AS_FILL_SHAPE,
	AS_FILL_TRIANGLE,

	AS_DRAW_STRING,
	AS_DRAW_STRING_WITH_DELTA,

	AS_SYNC,

	AS_VIEW_CREATE,
	AS_VIEW_DELETE,
	AS_VIEW_CREATE_ROOT,
	AS_VIEW_SHOW,
	AS_VIEW_HIDE,
	AS_VIEW_MOVE,
	AS_VIEW_RESIZE,
	AS_VIEW_DRAW,

	// View/Layer definitions
	AS_VIEW_GET_COORD,
	AS_VIEW_SET_FLAGS,
	AS_VIEW_SET_ORIGIN,
	AS_VIEW_GET_ORIGIN,
	AS_VIEW_RESIZE_MODE,
	AS_VIEW_SET_CURSOR,
	AS_VIEW_BEGIN_RECT_TRACK,
	AS_VIEW_END_RECT_TRACK,
	AS_VIEW_DRAG_RECT,
	AS_VIEW_DRAG_IMAGE,
	AS_VIEW_SCROLL,
	AS_VIEW_SET_LINE_MODE,
	AS_VIEW_GET_LINE_MODE,
	AS_VIEW_PUSH_STATE,
	AS_VIEW_POP_STATE,
	AS_VIEW_SET_SCALE,
	AS_VIEW_GET_SCALE,
	AS_VIEW_SET_DRAWING_MODE,
	AS_VIEW_GET_DRAWING_MODE,
	AS_VIEW_SET_BLENDING_MODE,
	AS_VIEW_GET_BLENDING_MODE,
	AS_VIEW_SET_PEN_LOC,
	AS_VIEW_GET_PEN_LOC,
	AS_VIEW_SET_PEN_SIZE,
	AS_VIEW_GET_PEN_SIZE,
	AS_VIEW_SET_HIGH_COLOR,
	AS_VIEW_SET_LOW_COLOR,
	AS_VIEW_SET_VIEW_COLOR,
	AS_VIEW_GET_HIGH_COLOR,
	AS_VIEW_GET_LOW_COLOR,
	AS_VIEW_GET_VIEW_COLOR,
	AS_VIEW_PRINT_ALIASING,
	AS_VIEW_CLIP_TO_PICTURE,
	AS_VIEW_GET_CLIP_REGION,
	AS_VIEW_DRAW_BITMAP,
	AS_VIEW_SET_EVENT_MASK,
	AS_VIEW_SET_MOUSE_EVENT_MASK,

	AS_VIEW_DRAW_STRING,
	AS_VIEW_SET_CLIP_REGION,
	AS_VIEW_LINE_ARRAY,
	AS_VIEW_BEGIN_PICTURE,
	AS_VIEW_APPEND_TO_PICTURE,
	AS_VIEW_END_PICTURE,
	AS_VIEW_COPY_BITS,
	AS_VIEW_DRAW_PICTURE,
	AS_VIEW_INVALIDATE_RECT,
	AS_VIEW_INVALIDATE_REGION,
	AS_VIEW_INVERT_RECT,
	AS_VIEW_MOVE_TO,
	AS_VIEW_RESIZE_TO,
	AS_VIEW_SET_STATE,
	AS_VIEW_SET_FONT_STATE,
	AS_VIEW_GET_STATE,
	AS_VIEW_SET_VIEW_BITMAP,
	AS_VIEW_SET_PATTERN,
	AS_SET_CURRENT_VIEW,

	// BDirectWindow codes
	AS_DIRECT_WINDOW_GET_SYNC_DATA,
	AS_DIRECT_WINDOW_SET_FULLSCREEN,

	AS_LAST_CODE
};

// Cursor types, currently they are all private besides the first two
enum cursor_which {
	B_CURSOR_DEFAULT = 1,
	B_CURSOR_TEXT,
	B_CURSOR_MOVE,
	B_CURSOR_DRAG,
	B_CURSOR_RESIZE,
	B_CURSOR_RESIZE_NWSE,
	B_CURSOR_RESIZE_NESW,
	B_CURSOR_RESIZE_NS,
	B_CURSOR_RESIZE_EW,
	B_CURSOR_OTHER,
	B_CURSOR_APP,
	B_CURSOR_INVALID
};

// bitmap allocation flags
enum {
	kAllocator			= 0x1,
	kFramebuffer		= 0x2,
	kHeap				= 0x4,
	kNewAllocatorArea	= 0x8,
};

#endif	// APP_SERVER_PROTOCOL_H
