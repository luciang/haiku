#include "OggSeekable.h"
#include "OggSpeexSeekable.h"
#include "OggTobiasSeekable.h"
#include "OggVorbisSeekable.h"
#include <Autolock.h>
#include <stdio.h>

#define TRACE_THIS 1
#if TRACE_THIS
  #define TRACE printf
#else
  #define TRACE(a...) ((void)0)
#endif

/*
 * OggSeekable codec identification and instantiation
 */

/* static */ OggSeekable *
OggSeekable::makeOggSeekable(BPositionIO * positionIO, BLocker * positionLock,
                             long serialno, const ogg_packet & packet)
{
	TRACE("OggSeekable::makeOggSeekable\n");
	OggSeekable * stream;
	if (OggVorbisSeekable::IsValidHeader(packet)) {
		stream = new OggVorbisSeekable(serialno);
	} else if (OggTobiasSeekable::IsValidHeader(packet)) {
		stream = new OggTobiasSeekable(serialno);
	} else if (OggSpeexSeekable::IsValidHeader(packet)) {
		stream = new OggSpeexSeekable(serialno);
//	} else if (OggTheoraSeekable::IsValidHeader(packet)) {
//		stream = new OggTheoraSeekable(serialno);
	} else {
		stream = new OggSeekable(serialno);
	}
	stream->fPositionIO = positionIO;
	stream->fPositionLock = positionLock;
	return stream;
}


/*
 * OggSeekable generic functions
 */

OggSeekable::OggSeekable(long serialno)
	: OggTrack(serialno)
{
	TRACE("OggSeekable::OggSeekable\n");
	ogg_sync_init(&fSync);
	ogg_stream_init(&fStreamState,serialno);
	fPosition = 0;
	fLastPagePosition = 0;
	fFrameRate = 0;
}


OggSeekable::~OggSeekable()
{
	// free internal stream state storage
	ogg_sync_clear(&fSync);
	ogg_stream_clear(&fStreamState);
}


static void 
insert_position(std::vector<off_t> & positions, off_t position)
{
	positions.push_back(position);
}


status_t
OggSeekable::AddPage(off_t position, const ogg_page & page)
{
	TRACE("OggSeekable::AddPage\n");
	insert_position(fPagePositions, position);
	char * buffer;
	// copy the header to our local sync
	buffer = ogg_sync_buffer(&fSync, page.header_len);
	memcpy(buffer, page.header, page.header_len);
	ogg_sync_wrote(&fSync, page.header_len);
	// copy the body to our local sync
	buffer = ogg_sync_buffer(&fSync, page.body_len);
	memcpy(buffer, page.body, page.body_len);
	ogg_sync_wrote(&fSync, page.body_len);
	fPosition = position + page.header_len + page.body_len;
	return B_OK;
}


void
OggSeekable::SetLastPagePosition(off_t position)
{
	fLastPagePosition = max_c(fLastPagePosition, position);
}


off_t
OggSeekable::GetLastPagePosition()
{
	return fLastPagePosition;
}


off_t
OggSeekable::Seek(off_t position, int32 mode)
{
	BAutolock autolock(fPositionLock);
	off_t result = fPositionIO->Seek(position, mode);
	if (result >= 0) {
		fPosition = result;
		ogg_sync_reset(&fSync);
		ogg_stream_reset(&fStreamState);
	}
	return result;
}


off_t
OggSeekable::Position(void) const
{
	return fPosition;
}


status_t
OggSeekable::GetSize(off_t * size) 
{
	BAutolock autolock(fPositionLock);
	off_t prior = fPositionIO->Position();
	off_t result = fPositionIO->Seek(0, SEEK_END);
	fPositionIO->Seek(prior, SEEK_SET);
	if (result >= 0) {
		*size = result;
		result = B_OK;
	}
	return result;
}


