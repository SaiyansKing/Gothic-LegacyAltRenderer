#include "hook.h"

#include <atomic>
#include <thread>
#include <string>
#include <algorithm>
#include <intrin.h>
#include <d3d.h>

#include <shlwapi.h>
#pragma comment(lib, "shlwapi.lib")

struct ddraw_dll
{
	HMODULE dll;
	FARPROC	AcquireDDThreadLock;
	FARPROC	CheckFullscreen;
	FARPROC	CompleteCreateSysmemSurface;
	FARPROC	D3DParseUnknownCommand;
	FARPROC	DDGetAttachedSurfaceLcl;
	FARPROC	DDInternalLock;
	FARPROC	DDInternalUnlock;
	FARPROC	DSoundHelp;
	FARPROC	DirectDrawCreate;
	FARPROC	DirectDrawCreateClipper;
	FARPROC	DirectDrawCreateEx;
	FARPROC	DirectDrawEnumerateA;
	FARPROC	DirectDrawEnumerateExA;
	FARPROC	DirectDrawEnumerateExW;
	FARPROC	DirectDrawEnumerateW;
	FARPROC	DllCanUnloadNow;
	FARPROC	DllGetClassObject;
	FARPROC	GetDDSurfaceLocal;
	FARPROC	GetOLEThunkData;
	FARPROC	GetSurfaceFromDC;
	FARPROC	RegisterSpecialCase;
	FARPROC	ReleaseDDThreadLock;
} ddraw;

typedef void(*LPSmoothStretch)(int, int, int, int, int, int, int, void*, int, int, int, int, int, int, int, void*);
LPSmoothStretch SmoothStretch;

constexpr int option_lighthack = 1000;
constexpr int option_reversedepth = 1001;
constexpr int option_vsync = 1002;
constexpr int option_launch = 1003;
constexpr int option_skip = 1004;

std::atomic<bool> enable_radialFog_G1 = true;
std::atomic<bool> selected_lighthack = false;
std::atomic<bool> selected_rdepth = true;
std::atomic<bool> selected_vsync = false;
std::atomic<bool> selected_skip = false;
std::atomic<int> selected_option = 0;
std::atomic<int> selected_msaa = 0;

bool IsG1 = false;
bool IsG2 = false;

static void LoadCFG(HWND hWnd, HWND rendererBox, HWND msaaBox)
{
	char executablePath[MAX_PATH];
	GetModuleFileNameA(GetModuleHandleA(nullptr), executablePath, sizeof(executablePath));
	PathRemoveFileSpecA(executablePath);

	FILE* f;
	errno_t err = fopen_s(&f, (std::string(executablePath) + "\\legacyaltrenderer.ini").c_str(), "r");
	if(err == 0)
	{
		char readedLine[1024];
		while(fgets(readedLine, sizeof(readedLine), f) != nullptr)
		{
			size_t len = strlen(readedLine);
			if(len > 0)
			{
				if(readedLine[len - 1] == '\n' || readedLine[len - 1] == '\r')
					len -= 1;
				if(len > 0)
				{
					if(readedLine[len - 1] == '\n' || readedLine[len - 1] == '\r')
						len -= 1;
				}
			}
			if(len == 0)
				continue;

			std::size_t eqpos;
			std::string rLine = std::string(readedLine, len);
			std::transform(rLine.begin(), rLine.end(), rLine.begin(), toupper);
			if((eqpos = rLine.find("=")) != std::string::npos)
			{
				std::string lhLine = rLine.substr(0, eqpos);
				std::string rhLine = rLine.substr(eqpos + 1);
				lhLine.erase(lhLine.find_last_not_of(' ') + 1);
				lhLine.erase(0, lhLine.find_first_not_of(' '));
				rhLine.erase(rhLine.find_last_not_of(' ') + 1);
				rhLine.erase(0, rhLine.find_first_not_of(' '));
				if(lhLine == "LIGHTHACK")
					selected_lighthack.store(rhLine == "TRUE" || rhLine == "1");
				else if(lhLine == "REVERSEDDEPTHBUFFER")
					selected_rdepth.store(rhLine == "TRUE" || rhLine == "1");
				else if(lhLine == "VSYNC")
					selected_vsync.store(rhLine == "TRUE" || rhLine == "1");
				else if(lhLine == "MSAA")
				{
					try {selected_msaa.store(std::stoi(rhLine));}
					catch(const std::exception&) {selected_msaa.store(0);}
				}
				else if(lhLine == "RENDERER")
				{
					try {selected_option.store(std::stoi(rhLine));}
					catch(const std::exception&) {selected_option.store(0);}
				}
				else if(lhLine == "RADIALFOG_G1")
					enable_radialFog_G1.store(rhLine == "TRUE" || rhLine == "1");
				else if(lhLine == "SKIPLAUNCHER")
					selected_skip.store(rhLine == "TRUE" || rhLine == "1");
			}
		}
		fclose(f);
	}

	CheckDlgButton(hWnd, option_lighthack, (selected_lighthack.load() ? BST_CHECKED : BST_UNCHECKED));
	CheckDlgButton(hWnd, option_reversedepth, (selected_rdepth.load() ? BST_CHECKED : BST_UNCHECKED));
	CheckDlgButton(hWnd, option_vsync, (selected_vsync.load() ? BST_CHECKED : BST_UNCHECKED));
	CheckDlgButton(hWnd, option_skip, (selected_skip.load() ? BST_CHECKED : BST_UNCHECKED));

	int msaaOption = selected_msaa.load();
	if(msaaOption == 8) SendMessage(msaaBox, LB_SETCURSEL, 3, 0);
	else if(msaaOption == 4) SendMessage(msaaBox, LB_SETCURSEL, 2, 0);
	else if(msaaOption == 2) SendMessage(msaaBox, LB_SETCURSEL, 1, 0);
	else SendMessage(msaaBox, LB_SETCURSEL, 0, 0);

	int rendererOption = selected_option.load();
	if(rendererOption == 3) SendMessage(rendererBox, LB_SETCURSEL, 5, 0);
	else if(rendererOption == 6) SendMessage(rendererBox, LB_SETCURSEL, 4, 0);
	else if(rendererOption == 5) SendMessage(rendererBox, LB_SETCURSEL, 3, 0);
	else if(rendererOption == 12) SendMessage(rendererBox, LB_SETCURSEL, 2, 0);
	else if(rendererOption == 9) SendMessage(rendererBox, LB_SETCURSEL, 1, 0);
	else SendMessage(rendererBox, LB_SETCURSEL, 0, 0);
}

static void SaveCFG()
{
	char executablePath[MAX_PATH];
	GetModuleFileNameA(GetModuleHandleA(nullptr), executablePath, sizeof(executablePath));
	PathRemoveFileSpecA(executablePath);

	FILE* f;
	errno_t err = fopen_s(&f, (std::string(executablePath) + "\\legacyaltrenderer.ini").c_str(), "w");
	if(err == 0)
	{
		fputs((std::string("LightHack = ") + (selected_lighthack.load() ? "True\n" : "False\n")).c_str(), f);
		fputs((std::string("ReversedDepthBuffer = ") + (selected_rdepth.load() ? "True\n" : "False\n")).c_str(), f);
		fputs((std::string("VSync = ") + (selected_vsync.load() ? "True\n" : "False\n")).c_str(), f);
		fputs((std::string("MSAA = ") + std::to_string(selected_msaa.load()) + "\n").c_str(), f);
		fputs((std::string("Renderer = ") + std::to_string(selected_option.load()) + "\n").c_str(), f);
		fputs((std::string("RadialFog_G1 = ") + (enable_radialFog_G1.load() ? "True\n" : "False\n")).c_str(), f);
		fputs((std::string("SkipLauncher = ") + (selected_skip.load() ? "True\n" : "False\n")).c_str(), f);
		fclose(f);
	}
}

__forceinline __m128i _sym_mm_mullo_epu32(__m128i a, __m128i b)
{
	__m128i dest02 = _mm_mul_epu32(a, b);
	__m128i dest13 = _mm_mul_epu32(_mm_shuffle_epi32(a, 0xF5), _mm_shuffle_epi32(b, 0xF5));
	return _mm_unpacklo_epi32(_mm_shuffle_epi32(dest02, 0xD8), _mm_shuffle_epi32(dest13, 0xD8));
}

