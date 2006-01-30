/*
 * EchoGals/Echo24 BeOS Driver for Echo audio cards
 *
 * Copyright (c) 2003, Jerome Duval (jerome.duval@free.fr)
 *
 * Original code : BeOS Driver for Intel ICH AC'97 Link interface
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
#ifndef _MULTI_H_
#define _MULTI_H_

#include "OsSupportBeOS.h"
#include "MixerXface.h"

typedef struct _multi_mixer_control {
	struct _multi_dev 	*multi;
	void	(*get) (void *card, MIXER_AUDIO_CHANNEL, int32 type, float *values);
	void	(*set) (void *card, MIXER_AUDIO_CHANNEL, int32 type, float *values);
	MIXER_AUDIO_CHANNEL 	channel;
	int32 type;
	multi_mix_control	mix_control;
} multi_mixer_control;

#define MULTI_CONTROL_FIRSTID	1024
#define MULTI_CONTROL_MASTERID	0

typedef struct _multi_dev {
	void	*card;
#define MULTICONTROLSNUM 128
	multi_mixer_control controls[MULTICONTROLSNUM];
	uint32 control_count;
	
#define MULTICHANNUM 128
	multi_channel_info chans[MULTICHANNUM];
	uint32 output_channel_count;
	uint32 input_channel_count;
	uint32 output_bus_channel_count;
	uint32 input_bus_channel_count;
	uint32 aux_bus_channel_count;
} multi_dev;

#endif
