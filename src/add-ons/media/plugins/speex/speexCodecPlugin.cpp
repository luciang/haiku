#include <stdio.h>
#include <Autolock.h>
#include <DataIO.h>
#include <Locker.h>
#include <MediaFormats.h>
#include <MediaRoster.h>
#include <vector>
#include "ogg/ogg.h"
#include "speexCodecPlugin.h"
#include "speexCodecDefaults.h"
#include "OggSpeexFormats.h"

#define TRACE_THIS 0
#if TRACE_THIS
  #define TRACE printf
#else
  #define TRACE(a...)
#endif

#define DECODE_BUFFER_SIZE	(32 * 1024)


inline void
AdjustBufferSize(media_raw_audio_format * raf, bigtime_t buffer_duration = 50000 /* 50 ms */)
{
	size_t frame_size = (raf->format & 0xf) * raf->channel_count;
	if (raf->buffer_size <= frame_size) {
		raf->buffer_size = frame_size * (size_t)((raf->frame_rate * buffer_duration) / 1000000.0);
	} else {
		raf->buffer_size = (raf->buffer_size / frame_size) * frame_size;
	}
}


static media_format
speex_decoded_media_format()
{
	media_format format;
	format.type = B_MEDIA_RAW_AUDIO;
	init_speex_media_raw_audio_format(&format.u.raw_audio);
	return format;
}


/*
 * SpeexDecoder
 */


SpeexDecoder::SpeexDecoder()
{
	TRACE("SpeexDecoder::SpeexDecoder\n");
	speex_bits_init(&fBits);
	fDecoderState = 0;
	fHeader = 0;
	fStereoState = 0;
	fSpeexOutputLength = 0;
	fStartTime = 0;
	fFrameSize = 0;
	fOutputBufferSize = 0;
}


SpeexDecoder::~SpeexDecoder()
{
	TRACE("SpeexDecoder::~SpeexDecoder\n");
	// the fStereoState is destroyed by fDecoderState
//	delete fStereoState;
	speex_bits_destroy(&fBits);
	speex_decoder_destroy(fDecoderState);
}


void
SpeexDecoder::GetCodecInfo(media_codec_info *info)
{
	strncpy(info->short_name, "speex-libspeex", sizeof(info->short_name));
	strncpy(info->pretty_name, "speex decoder [libspeex], by Andrew Bachmann", sizeof(info->pretty_name));
}