void SmoothStretch_SSE2(int srcX, int srcY, int srcXW, int srcYH, int srcW, int srcH, int srcP, void* srcPixels, int dstX, int dstY, int dstXW, int dstYH, int dstW, int dstH, int dstP, void* dstPixels)
{
	int salast;
	const __m128i s255 = _mm_set1_epi32(0xFF);

	int spixelw = (srcXW - 1);
	int spixelh = (srcYH - 1);
	int sx = static_cast<int>(65536.0f * spixelw / (dstXW - 1));
	int sy = static_cast<int>(65536.0f * spixelh / (dstYH - 1));

	int ssx = (srcXW << 16) - 1;
	int ssy = (srcYH << 16) - 1;

	unsigned int* sp = reinterpret_cast<unsigned int*>(reinterpret_cast<unsigned char*>(srcPixels) + (srcY * srcP + (srcX * 4)));
	signed int* dp = reinterpret_cast<signed int*>(reinterpret_cast<unsigned char*>(dstPixels) + (dstY * dstP + (dstX * 4)));

	int dgap = dstP / 4;
	int spixelgap = srcP / 4;

	int csay = 0;
	int xEnd = dstXW, yEnd = dstYH;
	for(int y = 0; y < yEnd; ++y)
	{
		signed int* cdp = dp;
		unsigned int* csp = sp;
		int csax = 0;
		for(int x = 0; x < xEnd; ++x)
		{
			int ex = (csax & 0xFFFF);
			int ey = (csay & 0xFFFF);
			int cx = (csax >> 16);
			int cy = (csay >> 16);
			bool sstepx = (cx < spixelw);
			unsigned int* c00 = sp;
			unsigned int* c01 = (sstepx ? c00 + 1 : c00);
			unsigned int* c10 = (cy < spixelh ? c00 + spixelgap : c00);
			unsigned int* c11 = (sstepx ? c10 + 1 : c10);

			const __m128i sex = _mm_set1_epi32(ex);
			const __m128i sc00 = _mm_unpacklo_epi16(_mm_unpacklo_epi8(_mm_cvtsi32_si128(*c00), _mm_setzero_si128()), _mm_setzero_si128());
			const __m128i sc01 = _mm_unpacklo_epi16(_mm_unpacklo_epi8(_mm_cvtsi32_si128(*c01), _mm_setzero_si128()), _mm_setzero_si128());
			const __m128i sc10 = _mm_unpacklo_epi16(_mm_unpacklo_epi8(_mm_cvtsi32_si128(*c10), _mm_setzero_si128()), _mm_setzero_si128());
			const __m128i sc11 = _mm_unpacklo_epi16(_mm_unpacklo_epi8(_mm_cvtsi32_si128(*c11), _mm_setzero_si128()), _mm_setzero_si128());
			__m128i r1 = _mm_and_si128(_mm_add_epi32(_mm_srli_epi32(_sym_mm_mullo_epu32(_mm_sub_epi32(sc01, sc00), sex), 16), sc00), s255);
			__m128i r2 = _mm_and_si128(_mm_add_epi32(_mm_srli_epi32(_sym_mm_mullo_epu32(_mm_sub_epi32(sc11, sc10), sex), 16), sc10), s255);
			__m128i r = _mm_and_si128(_mm_add_epi32(_mm_srli_epi32(_sym_mm_mullo_epu32(_mm_sub_epi32(r2, r1), _mm_set1_epi32(ey)), 16), r1), s255);
			r = _mm_packs_epi32(r, r);
			_mm_stream_si32(dp, _mm_cvtsi128_si32(_mm_packus_epi16(r, r)));

			salast = csax;
			csax += sx;
			csax = (csax > ssx ? ssx : csax);
			sp += ((csax >> 16) - (salast >> 16));
			++dp;
		}
		salast = csay;
		csay += sy;
		csay = (csay > ssy ? ssy : csay);
		sp = csp + (((csay >> 16) - (salast >> 16)) * spixelgap);
		dp = cdp + dgap;
	}
	_mm_sfence();
}

void SmoothStretch_SSSE3(int srcX, int srcY, int srcXW, int srcYH, int srcW, int srcH, int srcP, void* srcPixels, int dstX, int dstY, int dstXW, int dstYH, int dstW, int dstH, int dstP, void* dstPixels)
{
	int salast;
	const __m128i s255 = _mm_set1_epi32(0xFF);
	const __m128i loadMask = _mm_set_epi8(15, 14, 13, 3, 12, 11, 10, 2, 9, 8, 7, 1, 6, 5, 4, 0);
	const __m128i shuffleMask = _mm_set_epi8(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 12, 8, 4, 0);

	int spixelw = (srcXW - 1);
	int spixelh = (srcYH - 1);
	int sx = static_cast<int>(65536.0f * spixelw / (dstXW - 1));
	int sy = static_cast<int>(65536.0f * spixelh / (dstYH - 1));

	int ssx = (srcXW << 16) - 1;
	int ssy = (srcYH << 16) - 1;

	unsigned int* sp = reinterpret_cast<unsigned int*>(reinterpret_cast<unsigned char*>(srcPixels) + (srcY * srcP + (srcX * 4)));
	signed int* dp = reinterpret_cast<signed int*>(reinterpret_cast<unsigned char*>(dstPixels) + (dstY * dstP + (dstX * 4)));

	int dgap = dstP / 4;
	int spixelgap = srcP / 4;

	int csay = 0;
	int xEnd = dstXW, yEnd = dstYH;
	for(int y = 0; y < yEnd; ++y)
	{
		signed int* cdp = dp;
		unsigned int* csp = sp;
		int csax = 0;
		for(int x = 0; x < xEnd; ++x)
		{
			int ex = (csax & 0xFFFF);
			int ey = (csay & 0xFFFF);
			int cx = (csax >> 16);
			int cy = (csay >> 16);
			bool sstepx = (cx < spixelw);
			unsigned int* c00 = sp;
			unsigned int* c01 = (sstepx ? c00 + 1 : c00);
			unsigned int* c10 = (cy < spixelh ? c00 + spixelgap : c00);
			unsigned int* c11 = (sstepx ? c10 + 1 : c10);

			const __m128i sex = _mm_set1_epi32(ex);
			const __m128i sc00 = _mm_shuffle_epi8(_mm_cvtsi32_si128(*c00), loadMask);
			const __m128i sc01 = _mm_shuffle_epi8(_mm_cvtsi32_si128(*c01), loadMask);
			const __m128i sc10 = _mm_shuffle_epi8(_mm_cvtsi32_si128(*c10), loadMask);
			const __m128i sc11 = _mm_shuffle_epi8(_mm_cvtsi32_si128(*c11), loadMask);
			__m128i r1 = _mm_and_si128(_mm_add_epi32(_mm_srli_epi32(_sym_mm_mullo_epu32(_mm_sub_epi32(sc01, sc00), sex), 16), sc00), s255);
			__m128i r2 = _mm_and_si128(_mm_add_epi32(_mm_srli_epi32(_sym_mm_mullo_epu32(_mm_sub_epi32(sc11, sc10), sex), 16), sc10), s255);
			__m128i r = _mm_add_epi32(_mm_srli_epi32(_sym_mm_mullo_epu32(_mm_sub_epi32(r2, r1), _mm_set1_epi32(ey)), 16), r1);
			_mm_stream_si32(dp, _mm_cvtsi128_si32(_mm_shuffle_epi8(r, shuffleMask)));

			salast = csax;
			csax += sx;
			csax = (csax > ssx ? ssx : csax);
			sp += ((csax >> 16) - (salast >> 16));
			++dp;
		}
		salast = csay;
		csay += sy;
		csay = (csay > ssy ? ssy : csay);
		sp = csp + (((csay >> 16) - (salast >> 16)) * spixelgap);
		dp = cdp + dgap;
	}
	_mm_sfence();
}

