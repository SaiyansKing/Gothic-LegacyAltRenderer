#include "D3D9Renderer.h"
#include "Conversions.h"
#include "hook.h"

#include <intrin.h>
#include <algorithm>
#include <vector>
#include <array>

extern bool IsG1;
extern bool IsG2;

static char* GPUDeviceName = const_cast<char*>("DirectX9");
static D3DPRESENT_PARAMETERS g_Direct3D9_PParams;

DWORD Direct3DCurrentFVF = static_cast<DWORD>(-1);
int Direct3DCurrentStride = 0;
int fullscreenExclusivent = 0;
int WindowPositionX = 0;
int WindowPositionY = 0;
int Direct3DDeviceCreated = 0;

DWORD Direct3DCreate9_12_Ptr = 0;
DWORD Direct3DCreate9Ex_Ptr = 0;
DWORD Direct3DCreate9_Ptr = 0;

IDirect3D9Ex* g_Direct3D9Ex = nullptr;
IDirect3D9* g_Direct3D9 = nullptr;
IDirect3DDevice9Ex* g_Direct3D9Device9Ex = nullptr;
IDirect3DDevice9* g_Direct3D9Device9 = nullptr;

IDirect3DPixelShader9* g_GammaCorrectionPS = nullptr;
IDirect3DSurface9* g_DefaultRenderTarget = nullptr;
IDirect3DSurface9* g_ManagedBoundTarget = nullptr;
IDirect3DTexture9* g_ManagedBackBuffer = nullptr;
IDirect3DTexture9* g_ManagedVideoBuffer = nullptr;
IDirect3DIndexBuffer9* g_ManagedIndexBuffer = nullptr;
UINT g_ManagedIndexBufferSize = 0;
UINT g_ManagedIndexBufferPos = 0;
bool g_CreateStaticVertexBuffer = false;
bool g_ReadOnlyLightmapData = false;
bool g_HaveGammeCorrection = true;

D3DMULTISAMPLE_TYPE g_MultiSampleAntiAliasing = D3DMULTISAMPLE_NONE;
bool g_UseVsync = false;

float g_DeviceGamma = 0.5f;
float g_DeviceContrast = 0.5f;
float g_DeviceBrightness = 0.5f;

class MyDirectDrawSurface7;
class MyDirect3DVertexBuffer7;
std::vector<MyDirectDrawSurface7*> g_Direct3D9Textures;
std::vector<MyDirect3DVertexBuffer7*> g_Direct3D9VertexBuffers;
std::vector<DDSURFACEDESC2> g_Direct3D9VideoModes;

static const GUID D3D9_IID_IDirect3D9 = {0x81BDCBCA, 0x64D4, 0x426D, {0xAE, 0x8D, 0xAD, 0x01, 0x47, 0xF4, 0x27, 0x5C}};
static const GUID D3D9_IID_IDirect3DDevice9Ex = {0xB18B10CE, 0x2649, 0x405A, {0x87, 0x0F, 0x95, 0xF7, 0x77, 0xD4, 0x31, 0x3A}};

static const GUID D3D7_IID_IDirect3D7 = {0xF5049E77, 0x4861, 0x11D2, {0xA4, 0x07, 0x00, 0xA0, 0xC9, 0x06, 0x29, 0xA8}};

UINT CalculatePrimitiveCount(D3DPRIMITIVETYPE primitive, UINT vertices)
{
	switch(primitive)
	{
		case D3DPT_LINELIST: return vertices / 2;
		case D3DPT_LINESTRIP: return vertices - 1;
		case D3DPT_TRIANGLELIST: return vertices / 3;
		case D3DPT_TRIANGLESTRIP: return vertices - 2;
		case D3DPT_TRIANGLEFAN: return vertices - 2;
		default: return vertices;
	}
}

UINT GetDDSRowPitchSize(UINT width, bool dxt1)
{
	if(dxt1) return std::max<UINT>(1, ((width + 3) / 4)) * 8;
	else return std::max<UINT>(1, ((width + 3) / 4)) * 16;
}

WORD GetNumberOfBits(DWORD dwMask)
{
	WORD wBits = 0;
	while(dwMask)
	{
		dwMask = dwMask & (dwMask - 1);
		++wBits;
	}
	return wBits;
};

int ComputeFVFSize(DWORD fvf)
{
	int size = 0;
	if((fvf & D3DFVF_XYZ) == D3DFVF_XYZ) size += 3 * sizeof(float);
	else if((fvf & D3DFVF_XYZRHW) == D3DFVF_XYZRHW) size += 4 * sizeof(float);
	if((fvf & D3DFVF_NORMAL) == D3DFVF_NORMAL) size += 3 * sizeof(float);
	if((fvf & D3DFVF_DIFFUSE) == D3DFVF_DIFFUSE) size += sizeof(D3DCOLOR);
	if((fvf & D3DFVF_SPECULAR) == D3DFVF_SPECULAR) size += sizeof(D3DCOLOR);
	if((fvf & D3DFVF_TEX8) == D3DFVF_TEX8) size += 2 * sizeof(float) * 8;
	else if((fvf & D3DFVF_TEX7) == D3DFVF_TEX7) size += 2 * sizeof(float) * 7;
	else if((fvf & D3DFVF_TEX6) == D3DFVF_TEX6) size += 2 * sizeof(float) * 6;
	else if((fvf & D3DFVF_TEX5) == D3DFVF_TEX5) size += 2 * sizeof(float) * 5;
	else if((fvf & D3DFVF_TEX4) == D3DFVF_TEX4) size += 2 * sizeof(float) * 4;
	else if((fvf & D3DFVF_TEX3) == D3DFVF_TEX3) size += 2 * sizeof(float) * 3;
	else if((fvf & D3DFVF_TEX2) == D3DFVF_TEX2) size += 2 * sizeof(float) * 2;
	else if((fvf & D3DFVF_TEX1) == D3DFVF_TEX1) size += 2 * sizeof(float);
	return size;
}

void SetCurrentFVF(DWORD fvf)
{
	if(Direct3DCurrentFVF != fvf)
	{
		Direct3DCurrentFVF = fvf;
		IDirect3DDevice9_SetFVF(g_Direct3D9Device9, fvf);
		Direct3DCurrentStride = ComputeFVFSize(fvf);
	}
}

void IssueTextureUpdate(IDirect3DTexture9* texture, unsigned char* data, UINT width, UINT height, UINT miplevel, D3DFORMAT format)
{
	if(!texture)
		return;

	IDirect3DTexture9* stagingTexture;
	HRESULT result = IDirect3DDevice9_CreateTexture(g_Direct3D9Device9, width, height, 1, 0, format, D3DPOOL_SYSTEMMEM, &stagingTexture, nullptr);
	if(FAILED(result))
		return;

	D3DLOCKED_RECT locked;
	result = IDirect3DTexture9_LockRect(stagingTexture, 0, &locked, nullptr, D3DLOCK_NOSYSLOCK);
	if(FAILED(result))
	{
		IDirect3DTexture9_Release(stagingTexture);
		return;
	}

	unsigned char* srcData = data;
	unsigned char* dstData = reinterpret_cast<unsigned char*>(locked.pBits);
	int rowPitch = width * 4;
	int rowHeight = height;
	if(format != D3DFMT_A8R8G8B8)
	{
		rowPitch = GetDDSRowPitchSize(width, format == D3DFMT_DXT1);
		rowHeight = ((height + 3) / 4);
	}

	if(rowPitch == locked.Pitch)
		memcpy(dstData, srcData, rowPitch * rowHeight);
	else
	{
		if(rowPitch > locked.Pitch)
			rowPitch = locked.Pitch;

		for(UINT row = 0; row < static_cast<UINT>(rowHeight); ++row)
		{
			memcpy(dstData, srcData, rowPitch);
			srcData += rowPitch;
			dstData += locked.Pitch;
		}
	}

	result = IDirect3DTexture9_UnlockRect(stagingTexture, 0);
	if(FAILED(result))
	{
		IDirect3DTexture9_Release(stagingTexture);
		return;
	}
	IDirect3DSurface9* destSurface;
	IDirect3DSurface9* stagingSurface;
	IDirect3DTexture9_GetSurfaceLevel(stagingTexture, 0, &stagingSurface);
	IDirect3DTexture9_GetSurfaceLevel(texture, miplevel, &destSurface);
	IDirect3DDevice9_UpdateSurface(g_Direct3D9Device9, stagingSurface, nullptr, destSurface, nullptr);
	IDirect3DSurface9_Release(destSurface);
	IDirect3DSurface9_Release(stagingSurface);
	IDirect3DTexture9_Release(stagingTexture);
}

void IssueMipMapGeneration(IDirect3DTexture9* texture, unsigned char* data, DDSURFACEDESC2& desc)
{
	typedef void(*LPSmoothStretch)(int, int, int, int, int, int, int, void*, int, int, int, int, int, int, int, void*);
	extern LPSmoothStretch SmoothStretch;

	if(desc.ddsCaps.dwCaps & DDSCAPS_MIPMAP)
	{
		for(DWORD i = 1; i < desc.dwMipMapCount; ++i)
		{
			UINT width = static_cast<UINT>(desc.dwWidth) >> i;
			UINT height = static_cast<UINT>(desc.dwHeight) >> i;

			unsigned char* dstData = new unsigned char[width * height * 4];
			SmoothStretch(0, 0, desc.dwWidth, desc.dwHeight, desc.dwWidth, desc.dwHeight, desc.dwWidth * 4, data,
						0, 0, width, height, width, height, width * 4, dstData);
			IssueTextureUpdate(texture, dstData, width, height, i, D3DFMT_A8R8G8B8);
			delete[] dstData;
		}
	}
}

UINT PushIndexBufferData(LPWORD lpwIndices, UINT dwIndexCount)
{
	if(g_ManagedIndexBufferSize < dwIndexCount || !g_ManagedIndexBuffer)
	{
		if(g_ManagedIndexBuffer)
			IDirect3DIndexBuffer9_Release(g_ManagedIndexBuffer);

		g_ManagedIndexBufferPos = 0;
		g_ManagedIndexBufferSize = std::max<UINT>(4096, dwIndexCount);
		IDirect3DDevice9_CreateIndexBuffer(g_Direct3D9Device9, g_ManagedIndexBufferSize * 2, D3DUSAGE_DYNAMIC, D3DFMT_INDEX16, D3DPOOL_DEFAULT, &g_ManagedIndexBuffer, nullptr);
		IDirect3DDevice9_SetIndices(g_Direct3D9Device9, g_ManagedIndexBuffer);
	}
	UINT startIndice = 0;
	if((dwIndexCount + g_ManagedIndexBufferPos) < g_ManagedIndexBufferSize)
	{
		startIndice = g_ManagedIndexBufferPos;
		g_ManagedIndexBufferPos += dwIndexCount;
	}
	else
		g_ManagedIndexBufferPos = 0;

	if(startIndice != 0)
	{
		void* indexBufferData;
		HRESULT result = IDirect3DIndexBuffer9_Lock(g_ManagedIndexBuffer, startIndice * 2, dwIndexCount * 2, &indexBufferData, (D3DLOCK_NOOVERWRITE|D3DLOCK_NOSYSLOCK));
		if(SUCCEEDED(result))
			memcpy(indexBufferData, lpwIndices, dwIndexCount * 2);

		IDirect3DIndexBuffer9_Unlock(g_ManagedIndexBuffer);
	}
	else
	{
		void* indexBufferData;
		HRESULT result = IDirect3DIndexBuffer9_Lock(g_ManagedIndexBuffer, 0, 0, &indexBufferData, (D3DLOCK_DISCARD|D3DLOCK_NOSYSLOCK));
		if(SUCCEEDED(result))
			memcpy(indexBufferData, lpwIndices, dwIndexCount * 2);

		IDirect3DIndexBuffer9_Unlock(g_ManagedIndexBuffer);
	}
	return startIndice;
}

void IssueDrawingBackBuffer()
{
	DWORD zWrite, zFunc, rangeFog, fogVMode, fogTMode, fogEnable, alphaTest, dithering, alphaBlend;
	DWORD clipping, culling, addrU, addrV, alphaO1, alphaO2, alphaA1, alphaA2, colorO1, colorO2;
	DWORD colorA1, colorA2, textureTransformFlags, textureCoordIndex, magFilter, minFilter;
	D3DVIEWPORT9 oldViewPort;
	IDirect3DBaseTexture9* oldTexture;
	IDirect3DDevice9_GetRenderState(g_Direct3D9Device9, D3DRS_ZWRITEENABLE, &zWrite);
	IDirect3DDevice9_GetRenderState(g_Direct3D9Device9, D3DRS_ZFUNC, &zFunc);
	IDirect3DDevice9_GetRenderState(g_Direct3D9Device9, D3DRS_RANGEFOGENABLE, &rangeFog);
	IDirect3DDevice9_GetRenderState(g_Direct3D9Device9, D3DRS_FOGVERTEXMODE, &fogVMode);
	IDirect3DDevice9_GetRenderState(g_Direct3D9Device9, D3DRS_FOGTABLEMODE, &fogTMode);
	IDirect3DDevice9_GetRenderState(g_Direct3D9Device9, D3DRS_FOGENABLE, &fogEnable);
	IDirect3DDevice9_GetRenderState(g_Direct3D9Device9, D3DRS_ALPHATESTENABLE, &alphaTest);
	IDirect3DDevice9_GetRenderState(g_Direct3D9Device9, D3DRS_DITHERENABLE, &dithering);
	IDirect3DDevice9_GetRenderState(g_Direct3D9Device9, D3DRS_ALPHABLENDENABLE, &alphaBlend);
	IDirect3DDevice9_GetRenderState(g_Direct3D9Device9, D3DRS_CLIPPING, &clipping);
	IDirect3DDevice9_GetRenderState(g_Direct3D9Device9, D3DRS_CULLMODE, &culling);
	IDirect3DDevice9_GetSamplerState(g_Direct3D9Device9, 0, D3DSAMP_ADDRESSU, &addrU);
	IDirect3DDevice9_GetSamplerState(g_Direct3D9Device9, 0, D3DSAMP_ADDRESSV, &addrV);
	IDirect3DDevice9_GetSamplerState(g_Direct3D9Device9, 0, D3DSAMP_MAGFILTER, &magFilter);
	IDirect3DDevice9_GetSamplerState(g_Direct3D9Device9, 0, D3DSAMP_MINFILTER, &minFilter);
	IDirect3DDevice9_GetTextureStageState(g_Direct3D9Device9, 0, D3DTSS_ALPHAOP, &alphaO1);
	IDirect3DDevice9_GetTextureStageState(g_Direct3D9Device9, 1, D3DTSS_ALPHAOP, &alphaO2);
	IDirect3DDevice9_GetTextureStageState(g_Direct3D9Device9, 0, D3DTSS_ALPHAARG1, &alphaA1);
	IDirect3DDevice9_GetTextureStageState(g_Direct3D9Device9, 0, D3DTSS_ALPHAARG2, &alphaA2);
	IDirect3DDevice9_GetTextureStageState(g_Direct3D9Device9, 0, D3DTSS_COLOROP, &colorO1);
	IDirect3DDevice9_GetTextureStageState(g_Direct3D9Device9, 1, D3DTSS_COLOROP, &colorO2);
	IDirect3DDevice9_GetTextureStageState(g_Direct3D9Device9, 0, D3DTSS_COLORARG1, &colorA1);
	IDirect3DDevice9_GetTextureStageState(g_Direct3D9Device9, 0, D3DTSS_COLORARG2, &colorA2);
	IDirect3DDevice9_GetTextureStageState(g_Direct3D9Device9, 0, D3DTSS_TEXTURETRANSFORMFLAGS, &textureTransformFlags);
	IDirect3DDevice9_GetTextureStageState(g_Direct3D9Device9, 0, D3DTSS_TEXCOORDINDEX, &textureCoordIndex);
	IDirect3DDevice9_GetViewport(g_Direct3D9Device9, &oldViewPort);
	IDirect3DDevice9_GetTexture(g_Direct3D9Device9, 0, &oldTexture);

	IDirect3DDevice9_SetRenderState(g_Direct3D9Device9, D3DRS_ZWRITEENABLE, FALSE);
	IDirect3DDevice9_SetRenderState(g_Direct3D9Device9, D3DRS_ZFUNC, D3DCMP_ALWAYS);
	IDirect3DDevice9_SetRenderState(g_Direct3D9Device9, D3DRS_RANGEFOGENABLE, FALSE);
	IDirect3DDevice9_SetRenderState(g_Direct3D9Device9, D3DRS_FOGVERTEXMODE, D3DFOG_NONE);
	IDirect3DDevice9_SetRenderState(g_Direct3D9Device9, D3DRS_FOGTABLEMODE, D3DFOG_NONE);
	IDirect3DDevice9_SetRenderState(g_Direct3D9Device9, D3DRS_FOGENABLE, FALSE);
	IDirect3DDevice9_SetRenderState(g_Direct3D9Device9, D3DRS_ALPHATESTENABLE, FALSE);
	IDirect3DDevice9_SetRenderState(g_Direct3D9Device9, D3DRS_DITHERENABLE, FALSE);
	IDirect3DDevice9_SetRenderState(g_Direct3D9Device9, D3DRS_ALPHABLENDENABLE, FALSE);
	IDirect3DDevice9_SetRenderState(g_Direct3D9Device9, D3DRS_CLIPPING, FALSE);
	IDirect3DDevice9_SetRenderState(g_Direct3D9Device9, D3DRS_CULLMODE, D3DCULL_NONE);
	IDirect3DDevice9_SetSamplerState(g_Direct3D9Device9, 0, D3DSAMP_ADDRESSU, D3DTADDRESS_CLAMP);
	IDirect3DDevice9_SetSamplerState(g_Direct3D9Device9, 0, D3DSAMP_ADDRESSV, D3DTADDRESS_CLAMP);
	IDirect3DDevice9_SetSamplerState(g_Direct3D9Device9, 0, D3DSAMP_MAGFILTER, D3DTEXF_POINT);
	IDirect3DDevice9_SetSamplerState(g_Direct3D9Device9, 0, D3DSAMP_MINFILTER, D3DTEXF_POINT);
	IDirect3DDevice9_SetTextureStageState(g_Direct3D9Device9, 0, D3DTSS_ALPHAOP, D3DTOP_MODULATE);
	IDirect3DDevice9_SetTextureStageState(g_Direct3D9Device9, 1, D3DTSS_ALPHAOP, D3DTOP_DISABLE);
	IDirect3DDevice9_SetTextureStageState(g_Direct3D9Device9, 0, D3DTSS_ALPHAARG1, D3DTA_TEXTURE);
	IDirect3DDevice9_SetTextureStageState(g_Direct3D9Device9, 0, D3DTSS_ALPHAARG2, D3DTA_DIFFUSE);
	IDirect3DDevice9_SetTextureStageState(g_Direct3D9Device9, 0, D3DTSS_COLOROP, D3DTOP_MODULATE);
	IDirect3DDevice9_SetTextureStageState(g_Direct3D9Device9, 1, D3DTSS_COLOROP, D3DTOP_DISABLE);
	IDirect3DDevice9_SetTextureStageState(g_Direct3D9Device9, 0, D3DTSS_COLORARG1, D3DTA_TEXTURE);
	IDirect3DDevice9_SetTextureStageState(g_Direct3D9Device9, 0, D3DTSS_COLORARG2, D3DTA_DIFFUSE);
	IDirect3DDevice9_SetTextureStageState(g_Direct3D9Device9, 0, D3DTSS_TEXTURETRANSFORMFLAGS, D3DTTFF_DISABLE);
	IDirect3DDevice9_SetTextureStageState(g_Direct3D9Device9, 0, D3DTSS_TEXCOORDINDEX, 0);
	D3DVIEWPORT9 newViewPort = {0, 0, g_Direct3D9_PParams.BackBufferWidth, g_Direct3D9_PParams.BackBufferHeight, 0.f, 1.f};
	IDirect3DDevice9_SetViewport(g_Direct3D9Device9, &newViewPort);
	IDirect3DDevice9_SetTexture(g_Direct3D9Device9, 0, g_ManagedBackBuffer);

	//Draw texture
	{
		struct D3DTLVERTEX
		{
			float sx;
			float sy;
			float sz;
			float rhw;
			D3DCOLOR color;
			D3DCOLOR specular;
			float tu;
			float tv;
		};

		float minx = -0.5f;
		float miny = -0.5f;
		float maxx = static_cast<float>(g_Direct3D9_PParams.BackBufferWidth) - 0.5f;
		float maxy = static_cast<float>(g_Direct3D9_PParams.BackBufferHeight) - 0.5f;

		D3DTLVERTEX vertices[4];
		vertices[0].sx = minx;
		vertices[0].sy = miny;
		vertices[0].sz = 0.f;
		vertices[0].rhw = 1.f;
		vertices[0].color = 0xFFFFFFFF;
		vertices[0].specular = 0xFFFFFFFF;
		vertices[0].tu = 0.f;
		vertices[0].tv = 0.f;

		vertices[1].sx = maxx;
		vertices[1].sy = miny;
		vertices[1].sz = 0.f;
		vertices[1].rhw = 1.f;
		vertices[1].color = 0xFFFFFFFF;
		vertices[1].specular = 0xFFFFFFFF;
		vertices[1].tu = 1.f;
		vertices[1].tv = 0.f;

		vertices[2].sx = maxx;
		vertices[2].sy = maxy;
		vertices[2].sz = 0.f;
		vertices[2].rhw = 1.f;
		vertices[2].color = 0xFFFFFFFF;
		vertices[2].specular = 0xFFFFFFFF;
		vertices[2].tu = 1.f;
		vertices[2].tv = 1.f;

		vertices[3].sx = minx;
		vertices[3].sy = maxy;
		vertices[3].sz = 0.f;
		vertices[3].rhw = 1.f;
		vertices[3].color = 0xFFFFFFFF;
		vertices[3].specular = 0xFFFFFFFF;
		vertices[3].tu = 0.f;
		vertices[3].tv = 1.f;

		SetCurrentFVF((D3DFVF_XYZRHW|D3DFVF_DIFFUSE|D3DFVF_SPECULAR|D3DFVF_TEX1));
		if(g_HaveGammeCorrection)
		{
			float constants[4] = { 1.f - (g_DeviceGamma + 0.5f) + 1.f, g_DeviceBrightness + 0.5f, 1.f - (g_DeviceContrast + 0.5f) + 1.f, 1.f };
			IDirect3DDevice9_SetPixelShader(g_Direct3D9Device9, g_GammaCorrectionPS);
			IDirect3DDevice9_SetPixelShaderConstantF(g_Direct3D9Device9, 0, constants, 1);
		}
		IDirect3DDevice9_DrawPrimitiveUP(g_Direct3D9Device9, D3DPT_TRIANGLEFAN, 2, vertices, Direct3DCurrentStride);
		IDirect3DDevice9_SetPixelShader(g_Direct3D9Device9, nullptr);
	}

	IDirect3DDevice9_SetRenderState(g_Direct3D9Device9, D3DRS_ZWRITEENABLE, zWrite);
	IDirect3DDevice9_SetRenderState(g_Direct3D9Device9, D3DRS_ZFUNC, zFunc);
	IDirect3DDevice9_SetRenderState(g_Direct3D9Device9, D3DRS_RANGEFOGENABLE, rangeFog);
	IDirect3DDevice9_SetRenderState(g_Direct3D9Device9, D3DRS_FOGVERTEXMODE, fogVMode);
	IDirect3DDevice9_SetRenderState(g_Direct3D9Device9, D3DRS_FOGTABLEMODE, fogTMode);
	IDirect3DDevice9_SetRenderState(g_Direct3D9Device9, D3DRS_FOGENABLE, fogEnable);
	IDirect3DDevice9_SetRenderState(g_Direct3D9Device9, D3DRS_ALPHATESTENABLE, alphaTest);
	IDirect3DDevice9_SetRenderState(g_Direct3D9Device9, D3DRS_DITHERENABLE, dithering);
	IDirect3DDevice9_SetRenderState(g_Direct3D9Device9, D3DRS_ALPHABLENDENABLE, alphaBlend);
	IDirect3DDevice9_SetRenderState(g_Direct3D9Device9, D3DRS_CLIPPING, clipping);
	IDirect3DDevice9_SetRenderState(g_Direct3D9Device9, D3DRS_CULLMODE, culling);
	IDirect3DDevice9_SetSamplerState(g_Direct3D9Device9, 0, D3DSAMP_ADDRESSU, addrU);
	IDirect3DDevice9_SetSamplerState(g_Direct3D9Device9, 0, D3DSAMP_ADDRESSV, addrV);
	IDirect3DDevice9_SetSamplerState(g_Direct3D9Device9, 0, D3DSAMP_MAGFILTER, magFilter);
	IDirect3DDevice9_SetSamplerState(g_Direct3D9Device9, 0, D3DSAMP_MINFILTER, minFilter);
	IDirect3DDevice9_SetTextureStageState(g_Direct3D9Device9, 0, D3DTSS_ALPHAOP, alphaO1);
	IDirect3DDevice9_SetTextureStageState(g_Direct3D9Device9, 1, D3DTSS_ALPHAOP, alphaO2);
	IDirect3DDevice9_SetTextureStageState(g_Direct3D9Device9, 0, D3DTSS_ALPHAARG1, alphaA1);
	IDirect3DDevice9_SetTextureStageState(g_Direct3D9Device9, 0, D3DTSS_ALPHAARG2, alphaA2);
	IDirect3DDevice9_SetTextureStageState(g_Direct3D9Device9, 0, D3DTSS_COLOROP, colorO1);
	IDirect3DDevice9_SetTextureStageState(g_Direct3D9Device9, 1, D3DTSS_COLOROP, colorO2);
	IDirect3DDevice9_SetTextureStageState(g_Direct3D9Device9, 0, D3DTSS_COLORARG1, colorA1);
	IDirect3DDevice9_SetTextureStageState(g_Direct3D9Device9, 0, D3DTSS_COLORARG2, colorA2);
	IDirect3DDevice9_SetTextureStageState(g_Direct3D9Device9, 0, D3DTSS_TEXTURETRANSFORMFLAGS, textureTransformFlags);
	IDirect3DDevice9_SetTextureStageState(g_Direct3D9Device9, 0, D3DTSS_TEXCOORDINDEX, textureCoordIndex);
	IDirect3DDevice9_SetViewport(g_Direct3D9Device9, &oldViewPort);
	IDirect3DDevice9_SetTexture(g_Direct3D9Device9, 0, oldTexture);
}

