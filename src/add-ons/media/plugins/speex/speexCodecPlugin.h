#include "DecoderPlugin.h"
#include "speex.h"
#include "speex_header.h"
#include "speex_stereo.h"
#include "speex_callbacks.h"

class SpeexDecoder : public Decoder
{
public:
				SpeexDecoder();
				~SpeexDecoder();

	void		GetCodecInfo(media_codec_info *info);
	status_t	Setup(media_format *inputFormat,
					  const void *infoBuffer, size_t infoSize);

	status_t	NegotiateOutputFormat(media_format *ioDecodedFormat);

	status_t	Seek(uint32 seekTo,
					 int64 seekFrame, int64 *frame,
					 bigtime_t seekTime, bigtime_t *time);

							 
	status_t	Decode(void *buffer, int64 *frameCount,
					   media_header *mediaHeader, media_decode_info *info);
					   
private:
	SpeexBits		fBits;
	void *			fDecoderState;
	SpeexHeader	*	fHeader;
	SpeexStereoState * fStereoState;

	int				fSpeexOutputLength;

	bigtime_t		fStartTime;
	int				fFrameSize;
	int				fOutputBufferSize;
};


class SpeexDecoderPlugin : public DecoderPlugin
{
public:
	Decoder *	NewDecoder(uint index);
	status_t	GetSupportedFormats(media_format ** formats, size_t * count);
};
