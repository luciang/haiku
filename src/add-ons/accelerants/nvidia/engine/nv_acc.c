/* NV Acceleration functions */
/* Author:
   Rudolf Cornelissen 8/2003-1/2005.

   This code was possible thanks to:
    - the Linux XFree86 NV driver,
    - the Linux UtahGLX 3D driver.
*/

#define MODULE_BIT 0x00080000

#include "nv_std.h"

static void nv_init_for_3D(void);

/*acceleration notes*/

/*functions Be's app_server uses:
fill span (horizontal only)
fill rectangle (these 2 are very similar)
invert rectangle 
blit
*/

/*
	nVidia hardware info:
	We should be able to do FIFO assignment setup changes on-the-fly now, using
	all the engine-command-handles that are pre-defined on any FIFO channel.

	We should be able to even setup new additional handles to previously unused engine
	commands now, and DMA use should also be possible.
*/

/* FIFO channel pointers */
/* note:
 * every instance of the accelerant needs to have it's own pointers, as the registers
 * are cloned to different adress ranges for each one */
static cmd_nv_rop5_solid* nv_rop5_solid_ptr;
static cmd_nv_image_black_rectangle* nv_image_black_rectangle_ptr;
static cmd_nv_image_pattern* nv_image_pattern_ptr;
static cmd_nv_image_blit* nv_image_blit_ptr;
static cmd_nv3_gdi_rectangle_text* nv3_gdi_rectangle_text_ptr;

status_t nv_acc_wait_idle()
{
	/* wait until engine completely idle */
	while (ACCR(STATUS))
	{
		/* snooze a bit so I do not hammer the bus */
		snooze (100); 
	}

	return B_OK;
}

/* AFAIK this must be done for every new screenmode.
 * Engine required init. */