class MyClipper : public IDirectDrawClipper
{
	public:
		MyClipper() {refCount = 1;}

		/*** IUnknown methods ***/
		HRESULT __stdcall QueryInterface(REFIID riid, void** ppvObj) {return S_OK;}
		ULONG __stdcall AddRef() {return ++refCount;}
		ULONG __stdcall Release()
		{
			if(--refCount == 0)
			{
				delete this;
				return 0;
			}
			return refCount;
		}

		/*** IDirectDrawClipper methods ***/
		HRESULT __stdcall GetClipList(LPRECT x, LPRGNDATA y, LPDWORD z) {return S_OK;}
		HRESULT __stdcall GetHWnd(HWND* handle) {*handle = hWnd; return S_OK;}
		HRESULT __stdcall Initialize(LPDIRECTDRAW x, DWORD y) {return S_OK;}
		HRESULT __stdcall IsClipListChanged(BOOL* x) {return S_OK;}
		HRESULT __stdcall SetClipList(LPRGNDATA x, DWORD y) {return S_OK;}
		HRESULT __stdcall SetHWnd(DWORD x, HWND handle) {hWnd = handle; return S_OK;}

	private:
		HWND hWnd;
		int refCount;
};

class MyDirectDrawSurface7 : public IDirectDrawSurface7
{
	public:
		MyDirectDrawSurface7()
		{
			g_Direct3D9Textures.push_back(this);

			Data = nullptr;
			BaseTexture = nullptr;
			RefCount = 1;
		}

		~MyDirectDrawSurface7()
		{
			auto it = std::find(g_Direct3D9Textures.begin(), g_Direct3D9Textures.end(), this);
			if(it != g_Direct3D9Textures.end())
			{
				*it = g_Direct3D9Textures.back();
				g_Direct3D9Textures.pop_back();
			}

			for(LPDIRECTDRAWSURFACE7 mipmap : AttachedSurfaces)
				mipmap->Release();
			
			delete[] Data;
			if(BaseTexture)
				IDirect3DTexture9_Release(BaseTexture);
		}

		/*** IUnknown methods ***/
		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObj) {return S_OK;}
		ULONG STDMETHODCALLTYPE AddRef() {return ++RefCount;}
		ULONG STDMETHODCALLTYPE Release()
		{
			if(--RefCount == 0)
			{
				delete this;
				return 0;
			}
			return RefCount;
		}

		/*** IDirectDrawSurface7 methods ***/
		HRESULT STDMETHODCALLTYPE AddAttachedSurface(LPDIRECTDRAWSURFACE7 lpDDSAttachedSurface)
		{
			lpDDSAttachedSurface->AddRef();
			AttachedSurfaces.push_back(lpDDSAttachedSurface);
			return S_OK;
		}
		HRESULT STDMETHODCALLTYPE AddOverlayDirtyRect(LPRECT lpRect) {return S_OK;} // Not used
		HRESULT STDMETHODCALLTYPE Blt(LPRECT lpDestRect, LPDIRECTDRAWSURFACE7 lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwFlags, LPDDBLTFX lpDDBltFx) {return S_OK;} // Used but not needed
		HRESULT STDMETHODCALLTYPE BltBatch(LPDDBLTBATCH lpDDBltBatch, DWORD dwCount, DWORD dwFlags) {return S_OK;} // Used but not needed
		HRESULT STDMETHODCALLTYPE BltFast(DWORD dwX, DWORD dwY, LPDIRECTDRAWSURFACE7 lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwTrans) {return S_OK;} // Used but not needed
		HRESULT STDMETHODCALLTYPE DeleteAttachedSurface(DWORD dwFlags, LPDIRECTDRAWSURFACE7 lpDDSAttachedSurface) {return S_OK;} // Not used
		HRESULT STDMETHODCALLTYPE EnumAttachedSurfaces(LPVOID lpContext, LPDDENUMSURFACESCALLBACK7 lpEnumSurfacesCallback) {return S_OK;} // Not used
		HRESULT STDMETHODCALLTYPE EnumOverlayZOrders(DWORD dwFlags, LPVOID lpContext, LPDDENUMSURFACESCALLBACK7 lpfnCallback) {return S_OK;} // Not used
		HRESULT STDMETHODCALLTYPE Flip(LPDIRECTDRAWSURFACE7 lpDDSurfaceTargetOverride, DWORD dwFlags) {return S_OK;} // Used but not needed
		HRESULT STDMETHODCALLTYPE GetAttachedSurface(LPDDSCAPS2 lpDDSCaps, LPDIRECTDRAWSURFACE7* lplpDDAttachedSurface)
		{
			if(AttachedSurfaces.empty())
				return E_FAIL;

			*lplpDDAttachedSurface = AttachedSurfaces[0];
			AttachedSurfaces[0]->AddRef();
			return S_OK;
		}
		HRESULT STDMETHODCALLTYPE GetBltStatus(DWORD dwFlags) {return S_OK;} // Used but not needed
		HRESULT STDMETHODCALLTYPE GetCaps(LPDDSCAPS2 lpDDSCaps) {*lpDDSCaps = OriginalSurfaceDesc.ddsCaps; return S_OK;}
		HRESULT STDMETHODCALLTYPE GetClipper(LPDIRECTDRAWCLIPPER* lplpDDClipper) {*lplpDDClipper = nullptr; return S_OK;} // Used but not needed
		HRESULT STDMETHODCALLTYPE GetColorKey(DWORD dwFlags, LPDDCOLORKEY lpDDColorKey) {return S_OK;} // Not used
		HRESULT STDMETHODCALLTYPE GetDC(HDC* lphDC) {return S_OK;} // Not used
		HRESULT STDMETHODCALLTYPE GetFlipStatus(DWORD dwFlags) {return S_OK;} // Used but not needed
		HRESULT STDMETHODCALLTYPE GetOverlayPosition(LPLONG lplX, LPLONG lplY) {return S_OK;} // Not used
		HRESULT STDMETHODCALLTYPE GetPalette(LPDIRECTDRAWPALETTE* lplpDDPalette) {return S_OK;} // Used by Gothic but can't be emulated
		HRESULT STDMETHODCALLTYPE GetPixelFormat(LPDDPIXELFORMAT lpDDPixelFormat) {*lpDDPixelFormat = OriginalSurfaceDesc.ddpfPixelFormat; return S_OK;}
		HRESULT STDMETHODCALLTYPE GetSurfaceDesc(LPDDSURFACEDESC2 lpDDSurfaceDesc) {*lpDDSurfaceDesc = OriginalSurfaceDesc; return S_OK;}
		HRESULT STDMETHODCALLTYPE Initialize(LPDIRECTDRAW lpDD, LPDDSURFACEDESC2 lpDDSurfaceDesc) {return S_OK;} // Not used
		HRESULT STDMETHODCALLTYPE IsLost()
		{
			if(!BaseTexture) return DDERR_SURFACELOST;
			else return S_OK;
		}
		HRESULT STDMETHODCALLTYPE Lock(LPRECT lpDestRect, LPDDSURFACEDESC2 lpDDSurfaceDesc, DWORD dwFlags, HANDLE hEvent)
		{
			*lpDDSurfaceDesc = OriginalSurfaceDesc;
			if(!BaseTexture)
				return DDERR_SURFACELOST;

			int redBits = GetNumberOfBits(OriginalSurfaceDesc.ddpfPixelFormat.dwRBitMask);
			int greenBits = GetNumberOfBits(OriginalSurfaceDesc.ddpfPixelFormat.dwGBitMask);
			int blueBits = GetNumberOfBits(OriginalSurfaceDesc.ddpfPixelFormat.dwBBitMask);
			int alphaBits = GetNumberOfBits(OriginalSurfaceDesc.ddpfPixelFormat.dwRGBAlphaBitMask);
			int bpp = redBits + greenBits + blueBits + alphaBits;
			DWORD rowHeight = lpDDSurfaceDesc->dwHeight;
			if(BaseTextureFormat != D3DFMT_A8R8G8B8)
			{
				lpDDSurfaceDesc->lPitch = GetDDSRowPitchSize(lpDDSurfaceDesc->dwWidth, BaseTextureFormat == D3DFMT_DXT1);
				rowHeight = (rowHeight + 3) / 4;
			}
			else
				lpDDSurfaceDesc->lPitch = lpDDSurfaceDesc->dwWidth * (bpp == 16 ? 2 : 4);

			if((bpp != 24 && bpp != 16) || !Data)
			{
				delete[] Data;
				Data = new unsigned char[lpDDSurfaceDesc->lPitch * rowHeight];
			}
			lpDDSurfaceDesc->lpSurface = Data;
			return S_OK;
		}
		HRESULT STDMETHODCALLTYPE ReleaseDC(HDC hDC) {return S_OK;} // Not used
		HRESULT STDMETHODCALLTYPE Restore()
		{
			if(BaseTexture)
				return S_OK;

			UINT width = static_cast<UINT>(OriginalSurfaceDesc.dwWidth);
			UINT height = static_cast<UINT>(OriginalSurfaceDesc.dwHeight);
			UINT mipMapCount = 1;
			if(OriginalSurfaceDesc.ddsCaps.dwCaps & DDSCAPS_MIPMAP)
				mipMapCount = static_cast<UINT>(OriginalSurfaceDesc.dwMipMapCount);

			HRESULT result = IDirect3DDevice9_CreateTexture(g_Direct3D9Device9, width, height, mipMapCount, 0, BaseTextureFormat, D3DPOOL_DEFAULT, &BaseTexture, nullptr);
			if(SUCCEEDED(result))
			{
				int redBits = GetNumberOfBits(OriginalSurfaceDesc.ddpfPixelFormat.dwRBitMask);
				int greenBits = GetNumberOfBits(OriginalSurfaceDesc.ddpfPixelFormat.dwGBitMask);
				int blueBits = GetNumberOfBits(OriginalSurfaceDesc.ddpfPixelFormat.dwBBitMask);
				int alphaBits = GetNumberOfBits(OriginalSurfaceDesc.ddpfPixelFormat.dwRGBAlphaBitMask);
				int bpp = redBits + greenBits + blueBits + alphaBits;
				if(bpp == 16)
				{
					UINT width = static_cast<UINT>(OriginalSurfaceDesc.dwWidth);
					UINT height = static_cast<UINT>(OriginalSurfaceDesc.dwHeight);

					UINT realDataSize = width * height * 4;
					unsigned char* dst = new unsigned char[realDataSize];
					switch(OriginalSurfaceDesc.ddpfPixelFormat.dwFourCC)
					{
						case 1: Convert1555to8888(dst, Data, realDataSize); break;
						case 2: Convert4444to8888(dst, Data, realDataSize); break;
						default: Convert565to8888(dst, Data, realDataSize); break;
					}

					IssueTextureUpdate(BaseTexture, dst, width, height, 0, BaseTextureFormat);
					IssueMipMapGeneration(BaseTexture, dst, OriginalSurfaceDesc);
					delete[] dst;
				}
			}
			return result;
		}
		HRESULT STDMETHODCALLTYPE SetClipper(LPDIRECTDRAWCLIPPER lpDDClipper) {return S_OK;} // Used but not needed
		HRESULT STDMETHODCALLTYPE SetColorKey(DWORD dwFlags, LPDDCOLORKEY lpDDColorKey) {return S_OK;} // Not used
		HRESULT STDMETHODCALLTYPE SetOverlayPosition(LONG lX, LONG lY) {return S_OK;} // Not used
		HRESULT STDMETHODCALLTYPE SetPalette(LPDIRECTDRAWPALETTE lpDDPalette) {return S_OK;} // Used by Gothic but can't be emulated
		HRESULT STDMETHODCALLTYPE Unlock(LPRECT lpRect)
		{
			if(!BaseTexture)
				return DDERR_SURFACELOST;

			UINT width = static_cast<UINT>(OriginalSurfaceDesc.dwWidth);
			UINT height = static_cast<UINT>(OriginalSurfaceDesc.dwHeight);

			int redBits = GetNumberOfBits(OriginalSurfaceDesc.ddpfPixelFormat.dwRBitMask);
			int greenBits = GetNumberOfBits(OriginalSurfaceDesc.ddpfPixelFormat.dwGBitMask);
			int blueBits = GetNumberOfBits(OriginalSurfaceDesc.ddpfPixelFormat.dwBBitMask);
			int alphaBits = GetNumberOfBits(OriginalSurfaceDesc.ddpfPixelFormat.dwRGBAlphaBitMask);
			int bpp = redBits + greenBits + blueBits + alphaBits;
			if(bpp == 16)
			{
				if(!g_ReadOnlyLightmapData)
				{
					UINT realDataSize = width * height * 4;
					unsigned char* dst = new unsigned char[realDataSize];
					switch(OriginalSurfaceDesc.ddpfPixelFormat.dwFourCC)
					{
						case 1: Convert1555to8888(dst, Data, realDataSize); break;
						case 2: Convert4444to8888(dst, Data, realDataSize); break;
						default: Convert565to8888(dst, Data, realDataSize); break;
					}

					IssueTextureUpdate(BaseTexture, dst, width, height, 0, BaseTextureFormat);
					IssueMipMapGeneration(BaseTexture, dst, OriginalSurfaceDesc);
					delete[] dst;
				}
			}
			else if(bpp == 24 || bpp == 32)
			{
				if(OriginalSurfaceDesc.ddpfPixelFormat.dwFourCC == 1)
					ConvertRGBAtoBGRA(Data, Data, width * height * 4);

				IssueTextureUpdate(BaseTexture, Data, width, height, 0, BaseTextureFormat);
			}
			else
				IssueTextureUpdate(BaseTexture, Data, width, height, 0, BaseTextureFormat);

			if(bpp != 24 && bpp != 16)
			{
				delete[] Data;
				Data = nullptr;
			}
			return S_OK;
		}
		HRESULT STDMETHODCALLTYPE UpdateOverlay(LPRECT lpSrcRect, LPDIRECTDRAWSURFACE7 lpDDDestSurface, LPRECT lpDestRect, DWORD dwFlags, LPDDOVERLAYFX lpDDOverlayFx) {return S_OK;} // Not used
		HRESULT STDMETHODCALLTYPE UpdateOverlayDisplay(DWORD dwFlags) {return S_OK;} // Not used
		HRESULT STDMETHODCALLTYPE UpdateOverlayZOrder(DWORD dwFlags, LPDIRECTDRAWSURFACE7 lpDDSReference) {return S_OK;} // Not used
		HRESULT STDMETHODCALLTYPE GetDDInterface(LPVOID* lplpDD) {return S_OK;} // Not used
		HRESULT STDMETHODCALLTYPE PageLock(DWORD dwFlags) {return S_OK;} // Not used
		HRESULT STDMETHODCALLTYPE PageUnlock(DWORD dwFlags) {return S_OK;} // Not used
		HRESULT STDMETHODCALLTYPE SetSurfaceDesc(LPDDSURFACEDESC2 lpDDSurfaceDesc, DWORD dwFlags)
		{
			if(BaseTexture)
			{
				IDirect3DTexture9_Release(BaseTexture);
				BaseTexture = nullptr;
			}

			OriginalSurfaceDesc = *lpDDSurfaceDesc;
			BaseTextureFormat = D3DFMT_A8R8G8B8;

			int redBits = GetNumberOfBits(lpDDSurfaceDesc->ddpfPixelFormat.dwRBitMask);
			int greenBits = GetNumberOfBits(lpDDSurfaceDesc->ddpfPixelFormat.dwGBitMask);
			int blueBits = GetNumberOfBits(lpDDSurfaceDesc->ddpfPixelFormat.dwBBitMask);
			int alphaBits = GetNumberOfBits(lpDDSurfaceDesc->ddpfPixelFormat.dwRGBAlphaBitMask);
			int bpp = redBits + greenBits + blueBits + alphaBits;
			switch(bpp)
			{
				// 16bit -> 32bit conversion
				case 16: BaseTextureFormat = D3DFMT_A8R8G8B8; break;
				// 24bit -> 32bit conversion
				case 24: BaseTextureFormat = D3DFMT_A8R8G8B8; break;
				// 32bit textures(Unused by Gothic)
				case 32: BaseTextureFormat = D3DFMT_A8R8G8B8; break;
				// DDS-Texture
				case 0:
				{
					if((lpDDSurfaceDesc->ddpfPixelFormat.dwFlags & DDPF_FOURCC) == DDPF_FOURCC)
					{
						switch(lpDDSurfaceDesc->ddpfPixelFormat.dwFourCC)
						{
							case FOURCC_DXT1: BaseTextureFormat = D3DFMT_DXT1; break;
							case FOURCC_DXT2: BaseTextureFormat = D3DFMT_DXT2; break;
							case FOURCC_DXT3: BaseTextureFormat = D3DFMT_DXT3; break;
							case FOURCC_DXT4: BaseTextureFormat = D3DFMT_DXT4; break;
							case FOURCC_DXT5: BaseTextureFormat = D3DFMT_DXT5; break;
						}
					}
				}
			}

			UINT width = static_cast<UINT>(OriginalSurfaceDesc.dwWidth);
			UINT height = static_cast<UINT>(OriginalSurfaceDesc.dwHeight);
			UINT mipMapCount = 1;
			if(OriginalSurfaceDesc.ddsCaps.dwCaps & DDSCAPS_MIPMAP)
				mipMapCount = static_cast<UINT>(OriginalSurfaceDesc.dwMipMapCount);

			return IDirect3DDevice9_CreateTexture(g_Direct3D9Device9, width, height, mipMapCount, 0, BaseTextureFormat, D3DPOOL_DEFAULT, &BaseTexture, nullptr);
		}
		HRESULT STDMETHODCALLTYPE SetPrivateData(REFGUID guidTag, LPVOID lpData, DWORD cbSize, DWORD dwFlags) {return IDirect3DTexture9_SetPrivateData(BaseTexture, guidTag, lpData, cbSize, dwFlags);}
		HRESULT STDMETHODCALLTYPE GetPrivateData(REFGUID guidTag, LPVOID lpBuffer, LPDWORD lpcbBufferSize) {return IDirect3DTexture9_GetPrivateData(BaseTexture, guidTag, lpBuffer, lpcbBufferSize);}
		HRESULT STDMETHODCALLTYPE FreePrivateData(REFGUID guidTag) {return IDirect3DTexture9_FreePrivateData(BaseTexture, guidTag);}
		HRESULT STDMETHODCALLTYPE GetUniquenessValue(LPDWORD lpValue) {return S_OK;} // Not used
		HRESULT STDMETHODCALLTYPE ChangeUniquenessValue() {return S_OK;} // Not used
		HRESULT STDMETHODCALLTYPE SetPriority(DWORD dwPriority) {IDirect3DTexture9_SetPriority(BaseTexture, dwPriority); return S_OK;}
		HRESULT STDMETHODCALLTYPE GetPriority(LPDWORD dwPriority) {*dwPriority = IDirect3DTexture9_GetPriority(BaseTexture); return S_OK;}
		HRESULT STDMETHODCALLTYPE SetLOD(DWORD dwLOD) {IDirect3DTexture9_SetLOD(BaseTexture, dwLOD); return S_OK;}
		HRESULT STDMETHODCALLTYPE GetLOD(LPDWORD dwLOD) {*dwLOD = IDirect3DTexture9_GetLOD(BaseTexture); return S_OK;}

		void ReleaseTexture()
		{
			if(BaseTexture)
			{
				IDirect3DTexture9_Release(BaseTexture);
				BaseTexture = nullptr;
			}
		}
		D3DFORMAT GetTextureFormat() {return BaseTextureFormat;}
		IDirect3DTexture9* GetTexture() {return BaseTexture;}

	private:
		std::vector<IDirectDrawSurface7*> AttachedSurfaces;
		unsigned char* Data;
		IDirect3DTexture9* BaseTexture;
		D3DFORMAT BaseTextureFormat;
		DDSURFACEDESC2 OriginalSurfaceDesc;
		int RefCount;
};

class FakeDirectDrawSurface7 : public IDirectDrawSurface7
{
	public:
		FakeDirectDrawSurface7(DDSURFACEDESC2* desc, MyDirectDrawSurface7* baseTexture, int mipLevel)
		{
			Data = nullptr;
			BaseTexture = baseTexture;
			OriginalDesc = *desc;
			RefCount = 0;
			MipLevel = mipLevel;
		}

		~FakeDirectDrawSurface7()
		{
			for(LPDIRECTDRAWSURFACE7 mipmap : AttachedSurfaces)
				mipmap->Release();

			delete[] Data;
		}

		/*** IUnknown methods ***/
		HRESULT __stdcall QueryInterface(REFIID riid, void** ppvObj) {return S_OK;}
		ULONG __stdcall AddRef() {return ++RefCount;}
		ULONG __stdcall Release()
		{
			if(--RefCount == 0)
			{
				delete this;
				return 0;
			}
			return RefCount;
		}