void SmoothStretch_SSE41(int srcX, int srcY, int srcXW, int srcYH, int srcW, int srcH, int srcP, void* srcPixels, int dstX, int dstY, int dstXW, int dstYH, int dstW, int dstH, int dstP, void* dstPixels)
{
	int salast;
	const __m128i shuffleMask = _mm_set_epi8(0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 12, 8, 4, 0);
	const __m128i s255 = _mm_set1_epi32(0xFF);

	int spixelw = (srcXW - 1);
	int spixelh = (srcYH - 1);
	int sx = static_cast<int>(65536.0f * spixelw / (dstXW - 1));
	int sy = static_cast<int>(65536.0f * spixelh / (dstYH - 1));

	int ssx = (srcXW << 16) - 1;
	int ssy = (srcYH << 16) - 1;

	unsigned int* sp = reinterpret_cast<unsigned int*>(reinterpret_cast<unsigned char*>(srcPixels) + (srcY * srcP + (srcX * 4)));
	signed int* dp = reinterpret_cast<signed int*>(reinterpret_cast<unsigned char*>(dstPixels) + (dstY * dstP + (dstX * 4)));

	int dgap = dstP / 4;
	int spixelgap = srcP / 4;

	int csay = 0;
	int xEnd = dstXW, yEnd = dstYH;
	for(int y = 0; y < yEnd; ++y)
	{
		signed int* cdp = dp;
		unsigned int* csp = sp;
		int csax = 0;
		for(int x = 0; x < xEnd; ++x)
		{
			int ex = (csax & 0xFFFF);
			int ey = (csay & 0xFFFF);
			int cx = (csax >> 16);
			int cy = (csay >> 16);
			bool sstepx = (cx < spixelw);
			unsigned int* c00 = sp;
			unsigned int* c01 = (sstepx ? c00 + 1 : c00);
			unsigned int* c10 = (cy < spixelh ? c00 + spixelgap : c00);
			unsigned int* c11 = (sstepx ? c10 + 1 : c10);

			const __m128i sex = _mm_set1_epi32(ex);
			const __m128i sc00 = _mm_cvtepu8_epi32(_mm_cvtsi32_si128(*c00));
			const __m128i sc01 = _mm_cvtepu8_epi32(_mm_cvtsi32_si128(*c01));
			const __m128i sc10 = _mm_cvtepu8_epi32(_mm_cvtsi32_si128(*c10));
			const __m128i sc11 = _mm_cvtepu8_epi32(_mm_cvtsi32_si128(*c11));
			__m128i r1 = _mm_and_si128(_mm_add_epi32(_mm_srli_epi32(_mm_mullo_epi32(_mm_sub_epi32(sc01, sc00), sex), 16), sc00), s255);
			__m128i r2 = _mm_and_si128(_mm_add_epi32(_mm_srli_epi32(_mm_mullo_epi32(_mm_sub_epi32(sc11, sc10), sex), 16), sc10), s255);
			__m128i r = _mm_add_epi32(_mm_srli_epi32(_mm_mullo_epi32(_mm_sub_epi32(r2, r1), _mm_set1_epi32(ey)), 16), r1);
			_mm_stream_si32(dp, _mm_cvtsi128_si32(_mm_shuffle_epi8(r, shuffleMask)));

			salast = csax;
			csax += sx;
			csax = (csax > ssx ? ssx : csax);
			sp += ((csax >> 16) - (salast >> 16));
			++dp;
		}
		salast = csay;
		csay += sy;
		csay = (csay > ssy ? ssy : csay);
		sp = csp + (((csay >> 16) - (salast >> 16)) * spixelgap);
		dp = cdp + dgap;
	}
	_mm_sfence();
}

__forceinline __m128 __vectorcall VectorRound(__m128 V) noexcept
{
	__m128 sign = _mm_and_ps(V, _mm_castsi128_ps(_mm_setr_epi32(0x80000000, 0x80000000, 0x80000000, 0x80000000)));
	__m128 sMagic = _mm_or_ps(_mm_setr_ps(8388608.0f, 8388608.0f, 8388608.0f, 8388608.0f), sign);
	__m128 R1 = _mm_add_ps(V, sMagic);
	R1 = _mm_sub_ps(R1, sMagic);
	__m128 R2 = _mm_and_ps(V, _mm_castsi128_ps(_mm_setr_epi32(0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF, 0x7FFFFFFF)));
	__m128 mask = _mm_cmple_ps(R2, _mm_setr_ps(8388608.0f, 8388608.0f, 8388608.0f, 8388608.0f));
	R2 = _mm_andnot_ps(mask, V);
	R1 = _mm_and_ps(R1, mask);
	return _mm_xor_ps(R1, R2);
}

__forceinline __m128 __vectorcall VectorFNMADD(__m128 V1, __m128 V2, __m128 V3) noexcept
{
	return _mm_sub_ps(V3, _mm_mul_ps(V1, V2));
}

__forceinline __m128 __vectorcall VectorFMADD(__m128 V1, __m128 V2, __m128 V3) noexcept
{
	return _mm_add_ps(_mm_mul_ps(V1, V2), V3);
}

__forceinline __m128 __vectorcall VectorTan(__m128 V) noexcept
{
	__m128 TanCoefficients0 = _mm_setr_ps(1.0f, -4.667168334e-1f, 2.566383229e-2f, -3.118153191e-4f);
	__m128 TanCoefficients1 = _mm_setr_ps(4.981943399e-7f, -1.333835001e-1f, 3.424887824e-3f, -1.786170734e-5f);
	__m128 TanConstants = _mm_setr_ps(1.570796371f, 6.077100628e-11f, 0.000244140625f, 0.63661977228f);
	__m128 Mask = _mm_castsi128_ps(_mm_setr_epi32(0x00000001, 0x00000001, 0x00000001, 0x00000001));

	__m128 TwoDivPi = _mm_shuffle_ps(TanConstants, TanConstants, _MM_SHUFFLE(3, 3, 3, 3));
	__m128 Zero = _mm_setzero_ps();

	__m128 C0 = _mm_shuffle_ps(TanConstants, TanConstants, _MM_SHUFFLE(0, 0, 0, 0));
	__m128 C1 = _mm_shuffle_ps(TanConstants, TanConstants, _MM_SHUFFLE(1, 1, 1, 1));
	__m128 Epsilon = _mm_shuffle_ps(TanConstants, TanConstants, _MM_SHUFFLE(2, 2, 2, 2));

	__m128 VA = _mm_mul_ps(V, TwoDivPi);
	VA = VectorRound(VA);

	__m128 VC = VectorFNMADD(VA, C0, V);
	__m128 VB = _mm_max_ps(_mm_sub_ps(_mm_setzero_ps(), VA), VA);
	VC = VectorFNMADD(VA, C1, VC);
	VB = _mm_castsi128_ps(_mm_cvttps_epi32(VB));

	__m128 VC2 = _mm_mul_ps(VC, VC);
	__m128 T7 = _mm_shuffle_ps(TanCoefficients1, TanCoefficients1, _MM_SHUFFLE(3, 3, 3, 3));
	__m128 T6 = _mm_shuffle_ps(TanCoefficients1, TanCoefficients1, _MM_SHUFFLE(2, 2, 2, 2));
	__m128 T4 = _mm_shuffle_ps(TanCoefficients1, TanCoefficients1, _MM_SHUFFLE(0, 0, 0, 0));
	__m128 T3 = _mm_shuffle_ps(TanCoefficients0, TanCoefficients0, _MM_SHUFFLE(3, 3, 3, 3));
	__m128 T5 = _mm_shuffle_ps(TanCoefficients1, TanCoefficients1, _MM_SHUFFLE(1, 1, 1, 1));
	__m128 T2 = _mm_shuffle_ps(TanCoefficients0, TanCoefficients0, _MM_SHUFFLE(2, 2, 2, 2));
	__m128 T1 = _mm_shuffle_ps(TanCoefficients0, TanCoefficients0, _MM_SHUFFLE(1, 1, 1, 1));
	__m128 T0 = _mm_shuffle_ps(TanCoefficients0, TanCoefficients0, _MM_SHUFFLE(0, 0, 0, 0));

	__m128 VBIsEven = _mm_and_ps(VB, Mask);
	VBIsEven = _mm_castsi128_ps(_mm_cmpeq_epi32(_mm_castps_si128(VBIsEven), _mm_castps_si128(Zero)));

	__m128 N = VectorFMADD(VC2, T7, T6);
	__m128 D = VectorFMADD(VC2, T4, T3);
	N = VectorFMADD(VC2, N, T5);
	D = VectorFMADD(VC2, D, T2);
	N = _mm_mul_ps(VC2, N);
	D = VectorFMADD(VC2, D, T1);
	N = VectorFMADD(VC, N, VC);
	__m128 VCNearZero = _mm_and_ps(_mm_cmple_ps(VC, Epsilon), _mm_cmple_ps(_mm_mul_ps(Epsilon, _mm_set_ps1(-1.0f)), VC));
	D = VectorFMADD(VC2, D, T0);
	N = _mm_or_ps(_mm_andnot_ps(VCNearZero, N), _mm_and_ps(VC, VCNearZero));
	D = _mm_or_ps(_mm_andnot_ps(VCNearZero, D), _mm_and_ps(_mm_set_ps1(1.0f), VCNearZero));

	__m128 R0 = _mm_sub_ps(_mm_setzero_ps(), N);
	__m128 R1 = _mm_div_ps(N, D);
	R0 = _mm_div_ps(D, R0);

	__m128 VIsZero = _mm_cmpeq_ps(V, Zero);
	__m128 Result = _mm_or_ps(_mm_andnot_ps(VBIsEven, R0), _mm_and_ps(R1, VBIsEven));
	return _mm_or_ps(_mm_andnot_ps(VIsZero, Result), _mm_and_ps(Zero, VIsZero));
}

