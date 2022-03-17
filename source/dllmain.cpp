/**
* Copyright (C) 2020 Elisha Riedlinger
*
* This software is  provided 'as-is', without any express  or implied  warranty. In no event will the
* authors be held liable for any damages arising from the use of this software.
* Permission  is granted  to anyone  to use  this software  for  any  purpose,  including  commercial
* applications, and to alter it and redistribute it freely, subject to the following restrictions:
*
*   1. The origin of this software must not be misrepresented; you must not claim that you  wrote the
*      original  software. If you use this  software  in a product, an  acknowledgment in the product
*      documentation would be appreciated but is not required.
*   2. Altered source versions must  be plainly  marked as such, and  must not be  misrepresented  as
*      being the original software.
*   3. This notice may not be removed or altered from any source distribution.
*/

#include "d3d9.h"

#pragma comment(lib, "winmm.lib") // needed for timeBeginPeriod()/timeEndPeriod()

Direct3DShaderValidatorCreate9Proc m_pDirect3DShaderValidatorCreate9;
PSGPErrorProc m_pPSGPError;
PSGPSampleTextureProc m_pPSGPSampleTexture;
D3DPERF_BeginEventProc m_pD3DPERF_BeginEvent;
D3DPERF_EndEventProc m_pD3DPERF_EndEvent;
D3DPERF_GetStatusProc m_pD3DPERF_GetStatus;
D3DPERF_QueryRepeatFrameProc m_pD3DPERF_QueryRepeatFrame;
D3DPERF_SetMarkerProc m_pD3DPERF_SetMarker;
D3DPERF_SetOptionsProc m_pD3DPERF_SetOptions;
D3DPERF_SetRegionProc m_pD3DPERF_SetRegion;
DebugSetLevelProc m_pDebugSetLevel;
DebugSetMuteProc m_pDebugSetMute;
Direct3D9EnableMaximizedWindowedModeShimProc m_pDirect3D9EnableMaximizedWindowedModeShim;
Direct3DCreate9Proc m_pDirect3DCreate9;
Direct3DCreate9ExProc m_pDirect3DCreate9Ex;

HWND g_hFocusWindow = NULL;

bool bForceWindowedMode;
bool bUsePrimaryMonitor;
bool bCenterWindow;
bool bBorderlessFullscreen;
bool bAlwaysOnTop;
float fFPSLimit;

class FrameLimiter
{
private:
	static inline double TIME_Frequency = 0.0;
	static inline double TIME_Ticks = 0.0;
	static inline double TIME_Frametime = 0.0;

public:
	enum FPSLimitMode { FPS_NONE, FPS_REALTIME, FPS_ACCURATE };
	static void Init(FPSLimitMode mode)
	{
		LARGE_INTEGER frequency;

		QueryPerformanceFrequency(&frequency);
		static constexpr auto TICKS_PER_FRAME = 1;
		auto TICKS_PER_SECOND = (TICKS_PER_FRAME * fFPSLimit);
		if (mode == FPS_ACCURATE)
		{
			TIME_Frametime = 1000.0 / (double)fFPSLimit;
			TIME_Frequency = (double)frequency.QuadPart / 1000.0; // ticks are milliseconds
		}
		else // FPS_REALTIME
		{
			TIME_Frequency = (double)frequency.QuadPart / (double)TICKS_PER_SECOND; // ticks are 1/n frames (n = fFPSLimit)
		}
		Ticks();
	}
	static DWORD Sync_RT()
	{
		DWORD lastTicks, currentTicks;
		LARGE_INTEGER counter;

		QueryPerformanceCounter(&counter);
		lastTicks = (DWORD)TIME_Ticks;
		TIME_Ticks = (double)counter.QuadPart / TIME_Frequency;
		currentTicks = (DWORD)TIME_Ticks;

		return (currentTicks > lastTicks) ? currentTicks - lastTicks : 0;
	}
	static DWORD Sync_SLP()
	{
		LARGE_INTEGER counter;
		QueryPerformanceCounter(&counter);
		double millis_current = (double)counter.QuadPart / TIME_Frequency;
		double millis_delta = millis_current - TIME_Ticks;
		if (TIME_Frametime <= millis_delta)
		{
			TIME_Ticks = millis_current;
			return 1;
		}
		else if (TIME_Frametime - millis_delta > 2.0) // > 2ms
			Sleep(1); // Sleep for ~1ms
		else
			Sleep(0); // yield thread's time-slice (does not actually sleep)

		return 0;
	}

private:
	static void Ticks()
	{
		LARGE_INTEGER counter;
		QueryPerformanceCounter(&counter);
		TIME_Ticks = (double)counter.QuadPart / TIME_Frequency;
	}
};

