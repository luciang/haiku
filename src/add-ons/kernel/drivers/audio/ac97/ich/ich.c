/*
 * BeOS Driver for Intel ICH AC'97 Link interface
 *
 * Copyright (c) 2002, Marcus Overhagen <marcus@overhagen.de>
 *
 * All rights reserved.
 * Redistribution and use in source and binary forms, with or without modification, 
 * are permitted provided that the following conditions are met:
 *
 * - Redistributions of source code must retain the above copyright notice, 
 *   this list of conditions and the following disclaimer.
 * - Redistributions in binary form must reproduce the above copyright notice,
 *   this list of conditions and the following disclaimer in the documentation 
 *   and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND 
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED 
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE 
 * DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR 
 * ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES 
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS 
 * OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY 
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING 
 * NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, 
 * EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

//#define DEBUG 1

#include <KernelExport.h>
#include <Drivers.h>
#include <Errors.h>
#include <OS.h>
#include <malloc.h>
#include "debug.h"
#include "config.h"
#include "io.h"
#include "ich.h"
#include "util.h"
#include "ac97_multi.h"
#include "ac97.h"

int32 			api_version = B_CUR_DRIVER_API_VERSION;

volatile bool	int_thread_exit = false;
thread_id 		int_thread_id = -1;
ich_chan *		chan_pi = 0;
ich_chan *		chan_po = 0;
ich_chan *		chan_mc = 0;

int32 		ich_int(void *data);
int32 		ich_test_int(void *check);
bool 		interrupt_test(void);
void 		init_chan(ich_chan *chan);
void 		reset_chan(ich_chan *chan);
void 		chan_free_resources(void);
status_t 	map_io_memory(void);
status_t 	unmap_io_memory(void);
int32 		int_thread(void *data);

#if DEBUG > 2
void dump_chan(ich_chan *chan)
{
	int i;
	LOG(("chan->regbase = %#08x\n",chan->regbase));
	LOG(("chan->buffer_ready_sem = %#08x\n",chan->buffer_ready_sem));

	LOG(("chan->buffer_log_base = %#08x\n",chan->buffer_log_base));
	LOG(("chan->buffer_phy_base = %#08x\n",chan->buffer_phy_base));
	LOG(("chan->bd_phy_base = %#08x\n",chan->bd_phy_base));
	LOG(("chan->bd_log_base = %#08x\n",chan->bd_log_base));
	for (i = 0; i < ICH_BD_COUNT; i++) {
		LOG(("chan->buffer[%d] = %#08x\n",i,chan->buffer[i]));
	}
	for (i = 0; i < ICH_BD_COUNT; i++) {
		LOG(("chan->bd[%d] = %#08x\n",i,chan->bd[i]));
		LOG(("chan->bd[%d]->buffer = %#08x\n",i,chan->bd[i]->buffer));
		LOG(("chan->bd[%d]->length = %#08x\n",i,chan->bd[i]->length));
		LOG(("chan->bd[%d]->flags = %#08x\n",i,chan->bd[i]->flags));
	}
}
#endif

void init_chan(ich_chan *chan)
{
	int i;
	ASSERT(BUFFER_COUNT <= ICH_BD_COUNT);
	LOG(("init chan\n"));

	chan->lastindex = 0;
	chan->played_frames_count = 0;
	chan->played_real_time = 0;
	chan->running = false;

	for (i = 0; i < BUFFER_COUNT; i++) {
		chan->userbuffer[i] = ((char *)chan->userbuffer_base) + i * BUFFER_SIZE;
	}
	for (i = 0; i < ICH_BD_COUNT; i++) {
		chan->buffer[i] = ((char *)chan->buffer_log_base) + (i % BUFFER_COUNT) * BUFFER_SIZE;
		chan->bd[i] = (ich_bd *) (((char *)chan->bd_log_base) + i * sizeof(ich_bd));	
		chan->bd[i]->buffer = ((uint32)chan->buffer_phy_base) + (i % BUFFER_COUNT) * BUFFER_SIZE;
		chan->bd[i]->length = BUFFER_SIZE / config->sample_size;
		chan->bd[i]->flags = ICH_BDC_FLAG_IOC;
	}
	
	// set physical buffer descriptor base address
	ich_reg_write_32(chan->regbase, (uint32)chan->bd_phy_base);

	LOG(("init chan finished\n"));
}

void start_chan(ich_chan *chan)
{
	int32 civ;
	
	if (chan->running)
		return;

	#if DEBUG > 2
		dump_chan(chan);
	#endif

	civ = ich_reg_read_8(chan->regbase + ICH_REG_X_CIV);

	chan->lastindex = civ;
	chan->played_frames_count = -BUFFER_FRAMES_COUNT; /* gets bumped up to 0 in the first interrupt before playback starts */
	chan->played_real_time = 0;

	// step 1: clear status bits
	ich_reg_write_16(chan->regbase + ICH_REG_X_SR, SR_FIFOE | SR_BCIS | SR_LVBCI); 
	// step 2: prepare buffer transfer
	ich_reg_write_8(chan->regbase + ICH_REG_X_LVI, (civ + 28) % ICH_BD_COUNT);
	// step 3: enable interrupts & busmaster transfer
	ich_reg_write_8(chan->regbase + ICH_REG_X_CR, CR_RPBM | CR_LVBIE | CR_IOCE);

	chan->running = true;
}