status_t nv_acc_init()
{
	uint16 cnt;

	/* setup PTIMER: */
	//fixme? how about NV28 setup as just after coldstarting? (see nv_info.c)
	/* set timer numerator to 8 (in b0-15) */
	ACCW(PT_NUMERATOR, 0x00000008);
	/* set timer denominator to 3 (in b0-15) */
	ACCW(PT_DENOMINATR, 0x00000003);

	/* disable timer-alarm INT requests (b0) */
	ACCW(PT_INTEN, 0x00000000);
	/* reset timer-alarm INT status bit (b0) */
	ACCW(PT_INTSTAT, 0xffffffff);

	/* enable PRAMIN write access on pre NV10 before programming it! */
	if (si->ps.card_arch == NV04A)
	{
		/* set framebuffer config: type = notiling, PRAMIN write access enabled */
		NV_REG32(NV32_PFB_CONFIG_0) = 0x00001114;
	}

	/*** PFIFO ***/
	/* (setup caches) */
	/* disable caches reassign */
	ACCW(PF_CACHES, 0x00000000);
	/* cache1 push0 access disabled */
	ACCW(PF_CACH1_PSH0, 0x00000000);
	/* cache1 pull0 access disabled */
	ACCW(PF_CACH1_PUL0, 0x00000000);

	//fixme: set these up for DMA use one day..
	/* cache1 push1 mode = pio (disable DMA use) */
	ACCW(PF_CACH1_PSH1, 0x00000000);
	/* cache1 DMA instance adress = none (b0-15);
	 * instance being b4-19 with baseadress NV_PRAMIN_CTX_0 (0x00700000). */
	/* note:
	 * should point to a DMA definition in CTX register space (which is sort of RAM).
	 * This define tells the engine where the DMA cmd buffer is and what it's size is;
	 * inside that cmd buffer you'll find the engine handles for the FIFO channels,
	 * followed by actual issued engine commands. */
	ACCW(PF_CACH1_DMAI, 0x00000000);

	/* cache0 push0 access disabled */
	ACCW(PF_CACH0_PSH0, 0x00000000);
	/* cache0 pull0 access disabled */
	ACCW(PF_CACH0_PUL0, 0x00000000);
	/* RAM HT (hash table) baseadress = $10000 (b4-8), size = 4k,
	 * search = 128 (is byte offset between hash 'sets') */
	/* note:
	 * so HT base is $00710000, last is $00710fff.
	 * In this space you define the engine command handles (HT_HANDL_XX), which
	 * in turn points to the defines in CTX register space (which is sort of RAM) */
	ACCW(PF_RAMHT, 0x03000100);
	/* RAM FC baseadress = $11000 (b3-8) (size is fixed to 0.5k(?)) */
	/* note:
	 * so FC base is $00711000, last is $007111ff. (not used?) */
	ACCW(PF_RAMFC, 0x00000110);
	/* RAM RO baseadress = $11200 (b1-8), size = 0.5k */
	/* note:
	 * so RO base is $00711200, last is $007113ff. (not used?) */
	/* note also:
	 * This means(?) the PRAMIN CTX registers are accessible from base $00711400. */
	ACCW(PF_RAMRO, 0x00000112);
	/* PFIFO size: ch0-15 = 512 bytes, ch16-31 = 124 bytes */
	ACCW(PF_SIZE, 0x0000ffff);
	/* cache1 hash instance = $ffff (b0-15) */
	ACCW(PF_CACH1_HASH, 0x0000ffff);
	/* disable all PFIFO INTs */
	ACCW(PF_INTEN, 0x00000000);
	/* reset all PFIFO INT status bits */
	ACCW(PF_INTSTAT, 0xffffffff);
	/* cache0 pull0 engine = acceleration engine (graphics) */
	ACCW(PF_CACH0_PUL1, 0x00000001);
	/* cache1 push0 access enabled */
	ACCW(PF_CACH1_PSH0, 0x00000001);
	/* cache1 pull0 access enabled */
	ACCW(PF_CACH1_PUL0, 0x00000001);
	/* cache1 pull1 engine = acceleration engine (graphics) */
	ACCW(PF_CACH1_PUL1, 0x00000001);
	/* enable PFIFO caches reassign */
	ACCW(PF_CACHES, 0x00000001);

	/*** PRAMIN ***/
	/* RAMHT space (hash-table(?)) SETUP FIFO HANDLES */
	/* note:
	 * 'instance' tells you where the engine command is stored in 'PR_CTXx_x' sets
	 * below: instance being b4-19 with baseadress NV_PRAMIN_CTX_0 (0x00700000).
	 * That command is linked to the handle noted here. This handle is then used to
	 * tell the FIFO to which engine command it is connected!
	 * (CTX registers are actually a sort of RAM space.) */
	/* (first set) */
	ACCW(HT_HANDL_00, NV1_IMAGE_FROM_CPU); /* 32bit handle (not used?) */
	ACCW(HT_VALUE_00, 0x80011145); /* instance $1145, engine = acc engine, CHID = $00 */

	ACCW(HT_HANDL_01, NV_IMAGE_BLIT); /* 32bit handle */
	ACCW(HT_VALUE_01, 0x80011146); /* instance $1146, engine = acc engine, CHID = $00 */

	ACCW(HT_HANDL_02, NV3_GDI_RECTANGLE_TEXT); /* 32bit handle */
	ACCW(HT_VALUE_02, 0x80011147); /* instance $1147, engine = acc engine, CHID = $00 */

	ACCW(HT_HANDL_03, NV_RENDER_D3D0_TRIANGLE_ZETA); /* 32bit handle (not used?) */
	ACCW(HT_VALUE_03, 0x80011148); /* instance $1148, engine = acc engine, CHID = $00 */

	/* NV4_ and NV10_DX5_TEXTURE_TRIANGLE should be identical */
	ACCW(HT_HANDL_04, NV4_DX5_TEXTURE_TRIANGLE); /* 32bit handle */
	ACCW(HT_VALUE_04, 0x80011149); /* instance $1149, engine = acc engine, CHID = $00 */

	/* NV4_ and NV10_DX6_MULTI_TEXTURE_TRIANGLE should be identical */
	ACCW(HT_HANDL_05, NV4_DX6_MULTI_TEXTURE_TRIANGLE); /* 32bit handle (not used) */
	ACCW(HT_VALUE_05, 0x8001114a); /* instance $114a, engine = acc engine, CHID = $00 */

	ACCW(HT_HANDL_06, NV1_RENDER_SOLID_LIN); /* 32bit handle (not used) */
	if (si->ps.card_arch != NV04A)
		ACCW(HT_VALUE_06, 0x80011150); /* instance $1150, engine = acc engine, CHID = $00 */
	else
		ACCW(HT_VALUE_06, 0x8001114f); /* instance $114f, engine = acc engine, CHID = $00 */

	/* (second set) */
	ACCW(HT_HANDL_10, NV_ROP5_SOLID); /* 32bit handle */
	ACCW(HT_VALUE_10, 0x80011142); /* instance $1142, engine = acc engine, CHID = $00 */

	ACCW(HT_HANDL_11, NV_IMAGE_BLACK_RECTANGLE); /* 32bit handle */
	ACCW(HT_VALUE_11, 0x80011143); /* instance $1143, engine = acc engine, CHID = $00 */

	ACCW(HT_HANDL_12, NV_IMAGE_PATTERN); /* 32bit handle */
	ACCW(HT_VALUE_12, 0x80011144); /* instance $1144, engine = acc engine, CHID = $00 */

	ACCW(HT_HANDL_13, NV3_SURFACE_0); /* 32bit handle (not used) */
	ACCW(HT_VALUE_13, 0x8001114b); /* instance $114b, engine = acc engine, CHID = $00 */

	ACCW(HT_HANDL_14, NV3_SURFACE_1); /* 32bit handle (not used) */
	ACCW(HT_VALUE_14, 0x8001114c); /* instance $114c, engine = acc engine, CHID = $00 */

	ACCW(HT_HANDL_15, NV3_SURFACE_2); /* 32bit handle (not used) */
	ACCW(HT_VALUE_15, 0x8001114d); /* instance $114d, engine = acc engine, CHID = $00 */

	ACCW(HT_HANDL_16, NV3_SURFACE_3); /* 32bit handle (not used) */
	ACCW(HT_VALUE_16, 0x8001114e); /* instance $114e, engine = acc engine, CHID = $00 */

	/* fixme note:
	 * why not setup NV4_CONTEXT_SURFACES_ARGB_ZS as well?? (they are compatible..) */
	if (si->ps.card_arch != NV04A)
	{
		ACCW(HT_HANDL_17, NV10_CONTEXT_SURFACES_ARGB_ZS); /* 32bit handle (3D only) */
		ACCW(HT_VALUE_17, 0x8001114f); /* instance $114f, engine = acc engine, CHID = $00 */
	}

	/* program CTX registers: CTX1 is mostly done later (colorspace dependant) */
	/* note:
	 * CTX determines which HT handles point to what engine commands.
	 * (CTX registers are actually a sort of RAM space.) */
	/* setup a DMA define for use by command defines below.
	 * (would currently be used by CTX 'sets' 0x6 upto/including 0xe: 3D stuff.) */
	ACCW(PR_CTX0_R, 0x00003000); /* DMA page table present and of linear type;
								  * DMA target node is NVM (non-volatile memory?)
								  * (instead of doing PCI or AGP transfers) */
	ACCW(PR_CTX1_R, (si->ps.memory_size - 1)); /* DMA limit */
	ACCW(PR_CTX2_R, 0x00000002); /* DMA access type is READ_AND_WRITE */
	ACCW(PR_CTX3_R, 0x00000002); /* unknown (looks like this is rubbish/not needed?) */
	/* setup set '0' for cmd NV_ROP5_SOLID */
	ACCW(PR_CTX0_0, 0x01008043); /* NVclass $043, patchcfg ROP_AND, nv10+: little endian */
	ACCW(PR_CTX2_0, 0x00000000); /* DMA0 and DMA1 instance invalid */
	ACCW(PR_CTX3_0, 0x00000000); /* method traps disabled */
	/* setup set '1' for cmd NV_IMAGE_BLACK_RECTANGLE */
	ACCW(PR_CTX0_1, 0x01008019); /* NVclass $019, patchcfg ROP_AND, nv10+: little endian */
	ACCW(PR_CTX2_1, 0x00000000); /* DMA0 and DMA1 instance invalid */
	ACCW(PR_CTX3_1, 0x00000000); /* method traps disabled */
	/* setup set '2' for cmd NV_IMAGE_PATTERN */
	ACCW(PR_CTX0_2, 0x01008018); /* NVclass $018, patchcfg ROP_AND, nv10+: little endian */
	ACCW(PR_CTX2_2, 0x00000000); /* DMA0 and DMA1 instance invalid */
	ACCW(PR_CTX3_2, 0x00000000); /* method traps disabled */
	/* setup set '3' for cmd NV1_IMAGE_FROM_CPU (not used?) */
	ACCW(PR_CTX0_3, 0x01008021); /* NVclass $021, patchcfg ROP_AND, nv10+: little endian */
	ACCW(PR_CTX2_3, 0x00000000); /* DMA0 and DMA1 instance invalid */
	ACCW(PR_CTX3_3, 0x00000000); /* method traps disabled */
	/* setup set '4' for cmd NV_IMAGE_BLIT */
	ACCW(PR_CTX0_4, 0x0100805f); /* NVclass $05f, patchcfg ROP_AND, nv10+: little endian */
	ACCW(PR_CTX2_4, 0x00000000); /* DMA0 and DMA1 instance invalid */
	ACCW(PR_CTX3_4, 0x00000000); /* method traps disabled */
	/* setup set '5' for cmd NV3_GDI_RECTANGLE_TEXT */
	ACCW(PR_CTX0_5, 0x0100804b); /* NVclass $04b, patchcfg ROP_AND, nv10+: little endian */
	ACCW(PR_CTX2_5, 0x00000000); /* DMA0 and DMA1 instance invalid */
	ACCW(PR_CTX3_5, 0x00000000); /* method traps disabled */
	/* setup set '6' for cmd NV_RENDER_D3D0_TRIANGLE_ZETA (not used?) */
	ACCW(PR_CTX0_6, 0x0100a048); /* NVclass $048, patchcfg ROP_AND, userclip enable,
								  * nv10+: little endian */
	ACCW(PR_CTX1_6, 0x00000d01); /* format is A8RGB24, MSB mono */
	ACCW(PR_CTX2_6, 0x11401140); /* DMA0, DMA1 instance = $1140 */
	ACCW(PR_CTX3_6, 0x00000000); /* method traps disabled */
	/* setup set '7' ... */
	if (si->ps.card_arch != NV04A)
	{
		/* ... for cmd NV10_DX5_TEXTURE_TRIANGLE */
		ACCW(PR_CTX0_7, 0x0300a094); /* NVclass $094, patchcfg ROP_AND, userclip enable,
									  * context surface0 valid, nv10+: little endian */
	}
	else
	{
		/* ... for cmd NV4_DX5_TEXTURE_TRIANGLE */
		ACCW(PR_CTX0_7, 0x0300a054); /* NVclass $054, patchcfg ROP_AND, userclip enable,
									  * context surface0 valid */
	}
	ACCW(PR_CTX1_7, 0x00000d01); /* format is A8RGB24, MSB mono */
	ACCW(PR_CTX2_7, 0x11401140); /* DMA0, DMA1 instance = $1140 */
	ACCW(PR_CTX3_7, 0x00000000); /* method traps disabled */
	/* setup set '8' ... */
	if (si->ps.card_arch != NV04A)
	{
		/* ... for cmd NV10_DX6_MULTI_TEXTURE_TRIANGLE (not used) */
		ACCW(PR_CTX0_8, 0x0300a095); /* NVclass $095, patchcfg ROP_AND, userclip enable,
									  * context surface0 valid, nv10+: little endian */
	}
	else
	{
		/* ... for cmd NV4_DX6_MULTI_TEXTURE_TRIANGLE (not used) */
		ACCW(PR_CTX0_8, 0x0300a055); /* NVclass $055, patchcfg ROP_AND, userclip enable,
									  * context surface0 valid */
	}
	ACCW(PR_CTX1_8, 0x00000d01); /* format is A8RGB24, MSB mono */
	ACCW(PR_CTX2_8, 0x11401140); /* DMA0, DMA1 instance = $1140 */
	ACCW(PR_CTX3_8, 0x00000000); /* method traps disabled */
	/* setup set '9' for cmd NV3_SURFACE_0 (not used) */
	ACCW(PR_CTX0_9, 0x00000058); /* NVclass $058, nv10+: little endian */
	ACCW(PR_CTX2_9, 0x11401140); /* DMA0, DMA1 instance = $1140 */
	ACCW(PR_CTX3_9, 0x00000000); /* method traps disabled */
	/* setup set 'A' for cmd NV3_SURFACE_1 (not used) */
	ACCW(PR_CTX0_A, 0x00000059); /* NVclass $059, nv10+: little endian */
	ACCW(PR_CTX2_A, 0x11401140); /* DMA0, DMA1 instance = $1140 */
	ACCW(PR_CTX3_A, 0x00000000); /* method traps disabled */
	/* setup set 'B' for cmd NV3_SURFACE_2 (not used) */
	ACCW(PR_CTX0_B, 0x0000005a); /* NVclass $05a, nv10+: little endian */
	ACCW(PR_CTX2_B, 0x11401140); /* DMA0, DMA1 instance = $1140 */
	ACCW(PR_CTX3_B, 0x00000000); /* method traps disabled */
	/* setup set 'C' for cmd NV3_SURFACE_3 (not used) */
	ACCW(PR_CTX0_C, 0x0000005b); /* NVclass $05b, nv10+: little endian */
	ACCW(PR_CTX2_C, 0x11401140); /* DMA0, DMA1 instance = $1140 */
	ACCW(PR_CTX3_C, 0x00000000); /* method traps disabled */
	/* fixme: notes for set 'D' and set 'E':
	 * why not setup NV4_CONTEXT_SURFACES_ARGB_ZS for set 'D' as well??
	 * NV1_RENDER_SOLID_LIN could be moved to set 'E'?? */
	/* setup set 'D' ... */
	if (si->ps.card_arch != NV04A)
	{
		/* ... for cmd NV10_CONTEXT_SURFACES_ARGB_ZS (not used) */
		ACCW(PR_CTX0_D, 0x00000093); /* NVclass $093, nv10+: little endian */
	}
	else
	{
		/* ... for cmd NV1_RENDER_SOLID_LIN (not used) */
		ACCW(PR_CTX0_D, 0x0300a01c); /* NVclass $01c, patchcfg ROP_AND, userclip enable,
									  * context surface0 valid */
	}
	ACCW(PR_CTX2_D, 0x11401140); /* DMA0, DMA1 instance = $1140 */
	ACCW(PR_CTX3_D, 0x00000000); /* method traps disabled */
	/* setup set 'E' if needed ... */
	if (si->ps.card_arch != NV04A)
	{
		/* ... for cmd NV1_RENDER_SOLID_LIN (not used) */
		ACCW(PR_CTX0_E, 0x0300a01c); /* NVclass $01c, patchcfg ROP_AND, userclip enable,
									  * context surface0 valid, nv10+: little endian */
		ACCW(PR_CTX2_E, 0x11401140); /* DMA0, DMA1 instance = $1140 */
		ACCW(PR_CTX3_E, 0x00000000); /* method traps disabled */
	}

	/*** PGRAPH ***/
	if (si->ps.card_arch != NV04A)
	{
		/* set resetstate for most function blocks */
		ACCW(DEBUG0, 0x0003ffff);
		/* init some function blocks */
		ACCW(DEBUG1, 0x00118701);
		ACCW(DEBUG2, 0x24f82ad9);
		ACCW(DEBUG3, 0x55de0030);
		/* end resetstate for the function blocks */
		ACCW(DEBUG0, 0x00000000);
		/* disable specific functions */
		ACCW(NV10_DEBUG4, 0);
	}
	else
	{
		/* init some function blocks */
		ACCW(DEBUG0, 0x1231c001);
		ACCW(DEBUG1, 0x72111101);
		ACCW(DEBUG2, 0x11d5f071);
		ACCW(DEBUG3, 0x10d4ff31);
	}

	/* reset all cache sets */
	ACCW(CACHE1_1, 0);
	ACCW(CACHE1_2, 0);
	ACCW(CACHE1_3, 0);
	ACCW(CACHE1_4, 0);
	ACCW(CACHE1_5, 0);
	ACCW(CACHE2_1, 0);
	ACCW(CACHE2_2, 0);
	ACCW(CACHE2_3, 0);
	ACCW(CACHE2_4, 0);
	ACCW(CACHE2_5, 0);
	ACCW(CACHE3_1, 0);
	ACCW(CACHE3_2, 0);
	ACCW(CACHE3_3, 0);
	ACCW(CACHE3_4, 0);
	ACCW(CACHE3_5, 0);
	ACCW(CACHE4_1, 0);
	ACCW(CACHE4_2, 0);
	ACCW(CACHE4_3, 0);
	ACCW(CACHE4_4, 0);
	ACCW(CACHE4_5, 0);
	if (si->ps.card_arch != NV04A)
		ACCW(NV10_CACHE5_1, 0);
	ACCW(CACHE5_2, 0);
	ACCW(CACHE5_3, 0);
	ACCW(CACHE5_4, 0);
	ACCW(CACHE5_5, 0);
	if (si->ps.card_arch != NV04A)
		ACCW(NV10_CACHE6_1, 0);
	ACCW(CACHE6_2, 0);
	ACCW(CACHE6_3, 0);
	ACCW(CACHE6_4, 0);
	ACCW(CACHE6_5, 0);
	if (si->ps.card_arch != NV04A)
		ACCW(NV10_CACHE7_1, 0);
	ACCW(CACHE7_2, 0);
	ACCW(CACHE7_3, 0);
	ACCW(CACHE7_4, 0);
	ACCW(CACHE7_5, 0);
	if (si->ps.card_arch != NV04A)
		ACCW(NV10_CACHE8_1, 0);
	ACCW(CACHE8_2, 0);
	ACCW(CACHE8_3, 0);
	ACCW(CACHE8_4, 0);
	ACCW(CACHE8_5, 0);

	if (si->ps.card_arch != NV04A)
	{
		/* reset (disable) context switch stuff */
		ACCW(NV10_CTX_SW1, 0);
		ACCW(NV10_CTX_SW2, 0);
		ACCW(NV10_CTX_SW3, 0);
		ACCW(NV10_CTX_SW4, 0);
		ACCW(NV10_CTX_SW5, 0);
	}

	/* setup accesible card memory range for acc engine */
	ACCW(BBASE0, 0x00000000);
	ACCW(BBASE1, 0x00000000);
	ACCW(BBASE2, 0x00000000);
	ACCW(BBASE3, 0x00000000);
	ACCW(BLIMIT0, (si->ps.memory_size - 1));
	ACCW(BLIMIT1, (si->ps.memory_size - 1));
	ACCW(BLIMIT2, (si->ps.memory_size - 1));
	ACCW(BLIMIT3, (si->ps.memory_size - 1));
	if (si->ps.card_arch >= NV10A)
	{
		ACCW(NV10_BBASE4, 0x00000000);
		ACCW(NV10_BBASE5, 0x00000000);
		ACCW(NV10_BLIMIT4, (si->ps.memory_size - 1));
		ACCW(NV10_BLIMIT5, (si->ps.memory_size - 1));
	}
	if (si->ps.card_arch >= NV20A)
	{
		/* fixme(?): assuming more BLIMIT registers here: Then how about BBASE6-9?
		 * (linux fixed value 'BLIMIT6-9' 0x01ffffff) */
		ACCW(NV20_BLIMIT6, (si->ps.memory_size - 1));
		ACCW(NV20_BLIMIT7, (si->ps.memory_size - 1));
		ACCW(NV20_BLIMIT8, (si->ps.memory_size - 1));
		ACCW(NV20_BLIMIT9, (si->ps.memory_size - 1));
	}

	/* disable all acceleration engine INT reguests */
	ACCW(ACC_INTE, 0x00000000);

	/* reset all acceration engine INT status bits */
	ACCW(ACC_INTS, 0xffffffff);
	if (si->ps.card_arch != NV04A)
	{
		/* context control enabled */
		ACCW(NV10_CTX_CTRL, 0x10010100);
		/* all acceleration buffers, pitches and colors are valid */
		ACCW(NV10_ACC_STAT, 0xffffffff);
	}
	else
	{
		/* context control enabled */
		ACCW(NV04_CTX_CTRL, 0x10010100);
		/* all acceleration buffers, pitches and colors are valid */
		ACCW(NV04_ACC_STAT, 0xffffffff);
	}
	/* enable acceleration engine command FIFO */
	ACCW(FIFO_EN, 0x00000001);
	/* pattern shape value = 8x8, 2 color */
	ACCW(PAT_SHP, 0x00000000);
	if (si->ps.card_arch != NV04A)
	{
		/* surface type is non-swizzle */
		ACCW(NV10_SURF_TYP, 0x00000001);
	}
	else
	{
		/* surface type is non-swizzle */
		ACCW(NV04_SURF_TYP, 0x00000001);
	}

	/*** Set pixel width and format ***/
	//fixme (3D?):
	//the BPIXEL register holds the colorspaces for different engine 'contexts' or so.
	//B0-3 is 'channel' 0, b4-7 is 'channel '1', etc.
	//It looks like we are only using channel 0, so the settings for other channels
	//shouldn't matter yet.
	//When for instance rect_fill is going to be used on other buffers than the actual
	//screen, it's colorspace should be corrected. When the engine is setup in 32bit
	//desktop mode for example, the pixel's alpha channel doesn't get touched currently.
	//choose mode $d (which is Y32) to get alpha filled too.
	switch(si->dm.space)
	{
	case B_CMAP8:
		/* acc engine */
		ACCW(FORMATS, 0x00001010);
		if (si->ps.card_arch < NV30A)
			/* set depth 0-5: $1 = Y8 */
			ACCW(BPIXEL, 0x00111111);
		else
			/* set depth 0-1: $1 = Y8, $2 = X1R5G5B5_Z1R5G5B5 */
			ACCW(BPIXEL, 0x00000021);
		ACCW(STRD_FMT, 0x03020202);
		/* PRAMIN */
		ACCW(PR_CTX1_0, 0x00000302); /* format is X24Y8, LSB mono */
		ACCW(PR_CTX1_1, 0x00000302); /* format is X24Y8, LSB mono */
		ACCW(PR_CTX1_2, 0x00000202); /* format is X16A8Y8, LSB mono */
		ACCW(PR_CTX1_3, 0x00000302); /* format is X24Y8, LSB mono */
		ACCW(PR_CTX1_4, 0x00000302); /* format is X24Y8, LSB mono */
		ACCW(PR_CTX1_5, 0x00000302); /* format is X24Y8, LSB mono */
		ACCW(PR_CTX1_9, 0x00000302); /* format is X24Y8, LSB mono */
		ACCW(PR_CTX2_9, 0x00000302); /* dma_instance 0 valid, instance 1 invalid */
		ACCW(PR_CTX1_B, 0x00000000); /* format is invalid */
		ACCW(PR_CTX1_C, 0x00000000); /* format is invalid */
		if (si->ps.card_arch == NV04A)
		{
			ACCW(PR_CTX1_D, 0x00000302); /* format is X24Y8, LSB mono */
		}
		else
		{
			ACCW(PR_CTX1_D, 0x00000000); /* format is invalid */
			ACCW(PR_CTX1_E, 0x00000302); /* format is X24Y8, LSB mono */
		}
		break;
	case B_RGB15_LITTLE:
		/* acc engine */
		ACCW(FORMATS, 0x00002071);
		if (si->ps.card_arch < NV30A)
			/* set depth 0-5: $2 = X1R5G5B5_Z1R5G5B5, $6 = Y16 */
			ACCW(BPIXEL, 0x00226222);
		else
			/* set depth 0-1: $2 = X1R5G5B5_Z1R5G5B5, $4 = A1R5G5B5 */
			ACCW(BPIXEL, 0x00000042);
		ACCW(STRD_FMT, 0x09080808);
		/* PRAMIN */
		ACCW(PR_CTX1_0, 0x00000902); /* format is X17RGB15, LSB mono */
		ACCW(PR_CTX1_1, 0x00000902); /* format is X17RGB15, LSB mono */
		ACCW(PR_CTX1_2, 0x00000802); /* format is X16A1RGB15, LSB mono */
		ACCW(PR_CTX1_3, 0x00000902); /* format is X17RGB15, LSB mono */
		ACCW(PR_CTX1_4, 0x00000902); /* format is X17RGB15, LSB mono */
		ACCW(PR_CTX1_5, 0x00000902); /* format is X17RGB15, LSB mono */
		ACCW(PR_CTX1_9, 0x00000902); /* format is X17RGB15, LSB mono */
		ACCW(PR_CTX2_9, 0x00000902); /* dma_instance 0 valid, instance 1 invalid */
		if (si->ps.card_arch == NV04A)
		{
			ACCW(PR_CTX1_B, 0x00000702); /* format is X1RGB15, LSB mono */
			ACCW(PR_CTX1_C, 0x00000702); /* format is X1RGB15, LSB mono */
		}
		else
		{
			ACCW(PR_CTX1_B, 0x00000902); /* format is X17RGB15, LSB mono */
			ACCW(PR_CTX1_C, 0x00000902); /* format is X17RGB15, LSB mono */
			ACCW(PR_CTX1_E, 0x00000902); /* format is X17RGB15, LSB mono */
		}
		ACCW(PR_CTX1_D, 0x00000902); /* format is X17RGB15, LSB mono */
		break;
	case B_RGB16_LITTLE:
		/* acc engine */
		ACCW(FORMATS, 0x000050C2);
		if (si->ps.card_arch < NV30A)
			/* set depth 0-5: $5 = R5G6B5, $6 = Y16 */
			ACCW(BPIXEL, 0x00556555);
		else
			/* set depth 0-1: $5 = R5G6B5, $a = X1A7R8G8B8_O1A7R8G8B8 */
			ACCW(BPIXEL, 0x000000a5);
		if (si->ps.card_arch == NV04A)
			ACCW(STRD_FMT, 0x0c0b0b0b);
		else
			ACCW(STRD_FMT, 0x000b0b0c);
		/* PRAMIN */
		ACCW(PR_CTX1_0, 0x00000c02); /* format is X16RGB16, LSB mono */
		ACCW(PR_CTX1_1, 0x00000c02); /* format is X16RGB16, LSB mono */
		ACCW(PR_CTX1_2, 0x00000b02); /* format is A16RGB16, LSB mono */
		ACCW(PR_CTX1_3, 0x00000c02); /* format is X16RGB16, LSB mono */
		ACCW(PR_CTX1_4, 0x00000c02); /* format is X16RGB16, LSB mono */
		ACCW(PR_CTX1_5, 0x00000c02); /* format is X16RGB16, LSB mono */
		ACCW(PR_CTX1_9, 0x00000c02); /* format is X16RGB16, LSB mono */
		ACCW(PR_CTX2_9, 0x00000c02); /* dma_instance 0 valid, instance 1 invalid */
		if (si->ps.card_arch == NV04A)
		{
			ACCW(PR_CTX1_B, 0x00000702); /* format is X1RGB15, LSB mono */
			ACCW(PR_CTX1_C, 0x00000702); /* format is X1RGB15, LSB mono */
		}
		else
		{
			ACCW(PR_CTX1_B, 0x00000c02); /* format is X16RGB16, LSB mono */
			ACCW(PR_CTX1_C, 0x00000c02); /* format is X16RGB16, LSB mono */
			ACCW(PR_CTX1_E, 0x00000c02); /* format is X16RGB16, LSB mono */
		}
		ACCW(PR_CTX1_D, 0x00000c02); /* format is X16RGB16, LSB mono */
		break;
	case B_RGB32_LITTLE:case B_RGBA32_LITTLE:
		/* acc engine */
		ACCW(FORMATS, 0x000070e5);
		if (si->ps.card_arch < NV30A)
			/* set depth 0-5: $7 = X8R8G8B8_Z8R8G8B8, $d = Y32 */
			ACCW(BPIXEL, 0x0077d777);
		else
			/* set depth 0-1: $7 = X8R8G8B8_Z8R8G8B8, $e = V8YB8U8YA8 */
			ACCW(BPIXEL, 0x000000e7);
		ACCW(STRD_FMT, 0x0e0d0d0d);
		/* PRAMIN */
		ACCW(PR_CTX1_0, 0x00000e02); /* format is X8RGB24, LSB mono */
		ACCW(PR_CTX1_1, 0x00000e02); /* format is X8RGB24, LSB mono */
		ACCW(PR_CTX1_2, 0x00000d02); /* format is A8RGB24, LSB mono */
		ACCW(PR_CTX1_3, 0x00000e02); /* format is X8RGB24, LSB mono */
		ACCW(PR_CTX1_4, 0x00000e02); /* format is X8RGB24, LSB mono */
		ACCW(PR_CTX1_5, 0x00000e02); /* format is X8RGB24, LSB mono */
		ACCW(PR_CTX1_9, 0x00000e02); /* format is X8RGB24, LSB mono */
		ACCW(PR_CTX2_9, 0x00000e02); /* dma_instance 0 valid, instance 1 invalid */
		ACCW(PR_CTX1_B, 0x00000e02); /* format is X8RGB24, LSB mono */
		ACCW(PR_CTX1_C, 0x00000e02); /* format is X8RGB24, LSB mono */
		ACCW(PR_CTX1_D, 0x00000e02); /* format is X8RGB24, LSB mono */
		if (si->ps.card_arch >= NV10A)
			ACCW(PR_CTX1_E, 0x00000e02); /* format is X8RGB24, LSB mono */
		break;
	default:
		LOG(8,("ACC: init, invalid bit depth\n"));
		return B_ERROR;
	}

	/* setup some extra stuff for NV30A and later */
	if (si->ps.card_arch >= NV30A)
	{
/*
	fixme: Does not belong here (and not needed?)
	if(!chip->flatPanel)
	{
    	chip->PRAMDAC0[0x0578/4] = state->vpllB;	//0x00680578 = ??? never modified!
    	chip->PRAMDAC0[0x057C/4] = state->vpll2B;	//0x0068057c = ??? never modified!
	}
*/

		/* activate Zcullflush(?) */
		ACCW(DEBUG3, (ACCR(DEBUG3) | 0x00000001));
		/* unknown */
		ACCW(NV30_WHAT, (ACCR(NV30_WHAT) | 0x00040000));
	}

	/*** setup screen location and pitch ***/
	switch (si->ps.card_arch)
	{
	case NV04A:
	case NV10A:
		/* location of active screen in framebuffer */
		ACCW(OFFSET0, ((uint8*)si->fbc.frame_buffer - (uint8*)si->framebuffer));
		ACCW(OFFSET1, ((uint8*)si->fbc.frame_buffer - (uint8*)si->framebuffer));
		ACCW(OFFSET2, ((uint8*)si->fbc.frame_buffer - (uint8*)si->framebuffer));
		ACCW(OFFSET3, ((uint8*)si->fbc.frame_buffer - (uint8*)si->framebuffer));
		ACCW(OFFSET4, ((uint8*)si->fbc.frame_buffer - (uint8*)si->framebuffer));
		ACCW(OFFSET5, ((uint8*)si->fbc.frame_buffer - (uint8*)si->framebuffer));

		/* setup buffer pitch */
		ACCW(PITCH0, (si->fbc.bytes_per_row & 0x0000ffff));
		ACCW(PITCH1, (si->fbc.bytes_per_row & 0x0000ffff));
		ACCW(PITCH2, (si->fbc.bytes_per_row & 0x0000ffff));
		ACCW(PITCH3, (si->fbc.bytes_per_row & 0x0000ffff));
		ACCW(PITCH4, (si->fbc.bytes_per_row & 0x0000ffff));
		break;
	case NV20A:
	case NV30A:
	case NV40A:
		/* location of active screen in framebuffer */
		ACCW(NV20_OFFSET0, ((uint8*)si->fbc.frame_buffer - (uint8*)si->framebuffer));
		ACCW(NV20_OFFSET1, ((uint8*)si->fbc.frame_buffer - (uint8*)si->framebuffer));
		ACCW(NV20_OFFSET2, ((uint8*)si->fbc.frame_buffer - (uint8*)si->framebuffer));
		ACCW(NV20_OFFSET3, ((uint8*)si->fbc.frame_buffer - (uint8*)si->framebuffer));

		/* setup buffer pitch */
		ACCW(NV20_PITCH0, (si->fbc.bytes_per_row & 0x0000ffff));
		ACCW(NV20_PITCH1, (si->fbc.bytes_per_row & 0x0000ffff));
		ACCW(NV20_PITCH2, (si->fbc.bytes_per_row & 0x0000ffff));
		ACCW(NV20_PITCH3, (si->fbc.bytes_per_row & 0x0000ffff));
		break;
	}

	/*** setup tile and pipe stuff ***/
	if (si->ps.card_arch >= NV10A)
	{
/*
	fixme: setup elsewhere (does not belong here):
	chip->PRAMDAC[0x00000404/4] |= (1 << 25);//0x00680404 = ???
*/

		/* setup acc engine tile stuff: */
		/* reset tile adresses */
		ACCW(NV10_FBTIL0AD, 0);
		ACCW(NV10_FBTIL1AD, 0);
		ACCW(NV10_FBTIL2AD, 0);
		ACCW(NV10_FBTIL3AD, 0);
		ACCW(NV10_FBTIL4AD, 0);
		ACCW(NV10_FBTIL5AD, 0);
		ACCW(NV10_FBTIL6AD, 0);
		ACCW(NV10_FBTIL7AD, 0);
		/* copy tile setup stuff from 'source' to acc engine */
		if (si->ps.card_arch >= NV20A)
		{
			/* unknown: */
			ACCW(NV20_WHAT0, ACCR(NV20_FBWHAT0));
			ACCW(NV20_WHAT1, ACCR(NV20_FBWHAT1));
		}
		/* tile 0: */
		/* tile invalid, tile adress = $00000 (18bit) */
		ACCW(NV10_TIL0AD, ACCR(NV10_FBTIL0AD));
		/* set tile end adress (18bit) */
		ACCW(NV10_TIL0ED, ACCR(NV10_FBTIL0ED));
		/* set tile size pitch (8bit: b8-15) */
		ACCW(NV10_TIL0PT, ACCR(NV10_FBTIL0PT));
		/* set tile status */
		ACCW(NV10_TIL0ST, ACCR(NV10_FBTIL0ST));
		/* tile 1: */
		ACCW(NV10_TIL1AD, ACCR(NV10_FBTIL1AD));
		ACCW(NV10_TIL1ED, ACCR(NV10_FBTIL1ED));
		ACCW(NV10_TIL1PT, ACCR(NV10_FBTIL1PT));
		ACCW(NV10_TIL1ST, ACCR(NV10_FBTIL1ST));
		/* tile 2: */
		ACCW(NV10_TIL2AD, ACCR(NV10_FBTIL2AD));
		ACCW(NV10_TIL2ED, ACCR(NV10_FBTIL2ED));
		ACCW(NV10_TIL2PT, ACCR(NV10_FBTIL2PT));
		ACCW(NV10_TIL2ST, ACCR(NV10_FBTIL2ST));
		/* tile 3: */
		ACCW(NV10_TIL3AD, ACCR(NV10_FBTIL3AD));
		ACCW(NV10_TIL3ED, ACCR(NV10_FBTIL3ED));
		ACCW(NV10_TIL3PT, ACCR(NV10_FBTIL3PT));
		ACCW(NV10_TIL3ST, ACCR(NV10_FBTIL3ST));
		/* tile 4: */
		ACCW(NV10_TIL4AD, ACCR(NV10_FBTIL4AD));
		ACCW(NV10_TIL4ED, ACCR(NV10_FBTIL4ED));
		ACCW(NV10_TIL4PT, ACCR(NV10_FBTIL4PT));
		ACCW(NV10_TIL4ST, ACCR(NV10_FBTIL4ST));
		/* tile 5: */
		ACCW(NV10_TIL5AD, ACCR(NV10_FBTIL5AD));
		ACCW(NV10_TIL5ED, ACCR(NV10_FBTIL5ED));
		ACCW(NV10_TIL5PT, ACCR(NV10_FBTIL5PT));
		ACCW(NV10_TIL5ST, ACCR(NV10_FBTIL5ST));
		/* tile 6: */
		ACCW(NV10_TIL6AD, ACCR(NV10_FBTIL6AD));
		ACCW(NV10_TIL6ED, ACCR(NV10_FBTIL6ED));
		ACCW(NV10_TIL6PT, ACCR(NV10_FBTIL6PT));
		ACCW(NV10_TIL6ST, ACCR(NV10_FBTIL6ST));
		/* tile 7: */
		ACCW(NV10_TIL7AD, ACCR(NV10_FBTIL7AD));
		ACCW(NV10_TIL7ED, ACCR(NV10_FBTIL7ED));
		ACCW(NV10_TIL7PT, ACCR(NV10_FBTIL7PT));
		ACCW(NV10_TIL7ST, ACCR(NV10_FBTIL7ST));

		/* setup (clear) pipe */
		/* set eyetype to local, lightning is off */
		ACCW(NV10_XFMOD0, 0x10000000);
		/* disable all lights */
		ACCW(NV10_XFMOD1, 0x00000000);

		ACCW(NV10_PIPEADR, 0x00000040);
		ACCW(NV10_PIPEDAT, 0x00000008);

		/* note: upon writing data into the PIPEDAT register, the PIPEADR is
		 * probably auto-incremented! */
		ACCW(NV10_PIPEADR, 0x00000200);
		for (cnt = 0; cnt < (3 * 16); cnt++) ACCW(NV10_PIPEDAT, 0x00000000);

		ACCW(NV10_PIPEADR, 0x00000040);
		ACCW(NV10_PIPEDAT, 0x00000000);

		ACCW(NV10_PIPEADR, 0x00000800);
		for (cnt = 0; cnt < (16 * 16); cnt++) ACCW(NV10_PIPEDAT, 0x00000000);

		/* turn lightning on */
		ACCW(NV10_XFMOD0, 0x30000000);
		/* set light 1 to infinite type, other lights remain off */
		ACCW(NV10_XFMOD1, 0x00000004);

		ACCW(NV10_PIPEADR, 0x00006400);
		for (cnt = 0; cnt < (59 * 4); cnt++) ACCW(NV10_PIPEDAT, 0x00000000);

		ACCW(NV10_PIPEADR, 0x00006800);
		for (cnt = 0; cnt < (47 * 4); cnt++) ACCW(NV10_PIPEDAT, 0x00000000);

		ACCW(NV10_PIPEADR, 0x00006c00);
		for (cnt = 0; cnt < (3 * 4); cnt++) ACCW(NV10_PIPEDAT, 0x00000000);

		ACCW(NV10_PIPEADR, 0x00007000);
		for (cnt = 0; cnt < (19 * 4); cnt++) ACCW(NV10_PIPEDAT, 0x00000000);

		ACCW(NV10_PIPEADR, 0x00007400);
		for (cnt = 0; cnt < (12 * 4); cnt++) ACCW(NV10_PIPEDAT, 0x00000000);

		ACCW(NV10_PIPEADR, 0x00007800);
		for (cnt = 0; cnt < (12 * 4); cnt++) ACCW(NV10_PIPEDAT, 0x00000000);

		ACCW(NV10_PIPEADR, 0x00004400);
		for (cnt = 0; cnt < (8 * 4); cnt++) ACCW(NV10_PIPEDAT, 0x00000000);

		ACCW(NV10_PIPEADR, 0x00000000);
		for (cnt = 0; cnt < 16; cnt++) ACCW(NV10_PIPEDAT, 0x00000000);

		ACCW(NV10_PIPEADR, 0x00000040);
		for (cnt = 0; cnt < 4; cnt++) ACCW(NV10_PIPEDAT, 0x00000000);
	}

	/* setup 3D specifics */
	nv_init_for_3D();

	/*** setup acceleration engine command shortcuts (so via fifo) ***/
	/* set object handles (b31 = 1 selects 'config' function?) */
	/* note:
	 * probably depending on some other setup, there are 8 or 32 FIFO channels
	 * available. Assuming the current setup only has 8 channels because the 'rest'
	 * isn't setup here... */
	si->engine.fifo.handle[0] = NV_ROP5_SOLID;
	si->engine.fifo.handle[1] = NV_IMAGE_BLACK_RECTANGLE;
	si->engine.fifo.handle[2] = NV_IMAGE_PATTERN;
	si->engine.fifo.handle[3] = NV1_IMAGE_FROM_CPU;
	si->engine.fifo.handle[4] = NV_IMAGE_BLIT;
	si->engine.fifo.handle[5] = NV3_GDI_RECTANGLE_TEXT;
	si->engine.fifo.handle[6] = NV1_RENDER_SOLID_LIN;
	si->engine.fifo.handle[7] = NV4_DX5_TEXTURE_TRIANGLE;
	/* preset no FIFO channels assigned to cmd's */
	for (cnt = 0; cnt < 0x20; cnt++)
	{
		si->engine.fifo.ch_ptr[cnt] = 0;
	}
	/* set handle's pointers to their assigned FIFO channels */
	for (cnt = 0; cnt < 0x08; cnt++)
	{
		si->engine.fifo.ch_ptr[((si->engine.fifo.handle[cnt]) & 0x0000001f)] =
			(NVACC_FIFO + (cnt * 0x00002000));
	}
	/* program FIFO assignments */
	ACCW(FIFO_CH0, si->engine.fifo.handle[0]); /* Raster OPeration */
	ACCW(FIFO_CH1, si->engine.fifo.handle[1]); /* Clip */
	ACCW(FIFO_CH2, si->engine.fifo.handle[2]); /* Pattern */
	ACCW(FIFO_CH3, si->engine.fifo.handle[3]); /* Pixmap (not used or 3D only?) */
	ACCW(FIFO_CH4, si->engine.fifo.handle[4]); /* Blit */
	ACCW(FIFO_CH5, si->engine.fifo.handle[5]); /* Bitmap */
	ACCW(FIFO_CH6, si->engine.fifo.handle[6]); /* Line (not used or 3D only?) */
	ACCW(FIFO_CH7, si->engine.fifo.handle[7]); /* Textured Triangle (3D only) */

	/* initialize our local pointers */
	nv_acc_assert_fifo();

	/* do first actual acceleration engine command:
	 * setup clipping region (workspace size) to 32768 x 32768 pixels:
	 * wait for room in fifo for clipping cmd if needed.
	 * (fifo holds 256 32bit words: count those, not bytes) */
	while (((nv_image_black_rectangle_ptr->FifoFree) >> 2) < 2)
	{
		/* snooze a bit so I do not hammer the bus */
		snooze (10); 
	}
	/* now setup clipping (writing 2 32bit words) */
	nv_image_black_rectangle_ptr->TopLeft = 0x00000000;
	nv_image_black_rectangle_ptr->HeightWidth = 0x80008000;

	return B_OK;
}