status_t
SpeexDecoder::Setup(media_format *inputFormat,
				  const void *infoBuffer, size_t infoSize)
{
	TRACE("SpeexDecoder::Setup\n");
	if (!format_is_compatible(speex_encoded_media_format(),*inputFormat)) {
		return B_MEDIA_BAD_FORMAT;
	}
	// grab header packets from meta data
	if (inputFormat->MetaDataSize() != sizeof(std::vector<ogg_packet>)) {
		TRACE("SpeexDecoder::Setup not called with ogg_packet<vector> meta data: not speex\n");
		return B_ERROR;
	}
	std::vector<ogg_packet> * packets = (std::vector<ogg_packet> *)inputFormat->MetaData();
	if (packets->size() < 2) {
		TRACE("SpeexDecoder::Setup not called with at least two ogg_packets: not speex\n");
		return B_ERROR;
	}
	// parse header packet
	ogg_packet * packet = &(*packets)[0];
	fHeader = speex_packet_to_header((char*)packet->packet, packet->bytes);
	if (fHeader == NULL) {
		TRACE("SpeexDecoder::Setup failed in ogg_packet to speex_header conversion\n");
		return B_ERROR;
	}
	if (packets->size() != 2 + (unsigned)fHeader->extra_headers) {
		TRACE("SpeexDecoder::Setup not called with all the extra headers\n");
		delete fHeader;
		fHeader = 0;
		return B_ERROR;
	}
	// modify header to reflect settings
	switch (SpeexSettings::PreferredBand()) {
	case narrow_band:
		fHeader->mode = 0; 
		break;
	case wide_band:
		fHeader->mode = 1;
		break;
	case ultra_wide_band:
		fHeader->mode = 2;
		break;
	case automatic_band:
	default:
		break;
	}
	switch (SpeexSettings::PreferredChannels()) {
	case mono_channels:
		fHeader->nb_channels = 1;
		break;
	case stereo_channels:
		fHeader->nb_channels = 2;
		break;
	case automatic_channels:
	default:
		break;
	}
	if (fHeader->nb_channels == 0) {
		fHeader->nb_channels = 1;
	}
	if (fHeader->frames_per_packet == 0) {
		fHeader->frames_per_packet = 1;
	}
	if (SpeexSettings::SamplingRate() != 0) {
		fHeader->rate = SpeexSettings::SamplingRate();
	}
	// sanity checks
#ifdef STRICT_SPEEX
	if (header->speex_version_id > 1) {
		TRACE("SpeexDecoder::Setup failed: version id too new");
		return B_ERROR;
	}
#endif
	if (fHeader->mode >= SPEEX_NB_MODES) {
		TRACE("SpeexDecoder::Setup failed: unknown speex mode\n");
		return B_ERROR;
	}
	// setup from header
	SpeexMode * mode = speex_mode_list[fHeader->mode];
#ifdef STRICT_SPEEX
	if (mode->bitstream_version != fHeader->mode_bitstream_version) {
		TRACE("SpeexDecoder::Setup failed: bitstream version mismatch");
		return B_ERROR;
	}
#endif
	// initialize decoder
	fDecoderState = speex_decoder_init(mode);
	if (fDecoderState == NULL) {
		TRACE("SpeexDecoder::Setup failed to initialize the decoder state");
		return B_ERROR;
	}
	if (SpeexSettings::PerceptualPostFilter()) {
		int enabled = 1;
		speex_decoder_ctl(fDecoderState, SPEEX_SET_ENH, &enabled);
	}
	speex_decoder_ctl(fDecoderState, SPEEX_GET_FRAME_SIZE, &fHeader->frame_size);
	if (fHeader->nb_channels == 2) {
		SpeexCallback callback;
		SpeexStereoState stereo = SPEEX_STEREO_STATE_INIT;
		callback.callback_id = SPEEX_INBAND_STEREO;
		callback.func = speex_std_stereo_request_handler;
		fStereoState = new SpeexStereoState(stereo);
		callback.data = fStereoState;
		speex_decoder_ctl(fDecoderState, SPEEX_SET_HANDLER, &callback);
	}
	speex_decoder_ctl(fDecoderState, SPEEX_SET_SAMPLING_RATE, &fHeader->rate);
	// fill out the encoding format
	media_format requested_format = speex_decoded_media_format();
	((media_raw_audio_format)requested_format.u.raw_audio) = inputFormat->u.encoded_audio.output;
	return NegotiateOutputFormat(&requested_format);
}


status_t
SpeexDecoder::NegotiateOutputFormat(media_format *ioDecodedFormat)
{
	TRACE("SpeexDecoder::NegotiateOutputFormat\n");
	// BMediaTrack::DecodedFormat
	// Pass in ioFormat the format that you want (with wildcards
	// as applicable). The codec will find and return in ioFormat 
	// its best matching format.
	//
	// BMediaDecoder::SetOutputFormat
	// sets the format the decoder should output. On return, 
	// the outputFormat is changed to match the actual format
	// that will be output; this can be different if you 
	// specified any wildcards.
	//
	// Be R5 behavior seems to be that we can never fail.  If we
	// don't support the requested format, just return one we do.
	media_format format = speex_decoded_media_format();
	format.u.raw_audio.frame_rate = (float)fHeader->rate;
	format.u.raw_audio.channel_count = fHeader->nb_channels;
	format.u.raw_audio.channel_mask = B_CHANNEL_LEFT | (fHeader->nb_channels != 1 ? B_CHANNEL_RIGHT : 0);
	if (!format_is_compatible(format,*ioDecodedFormat)) {
		*ioDecodedFormat = format;
	}
	ioDecodedFormat->SpecializeTo(&format);
	AdjustBufferSize(&ioDecodedFormat->u.raw_audio);
	int output_length = fHeader->frame_size * format.AudioFrameSize();
	ioDecodedFormat->u.raw_audio.buffer_size
	  = ((ioDecodedFormat->u.raw_audio.buffer_size - 1) / output_length + 1) * output_length;
	// setup output variables
	fFrameSize = ioDecodedFormat->AudioFrameSize();
	fOutputBufferSize = ioDecodedFormat->u.raw_audio.buffer_size;
	fSpeexOutputLength = output_length;
	return B_OK;
}