		/*** IDirectDrawSurface7 methods ***/
		HRESULT __stdcall AddAttachedSurface(LPDIRECTDRAWSURFACE7 lpDDSAttachedSurface)
		{
			lpDDSAttachedSurface->AddRef();
			AttachedSurfaces.push_back(lpDDSAttachedSurface);
			return S_OK;
		}
		HRESULT __stdcall AddOverlayDirtyRect(LPRECT lpRect) {return S_OK;} // Not used
		HRESULT __stdcall Blt(LPRECT lpDestRect, LPDIRECTDRAWSURFACE7 lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwFlags, LPDDBLTFX lpDDBltFx) {return S_OK;} // Not used
		HRESULT __stdcall BltBatch(LPDDBLTBATCH lpDDBltBatch, DWORD dwCount, DWORD dwFlags) {return S_OK;} // Not used
		HRESULT __stdcall BltFast(DWORD dwX, DWORD dwY, LPDIRECTDRAWSURFACE7 lpDDSrcSurface, LPRECT lpSrcRect, DWORD dwTrans) {return S_OK;} // Not used
		HRESULT __stdcall DeleteAttachedSurface(DWORD dwFlags, LPDIRECTDRAWSURFACE7 lpDDSAttachedSurface) {return S_OK;} // Not used
		HRESULT __stdcall EnumAttachedSurfaces(LPVOID lpContext, LPDDENUMSURFACESCALLBACK7 lpEnumSurfacesCallback) {return S_OK;} // Not used
		HRESULT __stdcall EnumOverlayZOrders(DWORD dwFlags, LPVOID lpContext, LPDDENUMSURFACESCALLBACK7 lpfnCallback) {return S_OK;} // Not used
		HRESULT __stdcall Flip(LPDIRECTDRAWSURFACE7 lpDDSurfaceTargetOverride, DWORD dwFlags) {return S_OK;} // Not used
		HRESULT __stdcall GetAttachedSurface(LPDDSCAPS2 lpDDSCaps, LPDIRECTDRAWSURFACE7* lplpDDAttachedSurface)
		{
			if(AttachedSurfaces.empty())
				return E_FAIL;

			*lplpDDAttachedSurface = AttachedSurfaces[0];
			AttachedSurfaces[0]->AddRef();
			return S_OK;
		}
		HRESULT __stdcall GetBltStatus(DWORD dwFlags) {return S_OK;} // Not used
		HRESULT __stdcall GetCaps(LPDDSCAPS2 lpDDSCaps) {*lpDDSCaps = OriginalDesc.ddsCaps; return S_OK;}
		HRESULT __stdcall GetClipper(LPDIRECTDRAWCLIPPER* lplpDDClipper) {return S_OK;} // Not used
		HRESULT __stdcall GetColorKey(DWORD dwFlags, LPDDCOLORKEY lpDDColorKey) {return S_OK;} // Not used
		HRESULT __stdcall GetDC(HDC* lphDC) {return S_OK;} // Not used
		HRESULT __stdcall GetFlipStatus(DWORD dwFlags) {return S_OK;} // Not used
		HRESULT __stdcall GetOverlayPosition(LPLONG lplX, LPLONG lplY) {return S_OK;} // Not used
		HRESULT __stdcall GetPalette(LPDIRECTDRAWPALETTE* lplpDDPalette) {return S_OK;} // Not used
		HRESULT __stdcall GetPixelFormat(LPDDPIXELFORMAT lpDDPixelFormat) {*lpDDPixelFormat = OriginalDesc.ddpfPixelFormat; return S_OK;}
		HRESULT __stdcall GetSurfaceDesc(LPDDSURFACEDESC2 lpDDSurfaceDesc) {*lpDDSurfaceDesc = OriginalDesc; return S_OK;}
		HRESULT __stdcall Initialize(LPDIRECTDRAW lpDD, LPDDSURFACEDESC2 lpDDSurfaceDesc) {return S_OK;} // Not used
		HRESULT __stdcall IsLost() {return S_OK;} // Not used
		HRESULT __stdcall Lock(LPRECT lpDestRect, LPDDSURFACEDESC2 lpDDSurfaceDesc, DWORD dwFlags, HANDLE hEvent)
		{
			*lpDDSurfaceDesc = OriginalDesc;

			lpDDSurfaceDesc->dwWidth = static_cast<UINT>(OriginalDesc.dwWidth) >> MipLevel;
			lpDDSurfaceDesc->dwHeight = static_cast<UINT>(OriginalDesc.dwHeight) >> MipLevel;
			DWORD rowHeight = lpDDSurfaceDesc->dwHeight;
			if(BaseTexture && BaseTexture->GetTextureFormat() != D3DFMT_A8R8G8B8)
			{
				lpDDSurfaceDesc->lPitch = GetDDSRowPitchSize(lpDDSurfaceDesc->dwWidth, BaseTexture->GetTextureFormat() == D3DFMT_DXT1);
				rowHeight = (rowHeight + 3) / 4;
			}
			else
			{
				int redBits = GetNumberOfBits(OriginalDesc.ddpfPixelFormat.dwRBitMask);
				int greenBits = GetNumberOfBits(OriginalDesc.ddpfPixelFormat.dwGBitMask);
				int blueBits = GetNumberOfBits(OriginalDesc.ddpfPixelFormat.dwBBitMask);
				int alphaBits = GetNumberOfBits(OriginalDesc.ddpfPixelFormat.dwRGBAlphaBitMask);
				int bpp = redBits + greenBits + blueBits + alphaBits;
				lpDDSurfaceDesc->lPitch = lpDDSurfaceDesc->dwWidth * (bpp == 16 ? 2 : 4);
			}

			delete[] Data;
			Data = new unsigned char[lpDDSurfaceDesc->lPitch * rowHeight];
			lpDDSurfaceDesc->lpSurface = Data;
			return S_OK;
		}
		HRESULT __stdcall ReleaseDC(HDC hDC) {return S_OK;} // Not used
		HRESULT __stdcall Restore() {return S_OK;} // Not used
		HRESULT __stdcall SetClipper(LPDIRECTDRAWCLIPPER lpDDClipper) {return S_OK;} // Not used
		HRESULT __stdcall SetColorKey(DWORD dwFlags, LPDDCOLORKEY lpDDColorKey) {return S_OK;} // Not used
		HRESULT __stdcall SetOverlayPosition(LONG lX, LONG lY) {return S_OK;} // Not used
		HRESULT __stdcall SetPalette(LPDIRECTDRAWPALETTE lpDDPalette) {return S_OK;} // Not used
		HRESULT __stdcall Unlock(LPRECT lpRect)
		{
			int redBits = GetNumberOfBits(OriginalDesc.ddpfPixelFormat.dwRBitMask);
			int greenBits = GetNumberOfBits(OriginalDesc.ddpfPixelFormat.dwGBitMask);
			int blueBits = GetNumberOfBits(OriginalDesc.ddpfPixelFormat.dwBBitMask);
			int alphaBits = GetNumberOfBits(OriginalDesc.ddpfPixelFormat.dwRGBAlphaBitMask);
			int bpp = redBits + greenBits + blueBits + alphaBits;
			if(bpp != 16 && BaseTexture)
			{
				if(BaseTexture->IsLost() == DDERR_SURFACELOST)
					BaseTexture->Restore();

				UINT width = static_cast<UINT>(OriginalDesc.dwWidth) >> MipLevel;
				UINT height = static_cast<UINT>(OriginalDesc.dwHeight) >> MipLevel;
				if((bpp == 24 || bpp == 32) && OriginalDesc.ddpfPixelFormat.dwFourCC == 1)
					ConvertRGBAtoBGRA(Data, Data, width * height * 4);

				IssueTextureUpdate(BaseTexture->GetTexture(), Data, width, height, MipLevel, BaseTexture->GetTextureFormat());
			}

			delete[] Data;
			Data = nullptr;
			return S_OK;
		}
		HRESULT __stdcall UpdateOverlay(LPRECT lpSrcRect, LPDIRECTDRAWSURFACE7 lpDDDestSurface, LPRECT lpDestRect, DWORD dwFlags, LPDDOVERLAYFX lpDDOverlayFx) {return S_OK;} // Not used
		HRESULT __stdcall UpdateOverlayDisplay(DWORD dwFlags) {return S_OK;} // Not used
		HRESULT __stdcall UpdateOverlayZOrder(DWORD dwFlags, LPDIRECTDRAWSURFACE7 lpDDSReference) {return S_OK;} // Not used
		HRESULT __stdcall GetDDInterface(LPVOID* lplpDD) {return S_OK;} // Not used
		HRESULT __stdcall PageLock(DWORD dwFlags) {return S_OK;} // Not used
		HRESULT __stdcall PageUnlock(DWORD dwFlags) {return S_OK;} // Not used
		HRESULT __stdcall SetSurfaceDesc(LPDDSURFACEDESC2 lpDDSurfaceDesc, DWORD dwFlags) {return S_OK;} // Not used
		HRESULT __stdcall SetPrivateData(REFGUID guidTag, LPVOID lpData, DWORD cbSize, DWORD dwFlags) {return S_OK;} // Not used
		HRESULT __stdcall GetPrivateData(REFGUID guidTag, LPVOID lpBuffer, LPDWORD lpcbBufferSize) {return S_OK;} // Not used
		HRESULT __stdcall FreePrivateData(REFGUID guidTag) {return S_OK;} // Not used
		HRESULT __stdcall GetUniquenessValue(LPDWORD lpValue) {return S_OK;} // Not used
		HRESULT __stdcall ChangeUniquenessValue() {return S_OK;} // Not used
		HRESULT __stdcall SetPriority(DWORD dwPriority) {return S_OK;} // Not used
		HRESULT __stdcall GetPriority(LPDWORD dwPriority) {return S_OK;} // Not used
		HRESULT __stdcall SetLOD(DWORD dwLOD) {return S_OK;} // Not used
		HRESULT __stdcall GetLOD(LPDWORD dwLOD) {return S_OK;} // Not used

	private:
		std::vector<IDirectDrawSurface7*> AttachedSurfaces;
		unsigned char* Data;
		MyDirectDrawSurface7* BaseTexture;
		DDSURFACEDESC2 OriginalDesc;
		int RefCount;
		int MipLevel;
};

#define VB_SOFT_LOCK (1 << 0)
#define VB_DISCARD_LOCK (1 << 1)
class MyDirect3DVertexBuffer7 : public IDirect3DVertexBuffer7
{
	public:
		MyDirect3DVertexBuffer7(D3DVERTEXBUFFERDESC& originalDesc)
		{
			g_Direct3D9VertexBuffers.push_back(this);

			RefCount = 1;
			OriginalDesc = originalDesc;

			VertexBuffer = nullptr;
			VertexStride = ComputeFVFSize(OriginalDesc.dwFVF);
			VertexSize = OriginalDesc.dwNumVertices * VertexStride;
			VertexDirty |= VB_DISCARD_LOCK;
			VertexStatic = g_CreateStaticVertexBuffer;
			if(VertexStatic)
				VertexData = malloc(VertexSize);
		}

		~MyDirect3DVertexBuffer7()
		{
			auto it = std::find(g_Direct3D9VertexBuffers.begin(), g_Direct3D9VertexBuffers.end(), this);
			if(it != g_Direct3D9VertexBuffers.end())
			{
				*it = g_Direct3D9VertexBuffers.back();
				g_Direct3D9VertexBuffers.pop_back();
			}

			free(VertexData);
			if(VertexBuffer)
				IDirect3DVertexBuffer9_Release(VertexBuffer);
		}

		/*** IUnknown methods ***/
		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObj) {return S_OK;}
		ULONG STDMETHODCALLTYPE AddRef() {return ++RefCount;}
		ULONG STDMETHODCALLTYPE Release()
		{
			if(--RefCount == 0)
			{
				delete this;
				return 0;
			}
			return RefCount;
		}

		/*** IDirect3DVertexBuffer7 methods ***/
		HRESULT STDMETHODCALLTYPE GetVertexBufferDesc(LPD3DVERTEXBUFFERDESC lpVBDesc) {*lpVBDesc = OriginalDesc; return S_OK;}
		HRESULT STDMETHODCALLTYPE Lock(DWORD dwFlags, LPVOID* lplpData, LPDWORD lpdwSize)
		{
			if(lpdwSize)
				*lpdwSize = VertexSize;

			if(!VertexStatic)
			{
				DWORD vbFlags = D3DLOCK_NOSYSLOCK;
				if(!VertexBuffer || dwFlags & 0x00002000L) vbFlags |= D3DLOCK_DISCARD;
				else vbFlags |= D3DLOCK_NOOVERWRITE;

				if(!VertexBuffer)
					IDirect3DDevice9_CreateVertexBuffer(g_Direct3D9Device9, VertexSize, (D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY), OriginalDesc.dwFVF, D3DPOOL_DEFAULT, &VertexBuffer, nullptr);

				void* vertexBufferData;
				HRESULT result = IDirect3DVertexBuffer9_Lock(VertexBuffer, 0, 0, &vertexBufferData, vbFlags);
				if(SUCCEEDED(result))
					*lplpData = vertexBufferData;
				else
					return result;
			}
			else
			{
				*lplpData = VertexData;
				VertexDirty |= ((dwFlags & 0x00002000L) ? VB_DISCARD_LOCK : VB_SOFT_LOCK);
			}
			return S_OK;
		}
		HRESULT STDMETHODCALLTYPE Optimize(void* lpD3DDevice, DWORD dwFlags) {return S_OK;} // Used by Gothic but can't be emulated
		HRESULT STDMETHODCALLTYPE ProcessVertices(DWORD dwVertexOp, DWORD dwDestIndex, DWORD dwCount, void* lpSrcBuffer, DWORD dwSrcIndex, void* lpD3DDevice, DWORD dwFlags) {return S_OK;} // Not used
		HRESULT STDMETHODCALLTYPE ProcessVerticesStrided(DWORD dwVertexOp, DWORD dwDestIndex, DWORD dwCount, LPD3DDRAWPRIMITIVESTRIDEDDATA lpVertexArray, DWORD dwSrcIndex, void* lpD3DDevice, DWORD dwFlags) {return S_OK;} // Not used
		HRESULT STDMETHODCALLTYPE Unlock()
		{
			if(!VertexStatic && VertexBuffer)
				return IDirect3DVertexBuffer9_Unlock(VertexBuffer);
			return S_OK;
		}

		void ReleaseVertexBuffer()
		{
			if(VertexBuffer)
			{
				IDirect3DVertexBuffer9_Release(VertexBuffer);
				VertexBuffer = nullptr;
			}
		}
		IDirect3DVertexBuffer9* GetVertexBuffer()
		{
			if(!VertexBuffer)
			{
				IDirect3DDevice9_CreateVertexBuffer(g_Direct3D9Device9, VertexSize, (VertexStatic ? 0 : D3DUSAGE_DYNAMIC|D3DUSAGE_WRITEONLY), OriginalDesc.dwFVF, D3DPOOL_DEFAULT, &VertexBuffer, nullptr);
				VertexDirty |= VB_DISCARD_LOCK;
			}
			if(VertexStatic && VertexDirty != 0)
			{
				DWORD vbFlags = D3DLOCK_NOSYSLOCK;
				if(!VertexStatic)
				{
					if(VertexDirty & VB_DISCARD_LOCK) vbFlags |= D3DLOCK_DISCARD;
					else vbFlags |= D3DLOCK_NOOVERWRITE;
				}

				void* vertexBufferData;
				HRESULT result = IDirect3DVertexBuffer9_Lock(VertexBuffer, 0, 0, &vertexBufferData, vbFlags);
				if(SUCCEEDED(result))
					memcpy(vertexBufferData, VertexData, VertexSize);

				IDirect3DVertexBuffer9_Unlock(VertexBuffer);
				VertexDirty = 0;
			}
			return VertexBuffer;
		}
		DWORD GetFVF() {return OriginalDesc.dwFVF;}
		DWORD GetNumVertices() {return OriginalDesc.dwNumVertices;}
		DWORD GetStride() {return VertexStride;}

	private:
		IDirect3DVertexBuffer9* VertexBuffer;
		void* VertexData;
		D3DVERTEXBUFFERDESC OriginalDesc;
		DWORD VertexSize;
		DWORD VertexStride;
		DWORD VertexDirty;
		bool VertexStatic;
		int RefCount;
};

void ResetDevice()
{
	if(g_DefaultRenderTarget)
	{
		IDirect3DSurface9_Release(g_DefaultRenderTarget);
		g_DefaultRenderTarget = nullptr;
	}

	if(g_ManagedBoundTarget)
	{
		IDirect3DSurface9_Release(g_ManagedBoundTarget);
		g_ManagedBoundTarget = nullptr;
	}

	if(g_ManagedBackBuffer)
	{
		IDirect3DTexture9_Release(g_ManagedBackBuffer);
		g_ManagedBackBuffer = nullptr;
	}

	if(g_ManagedIndexBuffer)
	{
		IDirect3DIndexBuffer9_Release(g_ManagedIndexBuffer);
		g_ManagedIndexBuffer = nullptr;
	}

	for(MyDirectDrawSurface7* texture : g_Direct3D9Textures)
		texture->ReleaseTexture();

	for(MyDirect3DVertexBuffer7* vertexBuffer : g_Direct3D9VertexBuffers)
		vertexBuffer->ReleaseVertexBuffer();

	HRESULT result;
	do
	{
		result = IDirect3DDevice9_Reset(g_Direct3D9Device9, &g_Direct3D9_PParams);
		if(FAILED(result)) Sleep(2000);
	} while(FAILED(result));
	
	IDirect3DDevice9_GetRenderTarget(g_Direct3D9Device9, 0, &g_DefaultRenderTarget);
	if(IsG2)
	{
		// Release dynamic vertex buffer because it'll get created later
		DWORD dynamicVertexBuffer = *reinterpret_cast<DWORD*>(0x9FCA04);
		if(dynamicVertexBuffer)
		{
			reinterpret_cast<void(__thiscall*)(DWORD)>(0x5FC310)(dynamicVertexBuffer);
			reinterpret_cast<void(__cdecl*)(DWORD)>(0x565F60)(dynamicVertexBuffer);
		}

		// XD3D_InitPort
		WriteStack(0x6456B8, "\xE9\xCA\x00\x00\x00\x90");
		reinterpret_cast<void(__thiscall*)(DWORD, UINT, UINT, int, int, int)>(0x645150)
			(*reinterpret_cast<DWORD*>(0x982F08), g_Direct3D9_PParams.BackBufferWidth, g_Direct3D9_PParams.BackBufferHeight, 32, 0, 1);
		WriteStack(0x6456B8, "\x0F\x84\x81\x00\x00\x00");
	}
	else if(IsG1)
	{
		// Release dynamic vertex buffer because it'll get created later
		DWORD dynamicVertexBuffer = *reinterpret_cast<DWORD*>(0x929D6C);
		if(dynamicVertexBuffer)
		{
			reinterpret_cast<void(__thiscall*)(DWORD)>(0x5D11F0)(dynamicVertexBuffer);
			reinterpret_cast<void(__cdecl*)(DWORD)>(0x54EBB0)(dynamicVertexBuffer);
		}

		// XD3D_InitPort
		WriteStack(0x70F624, "\xE9\x09\x01\x00\x00\x90");
		reinterpret_cast<void(__thiscall*)(DWORD, UINT, UINT, int, int, int)>(0x70F250)
			(*reinterpret_cast<DWORD*>(0x8C5ED0), g_Direct3D9_PParams.BackBufferWidth, g_Direct3D9_PParams.BackBufferHeight, 32, 0, 1);
		WriteStack(0x70F624, "\x0F\x84\x9F\x00\x00\x00");
	}

	D3DMATRIX matrix;
	matrix._11 = 0.0f; matrix._12 = 0.0f; matrix._13 = 0.0f; matrix._14 = 0.0f;
	matrix._21 = 0.0f; matrix._22 = 1.0f; matrix._23 = 0.0f; matrix._24 = 0.0f;
	matrix._31 = 0.0f; matrix._32 = 0.0f; matrix._33 = 1.0f; matrix._34 = 1.0f;
	matrix._41 = 0.0f; matrix._42 = 0.0f; matrix._43 = -1.0f; matrix._44 = 0.0f;
	IDirect3DDevice9_SetTransform(g_Direct3D9Device9, D3DTS_PROJECTION, &matrix);
	if(g_MultiSampleAntiAliasing == D3DMULTISAMPLE_NONE)
	{
		result = IDirect3DDevice9_CreateTexture(g_Direct3D9Device9, g_Direct3D9_PParams.BackBufferWidth, g_Direct3D9_PParams.BackBufferHeight, 1,
			D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &g_ManagedBackBuffer, NULL);
		if(SUCCEEDED(result))
		{
			IDirect3DTexture9_GetSurfaceLevel(g_ManagedBackBuffer, 0, &g_ManagedBoundTarget);
			IDirect3DDevice9_SetRenderTarget(g_Direct3D9Device9, 0, g_ManagedBoundTarget);
		}
	}
	else
	{
		result = IDirect3DDevice9_CreateTexture(g_Direct3D9Device9, g_Direct3D9_PParams.BackBufferWidth, g_Direct3D9_PParams.BackBufferHeight, 1,
			D3DUSAGE_RENDERTARGET, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &g_ManagedBackBuffer, NULL);
		if(SUCCEEDED(result))
		{
			result = IDirect3DDevice9_CreateRenderTarget(g_Direct3D9Device9, g_Direct3D9_PParams.BackBufferWidth, g_Direct3D9_PParams.BackBufferHeight,
				D3DFMT_X8R8G8B8, g_MultiSampleAntiAliasing, 0, FALSE, &g_ManagedBoundTarget, NULL);
			if(SUCCEEDED(result))
				IDirect3DDevice9_SetRenderTarget(g_Direct3D9Device9, 0, g_ManagedBoundTarget);
		}
	}
}