static void nv_init_for_3D(void)
{
	/* setup PGRAPH unknown registers and modify (pre-cleared) pipe stuff for 3D use */
	if (si->ps.card_arch >= NV10A)
	{
		/* setup unknown PGRAPH stuff */
		ACCW(PGWHAT_00, 0x00000000);
		ACCW(PGWHAT_01, 0x00000000);
		ACCW(PGWHAT_02, 0x00000000);
		ACCW(PGWHAT_03, 0x00000000);

		ACCW(PGWHAT_04, 0x00001000);
		ACCW(PGWHAT_05, 0x00001000);
		ACCW(PGWHAT_06, 0x4003ff80);

		ACCW(PGWHAT_07, 0x00000000);
		ACCW(PGWHAT_08, 0x00000000);
		ACCW(PGWHAT_09, 0x00000000);
		ACCW(PGWHAT_0A, 0x00000000);
		ACCW(PGWHAT_0B, 0x00000000);

		ACCW(PGWHAT_0C, 0x00080008);
		ACCW(PGWHAT_0D, 0x00080008);

		ACCW(PGWHAT_0E, 0x00000000);
		ACCW(PGWHAT_0F, 0x00000000);
		ACCW(PGWHAT_10, 0x00000000);
		ACCW(PGWHAT_11, 0x00000000);
		ACCW(PGWHAT_12, 0x00000000);
		ACCW(PGWHAT_13, 0x00000000);
		ACCW(PGWHAT_14, 0x00000000);
		ACCW(PGWHAT_15, 0x00000000);
		ACCW(PGWHAT_16, 0x00000000);
		ACCW(PGWHAT_17, 0x00000000);
		ACCW(PGWHAT_18, 0x00000000);

		ACCW(PGWHAT_19, 0x10000000);

		ACCW(PGWHAT_1A, 0x00000000);
		ACCW(PGWHAT_1B, 0x00000000);
		ACCW(PGWHAT_1C, 0x00000000);
		ACCW(PGWHAT_1D, 0x00000000);
		ACCW(PGWHAT_1E, 0x00000000);
		ACCW(PGWHAT_1F, 0x00000000);
		ACCW(PGWHAT_20, 0x00000000);
		ACCW(PGWHAT_21, 0x00000000);

		ACCW(PGWHAT_22, 0x08000000);

		ACCW(PGWHAT_23, 0x00000000);
		ACCW(PGWHAT_24, 0x00000000);
		ACCW(PGWHAT_25, 0x00000000);
		ACCW(PGWHAT_26, 0x00000000);

		ACCW(PGWHAT_27, 0x4b7fffff);

		ACCW(PGWHAT_28, 0x00000000);
		ACCW(PGWHAT_29, 0x00000000);
		ACCW(PGWHAT_2A, 0x00000000);

		/* setup window clipping */
		/* b0-11 = min; b16-27 = max.
		 * note:
		 * probably two's complement values, so setting to max range here:
		 * which would be -2048 upto/including +2047. */
		/* horizontal */
		ACCW(WINCLIP_H_0, 0x07ff0800);
		ACCW(WINCLIP_H_1, 0x07ff0800);
		ACCW(WINCLIP_H_2, 0x07ff0800);
		ACCW(WINCLIP_H_3, 0x07ff0800);
		ACCW(WINCLIP_H_4, 0x07ff0800);
		ACCW(WINCLIP_H_5, 0x07ff0800);
		ACCW(WINCLIP_H_6, 0x07ff0800);
		ACCW(WINCLIP_H_7, 0x07ff0800);
		/* vertical */
		ACCW(WINCLIP_V_0, 0x07ff0800);
		ACCW(WINCLIP_V_1, 0x07ff0800);
		ACCW(WINCLIP_V_2, 0x07ff0800);
		ACCW(WINCLIP_V_3, 0x07ff0800);
		ACCW(WINCLIP_V_4, 0x07ff0800);
		ACCW(WINCLIP_V_5, 0x07ff0800);
		ACCW(WINCLIP_V_6, 0x07ff0800);
		ACCW(WINCLIP_V_7, 0x07ff0800);

		/* setup (initialize) pipe */
		/* set eyetype to local, lightning etc. is off */
		ACCW(NV10_XFMOD0, 0x10000000);
		/* disable all lights */
		ACCW(NV10_XFMOD1, 0x00000000);

		/* note: upon writing data into the PIPEDAT register, the PIPEADR is
		 * probably auto-incremented! */
		/* (pipe adress = b2-16, pipe data = b0-31) */
		ACCW(NV10_PIPEADR, 0x00006740);
		ACCW(NV10_PIPEDAT, 0x00000000);
		ACCW(NV10_PIPEDAT, 0x00000000);
		ACCW(NV10_PIPEDAT, 0x00000000);
		ACCW(NV10_PIPEDAT, 0x3f800000);

		ACCW(NV10_PIPEADR, 0x00006750);
		ACCW(NV10_PIPEDAT, 0x40000000);
		ACCW(NV10_PIPEDAT, 0x40000000);
		ACCW(NV10_PIPEDAT, 0x40000000);
		ACCW(NV10_PIPEDAT, 0x40000000);

		ACCW(NV10_PIPEADR, 0x00006760);
		ACCW(NV10_PIPEDAT, 0x00000000);
		ACCW(NV10_PIPEDAT, 0x00000000);
		ACCW(NV10_PIPEDAT, 0x3f800000);
		ACCW(NV10_PIPEDAT, 0x00000000);

		ACCW(NV10_PIPEADR, 0x00006770);
		ACCW(NV10_PIPEDAT, 0xc5000000);
		ACCW(NV10_PIPEDAT, 0xc5000000);
		ACCW(NV10_PIPEDAT, 0x00000000);
		ACCW(NV10_PIPEDAT, 0x00000000);

		ACCW(NV10_PIPEADR, 0x00006780);
		ACCW(NV10_PIPEDAT, 0x00000000);
		ACCW(NV10_PIPEDAT, 0x00000000);
		ACCW(NV10_PIPEDAT, 0x3f800000);
		ACCW(NV10_PIPEDAT, 0x00000000);

		ACCW(NV10_PIPEADR, 0x000067a0);
		ACCW(NV10_PIPEDAT, 0x3f800000);
		ACCW(NV10_PIPEDAT, 0x3f800000);
		ACCW(NV10_PIPEDAT, 0x3f800000);
		ACCW(NV10_PIPEDAT, 0x3f800000);

		ACCW(NV10_PIPEADR, 0x00006ab0);
		ACCW(NV10_PIPEDAT, 0x3f800000);
		ACCW(NV10_PIPEDAT, 0x3f800000);
		ACCW(NV10_PIPEDAT, 0x3f800000);

		ACCW(NV10_PIPEADR, 0x00006ac0);
		ACCW(NV10_PIPEDAT, 0x00000000);
		ACCW(NV10_PIPEDAT, 0x00000000);
		ACCW(NV10_PIPEDAT, 0x00000000);

		ACCW(NV10_PIPEADR, 0x00006c10);
		ACCW(NV10_PIPEDAT, 0xbf800000);

		ACCW(NV10_PIPEADR, 0x00007030);
		ACCW(NV10_PIPEDAT, 0x7149f2ca);

		ACCW(NV10_PIPEADR, 0x00007040);
		ACCW(NV10_PIPEDAT, 0x7149f2ca);

		ACCW(NV10_PIPEADR, 0x00007050);
		ACCW(NV10_PIPEDAT, 0x7149f2ca);

		ACCW(NV10_PIPEADR, 0x00007060);
		ACCW(NV10_PIPEDAT, 0x7149f2ca);

		ACCW(NV10_PIPEADR, 0x00007070);
		ACCW(NV10_PIPEDAT, 0x7149f2ca);

		ACCW(NV10_PIPEADR, 0x00007080);
		ACCW(NV10_PIPEDAT, 0x7149f2ca);

		ACCW(NV10_PIPEADR, 0x00007090);
		ACCW(NV10_PIPEDAT, 0x7149f2ca);

		ACCW(NV10_PIPEADR, 0x000070a0);
		ACCW(NV10_PIPEDAT, 0x7149f2ca);

		ACCW(NV10_PIPEADR, 0x00006a80);
		ACCW(NV10_PIPEDAT, 0x00000000);
		ACCW(NV10_PIPEDAT, 0x00000000);
		ACCW(NV10_PIPEDAT, 0x3f800000);

		ACCW(NV10_PIPEADR, 0x00006aa0);
		ACCW(NV10_PIPEDAT, 0x00000000);
		ACCW(NV10_PIPEDAT, 0x00000000);
		ACCW(NV10_PIPEDAT, 0x00000000);

		ACCW(NV10_PIPEADR, 0x00000040);
		ACCW(NV10_PIPEDAT, 0x00000005);

		ACCW(NV10_PIPEADR, 0x00006400);
		ACCW(NV10_PIPEDAT, 0x3f800000);
		ACCW(NV10_PIPEDAT, 0x3f800000);
		ACCW(NV10_PIPEDAT, 0x4b7fffff);
		ACCW(NV10_PIPEDAT, 0x00000000);

		ACCW(NV10_PIPEADR, 0x00006410);
		ACCW(NV10_PIPEDAT, 0xc5000000);
		ACCW(NV10_PIPEDAT, 0xc5000000);
		ACCW(NV10_PIPEDAT, 0x00000000);
		ACCW(NV10_PIPEDAT, 0x00000000);

		ACCW(NV10_PIPEADR, 0x00006420);
		ACCW(NV10_PIPEDAT, 0x00000000);
		ACCW(NV10_PIPEDAT, 0x00000000);
		ACCW(NV10_PIPEDAT, 0x00000000);
		ACCW(NV10_PIPEDAT, 0x00000000);

		ACCW(NV10_PIPEADR, 0x00006430);
		ACCW(NV10_PIPEDAT, 0x00000000);
		ACCW(NV10_PIPEDAT, 0x00000000);
		ACCW(NV10_PIPEDAT, 0x00000000);
		ACCW(NV10_PIPEDAT, 0x00000000);

		ACCW(NV10_PIPEADR, 0x000064c0);
		ACCW(NV10_PIPEDAT, 0x3f800000);
		ACCW(NV10_PIPEDAT, 0x3f800000);
		ACCW(NV10_PIPEDAT, 0x477fffff);
		ACCW(NV10_PIPEDAT, 0x3f800000);

		ACCW(NV10_PIPEADR, 0x000064d0);
		ACCW(NV10_PIPEDAT, 0xc5000000);
		ACCW(NV10_PIPEDAT, 0xc5000000);
		ACCW(NV10_PIPEDAT, 0x00000000);
		ACCW(NV10_PIPEDAT, 0x00000000);

		ACCW(NV10_PIPEADR, 0x000064e0);
		ACCW(NV10_PIPEDAT, 0xc4fff000);
		ACCW(NV10_PIPEDAT, 0xc4fff000);
		ACCW(NV10_PIPEDAT, 0x00000000);
		ACCW(NV10_PIPEDAT, 0x00000000);

		ACCW(NV10_PIPEADR, 0x000064f0);
		ACCW(NV10_PIPEDAT, 0x00000000);
		ACCW(NV10_PIPEDAT, 0x00000000);
		ACCW(NV10_PIPEDAT, 0x00000000);
		ACCW(NV10_PIPEDAT, 0x00000000);

		/* turn lightning on */
		ACCW(NV10_XFMOD0, 0x30000000);
		/* set light 1 to infinite type, other lights remain off */
		ACCW(NV10_XFMOD1, 0x00000004);

		/* Z-buffer state is:
		 * initialized, set to: 'fixed point' (integer?); Z-buffer; 16bits depth */
		/* note:
		 * other options possible are: floating point; 24bits depth; W-buffer(?) */
		ACCW(GLOB_STAT_0, 0x10000000);
		/* set DMA instance 2 and 3 to be invalid */
		ACCW(GLOB_STAT_1, 0x00000000);
	}
}

