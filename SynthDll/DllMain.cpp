#include <SynthDll.h>
#include <Windows.h>
#include <imgui/imgui.h>
#include <math.h>

extern "C" 
{

	struct InternalState
	{
		bool isInitialized;
		bool showTestWindow;

		float Frequency;
		float Phase;
	};


	__declspec(dllexport) void SynthUpdate(SynthContext* ctx)
	{
		InternalState* state = (InternalState*)ctx->memBase;
		if (state->isInitialized == false)
		{
			state->isInitialized = true;
			state->Frequency = 120.0f;
		}

		ImGui::SetInternalState(ctx->imguiState);

		if (ImGui::Begin("Reload"))
		{
			if (ImGui::Button("Reload DLL"))
			{
				ctx->requestReload = true;
			}

			ImGui::DragFloat("Frequency", &state->Frequency, 10.0f);
		}
		ImGui::End();
		
		// fill audio buffer

		if (state->Frequency < 10.0f)
			state->Frequency = 10.0f;
		if (state->Frequency > 8000.0f)
			state->Frequency = 8000.0f;

		int sampleRate = ctx->wfx->nSamplesPerSec;
		int numChannels = ctx->wfx->nChannels;

		// ok now we know we can fill it with float data!
		float* output = (float*)ctx->audioData;

		// Compute the phase increment for the current frequency
		float phaseInc = 2 * 3.1415f * state->Frequency / sampleRate;

		float volume = 0.05f;

		// Generate the samples
		for (UINT32 i = 0; i < ctx->audioFrameCount; i++)
		{
			float x = float(volume * sin(state->Phase));
			for (int ch = 0; ch < numChannels; ch++)
				*output++ = x;
			state->Phase += phaseInc;
		}

		// Bring phase back into range [0, 2pi]
		state->Phase = fmodf(state->Phase, 2 * 3.1415f);
	}

}