void reset_chan(ich_chan *chan)
{
	int i, cr;
	ich_reg_write_8(chan->regbase + ICH_REG_X_CR, 0);
	snooze(10000); // 10 ms

	chan->running = false;

	ich_reg_write_8(chan->regbase + ICH_REG_X_CR, CR_RR);
	for (i = 0; i < 10000; i++) {
		cr = ich_reg_read_8(chan->regbase + ICH_REG_X_CR);
		if (cr == 0) {
			LOG(("channel reset finished, %d\n",i));
			return;
		}
		snooze(1);
	}
	LOG(("channel reset failed after 10ms\n"));
}

bool interrupt_test(void)
{
	bool *testresult;
	bool result;
	bigtime_t duration;
	int i;

	LOG(("testing if interrupt is working\n"));
	
	// our stack is not mapped in interrupt context, we must use malloc
	// to have a valid pointer inside the interrupt handler
	testresult = malloc(sizeof(bool)); 
	*testresult = false; // assume it's not working
	
	install_io_interrupt_handler(config->irq, ich_test_int, testresult, 0);

	// clear status bits
	ich_reg_write_16(chan_po->regbase + ICH_REG_X_SR, SR_FIFOE | SR_BCIS | SR_LVBCI); 
	// start transfer of 1 buffer
	ich_reg_write_8(chan_po->regbase + ICH_REG_X_LVI, (ich_reg_read_8(chan_po->regbase + ICH_REG_X_LVI) + 1) % ICH_BD_COUNT);
	// enable interrupts & busmaster transfer
	ich_reg_write_8(chan_po->regbase + ICH_REG_X_CR, CR_RPBM | CR_LVBIE | CR_IOCE);

	// the interrupt handler will set *testresult to true
	duration = system_time();
	for (i = 0; i < 500; i++) {
		if (*testresult)
			break;
		else
			snooze(1000); // 1 ms
	}
	duration = system_time() - duration;

	// disable interrupts & busmaster transfer
	ich_reg_write_8(chan_po->regbase + ICH_REG_X_CR, 0);

	snooze(25000);

	remove_io_interrupt_handler(config->irq, ich_test_int, testresult);
	result = *testresult;
	free(testresult);

	#if DEBUG
		if (result) {
			LOG(("got interrupt after %Lu us\n",duration));
		} else {
			LOG(("no interrupt, timeout after %Lu us\n",duration));
		}
	#endif

	return result;
}

void chan_free_resources(void)
{
	if (chan_po) {
		if (chan_po->buffer_ready_sem > B_OK)
			delete_sem(chan_po->buffer_ready_sem);
		if (chan_po->bd_area > B_OK)
			delete_area(chan_po->bd_area);
		if (chan_po->buffer_area > B_OK)
			delete_area(chan_po->buffer_area);
		if (chan_po->userbuffer_area > B_OK)
			delete_area(chan_po->userbuffer_area);
		free(chan_po);
	}
	if (chan_pi) {
		if (chan_pi->buffer_ready_sem > B_OK)
			delete_sem(chan_pi->buffer_ready_sem);
		if (chan_pi->bd_area > B_OK)
			delete_area(chan_pi->bd_area);
		if (chan_pi->buffer_area > B_OK)
			delete_area(chan_pi->buffer_area);
		if (chan_pi->userbuffer_area > B_OK)
			delete_area(chan_pi->userbuffer_area);
		free(chan_pi);
	}
	if (chan_mc) {
		if (chan_mc->buffer_ready_sem > B_OK)
			delete_sem(chan_mc->buffer_ready_sem);
		if (chan_mc->bd_area > B_OK)
			delete_area(chan_mc->bd_area);
		if (chan_mc->buffer_area > B_OK)
			delete_area(chan_mc->buffer_area);
		if (chan_mc->userbuffer_area > B_OK)
			delete_area(chan_mc->userbuffer_area);
		free(chan_mc);
	}
}