FrameLimiter::FPSLimitMode mFPSLimitMode = FrameLimiter::FPSLimitMode::FPS_NONE;

HRESULT m_IDirect3DDevice9Ex::Present(CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion)
{
	if (mFPSLimitMode == FrameLimiter::FPSLimitMode::FPS_REALTIME)
		while (!FrameLimiter::Sync_RT());
	else if (mFPSLimitMode == FrameLimiter::FPSLimitMode::FPS_ACCURATE)
		while (!FrameLimiter::Sync_SLP());

	return ProxyInterface->Present(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion);
}

HRESULT m_IDirect3DDevice9Ex::PresentEx(THIS_ CONST RECT* pSourceRect, CONST RECT* pDestRect, HWND hDestWindowOverride, CONST RGNDATA* pDirtyRegion, DWORD dwFlags)
{
	if (mFPSLimitMode == FrameLimiter::FPSLimitMode::FPS_REALTIME)
		while (!FrameLimiter::Sync_RT());
	else if (mFPSLimitMode == FrameLimiter::FPSLimitMode::FPS_ACCURATE)
		while (!FrameLimiter::Sync_SLP());

	return ProxyInterface->PresentEx(pSourceRect, pDestRect, hDestWindowOverride, pDirtyRegion, dwFlags);
}

void ForceWindowed(D3DPRESENT_PARAMETERS* pPresentationParameters, D3DDISPLAYMODEEX* pFullscreenDisplayMode = NULL)
{
	HWND hwnd = pPresentationParameters->hDeviceWindow ? pPresentationParameters->hDeviceWindow : g_hFocusWindow;
	HMONITOR monitor = MonitorFromWindow((!bUsePrimaryMonitor && hwnd) ? hwnd : GetDesktopWindow(), MONITOR_DEFAULTTONEAREST);
	MONITORINFO info;
	info.cbSize = sizeof(MONITORINFO);
	GetMonitorInfo(monitor, &info);
	int DesktopResX = info.rcMonitor.right - info.rcMonitor.left;
	int DesktopResY = info.rcMonitor.bottom - info.rcMonitor.top;

	int left = (int)info.rcMonitor.left;
	int top = (int)info.rcMonitor.top;

	if (!bBorderlessFullscreen)
	{
		left += (int)(((float)DesktopResX / 2.0f) - ((float)pPresentationParameters->BackBufferWidth / 2.0f));
		top += (int)(((float)DesktopResY / 2.0f) - ((float)pPresentationParameters->BackBufferHeight / 2.0f));
	}

	pPresentationParameters->Windowed = 1;

	// This must be set to default (0) on windowed mode as per D3D9 spec
	pPresentationParameters->FullScreen_RefreshRateInHz = D3DPRESENT_RATE_DEFAULT;

	// If exists, this must match the rate in PresentationParameters
	if (pFullscreenDisplayMode != NULL)
		pFullscreenDisplayMode->RefreshRate = D3DPRESENT_RATE_DEFAULT;

	// This flag is not available on windowed mode as per D3D9 spec
	pPresentationParameters->PresentationInterval &= ~D3DPRESENT_DONOTFLIP;

	if (hwnd != NULL)
	{
		UINT uFlags = SWP_SHOWWINDOW;
		if (bBorderlessFullscreen)
		{
			LONG lOldStyle = GetWindowLong(hwnd, GWL_STYLE);
			LONG lOldExStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
			LONG lNewStyle = lOldStyle & ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZE | WS_MAXIMIZE | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_DLGFRAME);
			lNewStyle |= (lOldStyle & WS_CHILD) ? 0 : WS_POPUP;
			LONG lNewExStyle = lOldExStyle & ~(WS_EX_CONTEXTHELP | WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE | WS_EX_TOOLWINDOW);
			lNewExStyle |= WS_EX_APPWINDOW;

			if (lNewStyle != lOldStyle)
			{
				SetWindowLong(hwnd, GWL_STYLE, lNewStyle);
				uFlags |= SWP_FRAMECHANGED;
			}
			if (lNewExStyle != lOldExStyle)
			{
				SetWindowLong(hwnd, GWL_EXSTYLE, lNewExStyle);
				uFlags |= SWP_FRAMECHANGED;
			}
			SetWindowPos(hwnd, bAlwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, left, top, DesktopResX, DesktopResY, uFlags);
		}
		else
		{
			if (!bCenterWindow)
				uFlags |= SWP_NOMOVE;

			SetWindowPos(hwnd, bAlwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, left, top, pPresentationParameters->BackBufferWidth, pPresentationParameters->BackBufferHeight, uFlags);
		}
	}
}