void __fastcall zCCamera_CreateProjectionMatrix(DWORD zCCamera, DWORD _EDX, float* matrix, float far_plane, float near_plane, float fov_horiz, float fov_vert)
{
	float zRange = far_plane / (far_plane - near_plane);
	float fov[2] = {fov_horiz, fov_vert};
	float fovMultiply[2] = {0.5f, 0.5f};
	float fRange[2] = {zRange, -zRange * near_plane};

	__m128 vFov = _mm_rcp_ps(VectorTan(_mm_mul_ps(_mm_castpd_ps(_mm_set_sd(*reinterpret_cast<double*>(fov))), _mm_castpd_ps(_mm_set_sd(*reinterpret_cast<double*>(fovMultiply))))));
	__m128 vRange = _mm_castpd_ps(_mm_set_sd(*reinterpret_cast<double*>(fRange)));

	__m128 vValues = _mm_movelh_ps(vFov, vRange);
	__m128 vTemp = _mm_move_ss(_mm_setzero_ps(), vValues);
	_mm_storeu_ps(matrix + 0x00, vTemp);
	vTemp = _mm_and_ps(vValues, _mm_castsi128_ps(_mm_setr_epi32(0x00000000, 0xFFFFFFFF, 0x00000000, 0x00000000)));
	_mm_storeu_ps(matrix + 0x04, vTemp);
	vValues = _mm_shuffle_ps(vValues, _mm_setr_ps(0.0f, 0.0f, 0.0f, 1.0f), _MM_SHUFFLE(3, 2, 3, 2));
	vTemp = _mm_shuffle_ps(_mm_setzero_ps(), vValues, _MM_SHUFFLE(3, 0, 0, 0));
	_mm_storeu_ps(matrix + 0x08, vTemp);
	vTemp = _mm_shuffle_ps(vTemp, vValues, _MM_SHUFFLE(2, 1, 0, 0));
	_mm_storeu_ps(matrix + 0x0C, vTemp);
}

void __cdecl HookSortMaterialPolys_G1(DWORD* polyListArray, DWORD polyListLen, DWORD stride, DWORD comparator)
{
	if(polyListLen > 0)
	{
		std::sort(polyListArray, polyListArray + polyListLen, [](DWORD a, DWORD b) -> bool
			{
				DWORD aLightMap = *reinterpret_cast<DWORD*>(a + 0x1C);
				DWORD bLightMap = *reinterpret_cast<DWORD*>(b + 0x1C);
				return (aLightMap ? (*reinterpret_cast<DWORD*>(aLightMap + 0x48)) : 0) < (bLightMap ? (*reinterpret_cast<DWORD*>(bLightMap + 0x48)) : 0);
			});
	}
}

void __cdecl HookSortMaterialPolys_G2(DWORD* polyListArray, DWORD polyListLen, DWORD stride, DWORD comparator, int falltoqs)
{
	if(polyListLen > 0)
	{
		std::sort(polyListArray, polyListArray + polyListLen, [](DWORD a, DWORD b) -> bool
			{
				DWORD aLightMap = *reinterpret_cast<DWORD*>(a + 0x1C);
				DWORD bLightMap = *reinterpret_cast<DWORD*>(b + 0x1C);
				return (aLightMap ? (*reinterpret_cast<DWORD*>(aLightMap + 0x48)) : 0) < (bLightMap ? (*reinterpret_cast<DWORD*>(bLightMap + 0x48)) : 0);
			});
	}
}

void __cdecl HookSortPortals_G2(int* portalListArray, DWORD portalListLen, DWORD stride, DWORD comparator)
{
	if(portalListLen > 0)
	{
		std::sort(portalListArray, portalListArray + portalListLen, [](int a, int b) -> bool
			{
				DWORD portals = *reinterpret_cast<DWORD*>(0x8D4960) + 4;
				int p1 = static_cast<int>(*reinterpret_cast<float*>(portals + (a * 16)));
				int p2 = static_cast<int>(*reinterpret_cast<float*>(portals + (b * 16)));
				return p1 < p2;
			});
	}
}

void __cdecl HookSortVobs_G2(DWORD* vobListArray, DWORD vobListLen, DWORD stride, DWORD comparator)
{
	if(vobListLen > 0)
	{
		std::sort(vobListArray, vobListArray + vobListLen, [](DWORD a, DWORD b) -> bool
			{
				DWORD aVisual = *reinterpret_cast<DWORD*>(a + 0xC8);
				DWORD bVisual = *reinterpret_cast<DWORD*>(b + 0xC8);
				return aVisual < bVisual;
			});
	}
}

int __fastcall HookReadVidDevice_G1(DWORD zCOptions, DWORD _EDX, DWORD sectorString, const char* keyName, int defaultValue)
{
	reinterpret_cast<int(__thiscall*)(DWORD, DWORD, const char*, int, int)>(0x45C950)(zCOptions, sectorString, keyName, 0, 0);
	return 0;
}

int __fastcall HookReadVidDevice_G2(DWORD zCOptions, DWORD _EDX, DWORD sectorString, const char* keyName, int defaultValue)
{
	reinterpret_cast<int(__thiscall*)(DWORD, DWORD, const char*, int, int)>(0x461E30)(zCOptions, sectorString, keyName, 0, 0);
	return 0;
}

void __fastcall zCSkyControler_Outdoor_Interpolate_G1(DWORD zCSkyControler_Outdoor)
{
	float lastMasterTime = *reinterpret_cast<float*>(zCSkyControler_Outdoor + 0x6C);
	*reinterpret_cast<float*>(zCSkyControler_Outdoor + 0x6C) = 1.0f;
	reinterpret_cast<void(__thiscall*)(DWORD)>(0x5BE4F0)(zCSkyControler_Outdoor);
	*reinterpret_cast<float*>(zCSkyControler_Outdoor + 0x6C) = lastMasterTime;
}

void __fastcall zCSkyControler_Outdoor_Interpolate_G2(DWORD zCSkyControler_Outdoor)
{
	float lastMasterTime = *reinterpret_cast<float*>(zCSkyControler_Outdoor + 0x80);
	*reinterpret_cast<float*>(zCSkyControler_Outdoor + 0x80) = 1.0f;
	reinterpret_cast<void(__thiscall*)(DWORD)>(0x5E8C20)(zCSkyControler_Outdoor);
	*reinterpret_cast<float*>(zCSkyControler_Outdoor + 0x80) = lastMasterTime;
}

void __fastcall oCItem_RotateInInventory(DWORD oCItem)
{
	reinterpret_cast<void(__thiscall*)(DWORD, int)>(0x672560)(oCItem, 1);

	float rotAxis[3] = {0.f, 0.f, 0.f};
	float* bbox3d = reinterpret_cast<float*>(oCItem + 0x7C);

	float val = -1.f;
	int index = 0;
	for(int i = 0; i < 3; ++i)
	{
		float v = (bbox3d[i + 3] - bbox3d[i]);
		if(v > val)
		{
			val = v;
			index = i;
		}
	}
	rotAxis[index] = 1.f;

	reinterpret_cast<void(__thiscall*)(DWORD, float*, float)>(0x5EE100)(oCItem, rotAxis, 20.f * (*reinterpret_cast<float*>(0x8CF1F0) / 1000.f));
}

struct zTRndSimpleVertex
{
	float pos[2];
	float z;
	float uv[2];
	DWORD color;
};

