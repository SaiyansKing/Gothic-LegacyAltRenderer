#include "hook.h"

#include <ddraw.h>
#include <algorithm>
#include <vector>

extern bool IsG1;
extern bool IsG2;

std::vector<DDSURFACEDESC2> g_d3d7videoModes;
bool g_d3d7FullscreenExclusive = false;
int g_d3d7BackBufferWidth = 800;
int g_d3d7BackBufferHeight = 600;

void FetchDisplayModes()
{
    HWND gothicHWND = *reinterpret_cast<HWND*>(IsG1 ? 0x86F4B8 : 0x8D422C);
    HMONITOR useMonitor = MonitorFromWindow(gothicHWND, MONITOR_DEFAULTTOPRIMARY);

    MONITORINFOEXA miex;
    miex.cbSize = sizeof(MONITORINFOEXA);
    strcat_s(miex.szDevice, "\\\\.\\DISPLAY1");
    GetMonitorInfoA(useMonitor, &miex);
    for(DWORD i = 0;; ++i)
    {
        DEVMODEA devmode = {};
        devmode.dmSize = sizeof(DEVMODEA);
        devmode.dmDriverExtra = 0;
        if(!EnumDisplaySettingsA(miex.szDevice, i, &devmode) || (devmode.dmFields & DM_BITSPERPEL) != DM_BITSPERPEL)
            break;

        if(devmode.dmBitsPerPel < 24)
            continue;

        auto it = std::find_if(g_d3d7videoModes.begin(), g_d3d7videoModes.end(),
            [&devmode](DDSURFACEDESC2& a) {return (a.dwWidth == devmode.dmPelsWidth && a.dwHeight == devmode.dmPelsHeight); });
        if(it == g_d3d7videoModes.end())
        {
            DDSURFACEDESC2 newMode = {};
            newMode.dwSize = sizeof(DDSURFACEDESC2);
            newMode.dwWidth = devmode.dmPelsWidth;
            newMode.dwHeight = devmode.dmPelsHeight;
            newMode.ddpfPixelFormat.dwRGBBitCount = 32;
            newMode.dwRefreshRate = 60;
            g_d3d7videoModes.push_back(newMode);
        }
    }
}

HRESULT __stdcall Hooked_EnumModeCallback(LPDDSURFACEDESC2 DDSD, LPVOID lpContext)
{
    if(DDSD->ddpfPixelFormat.dwRGBBitCount < 24)
        return DDENUMRET_OK;

    auto it = std::find_if(g_d3d7videoModes.begin(), g_d3d7videoModes.end(),
        [&DDSD](DDSURFACEDESC2& a) {return (a.dwWidth == DDSD->dwWidth && a.dwHeight == DDSD->dwHeight); });
    if(it == g_d3d7videoModes.end())
    {
        g_d3d7videoModes.push_back(*DDSD);
        g_d3d7videoModes.back().ddpfPixelFormat.dwRGBBitCount = 32;
        g_d3d7videoModes.back().dwRefreshRate = 60;
    }
    return DDENUMRET_OK;
}

HRESULT __fastcall Hooked_EnumDisplayModes(DWORD ddraw7, DWORD _EDX, LPDDSURFACEDESC2 lpDDSurfaceDesc2, LPVOID lpContext, LPDDENUMMODESCALLBACK2 lpEnumModesCallback)
{
    HRESULT res = DD_OK;
    if(g_d3d7videoModes.empty())
    {
        res = reinterpret_cast<HRESULT(__stdcall*)(DWORD, int, LPDDSURFACEDESC2, LPVOID, LPDDENUMMODESCALLBACK2)>
            (*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(ddraw7) + 0x20))(ddraw7, 0, lpDDSurfaceDesc2, lpContext, Hooked_EnumModeCallback);
        if(FAILED(res) || g_d3d7videoModes.empty())
        {
            FetchDisplayModes();
            res = DD_OK;
        }
        {
            auto it = std::find_if(g_d3d7videoModes.begin(), g_d3d7videoModes.end(),
                [](DDSURFACEDESC2& a) {return (a.dwWidth == 800 && a.dwHeight == 600);});
            if(it == g_d3d7videoModes.end())
            {
                DDSURFACEDESC2 newMode = {};
                newMode.dwSize = sizeof(DDSURFACEDESC2);
                newMode.dwWidth = 800;
                newMode.dwHeight = 600;
                newMode.ddpfPixelFormat.dwRGBBitCount = 32;
                newMode.dwRefreshRate = 60;
                g_d3d7videoModes.push_back(newMode);
            }
        }
        std::sort(g_d3d7videoModes.begin(), g_d3d7videoModes.end(), [](DDSURFACEDESC2& a, DDSURFACEDESC2& b) -> bool
        {
            return (a.dwWidth < b.dwWidth) || ((a.dwWidth == b.dwWidth) && (a.dwHeight < b.dwHeight));
        });
    }
    if(SUCCEEDED(res))
    {
        for(DDSURFACEDESC2& mode : g_d3d7videoModes)
            (*lpEnumModesCallback)(&mode, lpContext);
    }
    return res;
}