HRESULT m_IDirect3D9Ex::CreateDevice(UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DDevice9** ppReturnedDeviceInterface)
{
	if (bForceWindowedMode)
	{
		g_hFocusWindow = hFocusWindow;
		ForceWindowed(pPresentationParameters);
	}

	HRESULT hr = ProxyInterface->CreateDevice(Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters, ppReturnedDeviceInterface);

	if (SUCCEEDED(hr) && ppReturnedDeviceInterface)
	{
		*ppReturnedDeviceInterface = new m_IDirect3DDevice9Ex((IDirect3DDevice9Ex*)*ppReturnedDeviceInterface, this, IID_IDirect3DDevice9);
	}

	return hr;
}

HRESULT m_IDirect3DDevice9Ex::Reset(D3DPRESENT_PARAMETERS* pPresentationParameters)
{
	if (bForceWindowedMode)
		ForceWindowed(pPresentationParameters);

	return ProxyInterface->Reset(pPresentationParameters);
}

HRESULT m_IDirect3D9Ex::CreateDeviceEx(THIS_ UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pPresentationParameters, D3DDISPLAYMODEEX* pFullscreenDisplayMode, IDirect3DDevice9Ex** ppReturnedDeviceInterface)
{
	if (bForceWindowedMode)
	{
		g_hFocusWindow = hFocusWindow;
		ForceWindowed(pPresentationParameters, pFullscreenDisplayMode);
	}

	HRESULT hr = ProxyInterface->CreateDeviceEx(Adapter, DeviceType, hFocusWindow, BehaviorFlags, pPresentationParameters, pFullscreenDisplayMode, ppReturnedDeviceInterface);

	if (SUCCEEDED(hr) && ppReturnedDeviceInterface)
	{
		*ppReturnedDeviceInterface = new m_IDirect3DDevice9Ex(*ppReturnedDeviceInterface, this, IID_IDirect3DDevice9Ex);
	}

	return hr;
}

HRESULT m_IDirect3DDevice9Ex::ResetEx(THIS_ D3DPRESENT_PARAMETERS* pPresentationParameters, D3DDISPLAYMODEEX* pFullscreenDisplayMode)
{
	if (bForceWindowedMode)
		ForceWindowed(pPresentationParameters, pFullscreenDisplayMode);

	return ProxyInterface->ResetEx(pPresentationParameters, pFullscreenDisplayMode);
}