/* fixme? (check this out..)
 * Looks like this stuff can be very much simplified and speed-up, as it seems it's not
 * nessesary to wait for the engine to become idle before re-assigning channels.
 * Because the cmd handles are actually programmed _inside_ the fifo channels, it might
 * well be that the assignment is buffered along with the commands that still have to 
 * be executed!
 * (sounds very plausible to me :) */
void nv_acc_assert_fifo(void)
{
	/* does every engine cmd this accelerant needs have a FIFO channel? */
	//fixme: can probably be optimized for both speed and channel selection...
	if (!si->engine.fifo.ch_ptr[(NV_ROP5_SOLID & 0x0000001f)] ||
		!si->engine.fifo.ch_ptr[(NV_IMAGE_BLACK_RECTANGLE & 0x0000001f)] ||
		!si->engine.fifo.ch_ptr[(NV_IMAGE_PATTERN & 0x0000001f)] ||
		!si->engine.fifo.ch_ptr[(NV_IMAGE_BLIT & 0x0000001f)] ||
		!si->engine.fifo.ch_ptr[(NV3_GDI_RECTANGLE_TEXT & 0x0000001f)])
	{
		uint16 cnt;

		/* no, wait until the engine is idle before re-assigning the FIFO */
		nv_acc_wait_idle();

		/* free the FIFO channels we want from the currently assigned cmd's */
		si->engine.fifo.ch_ptr[(si->engine.fifo.handle[0] & 0x0000001f)] = 0;
		si->engine.fifo.ch_ptr[(si->engine.fifo.handle[1] & 0x0000001f)] = 0;
		si->engine.fifo.ch_ptr[(si->engine.fifo.handle[2] & 0x0000001f)] = 0;
		si->engine.fifo.ch_ptr[(si->engine.fifo.handle[4] & 0x0000001f)] = 0;
		si->engine.fifo.ch_ptr[(si->engine.fifo.handle[5] & 0x0000001f)] = 0;

		/* set new object handles */
		si->engine.fifo.handle[0] = NV_ROP5_SOLID;
		si->engine.fifo.handle[1] = NV_IMAGE_BLACK_RECTANGLE;
		si->engine.fifo.handle[2] = NV_IMAGE_PATTERN;
		si->engine.fifo.handle[4] = NV_IMAGE_BLIT;
		si->engine.fifo.handle[5] = NV3_GDI_RECTANGLE_TEXT;

		/* set handle's pointers to their assigned FIFO channels */
		for (cnt = 0; cnt < 0x08; cnt++)
		{
			si->engine.fifo.ch_ptr[((si->engine.fifo.handle[cnt]) & 0x0000001f)] =
				(NVACC_FIFO + (cnt * 0x00002000));
		}

		/* program new FIFO assignments */
		ACCW(FIFO_CH0, si->engine.fifo.handle[0]); /* Raster OPeration */
		ACCW(FIFO_CH1, si->engine.fifo.handle[1]); /* Clip */
		ACCW(FIFO_CH2, si->engine.fifo.handle[2]); /* Pattern */
		ACCW(FIFO_CH4, si->engine.fifo.handle[4]); /* Blit */
		ACCW(FIFO_CH5, si->engine.fifo.handle[5]); /* Bitmap */
	}

	/* update our local pointers */
	nv_rop5_solid_ptr = (cmd_nv_rop5_solid*)
		&(regs[(si->engine.fifo.ch_ptr[(NV_ROP5_SOLID & 0x0000001f)]) >> 2]);

	nv_image_black_rectangle_ptr = (cmd_nv_image_black_rectangle*)
		&(regs[(si->engine.fifo.ch_ptr[(NV_IMAGE_BLACK_RECTANGLE & 0x0000001f)]) >> 2]);

	nv_image_pattern_ptr = (cmd_nv_image_pattern*)
		&(regs[(si->engine.fifo.ch_ptr[(NV_IMAGE_PATTERN & 0x0000001f)]) >> 2]);

	nv_image_blit_ptr = (cmd_nv_image_blit*)
		&(regs[(si->engine.fifo.ch_ptr[(NV_IMAGE_BLIT & 0x0000001f)]) >> 2]);

	nv3_gdi_rectangle_text_ptr = (cmd_nv3_gdi_rectangle_text*)
		&(regs[(si->engine.fifo.ch_ptr[(NV3_GDI_RECTANGLE_TEXT & 0x0000001f)]) >> 2]);
}