class MyDirect3DDevice7 : public IDirect3DDevice7
{
	public:
		MyDirect3DDevice7()
		{
			RefCount = 1;

			ZeroMemory(&FakeDeviceDesc, sizeof(D3DDEVICEDESC7));
			FakeDeviceDesc.dwDevCaps = (0x00000001L|D3DDEVCAPS_EXECUTESYSTEMMEMORY|D3DDEVCAPS_TLVERTEXSYSTEMMEMORY|D3DDEVCAPS_TEXTUREVIDEOMEMORY|D3DDEVCAPS_DRAWPRIMTLVERTEX
				|D3DDEVCAPS_CANRENDERAFTERFLIP|D3DDEVCAPS_DRAWPRIMITIVES2|D3DDEVCAPS_DRAWPRIMITIVES2EX|D3DDEVCAPS_HWTRANSFORMANDLIGHT|D3DDEVCAPS_HWRASTERIZATION);
			FakeDeviceDesc.dpcLineCaps.dwSize = sizeof(D3DPRIMCAPS);
			FakeDeviceDesc.dpcLineCaps.dwMiscCaps = D3DPMISCCAPS_MASKZ;
			FakeDeviceDesc.dpcLineCaps.dwRasterCaps = (D3DPRASTERCAPS_DITHER|D3DPRASTERCAPS_ZTEST|0x00000020L|D3DPRASTERCAPS_FOGVERTEX|D3DPRASTERCAPS_FOGTABLE
				|D3DPRASTERCAPS_MIPMAPLODBIAS|0x00004000L|D3DPRASTERCAPS_ANISOTROPY|D3DPRASTERCAPS_WFOG|D3DPRASTERCAPS_ZFOG);
			FakeDeviceDesc.dpcLineCaps.dwZCmpCaps = (D3DPCMPCAPS_NEVER|D3DPCMPCAPS_LESS|D3DPCMPCAPS_EQUAL|D3DPCMPCAPS_LESSEQUAL|D3DPCMPCAPS_GREATER|D3DPCMPCAPS_NOTEQUAL
				|D3DPCMPCAPS_GREATEREQUAL|D3DPCMPCAPS_ALWAYS);
			FakeDeviceDesc.dpcLineCaps.dwSrcBlendCaps = (D3DPBLENDCAPS_ZERO|D3DPBLENDCAPS_ONE|D3DPBLENDCAPS_SRCCOLOR|D3DPBLENDCAPS_INVSRCCOLOR|D3DPBLENDCAPS_SRCALPHA
				|D3DPBLENDCAPS_INVSRCALPHA|D3DPBLENDCAPS_DESTALPHA|D3DPBLENDCAPS_INVDESTALPHA|D3DPBLENDCAPS_DESTCOLOR|D3DPBLENDCAPS_INVDESTCOLOR|D3DPBLENDCAPS_SRCALPHASAT
				|D3DPBLENDCAPS_BOTHSRCALPHA|D3DPBLENDCAPS_BOTHINVSRCALPHA);
			FakeDeviceDesc.dpcLineCaps.dwDestBlendCaps = (D3DPBLENDCAPS_ZERO|D3DPBLENDCAPS_ONE|D3DPBLENDCAPS_SRCCOLOR|D3DPBLENDCAPS_INVSRCCOLOR|D3DPBLENDCAPS_SRCALPHA
				|D3DPBLENDCAPS_INVSRCALPHA|D3DPBLENDCAPS_DESTALPHA|D3DPBLENDCAPS_INVDESTALPHA|D3DPBLENDCAPS_DESTCOLOR|D3DPBLENDCAPS_INVDESTCOLOR|D3DPBLENDCAPS_SRCALPHASAT);
			FakeDeviceDesc.dpcLineCaps.dwAlphaCmpCaps = (D3DPCMPCAPS_NEVER|D3DPCMPCAPS_LESS|D3DPCMPCAPS_EQUAL|D3DPCMPCAPS_LESSEQUAL|D3DPCMPCAPS_GREATER|D3DPCMPCAPS_NOTEQUAL
				|D3DPCMPCAPS_GREATEREQUAL|D3DPCMPCAPS_ALWAYS);
			FakeDeviceDesc.dpcLineCaps.dwShadeCaps = (0x00000002L|D3DPSHADECAPS_COLORGOURAUDRGB|0x00000080L|D3DPSHADECAPS_SPECULARGOURAUDRGB
				|0x00001000L|D3DPSHADECAPS_ALPHAGOURAUDBLEND|0x00040000L|D3DPSHADECAPS_FOGGOURAUD);
			FakeDeviceDesc.dpcLineCaps.dwTextureCaps = (D3DPTEXTURECAPS_PERSPECTIVE|D3DPTEXTURECAPS_ALPHA|0x00000008L|0x00000010L
				|D3DPTEXTURECAPS_TEXREPEATNOTSCALEDBYSIZE|D3DPTEXTURECAPS_CUBEMAP|0x00001000L);
			FakeDeviceDesc.dpcLineCaps.dwTextureFilterCaps = (0x00000001L|0x00000002L|0x00000004L|0x00000008L
				|0x00000010L|0x00000020L|D3DPTFILTERCAPS_MINFPOINT|D3DPTFILTERCAPS_MINFLINEAR|D3DPTFILTERCAPS_MINFANISOTROPIC
				|D3DPTFILTERCAPS_MIPFPOINT|D3DPTFILTERCAPS_MIPFLINEAR|D3DPTFILTERCAPS_MAGFPOINT|D3DPTFILTERCAPS_MAGFLINEAR|D3DPTFILTERCAPS_MAGFANISOTROPIC);
			FakeDeviceDesc.dpcLineCaps.dwTextureBlendCaps = (0x00000001L|0x00000002L|0x00000004L|0x00000008L|0x00000010L
				|0x00000020L|0x00000040L|0x00000080L);
			FakeDeviceDesc.dpcLineCaps.dwTextureAddressCaps = (D3DPTADDRESSCAPS_WRAP|D3DPTADDRESSCAPS_MIRROR|D3DPTADDRESSCAPS_CLAMP|D3DPTADDRESSCAPS_BORDER|D3DPTADDRESSCAPS_INDEPENDENTUV);
			FakeDeviceDesc.dpcTriCaps.dwSize = sizeof(D3DPRIMCAPS);
			FakeDeviceDesc.dpcTriCaps.dwMiscCaps = (D3DPMISCCAPS_MASKZ|D3DPMISCCAPS_CULLNONE|D3DPMISCCAPS_CULLCW|D3DPMISCCAPS_CULLCCW);
			FakeDeviceDesc.dpcTriCaps.dwRasterCaps = (D3DPRASTERCAPS_DITHER|D3DPRASTERCAPS_ZTEST|0x00000020L|D3DPRASTERCAPS_FOGVERTEX|D3DPRASTERCAPS_FOGTABLE
				|D3DPRASTERCAPS_MIPMAPLODBIAS|0x00004000L|D3DPRASTERCAPS_ANISOTROPY|D3DPRASTERCAPS_WFOG|D3DPRASTERCAPS_ZFOG);
			FakeDeviceDesc.dpcTriCaps.dwZCmpCaps = (D3DPCMPCAPS_NEVER|D3DPCMPCAPS_LESS|D3DPCMPCAPS_EQUAL|D3DPCMPCAPS_LESSEQUAL|D3DPCMPCAPS_GREATER|D3DPCMPCAPS_NOTEQUAL
				|D3DPCMPCAPS_GREATEREQUAL|D3DPCMPCAPS_ALWAYS);
			FakeDeviceDesc.dpcTriCaps.dwSrcBlendCaps = (D3DPBLENDCAPS_ZERO|D3DPBLENDCAPS_ONE|D3DPBLENDCAPS_SRCCOLOR|D3DPBLENDCAPS_INVSRCCOLOR|D3DPBLENDCAPS_SRCALPHA
				|D3DPBLENDCAPS_INVSRCALPHA|D3DPBLENDCAPS_DESTALPHA|D3DPBLENDCAPS_INVDESTALPHA|D3DPBLENDCAPS_DESTCOLOR|D3DPBLENDCAPS_INVDESTCOLOR|D3DPBLENDCAPS_SRCALPHASAT
				|D3DPBLENDCAPS_BOTHSRCALPHA|D3DPBLENDCAPS_BOTHINVSRCALPHA);
			FakeDeviceDesc.dpcTriCaps.dwDestBlendCaps = (D3DPBLENDCAPS_ZERO|D3DPBLENDCAPS_ONE|D3DPBLENDCAPS_SRCCOLOR|D3DPBLENDCAPS_INVSRCCOLOR|D3DPBLENDCAPS_SRCALPHA
				|D3DPBLENDCAPS_INVSRCALPHA|D3DPBLENDCAPS_DESTALPHA|D3DPBLENDCAPS_INVDESTALPHA|D3DPBLENDCAPS_DESTCOLOR|D3DPBLENDCAPS_INVDESTCOLOR|D3DPBLENDCAPS_SRCALPHASAT);
			FakeDeviceDesc.dpcTriCaps.dwAlphaCmpCaps = (D3DPCMPCAPS_NEVER|D3DPCMPCAPS_LESS|D3DPCMPCAPS_EQUAL|D3DPCMPCAPS_LESSEQUAL|D3DPCMPCAPS_GREATER|D3DPCMPCAPS_NOTEQUAL
				|D3DPCMPCAPS_GREATEREQUAL|D3DPCMPCAPS_ALWAYS);
			FakeDeviceDesc.dpcTriCaps.dwShadeCaps = (0x00000002L|D3DPSHADECAPS_COLORGOURAUDRGB|0x00000080L|D3DPSHADECAPS_SPECULARGOURAUDRGB
				|0x00001000L|D3DPSHADECAPS_ALPHAGOURAUDBLEND|0x00040000L|D3DPSHADECAPS_FOGGOURAUD);
			FakeDeviceDesc.dpcTriCaps.dwTextureCaps = (D3DPTEXTURECAPS_PERSPECTIVE|D3DPTEXTURECAPS_ALPHA|0x00000008L|0x00000010L
				|D3DPTEXTURECAPS_TEXREPEATNOTSCALEDBYSIZE|D3DPTEXTURECAPS_CUBEMAP|0x00001000L);
			FakeDeviceDesc.dpcTriCaps.dwTextureFilterCaps = (0x00000001L|0x00000002L|0x00000004L|0x00000008L
				|0x00000010L|0x00000020L|D3DPTFILTERCAPS_MINFPOINT|D3DPTFILTERCAPS_MINFLINEAR|D3DPTFILTERCAPS_MINFANISOTROPIC
				|D3DPTFILTERCAPS_MIPFPOINT|D3DPTFILTERCAPS_MIPFLINEAR|D3DPTFILTERCAPS_MAGFPOINT|D3DPTFILTERCAPS_MAGFLINEAR|D3DPTFILTERCAPS_MAGFANISOTROPIC);
			FakeDeviceDesc.dpcTriCaps.dwTextureBlendCaps = (0x00000001L|0x00000002L|0x00000004L|0x00000008L|0x00000010L
				|0x00000020L|0x00000040L|0x00000080L);
			FakeDeviceDesc.dpcTriCaps.dwTextureAddressCaps = (D3DPTADDRESSCAPS_WRAP|D3DPTADDRESSCAPS_MIRROR|D3DPTADDRESSCAPS_CLAMP|D3DPTADDRESSCAPS_BORDER|D3DPTADDRESSCAPS_INDEPENDENTUV);
			FakeDeviceDesc.dwDeviceRenderBitDepth = 1280;
			FakeDeviceDesc.dwDeviceZBufferBitDepth = 1536;
			FakeDeviceDesc.dwMinTextureWidth = 1;
			FakeDeviceDesc.dwMinTextureHeight = 1;
			FakeDeviceDesc.dwMaxTextureWidth = 16384;
			FakeDeviceDesc.dwMaxTextureHeight = 16384;
			FakeDeviceDesc.dwMaxTextureRepeat = 32768;
			FakeDeviceDesc.dwMaxTextureAspectRatio = 32768;
			FakeDeviceDesc.dwMaxAnisotropy = 16;
			FakeDeviceDesc.dvGuardBandLeft = -16384.0f;
			FakeDeviceDesc.dvGuardBandTop = -16384.0f;
			FakeDeviceDesc.dvGuardBandRight = 16384.0f;
			FakeDeviceDesc.dvGuardBandBottom = 16384.0f;
			FakeDeviceDesc.dvExtentsAdjust = 0.0f;
			FakeDeviceDesc.dwStencilCaps = (D3DSTENCILCAPS_KEEP|D3DSTENCILCAPS_ZERO|D3DSTENCILCAPS_REPLACE|D3DSTENCILCAPS_INCRSAT|D3DSTENCILCAPS_DECRSAT|D3DSTENCILCAPS_INVERT
				|D3DSTENCILCAPS_INCR|D3DSTENCILCAPS_DECR);
			FakeDeviceDesc.dwFVFCaps = (D3DFVFCAPS_DONOTSTRIPELEMENTS|8);
			FakeDeviceDesc.dwTextureOpCaps = (D3DTEXOPCAPS_DISABLE|D3DTEXOPCAPS_SELECTARG1|D3DTEXOPCAPS_SELECTARG2|D3DTEXOPCAPS_MODULATE|D3DTEXOPCAPS_MODULATE2X|D3DTEXOPCAPS_MODULATE4X
				|D3DTEXOPCAPS_ADD|D3DTEXOPCAPS_ADDSIGNED|D3DTEXOPCAPS_ADDSIGNED2X|D3DTEXOPCAPS_SUBTRACT|D3DTEXOPCAPS_ADDSMOOTH|D3DTEXOPCAPS_BLENDDIFFUSEALPHA|D3DTEXOPCAPS_BLENDTEXTUREALPHA
				|D3DTEXOPCAPS_BLENDFACTORALPHA|D3DTEXOPCAPS_BLENDTEXTUREALPHAPM|D3DTEXOPCAPS_BLENDCURRENTALPHA|D3DTEXOPCAPS_PREMODULATE|D3DTEXOPCAPS_MODULATEALPHA_ADDCOLOR
				|D3DTEXOPCAPS_MODULATECOLOR_ADDALPHA|D3DTEXOPCAPS_MODULATEINVALPHA_ADDCOLOR|D3DTEXOPCAPS_MODULATEINVCOLOR_ADDALPHA|D3DTEXOPCAPS_BUMPENVMAP|D3DTEXOPCAPS_BUMPENVMAPLUMINANCE
				|D3DTEXOPCAPS_DOTPRODUCT3);
			FakeDeviceDesc.wMaxTextureBlendStages = 4;
			FakeDeviceDesc.wMaxSimultaneousTextures = 4;
			FakeDeviceDesc.dwMaxActiveLights = 8;
			FakeDeviceDesc.dvMaxVertexW = 10000000000.0f;
			FakeDeviceDesc.deviceGUID = {0xF5049E78, 0x4861, 0x11D2, {0xA4, 0x07, 0x00, 0xA0, 0xC9, 0x06, 0x29, 0xA8}};
			FakeDeviceDesc.wMaxUserClipPlanes = 6;
			FakeDeviceDesc.wMaxVertexBlendMatrices = 4;
			FakeDeviceDesc.dwVertexProcessingCaps = (D3DVTXPCAPS_TEXGEN|D3DVTXPCAPS_MATERIALSOURCE7|D3DVTXPCAPS_DIRECTIONALLIGHTS|D3DVTXPCAPS_POSITIONALLIGHTS|D3DVTXPCAPS_LOCALVIEWER);
		}

		/*** IUnknown methods ***/
		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObj) {return S_OK;}
		ULONG STDMETHODCALLTYPE AddRef() {return ++RefCount;}
		ULONG STDMETHODCALLTYPE Release()
		{
			if(--RefCount == 0)
			{
				delete this;
				return 0;
			}
			return RefCount;
		}

		/*** IDirect3DDevice7 methods ***/
		HRESULT STDMETHODCALLTYPE GetCaps(LPD3DDEVICEDESC7 lpD3DDevDesc) {*lpD3DDevDesc = FakeDeviceDesc; return S_OK;}
		HRESULT STDMETHODCALLTYPE GetClipPlane(DWORD Index, float* pPlane) {return S_OK;} // Not Used
		HRESULT STDMETHODCALLTYPE SetClipPlane(DWORD dwIndex, float* pPlaneEquation) {return S_OK;} // Not Used
		HRESULT STDMETHODCALLTYPE GetClipStatus(D3DCLIPSTATUS9* lpD3DClipStatus) {return S_OK;} // Not Used
		HRESULT STDMETHODCALLTYPE SetClipStatus(D3DCLIPSTATUS9* lpD3DClipStatus) {return S_OK;} // Not Used
		HRESULT STDMETHODCALLTYPE GetDirect3D(void** ppD3D) {*ppD3D = nullptr; return S_OK;} // Not Used
		HRESULT STDMETHODCALLTYPE GetInfo(DWORD dwDevInfoID, LPVOID pDevInfoStruct, DWORD dwSize)
		{
			if(pDevInfoStruct && dwSize > 0) ZeroMemory(&pDevInfoStruct, dwSize);
			return S_OK;
		}
		HRESULT STDMETHODCALLTYPE GetLight(DWORD dwLightIndex, D3DLIGHT9* lpLight) {return IDirect3DDevice9_GetLight(g_Direct3D9Device9, dwLightIndex, lpLight);}
		HRESULT STDMETHODCALLTYPE GetLightEnable(DWORD Index, BOOL* pEnable) {return IDirect3DDevice9Ex_GetLightEnable(g_Direct3D9Device9, Index, pEnable);}
		HRESULT STDMETHODCALLTYPE GetMaterial(D3DMATERIAL9* lpMaterial) {return IDirect3DDevice9_GetMaterial(g_Direct3D9Device9, lpMaterial);}
		HRESULT STDMETHODCALLTYPE SetMaterial(D3DMATERIAL9* lpMaterial) {return IDirect3DDevice9_SetMaterial(g_Direct3D9Device9, lpMaterial);}
		HRESULT STDMETHODCALLTYPE GetRenderState(D3DRENDERSTATETYPE State, DWORD* pValue)
		{
			HRESULT result = S_OK;
			switch(State)
			{
				case 7:
				case 8:
				case 9:
				case 14:
				case 15:
				case 16:
				case 19:
				case 20:
				case 22:
				case 23:
				case 24:
				case 25:
				case 26:
				case 27:
				case 28:
				case 29:
				case 34:
				case 35:
				case 36:
				case 37:
				case 38:
				case 48:
				case 52:
				case 53:
				case 54:
				case 55:
				case 56:
				case 57:
				case 58:
				case 59:
				case 60:
				case 128:
				case 129:
				case 130:
				case 131:
				case 132:
				case 133:
				case 134:
				case 135:
				case 136:
				case 137:
				case 139:
				case 140:
				case 141:
				case 142:
				case 143:
				case 145:
				case 146:
				case 147:
				case 148:
				case 151:
				case 152:
					result = IDirect3DDevice9_GetRenderState(g_Direct3D9Device9, State, pValue);
					break;
				case 2:
					*pValue = 0;
					break;
				case 4:
					*pValue = 1;
					break;
				case 47:
					*pValue = 0;
					break;
			}
			return result;
		}
		HRESULT STDMETHODCALLTYPE SetRenderState(D3DRENDERSTATETYPE State, DWORD Value)
		{
			HRESULT result = S_OK;
			switch(State)
			{
				case 7:
				case 8:
				case 9:
				case 14:
				case 15:
				case 16:
				case 19:
				case 20:
				case 22:
				case 23:
				case 24:
				case 25:
				case 26:
				case 27:
				case 28:
				case 29:
				case 34:
				case 35:
				case 36:
				case 37:
				case 38:
				case 48:
				case 52:
				case 53:
				case 54:
				case 55:
				case 56:
				case 57:
				case 58:
				case 59:
				case 60:
				case 128:
				case 129:
				case 130:
				case 131:
				case 132:
				case 133:
				case 134:
				case 135:
				case 136:
				case 137:
				case 139:
				case 140:
				case 141:
				case 142:
				case 143:
				case 145:
				case 146:
				case 147:
				case 148:
				case 151:
				case 152:
					result = IDirect3DDevice9_SetRenderState(g_Direct3D9Device9, State, Value);
					break;
			}
			return result;
		}
		HRESULT STDMETHODCALLTYPE GetRenderTarget(LPDIRECTDRAWSURFACE7* lplpRenderTarget) {*lplpRenderTarget = nullptr; return S_OK;} // Not Used
		HRESULT STDMETHODCALLTYPE SetRenderTarget(LPDIRECTDRAWSURFACE7 lpNewRenderTarget, DWORD dwFlags) {return S_OK;} // Not Used
		HRESULT STDMETHODCALLTYPE GetTexture(DWORD dwStage, LPDIRECTDRAWSURFACE7* lplpTexture) {*lplpTexture = nullptr; return S_OK;}
		HRESULT STDMETHODCALLTYPE SetTexture(DWORD dwStage, LPDIRECTDRAWSURFACE7 lplpTexture)
		{
			if(lplpTexture) return IDirect3DDevice9_SetTexture(g_Direct3D9Device9, dwStage, static_cast<MyDirectDrawSurface7*>(lplpTexture)->GetTexture());
			else return IDirect3DDevice9_SetTexture(g_Direct3D9Device9, dwStage, nullptr);
		}
		HRESULT STDMETHODCALLTYPE GetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD* pValue)
		{
			HRESULT result = S_OK;
			switch(Type)
			{
				case D3DTSS_COLOROP:
				case D3DTSS_COLORARG1:
				case D3DTSS_COLORARG2:
				case D3DTSS_ALPHAOP:
				case D3DTSS_ALPHAARG1:
				case D3DTSS_ALPHAARG2:
				case D3DTSS_BUMPENVMAT00:
				case D3DTSS_BUMPENVMAT01:
				case D3DTSS_BUMPENVMAT10:
				case D3DTSS_BUMPENVMAT11:
				case D3DTSS_TEXCOORDINDEX:
				case D3DTSS_BUMPENVLSCALE:
				case D3DTSS_BUMPENVLOFFSET:
				case D3DTSS_TEXTURETRANSFORMFLAGS:
					result = IDirect3DDevice9_GetTextureStageState(g_Direct3D9Device9, Stage, Type, pValue);
					break;
				case 12:
				case 13:
					result = IDirect3DDevice9_GetSamplerState(g_Direct3D9Device9, Stage, D3DSAMP_ADDRESSU, pValue);
					break;
				case 14:
					result = IDirect3DDevice9_GetSamplerState(g_Direct3D9Device9, Stage, D3DSAMP_ADDRESSV, pValue);
					break;
				case 15:
					result = IDirect3DDevice9_GetSamplerState(g_Direct3D9Device9, Stage, D3DSAMP_BORDERCOLOR, pValue);
					break;
				case 16:
					result = IDirect3DDevice9_GetSamplerState(g_Direct3D9Device9, Stage, D3DSAMP_MAGFILTER, pValue);
					*pValue = (*pValue == D3DTEXF_ANISOTROPIC ? 5 : *pValue == D3DTEXF_LINEAR ? 2 : 1);
					break;
				case 17:
					result = IDirect3DDevice9_GetSamplerState(g_Direct3D9Device9, Stage, D3DSAMP_MINFILTER, pValue);
					*pValue = (*pValue == D3DTEXF_ANISOTROPIC ? 3 : *pValue == D3DTEXF_LINEAR ? 2 : 1);
					break;
				case 18:
					result = IDirect3DDevice9_GetSamplerState(g_Direct3D9Device9, Stage, D3DSAMP_MIPFILTER, pValue);
					*pValue = (*pValue == D3DTEXF_LINEAR ? 3 : *pValue == D3DTEXF_POINT ? 2 : 1);
					break;
				case 19:
					result = IDirect3DDevice9_GetSamplerState(g_Direct3D9Device9, Stage, D3DSAMP_MIPMAPLODBIAS, pValue);
					break;
				case 20:
					result = IDirect3DDevice9_GetSamplerState(g_Direct3D9Device9, Stage, D3DSAMP_MAXMIPLEVEL, pValue);
					break;
				case 21:
					result = IDirect3DDevice9_GetSamplerState(g_Direct3D9Device9, Stage, D3DSAMP_MAXANISOTROPY, pValue);
					break;
			}
			return result;
		}
		HRESULT STDMETHODCALLTYPE SetTextureStageState(DWORD Stage, D3DTEXTURESTAGESTATETYPE Type, DWORD Value)
		{
			HRESULT result = S_OK;
			switch(Type)
			{
				case D3DTSS_COLOROP:
				case D3DTSS_COLORARG1:
				case D3DTSS_COLORARG2:
				case D3DTSS_ALPHAOP:
				case D3DTSS_ALPHAARG1:
				case D3DTSS_ALPHAARG2:
				case D3DTSS_BUMPENVMAT00:
				case D3DTSS_BUMPENVMAT01:
				case D3DTSS_BUMPENVMAT10:
				case D3DTSS_BUMPENVMAT11:
				case D3DTSS_TEXCOORDINDEX:
				case D3DTSS_BUMPENVLSCALE:
				case D3DTSS_BUMPENVLOFFSET:
				case D3DTSS_TEXTURETRANSFORMFLAGS:
					result = IDirect3DDevice9_SetTextureStageState(g_Direct3D9Device9, Stage, Type, Value);
					break;
				case 12:
					IDirect3DDevice9_SetSamplerState(g_Direct3D9Device9, Stage, D3DSAMP_ADDRESSU, Value);
					result = IDirect3DDevice9_SetSamplerState(g_Direct3D9Device9, Stage, D3DSAMP_ADDRESSV, Value);
					break;
				case 13:
					result = IDirect3DDevice9_SetSamplerState(g_Direct3D9Device9, Stage, D3DSAMP_ADDRESSU, Value);
					break;
				case 14:
					result = IDirect3DDevice9_SetSamplerState(g_Direct3D9Device9, Stage, D3DSAMP_ADDRESSV, Value);
					break;
				case 15:
					result = IDirect3DDevice9_SetSamplerState(g_Direct3D9Device9, Stage, D3DSAMP_BORDERCOLOR, Value);
					break;
				case 16:
					result = IDirect3DDevice9_SetSamplerState(g_Direct3D9Device9, Stage, D3DSAMP_MAGFILTER, (Value == 5 ? D3DTEXF_ANISOTROPIC : Value == 2 ? D3DTEXF_LINEAR : D3DTEXF_POINT));
					break;
				case 17:
					result = IDirect3DDevice9_SetSamplerState(g_Direct3D9Device9, Stage, D3DSAMP_MINFILTER, (Value == 3 ? D3DTEXF_ANISOTROPIC : Value == 2 ? D3DTEXF_LINEAR : D3DTEXF_POINT));
					break;
				case 18:
					result = IDirect3DDevice9_SetSamplerState(g_Direct3D9Device9, Stage, D3DSAMP_MIPFILTER, (Value == 3 ? D3DTEXF_LINEAR : Value == 2 ? D3DTEXF_POINT : D3DTEXF_NONE));
					break;
				case 19:
					result = IDirect3DDevice9_SetSamplerState(g_Direct3D9Device9, Stage, D3DSAMP_MIPMAPLODBIAS, Value);
					break;
				case 20:
					result = IDirect3DDevice9_SetSamplerState(g_Direct3D9Device9, Stage, D3DSAMP_MAXMIPLEVEL, Value);
					break;
				case 21:
					result = IDirect3DDevice9_SetSamplerState(g_Direct3D9Device9, Stage, D3DSAMP_MAXANISOTROPY, Value);
					break;
			}
			return result;
		}
		HRESULT STDMETHODCALLTYPE GetTransform(D3DTRANSFORMSTATETYPE State, D3DMATRIX* pMatrix)
		{
			HRESULT result = S_OK;
			switch(State)
			{
				case 1:
					result = IDirect3DDevice9_GetTransform(g_Direct3D9Device9, D3DTS_WORLD, pMatrix);
					break;
				case 4:
					result = IDirect3DDevice9_GetTransform(g_Direct3D9Device9, D3DTS_WORLD1, pMatrix);
					break;
				case 5:
					result = IDirect3DDevice9_GetTransform(g_Direct3D9Device9, D3DTS_WORLD2, pMatrix);
					break;
				case 6:
					result = IDirect3DDevice9_GetTransform(g_Direct3D9Device9, D3DTS_WORLD3, pMatrix);
					break;
				case 2:
				case 3:
				case 16:
				case 17:
				case 18:
				case 19:
				case 20:
				case 21:
				case 22:
				case 23:
					result = IDirect3DDevice9_GetTransform(g_Direct3D9Device9, State, pMatrix);
					break;
			}
			return result;
		}
		HRESULT STDMETHODCALLTYPE SetTransform(D3DTRANSFORMSTATETYPE dtstTransformStateType, D3DMATRIX* lpD3DMatrix)
		{
			HRESULT result = S_OK;
			switch(dtstTransformStateType)
			{
				case 1:
					result = IDirect3DDevice9_SetTransform(g_Direct3D9Device9, D3DTS_WORLD, lpD3DMatrix);
					break;
				case 4:
					result = IDirect3DDevice9_SetTransform(g_Direct3D9Device9, D3DTS_WORLD1, lpD3DMatrix);
					break;
				case 5:
					result = IDirect3DDevice9_SetTransform(g_Direct3D9Device9, D3DTS_WORLD2, lpD3DMatrix);
					break;
				case 6:
					result = IDirect3DDevice9_SetTransform(g_Direct3D9Device9, D3DTS_WORLD3, lpD3DMatrix);
					break;
				case 2:
				case 3:
				case 16:
				case 17:
				case 18:
				case 19:
				case 20:
				case 21:
				case 22:
				case 23:
					result = IDirect3DDevice9_SetTransform(g_Direct3D9Device9, dtstTransformStateType, lpD3DMatrix);
					break;
			}
			return result;
		}
		HRESULT STDMETHODCALLTYPE GetViewport(D3DVIEWPORT9* lpViewport) {return IDirect3DDevice9_GetViewport(g_Direct3D9Device9, lpViewport);}
		HRESULT STDMETHODCALLTYPE SetViewport(D3DVIEWPORT9* lpViewport) {return IDirect3DDevice9_SetViewport(g_Direct3D9Device9, lpViewport);}
		HRESULT STDMETHODCALLTYPE ApplyStateBlock(DWORD dwBlockHandle) {return S_OK;} // Not Used
		HRESULT STDMETHODCALLTYPE BeginScene()
		{
			IDirect3DDevice9_BeginScene(g_Direct3D9Device9);
			if(g_ManagedBoundTarget)
				IDirect3DDevice9_SetRenderTarget(g_Direct3D9Device9, 0, g_ManagedBoundTarget);

			if(g_MultiSampleAntiAliasing != D3DMULTISAMPLE_NONE)
			{
				IDirect3DDevice9_SetRenderState(g_Direct3D9Device9, D3DRS_MULTISAMPLEANTIALIAS, TRUE);
				IDirect3DDevice9_SetRenderState(g_Direct3D9Device9, D3DRS_ANTIALIASEDLINEENABLE, TRUE);
			}
			return S_OK;
		}
		HRESULT STDMETHODCALLTYPE BeginStateBlock() {return S_OK;} // Not Used
		HRESULT STDMETHODCALLTYPE CaptureStateBlock(DWORD dwBlockHandle) {return S_OK;} // Not Used
		HRESULT STDMETHODCALLTYPE Clear(DWORD dwCount, D3DRECT* lpRects, DWORD dwFlags, D3DCOLOR dwColor, float dvZ, DWORD dwStencil) {return IDirect3DDevice9_Clear(g_Direct3D9Device9, dwCount, lpRects, dwFlags, dwColor, dvZ, dwStencil);}
		HRESULT STDMETHODCALLTYPE ComputeSphereVisibility(void* lpCenters, float* lpRadii, DWORD dwNumSpheres, DWORD dwFlags, LPDWORD lpdwReturnValues) {return S_OK;} // Not Used
		HRESULT STDMETHODCALLTYPE CreateStateBlock(D3DSTATEBLOCKTYPE d3dsbType, LPDWORD lpdwBlockHandle) {return S_OK;} // Not Used
		HRESULT STDMETHODCALLTYPE DeleteStateBlock(DWORD dwBlockHandle) {return S_OK;} // Not Used
		HRESULT STDMETHODCALLTYPE DrawIndexedPrimitive(D3DPRIMITIVETYPE dptPrimitiveType, DWORD dwVertexTypeDesc, LPVOID lpvVertices, DWORD dwVertexCount, LPWORD lpwIndices, DWORD dwIndexCount, DWORD dwFlags)
		{
			SetCurrentFVF(dwVertexTypeDesc);
			return IDirect3DDevice9_DrawIndexedPrimitiveUP(g_Direct3D9Device9, dptPrimitiveType, 0, dwVertexCount, CalculatePrimitiveCount(dptPrimitiveType, dwIndexCount), lpwIndices, D3DFMT_INDEX16, lpvVertices, Direct3DCurrentStride);
		}
		HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveStrided(D3DPRIMITIVETYPE dptPrimitiveType, DWORD dwVertexTypeDesc, LPD3DDRAWPRIMITIVESTRIDEDDATA lpVertexArray, DWORD dwVertexCount, LPWORD lpwIndices, DWORD dwIndexCount, DWORD dwFlags) {return S_OK;} // Not Used
		HRESULT STDMETHODCALLTYPE DrawIndexedPrimitiveVB(D3DPRIMITIVETYPE d3dptPrimitiveType, void* lpd3dVertexBuffer, DWORD dwStartVertex, DWORD dwNumVertices, LPWORD lpwIndices, DWORD dwIndexCount, DWORD dwFlags)
		{
			MyDirect3DVertexBuffer7* vertexBuffer = reinterpret_cast<MyDirect3DVertexBuffer7*>(lpd3dVertexBuffer);
			if(Direct3DCurrentFVF != vertexBuffer->GetFVF())
			{
				Direct3DCurrentFVF = vertexBuffer->GetFVF();
				IDirect3DDevice9_SetFVF(g_Direct3D9Device9, vertexBuffer->GetFVF());
				Direct3DCurrentStride = vertexBuffer->GetStride();
			}
			IDirect3DDevice9_SetStreamSource(g_Direct3D9Device9, 0, vertexBuffer->GetVertexBuffer(), 0, Direct3DCurrentStride);
			UINT startIndice = PushIndexBufferData(lpwIndices, dwIndexCount);
			return IDirect3DDevice9_DrawIndexedPrimitive(g_Direct3D9Device9, d3dptPrimitiveType, dwStartVertex, 0, dwNumVertices, startIndice, CalculatePrimitiveCount(d3dptPrimitiveType, dwIndexCount));
		}
		HRESULT STDMETHODCALLTYPE DrawPrimitive(D3DPRIMITIVETYPE dptPrimitiveType, DWORD dwVertexTypeDesc, LPVOID lpvVertices, DWORD dwVertexCount, DWORD dwFlags)
		{
			SetCurrentFVF(dwVertexTypeDesc);
			return IDirect3DDevice9_DrawPrimitiveUP(g_Direct3D9Device9, dptPrimitiveType, CalculatePrimitiveCount(dptPrimitiveType, dwVertexCount), lpvVertices, Direct3DCurrentStride);
		}
		HRESULT STDMETHODCALLTYPE DrawPrimitiveStrided(D3DPRIMITIVETYPE dptPrimitiveType, DWORD dwVertexTypeDesc, LPD3DDRAWPRIMITIVESTRIDEDDATA lpVertexArray, DWORD dwVertexCount, DWORD dwFlags) {return S_OK;} // Not Used
		HRESULT STDMETHODCALLTYPE DrawPrimitiveVB(D3DPRIMITIVETYPE d3dptPrimitiveType, void* lpd3dVertexBuffer, DWORD dwStartVertex, DWORD dwNumVertices, DWORD dwFlags)
		{
			MyDirect3DVertexBuffer7* vertexBuffer = reinterpret_cast<MyDirect3DVertexBuffer7*>(lpd3dVertexBuffer);
			if(Direct3DCurrentFVF != vertexBuffer->GetFVF())
			{
				Direct3DCurrentFVF = vertexBuffer->GetFVF();
				IDirect3DDevice9_SetFVF(g_Direct3D9Device9, Direct3DCurrentFVF);
				Direct3DCurrentStride = vertexBuffer->GetStride();
			}
			IDirect3DDevice9_SetStreamSource(g_Direct3D9Device9, 0, vertexBuffer->GetVertexBuffer(), 0, Direct3DCurrentStride);
			return IDirect3DDevice9_DrawPrimitive(g_Direct3D9Device9, d3dptPrimitiveType, dwStartVertex, CalculatePrimitiveCount(d3dptPrimitiveType, dwNumVertices));
		}
		HRESULT STDMETHODCALLTYPE EndScene()
		{
			if(g_ManagedBoundTarget)
			{
				IDirect3DDevice9_SetRenderTarget(g_Direct3D9Device9, 0, g_DefaultRenderTarget);
				if(g_MultiSampleAntiAliasing == D3DMULTISAMPLE_NONE)
				{
					IssueDrawingBackBuffer();
				}
				else
				{
					// Hack MSAA render target to texture so that we can apply gamme correction shader
					IDirect3DSurface9* tempSurface = nullptr;
					IDirect3DTexture9_GetSurfaceLevel(g_ManagedBackBuffer, 0, &tempSurface);
					IDirect3DDevice9_StretchRect(g_Direct3D9Device9, g_ManagedBoundTarget, nullptr, tempSurface, nullptr, D3DTEXF_NONE);
					IssueDrawingBackBuffer();
					IDirect3DSurface9_Release(tempSurface);
				}
			}
			IDirect3DDevice9_EndScene(g_Direct3D9Device9);
			HRESULT result = IDirect3DDevice9_TestCooperativeLevel(g_Direct3D9Device9);
			if(result == D3DERR_DEVICELOST)
			{
				//Reset later
				return S_OK;
			}
			else if(result == D3DERR_DEVICENOTRESET)
			{
				ResetDevice();
				return S_OK;
			}
			{
				if(g_Direct3D9Device9Ex) IDirect3DDevice9Ex_PresentEx(g_Direct3D9Device9Ex, nullptr, nullptr, nullptr, nullptr, (g_UseVsync ? 0 : D3DPRESENT_FORCEIMMEDIATE));
				else IDirect3DDevice9_Present(g_Direct3D9Device9, nullptr, nullptr, nullptr, nullptr);
			}
			return S_OK;
		}
		HRESULT STDMETHODCALLTYPE EndStateBlock(LPDWORD lpdwBlockHandle) {return S_OK;} // Not Used
		HRESULT STDMETHODCALLTYPE EnumTextureFormats(LPD3DENUMPIXELFORMATSCALLBACK lpd3dEnumPixelProc, LPVOID lpArg)
		{
			static std::array<DDPIXELFORMAT, 19> tformats =
			{ {
				{32, DDPF_ALPHA, 0, 8, 0x00, 0x00, 0x00, 0x00},
				{32, DDPF_LUMINANCE, 0, 8, 0xFF, 0x00, 0x00, 0x00},
				{32, DDPF_LUMINANCE|DDPF_ALPHAPIXELS, 0, 8, 0x0F, 0x00, 0x00, 0xF0},
				{32, DDPF_RGB, 0, 16, 0xF800, 0x7E0, 0x1F, 0x00},
				{32, DDPF_RGB|DDPF_ALPHAPIXELS, 0, 16, 0x7C00, 0x3E0, 0x1F, 0x8000},
				{32, DDPF_RGB|DDPF_ALPHAPIXELS, 0, 16, 0xF00, 0xF0, 0x0F, 0xF000},
				{32, DDPF_LUMINANCE|DDPF_ALPHAPIXELS, 0, 16, 0xFF, 0x00, 0x00, 0xFF00},
				{32, DDPF_BUMPDUDV, 0, 16, 0xFF, 0xFF00, 0x00, 0x00},
				{32, DDPF_BUMPDUDV|DDPF_BUMPLUMINANCE, 0, 16, 0x1F, 0x3E0, 0xFC00, 0x00},
				{32, DDPF_RGB, 0, 32, 0xFF0000, 0xFF00, 0xFF, 0x00},
				{32, DDPF_RGB|DDPF_ALPHAPIXELS, 0, 32, 0xFF0000, 0xFF00, 0xFF, 0xFF000000},
				{32, DDPF_FOURCC, MAKEFOURCC( 'Y','U','Y','2' ), 0, 0x00, 0x00, 0x00, 0x00},
				{32, DDPF_FOURCC, MAKEFOURCC( 'U','Y','V','Y' ), 0, 0x00, 0x00, 0x00, 0x00},
				{32, DDPF_FOURCC, MAKEFOURCC( 'A','Y','U','V' ), 0, 0x00, 0x00, 0x00, 0x00},
				{32, DDPF_FOURCC, FOURCC_DXT1, 0, 0x00, 0x00, 0x00, 0x00},
				{32, DDPF_FOURCC, FOURCC_DXT2, 0, 0x00, 0x00, 0x00, 0x00},
				{32, DDPF_FOURCC, FOURCC_DXT3, 0, 0x00, 0x00, 0x00, 0x00},
				{32, DDPF_FOURCC, FOURCC_DXT4, 0, 0x00, 0x00, 0x00, 0x00},
				{32, DDPF_FOURCC, FOURCC_DXT5, 0, 0x00, 0x00, 0x00, 0x00},
			} };

			for(DDPIXELFORMAT& ppf : tformats)
				(*lpd3dEnumPixelProc)(&ppf, lpArg);

			return S_OK;
		}
		HRESULT STDMETHODCALLTYPE Load(LPDIRECTDRAWSURFACE7 lpDestTex, LPPOINT lpDestPoint, LPDIRECTDRAWSURFACE7 lpSrcTex, LPRECT lprcSrcRect, DWORD dwFlags) {return S_OK;} // Not Used
		HRESULT STDMETHODCALLTYPE MultiplyTransform(D3DTRANSFORMSTATETYPE dtstTransformStateType, D3DMATRIX* lpD3DMatrix) {return S_OK;} // Not Used
		HRESULT STDMETHODCALLTYPE PreLoad(LPDIRECTDRAWSURFACE7 lpddsTexture) {return S_OK;} // Used but not needed
		HRESULT STDMETHODCALLTYPE ValidateDevice(DWORD* pNumPasses) {return IDirect3DDevice9_ValidateDevice(g_Direct3D9Device9, pNumPasses);}
		HRESULT STDMETHODCALLTYPE LightEnable(DWORD Index, BOOL Enable) {return IDirect3DDevice9_LightEnable(g_Direct3D9Device9, Index, Enable);}
		HRESULT STDMETHODCALLTYPE SetLight(DWORD dwLightIndex, D3DLIGHT9* lpLight) {return IDirect3DDevice9Ex_SetLight(g_Direct3D9Device9, dwLightIndex, lpLight);}

	private:
		D3DDEVICEDESC7 FakeDeviceDesc;
		int RefCount;
};