void __fastcall zCRenderer_DrawPolySimple_Fix_G1(DWORD zCRenderer, DWORD _EDX, DWORD texture, zTRndSimpleVertex* vertices, int numVertices)
{
	extern bool g_EmulateRadialFog;
	bool enabledRadialFog = g_EmulateRadialFog;
	if(enabledRadialFog)
	{
		void DisableRadialFog_G1();
		DisableRadialFog_G1();
	}

	int texWrap = reinterpret_cast<int(__thiscall*)(DWORD)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zCRenderer) + 0x4C))(zCRenderer);
	reinterpret_cast<void(__thiscall*)(DWORD, int)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zCRenderer) + 0x48))(zCRenderer, 0);
	for(int i = 0; i < numVertices; ++i)
	{
		zTRndSimpleVertex& vert = vertices[i];
		vert.pos[0] -= 0.5f;
		vert.pos[1] -= 0.5f;
	}
	reinterpret_cast<void(__thiscall*)(DWORD, DWORD, zTRndSimpleVertex*, int)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zCRenderer) + 0x24))(zCRenderer, texture, vertices, numVertices);
	reinterpret_cast<void(__thiscall*)(DWORD, int)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zCRenderer) + 0x48))(zCRenderer, texWrap);
	if(enabledRadialFog)
	{
		void EnableRadialFog_G1();
		EnableRadialFog_G1();
	}
}

void __fastcall zCRenderer_DrawPolySimple_Fix_G2(DWORD zCRenderer, DWORD _EDX, DWORD texture, zTRndSimpleVertex* vertices, int numVertices)
{
	int texWrap = reinterpret_cast<int(__thiscall*)(DWORD)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zCRenderer) + 0x64))(zCRenderer);
	reinterpret_cast<void(__thiscall*)(DWORD, int)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zCRenderer) + 0x60))(zCRenderer, 0);
	for(int i = 0; i < numVertices; ++i)
	{
		zTRndSimpleVertex& vert = vertices[i];
		vert.pos[0] -= 0.5f;
		vert.pos[1] -= 0.5f;
	}
	reinterpret_cast<void(__thiscall*)(DWORD, DWORD, zTRndSimpleVertex*, int)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zCRenderer) + 0x24))(zCRenderer, texture, vertices, numVertices);
	reinterpret_cast<void(__thiscall*)(DWORD, int)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zCRenderer) + 0x60))(zCRenderer, texWrap);
}

void __fastcall zCRenderer_DrawPolySimple_G1(DWORD zCRenderer, DWORD _EDX, DWORD texture, zTRndSimpleVertex* vertices, int numVertices)
{
	extern bool g_EmulateRadialFog;
	bool enabledRadialFog = g_EmulateRadialFog;
	if(enabledRadialFog)
	{
		void DisableRadialFog_G1();
		DisableRadialFog_G1();
	}

	for(int i = 0; i < numVertices; ++i)
	{
		zTRndSimpleVertex& vert = vertices[i];
		vert.pos[0] -= 0.5f;
		vert.pos[1] -= 0.5f;
	}
	reinterpret_cast<void(__thiscall*)(DWORD, DWORD, zTRndSimpleVertex*, int)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zCRenderer) + 0x24))(zCRenderer, texture, vertices, numVertices);
	if(enabledRadialFog)
	{
		void EnableRadialFog_G1();
		EnableRadialFog_G1();
	}
}

void __fastcall zCRenderer_DrawPolySimple_G2(DWORD zCRenderer, DWORD _EDX, DWORD texture, zTRndSimpleVertex* vertices, int numVertices)
{
	for(int i = 0; i < numVertices; ++i)
	{
		zTRndSimpleVertex& vert = vertices[i];
		vert.pos[0] -= 0.5f;
		vert.pos[1] -= 0.5f;
	}
	reinterpret_cast<void(__thiscall*)(DWORD, DWORD, zTRndSimpleVertex*, int)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zCRenderer) + 0x24))(zCRenderer, texture, vertices, numVertices);
}

__declspec(noinline) bool UTILS_IsWindowsVersionOrGreater(WORD wMajorVersion, WORD wMinorVersion, WORD wServicePackMajor)
{
	OSVERSIONINFOEXW VersionInfo = {0};
	VersionInfo.dwOSVersionInfoSize = sizeof(VersionInfo);
	VersionInfo.dwMajorVersion = wMajorVersion;
	VersionInfo.dwMinorVersion = wMinorVersion;
	VersionInfo.wServicePackMajor = wServicePackMajor;

	ULONGLONG ConditionMask = 0;
	VER_SET_CONDITION(ConditionMask, VER_MAJORVERSION, VER_GREATER_EQUAL);
	VER_SET_CONDITION(ConditionMask, VER_MINORVERSION, VER_GREATER_EQUAL);
	VER_SET_CONDITION(ConditionMask, VER_SERVICEPACKMAJOR, VER_GREATER_EQUAL);
	ULONG Condition = (VER_MAJORVERSION|VER_MINORVERSION|VER_SERVICEPACKMAJOR);

	if(HMODULE ntdllModule = GetModuleHandleA("ntdll.dll"))
	{
		DWORD VerifyVersion = reinterpret_cast<DWORD>(GetProcAddress(ntdllModule, "RtlVerifyVersionInfo"));
		if(VerifyVersion) return reinterpret_cast<int(WINAPI*)(POSVERSIONINFOEXW, ULONG, ULONGLONG)>(VerifyVersion)(&VersionInfo, Condition, ConditionMask) == 0;
	}
	if(HMODULE kernel32Module = GetModuleHandleA("kernel32.dll"))
	{
		DWORD VerifyVersion = reinterpret_cast<DWORD>(GetProcAddress(kernel32Module, "VerifyVersionInfoW"));
		if(VerifyVersion) return reinterpret_cast<BOOL(WINAPI*)(POSVERSIONINFOEXW, ULONG, ULONGLONG)>(VerifyVersion)(&VersionInfo, Condition, ConditionMask) != FALSE;
	}
	return false;
}
__forceinline bool UTILS_IsWindows10OrGreater() {return UTILS_IsWindowsVersionOrGreater(HIBYTE(_WIN32_WINNT_WIN10), LOBYTE(_WIN32_WINNT_WIN10), 0);}

LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam)
{
	static HWND lbox, abox;
	switch(message)
	{
		case WM_CREATE:
		{
			lbox = CreateWindow(L"LISTBOX", L"", (WS_CHILD|WS_VISIBLE|WS_BORDER), 5, 5, 185, 100, hWnd, 0, 0, 0);
			SendMessage(lbox, LB_SETITEMDATA, static_cast<int>(SendMessage(lbox, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"DirectX7"))), 7);
			SendMessage(lbox, LB_SETITEMDATA, static_cast<int>(SendMessage(lbox, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"DirectX9"))), 9);
			if(UTILS_IsWindows10OrGreater()) SendMessage(lbox, LB_SETITEMDATA, static_cast<int>(SendMessage(lbox, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"DirectX12"))), 12);
			SendMessage(lbox, LB_SETITEMDATA, static_cast<int>(SendMessage(lbox, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Vulkan (DXVK 1.10.3)"))), 5);
			SendMessage(lbox, LB_SETITEMDATA, static_cast<int>(SendMessage(lbox, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"Vulkan (DXVK 2.6)"))), 6);
			SendMessage(lbox, LB_SETITEMDATA, static_cast<int>(SendMessage(lbox, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"OpenGL (WineD3D 10.0)"))), 3);

			abox = CreateWindow(L"LISTBOX", L"", (WS_CHILD|WS_VISIBLE|WS_BORDER), 5, 110, 185, 70, hWnd, 0, 0, 0);
			SendMessage(abox, LB_SETITEMDATA, static_cast<int>(SendMessage(abox, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"No Anti-Aliasing"))), 0);
			SendMessage(abox, LB_SETITEMDATA, static_cast<int>(SendMessage(abox, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"2x MSAA"))), 2);
			SendMessage(abox, LB_SETITEMDATA, static_cast<int>(SendMessage(abox, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"4x MSAA"))), 4);
			SendMessage(abox, LB_SETITEMDATA, static_cast<int>(SendMessage(abox, LB_ADDSTRING, 0, reinterpret_cast<LPARAM>(L"8x MSAA"))), 8);

			CreateWindow(L"button", L"Light hack", (WS_CHILD|WS_VISIBLE|BS_CHECKBOX), 5, 180, 185, 18, hWnd, reinterpret_cast<HMENU>(option_lighthack), 0, 0);
			CreateWindow(L"button", L"Reverse depth buffer", (WS_CHILD|WS_VISIBLE|BS_CHECKBOX), 5, 200, 185, 18, hWnd, reinterpret_cast<HMENU>(option_reversedepth), 0, 0);
			CreateWindow(L"button", L"Vertical Synchronization", (WS_CHILD|WS_VISIBLE|BS_CHECKBOX), 5, 220, 185, 18, hWnd, reinterpret_cast<HMENU>(option_vsync), 0, 0);
			CreateWindow(L"button", L"Skip Launcher", (WS_CHILD|WS_VISIBLE|BS_CHECKBOX), 5, 240, 185, 18, hWnd, reinterpret_cast<HMENU>(option_skip), 0, 0);
			CreateWindow(L"button", L"Launch", (WS_CHILD|WS_VISIBLE|WS_BORDER), 5, 265, 185, 30, hWnd, reinterpret_cast<HMENU>(option_launch), 0, 0);

			LoadCFG(hWnd, lbox, abox);
			if(selected_skip.load())
				PostQuitMessage(0);
		}
		break;
		case WM_COMMAND:
		{
			int wmId = LOWORD(wParam);
			int wmEvent = HIWORD(wParam);
			if(wmId == option_launch)
			{
				int res = static_cast<int>(SendMessage(lbox, LB_GETITEMDATA, static_cast<int>(SendMessage(lbox, LB_GETCURSEL, 0, 0)), 0));
				selected_option.store(res);
				res = static_cast<int>(SendMessage(abox, LB_GETITEMDATA, static_cast<int>(SendMessage(abox, LB_GETCURSEL, 0, 0)), 0));
				selected_msaa.store(res);

				SaveCFG();
				PostQuitMessage(0);
			}
			else if(wmId == option_reversedepth)
			{
				CheckDlgButton(hWnd, option_reversedepth, (IsDlgButtonChecked(hWnd, option_reversedepth) ? BST_UNCHECKED : BST_CHECKED));
				if(IsDlgButtonChecked(hWnd, option_reversedepth)) selected_rdepth.store(true);
				else selected_rdepth.store(false);
			}
			else if(wmId == option_lighthack)
			{
				CheckDlgButton(hWnd, option_lighthack, (IsDlgButtonChecked(hWnd, option_lighthack) ? BST_UNCHECKED : BST_CHECKED));
				if(IsDlgButtonChecked(hWnd, option_lighthack)) selected_lighthack.store(true);
				else selected_lighthack.store(false);
			}
			else if(wmId == option_vsync)
			{
				CheckDlgButton(hWnd, option_vsync, (IsDlgButtonChecked(hWnd, option_vsync) ? BST_UNCHECKED : BST_CHECKED));
				if(IsDlgButtonChecked(hWnd, option_vsync)) selected_vsync.store(true);
				else selected_vsync.store(false);
			}
			else if(wmId == option_skip)
			{
				CheckDlgButton(hWnd, option_skip, (IsDlgButtonChecked(hWnd, option_skip) ? BST_UNCHECKED : BST_CHECKED));
				if(IsDlgButtonChecked(hWnd, option_skip)) selected_skip.store(true);
				else selected_skip.store(false);
			}
		}
		break;
		case WM_DISPLAYCHANGE: InvalidateRect(hWnd, nullptr, FALSE); break;
		case WM_DESTROY: PostQuitMessage(0); break;
		default: return DefWindowProc(hWnd, message, wParam, lParam);
	}
	return 0;
}

void InitRenderChoosing()
{
	int res = 7;
	selected_option.store(res);

	WNDCLASSEX wcex;
	wcex.cbSize = sizeof(WNDCLASSEX);
	wcex.style = CS_PARENTDC;
	wcex.lpfnWndProc = WndProc;
	wcex.cbClsExtra = 0;
	wcex.cbWndExtra = 0;
	wcex.hInstance = reinterpret_cast<HINSTANCE>(GetModuleHandleA(nullptr));
	wcex.hIcon = 0;
	wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
	wcex.hbrBackground = reinterpret_cast<HBRUSH>(16);
	wcex.lpszMenuName = 0;
	wcex.lpszClassName = L"Legacy Alternative Renderer";
	wcex.hIconSm = 0;
	RegisterClassEx(&wcex);

	int width = 200;
	int height = 330;
	int xPos = (GetSystemMetrics(SM_CXSCREEN) / 2) - (width / 2);
	int yPos = (GetSystemMetrics(SM_CYSCREEN) / 2) - (height / 2);

	HWND hWnd = CreateWindowEx(WS_EX_APPWINDOW, L"Legacy Alternative Renderer", L"Legacy Alternative Renderer",
		(WS_OVERLAPPED|WS_MINIMIZEBOX), xPos, yPos, width, height, NULL, NULL, wcex.hInstance, nullptr);
	ShowWindow(hWnd, SW_SHOW);
	UpdateWindow(hWnd);

	MSG msg;
	while(GetMessage(&msg, NULL, 0, 0))
	{
		TranslateMessage(&msg);
		DispatchMessage(&msg);
	}
	UnregisterClass(L"Legacy Alternative Renderer", wcex.hInstance);
}

DWORD __fastcall InitCommonControls_G1(DWORD zString, DWORD _EDX, DWORD argument)
{
	DWORD result = reinterpret_cast<DWORD(__thiscall*)(DWORD, DWORD)>(0x4013A0)(zString, argument);

	std::thread th(InitRenderChoosing);
	th.join();

	int option = selected_option.load();
	if(option != 7)
	{
		void InstallD3D9Renderer_G1(int, int, bool, bool);
		InstallD3D9Renderer_G1(option, selected_msaa.load(), selected_vsync.load(), enable_radialFog_G1.load());
	}
	else
	{
		void InstallD3D7Fixes_G1();
		InstallD3D7Fixes_G1();
	}
	if(selected_rdepth.load())
	{
		// Reverse depth buffer
		HookCall(0x53667C, reinterpret_cast<DWORD>(&zCCamera_CreateProjectionMatrix));
		HookCall(0x53682B, reinterpret_cast<DWORD>(&zCCamera_CreateProjectionMatrix));
		HookCall(0x536D09, reinterpret_cast<DWORD>(&zCCamera_CreateProjectionMatrix));
		HookCall(0x536E72, reinterpret_cast<DWORD>(&zCCamera_CreateProjectionMatrix));
		OverWrite(0x71F57F, 0x00000000);
		WriteStack(0x719E7F, "\x07");
		WriteStack(0x719E9A, "\x05");
		WriteStack(0x719A01, "\x07");
		WriteStack(0x70F9D4, "\x07");
		WriteStack(0x718829, "\x05");
		WriteStack(0x718838, "\x07");
		WriteStack(0x714E1C, "\x07");
		WriteStack(0x714E28, "\x05");
		WriteStack(0x714EEA, "\x07");
		WriteStack(0x715540, "\x07");
		WriteStack(0x71554C, "\x05");
		WriteStack(0x71560E, "\x07");
		WriteStack(0x715C0F, "\x07");
		WriteStack(0x715C1B, "\x05");
		WriteStack(0x715CCF, "\x07");
		WriteStack(0x7132F4, "\x07");
		WriteStack(0x718C09, "\x05");
		WriteStack(0x718C1D, "\x05");
		WriteStack(0x718C3B, "\x07");
		DWORD ChangeEsiToFour = reinterpret_cast<DWORD>(VirtualAlloc(nullptr, 32, (MEM_RESERVE | MEM_COMMIT), PAGE_EXECUTE_READWRITE));
		if(ChangeEsiToFour)
		{
			WriteStack(ChangeEsiToFour, "\xBE\x04\x00\x00\x00\x39\xB5\x90\x00\x00\x00");
			HookJMP(ChangeEsiToFour + 11, 0x718C4A);
			HookJMPN(0x718C44, ChangeEsiToFour);
		}
		WriteStack(0x535E97, "\xC7\x86\xE4\x08\x00\x00\x00\x00\x80\x3F");
		WriteStack(0x535EA3, "\xC7\x86\xE4\x08\x00\x00\x00\x00\x80\x3F");
		WriteStack(0x535EAF, "\xC7\x86\xE4\x08\x00\x00\x00\x00\x80\x3F");
	}
	if(selected_lighthack.load())
		HookCall(0x5C0B93, reinterpret_cast<DWORD>(&zCSkyControler_Outdoor_Interpolate_G1));
	
	if(*reinterpret_cast<BYTE*>(0x67303D) != 0xD8)
	{
		WriteStack(0x67303D, "\xD8\x1D\xA4\x08\x7D\x00");
		DWORD FixAnimation = reinterpret_cast<DWORD>(VirtualAlloc(nullptr, 32, (MEM_RESERVE|MEM_COMMIT), PAGE_EXECUTE_READWRITE));
		if(FixAnimation)
		{
			WriteStack(FixAnimation, "\x8B\xCF\xE8\x00\x00\x00\x00\xE9\x00\x00\x00\x00\x6A\x01\x8B\xCF\xE8\x00\x00\x00\x00\xE9\x00\x00\x00\x00");
			HookCall(FixAnimation + 2, reinterpret_cast<DWORD>(&oCItem_RotateInInventory));
			HookCall(FixAnimation + 16, 0x672560);
			HookJMP(FixAnimation + 7, 0x673053);
			HookJMP(FixAnimation + 21, 0x673053);
			HookJMP(0x673049, FixAnimation);
			HookJMP(0x67304E, FixAnimation + 12);
			WriteStack(0x673048, "\x0F\x8A");
		}
	}
	return result;
}