bool WINAPI DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved)
{
	static HMODULE d3d9dll = nullptr;

	switch (dwReason)
	{
		case DLL_PROCESS_ATTACH:
		{
			// Load dll
			char path[MAX_PATH];
			GetSystemDirectoryA(path, MAX_PATH);
			strcat_s(path, "\\d3d9.dll");
			d3d9dll = LoadLibraryA(path);

			if (d3d9dll)
			{
				// Get function addresses
				m_pDirect3DShaderValidatorCreate9 = (Direct3DShaderValidatorCreate9Proc)GetProcAddress(d3d9dll, "Direct3DShaderValidatorCreate9");
				m_pPSGPError = (PSGPErrorProc)GetProcAddress(d3d9dll, "PSGPError");
				m_pPSGPSampleTexture = (PSGPSampleTextureProc)GetProcAddress(d3d9dll, "PSGPSampleTexture");
				m_pD3DPERF_BeginEvent = (D3DPERF_BeginEventProc)GetProcAddress(d3d9dll, "D3DPERF_BeginEvent");
				m_pD3DPERF_EndEvent = (D3DPERF_EndEventProc)GetProcAddress(d3d9dll, "D3DPERF_EndEvent");
				m_pD3DPERF_GetStatus = (D3DPERF_GetStatusProc)GetProcAddress(d3d9dll, "D3DPERF_GetStatus");
				m_pD3DPERF_QueryRepeatFrame = (D3DPERF_QueryRepeatFrameProc)GetProcAddress(d3d9dll, "D3DPERF_QueryRepeatFrame");
				m_pD3DPERF_SetMarker = (D3DPERF_SetMarkerProc)GetProcAddress(d3d9dll, "D3DPERF_SetMarker");
				m_pD3DPERF_SetOptions = (D3DPERF_SetOptionsProc)GetProcAddress(d3d9dll, "D3DPERF_SetOptions");
				m_pD3DPERF_SetRegion = (D3DPERF_SetRegionProc)GetProcAddress(d3d9dll, "D3DPERF_SetRegion");
				m_pDebugSetLevel = (DebugSetLevelProc)GetProcAddress(d3d9dll, "DebugSetLevel");
				m_pDebugSetMute = (DebugSetMuteProc)GetProcAddress(d3d9dll, "DebugSetMute");
				m_pDirect3D9EnableMaximizedWindowedModeShim = (Direct3D9EnableMaximizedWindowedModeShimProc)GetProcAddress(d3d9dll, "Direct3D9EnableMaximizedWindowedModeShim");
				m_pDirect3DCreate9 = (Direct3DCreate9Proc)GetProcAddress(d3d9dll, "Direct3DCreate9");
				m_pDirect3DCreate9Ex = (Direct3DCreate9ExProc)GetProcAddress(d3d9dll, "Direct3DCreate9Ex");

				// ini
				HMODULE hm = NULL;
				GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCSTR)&Direct3DCreate9, &hm);
				GetModuleFileNameA(hm, path, sizeof(path));
				*strrchr(path, '\\') = '\0';
				strcat_s(path, "\\d3d9.ini");
				bForceWindowedMode = GetPrivateProfileInt("MAIN", "ForceWindowedMode", 0, path) != 0;
				fFPSLimit = static_cast<float>(GetPrivateProfileInt("MAIN", "FPSLimit", 0, path));
				bUsePrimaryMonitor = GetPrivateProfileInt("FORCEWINDOWED", "UsePrimaryMonitor", 0, path) != 0;
				bCenterWindow = GetPrivateProfileInt("FORCEWINDOWED", "CenterWindow", 1, path) != 0;
				bBorderlessFullscreen = GetPrivateProfileInt("FORCEWINDOWED", "BorderlessFullscreen", 0, path) != 0;
				bAlwaysOnTop = GetPrivateProfileInt("FORCEWINDOWED", "AlwaysOnTop", 0, path) != 0;

				if (fFPSLimit > 0.0f)
				{
					FrameLimiter::FPSLimitMode mode = (GetPrivateProfileInt("MAIN", "FPSLimitMode", 1, path) == 2) ? FrameLimiter::FPSLimitMode::FPS_ACCURATE : FrameLimiter::FPSLimitMode::FPS_REALTIME;
					if (mode == FrameLimiter::FPSLimitMode::FPS_ACCURATE)
						timeBeginPeriod(1);

					FrameLimiter::Init(mode);
					mFPSLimitMode = mode;
				}
				else
					mFPSLimitMode = FrameLimiter::FPSLimitMode::FPS_NONE;
			}
		}
		break;
		case DLL_PROCESS_DETACH:
		{
			if (mFPSLimitMode == FrameLimiter::FPSLimitMode::FPS_ACCURATE)
				timeEndPeriod(1);

			if (d3d9dll)
				FreeLibrary(d3d9dll);
		}
		break;
	}

	return true;
}