class MyDirect3D7 : public IDirect3D7
{
	public:
		MyDirect3D7() {RefCount = 1;}

		/*** IUnknown methods ***/
		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObj) {return S_OK;}
		ULONG STDMETHODCALLTYPE AddRef() {return ++RefCount;}
		ULONG STDMETHODCALLTYPE Release()
		{
			if(--RefCount == 0)
			{
				delete this;
				return 0;
			}
			return RefCount;
		}

		/*** IDirect3D7 methods ***/
		HRESULT STDMETHODCALLTYPE CreateDevice(REFCLSID rclsid, LPDIRECTDRAWSURFACE7 lpDDS, void** lplpD3DDevice)
		{
			*lplpD3DDevice = new MyDirect3DDevice7();
			return S_OK;
		}
		HRESULT STDMETHODCALLTYPE CreateVertexBuffer(LPD3DVERTEXBUFFERDESC lpVBDesc, void** lplpD3DVertexBuffer, DWORD dwFlags)
		{
			*lplpD3DVertexBuffer = new MyDirect3DVertexBuffer7(*lpVBDesc);
			return S_OK;
		}
		HRESULT STDMETHODCALLTYPE EnumDevices(LPD3DENUMDEVICESCALLBACK7 lpEnumDevicesCallback, LPVOID lpUserArg)
		{
			D3DDEVICEDESC7 devDesc;
			ZeroMemory(&devDesc, sizeof(D3DDEVICEDESC7));
			devDesc.dwDevCaps = (0x00000001L|D3DDEVCAPS_EXECUTESYSTEMMEMORY|D3DDEVCAPS_TLVERTEXSYSTEMMEMORY|D3DDEVCAPS_TEXTUREVIDEOMEMORY|D3DDEVCAPS_DRAWPRIMTLVERTEX
				|D3DDEVCAPS_CANRENDERAFTERFLIP|D3DDEVCAPS_DRAWPRIMITIVES2|D3DDEVCAPS_DRAWPRIMITIVES2EX|D3DDEVCAPS_HWTRANSFORMANDLIGHT|D3DDEVCAPS_HWRASTERIZATION);
			devDesc.dpcLineCaps.dwSize = sizeof(D3DPRIMCAPS);
			devDesc.dpcLineCaps.dwMiscCaps = D3DPMISCCAPS_MASKZ;
			devDesc.dpcLineCaps.dwRasterCaps = (D3DPRASTERCAPS_DITHER|D3DPRASTERCAPS_ZTEST|0x00000020L|D3DPRASTERCAPS_FOGVERTEX|D3DPRASTERCAPS_FOGTABLE
				|D3DPRASTERCAPS_MIPMAPLODBIAS|0x00004000L|D3DPRASTERCAPS_ANISOTROPY|D3DPRASTERCAPS_WFOG|D3DPRASTERCAPS_ZFOG);
			devDesc.dpcLineCaps.dwZCmpCaps = (D3DPCMPCAPS_NEVER|D3DPCMPCAPS_LESS|D3DPCMPCAPS_EQUAL|D3DPCMPCAPS_LESSEQUAL|D3DPCMPCAPS_GREATER|D3DPCMPCAPS_NOTEQUAL
				|D3DPCMPCAPS_GREATEREQUAL|D3DPCMPCAPS_ALWAYS);
			devDesc.dpcLineCaps.dwSrcBlendCaps = (D3DPBLENDCAPS_ZERO|D3DPBLENDCAPS_ONE|D3DPBLENDCAPS_SRCCOLOR|D3DPBLENDCAPS_INVSRCCOLOR|D3DPBLENDCAPS_SRCALPHA
				|D3DPBLENDCAPS_INVSRCALPHA|D3DPBLENDCAPS_DESTALPHA|D3DPBLENDCAPS_INVDESTALPHA|D3DPBLENDCAPS_DESTCOLOR|D3DPBLENDCAPS_INVDESTCOLOR|D3DPBLENDCAPS_SRCALPHASAT
				|D3DPBLENDCAPS_BOTHSRCALPHA|D3DPBLENDCAPS_BOTHINVSRCALPHA);
			devDesc.dpcLineCaps.dwDestBlendCaps = (D3DPBLENDCAPS_ZERO|D3DPBLENDCAPS_ONE|D3DPBLENDCAPS_SRCCOLOR|D3DPBLENDCAPS_INVSRCCOLOR|D3DPBLENDCAPS_SRCALPHA
				|D3DPBLENDCAPS_INVSRCALPHA|D3DPBLENDCAPS_DESTALPHA|D3DPBLENDCAPS_INVDESTALPHA|D3DPBLENDCAPS_DESTCOLOR|D3DPBLENDCAPS_INVDESTCOLOR|D3DPBLENDCAPS_SRCALPHASAT);
			devDesc.dpcLineCaps.dwAlphaCmpCaps = (D3DPCMPCAPS_NEVER|D3DPCMPCAPS_LESS|D3DPCMPCAPS_EQUAL|D3DPCMPCAPS_LESSEQUAL|D3DPCMPCAPS_GREATER|D3DPCMPCAPS_NOTEQUAL
				|D3DPCMPCAPS_GREATEREQUAL|D3DPCMPCAPS_ALWAYS);
			devDesc.dpcLineCaps.dwShadeCaps = (0x00000002L|D3DPSHADECAPS_COLORGOURAUDRGB|0x00000080L|D3DPSHADECAPS_SPECULARGOURAUDRGB
				|0x00001000L|D3DPSHADECAPS_ALPHAGOURAUDBLEND|0x00040000L|D3DPSHADECAPS_FOGGOURAUD);
			devDesc.dpcLineCaps.dwTextureCaps = (D3DPTEXTURECAPS_PERSPECTIVE|D3DPTEXTURECAPS_ALPHA|0x00000008L|0x00000010L
				|D3DPTEXTURECAPS_TEXREPEATNOTSCALEDBYSIZE|D3DPTEXTURECAPS_CUBEMAP|0x00001000L);
			devDesc.dpcLineCaps.dwTextureFilterCaps = (0x00000001L|0x00000002L|0x00000004L|0x00000008L
				|0x00000010L|0x00000020L|D3DPTFILTERCAPS_MINFPOINT|D3DPTFILTERCAPS_MINFLINEAR|D3DPTFILTERCAPS_MINFANISOTROPIC
				|D3DPTFILTERCAPS_MIPFPOINT|D3DPTFILTERCAPS_MIPFLINEAR|D3DPTFILTERCAPS_MAGFPOINT|D3DPTFILTERCAPS_MAGFLINEAR|D3DPTFILTERCAPS_MAGFANISOTROPIC);
			devDesc.dpcLineCaps.dwTextureBlendCaps = (0x00000001L|0x00000002L|0x00000004L|0x00000008L|0x00000010L
				|0x00000020L|0x00000040L|0x00000080L);
			devDesc.dpcLineCaps.dwTextureAddressCaps = (D3DPTADDRESSCAPS_WRAP|D3DPTADDRESSCAPS_MIRROR|D3DPTADDRESSCAPS_CLAMP|D3DPTADDRESSCAPS_BORDER|D3DPTADDRESSCAPS_INDEPENDENTUV);
			devDesc.dpcTriCaps.dwSize = sizeof(D3DPRIMCAPS);
			devDesc.dpcTriCaps.dwMiscCaps = (D3DPMISCCAPS_MASKZ|D3DPMISCCAPS_CULLNONE|D3DPMISCCAPS_CULLCW|D3DPMISCCAPS_CULLCCW);
			devDesc.dpcTriCaps.dwRasterCaps = (D3DPRASTERCAPS_DITHER|D3DPRASTERCAPS_ZTEST|0x00000020L|D3DPRASTERCAPS_FOGVERTEX|D3DPRASTERCAPS_FOGTABLE
				|D3DPRASTERCAPS_MIPMAPLODBIAS|0x00004000L|D3DPRASTERCAPS_ANISOTROPY|D3DPRASTERCAPS_WFOG|D3DPRASTERCAPS_ZFOG);
			devDesc.dpcTriCaps.dwZCmpCaps = (D3DPCMPCAPS_NEVER|D3DPCMPCAPS_LESS|D3DPCMPCAPS_EQUAL|D3DPCMPCAPS_LESSEQUAL|D3DPCMPCAPS_GREATER|D3DPCMPCAPS_NOTEQUAL
				|D3DPCMPCAPS_GREATEREQUAL|D3DPCMPCAPS_ALWAYS);
			devDesc.dpcTriCaps.dwSrcBlendCaps = (D3DPBLENDCAPS_ZERO|D3DPBLENDCAPS_ONE|D3DPBLENDCAPS_SRCCOLOR|D3DPBLENDCAPS_INVSRCCOLOR|D3DPBLENDCAPS_SRCALPHA
				|D3DPBLENDCAPS_INVSRCALPHA|D3DPBLENDCAPS_DESTALPHA|D3DPBLENDCAPS_INVDESTALPHA|D3DPBLENDCAPS_DESTCOLOR|D3DPBLENDCAPS_INVDESTCOLOR|D3DPBLENDCAPS_SRCALPHASAT
				|D3DPBLENDCAPS_BOTHSRCALPHA|D3DPBLENDCAPS_BOTHINVSRCALPHA);
			devDesc.dpcTriCaps.dwDestBlendCaps = (D3DPBLENDCAPS_ZERO|D3DPBLENDCAPS_ONE|D3DPBLENDCAPS_SRCCOLOR|D3DPBLENDCAPS_INVSRCCOLOR|D3DPBLENDCAPS_SRCALPHA
				|D3DPBLENDCAPS_INVSRCALPHA|D3DPBLENDCAPS_DESTALPHA|D3DPBLENDCAPS_INVDESTALPHA|D3DPBLENDCAPS_DESTCOLOR|D3DPBLENDCAPS_INVDESTCOLOR|D3DPBLENDCAPS_SRCALPHASAT);
			devDesc.dpcTriCaps.dwAlphaCmpCaps = (D3DPCMPCAPS_NEVER|D3DPCMPCAPS_LESS|D3DPCMPCAPS_EQUAL|D3DPCMPCAPS_LESSEQUAL|D3DPCMPCAPS_GREATER|D3DPCMPCAPS_NOTEQUAL
				|D3DPCMPCAPS_GREATEREQUAL|D3DPCMPCAPS_ALWAYS);
			devDesc.dpcTriCaps.dwShadeCaps = (0x00000002L|D3DPSHADECAPS_COLORGOURAUDRGB|0x00000080L|D3DPSHADECAPS_SPECULARGOURAUDRGB
				|0x00001000L|D3DPSHADECAPS_ALPHAGOURAUDBLEND|0x00040000L|D3DPSHADECAPS_FOGGOURAUD);
			devDesc.dpcTriCaps.dwTextureCaps = (D3DPTEXTURECAPS_PERSPECTIVE|D3DPTEXTURECAPS_ALPHA|0x00000008L|0x00000010L
				|D3DPTEXTURECAPS_TEXREPEATNOTSCALEDBYSIZE|D3DPTEXTURECAPS_CUBEMAP|0x00001000L);
			devDesc.dpcTriCaps.dwTextureFilterCaps = (0x00000001L|0x00000002L|0x00000004L|0x00000008L
				|0x00000010L|0x00000020L|D3DPTFILTERCAPS_MINFPOINT|D3DPTFILTERCAPS_MINFLINEAR|D3DPTFILTERCAPS_MINFANISOTROPIC
				|D3DPTFILTERCAPS_MIPFPOINT|D3DPTFILTERCAPS_MIPFLINEAR|D3DPTFILTERCAPS_MAGFPOINT|D3DPTFILTERCAPS_MAGFLINEAR|D3DPTFILTERCAPS_MAGFANISOTROPIC);
			devDesc.dpcTriCaps.dwTextureBlendCaps = (0x00000001L|0x00000002L|0x00000004L|0x00000008L|0x00000010L
				|0x00000020L|0x00000040L|0x00000080L);
			devDesc.dpcTriCaps.dwTextureAddressCaps = (D3DPTADDRESSCAPS_WRAP|D3DPTADDRESSCAPS_MIRROR|D3DPTADDRESSCAPS_CLAMP|D3DPTADDRESSCAPS_BORDER|D3DPTADDRESSCAPS_INDEPENDENTUV);
			devDesc.dwDeviceRenderBitDepth = 1280;
			devDesc.dwDeviceZBufferBitDepth = 1536;
			devDesc.dwMinTextureWidth = 1;
			devDesc.dwMinTextureHeight = 1;
			devDesc.dwMaxTextureWidth = 16384;
			devDesc.dwMaxTextureHeight = 16384;
			devDesc.dwMaxTextureRepeat = 32768;
			devDesc.dwMaxTextureAspectRatio = 32768;
			devDesc.dwMaxAnisotropy = 16;
			devDesc.dvGuardBandLeft = -16384.0f;
			devDesc.dvGuardBandTop = -16384.0f;
			devDesc.dvGuardBandRight = 16384.0f;
			devDesc.dvGuardBandBottom = 16384.0f;
			devDesc.dvExtentsAdjust = 0.0f;
			devDesc.dwStencilCaps = (D3DSTENCILCAPS_KEEP|D3DSTENCILCAPS_ZERO|D3DSTENCILCAPS_REPLACE|D3DSTENCILCAPS_INCRSAT|D3DSTENCILCAPS_DECRSAT|D3DSTENCILCAPS_INVERT
				|D3DSTENCILCAPS_INCR|D3DSTENCILCAPS_DECR);
			devDesc.dwFVFCaps = (D3DFVFCAPS_DONOTSTRIPELEMENTS|8);
			devDesc.dwTextureOpCaps = (D3DTEXOPCAPS_DISABLE|D3DTEXOPCAPS_SELECTARG1|D3DTEXOPCAPS_SELECTARG2|D3DTEXOPCAPS_MODULATE|D3DTEXOPCAPS_MODULATE2X|D3DTEXOPCAPS_MODULATE4X
				|D3DTEXOPCAPS_ADD|D3DTEXOPCAPS_ADDSIGNED|D3DTEXOPCAPS_ADDSIGNED2X|D3DTEXOPCAPS_SUBTRACT|D3DTEXOPCAPS_ADDSMOOTH|D3DTEXOPCAPS_BLENDDIFFUSEALPHA|D3DTEXOPCAPS_BLENDTEXTUREALPHA
				|D3DTEXOPCAPS_BLENDFACTORALPHA|D3DTEXOPCAPS_BLENDTEXTUREALPHAPM|D3DTEXOPCAPS_BLENDCURRENTALPHA|D3DTEXOPCAPS_PREMODULATE|D3DTEXOPCAPS_MODULATEALPHA_ADDCOLOR
				|D3DTEXOPCAPS_MODULATECOLOR_ADDALPHA|D3DTEXOPCAPS_MODULATEINVALPHA_ADDCOLOR|D3DTEXOPCAPS_MODULATEINVCOLOR_ADDALPHA|D3DTEXOPCAPS_BUMPENVMAP|D3DTEXOPCAPS_BUMPENVMAPLUMINANCE
				|D3DTEXOPCAPS_DOTPRODUCT3);
			devDesc.wMaxTextureBlendStages = 4;
			devDesc.wMaxSimultaneousTextures = 4;
			devDesc.dwMaxActiveLights = 8;
			devDesc.dvMaxVertexW = 10000000000.0f;
			devDesc.deviceGUID = {0xF5049E78, 0x4861, 0x11D2, {0xA4, 0x07, 0x00, 0xA0, 0xC9, 0x06, 0x29, 0xA8}};
			devDesc.wMaxUserClipPlanes = 6;
			devDesc.wMaxVertexBlendMatrices = 4;
			devDesc.dwVertexProcessingCaps = (D3DVTXPCAPS_TEXGEN|D3DVTXPCAPS_MATERIALSOURCE7|D3DVTXPCAPS_DIRECTIONALLIGHTS|D3DVTXPCAPS_POSITIONALLIGHTS|D3DVTXPCAPS_LOCALVIEWER);

			char name[256] = "DirectX9";
			(*lpEnumDevicesCallback)(GPUDeviceName, name, &devDesc, lpUserArg);
			return S_OK;
		}
		HRESULT STDMETHODCALLTYPE EvictManagedTextures()
		{
			if(g_Direct3D9Device9) return IDirect3DDevice9_EvictManagedResources(g_Direct3D9Device9);
			else return S_OK;
		}
		HRESULT STDMETHODCALLTYPE EnumZBufferFormats(REFCLSID riidDevice, LPD3DENUMPIXELFORMATSCALLBACK lpEnumCallback, LPVOID lpContext)
		{
			static std::array<DDPIXELFORMAT, 4> zformats =
			{ {
				{32, DDPF_ZBUFFER, 0, 16, 0x00, 0xFFFF, 0x00, 0x00},
				{32, DDPF_ZBUFFER, 0, 24, 0x00, 0xFFFFFF, 0x00, 0x00},
				{32, DDPF_ZBUFFER, 0, 32, 0x00, 0xFFFFFF, 0x00, 0x00},
				{32, DDPF_STENCILBUFFER|DDPF_ZBUFFER, 0, 32, 0x08, 0xFFFFFF, 0xFF000000, 0x00},
			} };

			for(DDPIXELFORMAT& ppf : zformats)
				(*lpEnumCallback)(&ppf, lpContext);

			return S_OK;
		}

	private:
		int RefCount;
};