/* screen to screen blit - i.e. move windows around and scroll within them. */
status_t nv_acc_setup_blit()
{
	/* setup solid pattern:
	 * wait for room in fifo for pattern cmd if needed.
	 * (fifo holds 256 32bit words: count those, not bytes) */
	while (((nv_image_pattern_ptr->FifoFree) >> 2) < 5)
	{
		/* snooze a bit so I do not hammer the bus */
		snooze (10); 
	}
	/* now setup pattern (writing 5 32bit words) */
	nv_image_pattern_ptr->SetShape = 0x00000000; /* 0 = 8x8, 1 = 64x1, 2 = 1x64 */
	nv_image_pattern_ptr->SetColor0 = 0xffffffff;
	nv_image_pattern_ptr->SetColor1 = 0xffffffff;
	nv_image_pattern_ptr->SetPattern[0] = 0xffffffff;
	nv_image_pattern_ptr->SetPattern[1] = 0xffffffff;

	/* ROP registers (Raster OPeration):
	 * wait for room in fifo for ROP cmd if needed.
	 * (fifo holds 256 32bit words: count those, not bytes) */
	while (((nv_rop5_solid_ptr->FifoFree) >> 2) < 1)
	{
		/* snooze a bit so I do not hammer the bus */
		snooze (10);
	}
	/* now setup ROP (writing 1 32bit word) */
	nv_rop5_solid_ptr->SetRop5 = 0xcc;

	return B_OK;
}

