#pragma once
#include <stdint.h>


extern "C"
{
	struct SynthContext
	{
		void*	memBase;
		size_t	memSize;
		void*	imguiState;
		size_t	imguiStateSize;
		bool	requestReload;

		// audio data
		uint32_t	audioFrameCount;
		uint8_t*	audioData;
		struct tWAVEFORMATEX* wfx;
		uint32_t	audioOutFlags;
	};


	// Main update function
	typedef void(*SynthUpdateFN)(SynthContext*);


}	// extern "C"