class MyDirectDraw : public IDirectDraw7
{
	public:
		MyDirectDraw()
		{
			RefCount = 1;
			ZeroMemory(&DisplayMode, sizeof(DDSURFACEDESC2));

			// Gothic calls GetDisplayMode without Setting it first, so do it here
			SetDisplayMode(800, 600, 32, 60, 0);
		}

		/*** IUnknown methods ***/
		HRESULT STDMETHODCALLTYPE QueryInterface(REFIID riid, void** ppvObj)
		{
			if(riid == D3D7_IID_IDirect3D7)
				*ppvObj = new MyDirect3D7();

			return S_OK;
		}
		ULONG STDMETHODCALLTYPE AddRef() {return ++RefCount;}
		ULONG STDMETHODCALLTYPE Release()
		{
			if(--RefCount == 0)
			{
				delete this;
				return 0;
			}
			return RefCount;
		}

		/*** IDirectDraw7 methods ***/
		HRESULT STDMETHODCALLTYPE GetAvailableVidMem(LPDDSCAPS2 lpDDSCaps2, LPDWORD lpdwTotal, LPDWORD lpdwFree) {return S_OK;} // Used but not needed
		HRESULT STDMETHODCALLTYPE GetCaps(LPDDCAPS lpDDDriverCaps, LPDDCAPS lpDDHELCaps) {return S_OK;} // Used but not needed
		HRESULT STDMETHODCALLTYPE SetCooperativeLevel(HWND hWnd, DWORD dwFlags) {return S_OK;} // Used but not needed
		HRESULT STDMETHODCALLTYPE GetDeviceIdentifier(LPDDDEVICEIDENTIFIER2 lpdddi, DWORD dwFlags)
		{
			ZeroMemory(lpdddi, sizeof(DDDEVICEIDENTIFIER2));
			strcpy_s(lpdddi->szDescription, GPUDeviceName);
			strcpy_s(lpdddi->szDriver, "DirectX9");
			lpdddi->guidDeviceIdentifier = {0xF5049E78, 0x4861, 0x11D2, {0xA4, 0x07, 0x00, 0xA0, 0xC9, 0x06, 0x29, 0xA8}};
			return S_OK;
		}
		HRESULT STDMETHODCALLTYPE GetDisplayMode(LPDDSURFACEDESC2 lpDDSurfaceDesc2)
		{
			*lpDDSurfaceDesc2 = DisplayMode;
			return S_OK;
		}
		HRESULT STDMETHODCALLTYPE SetDisplayMode(DWORD dwWidth, DWORD dwHeight, DWORD dwBPP, DWORD dwRefreshRate, DWORD dwFlags)
		{
			DisplayMode.dwWidth = dwWidth;
			DisplayMode.dwHeight = dwHeight;
			DisplayMode.dwRefreshRate = dwRefreshRate;
			DisplayMode.dwFlags = dwFlags;

			DisplayMode.ddpfPixelFormat.dwRGBBitCount = dwBPP;
			DisplayMode.ddpfPixelFormat.dwPrivateFormatBitCount = dwBPP;
			DisplayMode.dwFlags |= (DDSD_HEIGHT|DDSD_WIDTH|DDSD_PIXELFORMAT);
			DisplayMode.dwSize = sizeof(DisplayMode);
			DisplayMode.ddpfPixelFormat.dwSize = sizeof(DisplayMode.ddpfPixelFormat);
			return S_OK;
		}
		HRESULT STDMETHODCALLTYPE GetFourCCCodes(LPDWORD lpNumCodes, LPDWORD lpCodes) {return S_OK;} // Not Used
		HRESULT STDMETHODCALLTYPE GetGDISurface(LPDIRECTDRAWSURFACE7* lplpGDIDDSSurface) {return S_OK;} // Not Used
		HRESULT STDMETHODCALLTYPE GetMonitorFrequency(LPDWORD lpdwFrequency) {return S_OK;} // Not Used
		HRESULT STDMETHODCALLTYPE GetScanLine(LPDWORD lpdwScanLine) {return S_OK;} // Not Used
		HRESULT STDMETHODCALLTYPE GetSurfaceFromDC(HDC hdc, LPDIRECTDRAWSURFACE7* lpDDS) {return S_OK;} // Not Used
		HRESULT STDMETHODCALLTYPE GetVerticalBlankStatus(LPBOOL lpbIsInVB) {return S_OK;} // Not Used
		HRESULT STDMETHODCALLTYPE Compact() {return S_OK;} // Not Used
		HRESULT STDMETHODCALLTYPE CreateClipper(DWORD dwFlags, LPDIRECTDRAWCLIPPER* lplpDDClipper, IUnknown* pUnkOuter) {*lplpDDClipper = new MyClipper(); return S_OK;}
		HRESULT STDMETHODCALLTYPE CreatePalette(DWORD dwFlags, LPPALETTEENTRY lpDDColorArray, LPDIRECTDRAWPALETTE* lplpDDPalette, IUnknown* pUnkOuter) {return S_OK;} // Used by Gothic but can't be emulated
		HRESULT STDMETHODCALLTYPE CreateSurface(LPDDSURFACEDESC2 lpDDSurfaceDesc, LPDIRECTDRAWSURFACE7* lplpDDSurface, IUnknown* pUnkOuter)
		{
			if(lpDDSurfaceDesc->ddsCaps.dwCaps & DDSCAPS_OFFSCREENPLAIN)
			{
				lpDDSurfaceDesc->ddpfPixelFormat.dwSize = sizeof(DDPIXELFORMAT);
				lpDDSurfaceDesc->ddpfPixelFormat.dwFlags = DDPF_RGB;
				lpDDSurfaceDesc->ddpfPixelFormat.dwRGBBitCount = 3 * 8;
				lpDDSurfaceDesc->ddpfPixelFormat.dwRBitMask = 0x00FF0000;
				lpDDSurfaceDesc->ddpfPixelFormat.dwGBitMask = 0x0000FF00;
				lpDDSurfaceDesc->ddpfPixelFormat.dwBBitMask = 0x000000FF;
			}

			if(lpDDSurfaceDesc->ddpfPixelFormat.dwRGBBitCount == 16)
			{
				if(lpDDSurfaceDesc->ddpfPixelFormat.dwRBitMask == 0x7C00
					&& lpDDSurfaceDesc->ddpfPixelFormat.dwGBitMask == 0x3E0
					&& lpDDSurfaceDesc->ddpfPixelFormat.dwBBitMask == 0x1F
					&& lpDDSurfaceDesc->ddpfPixelFormat.dwRGBAlphaBitMask == 0x8000)
					lpDDSurfaceDesc->ddpfPixelFormat.dwFourCC = 1;
				else if(lpDDSurfaceDesc->ddpfPixelFormat.dwRBitMask == 0xF00
					&& lpDDSurfaceDesc->ddpfPixelFormat.dwGBitMask == 0xF0
					&& lpDDSurfaceDesc->ddpfPixelFormat.dwBBitMask == 0x0F
					&& lpDDSurfaceDesc->ddpfPixelFormat.dwRGBAlphaBitMask == 0xF000)
					lpDDSurfaceDesc->ddpfPixelFormat.dwFourCC = 2;
				else
					lpDDSurfaceDesc->ddpfPixelFormat.dwFourCC = 0;
			}
			else if(lpDDSurfaceDesc->ddpfPixelFormat.dwRGBBitCount == 24)
			{
				if(lpDDSurfaceDesc->ddpfPixelFormat.dwRBitMask == 0xFF
					&& lpDDSurfaceDesc->ddpfPixelFormat.dwGBitMask == 0xFF00
					&& lpDDSurfaceDesc->ddpfPixelFormat.dwBBitMask == 0xFF0000
					&& lpDDSurfaceDesc->ddpfPixelFormat.dwRGBAlphaBitMask == 0x00)
					lpDDSurfaceDesc->ddpfPixelFormat.dwFourCC = 1;
				else
					lpDDSurfaceDesc->ddpfPixelFormat.dwFourCC = 0;
			}
			else if(lpDDSurfaceDesc->ddpfPixelFormat.dwRGBBitCount == 32)
			{
				if(lpDDSurfaceDesc->ddpfPixelFormat.dwRBitMask == 0xFF
					&& lpDDSurfaceDesc->ddpfPixelFormat.dwGBitMask == 0xFF00
					&& lpDDSurfaceDesc->ddpfPixelFormat.dwBBitMask == 0xFF0000
					&& lpDDSurfaceDesc->ddpfPixelFormat.dwRGBAlphaBitMask == 0xFF000000)
					lpDDSurfaceDesc->ddpfPixelFormat.dwFourCC = 1;
				else
					lpDDSurfaceDesc->ddpfPixelFormat.dwFourCC = 0;
			}

			MyDirectDrawSurface7* mySurface = new MyDirectDrawSurface7();
			if(lpDDSurfaceDesc->ddsCaps.dwCaps & DDSCAPS_MIPMAP)
			{
				DDSURFACEDESC2 desc = *lpDDSurfaceDesc;

				FakeDirectDrawSurface7* lastMip = nullptr;
				int level = 0;
				while(desc.dwMipMapCount > 1)
				{
					--desc.dwMipMapCount;
					desc.ddsCaps.dwCaps2 |= DDSCAPS2_MIPMAPSUBLEVEL;

					FakeDirectDrawSurface7* mip = new FakeDirectDrawSurface7(&desc, mySurface, ++level);
					if(!lastMip) mySurface->AddAttachedSurface(mip);
					else lastMip->AddAttachedSurface(mip);
					lastMip = mip;
				}
			}

			*lplpDDSurface = mySurface;
			mySurface->SetSurfaceDesc(lpDDSurfaceDesc, 0);
			return S_OK;
		}
		HRESULT STDMETHODCALLTYPE DuplicateSurface(LPDIRECTDRAWSURFACE7 lpDDSurface, LPDIRECTDRAWSURFACE7* lplpDupDDSurface) {return S_OK;} // Not Used
		HRESULT STDMETHODCALLTYPE EnumDisplayModes(DWORD dwFlags, LPDDSURFACEDESC2 lpDDSurfaceDesc2, LPVOID lpContext, LPDDENUMMODESCALLBACK2 lpEnumModesCallback)
		{
			for(DDSURFACEDESC2& mode : g_Direct3D9VideoModes)
				(*lpEnumModesCallback)(&mode, lpContext);

			return S_OK;
		}
		HRESULT STDMETHODCALLTYPE EnumSurfaces(DWORD dwFlags, LPDDSURFACEDESC2 lpDDSD2, LPVOID lpContext, LPDDENUMSURFACESCALLBACK7 lpEnumSurfacesCallback) {return S_OK;} // Not Used
		HRESULT STDMETHODCALLTYPE EvaluateMode(DWORD dwFlags, DWORD* pSecondsUntilTimeout) {return S_OK;} // Not Used
		HRESULT STDMETHODCALLTYPE FlipToGDISurface() {return S_OK;} // Used by Gothic but can't be emulated
		HRESULT STDMETHODCALLTYPE Initialize(GUID* lpGUID) {return S_OK;} // Not Used
		HRESULT STDMETHODCALLTYPE RestoreDisplayMode() {return S_OK;} // Not Used
		HRESULT STDMETHODCALLTYPE WaitForVerticalBlank(DWORD dwFlags, HANDLE hEvent) {return S_OK;} // Used by Gothic but can't be emulated
		HRESULT STDMETHODCALLTYPE RestoreAllSurfaces() {return S_OK;} // Not Used
		HRESULT STDMETHODCALLTYPE StartModeTest(LPSIZE lpModesToTest, DWORD dwNumEntries, DWORD dwFlags) {return S_OK;} // Not Used
		HRESULT STDMETHODCALLTYPE TestCooperativeLevel() {return S_OK;} // Not Used

	private:
		int RefCount;
		DDSURFACEDESC2 DisplayMode;
};

HRESULT WINAPI HookDirectDrawCreateEx_G1(GUID* lpGuid, LPVOID* lplpDD, REFIID iid, IUnknown* pUnkOuter)
{
    if(!g_Direct3D9)
    {
		if(Direct3DCreate9_12_Ptr)
		{
			typedef struct _D3D9ON12_ARGS
			{
				BOOL Enable9On12;
				IUnknown* pD3D12Device;
				IUnknown* ppD3D12Queues[2];
				UINT NumQueues;
				UINT NodeMask;
			} D3D9ON12_ARGS;

			D3D9ON12_ARGS args = {};
			args.Enable9On12 = TRUE;
            g_Direct3D9 = reinterpret_cast<IDirect3D9*(__stdcall*)(UINT, D3D9ON12_ARGS*, UINT)>(Direct3DCreate9_12_Ptr)(D3D_SDK_VERSION, &args, 1);
		}
        else if(Direct3DCreate9Ex_Ptr)
        {
            HRESULT result = reinterpret_cast<HRESULT(__stdcall*)(UINT, IDirect3D9Ex**)>(Direct3DCreate9Ex_Ptr)(D3D_SDK_VERSION, &g_Direct3D9Ex);
            if(SUCCEEDED(result))
				IDirect3D9Ex_QueryInterface(g_Direct3D9Ex, D3D9_IID_IDirect3D9, reinterpret_cast<void**>(&g_Direct3D9));
        }
        else if(Direct3DCreate9_Ptr)
            g_Direct3D9 = reinterpret_cast<IDirect3D9*(__stdcall*)(UINT)>(Direct3DCreate9_Ptr)(D3D_SDK_VERSION);

		if(g_Direct3D9)
		{
			HWND gothicHWND = *reinterpret_cast<HWND*>(0x86F4B8);
			HMONITOR useMonitor = MonitorFromWindow(gothicHWND, MONITOR_DEFAULTTOPRIMARY);
			UINT adapterIndex = 0;
			UINT adapters = IDirect3D9_GetAdapterCount(g_Direct3D9);
			for(UINT i = 0; i < adapters; ++i)
			{
				if(IDirect3D9_GetAdapterMonitor(g_Direct3D9, i) == useMonitor)
				{
					adapterIndex = i;
					break;
				}
			}

			UINT modes = IDirect3D9_GetAdapterModeCount(g_Direct3D9, adapterIndex, D3DFMT_X8R8G8B8);
			for(UINT i = 0; i < modes; ++i)
			{
				D3DDISPLAYMODE dmode;
				if(SUCCEEDED(IDirect3D9_EnumAdapterModes(g_Direct3D9, adapterIndex, D3DFMT_X8R8G8B8, i, &dmode)))
				{
					auto it = std::find_if(g_Direct3D9VideoModes.begin(), g_Direct3D9VideoModes.end(),
						[&dmode](DDSURFACEDESC2& a) {return (a.dwWidth == dmode.Width && a.dwHeight == dmode.Height); });
					if(it == g_Direct3D9VideoModes.end())
					{
						DDSURFACEDESC2 newMode = {};
						newMode.dwSize = sizeof(DDSURFACEDESC2);
						newMode.dwWidth = dmode.Width;
						newMode.dwHeight = dmode.Height;
						newMode.ddpfPixelFormat.dwRGBBitCount = 32;
						newMode.dwRefreshRate = 60;
						g_Direct3D9VideoModes.push_back(newMode);
					}
				}
			}
			if(g_Direct3D9VideoModes.empty())
			{
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

					auto it = std::find_if(g_Direct3D9VideoModes.begin(), g_Direct3D9VideoModes.end(),
						[&devmode](DDSURFACEDESC2& a) {return (a.dwWidth == devmode.dmPelsWidth && a.dwHeight == devmode.dmPelsHeight);});
					if(it == g_Direct3D9VideoModes.end())
					{
						DDSURFACEDESC2 newMode = {};
						newMode.dwSize = sizeof(DDSURFACEDESC2);
						newMode.dwWidth = devmode.dmPelsWidth;
						newMode.dwHeight = devmode.dmPelsHeight;
						newMode.ddpfPixelFormat.dwRGBBitCount = 32;
						newMode.dwRefreshRate = 60;
						g_Direct3D9VideoModes.push_back(newMode);
					}
				}
			}
			{
				auto it = std::find_if(g_Direct3D9VideoModes.begin(), g_Direct3D9VideoModes.end(),
					[](DDSURFACEDESC2& a) {return (a.dwWidth == 800 && a.dwHeight == 600); });
				if(it == g_Direct3D9VideoModes.end())
				{
					DDSURFACEDESC2 newMode = {};
					newMode.dwSize = sizeof(DDSURFACEDESC2);
					newMode.dwWidth = 800;
					newMode.dwHeight = 600;
					newMode.ddpfPixelFormat.dwRGBBitCount = 32;
					newMode.dwRefreshRate = 60;
					g_Direct3D9VideoModes.push_back(newMode);
				}
			}
			std::sort(g_Direct3D9VideoModes.begin(), g_Direct3D9VideoModes.end(), [](DDSURFACEDESC2& a, DDSURFACEDESC2& b) -> bool
			{
				return (a.dwWidth < b.dwWidth) || ((a.dwWidth == b.dwWidth) && (a.dwHeight < b.dwHeight));
			});

			UINT width = 800;
			UINT height = 600;
			UINT refreshRate = 60;
			{
				DEVMODEA devMode = {};
				devMode.dmSize = sizeof(DEVMODEA);

				MONITORINFOEXA miex;
				miex.cbSize = sizeof(MONITORINFOEXA);
				if(GetMonitorInfoA(useMonitor, &miex))
				{
					if(EnumDisplaySettingsA(miex.szDevice, ENUM_CURRENT_SETTINGS, &devMode))
					{
						width = devMode.dmPelsWidth;
						height = devMode.dmPelsHeight;
						refreshRate = devMode.dmDisplayFrequency;
					}
				}
				else
				{
					if(EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &devMode))
					{
						width = devMode.dmPelsWidth;
						height = devMode.dmPelsHeight;
						refreshRate = devMode.dmDisplayFrequency;
					}
				}

				DWORD options = *reinterpret_cast<DWORD*>(0x869694);
				int resWidth = reinterpret_cast<int(__thiscall*)(DWORD, DWORD, const char*, UINT)>(0x45CDB0)(options, 0x869248, "zVidResFullscreenX", width);
				int resHeight = reinterpret_cast<int(__thiscall*)(DWORD, DWORD, const char*, UINT)>(0x45CDB0)(options, 0x869248, "zVidResFullscreenY", height);
				fullscreenExclusivent = reinterpret_cast<int(__thiscall*)(DWORD, DWORD, const char*, int)>(0x45CDB0)(options, 0x869248, "zStartupWindowed", 1);
				if(fullscreenExclusivent)
				{
					WindowPositionX = *reinterpret_cast<int*>(*reinterpret_cast<DWORD*>(0x4F42DA));
					WindowPositionY = *reinterpret_cast<int*>(*reinterpret_cast<DWORD*>(0x4F42D3));
					if(WindowPositionX == static_cast<int>(0x80000000) || WindowPositionY == static_cast<int>(0x80000000))
					{
						WindowPositionX = 0;
						WindowPositionY = 0;
					}
				}

				bool foundMode = false;
				for(DDSURFACEDESC2& mode : g_Direct3D9VideoModes)
				{
					if(static_cast<int>(mode.dwWidth) == resWidth && static_cast<int>(mode.dwHeight) == resHeight)
					{
						foundMode = true;
						break;
					}
				}
				if(!foundMode)
				{
					resWidth = static_cast<int>(width);
					resHeight = static_cast<int>(height);

					reinterpret_cast<int(__thiscall*)(DWORD, DWORD, const char*, int, int)>(0x45C950)(options, 0x869248, "zVidResFullscreenX", resWidth, 0);
					reinterpret_cast<int(__thiscall*)(DWORD, DWORD, const char*, int, int)>(0x45C950)(options, 0x869248, "zVidResFullscreenY", resHeight, 0);
				}
				else
				{
					width = static_cast<UINT>(resWidth);
					height = static_cast<UINT>(resHeight);
				}
			}

			std::vector<D3DFORMAT> depthFormats;
			if(g_Direct3D9Ex) depthFormats.push_back(D3DFMT_D32_LOCKABLE);
			depthFormats.push_back(D3DFMT_D32F_LOCKABLE);
			depthFormats.push_back(D3DFMT_D32);
			depthFormats.push_back(D3DFMT_D24X8);
			depthFormats.push_back(D3DFMT_D24S8);
			depthFormats.push_back(D3DFMT_D24FS8);
			depthFormats.push_back(D3DFMT_D24S8);
			depthFormats.push_back(D3DFMT_D24X4S4);
			depthFormats.push_back(D3DFMT_D16_LOCKABLE);
			depthFormats.push_back(D3DFMT_D16);

			ZeroMemory(&g_Direct3D9_PParams, sizeof(D3DPRESENT_PARAMETERS));
			g_Direct3D9_PParams.hDeviceWindow = gothicHWND;
			g_Direct3D9_PParams.BackBufferWidth = width;
			g_Direct3D9_PParams.BackBufferHeight = height;
			if(g_Direct3D9Ex) g_Direct3D9_PParams.BackBufferCount = 2;
			else g_Direct3D9_PParams.BackBufferCount = 1;
			if(g_Direct3D9Ex) g_Direct3D9_PParams.SwapEffect = D3DSWAPEFFECT_FLIPEX;
			else g_Direct3D9_PParams.SwapEffect = D3DSWAPEFFECT_DISCARD;
			{
				g_Direct3D9_PParams.Windowed = TRUE;
				g_Direct3D9_PParams.BackBufferFormat = D3DFMT_UNKNOWN;
				g_Direct3D9_PParams.FullScreen_RefreshRateInHz = 0;
			}
			g_Direct3D9_PParams.PresentationInterval = (g_UseVsync ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE);
			g_Direct3D9_PParams.EnableAutoDepthStencil = TRUE;

			std::vector<D3DMULTISAMPLE_TYPE> checkMSAA;
			switch(g_MultiSampleAntiAliasing)
			{
				case D3DMULTISAMPLE_8_SAMPLES: checkMSAA.push_back(D3DMULTISAMPLE_8_SAMPLES);
				case D3DMULTISAMPLE_4_SAMPLES: checkMSAA.push_back(D3DMULTISAMPLE_4_SAMPLES);
				case D3DMULTISAMPLE_2_SAMPLES: checkMSAA.push_back(D3DMULTISAMPLE_2_SAMPLES);
				default: break;
			}

			if(!checkMSAA.empty())
			{
				D3DMULTISAMPLE_TYPE supportedMSAA = D3DMULTISAMPLE_NONE;
				for(D3DMULTISAMPLE_TYPE& msaa : checkMSAA)
				{
					if(SUCCEEDED(IDirect3D9_CheckDeviceMultiSampleType(g_Direct3D9, adapterIndex, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, TRUE, msaa, NULL)))
					{
						supportedMSAA = msaa;
						break;
					}
				}

				if(supportedMSAA == D3DMULTISAMPLE_NONE)
					MessageBoxA(nullptr, "This device doesn't support MSAA.\nGame will be launched without MSAA.", "Warning", MB_ICONHAND);
				else if(g_MultiSampleAntiAliasing != supportedMSAA)
					MessageBoxA(nullptr, "This device doesn't support requested MSAA.\nGame will be launched in different MSAA.", "Warning", MB_ICONHAND);

				g_MultiSampleAntiAliasing = supportedMSAA;
			}

			g_Direct3D9_PParams.MultiSampleType = g_MultiSampleAntiAliasing;
			g_Direct3D9_PParams.MultiSampleQuality = 0;

			D3DCAPS9 caps;
			IDirect3D9_GetDeviceCaps(g_Direct3D9, adapterIndex, D3DDEVTYPE_HAL, &caps);

			DWORD device_flags = (D3DCREATE_FPU_PRESERVE|D3DCREATE_NOWINDOWCHANGES);
			if(caps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) device_flags |= D3DCREATE_HARDWARE_VERTEXPROCESSING;
			else device_flags |= D3DCREATE_SOFTWARE_VERTEXPROCESSING;

			HRESULT result = E_FAIL;
			for(D3DFORMAT fmt : depthFormats)
			{
				g_Direct3D9_PParams.AutoDepthStencilFormat = fmt;
				result = IDirect3D9_CreateDevice(g_Direct3D9, adapterIndex, D3DDEVTYPE_HAL, gothicHWND, device_flags, &g_Direct3D9_PParams, &g_Direct3D9Device9);
				if(SUCCEEDED(result))
					break;
			}
			if(FAILED(result))
			{
				MessageBoxA(nullptr, "Failed to create Direct3D9 Device Context.", "Fatal Error", MB_ICONHAND);
				exit(-1);
				return S_OK;
			}
			if(g_Direct3D9Ex)
				IDirect3DDevice9_QueryInterface(g_Direct3D9Device9, D3D9_IID_IDirect3DDevice9Ex, reinterpret_cast<void**>(&g_Direct3D9Device9Ex));

			if(!fullscreenExclusivent)
			{
				DEVMODE desiredMode = {};
				desiredMode.dmSize = sizeof(DEVMODE);
				desiredMode.dmPelsWidth = static_cast<DWORD>(g_Direct3D9_PParams.BackBufferWidth);
				desiredMode.dmPelsHeight = static_cast<DWORD>(g_Direct3D9_PParams.BackBufferHeight);
				desiredMode.dmFields = (DM_PELSHEIGHT|DM_PELSWIDTH);
				ChangeDisplaySettings(&desiredMode, CDS_FULLSCREEN);

				LONG lStyle = GetWindowLongA(gothicHWND, GWL_STYLE);
				LONG lExStyle = GetWindowLongA(gothicHWND, GWL_EXSTYLE);
				lStyle &= ~(WS_CAPTION|WS_THICKFRAME|WS_MINIMIZE|WS_MAXIMIZE|WS_SYSMENU);
				lExStyle &= ~(WS_EX_DLGMODALFRAME|WS_EX_CLIENTEDGE|WS_EX_STATICEDGE);
				SetWindowLongA(gothicHWND, GWL_STYLE, lStyle);
				SetWindowLongA(gothicHWND, GWL_EXSTYLE, (lExStyle|WS_EX_TOPMOST));
				SetWindowPos(gothicHWND, HWND_TOPMOST, WindowPositionX, WindowPositionY, g_Direct3D9_PParams.BackBufferWidth, g_Direct3D9_PParams.BackBufferHeight, SWP_SHOWWINDOW);
			}
			else
				SetWindowPos(gothicHWND, HWND_BOTTOM, WindowPositionX, WindowPositionY, g_Direct3D9_PParams.BackBufferWidth, g_Direct3D9_PParams.BackBufferHeight, (SWP_NOZORDER|SWP_SHOWWINDOW));

			D3DADAPTER_IDENTIFIER9 identifier;
			if(SUCCEEDED(IDirect3D9_GetAdapterIdentifier(g_Direct3D9, adapterIndex, 0, &identifier)))
				GPUDeviceName = _strdup(identifier.Description);

			IDirect3DDevice9_GetRenderTarget(g_Direct3D9Device9, 0, &g_DefaultRenderTarget);
			if(g_MultiSampleAntiAliasing == D3DMULTISAMPLE_NONE)
			{
				result = IDirect3DDevice9_CreateTexture(g_Direct3D9Device9, g_Direct3D9_PParams.BackBufferWidth, g_Direct3D9_PParams.BackBufferHeight, 1,
					D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &g_ManagedBackBuffer, NULL);
				if(SUCCEEDED(result))
					IDirect3DTexture9_GetSurfaceLevel(g_ManagedBackBuffer, 0, &g_ManagedBoundTarget);
			}
			else
			{
				result = IDirect3DDevice9_CreateTexture(g_Direct3D9Device9, g_Direct3D9_PParams.BackBufferWidth, g_Direct3D9_PParams.BackBufferHeight, 1,
					D3DUSAGE_RENDERTARGET, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &g_ManagedBackBuffer, NULL);
				if(SUCCEEDED(result))
					IDirect3DDevice9_CreateRenderTarget(g_Direct3D9Device9, g_Direct3D9_PParams.BackBufferWidth, g_Direct3D9_PParams.BackBufferHeight,
						D3DFMT_X8R8G8B8, g_MultiSampleAntiAliasing, 0, FALSE, &g_ManagedBoundTarget, NULL);
			}

			result = IDirect3DDevice9_CreatePixelShader(g_Direct3D9Device9, D3D9_GammaCorrection, &g_GammaCorrectionPS);
			if(FAILED(result))
				g_HaveGammeCorrection = false;

			Direct3DDeviceCreated = 1;
			*lplpDD = new MyDirectDraw();
			return S_OK;
		}
        MessageBoxA(nullptr, "Failed to create Direct3D9 Interface.", "Fatal Error", MB_ICONHAND);
		exit(-1);
    }
	*lplpDD = new MyDirectDraw();
    return S_OK;
}