status_t nv_acc_blit(uint16 xs,uint16 ys,uint16 xd,uint16 yd,uint16 w,uint16 h)
{
	/* Note: blit-copy direction is determined inside riva hardware: no setup needed */

	/* instruct engine what to blit:
	 * wait for room in fifo for blit cmd if needed.
	 * (fifo holds 256 32bit words: count those, not bytes) */
	while (((nv_image_blit_ptr->FifoFree) >> 2) < 3)
	{
		/* snooze a bit so I do not hammer the bus */
		snooze (10); 
	}
	/* now setup blit (writing 3 32bit words) */
	nv_image_blit_ptr->SourceOrg = ((ys << 16) | xs);
	nv_image_blit_ptr->DestOrg = ((yd << 16) | xd);
	nv_image_blit_ptr->HeightWidth = (((h + 1) << 16) | (w + 1));

	return B_OK;
}

/* rectangle fill - i.e. workspace and window background color */
/* span fill - i.e. (selected) menuitem background color (Dano) */
status_t nv_acc_setup_rectangle(uint32 color)
{
	/* setup solid pattern:
	 * wait for room in fifo for pattern cmd if needed.
	 * (fifo holds 256 32bit words: count those, not bytes) */
	while (((nv_image_pattern_ptr->FifoFree) >> 2) < 5)
	{
		/* snooze a bit so I do not hammer the bus */
		snooze (10); 
	}
	/* now setup pattern (writing 5 32bit words) */
	nv_image_pattern_ptr->SetShape = 0x00000000; /* 0 = 8x8, 1 = 64x1, 2 = 1x64 */
	nv_image_pattern_ptr->SetColor0 = 0xffffffff;
	nv_image_pattern_ptr->SetColor1 = 0xffffffff;
	nv_image_pattern_ptr->SetPattern[0] = 0xffffffff;
	nv_image_pattern_ptr->SetPattern[1] = 0xffffffff;

	/* ROP registers (Raster OPeration):
	 * wait for room in fifo for ROP cmd if needed.
	 * (fifo holds 256 32bit words: count those, not bytes) */
	while (((nv_rop5_solid_ptr->FifoFree) >> 2) < 1)
	{
		/* snooze a bit so I do not hammer the bus */
		snooze (10); 
	}
	/* now setup ROP (writing 1 32bit word) for GXcopy */
	nv_rop5_solid_ptr->SetRop5 = 0xcc;

	/* setup fill color:
	 * wait for room in fifo for bitmap cmd if needed.
	 * (fifo holds 256 32bit words: count those, not bytes) */
	while (((nv3_gdi_rectangle_text_ptr->FifoFree) >> 2) < 1)
	{
		/* snooze a bit so I do not hammer the bus */
		snooze (10);
	}
	/* now setup color (writing 1 32bit word) */
	nv3_gdi_rectangle_text_ptr->Color1A = color;

	return B_OK;
}