HRESULT __stdcall Hooked_CreateContext_G1(DWORD deviceIndex, DWORD flags, HWND hwnd, HWND hwndFocus, DWORD numColorBits, DWORD numAlphaBits, DWORD numDepthbits, DWORD numStencilBits,
    DWORD numBackBuffers, DWORD width, DWORD height, DWORD refreshRate, void* ppCtx)
{
    HRESULT res = reinterpret_cast<HRESULT(__stdcall*)(DWORD, DWORD, HWND, HWND, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, void*)>
        (0x75B71A)(deviceIndex, flags, hwnd, hwndFocus, numColorBits, numAlphaBits, numDepthbits, numStencilBits, numBackBuffers, width, height, refreshRate, ppCtx);
    if(res != S_OK)
    {
        // Try hard reset
        reinterpret_cast<void(__cdecl*)(void)>(0x75D626)();
        reinterpret_cast<void(__cdecl*)(void)>(0x75C9A3)();
        res = reinterpret_cast<HRESULT(__stdcall*)(DWORD, DWORD, HWND, HWND, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, void*)>
            (0x75B71A)(deviceIndex, flags, hwnd, hwndFocus, numColorBits, numAlphaBits, numDepthbits, numStencilBits, numBackBuffers, width, height, refreshRate, ppCtx);
    }
    return res;
}

HRESULT __stdcall Hooked_CreateContext_G2(DWORD deviceIndex, DWORD flags, HWND hwnd, HWND hwndFocus, DWORD numColorBits, DWORD numAlphaBits, DWORD numDepthbits, DWORD numStencilBits,
    DWORD numBackBuffers, DWORD width, DWORD height, DWORD refreshRate, void* ppCtx)
{
    HRESULT res = reinterpret_cast<HRESULT(__stdcall*)(DWORD, DWORD, HWND, HWND, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, void*)>
        (0x7B4E0E)(deviceIndex, flags, hwnd, hwndFocus, numColorBits, numAlphaBits, numDepthbits, numStencilBits, numBackBuffers, width, height, refreshRate, ppCtx);
    if(res != S_OK)
    {
        // Try hard reset
        reinterpret_cast<void(__cdecl*)(void)>(0x7B6D1A)();
        reinterpret_cast<void(__cdecl*)(void)>(0x7B6097)();
        res = reinterpret_cast<HRESULT(__stdcall*)(DWORD, DWORD, HWND, HWND, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, DWORD, void*)>
            (0x7B4E0E)(deviceIndex, flags, hwnd, hwndFocus, numColorBits, numAlphaBits, numDepthbits, numStencilBits, numBackBuffers, width, height, refreshRate, ppCtx);
    }
    return res;
}

void HandleLWindowFocus(HWND hwnd, bool inFocus)
{
    static int LastFocusState = -1;
    if(!g_d3d7FullscreenExclusive || (inFocus && LastFocusState == 1) || (!inFocus && LastFocusState == 0))
        return;

    if(!inFocus)
    {
        ChangeDisplaySettings(nullptr, 0);
        ShowWindow(hwnd, SW_MINIMIZE);
        LastFocusState = 0;
    }
    else
    {
        ShowWindow(hwnd, SW_RESTORE);
		{
			DEVMODE desiredMode = {};
			desiredMode.dmSize = sizeof(DEVMODE);
			desiredMode.dmPelsWidth = static_cast<DWORD>(g_d3d7BackBufferWidth);
			desiredMode.dmPelsHeight = static_cast<DWORD>(g_d3d7BackBufferHeight);
			desiredMode.dmFields = (DM_PELSHEIGHT|DM_PELSWIDTH);
			ChangeDisplaySettings(&desiredMode, CDS_FULLSCREEN);

			LONG lStyle = GetWindowLongA(hwnd, GWL_STYLE);
			LONG lExStyle = GetWindowLongA(hwnd, GWL_EXSTYLE);
			lStyle &= ~(WS_CAPTION|WS_THICKFRAME|WS_MINIMIZE|WS_MAXIMIZE|WS_SYSMENU);
			lExStyle &= ~(WS_EX_DLGMODALFRAME|WS_EX_CLIENTEDGE|WS_EX_STATICEDGE);
			SetWindowLongA(hwnd, GWL_STYLE, lStyle);
			SetWindowLongA(hwnd, GWL_EXSTYLE, (lExStyle|WS_EX_TOPMOST));
			SetWindowPos(hwnd, HWND_TOPMOST, 0, 0, g_d3d7BackBufferWidth, g_d3d7BackBufferHeight, (SWP_NOMOVE|SWP_SHOWWINDOW));
		}
        LastFocusState = 1;
    }
}