HRESULT WINAPI HookDirectDrawCreateEx_G2(GUID* lpGuid, LPVOID* lplpDD, REFIID iid, IUnknown* pUnkOuter)
{
    if(!g_Direct3D9)
    {
		if(Direct3DCreate9_12_Ptr)
		{
			typedef struct _D3D9ON12_ARGS
			{
				BOOL Enable9On12;
				IUnknown* pD3D12Device;
				IUnknown* ppD3D12Queues[2];
				UINT NumQueues;
				UINT NodeMask;
			} D3D9ON12_ARGS;

			D3D9ON12_ARGS args = {};
			args.Enable9On12 = TRUE;
            g_Direct3D9 = reinterpret_cast<IDirect3D9*(__stdcall*)(UINT, D3D9ON12_ARGS*, UINT)>(Direct3DCreate9_12_Ptr)(D3D_SDK_VERSION, &args, 1);
		}
        else if(Direct3DCreate9Ex_Ptr)
        {
            HRESULT result = reinterpret_cast<HRESULT(__stdcall*)(UINT, IDirect3D9Ex**)>(Direct3DCreate9Ex_Ptr)(D3D_SDK_VERSION, &g_Direct3D9Ex);
            if(SUCCEEDED(result))
				IDirect3D9Ex_QueryInterface(g_Direct3D9Ex, D3D9_IID_IDirect3D9, reinterpret_cast<void**>(&g_Direct3D9));
        }
        else if(Direct3DCreate9_Ptr)
            g_Direct3D9 = reinterpret_cast<IDirect3D9*(__stdcall*)(UINT)>(Direct3DCreate9_Ptr)(D3D_SDK_VERSION);

		if(g_Direct3D9)
		{
			HWND gothicHWND = *reinterpret_cast<HWND*>(0x8D422C);
			HMONITOR useMonitor = MonitorFromWindow(gothicHWND, MONITOR_DEFAULTTOPRIMARY);
			UINT adapterIndex = 0;
			UINT adapters = IDirect3D9_GetAdapterCount(g_Direct3D9);
			for(UINT i = 0; i < adapters; ++i)
			{
				if(IDirect3D9_GetAdapterMonitor(g_Direct3D9, i) == useMonitor)
				{
					adapterIndex = i;
					break;
				}
			}

			UINT modes = IDirect3D9_GetAdapterModeCount(g_Direct3D9, adapterIndex, D3DFMT_X8R8G8B8);
			for(UINT i = 0; i < modes; ++i)
			{
				D3DDISPLAYMODE dmode;
				if(SUCCEEDED(IDirect3D9_EnumAdapterModes(g_Direct3D9, adapterIndex, D3DFMT_X8R8G8B8, i, &dmode)))
				{
					auto it = std::find_if(g_Direct3D9VideoModes.begin(), g_Direct3D9VideoModes.end(),
						[&dmode](DDSURFACEDESC2& a) {return (a.dwWidth == dmode.Width && a.dwHeight == dmode.Height); });
					if(it == g_Direct3D9VideoModes.end())
					{
						DDSURFACEDESC2 newMode = {};
						newMode.dwSize = sizeof(DDSURFACEDESC2);
						newMode.dwWidth = dmode.Width;
						newMode.dwHeight = dmode.Height;
						newMode.ddpfPixelFormat.dwRGBBitCount = 32;
						newMode.dwRefreshRate = 60;
						g_Direct3D9VideoModes.push_back(newMode);
					}
				}
			}
			if(g_Direct3D9VideoModes.empty())
			{
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

					auto it = std::find_if(g_Direct3D9VideoModes.begin(), g_Direct3D9VideoModes.end(),
						[&devmode](DDSURFACEDESC2& a) {return (a.dwWidth == devmode.dmPelsWidth && a.dwHeight == devmode.dmPelsHeight);});
					if(it == g_Direct3D9VideoModes.end())
					{
						DDSURFACEDESC2 newMode = {};
						newMode.dwSize = sizeof(DDSURFACEDESC2);
						newMode.dwWidth = devmode.dmPelsWidth;
						newMode.dwHeight = devmode.dmPelsHeight;
						newMode.ddpfPixelFormat.dwRGBBitCount = 32;
						newMode.dwRefreshRate = 60;
						g_Direct3D9VideoModes.push_back(newMode);
					}
				}
			}
			{
				auto it = std::find_if(g_Direct3D9VideoModes.begin(), g_Direct3D9VideoModes.end(),
					[](DDSURFACEDESC2& a) {return (a.dwWidth == 800 && a.dwHeight == 600); });
				if(it == g_Direct3D9VideoModes.end())
				{
					DDSURFACEDESC2 newMode = {};
					newMode.dwSize = sizeof(DDSURFACEDESC2);
					newMode.dwWidth = 800;
					newMode.dwHeight = 600;
					newMode.ddpfPixelFormat.dwRGBBitCount = 32;
					newMode.dwRefreshRate = 60;
					g_Direct3D9VideoModes.push_back(newMode);
				}
			}
			std::sort(g_Direct3D9VideoModes.begin(), g_Direct3D9VideoModes.end(), [](DDSURFACEDESC2& a, DDSURFACEDESC2& b) -> bool
			{
				return (a.dwWidth < b.dwWidth) || ((a.dwWidth == b.dwWidth) && (a.dwHeight < b.dwHeight));
			});

			UINT width = 800;
			UINT height = 600;
			UINT refreshRate = 60;
			{
				DEVMODEA devMode = {};
				devMode.dmSize = sizeof(DEVMODEA);

				MONITORINFOEXA miex;
				miex.cbSize = sizeof(MONITORINFOEXA);
				if(GetMonitorInfoA(useMonitor, &miex))
				{
					if(EnumDisplaySettingsA(miex.szDevice, ENUM_CURRENT_SETTINGS, &devMode))
					{
						width = devMode.dmPelsWidth;
						height = devMode.dmPelsHeight;
						refreshRate = devMode.dmDisplayFrequency;
					}
				}
				else
				{
					if(EnumDisplaySettingsA(nullptr, ENUM_CURRENT_SETTINGS, &devMode))
					{
						width = devMode.dmPelsWidth;
						height = devMode.dmPelsHeight;
						refreshRate = devMode.dmDisplayFrequency;
					}
				}

				DWORD options = *reinterpret_cast<DWORD*>(0x8CD988);
				int resWidth = reinterpret_cast<int(__thiscall*)(DWORD, DWORD, const char*, UINT)>(0x462390)(options, 0x8CD474, "zVidResFullscreenX", width);
				int resHeight = reinterpret_cast<int(__thiscall*)(DWORD, DWORD, const char*, UINT)>(0x462390)(options, 0x8CD474, "zVidResFullscreenY", height);
				fullscreenExclusivent = reinterpret_cast<int(__thiscall*)(DWORD, DWORD, const char*, int)>(0x462390)(options, 0x8CD474, "zStartupWindowed", 1);
				if(fullscreenExclusivent)
				{
					WindowPositionX = *reinterpret_cast<int*>(*reinterpret_cast<DWORD*>(0x503216));
					WindowPositionY = *reinterpret_cast<int*>(*reinterpret_cast<DWORD*>(0x503209));
					if(WindowPositionX == static_cast<int>(0x80000000) || WindowPositionY == static_cast<int>(0x80000000))
					{
						WindowPositionX = 0;
						WindowPositionY = 0;
					}
				}

				bool foundMode = false;
				for(DDSURFACEDESC2& mode : g_Direct3D9VideoModes)
				{
					if(static_cast<int>(mode.dwWidth) == resWidth && static_cast<int>(mode.dwHeight) == resHeight)
					{
						foundMode = true;
						break;
					}
				}
				if(!foundMode)
				{
					resWidth = static_cast<int>(width);
					resHeight = static_cast<int>(height);

					reinterpret_cast<int(__thiscall*)(DWORD, DWORD, const char*, int, int)>(0x461E30)(options, 0x8CD474, "zVidResFullscreenX", resWidth, 0);
					reinterpret_cast<int(__thiscall*)(DWORD, DWORD, const char*, int, int)>(0x461E30)(options, 0x8CD474, "zVidResFullscreenY", resHeight, 0);
				}
				else
				{
					width = static_cast<UINT>(resWidth);
					height = static_cast<UINT>(resHeight);
				}
			}

			std::vector<D3DFORMAT> depthFormats;
			if(g_Direct3D9Ex) depthFormats.push_back(D3DFMT_D32_LOCKABLE);
			depthFormats.push_back(D3DFMT_D32F_LOCKABLE);
			depthFormats.push_back(D3DFMT_D32);
			depthFormats.push_back(D3DFMT_D24X8);
			depthFormats.push_back(D3DFMT_D24S8);
			depthFormats.push_back(D3DFMT_D24FS8);
			depthFormats.push_back(D3DFMT_D24S8);
			depthFormats.push_back(D3DFMT_D24X4S4);
			depthFormats.push_back(D3DFMT_D16_LOCKABLE);
			depthFormats.push_back(D3DFMT_D16);

			ZeroMemory(&g_Direct3D9_PParams, sizeof(D3DPRESENT_PARAMETERS));
			g_Direct3D9_PParams.hDeviceWindow = gothicHWND;
			g_Direct3D9_PParams.BackBufferWidth = width;
			g_Direct3D9_PParams.BackBufferHeight = height;
			if(g_Direct3D9Ex) g_Direct3D9_PParams.BackBufferCount = 2;
			else g_Direct3D9_PParams.BackBufferCount = 1;
			if(g_Direct3D9Ex) g_Direct3D9_PParams.SwapEffect = D3DSWAPEFFECT_FLIPEX;
			else g_Direct3D9_PParams.SwapEffect = D3DSWAPEFFECT_DISCARD;
			{
				g_Direct3D9_PParams.Windowed = TRUE;
				g_Direct3D9_PParams.BackBufferFormat = D3DFMT_UNKNOWN;
				g_Direct3D9_PParams.FullScreen_RefreshRateInHz = 0;
			}
			g_Direct3D9_PParams.PresentationInterval = (g_UseVsync ? D3DPRESENT_INTERVAL_ONE : D3DPRESENT_INTERVAL_IMMEDIATE);
			g_Direct3D9_PParams.EnableAutoDepthStencil = TRUE;

			std::vector<D3DMULTISAMPLE_TYPE> checkMSAA;
			switch(g_MultiSampleAntiAliasing)
			{
				case D3DMULTISAMPLE_8_SAMPLES: checkMSAA.push_back(D3DMULTISAMPLE_8_SAMPLES);
				case D3DMULTISAMPLE_4_SAMPLES: checkMSAA.push_back(D3DMULTISAMPLE_4_SAMPLES);
				case D3DMULTISAMPLE_2_SAMPLES: checkMSAA.push_back(D3DMULTISAMPLE_2_SAMPLES);
				default: break;
			}

			if(!checkMSAA.empty())
			{
				D3DMULTISAMPLE_TYPE supportedMSAA = D3DMULTISAMPLE_NONE;
				for(D3DMULTISAMPLE_TYPE& msaa : checkMSAA)
				{
					if(SUCCEEDED(IDirect3D9_CheckDeviceMultiSampleType(g_Direct3D9, adapterIndex, D3DDEVTYPE_HAL, D3DFMT_X8R8G8B8, TRUE, msaa, NULL)))
					{
						supportedMSAA = msaa;
						break;
					}
				}

				if(supportedMSAA == D3DMULTISAMPLE_NONE)
					MessageBoxA(nullptr, "This device doesn't support MSAA.\nGame will be launched without MSAA.", "Warning", MB_ICONHAND);
				else if(g_MultiSampleAntiAliasing != supportedMSAA)
					MessageBoxA(nullptr, "This device doesn't support requested MSAA.\nGame will be launched in different MSAA.", "Warning", MB_ICONHAND);

				g_MultiSampleAntiAliasing = supportedMSAA;
			}

			g_Direct3D9_PParams.MultiSampleType = g_MultiSampleAntiAliasing;
			g_Direct3D9_PParams.MultiSampleQuality = 0;

			D3DCAPS9 caps;
			IDirect3D9_GetDeviceCaps(g_Direct3D9, adapterIndex, D3DDEVTYPE_HAL, &caps);

			DWORD device_flags = (D3DCREATE_FPU_PRESERVE|D3DCREATE_NOWINDOWCHANGES);
			if(caps.DevCaps & D3DDEVCAPS_HWTRANSFORMANDLIGHT) device_flags |= D3DCREATE_HARDWARE_VERTEXPROCESSING;
			else device_flags |= D3DCREATE_SOFTWARE_VERTEXPROCESSING;

			HRESULT result = E_FAIL;
			for(D3DFORMAT fmt : depthFormats)
			{
				g_Direct3D9_PParams.AutoDepthStencilFormat = fmt;
				result = IDirect3D9_CreateDevice(g_Direct3D9, adapterIndex, D3DDEVTYPE_HAL, gothicHWND, device_flags, &g_Direct3D9_PParams, &g_Direct3D9Device9);
				if(SUCCEEDED(result))
					break;
			}
			if(FAILED(result))
			{
				MessageBoxA(nullptr, "Failed to create Direct3D9 Device Context.", "Fatal Error", MB_ICONHAND);
				exit(-1);
				return S_OK;
			}
			if(g_Direct3D9Ex)
				IDirect3DDevice9_QueryInterface(g_Direct3D9Device9, D3D9_IID_IDirect3DDevice9Ex, reinterpret_cast<void**>(&g_Direct3D9Device9Ex));

			if(!fullscreenExclusivent)
			{
				DEVMODE desiredMode = {};
				desiredMode.dmSize = sizeof(DEVMODE);
				desiredMode.dmPelsWidth = static_cast<DWORD>(g_Direct3D9_PParams.BackBufferWidth);
				desiredMode.dmPelsHeight = static_cast<DWORD>(g_Direct3D9_PParams.BackBufferHeight);
				desiredMode.dmFields = (DM_PELSHEIGHT|DM_PELSWIDTH);
				ChangeDisplaySettings(&desiredMode, CDS_FULLSCREEN);

				LONG lStyle = GetWindowLongA(gothicHWND, GWL_STYLE);
				LONG lExStyle = GetWindowLongA(gothicHWND, GWL_EXSTYLE);
				lStyle &= ~(WS_CAPTION|WS_THICKFRAME|WS_MINIMIZE|WS_MAXIMIZE|WS_SYSMENU);
				lExStyle &= ~(WS_EX_DLGMODALFRAME|WS_EX_CLIENTEDGE|WS_EX_STATICEDGE);
				SetWindowLongA(gothicHWND, GWL_STYLE, lStyle);
				SetWindowLongA(gothicHWND, GWL_EXSTYLE, (lExStyle|WS_EX_TOPMOST));
				SetWindowPos(gothicHWND, HWND_TOPMOST, WindowPositionX, WindowPositionY, g_Direct3D9_PParams.BackBufferWidth, g_Direct3D9_PParams.BackBufferHeight, SWP_SHOWWINDOW);
			}
			else
				SetWindowPos(gothicHWND, HWND_BOTTOM, WindowPositionX, WindowPositionY, g_Direct3D9_PParams.BackBufferWidth, g_Direct3D9_PParams.BackBufferHeight, (SWP_NOZORDER|SWP_SHOWWINDOW));

			D3DADAPTER_IDENTIFIER9 identifier;
			if(SUCCEEDED(IDirect3D9_GetAdapterIdentifier(g_Direct3D9, adapterIndex, 0, &identifier)))
				GPUDeviceName = _strdup(identifier.Description);

			IDirect3DDevice9_GetRenderTarget(g_Direct3D9Device9, 0, &g_DefaultRenderTarget);
			if(g_MultiSampleAntiAliasing == D3DMULTISAMPLE_NONE)
			{
				result = IDirect3DDevice9_CreateTexture(g_Direct3D9Device9, g_Direct3D9_PParams.BackBufferWidth, g_Direct3D9_PParams.BackBufferHeight, 1,
					D3DUSAGE_RENDERTARGET, D3DFMT_A8R8G8B8, D3DPOOL_DEFAULT, &g_ManagedBackBuffer, NULL);
				if(SUCCEEDED(result))
					IDirect3DTexture9_GetSurfaceLevel(g_ManagedBackBuffer, 0, &g_ManagedBoundTarget);
			}
			else
			{
				result = IDirect3DDevice9_CreateTexture(g_Direct3D9Device9, g_Direct3D9_PParams.BackBufferWidth, g_Direct3D9_PParams.BackBufferHeight, 1,
					D3DUSAGE_RENDERTARGET, D3DFMT_X8R8G8B8, D3DPOOL_DEFAULT, &g_ManagedBackBuffer, NULL);
				if(SUCCEEDED(result))
					IDirect3DDevice9_CreateRenderTarget(g_Direct3D9Device9, g_Direct3D9_PParams.BackBufferWidth, g_Direct3D9_PParams.BackBufferHeight,
						D3DFMT_X8R8G8B8, g_MultiSampleAntiAliasing, 0, FALSE, &g_ManagedBoundTarget, NULL);
			}

			result = IDirect3DDevice9_CreatePixelShader(g_Direct3D9Device9, D3D9_GammaCorrection, &g_GammaCorrectionPS);
			if(FAILED(result))
				g_HaveGammeCorrection = false;
			
			Direct3DDeviceCreated = 1;
			*lplpDD = new MyDirectDraw();
			return S_OK;
		}
        MessageBoxA(nullptr, "Failed to create Direct3D9 Interface.", "Fatal Error", MB_ICONHAND);
		exit(-1);
    }
	*lplpDD = new MyDirectDraw();
    return S_OK;
}

