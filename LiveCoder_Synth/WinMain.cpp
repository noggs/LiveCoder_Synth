#include "SynthDll.h"
#include <windows.h>
#include <stdlib.h>
#include <string.h>
#include <tchar.h>
#include <mmdeviceapi.h>
#include <Audioclient.h>
#include "imgui/imgui.h"
#include "ImguiDX11.h"
#include <assert.h>


#pragma comment(lib, "D3D11.lib")
#pragma comment(lib, "D3dcompiler.lib")

void LoadOrUpdateDll();
void ReleaseDll(bool releaseMem);

int InitAudio();
void UpdateAudio();
void ShutdownAudio();

LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	if (ImGui_ImplDX11_WndProcHandler(hWnd, uMsg, wParam, lParam))
		return true;

	switch (uMsg)
	{
	case WM_SIZE:
		HandleWmSize(wParam, lParam);
		return 0;
		break;

	case WM_SYSCOMMAND:
		if ((wParam & 0xfff0) == SC_KEYMENU) // Disable ALT application menu
			return 0;
		break;

	case WM_DESTROY:
		PostQuitMessage(0);
		break;
	}

	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}




int CALLBACK WinMain(
	_In_ HINSTANCE hInstance,
	_In_ HINSTANCE hPrevInstance,
	_In_ LPSTR     lpCmdLine,
	_In_ int       nCmdShow
	) 
{
	static TCHAR szWindowClass[] = _T("LiveCoderSYNTH");
	static TCHAR szTitle[] = _T("LiveCoder SYNTH");

	WNDCLASS wnd = {};
	wnd.lpfnWndProc = WndProc;
	wnd.hInstance = hInstance;
	wnd.lpszClassName = szWindowClass;

	if (!RegisterClass(&wnd))
	{
		return 1;
	}

	HWND hWnd = CreateWindowEx(
		0,
		szWindowClass,
		szTitle,
		WS_OVERLAPPEDWINDOW,
		CW_USEDEFAULT,
		CW_USEDEFAULT,
		800,
		600,
		NULL,
		NULL,
		hInstance,
		NULL
		);

	if (!hWnd)
	{
		return 1;
	}

	// Initialize Direct3D
	if (CreateDeviceD3D(hWnd) < 0)
	{
		CleanupDeviceD3D();
		UnregisterClass(szWindowClass, wnd.hInstance);
		return 1;
	}


	ShowWindow(hWnd, nCmdShow);
	UpdateWindow(hWnd);

	InitImguiDX11(hWnd);

	MSG msg;
	ZeroMemory(&msg, sizeof(msg));

	if (InitAudio() != 0)
	{
		// error lets exit
		msg.message = WM_QUIT;
	}

	while (msg.message != WM_QUIT)
	{
		if (PeekMessage(&msg, NULL, 0U, 0U, PM_REMOVE))
		{
			TranslateMessage(&msg);
			DispatchMessage(&msg);
			continue;
		}
		ImGui_ImplDX11_NewFrame();
		
		LoadOrUpdateDll();

		UpdateAudio();

		// Rendering
		static float clear_col[4] = { 0.1f, 0.1f, 0.1f, 0.0f };
		g_pd3dDeviceContext->ClearRenderTargetView(g_mainRenderTargetView, (float*)&clear_col);
		ImGui::Render();
		g_pSwapChain->Present(0, 0);
	}

	ReleaseDll(true);
	ShutdownAudio();

	ImGui_ImplDX11_Shutdown();
	CleanupDeviceD3D();
	UnregisterClass(szWindowClass, wnd.hInstance);

	return (int)msg.wParam;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////

static HMODULE sSynthDll = 0;
static SynthUpdateFN sProcSynthUpdate = NULL;
static void* sSynthMem = NULL;
static size_t sSynthMemSize = 1024 * 1024 * 1024;

static TCHAR sDllSrcPath[MAX_PATH + 1] = {};

static HANDLE sFileChangeHandle = INVALID_HANDLE_VALUE;
static UINT32 sFileChangeDelay = 0;
static FILETIME sFileChangeTime = { 0 };

void LoadOrUpdateDll()
{
	if (sDllSrcPath[0] == 0)
	{
		GetModuleFileName(NULL, sDllSrcPath, MAX_PATH + 1);
		TCHAR* srcFinalPathChar = _tcsrchr(sDllSrcPath, _T('\\'));
		_tcscpy(srcFinalPathChar + 1, _T("SynthDll.dll"));
	}

	// check for file changes
	if (sFileChangeHandle != INVALID_HANDLE_VALUE)
	{
		DWORD waitStatus = WaitForSingleObject(sFileChangeHandle, 0);
		switch (waitStatus)
		{
		case WAIT_OBJECT_0:
		{
			WIN32_FILE_ATTRIBUTE_DATA fad;
			GetFileAttributesEx(sDllSrcPath, GetFileExInfoStandard, &fad);
			if (memcmp(&fad.ftLastWriteTime, &sFileChangeTime, sizeof(FILETIME)) != 0)
			{
				sFileChangeTime = fad.ftLastWriteTime;
				sFileChangeDelay = 3;
			}
			if (TRUE != FindNextChangeNotification(sFileChangeHandle))
			{
				DWORD err = GetLastError();
				OutputDebugString(_T("Cannot FindNextChangeNotification!"));
			}
			break;
		}
		case WAIT_TIMEOUT:
			break;
		default:
			OutputDebugString(_T("WaitForSingleObject(sFileChangeHandle) failed!"));
			break;
		}
	}
	else
	{
		// check every frame if the dll has changed...
		WIN32_FILE_ATTRIBUTE_DATA fad;
		GetFileAttributesEx(sDllSrcPath, GetFileExInfoStandard, &fad);
		if (memcmp(&fad.ftLastWriteTime, &sFileChangeTime, sizeof(FILETIME)) != 0)
		{
			sFileChangeTime = fad.ftLastWriteTime;
			sFileChangeDelay = 3;
		}
	}

	// auto reload DLL if it has changed
	if (sFileChangeDelay > 0)
	{
		if (--sFileChangeDelay == 0)
		{
			ReleaseDll(false);
		}
	}

	if (sSynthDll == 0)
	{
		// first, copy the dll so it can be overwrittem

		// path to exe (dll should be in the same folder)
		do
		{
			TCHAR srcPath[MAX_PATH + 1];
			TCHAR dstPath[MAX_PATH + 1];
			if (FAILED(GetModuleFileName(NULL, srcPath, MAX_PATH + 1)))
				break;
			if (FAILED(GetModuleFileName(NULL, dstPath, MAX_PATH + 1)))
				break;

			TCHAR* srcFinalPathChar = _tcsrchr(srcPath, _T('\\'));
			_tcscpy(srcFinalPathChar + 1, _T("SynthDll.dll"));
			_tcscpy(_tcsrchr(dstPath, _T('\\')) + 1, _T("SynthDllActive.dll"));

			if (FALSE == CopyFile(srcPath, dstPath, FALSE))
				break;
			
			// Read filetime of the compiled dll
			WIN32_FILE_ATTRIBUTE_DATA fad;
			if (GetFileAttributesEx(srcPath, GetFileExInfoStandard, &fad))
			{
				sFileChangeTime = fad.ftLastWriteTime;
			}

			// setup file change notification
			if (sFileChangeHandle == INVALID_HANDLE_VALUE)
			{
				*(srcFinalPathChar + 1) = '\0';
				sFileChangeHandle = FindFirstChangeNotification(srcPath, FALSE, FILE_NOTIFY_CHANGE_LAST_WRITE | FILE_NOTIFY_CHANGE_SIZE);
				if (sFileChangeHandle == INVALID_HANDLE_VALUE)
				{
					DWORD err = GetLastError();
					OutputDebugString(_T("Unable to setup FindFirstChangeNotification"));
				}
			}

			// load the copy
			sSynthDll = ::LoadLibrary(_T("SynthDllActive.dll"));

			if (sSynthDll)
			{
				sProcSynthUpdate = (SynthUpdateFN)::GetProcAddress(sSynthDll, "SynthUpdate");
			}
		} while (0);
	}

	if (sSynthMem == 0)
	{
		sSynthMem = VirtualAlloc(NULL, sSynthMemSize, MEM_COMMIT | MEM_RESERVE, PAGE_READWRITE);
	}
}


void ReleaseDll(bool releaseMem)
{
	if (sSynthMem && releaseMem)
	{
		VirtualFree(sSynthMem, 0, MEM_RELEASE);
		sSynthMem = NULL;
	}

	if (sSynthDll)
	{
		sProcSynthUpdate = NULL;
		::FreeLibrary(sSynthDll);
		sSynthDll = NULL;
	}
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////


static IAudioClient* sAudioClient = NULL;
static IAudioRenderClient* sAudioRenderClient = NULL;
static WAVEFORMATEX *sAudioFormat = NULL;
static UINT32 sBufferFrameCount = 0;

#define SAFE_RELEASE(punk)  \
              if ((punk) != NULL)  \
                { (punk)->Release(); (punk) = NULL; }


#define EXIT_ON_ERROR(hres)  \
              if (FAILED(hres)) { break; }

int InitAudio()
{
	const CLSID CLSID_MMDeviceEnumerator = __uuidof(MMDeviceEnumerator);
	const IID IID_IMMDeviceEnumerator = __uuidof(IMMDeviceEnumerator);
	const IID IID_IAudioClient = __uuidof(IAudioClient);
	const IID IID_IAudioRenderClient = __uuidof(IAudioRenderClient);

	REFERENCE_TIME hnsRequestedDuration = 100;	// 1000 * 100ns = 100ms buffer
	HRESULT hr;
	IMMDeviceEnumerator *pEnumerator = NULL;
	IMMDevice *pDevice = NULL;
	UINT32 bufferFrameCount = 0;

	do
	{
		hr = CoCreateInstance(CLSID_MMDeviceEnumerator, NULL, CLSCTX_ALL, IID_IMMDeviceEnumerator, (void**)&pEnumerator);
		EXIT_ON_ERROR(hr);

		hr = pEnumerator->GetDefaultAudioEndpoint(
			eRender, eConsole, &pDevice);
		EXIT_ON_ERROR(hr);

		hr = pDevice->Activate(
			IID_IAudioClient, CLSCTX_ALL,
			NULL, (void**)&sAudioClient);
		EXIT_ON_ERROR(hr);

		hr = sAudioClient->GetMixFormat(&sAudioFormat);
		EXIT_ON_ERROR(hr);

		hr = sAudioClient->Initialize(
			AUDCLNT_SHAREMODE_SHARED,
			0,
			hnsRequestedDuration,
			0,
			sAudioFormat,
			NULL);
		EXIT_ON_ERROR(hr);

		// Get the actual size of the allocated buffer.
		hr = sAudioClient->GetBufferSize(&bufferFrameCount);
		EXIT_ON_ERROR(hr);
		sBufferFrameCount = bufferFrameCount;

		hr = sAudioClient->GetService(
			IID_IAudioRenderClient,
			(void**)&sAudioRenderClient);
		EXIT_ON_ERROR(hr);

		SAFE_RELEASE(pEnumerator);
		SAFE_RELEASE(pDevice);

		hr = sAudioClient->Start();  // Start playing.
		EXIT_ON_ERROR(hr);

		// success!
		return 0;
	} while (0);


	CoTaskMemFree(sAudioFormat);
	SAFE_RELEASE(sAudioClient);
	SAFE_RELEASE(sAudioRenderClient);
	return 1;
}


void ShutdownAudio()
{
	HRESULT hr;
	hr = sAudioClient->Stop();  // Stop playing.
	assert(SUCCEEDED(hr));

	CoTaskMemFree(sAudioFormat);
	SAFE_RELEASE(sAudioClient);
	SAFE_RELEASE(sAudioRenderClient);
}


void UpdateAudio()
{
	HRESULT hr;
	UINT32 numFramesPadding;
	UINT32 numFramesAvailable;
	BYTE* pData;

	if (sProcSynthUpdate)
	{


		hr = sAudioClient->GetCurrentPadding(&numFramesPadding);
		assert(SUCCEEDED(hr));

		numFramesAvailable = sBufferFrameCount - numFramesPadding;

		// Grab all the available space in the shared buffer.
		hr = sAudioRenderClient->GetBuffer(numFramesAvailable, &pData);
		assert(SUCCEEDED(hr));


		SynthContext context = {
			sSynthMem,
			sSynthMemSize,
			ImGui::GetInternalState(),
			ImGui::GetInternalStateSize(),
			false,
			numFramesAvailable,
			pData,
			sAudioFormat,
			0
		};

		sProcSynthUpdate(&context);

		hr = sAudioRenderClient->ReleaseBuffer(numFramesAvailable, context.audioOutFlags);
		assert(SUCCEEDED(hr));


		if (context.requestReload)
		{
			ReleaseDll(false);
		}

		if (context.audioOutFlags == AUDCLNT_BUFFERFLAGS_SILENT)
		{
			//request exit
		}
	}


	
}