status_t map_io_memory(void)
{
	if ((config->type & TYPE_ICH4) == 0)
		return B_OK;

	ASSERT(config->log_mmbar == 0);
	ASSERT(config->log_mbbar == 0);
	ASSERT(config->mmbar != 0);
	ASSERT(config->mbbar != 0);
	
	config->area_mmbar = map_physical_memory("ich_ac97 mmbar io",(void *)config->mmbar, B_PAGE_SIZE, B_ANY_KERNEL_BLOCK_ADDRESS, B_READ_AREA | B_WRITE_AREA, &config->log_mmbar);
	if (config->area_mmbar <= B_OK) {
		LOG(("mapping of mmbar io failed, error = %#x\n",config->area_mmbar));
		return B_ERROR;
	}
	LOG(("mapping of mmbar: area %#x, phys %#x, log %#x\n", config->area_mmbar, config->mmbar, config->log_mmbar));
	config->area_mbbar = map_physical_memory("ich_ac97 mbbar io",(void *)config->mbbar, B_PAGE_SIZE, B_ANY_KERNEL_BLOCK_ADDRESS, B_READ_AREA | B_WRITE_AREA, &config->log_mbbar);
	if (config->area_mbbar <= B_OK) {
		LOG(("mapping of mbbar io failed, error = %#x\n",config->area_mbbar));
		delete_area(config->area_mmbar);
		config->area_mmbar = -1;
		return B_ERROR;
	}
	LOG(("mapping of mbbar: area %#x, phys %#x, log %#x\n", config->area_mbbar, config->mbbar, config->log_mbbar));
	return B_OK;
}

status_t unmap_io_memory(void)
{
	status_t rv;
	if ((config->type & TYPE_ICH4) == 0)
		return B_OK;
	rv  = delete_area(config->area_mmbar);
	rv |= delete_area(config->area_mbbar);
	return rv;
}

int32 int_thread(void *data)
{
	cpu_status status;
	while (!int_thread_exit) {
		status = disable_interrupts();
		ich_int(data);
		restore_interrupts(status);
		snooze(1500);
	}
	return 0;
}

status_t
init_hardware(void)
{
	LOG_CREATE();
	if (B_OK == probe_device()) {
		PRINT(("ALL YOUR BASE ARE BELONG TO US\n"));
		return B_OK;
	} else {
		LOG(("hardware not found\n"));
		return B_ERROR;
	}
}