status_t nv_acc_rectangle(uint32 xs,uint32 xe,uint32 ys,uint32 yl)
{
	/* instruct engine what to fill:
	 * wait for room in fifo for bitmap cmd if needed.
	 * (fifo holds 256 32bit words: count those, not bytes) */
	while (((nv3_gdi_rectangle_text_ptr->FifoFree) >> 2) < 2)
	{
		/* snooze a bit so I do not hammer the bus */
		snooze (10); 
	}
	/* now setup fill (writing 2 32bit words) */
	nv3_gdi_rectangle_text_ptr->UnclippedRectangle[0].LeftTop =
		((xs << 16) | (ys & 0x0000ffff));
	nv3_gdi_rectangle_text_ptr->UnclippedRectangle[0].WidthHeight =
		(((xe - xs) << 16) | (yl & 0x0000ffff));

	return B_OK;
}

/* rectangle invert - i.e. text cursor and text selection */
status_t nv_acc_setup_rect_invert()
{
	/* setup solid pattern:
	 * wait for room in fifo for pattern cmd if needed.
	 * (fifo holds 256 32bit words: count those, not bytes) */
	while (((nv_image_pattern_ptr->FifoFree) >> 2) < 5)
	{
		/* snooze a bit so I do not hammer the bus */
		snooze (10); 
	}
	/* now setup pattern (writing 5 32bit words) */
	nv_image_pattern_ptr->SetShape = 0x00000000; /* 0 = 8x8, 1 = 64x1, 2 = 1x64 */
	nv_image_pattern_ptr->SetColor0 = 0xffffffff;
	nv_image_pattern_ptr->SetColor1 = 0xffffffff;
	nv_image_pattern_ptr->SetPattern[0] = 0xffffffff;
	nv_image_pattern_ptr->SetPattern[1] = 0xffffffff;

	/* ROP registers (Raster OPeration):
	 * wait for room in fifo for ROP cmd if needed.
	 * (fifo holds 256 32bit words: count those, not bytes) */
	while (((nv_rop5_solid_ptr->FifoFree) >> 2) < 1)
	{
		/* snooze a bit so I do not hammer the bus */
		snooze (10); 
	}
	/* now setup ROP (writing 1 32bit word) for GXinvert */
	nv_rop5_solid_ptr->SetRop5 = 0x55;

	/* reset fill color:
	 * wait for room in fifo for bitmap cmd if needed.
	 * (fifo holds 256 32bit words: count those, not bytes) */
	while (((nv3_gdi_rectangle_text_ptr->FifoFree) >> 2) < 1)
	{
		/* snooze a bit so I do not hammer the bus */
		snooze (10); 
	}
	/* now reset color (writing 1 32bit word) */
	nv3_gdi_rectangle_text_ptr->Color1A = 0x00000000;

	return B_OK;
}

