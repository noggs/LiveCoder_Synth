#pragma once

#include <d3d11.h>
#include "imgui/examples/directx11_example/imgui_impl_dx11.h"

extern ID3D11Device*            g_pd3dDevice;
extern ID3D11DeviceContext*     g_pd3dDeviceContext;
extern IDXGISwapChain*          g_pSwapChain;
extern ID3D11RenderTargetView*  g_mainRenderTargetView;


void InitImguiDX11(HWND hWnd);
void HandleWmSize(WPARAM wParam, LPARAM lParam);
void CreateRenderTarget();
HRESULT CreateDeviceD3D(HWND hWnd);
void CleanupRenderTarget();
void CleanupDeviceD3D();

LRESULT ImGui_ImplDX11_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);