void 
dump_hardware_regs()
{
	LOG(("GLOB_CNT = %#08x\n",ich_reg_read_32(ICH_REG_GLOB_CNT)));
	LOG(("GLOB_STA = %#08x\n",ich_reg_read_32(ICH_REG_GLOB_STA)));
	LOG(("PI ICH_REG_X_BDBAR = %#x\n",ich_reg_read_32(ICH_REG_X_BDBAR + ICH_REG_PI_BASE)));
	LOG(("PI ICH_REG_X_CIV = %#x\n",ich_reg_read_8(ICH_REG_X_CIV + ICH_REG_PI_BASE)));
	LOG(("PI ICH_REG_X_LVI = %#x\n",ich_reg_read_8(ICH_REG_X_LVI + ICH_REG_PI_BASE)));
	LOG(("PI ICH_REG_X_SR = %#x\n",ich_reg_read_16(ICH_REG_X_SR + ICH_REG_PI_BASE)));
	LOG(("PI ICH_REG_X_PICB = %#x\n",ich_reg_read_16(ICH_REG_X_PICB + ICH_REG_PI_BASE)));
	LOG(("PI ICH_REG_X_PIV = %#x\n",ich_reg_read_8(ICH_REG_X_PIV + ICH_REG_PI_BASE)));
	LOG(("PI ICH_REG_X_CR = %#x\n",ich_reg_read_8(ICH_REG_X_CR + ICH_REG_PI_BASE)));
	LOG(("PO ICH_REG_X_BDBAR = %#x\n",ich_reg_read_32(ICH_REG_X_BDBAR + ICH_REG_PO_BASE)));
	LOG(("PO ICH_REG_X_CIV = %#x\n",ich_reg_read_8(ICH_REG_X_CIV + ICH_REG_PO_BASE)));
	LOG(("PO ICH_REG_X_LVI = %#x\n",ich_reg_read_8(ICH_REG_X_LVI + ICH_REG_PO_BASE)));
	LOG(("PO ICH_REG_X_SR = %#x\n",ich_reg_read_16(ICH_REG_X_SR + ICH_REG_PO_BASE)));
	LOG(("PO ICH_REG_X_PICB = %#x\n",ich_reg_read_16(ICH_REG_X_PICB + ICH_REG_PO_BASE)));
	LOG(("PO ICH_REG_X_PIV = %#x\n",ich_reg_read_8(ICH_REG_X_PIV + ICH_REG_PO_BASE)));
	LOG(("PO ICH_REG_X_CR = %#x\n",ich_reg_read_8(ICH_REG_X_CR + ICH_REG_PO_BASE)));
}