status_t
OggSeekable::ReadPage(ogg_page * page, int read_size)
{
//	TRACE("OggSeekable::ReadPage (%llu)\n", fPosition);
	int result;
	while ((result = ogg_sync_pageout(&fSync, page)) == 1) {
		if (ogg_page_serialno(page) == GetSerial()) {
			return B_OK;
		}
	}
	BAutolock autolock(fPositionLock);
	// align to page boundary
	int offset;
	while ((offset = ogg_sync_pageseek(&fSync, page)) <= 0) {
		if (offset == 0) {
			char * buffer = ogg_sync_buffer(&fSync, read_size);
			ssize_t bytes = fPositionIO->ReadAt(fPosition, buffer, read_size);
			if (bytes == 0) {
				TRACE("OggSeekable::ReadPage (%llu)\n", fPosition);
				TRACE("OggSeekable::ReadPage: ReadAt: no data\n");
				return B_LAST_BUFFER_ERROR;
			}
			if (bytes < 0) {
				TRACE("OggSeekable::ReadPage (%llu)\n", fPosition);
				TRACE("OggSeekable::ReadPage: ReadAt: error\n");
				return bytes;
			}
			fPosition += bytes;
			if (ogg_sync_wrote(&fSync, bytes) != 0) {
				TRACE("OggSeekable::ReadPage (%llu)\n", fPosition);
				TRACE("OggSeekable::ReadPage: ogg_sync_wrote failed?: error\n");
				return B_ERROR;
			}
		}
	}
	// repeat until this is one of our pages
	while (ogg_page_serialno(page) != GetSerial()) {
		int result;
		// read the page
		while ((result = ogg_sync_pageout(&fSync, page)) == 0) {
			char * buffer = ogg_sync_buffer(&fSync, read_size);
			ssize_t bytes = fPositionIO->ReadAt(fPosition, buffer, read_size);
			if (bytes == 0) {
				TRACE("OggSeekable::ReadPage (%llu)\n", fPosition);
				TRACE("OggSeekable::ReadPage: ReadAt 2: no data\n");
				return B_LAST_BUFFER_ERROR;
			}
			if (bytes < 0) {
				TRACE("OggSeekable::ReadPage (%llu)\n", fPosition);
				TRACE("OggSeekable::ReadPage: ReadAt 2: error\n");
				return bytes;
			}
			fPosition += bytes;
			if (ogg_sync_wrote(&fSync, bytes) != 0) {
				TRACE("OggSeekable::ReadPage (%llu)\n", fPosition);
				TRACE("OggSeekable::ReadPage: ogg_sync_wrote 2 failed?: error\n");
				return B_ERROR;
			}
		}
		if (result == -1) {
			TRACE("OggSeekable::ReadPage (%llu)\n", fPosition);
			TRACE("OggSeekable::ReadPage: ogg_sync_pageout: not synced!\n");
			debugger("lost sync");
			return B_ERROR;
		}
		if (ogg_page_version(page) != 0) {
			TRACE("OggSeekable::ReadPage (%llu)\n", fPosition);
			TRACE("OggSeekable::ReadPage: ogg_page_version: error in page encoding??\n");
#ifdef STRICT_OGG
			return B_ERROR;
#endif
		}
	}
	return B_OK;
}


status_t
OggSeekable::GetStreamInfo(int64 *frameCount, bigtime_t *duration,
                         media_format *format)
{
	TRACE("OggSeekable::GetStreamInfo\n");
	status_t result = B_OK;
	ogg_packet packet;
	if (GetHeaderPackets().size() < 1) {
		result = GetPacket(&packet);
		if (result != B_OK) {
			TRACE("OggSeekable::GetStreamInfo failed to get header packet\n");
			return result;
		}
		SaveHeaderPacket(packet);
	}
	packet = GetHeaderPackets()[0];
	if (!packet.b_o_s) {
		TRACE("OggSeekable::GetStreamInfo failed : not beginning of stream\n");
		return B_ERROR; // first packet was not beginning of stream
	}

	// parse header packet
	uint32 four_bytes = *(uint32*)(&packet);

	// get the format for the description
	media_format_description description;
	description.family = B_MISC_FORMAT_FAMILY;
	description.u.misc.file_format = 'OggS';
	description.u.misc.codec = four_bytes;
	BMediaFormats formats;
	result = formats.InitCheck();
	if (result == B_OK) {
		result = formats.GetFormatFor(description, format);
	}
	if (result != B_OK) {
		// ignore the error, allow the user to use ReadChunk interface
	}

	// fill out format from header packet
	format->user_data_type = B_CODEC_TYPE_INFO;
	strncpy((char*)format->user_data, (char*)(&packet), 4);

	format->SetMetaData((void*)&GetHeaderPackets(),sizeof(GetHeaderPackets()));
	*duration = 140000000;
	*frameCount = 60000;
	return B_OK;
}

