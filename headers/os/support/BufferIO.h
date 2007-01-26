/*
 * Copyright 2007, Haiku, Inc. All Rights Reserved.
 * Distributed under the terms of the MIT License.
 */
#ifndef _BUFFER_IO_H
#define _BUFFER_IO_H


#include <DataIO.h>


class BBufferIO : public BPositionIO {
	public:
		BBufferIO(BPositionIO* stream, size_t bufferSize = 65536L,
			bool ownsStream = true);
		virtual ~BBufferIO();

		virtual ssize_t ReadAt(off_t pos, void* buffer, size_t size);
		virtual ssize_t WriteAt(off_t pos, const void* buffer, size_t size);
		virtual off_t Seek(off_t position, uint32 seekMode);
		virtual off_t Position() const;
		virtual status_t SetSize(off_t size);
		virtual status_t Flush();
		BPositionIO* Stream() const;
		size_t BufferSize() const;
		bool OwnsStream() const;
		void SetOwnsStream(bool ownsStream);

		void PrintToStream() const;

	private:
		virtual status_t _Reserved_BufferIO_0(void*);
		virtual status_t _Reserved_BufferIO_1(void*);
		virtual status_t _Reserved_BufferIO_2(void*);
		virtual status_t _Reserved_BufferIO_3(void*);
		virtual status_t _Reserved_BufferIO_4(void*);

		off_t fBufferStart;
		BPositionIO* fStream;
		char* fBuffer;
		size_t fBufferSize;
		size_t fBufferUsed;
		uint32 _reserved_ints[6];
		bool fBufferIsDirty;
		bool fOwnsStream;
		bool _reserved_bools[6];
};

#endif	// _BUFFER_IO_H
