/*
	Copyright 1999, Be Incorporated.   All Rights Reserved.
	This file may be used under the terms of the Be Sample Code License.

	Other authors:
	Mark Watson;
	Apsed;
	Rudolf Cornelissen 10/2002-4/2005.
*/

#ifndef DRIVERINTERFACE_H
#define DRIVERINTERFACE_H

#include <Accelerant.h>
#include "video_overlay.h"
#include <Drivers.h>
#include <PCI.h>
#include <OS.h>
#include "AGP.h"

#define DRIVER_PREFIX "nv" // apsed

/*
	Internal driver state (also for sharing info between driver and accelerant)
*/
#if defined(__cplusplus)
extern "C" {
#endif

typedef struct {
	sem_id	sem;
	int32	ben;
} benaphore;

#define INIT_BEN(x)		x.sem = create_sem(0, "NV "#x" benaphore");  x.ben = 0;
#define AQUIRE_BEN(x)	if((atomic_add(&(x.ben), 1)) >= 1) acquire_sem(x.sem);
#define RELEASE_BEN(x)	if((atomic_add(&(x.ben), -1)) > 1) release_sem(x.sem);
#define	DELETE_BEN(x)	delete_sem(x.sem);


#define NV_PRIVATE_DATA_MAGIC	0x0009 /* a private driver rev, of sorts */

/*dualhead extensions to flags*/
#define DUALHEAD_OFF (0<<6)
#define DUALHEAD_CLONE (1<<6)
#define DUALHEAD_ON (2<<6)
#define DUALHEAD_SWITCH (3<<6)
#define DUALHEAD_BITS (3<<6)
#define DUALHEAD_CAPABLE (1<<8)
#define TV_BITS (3<<9)
#define TV_MON (0<<9
#define TV_PAL (1<<9)
#define TV_NTSC (2<<9)
#define TV_CAPABLE (1<<11)
#define TV_VIDEO (1<<12)

#define SKD_MOVE_CURSOR    0x00000001
#define SKD_PROGRAM_CLUT   0x00000002
#define SKD_SET_START_ADDR 0x00000004
#define SKD_SET_CURSOR     0x00000008
#define SKD_HANDLER_INSTALLED 0x80000000

enum {
	NV_GET_PRIVATE_DATA = B_DEVICE_OP_CODES_END + 1,
	NV_GET_PCI,
	NV_SET_PCI,
	NV_DEVICE_NAME,
	NV_RUN_INTERRUPTS,
	NV_GET_NTH_AGP_INFO,
	NV_ENABLE_AGP,
	NV_ISA_OUT,
	NV_ISA_IN
};

/* handles to pre-defined engine commands */
#define NV_ROP5_SOLID					0x00000000 /* 2D */
#define NV_IMAGE_BLACK_RECTANGLE		0x00000001 /* 2D/3D */
#define NV_IMAGE_PATTERN				0x00000002 /* 2D */
#define NV3_SURFACE_0					0x00000003 /* 3D: old cmd */
#define NV3_SURFACE_1					0x00000004 /* 3D: old cmd */
#define NV3_SURFACE_2					0x00000005 /* 3D: nolonger used */
#define NV3_SURFACE_3					0x00000006 /* 3D: nolonger used */
#define NV4_CONTEXT_SURFACES_ARGB_ZS	0x00000007 /* 3D */
#define NV10_CONTEXT_SURFACES_ARGB_ZS	0x00000007 /* 3D */
//fixme: learn the rules of nvidia's hardware hashtable implementation!!!
//#define NV4_SURFACE						0x00000008 /* 2D */
//#define NV10_CONTEXT_SURFACES_2D		0x00000008 /* 2D */
#define NV4_SURFACE						0x00000010 /* 2D */
#define NV10_CONTEXT_SURFACES_2D		0x00000010 /* 2D */
#define NV1_IMAGE_FROM_CPU				0x00000010 /* 2D: unused */
#define NV_IMAGE_BLIT					0x00000011 /* 2D */
/* fixme:
 * never use NV3_GDI_RECTANGLE_TEXT for DMA acceleration:
 * There's a hardware fault in the input->output colorspace conversion here.
 * Besides, in NV40 and up this command nolonger exists. Both 'facts' are confirmed
 * by testing.
 * Note:
 * There's no (big) speed difference between NV3_GDI_RECTANGLE_TEXT and
 * NV4_GDI_RECTANGLE_TEXT (tested with BeRoMeter 1.2.6). */
#define NV3_GDI_RECTANGLE_TEXT			0x00000012 /* 2D */
#define NV4_GDI_RECTANGLE_TEXT			0x00000012 /* 2D */
#define NV_RENDER_D3D0_TRIANGLE_ZETA	0x00000013 /* 3D: nolonger used */
#define NV4_DX5_TEXTURE_TRIANGLE		0x00000014 /* 3D */
#define NV10_DX5_TEXTURE_TRIANGLE		0x00000014 /* 3D */
#define NV4_DX6_MULTI_TEXTURE_TRIANGLE	0x00000015 /* unused (yet?) */
#define NV10_DX6_MULTI_TEXTURE_TRIANGLE	0x00000015 /* unused (yet?) */
#define NV1_RENDER_SOLID_LIN			0x00000016 /* 2D: unused */

/* max. number of overlay buffers */
#define MAXBUFFERS 3

/* internal used info on overlay buffers */
typedef	struct {
	uint16 slopspace;
	uint32 size;
} int_buf_info;

typedef struct { // apsed, see comments in nv.settings
	// for driver
	char   accelerant[B_FILE_NAME_LENGTH];
	bool   dumprom;
	// for accelerant
	uint32 logmask;
	uint32 memory;
	bool   usebios;
	bool   hardcursor;
	bool   switchhead;
	bool   force_pci;
	bool   unhide_fw;
	bool   pgm_panel;
	bool   dma_acc;
} nv_settings;

/*shared info*/
typedef struct {
  /*a few ID things*/
	uint16	vendor_id;	/* PCI vendor ID, from pci_info */
	uint16	device_id;	/* PCI device ID, from pci_info */
	uint8	revision;	/* PCI device revsion, from pci_info */
	uint8	bus;		/* PCI bus number, from pci_info */
	uint8	device;		/* PCI device number on bus, from pci_info */
	uint8	function;	/* PCI function number in device, from pci_info */

  /* bug workaround for 4.5.0 */
	uint32 use_clone_bugfix;	/*for 4.5.0, cloning of physical memory does not work*/
	uint32 * clone_bugfix_regs;

  /*memory mappings*/
	area_id	regs_area;	/* Kernel's area_id for the memory mapped registers.
							It will be cloned into the accelerant's	address
							space. */

	area_id	fb_area;	/* Frame buffer's area_id.  The addresses are shared with all teams. */
	area_id pseudo_dma_area;	/* Pseudo dma area_id. Shared by all teams. */
	area_id	dma_buffer_area;	/* Area assigned for dma*/

	void	*framebuffer;		/* As viewed from virtual memory */
	void	*framebuffer_pci;	/* As viewed from the PCI bus (for DMA) */

	void	*pseudo_dma;		/* As viewed from virtual memory */

	void	*dma_buffer;		/* buffer for dma*/
	void	*dma_buffer_pci;	/* buffer for dma - from PCI bus*/

  /*screenmode list*/
	area_id	mode_area;              /* Contains the list of display modes the driver supports */
	uint32	mode_count;             /* Number of display modes in the list */

  /*flags - used by driver*/
	uint32 flags;

  /*vblank semaphore*/
	sem_id	vblank;	                /* The vertical blank semaphore. Ownership will be
						transfered to the team opening the device first */
  /*cursor information*/
	struct {
		uint16	hot_x;		/* Cursor hot spot. The top left corner of the cursor */
		uint16	hot_y;		/* is 0,0 */
		uint16	x;		/* The location of the cursor hot spot on the */
		uint16	y;		/* desktop */
		uint16	width;		/* Width and height of the cursor shape (always 16!) */
		uint16	height;
		bool	is_visible;	/* Is the cursor currently displayed? */
		bool	dh_right;	/* Is cursor on right side of stretched screen? */
	} cursor;

  /*colour lookup table*/
	uint8	color_data[3 * 256];	/* Colour lookup table - as used by DAC */

  /*more display mode stuff*/
	display_mode dm;		/* current display mode configuration: head1 */
	bool acc_mode;			/* signals (non)accelerated mode */
	bool interlaced_tv_mode;/* signals interlaced CRTC TV output mode */
	bool crtc_switch_mode;	/* signals dualhead switch mode if panels are used */

  /*frame buffer config - for BDirectScreen*/
	frame_buffer_config fbc;	/* bytes_per_row and start of frame buffer: head1 */

  /*acceleration engine*/
	struct {
		uint32		count;		/* last dwgsync slot used */
		uint32		last_idle;	/* last dwgsync slot we *know* the engine was idle after */ 
		benaphore	lock;		/* for serializing access to the acc engine */
		struct {
			uint32	handle[0x08];	/* FIFO channel's cmd handle for the owning cmd */
			uint32	ch_ptr[0x20];	/* cmd handle's ptr to it's assigned FIFO ch (if any) */
		} fifo;
		struct {
			uint32 *cmdbuffer;	/* location of DMA command buffer */
			uint32 put;			/* last 32-bit-word adress given to engine to exec. to */
			uint32 current;		/* first free 32-bit-word adress in buffer */
			uint32 free;		/* nr. of useable free 32-bit words remaining in buffer */
			uint32 max;			/* command buffer's useable size in 32-bit words */
		} dma;
	} engine;

	/* fixme:
	 * the stuff below has to be extended and/or changed I guess:
	 * for now only a single 3D 'clone' driver is supported. */
	/* pointers to first and last free memory adress for 3D use: cardmem local offsets */
	uint32 mem_low;
	uint32 mem_high;
	/* flag to inform 3D add-on to stop rendering (set by 2D, reset by 3D drv) */
	bool mode_changed;
	/* flag to inform 3D add-on a mode-change is currently in progress
	 * (set and reset by 2D drv) */
	bool mode_changing;

  /* card info - information gathered from PINS (and other sources) */
	enum
	{	// card_type in order of date of NV chip design
		NV04 = 0,
		NV05,
		NV05M64,
		NV06,
		NV10,
		NV11,
		NV11M,
		NV15,
		NV17,
		NV17M,
		NV18,
		NV18M,
		NV20,
		NV25,
		NV28,
		NV30,
		NV31,
		NV34,
		NV35,
		NV36,
		NV38,
		NV40,
		NV41,
		NV43,
		NV44,
		NV45
	};
	enum
	{	// card_arch in order of date of NV chip design
		NV04A = 0,
		NV10A,
		NV20A,
		NV30A,
		NV40A
	};
	enum
	{	// tvout_chip_type in order of capability (more or less)
		NONE = 0,
		CH7003,
		CH7004,
		CH7005,
		CH7006,
		CH7007,
		CH7008,
		SAA7102,
		SAA7103,
		SAA7104,
		SAA7105,
		BT868,
		BT869,
		CX25870,
		CX25871,
		NVIDIA
	};

	struct
	{
		/* specialised registers for card initialisation read from NV BIOS (pins) */

		/* general card information */
		uint32 card_type;           /* see card_type enum above */
		uint32 card_arch;           /* see card_arch enum above */
		bool laptop;	            /* mobile chipset or not ('internal' flatpanel!) */
		bool slaved_tmds1;			/* external TMDS encoder active on CRTC1 */
		bool slaved_tmds2;			/* external TMDS encoder active on CRTC2 */
		bool master_tmds1;			/* on die TMDS encoder active on CRTC1 */
		bool master_tmds2;			/* on die TMDS encoder active on CRTC2 */
		bool tmds1_active;			/* found panel on CRTC1 that is active */
		bool tmds2_active;			/* found panel on CRTC2 that is active */
		display_timing p1_timing;	/* 'modeline' fetched for panel 1 */
		display_timing p2_timing;	/* 'modeline' fetched for panel 2 */
		float panel1_aspect;		/* panel's aspect ratio */
		float panel2_aspect;		/* panel's aspect ratio */
		bool crtc2_prim;			/* using CRTC2 as primary CRTC */
		uint32 tvout_chip_type;     /* see tvchip_type enum above */
		uint8 monitors;				/* output devices connection matrix */
		status_t pins_status;		/* B_OK if read correctly, B_ERROR if faked */

		/* PINS */
		float f_ref;				/* PLL reference-oscillator frequency (Mhz) */
		bool ext_pll;				/* the extended PLL contains more dividers */
		uint32 max_system_vco;		/* graphics engine PLL VCO limits (Mhz) */
		uint32 min_system_vco;
		uint32 max_pixel_vco;		/* dac1 PLL VCO limits (Mhz) */
		uint32 min_pixel_vco;
		uint32 max_video_vco;		/* dac2 PLL VCO limits (Mhz) */
		uint32 min_video_vco;
		uint32 std_engine_clock;	/* graphics engine clock speed needed (Mhz) */
		uint32 std_memory_clock;	/* card memory clock speed needed (Mhz) */
		uint32 max_dac1_clock;		/* dac1 limits (Mhz) */
		uint32 max_dac1_clock_8;	/* dac1 limits correlated to RAMspeed limits (Mhz) */
		uint32 max_dac1_clock_16;
		uint32 max_dac1_clock_24;
		uint32 max_dac1_clock_32;
		uint32 max_dac1_clock_32dh;
		uint32 max_dac2_clock;		/* dac2 limits (Mhz) */
		uint32 max_dac2_clock_8;	/* dac2, maven limits correlated to RAMspeed limits (Mhz) */
		uint32 max_dac2_clock_16;
		uint32 max_dac2_clock_24;
		uint32 max_dac2_clock_32;
		uint32 max_dac2_clock_32dh;
		bool secondary_head;		/* presence of functions */
		bool tvout;
		bool primary_dvi;
		bool secondary_dvi;
		uint32 memory_size;			/* memory (in bytes) */
	} ps;

	/* mirror of the ROM (copied in driver, because may not be mapped permanently) */
	uint8 rom_mirror[65536];

	/* some configuration settings from ~/config/settings/kernel/drivers/nv.settings if exists */
	nv_settings settings;

	struct
	{
		overlay_buffer myBuffer[MAXBUFFERS];/* scaler input buffers */
		int_buf_info myBufInfo[MAXBUFFERS];	/* extra info on scaler input buffers */
		overlay_token myToken;				/* scaler is free/in use */
		benaphore lock;						/* for creating buffers and aquiring overlay unit routines */
		bool crtc;							/* location of overlay unit */
		/* variables needed for virtualscreens (move_overlay()): */
		bool active;						/* true is overlay currently in use */
		overlay_window ow;					/* current position of overlay output window */
		overlay_buffer ob;					/* current inputbuffer in use */
		overlay_view my_ov;					/* current corrected view in inputbuffer */
		uint32 h_ifactor;					/* current 'unclipped' horizontal inverse scaling factor */
		uint32 v_ifactor;					/* current 'unclipped' vertical inverse scaling factor */
	} overlay;

} shared_info;

/* Read or write a value in PCI configuration space */
typedef struct {
	uint32	magic;		/* magic number to make sure the caller groks us */
	uint32	offset;		/* Offset to read/write */
	uint32	size;		/* Number of bytes to transfer */
	uint32	value;		/* The value read or written */
} nv_get_set_pci;

/* Set some boolean condition (like enabling or disabling interrupts) */
typedef struct {
	uint32	magic;		/* magic number to make sure the caller groks us */
	bool	do_it;		/* state to set */
} nv_set_bool_state;

/* Retrieve the area_id of the kernel/accelerant shared info */
typedef struct {
	uint32	magic;		/* magic number to make sure the caller groks us */
	area_id	shared_info_area;	/* area_id containing the shared information */
} nv_get_private_data;

/* Retrieve the device name.  Usefull for when we have a file handle, but want
to know the device name (like when we are cloning the accelerant) */
typedef struct {
	uint32	magic;		/* magic number to make sure the caller groks us */
	char	*name;		/* The name of the device, less the /dev root */
} nv_device_name;

/* Retrieve an AGP device interface if there. Usefull to find the AGP speed scheme
used (pre 3.x or 3.x) */
typedef struct {
	uint32		magic;	/* magic number to make sure the caller groks us */
	bool		agp_bus;/* indicates if we have access to the AGP busmanager */
	uint8		index;	/* device index in list of devices found */
	bool		exist;	/* we got AGP device info */
	agp_info	agpi;	/* AGP interface info of a device */
} nv_nth_agp_info;

/* Execute an AGP command */
typedef struct {
	uint32		magic;	/* magic number to make sure the caller groks us */
	bool		agp_bus;/* indicates if we have access to the AGP busmanager */
	uint32		cmd;	/* actual command to execute */
} nv_cmd_agp;

/* Read or write a value in ISA I/O space */
typedef struct {
	uint32	magic;		/* magic number to make sure the caller groks us */
	uint16	adress;		/* Offset to read/write */
	uint8	size;		/* Number of bytes to transfer */
	uint16	data;		/* The value read or written */
} nv_in_out_isa;

enum {
	
	_WAIT_FOR_VBLANK = (1 << 0)
};

#if defined(__cplusplus)
}
#endif


#endif