status_t
OggSeekable::Seek(uint32 seekTo, int64 *frame, bigtime_t *time)
{
	int64 granulepos;
	if (seekTo & B_MEDIA_SEEK_TO_TIME) {
		TRACE("OggSeekable::Seek: seek to time: %lld\n", *time);
		if ((fFrameRate == 0) && (*time != 0)) {
			TRACE("OggSeekable::Seek: fFrameRate unset, seek to non-zero time unsupported\n");
			return B_UNSUPPORTED;
		}
		granulepos = fFirstGranulepos + (int64) (*time * (long long)fFrameRate) / 1000000LL;
	} else if (seekTo & B_MEDIA_SEEK_TO_FRAME) {
		granulepos = fFirstGranulepos + *frame;
	} else {
		TRACE("OggSeekable::Seek: bad seekTo");
		return B_BAD_VALUE;
	}
	TRACE("OggSeekable::Seek: seek to granulepos: %lld\n", granulepos);

	// find the first packet with data in it
	ogg_page page;
	off_t left = 0;
	Seek(0, SEEK_SET);
	uint header_packets_left = GetHeaderPackets().size();
	while (header_packets_left > 0) {
		status_t status = ReadPage(&page, B_PAGE_SIZE);
		if (status != B_OK) {
			TRACE("OggSeekable::Seek: ReadPage = %s\n", strerror(status));
			return status;
		}
		header_packets_left -= ogg_page_packets(&page);
		left += page.header_len + page.body_len;
	}
	if (ogg_page_continued(&page)) {
		TRACE("OggSeekable::Seek: warning: data packet began in header page!\n");
		// how to handle this?
	}
	
	// binary search to find our place
	int64 left_granulepos = 0;
	off_t right = GetLastPagePosition();
	int64 right_granulepos = 0;
	bool done = false;
	while (!done) {
		TRACE("  Seek: [%llu,%llu]: ", left, right);
		ogg_sync_reset(&fSync);
		ogg_stream_reset(&fStreamState);
		if (right - left > B_PAGE_SIZE) {
			fPosition = (right + left) / 2;
		} else {
			fPosition = left;
			done = true;
		}
		do {
			status_t status = ReadPage(&page, B_PAGE_SIZE);
			if (status != B_OK) {
				TRACE("OggSeekable::Seek: ReadPage = %s\n", strerror(status));
				return status;
			}
			if (ogg_stream_pagein(&fStreamState, &page) != 0) {
				TRACE("OggSeekable::Seek: ogg_stream_pagein: failed??\n");
				return B_ERROR;
			}
		} while (ogg_page_granulepos(&page) == -1);
		TRACE("granulepos = %lld, ", ogg_page_granulepos(&page));
		TRACE("fPosition = %llu\n", fPosition);
		// check the granulepos of the page against the requested frame
		if (ogg_page_granulepos(&page) < granulepos) {
			// The packet that the frame is in is someplace after
			// the last complete packet in this page.  (It may yet
			// be in this page, but in a packet completed on the
			// next page.)
			left = (right + left) / 2;
			left_granulepos = ogg_page_granulepos(&page);
			continue;
		} else {
			right = (right + left) / 2;
			right_granulepos = ogg_page_granulepos(&page);
			continue;
		}
	}

	// if not zero, look for a granulepos in a packet
	if (*frame != 0) {
		ogg_packet packet;
		do {
			status_t status = GetPacket(&packet);
			if (status != B_OK) {
				TRACE("OggSeekable::Seek: GetPacket = %s\n", strerror(status));
				return status;
			}
		} while (packet.granulepos == -1);
		granulepos = packet.granulepos;
		fCurrentFrame = granulepos - fFirstGranulepos;
		fCurrentTime = (1000000LL * fCurrentFrame) / (long long)fFrameRate;
	} else {
		fCurrentFrame = 0;
		fCurrentTime = 0;
	}
	TRACE("OggSeekable::Seek done: ");
	TRACE("[%lld,%lld] => ", left_granulepos, right_granulepos);
	TRACE("found frame %lld at time %llu\n", fCurrentFrame, fCurrentTime);
	*frame = fCurrentFrame;
	*time = fCurrentTime;
	return B_OK;
}

		// The packet that the frame is in is someplace before or
		// in the last complete packet in this page. (It may be in
		// a prior page)