HRESULT WINAPI HookDirectDrawEnumerateA(LPDDENUMCALLBACKA lpCallback, LPVOID lpContext)
{
    GUID deviceGUID = {0xF5049E78, 0x4861, 0x11D2, {0xA4, 0x07, 0x00, 0xA0, 0xC9, 0x06, 0x29, 0xA8}};
    lpCallback(&deviceGUID, const_cast<char*>("DirectX9"), const_cast<char*>("DirectX9"), lpContext);
    return S_OK;
}

HRESULT WINAPI HookDirectDrawEnumerateExA(LPDDENUMCALLBACKEXA lpCallback, LPVOID lpContext, DWORD dwFlags)
{
    GUID deviceGUID = {0xF5049E78, 0x4861, 0x11D2, {0xA4, 0x07, 0x00, 0xA0, 0xC9, 0x06, 0x29, 0xA8}};
    lpCallback(&deviceGUID, const_cast<char*>("DirectX9"), const_cast<char*>("DirectX9"), lpContext, nullptr);
    return S_OK;
}

int __fastcall HookzBinkPlayerGetPixelFormat(DWORD zBinkPlayer, DWORD _EDX, DWORD desc)
{
	return 3;
}

DWORD __fastcall HookAcquireVertexBuffer_G1(DWORD zVertexBuffer, DWORD _EDX, DWORD vertexFormat, DWORD numVertRequired, DWORD createFlags, DWORD lockFlags, int& indexVertexStart)
{
	g_CreateStaticVertexBuffer = true;
	DWORD vb = reinterpret_cast<DWORD(__thiscall*)(DWORD, DWORD, DWORD, DWORD, DWORD, int&)>(0x5D1590)(zVertexBuffer, vertexFormat, numVertRequired, createFlags, lockFlags, indexVertexStart);
	g_CreateStaticVertexBuffer = false;
	return vb;
}

DWORD __fastcall HookAcquireVertexBuffer_G2(DWORD zVertexBuffer, DWORD _EDX, DWORD vertexFormat, DWORD numVertRequired, DWORD createFlags, DWORD lockFlags, int& indexVertexStart)
{
	g_CreateStaticVertexBuffer = true;
	DWORD vb = reinterpret_cast<DWORD(__thiscall*)(DWORD, DWORD, DWORD, DWORD, DWORD, int&)>(0x5FC600)(zVertexBuffer, vertexFormat, numVertRequired, createFlags, lockFlags, indexVertexStart);
	g_CreateStaticVertexBuffer = false;
	return vb;
}

float* __fastcall HookGetLightStatAtPos_G1(DWORD zCPolygon, DWORD _EDX, float* result, float* pos)
{
	g_ReadOnlyLightmapData = true;
	float* r = reinterpret_cast<float*(__thiscall*)(DWORD, float*, float*)>(0x597950)(zCPolygon, result, pos);
	g_ReadOnlyLightmapData = false;
	return r;
}

float* __fastcall HookGetLightStatAtPos_G2(DWORD zCPolygon, DWORD _EDX, float* result, float* pos)
{
	g_ReadOnlyLightmapData = true;
	float* r = reinterpret_cast<float*(__thiscall*)(DWORD, float*, float*)>(0x5B9410)(zCPolygon, result, pos);
	g_ReadOnlyLightmapData = false;
	return r;
}

void __cdecl HookSetMode_G1(UINT width, UINT height, int bpp, DWORD contextHandle)
{
	reinterpret_cast<void(__cdecl*)(UINT, UINT, int, DWORD)>(0x702180)(width, height, bpp, contextHandle);
	if(!Direct3DDeviceCreated)
		return;

	g_Direct3D9_PParams.BackBufferWidth = width;
	g_Direct3D9_PParams.BackBufferHeight = height;
	ResetDevice();
	if(!fullscreenExclusivent)
	{
		DEVMODE desiredMode = {};
		desiredMode.dmSize = sizeof(DEVMODE);
		desiredMode.dmPelsWidth = static_cast<DWORD>(g_Direct3D9_PParams.BackBufferWidth);
		desiredMode.dmPelsHeight = static_cast<DWORD>(g_Direct3D9_PParams.BackBufferHeight);
		desiredMode.dmFields = (DM_PELSHEIGHT|DM_PELSWIDTH);
		ChangeDisplaySettings(&desiredMode, CDS_FULLSCREEN);

		HWND gothicHWND = *reinterpret_cast<HWND*>(0x86F4B8);
		LONG lStyle = GetWindowLongA(gothicHWND, GWL_STYLE);
		LONG lExStyle = GetWindowLongA(gothicHWND, GWL_EXSTYLE);
		lStyle &= ~(WS_CAPTION|WS_THICKFRAME|WS_MINIMIZE|WS_MAXIMIZE|WS_SYSMENU);
		lExStyle &= ~(WS_EX_DLGMODALFRAME|WS_EX_CLIENTEDGE|WS_EX_STATICEDGE);
		SetWindowLongA(gothicHWND, GWL_STYLE, lStyle);
		SetWindowLongA(gothicHWND, GWL_EXSTYLE, (lExStyle|WS_EX_TOPMOST));
		SetWindowPos(gothicHWND, HWND_TOPMOST, WindowPositionX, WindowPositionY, g_Direct3D9_PParams.BackBufferWidth, g_Direct3D9_PParams.BackBufferHeight, SWP_SHOWWINDOW);
	}
}

void __cdecl HookSetMode_G2(UINT width, UINT height, int bpp, DWORD contextHandle)
{
	reinterpret_cast<void(__cdecl*)(UINT, UINT, int, DWORD)>(0x7ABDB0)(width, height, bpp, contextHandle);
	if(!Direct3DDeviceCreated)
		return;

	g_Direct3D9_PParams.BackBufferWidth = width;
	g_Direct3D9_PParams.BackBufferHeight = height;
	ResetDevice();
	if(!fullscreenExclusivent)
	{
		DEVMODE desiredMode = {};
		desiredMode.dmSize = sizeof(DEVMODE);
		desiredMode.dmPelsWidth = static_cast<DWORD>(g_Direct3D9_PParams.BackBufferWidth);
		desiredMode.dmPelsHeight = static_cast<DWORD>(g_Direct3D9_PParams.BackBufferHeight);
		desiredMode.dmFields = (DM_PELSHEIGHT|DM_PELSWIDTH);
		ChangeDisplaySettings(&desiredMode, CDS_FULLSCREEN);

		HWND gothicHWND = *reinterpret_cast<HWND*>(0x8D422C);
		LONG lStyle = GetWindowLongA(gothicHWND, GWL_STYLE);
		LONG lExStyle = GetWindowLongA(gothicHWND, GWL_EXSTYLE);
		lStyle &= ~(WS_CAPTION|WS_THICKFRAME|WS_MINIMIZE|WS_MAXIMIZE|WS_SYSMENU);
		lExStyle &= ~(WS_EX_DLGMODALFRAME|WS_EX_CLIENTEDGE|WS_EX_STATICEDGE);
		SetWindowLongA(gothicHWND, GWL_STYLE, lStyle);
		SetWindowLongA(gothicHWND, GWL_EXSTYLE, (lExStyle|WS_EX_TOPMOST));
		SetWindowPos(gothicHWND, HWND_TOPMOST, WindowPositionX, WindowPositionY, g_Direct3D9_PParams.BackBufferWidth, g_Direct3D9_PParams.BackBufferHeight, SWP_SHOWWINDOW);
	}
}

int __fastcall HookReadStartupWindowed(DWORD zCOptions, DWORD _EDX, DWORD sectorString, const char* keyName, int defaultValue)
{
	return 1;
}

void HandleWindowFocus(HWND hwnd, bool inFocus)
{
	if(!Direct3DDeviceCreated)
		return;

	static int LastFocusState = 1;
    if(fullscreenExclusivent || (inFocus && LastFocusState == 1) || (!inFocus && LastFocusState == 0))
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
			desiredMode.dmPelsWidth = static_cast<DWORD>(g_Direct3D9_PParams.BackBufferWidth);
			desiredMode.dmPelsHeight = static_cast<DWORD>(g_Direct3D9_PParams.BackBufferHeight);
			desiredMode.dmFields = (DM_PELSHEIGHT|DM_PELSWIDTH);
			ChangeDisplaySettings(&desiredMode, CDS_FULLSCREEN);

			LONG lStyle = GetWindowLongA(hwnd, GWL_STYLE);
			LONG lExStyle = GetWindowLongA(hwnd, GWL_EXSTYLE);
			lStyle &= ~(WS_CAPTION|WS_THICKFRAME|WS_MINIMIZE|WS_MAXIMIZE|WS_SYSMENU);
			lExStyle &= ~(WS_EX_DLGMODALFRAME|WS_EX_CLIENTEDGE|WS_EX_STATICEDGE);
			SetWindowLongA(hwnd, GWL_STYLE, lStyle);
			SetWindowLongA(hwnd, GWL_EXSTYLE, (lExStyle|WS_EX_TOPMOST));
			SetWindowPos(hwnd, HWND_TOPMOST, WindowPositionX, WindowPositionY, g_Direct3D9_PParams.BackBufferWidth, g_Direct3D9_PParams.BackBufferHeight, SWP_SHOWWINDOW);
		}
        LastFocusState = 1;
    }
}

HWND __cdecl HookGetForegroundWindow_G1()
{
    HWND window = *reinterpret_cast<HWND*>(0x86F4B8);
    if(GetForegroundWindow() != window)
        HandleWindowFocus(window, false);
    else
        HandleWindowFocus(window, true);
    return window;
}

HWND __cdecl HookGetForegroundWindow_G2()
{
    HWND window = *reinterpret_cast<HWND*>(0x8D422C);
    if(GetForegroundWindow() != window)
        HandleWindowFocus(window, false);
    else
        HandleWindowFocus(window, true);
    return window;
}

void __fastcall HookSetGammaCorrection_G1(DWORD zCRnd_D3D, DWORD _EDX, float gamma, float contrast, float brightness)
{
	*reinterpret_cast<float*>(zCRnd_D3D + 0x4A4) = gamma;

	g_DeviceGamma = gamma;
	g_DeviceContrast = contrast;
	g_DeviceBrightness = brightness;
}

void __fastcall HookSetGammaCorrection_G2(DWORD zCRnd_D3D, DWORD _EDX, float gamma, float contrast, float brightness)
{
	*reinterpret_cast<float*>(zCRnd_D3D + 0x4A8) = gamma;

	g_DeviceGamma = gamma;
	g_DeviceContrast = contrast;
	g_DeviceBrightness = brightness;
}

unsigned char* FrontBufferData = nullptr;
HRESULT __stdcall FrontBufferLock(LPRECT lpDestRect, LPDDSURFACEDESC2 lpDDSurfaceDesc, DWORD dwFlags, HANDLE hEvent)
{
	if(g_ManagedBoundTarget && g_MultiSampleAntiAliasing != D3DMULTISAMPLE_NONE)
	{
		// Hack MSAA render target to texture so that we can fetch the pixels from vram
		IDirect3DSurface9* tempSurface = nullptr;
		IDirect3DTexture9_GetSurfaceLevel(g_ManagedBackBuffer, 0, &tempSurface);
		IDirect3DDevice9_StretchRect(g_Direct3D9Device9, g_ManagedBoundTarget, nullptr, tempSurface, nullptr, D3DTEXF_NONE);
		IDirect3DSurface9_Release(tempSurface);
	}

	bool FAILURE = false;
	IDirect3DSurface9* defaultBackBuffer;
	if(g_ManagedBackBuffer)
		IDirect3DTexture9_GetSurfaceLevel(g_ManagedBackBuffer, 0, &defaultBackBuffer);
	else
		IDirect3DDevice9_GetRenderTarget(g_Direct3D9Device9, 0, &defaultBackBuffer);
	{
		D3DSURFACE_DESC desc;
		HRESULT result = IDirect3DSurface9_GetDesc(defaultBackBuffer, &desc);
		if(SUCCEEDED(result))
		{
			IDirect3DSurface9* surface;
			result = IDirect3DDevice9_CreateOffscreenPlainSurface(g_Direct3D9Device9, desc.Width, desc.Height, desc.Format, D3DPOOL_SYSTEMMEM, &surface, NULL);
			if(FAILED(result))
			{
				FAILURE = true;
				goto EXIT_HERE;
			}

			result = IDirect3DDevice9_GetRenderTargetData(g_Direct3D9Device9, defaultBackBuffer, surface);
			if(FAILED(result))
			{
				IDirect3DSurface9_Release(surface);
				FAILURE = true;
				goto EXIT_HERE;
			}

			unsigned char* pixels = new unsigned char[desc.Width * desc.Height * 4];
			if(!pixels)
			{
				IDirect3DSurface9_Release(surface);
				FAILURE = true;
				goto EXIT_HERE;
			}

			D3DLOCKED_RECT locked;
			result = IDirect3DSurface9_LockRect(surface, &locked, NULL, D3DLOCK_READONLY);
			if(FAILED(result))
			{
				IDirect3DSurface9_Release(surface);
				free(pixels);
				FAILURE = true;
				goto EXIT_HERE;
			}

			if(desc.Format == D3DFMT_X8R8G8B8 || desc.Format == D3DFMT_A8R8G8B8)
			{
				int perfectPitch = desc.Width * 4;
				if(locked.Pitch == perfectPitch)
					memcpy(pixels, locked.pBits, perfectPitch * desc.Height);
				else
				{
					if(perfectPitch > locked.Pitch)
						perfectPitch = locked.Pitch;

					unsigned char* dstData = pixels;
					unsigned char* srcData = reinterpret_cast<unsigned char*>(locked.pBits);
					for(UINT row = 0; row < desc.Height; ++row)
					{
						memcpy(dstData, srcData, perfectPitch);
						srcData += locked.Pitch;
						dstData += perfectPitch;
					}
				}

				IDirect3DSurface9_UnlockRect(surface);
				IDirect3DSurface9_Release(surface);

				lpDDSurfaceDesc->lPitch = desc.Width * 4;
				lpDDSurfaceDesc->dwWidth = desc.Width;
				lpDDSurfaceDesc->dwHeight = desc.Height;
				lpDDSurfaceDesc->lpSurface = pixels;
				FrontBufferData = pixels;
			}
			else
			{
				IDirect3DSurface9_UnlockRect(surface);
				IDirect3DSurface9_Release(surface);
				free(pixels);
				FAILURE = true;
				goto EXIT_HERE;
			}
		}
	}
	EXIT_HERE:
	IDirect3DSurface9_Release(defaultBackBuffer);
	if(FAILURE)
	{
		FrontBufferData = new unsigned char[256 * 256 * 4];
		memset(FrontBufferData, 0, 256 * 256 * 4);

		lpDDSurfaceDesc->lPitch = 256 * 4;
		lpDDSurfaceDesc->dwWidth = 256;
		lpDDSurfaceDesc->dwHeight = 256;
		lpDDSurfaceDesc->lpSurface = FrontBufferData;
	}

	lpDDSurfaceDesc->ddpfPixelFormat.dwRGBBitCount = 32;
	lpDDSurfaceDesc->ddpfPixelFormat.dwRBitMask = 0x00FF0000;
	lpDDSurfaceDesc->ddpfPixelFormat.dwGBitMask = 0x0000FF00;
	lpDDSurfaceDesc->ddpfPixelFormat.dwBBitMask = 0x000000FF;
	lpDDSurfaceDesc->ddpfPixelFormat.dwRGBAlphaBitMask = 0x00000000;
	return S_OK;
}

HRESULT STDMETHODCALLTYPE FrontBufferUnlock(DWORD texturePointer, LPRECT lpRect)
{
	delete[] FrontBufferData;
	FrontBufferData = nullptr;
	return S_OK;
}

void InstallD3D9Renderer_G1(int rendererOption, int msaa, bool vsync)
{
	const char* renderDLL = "d3d9.dll";
	if(rendererOption == 5)
		renderDLL = "dxvk.dll";
	else if(rendererOption == 6)
		renderDLL = "dxvk2.dll";
	else if(rendererOption == 3)
		renderDLL = "d3d9onGL.dll";

	if(HMODULE d3d9Module = LoadLibraryA(renderDLL))
	{
		if(rendererOption == 12)
		{
			if((Direct3DCreate9_12_Ptr = reinterpret_cast<DWORD>(GetProcAddress(d3d9Module, "Direct3DCreate9On12"))) == 0)
			{
				MessageBoxA(nullptr, "Failed to load D3D9on12 - most likely your system is out-dated.", "Fatal Error", MB_ICONHAND);
				exit(-1);
			}
		}

		if(rendererOption == 9 && msaa == 0 && (Direct3DCreate9Ex_Ptr = reinterpret_cast<DWORD>(GetProcAddress(d3d9Module, "Direct3DCreate9Ex"))) != 0
			|| (Direct3DCreate9_Ptr = reinterpret_cast<DWORD>(GetProcAddress(d3d9Module, "Direct3DCreate9"))) != 0)
		{
			g_MultiSampleAntiAliasing = static_cast<D3DMULTISAMPLE_TYPE>(msaa);
			g_UseVsync = vsync;

			HookJMPN(0x75B482, reinterpret_cast<DWORD>(&HookDirectDrawCreateEx_G1));
			HookJMPN(0x75B488, reinterpret_cast<DWORD>(&HookDirectDrawEnumerateA));
			HookJMPN(0x772E48, reinterpret_cast<DWORD>(&HookDirectDrawEnumerateExA));
			HookJMP(0x43C9E0, reinterpret_cast<DWORD>(&HookzBinkPlayerGetPixelFormat));
			HookCall(0x5A502D, reinterpret_cast<DWORD>(&HookAcquireVertexBuffer_G1));
			HookCall(0x5D5527, reinterpret_cast<DWORD>(&HookGetLightStatAtPos_G1));
			HookCall(0x42AA4B, reinterpret_cast<DWORD>(&HookSetMode_G1));
			HookCall(0x6019CB, reinterpret_cast<DWORD>(&HookReadStartupWindowed));
			HookCallN(0x71F1BA, reinterpret_cast<DWORD>(&FrontBufferLock));
			HookCallN(0x71F340, reinterpret_cast<DWORD>(&FrontBufferUnlock));
			HookCallN(0x71F3EA, reinterpret_cast<DWORD>(&FrontBufferUnlock));
			HookCallN(0x71F51B, reinterpret_cast<DWORD>(&FrontBufferUnlock));
			HookCallN(0x4F6BF6, reinterpret_cast<DWORD>(&HookGetForegroundWindow_G1));
			OverWrite(0x7DF06C, reinterpret_cast<DWORD>(&HookSetGammaCorrection_G1));
			WriteStack(0x712860, "\x33\xC0\xC2\x04\x00\x90\x90");
			HookJMPN(0x71EE02, 0x71F03F);
			HookJMPN(0x711060, 0x7111A5);
			HookJMPN(0x71273B, 0x712758);
			OverWriteByte(0x4F6C0E, 0xEB);
			Nop(0x72018B, 2);
		}
	}
	else
	{
		if(rendererOption == 5 || rendererOption == 6)
		{
			MessageBoxA(nullptr, "Failed to load DXVK - most likely your system don't support vulkan.", "Fatal Error", MB_ICONHAND);
			exit(-1);
		}
		else if(rendererOption == 3)
		{
			MessageBoxA(nullptr, "Failed to load Wine D3D.", "Fatal Error", MB_ICONHAND);
			exit(-1);
		}
		else
		{
			MessageBoxA(nullptr, "Failed to load DirectX9 library.", "Fatal Error", MB_ICONHAND);
			exit(-1);
		}
	}
}

void InstallD3D9Renderer_G2(int rendererOption, int msaa, bool vsync)
{
	const char* renderDLL = "d3d9.dll";
	if(rendererOption == 5)
		renderDLL = "dxvk.dll";
	else if(rendererOption == 6)
		renderDLL = "dxvk2.dll";
	else if(rendererOption == 3)
		renderDLL = "d3d9onGL.dll";
	
	if(HMODULE d3d9Module = LoadLibraryA(renderDLL))
	{
		if(rendererOption == 12)
		{
			if((Direct3DCreate9_12_Ptr = reinterpret_cast<DWORD>(GetProcAddress(d3d9Module, "Direct3DCreate9On12"))) == 0)
			{
				MessageBoxA(nullptr, "Failed to load D3D9on12 - most likely your system is out-dated.", "Fatal Error", MB_ICONHAND);
				exit(-1);
			}
		}

		if(rendererOption == 9 && msaa == 0 && (Direct3DCreate9Ex_Ptr = reinterpret_cast<DWORD>(GetProcAddress(d3d9Module, "Direct3DCreate9Ex"))) != 0
			|| (Direct3DCreate9_Ptr = reinterpret_cast<DWORD>(GetProcAddress(d3d9Module, "Direct3DCreate9"))) != 0)
		{
			g_MultiSampleAntiAliasing = static_cast<D3DMULTISAMPLE_TYPE>(msaa);
			g_UseVsync = vsync;

			HookJMPN(0x7B4B94, reinterpret_cast<DWORD>(&HookDirectDrawCreateEx_G2));
			HookJMPN(0x7B4B9A, reinterpret_cast<DWORD>(&HookDirectDrawEnumerateExA));
			HookJMP(0x440790, reinterpret_cast<DWORD>(&HookzBinkPlayerGetPixelFormat));
			HookCall(0x5C6E91, reinterpret_cast<DWORD>(&HookAcquireVertexBuffer_G2));
			HookCall(0x600974, reinterpret_cast<DWORD>(&HookGetLightStatAtPos_G2));
			HookCall(0x42CDBD, reinterpret_cast<DWORD>(&HookSetMode_G2));
			HookCall(0x630B66, reinterpret_cast<DWORD>(&HookReadStartupWindowed));
			HookCallN(0x657A8A, reinterpret_cast<DWORD>(&FrontBufferLock));
			HookCallN(0x657C10, reinterpret_cast<DWORD>(&FrontBufferUnlock));
			HookCallN(0x657CBA, reinterpret_cast<DWORD>(&FrontBufferUnlock));
			HookCallN(0x657DEB, reinterpret_cast<DWORD>(&FrontBufferUnlock));
			HookCallN(0x505535, reinterpret_cast<DWORD>(&HookGetForegroundWindow_G2));
			OverWrite(0x83B210, reinterpret_cast<DWORD>(&HookSetGammaCorrection_G2));
			WriteStack(0x648D10, "\x33\xC0\xC2\x04\x00\x90\x90");
			HookJMPN(0x6576D2, 0x65790F);
			HookJMPN(0x64759E, 0x647652);
			HookJMPN(0x648BDD, 0x648BF8);
			HookJMP(0x505541, 0x505850);
			Nop(0x658BCB, 2);
			Nop(0x6562A7, 2);
			Nop(0x6564CD, 2);
		}
	}
	else
	{
		if(rendererOption == 5 || rendererOption == 6)
		{
			MessageBoxA(nullptr, "Failed to load DXVK - most likely your system don't support vulkan.", "Fatal Error", MB_ICONHAND);
			exit(-1);
		}
		else if(rendererOption == 3)
		{
			MessageBoxA(nullptr, "Failed to load Wine D3D.", "Fatal Error", MB_ICONHAND);
			exit(-1);
		}
		else
		{
			MessageBoxA(nullptr, "Failed to load DirectX9 library.", "Fatal Error", MB_ICONHAND);
			exit(-1);
		}
	}
}

bool TempVideoBuffer_Lock(unsigned char*& data, INT& pitch, UINT width, UINT height)
{
	HRESULT result = IDirect3DDevice9_CreateTexture(g_Direct3D9Device9, width, height, 1, 0, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &g_ManagedVideoBuffer, nullptr);
	if(FAILED(result))
		return false;

	D3DLOCKED_RECT locked;
	result = IDirect3DTexture9_LockRect(g_ManagedVideoBuffer, 0, &locked, nullptr, D3DLOCK_NOSYSLOCK);
	if(FAILED(result))
	{
		IDirect3DTexture9_Release(g_ManagedVideoBuffer);
		return false;
	}

	pitch = locked.Pitch;
	data = reinterpret_cast<unsigned char*>(locked.pBits);
	return true;
}

void TempVideoBuffer_Unlock(IDirectDrawSurface7* tex)
{
	HRESULT result = IDirect3DTexture9_UnlockRect(g_ManagedVideoBuffer, 0);
	if(FAILED(result))
	{
		IDirect3DTexture9_Release(g_ManagedVideoBuffer);
		return;
	}

	IDirect3DTexture9* texture = static_cast<MyDirectDrawSurface7*>(tex)->GetTexture();
	IDirect3DSurface9* destSurface;
	IDirect3DSurface9* stagingSurface;
	IDirect3DTexture9_GetSurfaceLevel(g_ManagedVideoBuffer, 0, &stagingSurface);
	IDirect3DTexture9_GetSurfaceLevel(texture, 0, &destSurface);
	IDirect3DDevice9_UpdateSurface(g_Direct3D9Device9, stagingSurface, nullptr, destSurface, nullptr);
	IDirect3DSurface9_Release(destSurface);
	IDirect3DSurface9_Release(stagingSurface);
	IDirect3DTexture9_Release(g_ManagedVideoBuffer);
}

void TempVideoBuffer_Discard()
{
	IDirect3DTexture9_Release(g_ManagedVideoBuffer);
}
