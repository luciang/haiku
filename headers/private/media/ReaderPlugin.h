#ifndef _READER_PLUGIN_H
#define _READER_PLUGIN_H

#include <MediaTrack.h>
#include "MediaPlugin.h"

namespace BPrivate { namespace media {

class PluginManager;

enum {
	B_MEDIA_SEEK_TO_TIME	= 0x10000,
	B_MEDIA_SEEK_TO_FRAME	= 0x20000
};

class Reader {
public:
								Reader();
	virtual						~Reader();

	virtual	const char*			Copyright() = 0;

	virtual	status_t			Sniff(int32* streamCount) = 0;

	virtual	void				GetFileFormatInfo(media_file_format* mff) = 0;

	virtual	status_t			AllocateCookie(int32 streamNumber,
									void** cookie) = 0;
	virtual	status_t			FreeCookie(void* cookie) = 0;

	virtual	status_t			GetStreamInfo(void* cookie, int64* frameCount,
									bigtime_t *duration, media_format* format,
									const void** infoBuffer,
									size_t* infoSize) = 0;

	virtual	status_t			Seek(void* cookie, uint32 flags, int64* frame,
									bigtime_t* time);
	virtual	status_t			FindKeyFrame(void* cookie, uint32 flags,
									int64* frame, bigtime_t* time);

	virtual	status_t			GetNextChunk(void* cookie,
									const void** chunkBuffer, size_t* chunkSize,
									media_header* mediaHeader) = 0;

			BDataIO*			Source() const;

	virtual status_t			Perform(perform_code code, void* data);

private:
	virtual void				_ReservedReader1();
	virtual void				_ReservedReader2();
	virtual void				_ReservedReader3();
	virtual void				_ReservedReader4();
	virtual void				_ReservedReader5();

public: // XXX for test programs only
			void				Setup(BDataIO* source);

			BDataIO*			fSource;

	// needed for plug-in reference count management
	friend class PluginManager;
			MediaPlugin*		fMediaPlugin;

			uint32				fReserved[5];
};


class ReaderPlugin : public virtual MediaPlugin {
public:
	virtual	Reader*				NewReader() = 0;
};

} } // namespace BPrivate::media

using namespace BPrivate::media;

#endif // _READER_PLUGIN_H