status_t nv_acc_rectangle_invert(uint32 xs,uint32 xe,uint32 ys,uint32 yl)
{
	/* instruct engine what to invert:
	 * wait for room in fifo for bitmap cmd if needed.
	 * (fifo holds 256 32bit words: count those, not bytes) */
	while (((nv3_gdi_rectangle_text_ptr->FifoFree) >> 2) < 2)
	{
		/* snooze a bit so I do not hammer the bus */
		snooze (10); 
	}
	/* now setup invert (writing 2 32bit words) */
	nv3_gdi_rectangle_text_ptr->UnclippedRectangle[0].LeftTop =
		((xs << 16) | (ys & 0x0000ffff));
	nv3_gdi_rectangle_text_ptr->UnclippedRectangle[0].WidthHeight =
		(((xe - xs) << 16) | (yl & 0x0000ffff));

	return B_OK;
}

/* screen to screen tranparent blit */
status_t nv_acc_transparent_blit(uint16 xs,uint16 ys,uint16 xd,uint16 yd,uint16 w,uint16 h,uint32 colour)
{
	//fixme: implement.

	return B_ERROR;
}

/* screen to screen scaled filtered blit - i.e. scale video in memory */
status_t nv_acc_video_blit(uint16 xs,uint16 ys,uint16 ws, uint16 hs,
	uint16 xd,uint16 yd,uint16 wd,uint16 hd)
{
	//fixme: implement.

	return B_ERROR;
}
