// NullFilterAddOn.cpp

#include "NullFilterAddOn.h"

#include "AudioFilterNode.h"
#include "NullAudioOp.h"

#include <Entry.h>
#include <Debug.h>
#include <cstring>
#include <cstdlib>

// -------------------------------------------------------- //
// _NullFilterNode
// -------------------------------------------------------- //

class _NullFilterNode :
	public	AudioFilterNode {
	typedef	AudioFilterNode _inherited;

public:
	virtual ~_NullFilterNode() {}
	_NullFilterNode(
		const char*									name,
		IAudioOpFactory*						opFactory,
		BMediaAddOn*								addOn=0) :
		BMediaNode(name),
		AudioFilterNode(name, opFactory, addOn) {}

	// be rather permissive about formats
	status_t getRequiredFormat(
		media_format&								ioFormat) {
		
		if(ioFormat.type != B_MEDIA_RAW_AUDIO)
			return B_MEDIA_BAD_FORMAT;
		ioFormat.u.raw_audio = media_raw_audio_format::wildcard;
		ioFormat.u.raw_audio.channel_count = 1;
		
		return B_OK;
	}
	
	status_t getPreferredFormat(
		media_format&								ioFormat) {
		status_t err = _inherited::getPreferredFormat(ioFormat);
		if(err < B_OK)
			return err;
			
		ioFormat.u.raw_audio.channel_count = 1;
		return B_OK;
	}
};


// -------------------------------------------------------- //
// instantiation function
// -------------------------------------------------------- //

extern "C" _EXPORT BMediaAddOn* make_media_addon(image_id image) {
	return new NullFilterAddOn(image);
}

// -------------------------------------------------------- //
// ctor/dtor
// -------------------------------------------------------- //

//NullFilterAddOn::~NullFilterAddOn() {}
NullFilterAddOn::NullFilterAddOn(image_id id) :
	BMediaAddOn(id) {}

// -------------------------------------------------------- //
// BMediaAddOn impl
// -------------------------------------------------------- //

status_t NullFilterAddOn::InitCheck(
	const char** out_failure_text) {
	return B_OK;
}
	
int32 NullFilterAddOn::CountFlavors() {
	return 1;
}

status_t NullFilterAddOn::GetFlavorAt(
	int32 n,
	const flavor_info** out_info) {
	if(n)
		return B_ERROR;
	
	flavor_info* pInfo = new flavor_info;
	pInfo->internal_id = n;
	pInfo->name = "NullFilter";
	pInfo->info =
		"NullFilter (empty test filter).\n"
		"by Eric Moon (8 September 1999)";
	pInfo->kinds = B_BUFFER_CONSUMER | B_BUFFER_PRODUCER | B_CONTROLLABLE;
	pInfo->flavor_flags = 0;
	pInfo->possible_count = 0;
	
	pInfo->in_format_count = 1;
	media_format* pFormat = new media_format;
	pFormat->type = B_MEDIA_RAW_AUDIO;
	pFormat->u.raw_audio = media_raw_audio_format::wildcard;
	pInfo->in_formats = pFormat;

	pInfo->out_format_count = 1;
	pFormat = new media_format;
	pFormat->type = B_MEDIA_RAW_AUDIO;
	pFormat->u.raw_audio = media_raw_audio_format::wildcard;
	pInfo->out_formats = pFormat;
	
	*out_info = pInfo;
	return B_OK;
}

BMediaNode* NullFilterAddOn::InstantiateNodeFor(
	const flavor_info* info,
	BMessage* config,
	status_t* out_error) {

	return new _NullFilterNode(
		"NullFilter",
		new NullAudioOpFactory(),
		this);
}

status_t NullFilterAddOn::GetConfigurationFor(
	BMediaNode* your_node,
	BMessage* into_message) {
	
	// no config yet
	return B_OK;
}


// END -- NullFilterAddOn.cpp --