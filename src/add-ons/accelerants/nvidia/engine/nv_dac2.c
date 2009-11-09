/* program the secondary DAC */
/* Author:
   Rudolf Cornelissen 12/2003-9/2009
*/

#define MODULE_BIT 0x00001000

#include "nv_std.h"

static void nv_dac2_dump_pix_pll(void);
static status_t nv10_nv20_dac2_pix_pll_find(
	display_mode target,float * calc_pclk,uint8 * m_result,uint8 * n_result,uint8 * p_result, uint8 test);

/* see if an analog VGA monitor is connected to connector #2 */
bool nv_dac2_crt_connected()
{
	uint32 output, dac;
	bool present;

	switch(si->ps.card_type) {
	/* NOTE:
	 * NV11 can't do this: It will report DAC1 status instead because it HAS no
	 * actual secondary DAC function. */
	/* (It DOES have a secondary palette RAM and pixelclock PLL though.) */
	case NV11:
	/* on NV40 arch (confirmed NV43, G71, G73) this routine doesn't work. */
	/* (on NV44 (confirmed Geforce 6200LE) this routine *does* work.) */
	case NV43:
	case G71:
	case G73:
		LOG(4,("DAC2: no load detection available. reporting no CRT detected on connector #2\n"));
		return false;
	}

	/* save output connector setting */
	output = DAC2R(OUTPUT);
	/* save DAC state */
	dac = DAC2R(TSTCTRL);

	/* turn on DAC2 */
	DAC2W(TSTCTRL, (DAC2R(TSTCTRL) & 0xfffeffff));
	/* select primary CRTC (head) and turn off CRT (and DVI?) outputs */
	DAC2W(OUTPUT, (output & 0x0000feee));
	/* wait for signal lines to stabilize */
	snooze(1000);
	/* re-enable CRT output */
	DAC2W(OUTPUT, (DAC2R(OUTPUT) | 0x00000001));

	/* setup RGB test signal levels to approx 30% of DAC range and enable them
	 * (NOTE: testsignal function block resides in DAC1 only (!)) */
	DACW(TSTDATA, ((0x2 << 30) | (0x140 << 20) | (0x140 << 10) | (0x140 << 0)));
	/* route test signals to output
	 * (NOTE: testsignal function block resides in DAC1 only (!)) */
	DACW(TSTCTRL, (DACR(TSTCTRL) | 0x00001000));
	/* wait for signal lines to stabilize */
	snooze(1000);

	/* do actual detection: all signals paths high == CRT connected */
	if (DAC2R(TSTCTRL) & 0x10000000)
	{
		present = true;
		LOG(4,("DAC2: CRT detected on connector #2\n"));
	}
	else
	{
		present = false;
		LOG(4,("DAC2: no CRT detected on connector #2\n"));
	}

	/* kill test signal routing
	 * (NOTE: testsignal function block resides in DAC1 only (!)) */
	DACW(TSTCTRL, (DACR(TSTCTRL) & 0xffffefff));

	/* restore output connector setting */
	DAC2W(OUTPUT, output);
	/* restore DAC state */
	DAC2W(TSTCTRL, dac);

	return present;
}

/*set the mode, brightness is a value from 0->2 (where 1 is equivalent to direct)*/
status_t nv_dac2_mode(int mode,float brightness)
{
	uint8 *r,*g,*b;
	int i, ri;

	/*set colour arrays to point to space reserved in shared info*/
	r = si->color_data;
	g = r + 256;
	b = g + 256;

	LOG(4,("DAC2: Setting screen mode %d brightness %f\n", mode, brightness));
	/* init the palette for brightness specified */
	/* (Nvidia cards always use MSbits from screenbuffer as index for PAL) */
	for (i = 0; i < 256; i++)
	{
		ri = i * brightness;
		if (ri > 255) ri = 255;
		b[i] = g[i] = r[i] = ri;
	}

	if (nv_dac2_palette(r,g,b) != B_OK) return B_ERROR;

	/* disable palette RAM adressing mask */
	NV_REG8(NV8_PAL2MASK) = 0xff;
	LOG(2,("DAC2: PAL pixrdmsk readback $%02x\n", NV_REG8(NV8_PAL2MASK)));

	return B_OK;
}

