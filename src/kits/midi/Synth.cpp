/**
 * @file Synth.cpp
 *
 * @author Matthijs Hollemans
 */

#include "debug.h"
#include "Synth.h"

//------------------------------------------------------------------------------

BSynth::BSynth()
{
	UNIMPLEMENTED
}

//------------------------------------------------------------------------------

BSynth::BSynth(synth_mode synth)
{
	UNIMPLEMENTED
}

//------------------------------------------------------------------------------

BSynth::~BSynth()
{
	UNIMPLEMENTED
}

//------------------------------------------------------------------------------

status_t BSynth::LoadSynthData(entry_ref* instrumentsFile)
{
	UNIMPLEMENTED
	return B_ERROR;
}

//------------------------------------------------------------------------------

status_t BSynth::LoadSynthData(synth_mode synth)
{
	UNIMPLEMENTED
	return B_ERROR;
}

//------------------------------------------------------------------------------

synth_mode BSynth::SynthMode(void)
{
	UNIMPLEMENTED
	return B_NO_SYNTH;
}

//------------------------------------------------------------------------------

void BSynth::Unload(void)
{
	UNIMPLEMENTED
}

//------------------------------------------------------------------------------

bool BSynth::IsLoaded(void) const
{
	UNIMPLEMENTED
	return false;
}

//------------------------------------------------------------------------------

status_t BSynth::SetSamplingRate(int32 sample_rate)
{
	UNIMPLEMENTED
	return B_ERROR;
}

//------------------------------------------------------------------------------

int32 BSynth::SamplingRate() const
{
	UNIMPLEMENTED
	return 0;
}

//------------------------------------------------------------------------------

status_t BSynth::SetInterpolation(interpolation_mode interp_mode)
{
	UNIMPLEMENTED
	return B_ERROR;
}

//------------------------------------------------------------------------------

interpolation_mode BSynth::Interpolation() const
{
	UNIMPLEMENTED
	return B_DROP_SAMPLE;
}

//------------------------------------------------------------------------------

void BSynth::SetReverb(reverb_mode rev_mode)
{
	UNIMPLEMENTED
}

//------------------------------------------------------------------------------

reverb_mode BSynth::Reverb() const
{
	UNIMPLEMENTED
	return B_REVERB_NONE;
}

//------------------------------------------------------------------------------

status_t BSynth::EnableReverb(bool reverb_enabled)
{
	UNIMPLEMENTED
	return B_ERROR;
}

//------------------------------------------------------------------------------

bool BSynth::IsReverbEnabled() const
{
	UNIMPLEMENTED
	return false;
}

//------------------------------------------------------------------------------

status_t BSynth::SetVoiceLimits(
	int16 maxSynthVoices, int16 maxSampleVoices, int16 limiterThreshhold)
{
	UNIMPLEMENTED
	return B_ERROR;
}

//------------------------------------------------------------------------------

int16 BSynth::MaxSynthVoices(void) const
{
	UNIMPLEMENTED
	return 0;
}

//------------------------------------------------------------------------------

int16 BSynth::MaxSampleVoices(void) const
{
	UNIMPLEMENTED
	return 0;
}

//------------------------------------------------------------------------------

int16 BSynth::LimiterThreshhold(void) const
{
	UNIMPLEMENTED
	return 0;
}

//------------------------------------------------------------------------------

void BSynth::SetSynthVolume(double theVolume)
{
	UNIMPLEMENTED
}

//------------------------------------------------------------------------------

double BSynth::SynthVolume(void) const
{
	UNIMPLEMENTED
	return 0;
}

//------------------------------------------------------------------------------

void BSynth::SetSampleVolume(double theVolume)
{
	UNIMPLEMENTED
}

//------------------------------------------------------------------------------

double BSynth::SampleVolume(void) const
{
	UNIMPLEMENTED
	return 0;
}

//------------------------------------------------------------------------------

status_t BSynth::GetAudio(
	int16* pLeft, int16* pRight, int32 max_samples) const
{
	UNIMPLEMENTED
	return B_ERROR;
}

//------------------------------------------------------------------------------

void BSynth::Pause(void)
{
	UNIMPLEMENTED
}

//------------------------------------------------------------------------------

void BSynth::Resume(void)
{
	UNIMPLEMENTED
}

//------------------------------------------------------------------------------

void BSynth::SetControllerHook(int16 controller, synth_controller_hook cback)
{
	UNIMPLEMENTED
}

//------------------------------------------------------------------------------

int32 BSynth::CountClients(void) const
{
	UNIMPLEMENTED
	return 0;
}

//------------------------------------------------------------------------------

void BSynth::_ReservedSynth1() { }
void BSynth::_ReservedSynth2() { }
void BSynth::_ReservedSynth3() { }
void BSynth::_ReservedSynth4() { }

//------------------------------------------------------------------------------

void BSynth::_init()
{
	UNIMPLEMENTED
}

//------------------------------------------------------------------------------

status_t BSynth::_do_load(synth_mode synth)
{
	UNIMPLEMENTED
	return B_ERROR;
}

//------------------------------------------------------------------------------

status_t BSynth::_load_insts(entry_ref* ref)
{
	UNIMPLEMENTED
	return B_ERROR;
}

//------------------------------------------------------------------------------