HWND __cdecl Hooked_GetForegroundWindow_G1()
{
    HWND window = *reinterpret_cast<HWND*>(0x86F4B8);
    if(GetForegroundWindow() != window)
        HandleLWindowFocus(window, false);
    else
        HandleLWindowFocus(window, true);
    return window;
}

HWND __cdecl Hooked_GetForegroundWindow_G2()
{
    HWND window = *reinterpret_cast<HWND*>(0x8D422C);
    if(GetForegroundWindow() != window)
        HandleLWindowFocus(window, false);
    else
        HandleLWindowFocus(window, true);
    return window;
}

int __fastcall Hooked_SetDevice_G1(DWORD zrenderer, DWORD _EDX, int num, int x, int y, int bpp, int mode)
{
    if(mode == 0)
    {
        g_d3d7FullscreenExclusive = true;
        mode = 1;
    }
    if(g_d3d7FullscreenExclusive)
    {
        DEVMODE desiredMode = {};
        desiredMode.dmSize = sizeof(DEVMODE);
        desiredMode.dmPelsWidth = static_cast<DWORD>(x);
        desiredMode.dmPelsHeight = static_cast<DWORD>(y);
        desiredMode.dmFields = (DM_PELSHEIGHT|DM_PELSWIDTH);
        ChangeDisplaySettings(&desiredMode, CDS_FULLSCREEN);
    }
    g_d3d7BackBufferWidth = x;
    g_d3d7BackBufferHeight = y;

    int res = reinterpret_cast<int(__thiscall*)(DWORD, int, int, int, int, int)>(0x6FD923)(zrenderer, num, x, y, bpp, mode);
    if(g_d3d7FullscreenExclusive)
    {
        HWND window = *reinterpret_cast<HWND*>(0x86F4B8);
		LONG lStyle = GetWindowLongA(window, GWL_STYLE);
		LONG lExStyle = GetWindowLongA(window, GWL_EXSTYLE);
		lStyle &= ~(WS_CAPTION|WS_THICKFRAME|WS_MINIMIZE|WS_MAXIMIZE|WS_SYSMENU);
		lExStyle &= ~(WS_EX_DLGMODALFRAME|WS_EX_CLIENTEDGE|WS_EX_STATICEDGE);
		SetWindowLongA(window, GWL_STYLE, lStyle);
		SetWindowLongA(window, GWL_EXSTYLE, (lExStyle|WS_EX_TOPMOST));
		SetWindowPos(window, HWND_TOPMOST, 0, 0, x, y, (SWP_NOMOVE|SWP_SHOWWINDOW));
    }
    return res;
}

int __fastcall Hooked_SetDevice_G2(DWORD zrenderer, DWORD _EDX, int num, int x, int y, int bpp, int mode)
{
    if(mode == 0)
    {
        g_d3d7FullscreenExclusive = true;
        mode = 1;
    }
    if(g_d3d7FullscreenExclusive)
    {
        DEVMODE desiredMode = {};
        desiredMode.dmSize = sizeof(DEVMODE);
        desiredMode.dmPelsWidth = static_cast<DWORD>(x);
        desiredMode.dmPelsHeight = static_cast<DWORD>(y);
        desiredMode.dmFields = (DM_PELSHEIGHT|DM_PELSWIDTH);
        ChangeDisplaySettings(&desiredMode, CDS_FULLSCREEN);
    }
    g_d3d7BackBufferWidth = x;
    g_d3d7BackBufferHeight = y;

    int res = reinterpret_cast<int(__thiscall*)(DWORD, int, int, int, int, int)>(0x628832)(zrenderer, num, x, y, bpp, mode);
    if(g_d3d7FullscreenExclusive)
    {
        HWND window = *reinterpret_cast<HWND*>(0x8D422C);
		LONG lStyle = GetWindowLongA(window, GWL_STYLE);
		LONG lExStyle = GetWindowLongA(window, GWL_EXSTYLE);
		lStyle &= ~(WS_CAPTION|WS_THICKFRAME|WS_MINIMIZE|WS_MAXIMIZE|WS_SYSMENU);
		lExStyle &= ~(WS_EX_DLGMODALFRAME|WS_EX_CLIENTEDGE|WS_EX_STATICEDGE);
		SetWindowLongA(window, GWL_STYLE, lStyle);
		SetWindowLongA(window, GWL_EXSTYLE, (lExStyle|WS_EX_TOPMOST));
		SetWindowPos(window, HWND_TOPMOST, 0, 0, x, y, (SWP_NOMOVE|SWP_SHOWWINDOW));
    }
    return res;
}