/*program the DAC palette using the given r,g,b values*/
status_t nv_dac2_palette(uint8 r[256],uint8 g[256],uint8 b[256])
{
	int i;

	LOG(4,("DAC2: setting palette\n"));

	/* select first PAL adress before starting programming */
	NV_REG8(NV8_PAL2INDW) = 0x00;

	/* loop through all 256 to program DAC */
	for (i = 0; i < 256; i++)
	{
		/* the 6 implemented bits are on b0-b5 of the bus */
		NV_REG8(NV8_PAL2DATA) = r[i];
		NV_REG8(NV8_PAL2DATA) = g[i];
		NV_REG8(NV8_PAL2DATA) = b[i];
	}
	if (NV_REG8(NV8_PAL2INDW) != 0x00)
	{
		LOG(8,("DAC2: PAL write index incorrect after programming\n"));
		return B_ERROR;
	}
if (1)
 {//reread LUT
	uint8 R, G, B;

	/* select first PAL adress to read (modulo 3 counter) */
	NV_REG8(NV8_PAL2INDR) = 0x00;
	for (i = 0; i < 256; i++)
	{
		R = NV_REG8(NV8_PAL2DATA);
		G = NV_REG8(NV8_PAL2DATA);
		B = NV_REG8(NV8_PAL2DATA);
		if ((r[i] != R) || (g[i] != G) || (b[i] != B)) 
			LOG(1,("DAC2 palette %d: w %x %x %x, r %x %x %x\n", i, r[i], g[i], b[i], R, G, B)); // apsed
	}
 }

	return B_OK;
}

/*program the pixpll - frequency in kHz*/
status_t nv_dac2_set_pix_pll(display_mode target)
{
	uint8 m=0,n=0,p=0;

	float pix_setting, req_pclk;
	status_t result;

	/* fix a DVI or laptop flatpanel to 60Hz refresh! */
	/* Note:
	 * The pixelclock drives the flatpanel modeline, not the CRTC modeline. */
	if (si->ps.monitors & CRTC2_TMDS)
	{
		LOG(4,("DAC2: Fixing DFP refresh to 60Hz!\n"));

		/* use the panel's modeline to determine the needed pixelclock */
		target.timing.pixel_clock = si->ps.p2_timing.pixel_clock;
	}

	req_pclk = (target.timing.pixel_clock)/1000.0;

	/* signal that we actually want to set the mode */
	result = nv_dac2_pix_pll_find(target,&pix_setting,&m,&n,&p, 1);
	if (result != B_OK) return result;

	/* dump old setup for learning purposes */
	nv_dac2_dump_pix_pll();

	/* some logging for learning purposes */
	LOG(4,("DAC2: current NV30_PLLSETUP settings: $%08x\n", DACR(NV30_PLLSETUP)));
	/* this register seems to (dis)connect functions blocks and PLLs:
	 * there seem to be two PLL types per function block (on some cards),
	 * b16-17 DAC1clk, b18-19 DAC2clk, b20-21 GPUclk, b22-23 MEMclk. */
	LOG(4,("DAC2: current (0x0000c040) settings: $%08x\n", NV_REG32(0x0000c040)));

	/* disable spread spectrum modes for the pixelPLLs _first_ */
	/* spread spectrum: b0,1 = GPUclk, b2,3 = MEMclk, b4,5 = DAC1clk, b6,7 = DAC2clk;
	 * b16-19 influence clock routing to digital outputs (internal/external LVDS transmitters?) */
	if (si->ps.card_arch >= NV30A)
		DACW(NV30_PLLSETUP, (DACR(NV30_PLLSETUP) & ~0x000000f0));

	/* we offer this option because some panels have very tight restrictions,
	 * and there's no overlapping settings range that makes them all work.
	 * note:
	 * this assumes the cards BIOS correctly programmed the panel (is likely) */
	//fixme: when VESA DDC EDID stuff is implemented, this option can be deleted...
	if ((si->ps.monitors & CRTC2_TMDS) && !si->settings.pgm_panel) {
		LOG(4,("DAC2: Not programming DFP refresh (specified in nvidia.settings)\n"));
	} else {
		LOG(4,("DAC2: Setting PIX PLL for pixelclock %f\n", req_pclk));

		/* program new frequency */
		DAC2W(PIXPLLC, ((p << 16) | (n << 8) | m));

		/* program 2nd set N and M scalers if they exist (b31=1 enables them) */
		if (si->ps.ext_pll) DAC2W(PIXPLLC2, 0x80000401);

		/* Give the PIXPLL frequency some time to lock... (there's no indication bit available) */
		snooze(1000);

		LOG(2,("DAC2: PIX PLL frequency should be locked now...\n"));
	}

	/* enable programmable PLLs */
	/* (confirmed PLLSEL to be a write-only register on NV04 and NV11!) */
	/* note:
	 * setup PLL assignment _after_ programming PLL */
	if (si->ps.card_arch < NV40A) {
		DACW(PLLSEL, 0x30000f00);
	} else {
		DACW(NV40_PLLSEL2, (DACR(NV40_PLLSEL2) & ~0x10000100));
		DACW(PLLSEL, 0x30000f04);
	}

	return B_OK;
}