status_t
init_driver(void)
{
	status_t rv;
	bigtime_t start;
	bool s0cr, s1cr, s2cr;

	LOG_CREATE();
	
	LOG(("init_driver\n"));
	
	ASSERT(sizeof(ich_bd) == 8);
	
	rv = probe_device();
	if (rv != B_OK) {
		LOG(("No supported audio hardware found.\n"));
		return B_ERROR;
	}

	PRINT((VERSION "\n"));
	PRINT(("found %s\n", config->name));
	PRINT(("IRQ = %ld, NAMBAR = %#lX, NABMBAR = %#lX, MMBAR = %#lX, MBBAR = %#lX\n",config->irq,config->nambar,config->nabmbar,config->mmbar,config->mbbar));

	/* before doing anything else, map the IO memory */
	rv = map_io_memory();
	if (rv != B_OK) {
		PRINT(("mapping of memory IO space failed\n"));
		return B_ERROR;
	}
	
	dump_hardware_regs();
	
	/* do a cold reset */
	LOG(("cold reset\n"));
	ich_reg_write_32(ICH_REG_GLOB_CNT, 0);
	snooze(50000); // 50 ms
	ich_reg_write_32(ICH_REG_GLOB_CNT, CNT_COLD | CNT_PRIE);
	LOG(("cold reset finished\n"));
	rv = ich_reg_read_32(ICH_REG_GLOB_CNT);
	if ((rv & CNT_COLD) == 0) {
		LOG(("cold reset failed\n"));
	}

	/* detect which codecs are ready */
	s0cr = s1cr = s2cr = false;
	start = system_time();
	do {
		rv = ich_reg_read_32(ICH_REG_GLOB_STA);
		if (!s0cr && (rv & STA_S0CR)) {
			s0cr = true;
			LOG(("AC_SDIN0 codec ready after %Ld us\n",(system_time() - start)));
		}
		if (!s1cr && (rv & STA_S1CR)) {
			s1cr = true;
			LOG(("AC_SDIN1 codec ready after %Ld us\n",(system_time() - start)));
		}
		if (!s2cr && (rv & STA_S2CR)) {
			s2cr = true;
			LOG(("AC_SDIN2 codec ready after %Ld us\n",(system_time() - start)));
		}
		snooze(50000);
	} while ((system_time() - start) < 1000000);

	if (!s0cr) {
		LOG(("AC_SDIN0 codec not ready\n"));
	}
	if (!s1cr) {
		LOG(("AC_SDIN1 codec not ready\n"));
	}
	if (!s2cr) {
		LOG(("AC_SDIN2 codec not ready\n"));
	}

	dump_hardware_regs();

	if (!s0cr && !s1cr && !s2cr) {
		PRINT(("compatible chipset found, but no codec ready!\n"));
		unmap_io_memory();
		return B_ERROR;
	}

	if (config->type & TYPE_ICH4) {
		/* we are using a ICH4 chipset, and assume that the codec beeing ready
		 * is the primary one.
		 */
		uint8 sdin;
		uint16 reset;
		uint8 id;
		reset = ich_codec_read(0x00);	/* access the primary codec */
		if (reset == 0 || reset == 0xFFFF) {
			LOG(("primary codec not present\n"));
		} //else {
			sdin = 0x02 & ich_reg_read_8(ICH_REG_SDM);
			id = 0x02 & (ich_codec_read(0x00 + 0x28) >> 14);
			LOG(("primary codec id %d is connected to AC_SDIN%d\n", id, sdin));
		//}
		reset = ich_codec_read(0x80);	/* access the secondary codec */
		if (reset == 0 || reset == 0xFFFF) {
			LOG(("secondary codec not present\n"));
		} //else {
			sdin = 0x02 & ich_reg_read_8(ICH_REG_SDM);
			id = 0x02 & (ich_codec_read(0x80 + 0x28) >> 14);
			LOG(("secondary codec id %d is connected to AC_SDIN%d\n", id, sdin));
		//}
		reset = ich_codec_read(0x100);	/* access the tertiary codec */
		if (reset == 0 || reset == 0xFFFF) {
			LOG(("tertiary codec not present\n"));
		} //else {
			sdin = 0x02 & ich_reg_read_8(ICH_REG_SDM);
			id = 0x02 & (ich_codec_read(0x100 + 0x28) >> 14);
			LOG(("tertiary codec id %d is connected to AC_SDIN%d\n", id, sdin));
		//}
		
		/* XXX this may be wrong */
		ich_reg_write_8(ICH_REG_SDM, (ich_reg_read_8(ICH_REG_SDM) & 0x0F) | 0x08 | 0x90);
	} else {
		/* we are using a pre-ICH4 chipset, that has a fixed mapping of
		 * AC_SDIN0 = primary, AC_SDIN1 = secondary codec.
		 */
		if (!s0cr && s2cr) {
			// is is unknown if this really works, perhaps we should better abort here
			LOG(("primary codec doesn't seem to be available, using secondary!\n"));
			config->codecoffset = 0x80;
		}
	}

	dump_hardware_regs();

	/* allocate memory for channel info struct */
	chan_pi = (ich_chan *) malloc(sizeof(ich_chan));
	chan_po = (ich_chan *) malloc(sizeof(ich_chan));
	chan_mc = (ich_chan *) malloc(sizeof(ich_chan));

	if (0 == chan_pi || 0 == chan_po || 0 == chan_mc) {
		PRINT(("couldn't allocate memory for channel descriptors!\n"));
		chan_free_resources();
		unmap_io_memory();
		return B_ERROR;	
	}

	/* allocate memory for buffer descriptors */
	chan_po->bd_area = alloc_mem(&chan_po->bd_phy_base, &chan_po->bd_log_base, ICH_BD_COUNT * sizeof(ich_bd), "ich_ac97 po buf desc");
	chan_pi->bd_area = alloc_mem(&chan_pi->bd_phy_base, &chan_pi->bd_log_base, ICH_BD_COUNT * sizeof(ich_bd), "ich_ac97 pi buf desc");
	chan_mc->bd_area = alloc_mem(&chan_mc->bd_phy_base, &chan_mc->bd_log_base, ICH_BD_COUNT * sizeof(ich_bd), "ich_ac97 mc buf desc");
	/* allocate memory buffers */
	chan_po->buffer_area = alloc_mem(&chan_po->buffer_phy_base, &chan_po->buffer_log_base, BUFFER_COUNT * BUFFER_SIZE, "ich_ac97 po buf");
	chan_pi->buffer_area = alloc_mem(&chan_pi->buffer_phy_base, &chan_pi->buffer_log_base, BUFFER_COUNT * BUFFER_SIZE, "ich_ac97 pi buf");
	chan_mc->buffer_area = alloc_mem(&chan_mc->buffer_phy_base, &chan_mc->buffer_log_base, BUFFER_COUNT * BUFFER_SIZE, "ich_ac97 mc buf");
	/* allocate memory exported userland buffers */
	chan_po->userbuffer_area = alloc_mem(NULL, &chan_po->userbuffer_base, BUFFER_COUNT * BUFFER_SIZE, "ich_ac97 po user buf");
	chan_pi->userbuffer_area = alloc_mem(NULL, &chan_pi->userbuffer_base, BUFFER_COUNT * BUFFER_SIZE, "ich_ac97 pi user buf");
	chan_mc->userbuffer_area = alloc_mem(NULL, &chan_mc->userbuffer_base, BUFFER_COUNT * BUFFER_SIZE, "ich_ac97 mc user buf");

	if (  chan_po->bd_area < B_OK || chan_po->buffer_area < B_OK || chan_po->userbuffer_area < B_OK ||
		  chan_pi->bd_area < B_OK || chan_pi->buffer_area < B_OK || chan_pi->userbuffer_area < B_OK ||
		  chan_mc->bd_area < B_OK || chan_mc->buffer_area < B_OK || chan_mc->userbuffer_area < B_OK) {
		PRINT(("couldn't allocate memory for DMA buffers!\n"));
		chan_free_resources();
		unmap_io_memory();
		return B_ERROR;	
	}

	/* allocate all semaphores */
	chan_po->buffer_ready_sem = create_sem(0,"po buffer ready");			/* pcm out buffer available */
	chan_pi->buffer_ready_sem = create_sem(0,"pi buffer ready");			/* 0 available pcm in buffers */
	chan_mc->buffer_ready_sem = create_sem(0,"mc buffer ready");			/* 0 available mic in buffers */
	
	if (chan_po->buffer_ready_sem < B_OK || chan_pi->buffer_ready_sem < B_OK || chan_mc->buffer_ready_sem < B_OK) {
		PRINT(("couldn't create semaphores!\n"));
		chan_free_resources();
		unmap_io_memory();
		return B_ERROR;	
	}

	/* setup channel register base address */
	chan_pi->regbase = ICH_REG_PI_BASE;
	chan_po->regbase = ICH_REG_PO_BASE;
	chan_mc->regbase = ICH_REG_MC_BASE;

	/* reset the codec */	
	LOG(("codec reset\n"));
	ich_codec_write(config->codecoffset + 0x00, 0x0000);
	snooze(50000); // 50 ms

	ac97_init();
	ac97_amp_enable(true);

	LOG(("codec vendor id = %#08x\n",ac97_get_vendor_id()));
	LOG(("codec descripton = %s\n",ac97_get_vendor_id_description()));
	LOG(("codec 3d enhancement = %s\n",ac97_get_3d_stereo_enhancement()));

	/* reset all channels */
	reset_chan(chan_pi);
	reset_chan(chan_po);
	reset_chan(chan_mc);

	/* init channels */
	init_chan(chan_pi);
	init_chan(chan_po);
	init_chan(chan_mc);
	
	/* first test if interrupts are working, on some Laptops they don't work :-( */
	if (config->irq != 0 && false == interrupt_test()) {
		LOG(("interrupt not working, using a kernel thread for polling\n"));
		config->irq = 0; /* don't use interrupts */
	}

	/* install interrupt or polling thread */
	if (config->irq != 0) {
		install_io_interrupt_handler(config->irq,ich_int,0,0);
	} else {
		int_thread_id = spawn_kernel_thread(int_thread, "ich_ac97 interrupt poller", B_REAL_TIME_PRIORITY, 0);
		resume_thread(int_thread_id);
	}

	/* enable master output */
	ich_codec_write(config->codecoffset + 0x02, 0x0000);
	/* enable pcm output */
	ich_codec_write(config->codecoffset + 0x18, 0x0404);

#if 0
	/* enable pcm input */
	ich_codec_write(config->codecoffset + 0x10, 0x0000);

	/* enable mic input */
	/* ich_codec_write(config->codecoffset + 0x0E, 0x0000); */

	/* select pcm input record */
	ich_codec_write(config->codecoffset + 0x1A, 4 | (4 << 8));
	/* enable PCM record */
	ich_codec_write(config->codecoffset + 0x1C, 0x0000);

	/* enable mic record */
	/* ich_codec_write(config->codecoffset + 0x1E, 0x0000); */
#endif

	LOG(("init_driver finished!\n"));
	return B_OK;
}

