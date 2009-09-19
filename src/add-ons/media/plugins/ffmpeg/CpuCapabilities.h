/*
 * Copyright (C) 2009 David McPaul
 *
 * All rights reserved. Distributed under the terms of the MIT License.
 */
 
#ifndef __CPU_CAPABILITIES__
#define __CPU_CAPABILITIES__

#define CAPABILITY_MMX 1
#define CAPABILITY_SSE1 2
#define CAPABILITY_SSE2 3
#define CAPABILITY_SSE3 4
#define CAPABILITY_SSSE3 5
#define CAPABILITY_SSE41 6
#define CAPABILITY_SSE42 7


class CPUCapabilities {
	public:
		CPUCapabilities();
		~CPUCapabilities();
	
		bool HasMMX();
		bool HasSSE1();
		bool HasSSE2();
		bool HasSSE3();
		bool HasSSSE3();
		bool HasSSE41();
		bool HasSSE42();
	
		void PrintCapabilities();
	
	private:
		uint32		capabilities;
	
		void setIntelCapabilities();
};

#endif	//__CPU_CAPABILITIES__