/*		ogg_packet packet;
		do {
			switch (ogg_stream_packetpeek(&fStreamState, &packet)) {
			case 1: // a packet found
				break;
			case 0: { // no packets found
				status_t status = ReadPage(&page, B_PAGE_SIZE);
				if (status != B_OK) {
					TRACE("OggSeekable::Seek: ReadPage = %s\n", strerror(status));
					return status;
				}
				if (ogg_stream_pagein(&fStreamState, &page) != 0) {
					TRACE("OggSeekable::Seek: ogg_stream_pagein: failed??\n");
					return B_ERROR;
				}
				continue;
			default: // error condition
				TRACE("OggSeekable::Seek: error peeking for packet\n");
				continue;
			}
			if (packet.granulepos == -1) {
				// useless packet with no seek info, eat it
				ogg_stream_packetout(&fStreamState, &packet);
			}
		} while (packet.granulepos == -1);
		if (packet.granulepos 
*/
/*		if (ogg_page_granulepos(&page) == *frame) {
			// The frame is in the last packet to end on this page.
			// If the last packet on this page was continued from
			// the prior page, we might need to go back farther to 
			// get the whole packet.
			if ((ogg_page_packets(&page) == 1) &&
			    (ogg_page_continued(&page)) &&
				(ogg_stream_packetpeek(&fStreamState, NULL) != 1)) {
				right = (right + left) / 2;
				continue;
			}
			// The frame is the last frame in the last packet on 
			// this page.  If there are more than 1 packet, discard 
			// all but the last and return.
			for (int i = 0 ; i < packets - 1 ; i++) {
				ogg_packet packet;
				GetPacket(&packet);
			}
			return B_OK;
		}*/


// the default chunk is an ogg packet
status_t
OggSeekable::GetNextChunk(void **chunkBuffer, int32 *chunkSize,
             media_header *mediaHeader)
{
	status_t result = GetPacket(&fChunkPacket);
	if (result != B_OK) {
		TRACE("OggSeekable::GetNextChunk failed: GetPacket = %s\n", strerror(result));
		return result;
	}
	*chunkBuffer = &fChunkPacket;
	*chunkSize = sizeof(fChunkPacket);
	return B_OK;
}


// subclass pull input function
status_t
OggSeekable::GetPacket(ogg_packet * packet)
{
	// at the end, pull the packet
	while (ogg_stream_packetpeek(&fStreamState, NULL) != 1) {
		ogg_page page;
		status_t status = ReadPage(&page);
		if (status != B_OK) {
			TRACE("OggSeekable::GetPacket: ReadPage = %s\n", strerror(status));
			return status;
		}
		if (ogg_stream_pagein(&fStreamState, &page) != 0) {
			TRACE("OggSeekable::GetPacket: ogg_stream_pagein: failed??\n");
			return B_ERROR;
		}
	}
	if (ogg_stream_packetout(&fStreamState, packet) != 1) {
		TRACE("OggSeekable::GetPacket: ogg_stream_packetout failed\n");
		return B_ERROR;
	}
	return B_OK;
}