void
uninit_driver(void)
{
	LOG(("uninit_driver()\n"));

	#if DEBUG
		if (chan_po) LOG(("chan_po frames_count = %Ld\n",chan_po->played_frames_count));
		if (chan_pi) LOG(("chan_pi frames_count = %Ld\n",chan_pi->played_frames_count));
		if (chan_mc) LOG(("chan_mc frames_count = %Ld\n",chan_mc->played_frames_count));
	#endif
	
	/* reset all channels */
	if (chan_pi)
		reset_chan(chan_pi);
	if (chan_po)
		reset_chan(chan_po);
	if (chan_mc)
		reset_chan(chan_mc);

	snooze(50000);
	
	/* remove the interrupt handler or thread */
	if (config->irq != 0) {
		remove_io_interrupt_handler(config->irq,ich_int,0);
	} else if (int_thread_id != -1) {
		status_t exit_value;
		int_thread_exit = true;
		wait_for_thread(int_thread_id, &exit_value);
	}

	/* invalidate hardware buffer descriptor base addresses */
	ich_reg_write_32(ICH_REG_PI_BASE, 0);
	ich_reg_write_32(ICH_REG_PO_BASE, 0);
	ich_reg_write_32(ICH_REG_MC_BASE, 0);

	/* free allocated channel semaphores and memory */
	chan_free_resources();

	/* the very last thing to do is unmap the io memory */
	unmap_io_memory();

	LOG(("uninit_driver() finished\n"));
}