void WINAPI InitCommonControls_G2(void)
{
	reinterpret_cast<void(WINAPI*)(void)>(*reinterpret_cast<DWORD*>(0x82E02C))();

	std::thread th(InitRenderChoosing);
	th.join();

	int option = selected_option.load();
	if(option != 7)
	{
		void InstallD3D9Renderer_G2(int, int, bool);
		InstallD3D9Renderer_G2(option, selected_msaa.load(), selected_vsync.load());
	}
	else
	{
		void InstallD3D7Fixes_G2();
		InstallD3D7Fixes_G2();
	}
	if(selected_rdepth.load())
	{
		// Reverse depth buffer
		HookCall(0x54A8BC, reinterpret_cast<DWORD>(&zCCamera_CreateProjectionMatrix));
		HookCall(0x54AA6B, reinterpret_cast<DWORD>(&zCCamera_CreateProjectionMatrix));
		HookCall(0x54B38E, reinterpret_cast<DWORD>(&zCCamera_CreateProjectionMatrix));
		WriteStack(0x5DFB65, "\xD9\x82\x00\x09\x00\x00");
		OverWrite(0x657E4F, 0x00000000);
		WriteStack(0x652553, "\x07");
		WriteStack(0x65255F, "\x05");
		WriteStack(0x651CE1, "\x07");
		WriteStack(0x645B88, "\x07");
		WriteStack(0x650B19, "\x05");
		WriteStack(0x650B28, "\x07");
		WriteStack(0x64BAA0, "\x07");
		WriteStack(0x64BAAC, "\x05");
		WriteStack(0x64BB6E, "\x07");
		WriteStack(0x64C15F, "\x07");
		WriteStack(0x64C16B, "\x05");
		WriteStack(0x64C21F, "\x07");
		WriteStack(0x64B4BF, "\x07");
		WriteStack(0x64B4CB, "\x05");
		WriteStack(0x64B57F, "\x07");
		WriteStack(0x64CDAE, "\x07");
		WriteStack(0x64CDBA, "\x05");
		WriteStack(0x64CE76, "\x07");
		WriteStack(0x649894, "\x07");
		WriteStack(0x650EEE, "\x05");
		WriteStack(0x650F02, "\x05");
		WriteStack(0x650F20, "\x07");
		DWORD ChangeEdiToFour = reinterpret_cast<DWORD>(VirtualAlloc(nullptr, 32, (MEM_RESERVE | MEM_COMMIT), PAGE_EXECUTE_READWRITE));
		if(ChangeEdiToFour)
		{
			WriteStack(ChangeEdiToFour, "\xBF\x04\x00\x00\x00\x39\xBD\x94\x00\x00\x00");
			HookJMP(ChangeEdiToFour + 11, 0x718C4A);
			HookJMPN(0x650F29, ChangeEdiToFour);
		}
		WriteStack(0x549FB4, "\xC7\x86\x00\x09\x00\x00\x00\x00\x80\x3F");
		WriteStack(0x549FC0, "\xC7\x86\x00\x09\x00\x00\x00\x00\x80\x3F");
		WriteStack(0x549FCC, "\xC7\x86\x00\x09\x00\x00\x00\x00\x80\x3F");
	}
	if(selected_lighthack.load())
		HookCall(0x5EAC28, reinterpret_cast<DWORD>(&zCSkyControler_Outdoor_Interpolate_G2));
}

__declspec(naked) void FakeAcquireDDThreadLock() { _asm { jmp[ddraw.AcquireDDThreadLock] } }
__declspec(naked) void FakeCheckFullscreen() { _asm { jmp[ddraw.CheckFullscreen] } }
__declspec(naked) void FakeCompleteCreateSysmemSurface() { _asm { jmp[ddraw.CompleteCreateSysmemSurface] } }
__declspec(naked) void FakeD3DParseUnknownCommand() { _asm { jmp[ddraw.D3DParseUnknownCommand] } }
__declspec(naked) void FakeDDGetAttachedSurfaceLcl() { _asm { jmp[ddraw.DDGetAttachedSurfaceLcl] } }
__declspec(naked) void FakeDDInternalLock() { _asm { jmp[ddraw.DDInternalLock] } }
__declspec(naked) void FakeDDInternalUnlock() { _asm { jmp[ddraw.DDInternalUnlock] } }
__declspec(naked) void FakeDSoundHelp() { _asm { jmp[ddraw.DSoundHelp] } }
__declspec(naked) void FakeDirectDrawCreate() { _asm { jmp[ddraw.DirectDrawCreate] } }
__declspec(naked) void FakeDirectDrawCreateClipper() { _asm { jmp[ddraw.DirectDrawCreateClipper] } }
__declspec(naked) void FakeDirectDrawCreateEx() { _asm { jmp[ddraw.DirectDrawCreateEx] } }
__declspec(naked) void FakeDirectDrawEnumerateA() { _asm { jmp[ddraw.DirectDrawEnumerateA] } }
__declspec(naked) void FakeDirectDrawEnumerateExA() { _asm { jmp[ddraw.DirectDrawEnumerateExA] } }
__declspec(naked) void FakeDirectDrawEnumerateExW() { _asm { jmp[ddraw.DirectDrawEnumerateExW] } }
__declspec(naked) void FakeDirectDrawEnumerateW() { _asm { jmp[ddraw.DirectDrawEnumerateW] } }
__declspec(naked) void FakeDllCanUnloadNow() { _asm { jmp[ddraw.DllCanUnloadNow] } }
__declspec(naked) void FakeDllGetClassObject() { _asm { jmp[ddraw.DllGetClassObject] } }
__declspec(naked) void FakeGetDDSurfaceLocal() { _asm { jmp[ddraw.GetDDSurfaceLocal] } }
__declspec(naked) void FakeGetOLEThunkData() { _asm { jmp[ddraw.GetOLEThunkData] } }
__declspec(naked) void FakeGetSurfaceFromDC() { _asm { jmp[ddraw.GetSurfaceFromDC] } }
__declspec(naked) void FakeRegisterSpecialCase() { _asm { jmp[ddraw.RegisterSpecialCase] } }
__declspec(naked) void FakeReleaseDDThreadLock() { _asm { jmp[ddraw.ReleaseDDThreadLock] } }