static void nv_dac2_dump_pix_pll(void)
{
	uint32 dividers1, dividers2;
	uint8 m1, n1, p1;
	uint8 m2 = 1, n2 = 1;
	float f_vco, f_phase, f_pixel;

	LOG(2,("DAC2: dumping current pixelPLL settings:\n"));

	dividers1 = DAC2R(PIXPLLC);
	m1 = (dividers1 & 0x000000ff);
	n1 = (dividers1 & 0x0000ff00) >> 8;
	p1 = 0x01 << ((dividers1 & 0x00070000) >> 16);
	LOG(2,("DAC2: divider1 settings ($%08x): M1=%d, N1=%d, P1=%d\n", dividers1, m1, n1, p1));

	if (si->ps.ext_pll) {
		dividers2 = DAC2R(PIXPLLC2);
		if (dividers2 & 0x80000000) {
			/* the extended PLL part is enabled */
			m2 = (dividers2 & 0x000000ff);
			n2 = (dividers2 & 0x0000ff00) >> 8;
			LOG(2,("DAC2: divider2 is enabled, settings ($%08x): M2=%d, N2=%d\n", dividers2, m2, n2));
		} else {
			LOG(2,("DAC2: divider2 is disabled ($%08x)\n", dividers2));
		}
	}

	/* log the frequencies found */
	f_phase = si->ps.f_ref / (m1 * m2);
	f_vco = (f_phase * n1 * n2);
	f_pixel = f_vco / p1;

	LOG(2,("DAC2: phase discriminator frequency is %fMhz\n", f_phase));
	LOG(2,("DAC2: VCO frequency is %fMhz\n", f_vco));
	LOG(2,("DAC2: pixelclock is %fMhz\n", f_pixel));
	LOG(2,("DAC2: end of dump.\n"));

	/* apparantly if a VESA modecall during boot fails we need to explicitly select the PLL's
	 * again (was already done during driver init) if we readout the current PLL setting.. */
	DACW(PLLSEL, 0x30000f00);
}

/* find nearest valid pix pll */
status_t nv_dac2_pix_pll_find
	(display_mode target,float * calc_pclk,uint8 * m_result,uint8 * n_result,uint8 * p_result, uint8 test)
{
	switch (si->ps.card_type) {
		default:   return nv10_nv20_dac2_pix_pll_find(target, calc_pclk, m_result, n_result, p_result, test);
	}
	return B_ERROR;
}

