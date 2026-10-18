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
#include "d3dx9.h"
#include "iathook.h"
#include "helpers.h"

#pragma comment(lib, "d3dx9.lib")
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
HMODULE g_hWrapperModule = NULL;

HMODULE d3d9dll = NULL;

bool bForceWindowedMode;
bool bUsePrimaryMonitor;
bool bCenterWindow;
bool bAlwaysOnTop;
bool bDoNotNotifyOnTaskSwitch;
bool bDisplayFPSCounter;
bool bEnableHooks;
bool bCaptureMouse;
float fFPSLimit;
int nFullScreenRefreshRateInHz;
int nForceWindowStyle;

char WinDir[MAX_PATH + 1];

// List of registered window classes and procedures
// WORD classAtom, ULONG_PTR WndProcPtr
std::vector<std::pair<WORD, ULONG_PTR>> WndProcList;

void HookModule(HMODULE hmod);
LRESULT WINAPI CustomWndProcA(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);
LRESULT WINAPI CustomWndProcW(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

class FrameLimiter
{
private:
	static inline double TIME_Frequency = 0.0;
	static inline double TIME_Ticks = 0.0;
	static inline double TIME_Frametime = 0.0;

public:
	static inline ID3DXFont* pFPSFont = nullptr;
	static inline ID3DXFont* pTimeFont = nullptr;

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
	static void ShowFPS(LPDIRECT3DDEVICE9EX device)
	{
		static std::list<int> m_times;

		//https://github.com/microsoft/VCSamples/blob/master/VC2012Samples/Windows%208%20samples/C%2B%2B/Windows%208%20app%20samples/Direct2D%20geometry%20realization%20sample%20(Windows%208)/C%2B%2B/FPSCounter.cpp#L279
		LARGE_INTEGER frequency;
		LARGE_INTEGER time;
		QueryPerformanceFrequency(&frequency);
		QueryPerformanceCounter(&time);

		if (m_times.size() == 50)
			m_times.pop_front();
		m_times.push_back(static_cast<int>(time.QuadPart));

		uint32_t fps = 0;
		if (m_times.size() >= 2)
			fps = static_cast<uint32_t>(0.5f + (static_cast<float>(m_times.size() - 1) * static_cast<float>(frequency.QuadPart)) / static_cast<float>(m_times.back() - m_times.front()));

		static int space = 0;
		if (!pFPSFont || !pTimeFont)
		{
			D3DDEVICE_CREATION_PARAMETERS cparams;
			RECT rect;
			device->GetCreationParameters(&cparams);
			GetClientRect(cparams.hFocusWindow, &rect);

			D3DXFONT_DESC fps_font;
			ZeroMemory(&fps_font, sizeof(D3DXFONT_DESC));
			fps_font.Height = rect.bottom / 20;
			fps_font.Width = 0;
			fps_font.Weight = 400;
			fps_font.MipLevels = 0;
			fps_font.Italic = 0;
			fps_font.CharSet = DEFAULT_CHARSET;
			fps_font.OutputPrecision = OUT_DEFAULT_PRECIS;
			fps_font.Quality = ANTIALIASED_QUALITY;
			fps_font.PitchAndFamily = DEFAULT_PITCH | FF_DONTCARE;
			wchar_t FaceName[] = L"Arial";
			memcpy(&fps_font.FaceName, &FaceName, sizeof(FaceName));

			D3DXFONT_DESC time_font = fps_font;
			time_font.Height = rect.bottom / 35;
			space = fps_font.Height + 5;

			if (D3DXCreateFontIndirect(device, &fps_font, &pFPSFont) != D3D_OK)
				return;

			if (D3DXCreateFontIndirect(device, &time_font, &pTimeFont) != D3D_OK)
				return;
		}
		else
		{
			auto DrawTextOutline = [](ID3DXFont* pFont, FLOAT X, FLOAT Y, D3DXCOLOR dColor, CONST PCHAR cString, ...)
				{
					const D3DXCOLOR BLACK(D3DCOLOR_XRGB(0, 0, 0));
					CHAR cBuffer[101] = "";

					va_list oArgs;
					va_start(oArgs, cString);
					_vsnprintf((cBuffer + strlen(cBuffer)), (sizeof(cBuffer) - strlen(cBuffer)), cString, oArgs);
					va_end(oArgs);

					RECT Rect[5] =
					{
						{ X - 1, Y, X + 500.0f, Y + 50.0f },
						{ X, Y - 1, X + 500.0f, Y + 50.0f },
						{ X + 1, Y, X + 500.0f, Y + 50.0f },
						{ X, Y + 1, X + 500.0f, Y + 50.0f },
						{ X, Y, X + 500.0f, Y + 50.0f },
					};

					if (dColor != BLACK)
					{
						for (auto i = 0; i < 4; i++)
							pFont->DrawText(NULL, cBuffer, -1, &Rect[i], DT_NOCLIP, BLACK);
					}

					pFont->DrawText(NULL, cBuffer, -1, &Rect[4], DT_NOCLIP, dColor);
				};

			static char str_format_fps[] = "%02d";
			static char str_format_time[] = "%.01f ms";
			static const D3DXCOLOR YELLOW(D3DCOLOR_XRGB(0xF7, 0xF7, 0));
			DrawTextOutline(pFPSFont, 10, 10, YELLOW, str_format_fps, fps);
			DrawTextOutline(pTimeFont, 10, space, YELLOW, str_format_time, (1.0f / fps) * 1000.0f);
		}
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

HRESULT m_IDirect3DDevice9Ex::EndScene()
{
	if (bDisplayFPSCounter)
		FrameLimiter::ShowFPS(ProxyInterface);

	return ProxyInterface->EndScene();
}

void CaptureMouse(HWND hWnd)
{
	RECT window_rect;
	GetWindowRect(hWnd, &window_rect);
	if (window_rect.left < 0)
		window_rect.left = 0;
	if (window_rect.top < 0)
		window_rect.top = 0;
	SetCapture(hWnd);
	ClipCursor(&window_rect);
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

	if (nForceWindowStyle != 1) // not borderless fullscreen
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
		int cx, cy;
		UINT uFlags = SWP_SHOWWINDOW;
		LONG lOldStyle = GetWindowLong(hwnd, GWL_STYLE);
		LONG lOldExStyle = GetWindowLong(hwnd, GWL_EXSTYLE);
		LONG lNewStyle, lNewExStyle;

		lOldExStyle &= ~(WS_EX_TOPMOST);

		if (nForceWindowStyle == 1)
		{
			cx = DesktopResX;
			cy = DesktopResY;
		}
		else
		{
			cx = pPresentationParameters->BackBufferWidth;
			cy = pPresentationParameters->BackBufferHeight;

			if (!bCenterWindow)
				uFlags |= SWP_NOMOVE;
		}

		switch (nForceWindowStyle)
		{
		case 1: // borderless fullscreen
		case 4: // borderless window (no style)
			lNewStyle = lOldStyle & ~(WS_CAPTION | WS_THICKFRAME | WS_MINIMIZE | WS_MAXIMIZE | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_DLGFRAME);
			lNewStyle |= (lOldStyle & WS_CHILD) ? 0 : WS_POPUP;
			lNewExStyle = lOldExStyle & ~(WS_EX_CONTEXTHELP | WS_EX_DLGMODALFRAME | WS_EX_CLIENTEDGE | WS_EX_STATICEDGE | WS_EX_WINDOWEDGE | WS_EX_TOOLWINDOW);
			lNewExStyle |= WS_EX_APPWINDOW;
			break;
		case 2: // window
			lNewStyle = (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX);
			lNewExStyle = (WS_EX_APPWINDOW | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE);
			break;
		case 3: // resizable window
			lNewStyle = (WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX | WS_THICKFRAME | WS_MAXIMIZEBOX);
			lNewExStyle = (WS_EX_APPWINDOW | WS_EX_WINDOWEDGE | WS_EX_CLIENTEDGE);
			break;
		}

		if (nForceWindowStyle)
		{
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
		}
		SetWindowPos(hwnd, bAlwaysOnTop ? HWND_TOPMOST : HWND_NOTOPMOST, left, top, cx, cy, uFlags);

		if (bDoNotNotifyOnTaskSwitch || bCaptureMouse)
		{
			if (bCaptureMouse)
				CaptureMouse(hwnd);

			WORD wClassAtom = GetClassWord(hwnd, GCW_ATOM);
			if (wClassAtom != 0)
			{
				bool found = false;
				for (unsigned int i = 0; i < WndProcList.size(); i++) {
					if (WndProcList[i].first == wClassAtom) {
						found = true;
						break;
					}
				}
				if (!found)
				{
					LONG_PTR wndproc = GetWindowLongPtr(hwnd, GWLP_WNDPROC);
					if (wndproc && !IsBadCodePtr((FARPROC)wndproc))
					{
						WndProcList.emplace_back(wClassAtom, wndproc);
						SetWindowLongPtr(hwnd, GWLP_WNDPROC, IsWindowUnicode(hwnd) ? (LONG_PTR)CustomWndProcW : (LONG_PTR)CustomWndProcA);
					}
				}
			}
		}
	}
}

void ForceFullScreenRefreshRateInHz(D3DPRESENT_PARAMETERS* pPresentationParameters)
{
	if (!pPresentationParameters->Windowed)
	{
		std::vector<int> list;
		DISPLAY_DEVICE dd;
		dd.cb = sizeof(DISPLAY_DEVICE);
		DWORD deviceNum = 0;
		while (EnumDisplayDevices(NULL, deviceNum, &dd, 0))
		{
			DISPLAY_DEVICE newdd = { 0 };
			newdd.cb = sizeof(DISPLAY_DEVICE);
			DWORD monitorNum = 0;
			DEVMODE dm = { 0 };
			while (EnumDisplayDevices(dd.DeviceName, monitorNum, &newdd, 0))
			{
				for (auto iModeNum = 0; EnumDisplaySettings(NULL, iModeNum, &dm) != 0; iModeNum++)
					list.emplace_back(dm.dmDisplayFrequency);
				monitorNum++;
			}
			deviceNum++;
		}

		std::sort(list.begin(), list.end());
		if (nFullScreenRefreshRateInHz > list.back() || nFullScreenRefreshRateInHz < list.front() || nFullScreenRefreshRateInHz < 0)
			pPresentationParameters->FullScreen_RefreshRateInHz = list.back();
		else
			pPresentationParameters->FullScreen_RefreshRateInHz = nFullScreenRefreshRateInHz;
	}
}

HRESULT m_IDirect3D9Ex::CreateDevice(UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pPresentationParameters, IDirect3DDevice9** ppReturnedDeviceInterface)
{
	g_hFocusWindow = hFocusWindow ? hFocusWindow : pPresentationParameters->hDeviceWindow;
	if (bForceWindowedMode)
	{
		ForceWindowed(pPresentationParameters);
	}

	if (nFullScreenRefreshRateInHz)
		ForceFullScreenRefreshRateInHz(pPresentationParameters);

	if (bDisplayFPSCounter)
	{
		if (FrameLimiter::pFPSFont)
			FrameLimiter::pFPSFont->Release();
		if (FrameLimiter::pTimeFont)
			FrameLimiter::pTimeFont->Release();
		FrameLimiter::pFPSFont = nullptr;
		FrameLimiter::pTimeFont = nullptr;
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

	if (nFullScreenRefreshRateInHz)
		ForceFullScreenRefreshRateInHz(pPresentationParameters);

	if (bDisplayFPSCounter)
	{
		if (FrameLimiter::pFPSFont)
			FrameLimiter::pFPSFont->OnLostDevice();
		if (FrameLimiter::pTimeFont)
			FrameLimiter::pTimeFont->OnLostDevice();
	}

	auto hRet = ProxyInterface->Reset(pPresentationParameters);

	if (bDisplayFPSCounter)
	{
		if (SUCCEEDED(hRet))
		{
			if (FrameLimiter::pFPSFont)
				FrameLimiter::pFPSFont->OnResetDevice();
			if (FrameLimiter::pTimeFont)
				FrameLimiter::pTimeFont->OnResetDevice();
		}
	}

	return hRet;
}

HRESULT m_IDirect3D9Ex::CreateDeviceEx(THIS_ UINT Adapter, D3DDEVTYPE DeviceType, HWND hFocusWindow, DWORD BehaviorFlags, D3DPRESENT_PARAMETERS* pPresentationParameters, D3DDISPLAYMODEEX* pFullscreenDisplayMode, IDirect3DDevice9Ex** ppReturnedDeviceInterface)
{
	g_hFocusWindow = hFocusWindow ? hFocusWindow : pPresentationParameters->hDeviceWindow;
	if (bForceWindowedMode)
	{
		ForceWindowed(pPresentationParameters, pFullscreenDisplayMode);
	}

	if (nFullScreenRefreshRateInHz)
		ForceFullScreenRefreshRateInHz(pPresentationParameters);

	if (bDisplayFPSCounter)
	{
		if (FrameLimiter::pFPSFont)
			FrameLimiter::pFPSFont->Release();
		if (FrameLimiter::pTimeFont)
			FrameLimiter::pTimeFont->Release();
		FrameLimiter::pFPSFont = nullptr;
		FrameLimiter::pTimeFont = nullptr;
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

	if (nFullScreenRefreshRateInHz)
		ForceFullScreenRefreshRateInHz(pPresentationParameters);

	if (bDisplayFPSCounter)
	{
		if (FrameLimiter::pFPSFont)
			FrameLimiter::pFPSFont->OnLostDevice();
		if (FrameLimiter::pTimeFont)
			FrameLimiter::pTimeFont->OnLostDevice();
	}

	auto hRet = ProxyInterface->ResetEx(pPresentationParameters, pFullscreenDisplayMode);

	if (bDisplayFPSCounter)
	{
		if (SUCCEEDED(hRet))
		{
			if (FrameLimiter::pFPSFont)
				FrameLimiter::pFPSFont->OnResetDevice();
			if (FrameLimiter::pTimeFont)
				FrameLimiter::pTimeFont->OnResetDevice();
		}
	}

	return hRet;
}

LRESULT WINAPI CustomWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, int idx)
{
	if (hWnd == g_hFocusWindow || _fnIsTopLevelWindow(hWnd)) // skip child windows like buttons, edit boxes, etc.
	{
		if (bAlwaysOnTop)
		{
			if ((GetWindowLong(hWnd, GWL_EXSTYLE) & WS_EX_TOPMOST) == 0)
				SetWindowPos(hWnd, HWND_TOPMOST, 0, 0, 0, 0, SWP_NOACTIVATE | SWP_NOMOVE | SWP_NOSIZE);
		}
		switch (uMsg)
		{
		case WM_ACTIVATE:
			if (bDoNotNotifyOnTaskSwitch && LOWORD(wParam) == WA_INACTIVE)
			{
				if ((HWND)lParam == NULL)
					return 0;
				DWORD dwPID = 0;
				GetWindowThreadProcessId((HWND)lParam, &dwPID);
				if (dwPID != GetCurrentProcessId())
					return 0;
			}
			if (bCaptureMouse && LOWORD(wParam) != WA_INACTIVE)
				CaptureMouse(hWnd);
			break;
		case WM_NCACTIVATE:
			if (bDoNotNotifyOnTaskSwitch && LOWORD(wParam) == WA_INACTIVE)
				return 0;
			if (bCaptureMouse && LOWORD(wParam) != WA_INACTIVE)
				CaptureMouse(hWnd);
			break;
		case WM_ACTIVATEAPP:
			if (bDoNotNotifyOnTaskSwitch && wParam == FALSE)
				return 0;
			if (bCaptureMouse && wParam == TRUE)
				CaptureMouse(hWnd);
			break;
		case WM_KILLFOCUS:
			if (bDoNotNotifyOnTaskSwitch)
			{
				if ((HWND)wParam == NULL)
					return 0;
				DWORD dwPID = 0;
				GetWindowThreadProcessId((HWND)wParam, &dwPID);
				if (dwPID != GetCurrentProcessId())
					return 0;
			}
			break;
		case WM_SETFOCUS:
		case WM_MOUSEACTIVATE:
			if (bCaptureMouse)
				CaptureMouse(hWnd);
			break;
		default:
			break;
		}
	}
	WNDPROC OrigProc = WNDPROC(WndProcList[idx].second);
	return OrigProc(hWnd, uMsg, wParam, lParam);
}

LRESULT WINAPI CustomWndProcA(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	WORD wClassAtom = GetClassWord(hWnd, GCW_ATOM);
	if (wClassAtom)
	{
		for (unsigned int i = 0; i < WndProcList.size(); i++) {
			if (WndProcList[i].first == wClassAtom) {
				return CustomWndProc(hWnd, uMsg, wParam, lParam, i);
			}
		}
	}
	// We should never reach here, but having safeguards anyway is good
	return DefWindowProcA(hWnd, uMsg, wParam, lParam);
}

LRESULT WINAPI CustomWndProcW(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
	WORD wClassAtom = GetClassWord(hWnd, GCW_ATOM);
	if (wClassAtom)
	{
		for (unsigned int i = 0; i < WndProcList.size(); i++) {
			if (WndProcList[i].first == wClassAtom) {
				return CustomWndProc(hWnd, uMsg, wParam, lParam, i);
			}
		}
	}
	// We should never reach here, but having safeguards anyway is good
	return DefWindowProcW(hWnd, uMsg, wParam, lParam);
}

typedef ATOM(__stdcall* RegisterClassA_fn)(const WNDCLASSA*);
typedef ATOM(__stdcall* RegisterClassW_fn)(const WNDCLASSW*);
typedef ATOM(__stdcall* RegisterClassExA_fn)(const WNDCLASSEXA*);
typedef ATOM(__stdcall* RegisterClassExW_fn)(const WNDCLASSEXW*);
RegisterClassA_fn oRegisterClassA = NULL;
RegisterClassW_fn oRegisterClassW = NULL;
RegisterClassExA_fn oRegisterClassExA = NULL;
RegisterClassExW_fn oRegisterClassExW = NULL;
ATOM __stdcall hk_RegisterClassA(WNDCLASSA* lpWndClass)
{
	if (!IsValueIntAtom(DWORD(lpWndClass->lpszClassName))) { // argument is a class name
		if (IsSystemClassNameA(lpWndClass->lpszClassName)) { // skip system classes like buttons, common controls, etc.
			return oRegisterClassA(lpWndClass);
		}
	}
	ULONG_PTR pWndProc = ULONG_PTR(lpWndClass->lpfnWndProc);
	lpWndClass->lpfnWndProc = CustomWndProcA;
	WORD wClassAtom = oRegisterClassA(lpWndClass);
	if (wClassAtom != 0)
	{
		WndProcList.emplace_back(wClassAtom, pWndProc);
	}
	return wClassAtom;
}
ATOM __stdcall hk_RegisterClassW(WNDCLASSW* lpWndClass)
{
	if (!IsValueIntAtom(DWORD(lpWndClass->lpszClassName))) { // argument is a class name
		if (IsSystemClassNameW(lpWndClass->lpszClassName)) { // skip system classes like buttons, common controls, etc.
			return oRegisterClassW(lpWndClass);
		}
	}
	ULONG_PTR pWndProc = ULONG_PTR(lpWndClass->lpfnWndProc);
	lpWndClass->lpfnWndProc = CustomWndProcW;
	WORD wClassAtom = oRegisterClassW(lpWndClass);
	if (wClassAtom != 0)
	{
		WndProcList.emplace_back(wClassAtom, pWndProc);
	}
	return wClassAtom;
}
ATOM __stdcall hk_RegisterClassExA(WNDCLASSEXA* lpWndClass)
{
	if (!IsValueIntAtom(DWORD(lpWndClass->lpszClassName))) { // argument is a class name
		if (IsSystemClassNameA(lpWndClass->lpszClassName)) { // skip system classes like buttons, common controls, etc.
			return oRegisterClassExA(lpWndClass);
		}
	}
	ULONG_PTR pWndProc = ULONG_PTR(lpWndClass->lpfnWndProc);
	lpWndClass->lpfnWndProc = CustomWndProcA;
	WORD wClassAtom = oRegisterClassExA(lpWndClass);
	if (wClassAtom != 0)
	{
		WndProcList.emplace_back(wClassAtom, pWndProc);
	}
	return wClassAtom;
}
ATOM __stdcall hk_RegisterClassExW(WNDCLASSEXW* lpWndClass)
{
	if (!IsValueIntAtom(DWORD(lpWndClass->lpszClassName))) { // argument is a class name
		if (IsSystemClassNameW(lpWndClass->lpszClassName)) { // skip system classes like buttons, common controls, etc.
			return oRegisterClassExW(lpWndClass);
		}
	}
	ULONG_PTR pWndProc = ULONG_PTR(lpWndClass->lpfnWndProc);
	lpWndClass->lpfnWndProc = CustomWndProcW;
	WORD wClassAtom = oRegisterClassExW(lpWndClass);
	if (wClassAtom != 0)
	{
		WndProcList.emplace_back(wClassAtom, pWndProc);
	}
	return wClassAtom;
}

typedef HWND(__stdcall* GetForegroundWindow_fn)(void);
GetForegroundWindow_fn oGetForegroundWindow = NULL;

HWND __stdcall hk_GetForegroundWindow()
{
	if (g_hFocusWindow && IsWindow(g_hFocusWindow))
		return g_hFocusWindow;
	return oGetForegroundWindow();
}

typedef HWND(__stdcall* GetActiveWindow_fn)(void);
GetActiveWindow_fn oGetActiveWindow = NULL;

HWND __stdcall hk_GetActiveWindow(void)
{
	HWND hWndActive = oGetActiveWindow();
	if (g_hFocusWindow && hWndActive == NULL && IsWindow(g_hFocusWindow))
	{
		if (GetCurrentThreadId() == GetWindowThreadProcessId(g_hFocusWindow, NULL))
			return g_hFocusWindow;
	}
	return hWndActive;
}

typedef HWND(__stdcall* GetFocus_fn)(void);
GetFocus_fn oGetFocus = NULL;

HWND __stdcall hk_GetFocus(void)
{
	HWND hWndFocus = oGetFocus();
	if (g_hFocusWindow && hWndFocus == NULL && IsWindow(g_hFocusWindow))
	{
		if (GetCurrentThreadId() == GetWindowThreadProcessId(g_hFocusWindow, NULL))
			return g_hFocusWindow;
	}
	return hWndFocus;
}

typedef HMODULE(__stdcall* LoadLibraryA_fn)(LPCSTR lpLibFileName);
LoadLibraryA_fn oLoadLibraryA;

HMODULE __stdcall hk_LoadLibraryA(LPCSTR lpLibFileName)
{
	HMODULE hmod = oLoadLibraryA(lpLibFileName);
	if (hmod)
	{
		HookModule(hmod);
	}
	return hmod;
}

typedef HMODULE(__stdcall* LoadLibraryW_fn)(LPCWSTR lpLibFileName);
LoadLibraryW_fn oLoadLibraryW;

HMODULE __stdcall hk_LoadLibraryW(LPCWSTR lpLibFileName)
{
	HMODULE hmod = oLoadLibraryW(lpLibFileName);
	if (hmod)
	{
		HookModule(hmod);
	}
	return hmod;
}

typedef HMODULE(__stdcall* LoadLibraryExA_fn)(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);
LoadLibraryExA_fn oLoadLibraryExA;

HMODULE __stdcall hk_LoadLibraryExA(LPCSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
{
	HMODULE hmod = oLoadLibraryExA(lpLibFileName, hFile, dwFlags);
	if (hmod && ((dwFlags & (LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE | LOAD_LIBRARY_AS_IMAGE_RESOURCE)) == 0))
	{
		HookModule(hmod);
	}
	return hmod;
}

typedef HMODULE(__stdcall* LoadLibraryExW_fn)(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags);
LoadLibraryExW_fn oLoadLibraryExW;

HMODULE __stdcall hk_LoadLibraryExW(LPCWSTR lpLibFileName, HANDLE hFile, DWORD dwFlags)
{
	HMODULE hmod = oLoadLibraryExW(lpLibFileName, hFile, dwFlags);
	if (hmod && ((dwFlags & (LOAD_LIBRARY_AS_DATAFILE | LOAD_LIBRARY_AS_DATAFILE_EXCLUSIVE | LOAD_LIBRARY_AS_IMAGE_RESOURCE)) == 0))
	{
		HookModule(hmod);
	}
	return hmod;
}

typedef BOOL(__stdcall* FreeLibrary_fn)(HMODULE hLibModule);
FreeLibrary_fn oFreeLibrary;

BOOL __stdcall hk_FreeLibrary(HMODULE hLibModule)
{
	if (hLibModule == g_hWrapperModule)
		return TRUE; // We will stay in memory, thank you very much

	return oFreeLibrary(hLibModule);
}

FARPROC __stdcall hk_GetProcAddress(HMODULE hModule, LPCSTR lpProcName)
{
	__try
	{
		if (!lstrcmpA(lpProcName, "RegisterClassA"))
		{
			if (oRegisterClassA == NULL)
				oRegisterClassA = (RegisterClassA_fn)GetProcAddress(hModule, lpProcName);
			return (FARPROC)hk_RegisterClassA;
		}
		if (!lstrcmpA(lpProcName, "RegisterClassW"))
		{
			if (oRegisterClassW == NULL)
				oRegisterClassW = (RegisterClassW_fn)GetProcAddress(hModule, lpProcName);
			return (FARPROC)hk_RegisterClassW;
		}
		if (!lstrcmpA(lpProcName, "RegisterClassExA"))
		{
			if (oRegisterClassExA == NULL)
				oRegisterClassExA = (RegisterClassExA_fn)GetProcAddress(hModule, lpProcName);
			return (FARPROC)hk_RegisterClassExA;
		}
		if (!lstrcmpA(lpProcName, "RegisterClassExW"))
		{
			if (oRegisterClassExW == NULL)
				oRegisterClassExW = (RegisterClassExW_fn)GetProcAddress(hModule, lpProcName);
			return (FARPROC)hk_RegisterClassExW;
		}
		if (!lstrcmpA(lpProcName, "GetForegroundWindow"))
		{
			if (oGetForegroundWindow == NULL)
				oGetForegroundWindow = (GetForegroundWindow_fn)GetProcAddress(hModule, lpProcName);
			return (FARPROC)hk_GetForegroundWindow;
		}
		if (!lstrcmpA(lpProcName, "GetActiveWindow"))
		{
			if (oGetActiveWindow == NULL)
				oGetActiveWindow = (GetActiveWindow_fn)GetProcAddress(hModule, lpProcName);
			return (FARPROC)hk_GetActiveWindow;
		}
		if (!lstrcmpA(lpProcName, "GetFocus"))
		{
			if (oGetFocus == NULL)
				oGetFocus = (GetFocus_fn)GetProcAddress(hModule, lpProcName);
			return (FARPROC)hk_GetFocus;
		}
		if (!lstrcmpA(lpProcName, "LoadLibraryA"))
		{
			if (oLoadLibraryA == NULL)
				oLoadLibraryA = (LoadLibraryA_fn)GetProcAddress(hModule, lpProcName);
			return (FARPROC)hk_LoadLibraryA;
		}
		if (!lstrcmpA(lpProcName, "LoadLibraryW"))
		{
			if (oLoadLibraryW == NULL)
				oLoadLibraryW = (LoadLibraryW_fn)GetProcAddress(hModule, lpProcName);
			return (FARPROC)hk_LoadLibraryW;
		}
		if (!lstrcmpA(lpProcName, "LoadLibraryExA"))
		{
			if (oLoadLibraryExA == NULL)
				oLoadLibraryExA = (LoadLibraryExA_fn)GetProcAddress(hModule, lpProcName);
			return (FARPROC)hk_LoadLibraryExA;
		}
		if (!lstrcmpA(lpProcName, "LoadLibraryExW"))
		{
			if (oLoadLibraryExW == NULL)
				oLoadLibraryExW = (LoadLibraryExW_fn)GetProcAddress(hModule, lpProcName);
			return (FARPROC)hk_LoadLibraryExW;
		}
		if (!lstrcmpA(lpProcName, "FreeLibrary"))
		{
			if (oFreeLibrary == NULL)
				oFreeLibrary = (FreeLibrary_fn)GetProcAddress(hModule, lpProcName);
			return (FARPROC)hk_FreeLibrary;
		}
	}
	__except ((GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION) ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH)
	{
	}

	return GetProcAddress(hModule, lpProcName);
}

void HookModule(HMODULE hmod)
{
	char modpath[MAX_PATH + 1];
	if (hmod == g_hWrapperModule) // don't hook ourselves
		return;

	if (GetModuleFileNameA(hmod, modpath, MAX_PATH)) {
		if (!_strnicmp(modpath, WinDir, strlen(WinDir))) { // skip system modules
			return;
		}
	}

	// user32.dll imports
	auto originalsUser32 = IATHook::Replace(
		hmod, "user32.dll",
		std::make_tuple("RegisterClassA", (void*)hk_RegisterClassA),
		std::make_tuple("RegisterClassW", (void*)hk_RegisterClassW),
		std::make_tuple("RegisterClassExA", (void*)hk_RegisterClassExA),
		std::make_tuple("RegisterClassExW", (void*)hk_RegisterClassExW),
		std::make_tuple("GetForegroundWindow", (void*)hk_GetForegroundWindow),
		std::make_tuple("GetActiveWindow", (void*)hk_GetActiveWindow),
		std::make_tuple("GetFocus", (void*)hk_GetFocus)
	);

	if (oRegisterClassA == NULL) { auto it = originalsUser32.find("RegisterClassA");   if (it != originalsUser32.end())  oRegisterClassA = (RegisterClassA_fn)it->second.get(); }
	if (oRegisterClassW == NULL) { auto it = originalsUser32.find("RegisterClassW");   if (it != originalsUser32.end())  oRegisterClassW = (RegisterClassW_fn)it->second.get(); }
	if (oRegisterClassExA == NULL) { auto it = originalsUser32.find("RegisterClassExA"); if (it != originalsUser32.end())  oRegisterClassExA = (RegisterClassExA_fn)it->second.get(); }
	if (oRegisterClassExW == NULL) { auto it = originalsUser32.find("RegisterClassExW"); if (it != originalsUser32.end())  oRegisterClassExW = (RegisterClassExW_fn)it->second.get(); }
	if (oGetForegroundWindow == NULL) { auto it = originalsUser32.find("GetForegroundWindow"); if (it != originalsUser32.end()) oGetForegroundWindow = (GetForegroundWindow_fn)it->second.get(); }
	if (oGetActiveWindow == NULL) { auto it = originalsUser32.find("GetActiveWindow");     if (it != originalsUser32.end()) oGetActiveWindow = (GetActiveWindow_fn)it->second.get(); }
	if (oGetFocus == NULL) { auto it = originalsUser32.find("GetFocus");            if (it != originalsUser32.end()) oGetFocus = (GetFocus_fn)it->second.get(); }

	// kernel32.dll imports
	auto originalsKernel32 = IATHook::Replace(
		hmod, "kernel32.dll",
		std::make_tuple("LoadLibraryA", (void*)hk_LoadLibraryA),
		std::make_tuple("LoadLibraryW", (void*)hk_LoadLibraryW),
		std::make_tuple("LoadLibraryExA", (void*)hk_LoadLibraryExA),
		std::make_tuple("LoadLibraryExW", (void*)hk_LoadLibraryExW),
		std::make_tuple("FreeLibrary", (void*)hk_FreeLibrary),
		std::make_tuple("GetProcAddress", (void*)hk_GetProcAddress)
	);

	if (oLoadLibraryA == NULL) { auto it = originalsKernel32.find("LoadLibraryA");   if (it != originalsKernel32.end()) oLoadLibraryA = (LoadLibraryA_fn)it->second.get(); }
	if (oLoadLibraryW == NULL) { auto it = originalsKernel32.find("LoadLibraryW");   if (it != originalsKernel32.end()) oLoadLibraryW = (LoadLibraryW_fn)it->second.get(); }
	if (oLoadLibraryExA == NULL) { auto it = originalsKernel32.find("LoadLibraryExA"); if (it != originalsKernel32.end()) oLoadLibraryExA = (LoadLibraryExA_fn)it->second.get(); }
	if (oLoadLibraryExW == NULL) { auto it = originalsKernel32.find("LoadLibraryExW"); if (it != originalsKernel32.end()) oLoadLibraryExW = (LoadLibraryExW_fn)it->second.get(); }
	if (oFreeLibrary == NULL) { auto it = originalsKernel32.find("FreeLibrary");    if (it != originalsKernel32.end()) oFreeLibrary = (FreeLibrary_fn)it->second.get(); }
}

void HookImportedModules()
{
	HMODULE hModule;
	HMODULE hm;

	hModule = GetModuleHandle(0);

	PIMAGE_DOS_HEADER img_dos_headers = (PIMAGE_DOS_HEADER)hModule;
	PIMAGE_NT_HEADERS img_nt_headers = (PIMAGE_NT_HEADERS)((BYTE*)img_dos_headers + img_dos_headers->e_lfanew);
	PIMAGE_IMPORT_DESCRIPTOR img_import_desc = (PIMAGE_IMPORT_DESCRIPTOR)((BYTE*)img_dos_headers + img_nt_headers->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT].VirtualAddress);
	if (img_dos_headers->e_magic != IMAGE_DOS_SIGNATURE)
		return;

	for (IMAGE_IMPORT_DESCRIPTOR* iid = img_import_desc; iid->Name != 0; iid++) {
		char* mod_name = (char*)((size_t*)(iid->Name + (size_t)hModule));
		hm = GetModuleHandleA(mod_name);
		// ual check
		if (hm && !(GetProcAddress(hm, "DirectInput8Create") != NULL && GetProcAddress(hm, "DirectSoundCreate8") != NULL && GetProcAddress(hm, "InternetOpenA") != NULL)) {
			HookModule(hm);
		}
	}
}

bool WINAPI DllMain(HMODULE hModule, DWORD dwReason, LPVOID lpReserved)
{
	switch (dwReason)
	{
	case DLL_PROCESS_ATTACH:
	{
		g_hWrapperModule = hModule;

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
			strcpy(strrchr(path, '\\'), "\\d3d9.ini");
			bForceWindowedMode = GetPrivateProfileInt("MAIN", "ForceWindowedMode", 0, path) != 0;
			fFPSLimit = static_cast<float>(GetPrivateProfileInt("MAIN", "FPSLimit", 0, path));
			nFullScreenRefreshRateInHz = GetPrivateProfileInt("MAIN", "FullScreenRefreshRateInHz", 0, path);
			bDisplayFPSCounter = GetPrivateProfileInt("MAIN", "DisplayFPSCounter", 0, path);
			bEnableHooks = GetPrivateProfileInt("MAIN", "EnableHooks", 0, path);
			bUsePrimaryMonitor = GetPrivateProfileInt("FORCEWINDOWED", "UsePrimaryMonitor", 0, path) != 0;
			bCenterWindow = GetPrivateProfileInt("FORCEWINDOWED", "CenterWindow", 1, path) != 0;
			bAlwaysOnTop = GetPrivateProfileInt("FORCEWINDOWED", "AlwaysOnTop", 0, path) != 0;
			bDoNotNotifyOnTaskSwitch = GetPrivateProfileInt("FORCEWINDOWED", "DoNotNotifyOnTaskSwitch", 0, path) != 0;
			nForceWindowStyle = GetPrivateProfileInt("FORCEWINDOWED", "ForceWindowStyle", 0, path);
			bCaptureMouse = GetPrivateProfileInt("FORCEWINDOWED", "CaptureMouse", 0, path) != 0;

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

			if (bEnableHooks && (bDoNotNotifyOnTaskSwitch || bCaptureMouse))
			{
				GetSystemWindowsDirectoryA(WinDir, MAX_PATH);

				HMODULE mainModule = GetModuleHandleA(nullptr);

				// Hook main module user32.dll imports
				{
					auto originalsUser32 = IATHook::Replace(
						mainModule, "user32.dll",
						std::make_tuple("RegisterClassA", (void*)hk_RegisterClassA),
						std::make_tuple("RegisterClassW", (void*)hk_RegisterClassW),
						std::make_tuple("RegisterClassExA", (void*)hk_RegisterClassExA),
						std::make_tuple("RegisterClassExW", (void*)hk_RegisterClassExW),
						std::make_tuple("GetForegroundWindow", (void*)hk_GetForegroundWindow),
						std::make_tuple("GetActiveWindow", (void*)hk_GetActiveWindow),
						std::make_tuple("GetFocus", (void*)hk_GetFocus)
					);

					if (oRegisterClassA == NULL) { auto it = originalsUser32.find("RegisterClassA");   if (it != originalsUser32.end())  oRegisterClassA = (RegisterClassA_fn)it->second.get(); }
					if (oRegisterClassW == NULL) { auto it = originalsUser32.find("RegisterClassW");   if (it != originalsUser32.end())  oRegisterClassW = (RegisterClassW_fn)it->second.get(); }
					if (oRegisterClassExA == NULL) { auto it = originalsUser32.find("RegisterClassExA"); if (it != originalsUser32.end())  oRegisterClassExA = (RegisterClassExA_fn)it->second.get(); }
					if (oRegisterClassExW == NULL) { auto it = originalsUser32.find("RegisterClassExW"); if (it != originalsUser32.end())  oRegisterClassExW = (RegisterClassExW_fn)it->second.get(); }
					if (oGetForegroundWindow == NULL) { auto it = originalsUser32.find("GetForegroundWindow"); if (it != originalsUser32.end()) oGetForegroundWindow = (GetForegroundWindow_fn)it->second.get(); }
					if (oGetActiveWindow == NULL) { auto it = originalsUser32.find("GetActiveWindow");     if (it != originalsUser32.end()) oGetActiveWindow = (GetActiveWindow_fn)it->second.get(); }
					if (oGetFocus == NULL) { auto it = originalsUser32.find("GetFocus");            if (it != originalsUser32.end()) oGetFocus = (GetFocus_fn)it->second.get(); }
				}

				// Hook main module kernel32.dll imports (including GetProcAddress)
				{
					auto originalsKernel32 = IATHook::Replace(
						mainModule, "kernel32.dll",
						std::make_tuple("LoadLibraryA", (void*)hk_LoadLibraryA),
						std::make_tuple("LoadLibraryW", (void*)hk_LoadLibraryW),
						std::make_tuple("LoadLibraryExA", (void*)hk_LoadLibraryExA),
						std::make_tuple("LoadLibraryExW", (void*)hk_LoadLibraryExW),
						std::make_tuple("FreeLibrary", (void*)hk_FreeLibrary),
						std::make_tuple("GetProcAddress", (void*)hk_GetProcAddress)
					);

					if (oLoadLibraryA == NULL) { auto it = originalsKernel32.find("LoadLibraryA");   if (it != originalsKernel32.end()) oLoadLibraryA = (LoadLibraryA_fn)it->second.get(); }
					if (oLoadLibraryW == NULL) { auto it = originalsKernel32.find("LoadLibraryW");   if (it != originalsKernel32.end()) oLoadLibraryW = (LoadLibraryW_fn)it->second.get(); }
					if (oLoadLibraryExA == NULL) { auto it = originalsKernel32.find("LoadLibraryExA"); if (it != originalsKernel32.end()) oLoadLibraryExA = (LoadLibraryExA_fn)it->second.get(); }
					if (oLoadLibraryExW == NULL) { auto it = originalsKernel32.find("LoadLibraryExW"); if (it != originalsKernel32.end()) oLoadLibraryExW = (LoadLibraryExW_fn)it->second.get(); }
					if (oFreeLibrary == NULL) { auto it = originalsKernel32.find("FreeLibrary");    if (it != originalsKernel32.end()) oFreeLibrary = (FreeLibrary_fn)it->second.get(); }
				}

				// Ensure d3d9.dll's IAT calls route through our hooks as well
				if (d3d9dll)
				{
					IATHook::Replace(d3d9dll, "kernel32.dll",
						std::make_tuple("GetProcAddress", (void*)hk_GetProcAddress)
					);

					auto u32_d3d9 = IATHook::Replace(d3d9dll, "user32.dll",
						std::make_tuple("GetForegroundWindow", (void*)hk_GetForegroundWindow)
					);
					if (oGetForegroundWindow == NULL) {
						auto it = u32_d3d9.find("GetForegroundWindow");
						if (it != u32_d3d9.end()) oGetForegroundWindow = (GetForegroundWindow_fn)it->second.get();
					}
				}

				// Hook ole32.dll's imports of user32 where applicable
				if (HMODULE ole32 = GetModuleHandleA("ole32.dll"))
				{
					auto originalsOleUser32 = IATHook::Replace(
						ole32, "user32.dll",
						std::make_tuple("RegisterClassA", (void*)hk_RegisterClassA),
						std::make_tuple("RegisterClassW", (void*)hk_RegisterClassW),
						std::make_tuple("RegisterClassExA", (void*)hk_RegisterClassExA),
						std::make_tuple("RegisterClassExW", (void*)hk_RegisterClassExW),
						std::make_tuple("GetActiveWindow", (void*)hk_GetActiveWindow)
					);

					if (oRegisterClassA == NULL) { auto it = originalsOleUser32.find("RegisterClassA");   if (it != originalsOleUser32.end())  oRegisterClassA = (RegisterClassA_fn)it->second.get(); }
					if (oRegisterClassW == NULL) { auto it = originalsOleUser32.find("RegisterClassW");   if (it != originalsOleUser32.end())  oRegisterClassW = (RegisterClassW_fn)it->second.get(); }
					if (oRegisterClassExA == NULL) { auto it = originalsOleUser32.find("RegisterClassExA"); if (it != originalsOleUser32.end())  oRegisterClassExA = (RegisterClassExA_fn)it->second.get(); }
					if (oRegisterClassExW == NULL) { auto it = originalsOleUser32.find("RegisterClassExW"); if (it != originalsOleUser32.end())  oRegisterClassExW = (RegisterClassExW_fn)it->second.get(); }
					if (oGetActiveWindow == NULL) { auto it = originalsOleUser32.find("GetActiveWindow");  if (it != originalsOleUser32.end())  oGetActiveWindow = (GetActiveWindow_fn)it->second.get(); }
				}

				HookImportedModules();
			}
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

IDirect3D9* WINAPI Direct3DCreate9(UINT SDKVersion)
{
	if (!m_pDirect3DCreate9)
	{
		return nullptr;
	}

	IDirect3D9* pD3D9 = m_pDirect3DCreate9(SDKVersion);

	if (pD3D9)
	{
		return new m_IDirect3D9Ex((IDirect3D9Ex*)pD3D9, IID_IDirect3D9);
	}

	return nullptr;
}

HRESULT WINAPI Direct3DCreate9Ex(UINT SDKVersion, IDirect3D9Ex** ppD3D)
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