int32 ich_test_int(void *check)
{
	/*
	 * This interrupt handler is used once to test if interrupt handling is working.
	 * If it gets executed and the interrupt is from pcm-out, it will set the bool
	 * pointed to by check to true.
	 */
	uint32 sta = ich_reg_read_32(ICH_REG_GLOB_STA);
	uint16 sr = ich_reg_read_16(chan_po->regbase + (config->swap_reg ? ICH_REG_X_PICB : ICH_REG_X_SR));

	if ((sta & STA_INTMASK) == 0)
		return B_UNHANDLED_INTERRUPT;

	if ((sta & STA_POINT) && (sr & SR_BCIS)) // pcm-out buffer completed
		*(bool *)check = true; // notify

	sr &= SR_FIFOE | SR_BCIS | SR_LVBCI;
	ich_reg_write_16(chan_po->regbase + (config->swap_reg ? ICH_REG_X_PICB : ICH_REG_X_SR),sr);

	return B_HANDLED_INTERRUPT;
}

int32 ich_int(void *unused)
{
	uint32 sta;
	sta = ich_reg_read_32(ICH_REG_GLOB_STA);
	
	if ((sta & STA_INTMASK) == 0)
		return B_UNHANDLED_INTERRUPT;
	
	if (sta & (STA_S0RI | STA_S1RI | STA_S2RI)) {
		/* ignore and clear resume interrupt(s) */
		ich_reg_write_32(ICH_REG_GLOB_STA, sta & (STA_S0RI | STA_S1RI | STA_S2RI));
	}
	
	if (sta & STA_POINT) { // pcm-out
		uint16 sr = ich_reg_read_16(chan_po->regbase + (config->swap_reg ? ICH_REG_X_PICB : ICH_REG_X_SR));
		sr &= SR_FIFOE | SR_BCIS | SR_LVBCI;

		if (sr & SR_BCIS) { // a buffer completed
			int32 count;
			int32 civ;

			/* shedule playback of the next buffers */		
			civ = ich_reg_read_8(chan_po->regbase + ICH_REG_X_CIV);
			ich_reg_write_8(chan_po->regbase + ICH_REG_X_LVI, (civ + 28) % ICH_BD_COUNT);

			/* calculate played buffer count since last interrupt */
			if (civ <= chan_po->lastindex)
				count = civ + ICH_BD_COUNT - chan_po->lastindex;
			else
				count = civ - chan_po->lastindex;
			
			if (count != 1)
				dprintf(DRIVER_NAME ": lost %ld po interrupts\n",count - 1);
			
			acquire_spinlock(&slock);
			chan_po->played_real_time = system_time();
			chan_po->played_frames_count += count * BUFFER_FRAMES_COUNT;
			chan_po->lastindex = civ;
			chan_po->backbuffer = chan_po->buffer[(civ + 1) % ICH_BD_COUNT];
			release_spinlock(&slock);

			get_sem_count(chan_po->buffer_ready_sem, &count);
			if (count <= 0)
				release_sem_etc(chan_po->buffer_ready_sem,1,B_DO_NOT_RESCHEDULE);
		}

		ich_reg_write_16(chan_po->regbase + (config->swap_reg ? ICH_REG_X_PICB : ICH_REG_X_SR),sr);
	}

	if (sta & STA_PIINT) { // pcm-in
		uint16 sr = ich_reg_read_16(chan_pi->regbase + (config->swap_reg ? ICH_REG_X_PICB : ICH_REG_X_SR));
		sr &= SR_FIFOE | SR_BCIS | SR_LVBCI;

		if (sr & SR_BCIS) { // a buffer completed
			int32 count;
			int32 civ;

			/* shedule record of the next buffers */		
			civ = ich_reg_read_8(chan_pi->regbase + ICH_REG_X_CIV);
			ich_reg_write_8(chan_pi->regbase + ICH_REG_X_LVI, (civ + 28) % ICH_BD_COUNT);

			/* calculate recorded buffer count since last interrupt */
			if (civ <= chan_pi->lastindex)
				count = civ + ICH_BD_COUNT - chan_pi->lastindex;
			else
				count = civ - chan_pi->lastindex;
			
			if (count != 1)
				dprintf(DRIVER_NAME ": lost %ld pi interrupts\n",count - 1);
			
			acquire_spinlock(&slock);
			chan_pi->played_real_time = system_time();
			chan_pi->played_frames_count += count * BUFFER_FRAMES_COUNT;
			chan_pi->lastindex = civ;
			chan_pi->backbuffer = chan_pi->buffer[(ICH_BD_COUNT - (civ - 1)) % ICH_BD_COUNT];
			release_spinlock(&slock);

			get_sem_count(chan_pi->buffer_ready_sem, &count);
			if (count <= 0)
				release_sem_etc(chan_pi->buffer_ready_sem,1,B_DO_NOT_RESCHEDULE);
		}

		ich_reg_write_16(chan_pi->regbase + (config->swap_reg ? ICH_REG_X_PICB : ICH_REG_X_SR),sr);
	}

	if (sta & STA_MINT) { // mic-in
		uint16 sr = ich_reg_read_16(chan_mc->regbase + (config->swap_reg ? ICH_REG_X_PICB : ICH_REG_X_SR));
		sr &= SR_FIFOE | SR_BCIS | SR_LVBCI;
		if (sr & SR_BCIS) { // a buffer completed
			release_sem_etc(chan_mc->buffer_ready_sem,1,B_DO_NOT_RESCHEDULE);
		}
		ich_reg_write_16(chan_mc->regbase + (config->swap_reg ? ICH_REG_X_PICB : ICH_REG_X_SR),sr);
	}
	return B_HANDLED_INTERRUPT;
}