void InstallD3D7Fixes_G1()
{
    OverWriteWord(0x64384A, 0xC88B);
    OverWriteWord(0x7BBF98, 0xC88B);
    HookCall(0x64384C, reinterpret_cast<DWORD>(&Hooked_EnumDisplayModes));
    HookCall(0x7BBF9A, reinterpret_cast<DWORD>(&Hooked_EnumDisplayModes));

    HookCall(0x70EAB9, reinterpret_cast<DWORD>(&Hooked_CreateContext_G1));
    HookCallN(0x4F6BF6, reinterpret_cast<DWORD>(&Hooked_GetForegroundWindow_G1));
    HookJMP(0x4F54A8, 0x4F57F4);
    OverWriteByte(0x4F6C0E, 0xEB);

    // Fix window transitions
    WriteStack(0x711FBE, "\x8B\x8E\x94\x09\x00\x00\x85\xC9\x0F\x85\x85\x03\x00\x00\x8A\x54\x24\x50\x6A\x00\x8D\x4C\x24\x18\x89\xAE\x94\x09\x00\x00\x90\x90");
    OverWriteWord(0x711F7E, 0xFD3B);
    WriteStack(0x7120EF, "\x89\xAE\x94\x09\x00\x00\x89\xAE\x78\x09\x00\x00\xE8\xF0\x02\x00\x00\x8B\x54\x24\x54\x8B\x44\x24\x50\x90\x90\x90\x6A\x00");
    OverWriteWord(0x712055, 0x006A);
    WriteStack(0x6FD923, "\x64\xA1\x00\x00\x00\x00\xE9\xD8\x45\x01\x00");
    HookJMP(0x711F00, reinterpret_cast<DWORD>(&Hooked_SetDevice_G1));
}

void InstallD3D7Fixes_G2()
{
    OverWriteWord(0x64384A, 0xC88B);
    OverWriteWord(0x7BBF98, 0xC88B);
    HookCall(0x64384C, reinterpret_cast<DWORD>(&Hooked_EnumDisplayModes));
    HookCall(0x7BBF9A, reinterpret_cast<DWORD>(&Hooked_EnumDisplayModes));

    HookCall(0x64453C, reinterpret_cast<DWORD>(&Hooked_CreateContext_G2));
    HookCallN(0x505535, reinterpret_cast<DWORD>(&Hooked_GetForegroundWindow_G2));
    HookJMP(0x5039D4, 0x50476E);
    HookJMP(0x503BAA, 0x50476E);
    HookJMP(0x503D72, 0x50476E);
    HookJMP(0x505541, 0x505850);

    // Fix window transitions
    WriteStack(0x6483F0, "\x8B\x8E\x9C\x09\x00\x00\x85\xC9\x0F\x85\x85\x03\x00\x00\x8A\x54\x24\x50\x6A\x00\x8D\x4C\x24\x18\x89\xAE\x9C\x09\x00\x00\x90\x90");
    OverWriteWord(0x6483B0, 0xFD3B);
    WriteStack(0x648521, "\x89\xAE\x9C\x09\x00\x00\x89\xAE\x80\x09\x00\x00\xE8\xEE\x02\x00\x00\x8B\x54\x24\x54\x8B\x44\x24\x50\x90\x90\x90\x6A\x00");
    OverWriteWord(0x648487, 0x006A);
    WriteStack(0x628832, "\x64\xA1\x00\x00\x00\x00\xE9\xF9\xFA\x01\x00");
    HookJMP(0x648330, reinterpret_cast<DWORD>(&Hooked_SetDevice_G2));
}