HRESULT WINAPI Direct3DShaderValidatorCreate9()
{
	if (!m_pDirect3DShaderValidatorCreate9)
	{
		return E_FAIL;
	}

	return m_pDirect3DShaderValidatorCreate9();
}

HRESULT WINAPI PSGPError()
{
	if (!m_pPSGPError)
	{
		return E_FAIL;
	}

	return m_pPSGPError();
}

HRESULT WINAPI PSGPSampleTexture()
{
	if (!m_pPSGPSampleTexture)
	{
		return E_FAIL;
	}

	return m_pPSGPSampleTexture();
}

int WINAPI D3DPERF_BeginEvent(D3DCOLOR col, LPCWSTR wszName)
{
	if (!m_pD3DPERF_BeginEvent)
	{
		return NULL;
	}

	return m_pD3DPERF_BeginEvent(col, wszName);
}

int WINAPI D3DPERF_EndEvent()
{
	if (!m_pD3DPERF_EndEvent)
	{
		return NULL;
	}

	return m_pD3DPERF_EndEvent();
}

DWORD WINAPI D3DPERF_GetStatus()
{
	if (!m_pD3DPERF_GetStatus)
	{
		return NULL;
	}

	return m_pD3DPERF_GetStatus();
}

BOOL WINAPI D3DPERF_QueryRepeatFrame()
{
	if (!m_pD3DPERF_QueryRepeatFrame)
	{
		return FALSE;
	}

	return m_pD3DPERF_QueryRepeatFrame();
}

void WINAPI D3DPERF_SetMarker(D3DCOLOR col, LPCWSTR wszName)
{
	if (!m_pD3DPERF_SetMarker)
	{
		return;
	}

	return m_pD3DPERF_SetMarker(col, wszName);
}

void WINAPI D3DPERF_SetOptions(DWORD dwOptions)
{
	if (!m_pD3DPERF_SetOptions)
	{
		return;
	}

	return m_pD3DPERF_SetOptions(dwOptions);
}

void WINAPI D3DPERF_SetRegion(D3DCOLOR col, LPCWSTR wszName)
{
	if (!m_pD3DPERF_SetRegion)
	{
		return;
	}

	return m_pD3DPERF_SetRegion(col, wszName);
}

HRESULT WINAPI DebugSetLevel(DWORD dw1)
{
	if (!m_pDebugSetLevel)
	{
		return E_FAIL;
	}

	return m_pDebugSetLevel(dw1);
}

void WINAPI DebugSetMute()
{
	if (!m_pDebugSetMute)
	{
		return;
	}

	return m_pDebugSetMute();
}

int WINAPI Direct3D9EnableMaximizedWindowedModeShim(BOOL mEnable)
{
	if (!m_pDirect3D9EnableMaximizedWindowedModeShim)
	{
		return NULL;
	}

	return m_pDirect3D9EnableMaximizedWindowedModeShim(mEnable);
}

IDirect3D9 *WINAPI Direct3DCreate9(UINT SDKVersion)
{
	if (!m_pDirect3DCreate9)
	{
		return nullptr;
	}

	IDirect3D9 *pD3D9 = m_pDirect3DCreate9(SDKVersion);

	if (pD3D9)
	{
		return new m_IDirect3D9Ex((IDirect3D9Ex*)pD3D9, IID_IDirect3D9);
	}

	return nullptr;
}

HRESULT WINAPI Direct3DCreate9Ex(UINT SDKVersion, IDirect3D9Ex **ppD3D)
{
	if (!m_pDirect3DCreate9Ex)
	{
		return E_FAIL;
	}

	HRESULT hr = m_pDirect3DCreate9Ex(SDKVersion, ppD3D);

	if (SUCCEEDED(hr) && ppD3D)
	{
		*ppD3D = new m_IDirect3D9Ex(*ppD3D, IID_IDirect3D9Ex);
	}

	return hr;
}