/* find nearest valid pixel PLL setting */
static status_t nv10_nv20_dac2_pix_pll_find(
	display_mode target,float * calc_pclk,uint8 * m_result,uint8 * n_result,uint8 * p_result, uint8 test)
{
	int m = 0, n = 0, p = 0/*, m_max*/;
	float error, error_best = 999999999;
	int best[3]; 
	float f_vco, max_pclk;
	float req_pclk = target.timing.pixel_clock/1000.0;

	LOG(4,("DAC2: NV10/NV20 restrictions apply\n"));

	/* determine the max. pixelclock for the current videomode */
	switch (target.space)
	{
		case B_CMAP8:
			max_pclk = si->ps.max_dac2_clock_8;
			break;
		case B_RGB15_LITTLE:
		case B_RGB16_LITTLE:
			max_pclk = si->ps.max_dac2_clock_16;
			break;
		case B_RGB24_LITTLE:
			max_pclk = si->ps.max_dac2_clock_24;
			break;
		case B_RGB32_LITTLE:
			max_pclk = si->ps.max_dac2_clock_32;
			break;
		default:
			/* use fail-safe value */
			max_pclk = si->ps.max_dac2_clock_32;
			break;
	}
	/* if some dualhead mode is active, an extra restriction might apply */
	if ((target.flags & DUALHEAD_BITS) && (target.space == B_RGB32_LITTLE))
		max_pclk = si->ps.max_dac2_clock_32dh;

	/* Make sure the requested pixelclock is within the PLL's operational limits */
	/* lower limit is min_pixel_vco divided by highest postscaler-factor */
	if (req_pclk < (si->ps.min_video_vco / 16.0))
	{
		LOG(4,("DAC2: clamping pixclock: requested %fMHz, set to %fMHz\n",
										req_pclk, (float)(si->ps.min_video_vco / 16.0)));
		req_pclk = (si->ps.min_video_vco / 16.0);
	}
	/* upper limit is given by pins in combination with current active mode */
	if (req_pclk > max_pclk)
	{
		LOG(4,("DAC2: clamping pixclock: requested %fMHz, set to %fMHz\n",
														req_pclk, (float)max_pclk));
		req_pclk = max_pclk;
	}

	/* iterate through all valid PLL postscaler settings */
	for (p=0x01; p < 0x20; p = p<<1)
	{
		/* calculate the needed VCO frequency for this postscaler setting */
		f_vco = req_pclk * p;

		/* check if this is within range of the VCO specs */
		if ((f_vco >= si->ps.min_video_vco) && (f_vco <= si->ps.max_video_vco))
		{
			/* FX5600 and FX5700 tweak for 2nd set N and M scalers */
			if (si->ps.ext_pll) f_vco /= 4;

			/* iterate trough all valid reference-frequency postscaler settings */
			for (m = 7; m <= 14; m++)
			{
				/* check if phase-discriminator will be within operational limits */
				//fixme: PLL calcs will be resetup/splitup/updated...
				if (si->ps.card_type == NV36)
				{
					if (((si->ps.f_ref / m) < 3.2) || ((si->ps.f_ref / m) > 6.4)) continue;
				}
				else
				{
					if (((si->ps.f_ref / m) < 1.0) || ((si->ps.f_ref / m) > 2.0)) continue;
				}

				/* calculate VCO postscaler setting for current setup.. */
				n = (int)(((f_vco * m) / si->ps.f_ref) + 0.5);
				/* ..and check for validity */
				if ((n < 1) || (n > 255))	continue;

				/* find error in frequency this setting gives */
				if (si->ps.ext_pll)
				{
					/* FX5600 and FX5700 tweak for 2nd set N and M scalers */
					error = fabs((req_pclk / 4) - (((si->ps.f_ref / m) * n) / p));
				}
				else
					error = fabs(req_pclk - (((si->ps.f_ref / m) * n) / p));

				/* note the setting if best yet */
				if (error < error_best)
				{
					error_best = error;
					best[0]=m;
					best[1]=n;
					best[2]=p;
				}
			}
		}
	}

	/* setup the scalers programming values for found optimum setting */
	m = best[0];
	n = best[1];
	p = best[2];

	/* log the VCO frequency found */
	f_vco = ((si->ps.f_ref / m) * n);
	/* FX5600 and FX5700 tweak for 2nd set N and M scalers */
	if (si->ps.ext_pll) f_vco *= 4;

	LOG(2,("DAC2: pix VCO frequency found %fMhz\n", f_vco));

	/* return the results */
	*calc_pclk = (f_vco / p);
	*m_result = m;
	*n_result = n;
	switch(p)
	{
	case 1:
		p = 0x00;
		break;
	case 2:
		p = 0x01;
		break;
	case 4:
		p = 0x02;
		break;
	case 8:
		p = 0x03;
		break;
	case 16:
		p = 0x04;
		break;
	}
	*p_result = p;

	/* display the found pixelclock values */
	LOG(2,("DAC2: pix PLL check: requested %fMHz got %fMHz, mnp 0x%02x 0x%02x 0x%02x\n",
		req_pclk, *calc_pclk, *m_result, *n_result, *p_result));

	return B_OK;
}