static status_t
ich_open(const char *name, uint32 flags, void** cookie)
{
	LOG(("open()\n"));
	return B_OK;
}

static status_t
ich_close(void* cookie)
{
	LOG(("close()\n"));
	return B_OK;
}

static status_t
ich_free(void* cookie)
{
	return B_OK;
}

static status_t
ich_control(void* cookie, uint32 op, void* arg, size_t len)
{
	return multi_control(cookie,op,arg,len);
}

static status_t
ich_read(void* cookie, off_t position, void *buf, size_t* num_bytes)
{
	*num_bytes = 0;				/* tell caller nothing was read */
	return B_IO_ERROR;
}

static status_t
ich_write(void* cookie, off_t position, const void* buffer, size_t* num_bytes)
{
	*num_bytes = 0;				/* tell caller nothing was written */
	return B_IO_ERROR;
}


/* -----
	null-terminated array of device names supported by this driver
----- */

static const char *ich_name[] = {
	"audio/multi/ich_ac97/1",
	NULL
};

/* -----
	function pointers for the device hooks entry points
----- */

device_hooks ich_hooks = {
	ich_open, 			/* -> open entry point */
	ich_close, 			/* -> close entry point */
	ich_free,			/* -> free cookie */
	ich_control, 		/* -> control entry point */
	ich_read,			/* -> read entry point */
	ich_write,			/* -> write entry point */
	NULL,				/* start select */
	NULL,				/* stop select */
	NULL,				/* scatter-gather read from the device */
	NULL				/* scatter-gather write to the device */
};

/* ----------
	publish_devices - return a null-terminated array of devices
	supported by this driver.
----- */

const char**
publish_devices(void)
{
	return ich_name;
}

/* ----------
	find_device - return ptr to device hooks structure for a
	given device name
----- */

device_hooks*
find_device(const char* name)
{
	return &ich_hooks;
}