status_t
SpeexDecoder::Seek(uint32 seekTo,
				 int64 seekFrame, int64 *frame,
				 bigtime_t seekTime, bigtime_t *time)
{
	TRACE("SpeexDecoder::Seek\n");
	int ignore = 0;
	speex_decoder_ctl(fDecoderState, SPEEX_RESET_STATE, &ignore);
	return B_OK;
}


status_t
SpeexDecoder::Decode(void *buffer, int64 *frameCount,
				   media_header *mediaHeader, media_decode_info *info /* = 0 */)
{
//	TRACE("SpeexDecoder::Decode\n");
	float * out_buffer = static_cast<float *>(buffer);
	int32	out_bytes_needed = fOutputBufferSize;
	
	bool synced = false;
	
	int total_samples = 0;
	while (out_bytes_needed >= fSpeexOutputLength) {
		// get a new packet
		const void *chunkBuffer;
		size_t chunkSize;
		media_header mh;
		status_t status = GetNextChunk(&chunkBuffer, &chunkSize, &mh);
		if (status == B_LAST_BUFFER_ERROR) {
			goto done;
		}			
		if (status != B_OK) {
			TRACE("SpeexDecoder::Decode: GetNextChunk failed\n");
			return status;
		}
		if (!synced) {
			if (mh.start_time > 0) {
				mediaHeader->start_time = mh.start_time - (1000000LL * total_samples) / fHeader->rate;
				synced = true;
			}
		}
		speex_bits_read_from(&fBits, (char*)chunkBuffer, chunkSize);
		for (int frame = 0 ; frame < fHeader->frames_per_packet ; frame++) {
			int ret = speex_decode(fDecoderState, &fBits, out_buffer);
			if (ret == -1) {
				break;
			}
			if (ret == -2) {
				TRACE("SpeexDecoder::Decode: corrupted stream?\n");
				break;
			}
			if (speex_bits_remaining(&fBits) < 0) {
				TRACE("SpeexDecoder::Decode: decoding overflow: corrupted stream?\n");
				break;
			}
			if (fHeader->nb_channels == 2) {
				speex_decode_stereo(out_buffer, fHeader->frame_size, fStereoState);
			}
			for (int i = 0 ; i < fHeader->frame_size * fHeader->nb_channels ; i++) {
				out_buffer[i] /= 32000.0;
			}
			out_buffer += fHeader->frame_size * fHeader->nb_channels;
			out_bytes_needed -= fSpeexOutputLength;
			total_samples += fHeader->frame_size;
		}
	}

done:	
	if (!synced) {
		mediaHeader->start_time = fStartTime;
	}
	fStartTime = mediaHeader->start_time + (1000000LL * total_samples) / fHeader->rate;

	*frameCount = (fOutputBufferSize - out_bytes_needed) / fFrameSize;

	if (out_buffer != buffer) {
		return B_OK;
	}
	return B_LAST_BUFFER_ERROR;
}


/*
 * SpeexDecoderPlugin
 */


Decoder *
SpeexDecoderPlugin::NewDecoder(uint index)
{
	return new SpeexDecoder;
}


static media_format speex_formats[1];

status_t
SpeexDecoderPlugin::GetSupportedFormats(media_format ** formats, size_t * count)
{
	media_format_description description = speex_description();
	media_format format = speex_encoded_media_format();

	BMediaFormats mediaFormats;
	status_t result = mediaFormats.InitCheck();
	if (result != B_OK) {
		return result;
	}
	result = mediaFormats.MakeFormatFor(&description, 1, &format);
	if (result != B_OK) {
		return result;
	}
	speex_formats[0] = format;

	*formats = speex_formats;
	*count = 1;

	return result;
}


/*
 * instantiate_plugin
 */


MediaPlugin *instantiate_plugin()
{
	return new SpeexDecoderPlugin;
}