BOOL WINAPI DllMain(HINSTANCE hInst, DWORD reason, LPVOID)
{
    if(reason == DLL_PROCESS_ATTACH)
    {
		char infoBuf[MAX_PATH];
		GetSystemDirectoryA(infoBuf, MAX_PATH);
		// We then append \ddraw.dll, which makes the string:
		// C:\windows\system32\ddraw.dll
		strcat_s(infoBuf, MAX_PATH, "\\ddraw.dll");

		ddraw.dll = LoadLibraryA(infoBuf);
		if(!ddraw.dll) return FALSE;

		ddraw.AcquireDDThreadLock = GetProcAddress(ddraw.dll, "AcquireDDThreadLock");
		ddraw.CheckFullscreen = GetProcAddress(ddraw.dll, "CheckFullscreen");
		ddraw.CompleteCreateSysmemSurface = GetProcAddress(ddraw.dll, "CompleteCreateSysmemSurface");
		ddraw.D3DParseUnknownCommand = GetProcAddress(ddraw.dll, "D3DParseUnknownCommand");
		ddraw.DDGetAttachedSurfaceLcl = GetProcAddress(ddraw.dll, "DDGetAttachedSurfaceLcl");
		ddraw.DDInternalLock = GetProcAddress(ddraw.dll, "DDInternalLock");
		ddraw.DDInternalUnlock = GetProcAddress(ddraw.dll, "DDInternalUnlock");
		ddraw.DSoundHelp = GetProcAddress(ddraw.dll, "DSoundHelp");
		ddraw.DirectDrawCreate = GetProcAddress(ddraw.dll, "DirectDrawCreate");
		ddraw.DirectDrawCreateClipper = GetProcAddress(ddraw.dll, "DirectDrawCreateClipper");
		ddraw.DirectDrawCreateEx = GetProcAddress(ddraw.dll, "DirectDrawCreateEx");
		ddraw.DirectDrawEnumerateA = GetProcAddress(ddraw.dll, "DirectDrawEnumerateA");
		ddraw.DirectDrawEnumerateExA = GetProcAddress(ddraw.dll, "DirectDrawEnumerateExA");
		ddraw.DirectDrawEnumerateExW = GetProcAddress(ddraw.dll, "DirectDrawEnumerateExW");
		ddraw.DirectDrawEnumerateW = GetProcAddress(ddraw.dll, "DirectDrawEnumerateW");
		ddraw.DllCanUnloadNow = GetProcAddress(ddraw.dll, "DllCanUnloadNow");
		ddraw.DllGetClassObject = GetProcAddress(ddraw.dll, "DllGetClassObject");
		ddraw.GetDDSurfaceLocal = GetProcAddress(ddraw.dll, "GetDDSurfaceLocal");
		ddraw.GetOLEThunkData = GetProcAddress(ddraw.dll, "GetOLEThunkData");
		ddraw.GetSurfaceFromDC = GetProcAddress(ddraw.dll, "GetSurfaceFromDC");
		ddraw.RegisterSpecialCase = GetProcAddress(ddraw.dll, "RegisterSpecialCase");
		ddraw.ReleaseDDThreadLock = GetProcAddress(ddraw.dll, "ReleaseDDThreadLock");

		bool HaveSSSE3 = false;
		bool HaveSSE41 = false;

		int cpuinfo[4];
		__cpuid(cpuinfo, 0);

		int nIds = cpuinfo[0];
		if(nIds >= 1)
		{
			__cpuid(cpuinfo, 1);
			if(cpuinfo[2] & 0x00000200) HaveSSSE3 = true;
			if(cpuinfo[2] & 0x00080000) HaveSSE41 = true;
		}

		if(HaveSSE41) SmoothStretch = reinterpret_cast<LPSmoothStretch>(SmoothStretch_SSE41);
		else if(HaveSSSE3) SmoothStretch = reinterpret_cast<LPSmoothStretch>(SmoothStretch_SSSE3);
		else SmoothStretch = reinterpret_cast<LPSmoothStretch>(SmoothStretch_SSE2);

        DWORD baseAddr = reinterpret_cast<DWORD>(GetModuleHandleA(nullptr));
        if(*reinterpret_cast<DWORD*>(baseAddr + 0x160) == 0x37A8D8 && *reinterpret_cast<DWORD*>(baseAddr + 0x37A960) == 0x7D01E4 && *reinterpret_cast<DWORD*>(baseAddr + 0x37A98B) == 0x7D01E8)
        {
            IsG1 = true;

			// Fix rendering ui half-pixel offset
			OverWriteByte(0x5AF31A, 0x56);
			OverWriteWord(0x5AF0DC, 0xCB8B);
			HookCall(0x5AF31B, reinterpret_cast<DWORD>(&zCRenderer_DrawPolySimple_Fix_G1));
			HookCall(0x5AF0E6, reinterpret_cast<DWORD>(&zCRenderer_DrawPolySimple_G1));

			// Optimize qsort's
			HookCall(0x5B1DA0, reinterpret_cast<DWORD>(&HookSortMaterialPolys_G1));

			// Disable ccDrawCaption
			OverWriteByte(0x4F2450, 0xC3);

			// Disable ccRenderCaption
			OverWriteByte(0x4F25D0, 0xC3);

			// Disable WM_NCPAINT
			HookJMP(0x4F5202, 0x4F57F4);

			// Disable WM_PAINT
			HookJMP(0x4F545D, 0x4F57F4);

			// Initialize renderer
			HookCall(0x4F3E22, reinterpret_cast<DWORD>(&InitCommonControls_G1));

			// Show correct savegame thumbnail
			WriteStack(0x434167, "\x8B\xEE\xEB\x21");
			WriteStack(0x4342AA, "\xEB\x07");
			WriteStack(0x4342B5, "\x55");
			WriteStack(0x4342E0, "\xEB\x15");

			// Show correctly used graphic device
			HookCall(0x601703, reinterpret_cast<DWORD>(&HookReadVidDevice_G1));

			WriteStack(0x71F8DF, "\x55\x56\xBE\x00\x00\x00\x00\x90\x90\x90\x90");
			WriteStack(0x71F8EC, "\x83\xFE\x01");
			WriteStack(0x71F9EC, "\x81\xC6\x18\xE7\x8D\x00");
			WriteStack(0x71FA01, "\x90\x90");

			WriteStack(0x71F5D9, "\xB8\x01\x00\x00\x00\xC3\x90");
			WriteStack(0x71F5E9, "\xB8\x01\x00\x00\x00\xC3\x90");

			WriteStack(0x42BB0D, "\xE8\xC7\x3A\x2F\x00\x90");
			WriteStack(0x42BBE1, "\xE8\x03\x3A\x2F\x00\x90");
        }
        else if(*reinterpret_cast<DWORD*>(baseAddr + 0x168) == 0x3D4318 && *reinterpret_cast<DWORD*>(baseAddr + 0x3D43A0) == 0x82E108 && *reinterpret_cast<DWORD*>(baseAddr + 0x3D43CB) == 0x82E10C)
        {
            IsG2 = true;

			// Fix rendering ui half-pixel offset
			OverWriteByte(0x5D45CA, 0x56);
			OverWriteWord(0x5D438C, 0xCB8B);
			HookCall(0x5D45CB, reinterpret_cast<DWORD>(&zCRenderer_DrawPolySimple_Fix_G2));
			HookCall(0x5D4396, reinterpret_cast<DWORD>(&zCRenderer_DrawPolySimple_G2));

			// Optimize qsort's
			HookCall(0x5D7BE3, reinterpret_cast<DWORD>(&HookSortMaterialPolys_G2));
			HookCall(0x52915F, reinterpret_cast<DWORD>(&HookSortPortals_G2));
			HookCall(0x52D927, reinterpret_cast<DWORD>(&HookSortVobs_G2));

			// Initialize renderer
			HookCallN(0x502D75, reinterpret_cast<DWORD>(&InitCommonControls_G2));

			// Disable ccDrawCaption
			OverWriteByte(0x500990, 0xC3);

			// Disable ccRenderCaption
			OverWriteByte(0x500B10, 0xC3);

			// Disable WM_NCPAINT
			OverWriteByte(0x503FCA, 0xEB);

			// Disable WM_PAINT
			OverWriteByte(0x50394D, 0xEB);

			// Show correct savegame thumbnail
			WriteStack(0x437157, "\x8B\xEE\xEB\x21");
			WriteStack(0x437283, "\xEB\x07");
			WriteStack(0x43728E, "\x55");
			WriteStack(0x4372B9, "\xEB\x15");

			// Show correctly used graphic device
			HookCall(0x63089E, reinterpret_cast<DWORD>(&HookReadVidDevice_G2));

			WriteStack(0x6581AD, "\x57\xBD\x00\x00\x00\x00\x90");
			WriteStack(0x6581B8, "\x83\xFD\x01\x90\x90\x90\x90");
			WriteStack(0x658302, "\x81\xC5\x30\x4C\x9A\x00");
			WriteStack(0x658321, "\x8B\xFD");
			WriteStack(0x658329, "\x55");

			WriteStack(0x657EA9, "\xB8\x01\x00\x00\x00\xC3\x90");
			WriteStack(0x657EB9, "\xB8\x01\x00\x00\x00\xC3\x90");

			WriteStack(0x42DF1F, "\xE8\x85\x9F\x22\x00\x90");
			WriteStack(0x42E000, "\xE8\xB4\x9E\x22\x00\x90");
        }

		void RegisterBinkPlayerHooks(); RegisterBinkPlayerHooks();
		if(IsG1 || IsG2)
		{
			char systempackPath[MAX_PATH];
			GetModuleFileNameA(GetModuleHandleA(nullptr), systempackPath, sizeof(systempackPath));
			PathRemoveFileSpecA(systempackPath);
			strcat_s(systempackPath, MAX_PATH, "\\SystemPack.ini");

			WritePrivateProfileStringA("DEBUG", "FixBinkNew", "False", systempackPath);
			WritePrivateProfileStringA("DEBUG", "FixBink", "False", systempackPath);
		}
    }
    return TRUE;
}
