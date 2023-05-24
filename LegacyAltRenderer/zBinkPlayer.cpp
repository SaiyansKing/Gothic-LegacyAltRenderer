#include "hook.h"
#include "zSTRING.h"

#include <thread>
#include <string>
#include <ddraw.h>
#include <d3d.h>

extern bool IsG1;
extern bool IsG2;
extern int Direct3DDeviceCreated;

bool NewBinkSetVolume = true;
DWORD BinkOpenWaveOut;
DWORD BinkSetSoundSystem;
DWORD BinkSetSoundOnOff;
DWORD BinkSetVolume;
DWORD BinkOpen;
DWORD BinkDoFrame;
DWORD BinkNextFrame;
DWORD BinkWait;
DWORD BinkPause;
DWORD BinkClose;
DWORD BinkGoto;
DWORD BinkCopyToBuffer;
DWORD BinkSetFrameRate;
DWORD BinkSetSimulate;

inline DWORD UTIL_power_of_2(DWORD input)
{
	DWORD value = 1;
	while(value < input) value <<= 1;
	return value;
}

struct BinkVideo
{
	BinkVideo(void* vid) : vid(vid) {}

	void* vid = nullptr;

	unsigned char* textureData = nullptr;
	LPDIRECTDRAWSURFACE7 texture = nullptr;
	DWORD width = 0;
	DWORD height = 0;
	bool useBGRA = false;

	float scaleTU = 1.f;
	float scaleTV = 1.f;

	float globalVolume = 1.f;
	float videoVolume = 1.f;
	bool updateVolume = true;
	bool scaleVideo = true;
};

float BinkPlayerReadGlobalVolume(DWORD zCOption)
{
	if(IsG1) return reinterpret_cast<float(__thiscall*)(DWORD, DWORD, DWORD, float)>(0x45E370)(zCOption, 0x869190, 0x869270, 1.f);
	else if(IsG2) return reinterpret_cast<float(__thiscall*)(DWORD, DWORD, DWORD, float)>(0x463A60)(zCOption, 0x8CD380, 0x8CD49C, 1.f);
	return 1.f;
}

bool BinkPlayerReadScaleVideos(DWORD zCOption)
{
	if(IsG1) return reinterpret_cast<int(__thiscall*)(DWORD, DWORD, DWORD, int)>(0x45CB80)(zCOption, 0x869388, 0x82E6E4, 1);
	else if(IsG2) return reinterpret_cast<int(__thiscall*)(DWORD, DWORD, DWORD, int)>(0x462160)(zCOption, 0x8CD5F0, 0x8925D8, 1);
	return true;
}

int __fastcall BinkPlayerPlayHandleEvents_G1(DWORD BinkPlayer)
{
	reinterpret_cast<void(__cdecl*)(void)>(0x4F6AC0)();

	BinkVideo* video = *reinterpret_cast<BinkVideo**>(BinkPlayer + 0x30);
	if(*reinterpret_cast<int*>(BinkPlayer + 0x84)) return 0;
	if(!video) return 0;
	if(!(*reinterpret_cast<int*>(BinkPlayer + 0x40))) return 1;

	DWORD zInput = *reinterpret_cast<DWORD*>(0x86CCA0);
	WORD key = reinterpret_cast<WORD(__thiscall*)(DWORD, int, int)>(*reinterpret_cast<DWORD*>
        (*reinterpret_cast<DWORD*>(zInput) + 0x2C))(zInput, 0, 0);
	reinterpret_cast<void(__thiscall*)(DWORD)>(*reinterpret_cast<DWORD*>
        (*reinterpret_cast<DWORD*>(zInput) + 0x70))(zInput);
	switch(key)
	{
		case 0x01: // DIK_ESCAPE
			reinterpret_cast<void(__thiscall*)(DWORD)>(*reinterpret_cast<DWORD*>
                (*reinterpret_cast<DWORD*>(BinkPlayer) + 0x20))(BinkPlayer);
			break;
		case 0x39: // DIK_SPACE
        {
			if(*reinterpret_cast<int*>(BinkPlayer + 0x1C))
			{
				reinterpret_cast<void(__stdcall*)(void*, int)>(BinkPause)(video->vid, 0);
				*reinterpret_cast<int*>(BinkPlayer + 0x1C) = 0;
			}
			else
			{
				reinterpret_cast<void(__stdcall*)(void*, int)>(BinkPause)(video->vid, 1);
				*reinterpret_cast<int*>(BinkPlayer + 0x1C) = 1;
			}
		}
		break;
		case 0x10: // DIK_Q
        {
			if(*reinterpret_cast<int*>(BinkPlayer + 0x24))
			{
				video->globalVolume = 0.f;
				video->updateVolume = true;
				*reinterpret_cast<int*>(BinkPlayer + 0x24) = 0;
			}
			else
			{
                video->globalVolume = BinkPlayerReadGlobalVolume(*reinterpret_cast<DWORD*>(0x869694));
				video->updateVolume = true;
				*reinterpret_cast<int*>(BinkPlayer + 0x24) = 1;
			}
		}
		break;
		case 0xC8: // DIK_UP
        {
			video->videoVolume = std::min<float>(1.0f, video->videoVolume + 0.05f);
			video->updateVolume = true;
		}
		break;
		case 0xD0: // DIK_DOWN
        {
			video->videoVolume = std::max<float>(0.0f, video->videoVolume - 0.05f);
			video->updateVolume = true;
		}
		break;
	}
	return 1;
}

int __fastcall BinkPlayerPlayHandleEvents_G2(DWORD BinkPlayer)
{
	reinterpret_cast<void(__cdecl*)(void)>(0x5053E0)();

	BinkVideo* video = *reinterpret_cast<BinkVideo**>(BinkPlayer + 0x30);
	if(*reinterpret_cast<int*>(BinkPlayer + 0x84)) return 0;
	if(!video) return 0;
	if(!(*reinterpret_cast<int*>(BinkPlayer + 0x40))) return 1;

	DWORD zInput = *reinterpret_cast<DWORD*>(0x8D1650);
	WORD key = reinterpret_cast<WORD(__thiscall*)(DWORD, int, int)>(*reinterpret_cast<DWORD*>
        (*reinterpret_cast<DWORD*>(zInput) + 0x2C))(zInput, 0, 0);
	reinterpret_cast<void(__thiscall*)(DWORD)>(*reinterpret_cast<DWORD*>
        (*reinterpret_cast<DWORD*>(zInput) + 0x74))(zInput);
	switch(key)
	{
		case 0x01: // DIK_ESCAPE
			reinterpret_cast<void(__thiscall*)(DWORD)>(*reinterpret_cast<DWORD*>
                (*reinterpret_cast<DWORD*>(BinkPlayer) + 0x20))(BinkPlayer);
			break;
		case 0x39: // DIK_SPACE
        {
			if(*reinterpret_cast<int*>(BinkPlayer + 0x1C))
			{
				reinterpret_cast<void(__stdcall*)(void*, int)>(BinkPause)(video->vid, 0);
				*reinterpret_cast<int*>(BinkPlayer + 0x1C) = 0;
			}
			else
			{
				reinterpret_cast<void(__stdcall*)(void*, int)>(BinkPause)(video->vid, 1);
				*reinterpret_cast<int*>(BinkPlayer + 0x1C) = 1;
			}
		}
		break;
		case 0x10: // DIK_Q
        {
			if(*reinterpret_cast<int*>(BinkPlayer + 0x24))
			{
				video->globalVolume = 0.f;
				video->updateVolume = true;
				*reinterpret_cast<int*>(BinkPlayer + 0x24) = 0;
			}
			else
			{
                video->globalVolume = BinkPlayerReadGlobalVolume(*reinterpret_cast<DWORD*>(0x8CD988));
				video->updateVolume = true;
				*reinterpret_cast<int*>(BinkPlayer + 0x24) = 1;
			}
		}
		break;
		case 0xC8: // DIK_UP
        {
			video->videoVolume = std::min<float>(1.0f, video->videoVolume + 0.05f);
			video->updateVolume = true;
		}
		break;
		case 0xD0: // DIK_DOWN
        {
			video->videoVolume = std::max<float>(0.0f, video->videoVolume - 0.05f);
			video->updateVolume = true;
		}
		break;
	}
	return 1;
}

float __fastcall BinkPlayerSetSoundVolume(DWORD BinkPlayer, DWORD _EDX, float volume)
{
	BinkVideo* video = *reinterpret_cast<BinkVideo**>(BinkPlayer + 0x30);
	video->videoVolume = std::min<float>(1.0f, std::max<float>(video->videoVolume, 0.0f));
	video->updateVolume = true;
	return 1;
}

int __fastcall BinkPlayerToggleSound_G1(DWORD BinkPlayer)
{
	BinkVideo* video = *reinterpret_cast<BinkVideo**>(BinkPlayer + 0x30);
	if(*reinterpret_cast<int*>(BinkPlayer + 0x24))
	{
		video->globalVolume = 0.f;
		video->updateVolume = true;
		*reinterpret_cast<int*>(BinkPlayer + 0x24) = 0;
	}
	else
	{
        video->globalVolume = BinkPlayerReadGlobalVolume(*reinterpret_cast<DWORD*>(0x869694));
		video->updateVolume = true;
		*reinterpret_cast<int*>(BinkPlayer + 0x24) = 1;
	}
	return 1;
}

int __fastcall BinkPlayerToggleSound_G2(DWORD BinkPlayer)
{
	BinkVideo* video = *reinterpret_cast<BinkVideo**>(BinkPlayer + 0x30);
	if(*reinterpret_cast<int*>(BinkPlayer + 0x24))
	{
		video->globalVolume = 0.f;
		video->updateVolume = true;
		*reinterpret_cast<int*>(BinkPlayer + 0x24) = 0;
	}
	else
	{
        video->globalVolume = BinkPlayerReadGlobalVolume(*reinterpret_cast<DWORD*>(0x8CD988));
		video->updateVolume = true;
		*reinterpret_cast<int*>(BinkPlayer + 0x24) = 1;
	}
	return 1;
}

int __fastcall BinkPlayerPause(DWORD BinkPlayer)
{
	BinkVideo* video = *reinterpret_cast<BinkVideo**>(BinkPlayer + 0x30);
	reinterpret_cast<void(__stdcall*)(void*, int)>(BinkPause)(video->vid, 1);
	return 1;
}

int __fastcall BinkPlayerUnpause(DWORD BinkPlayer)
{
	BinkVideo* video = *reinterpret_cast<BinkVideo**>(BinkPlayer + 0x30);
	reinterpret_cast<void(__stdcall*)(void*, int)>(BinkPause)(video->vid, 0);
	return 1;
}

int __fastcall BinkPlayerIsPlaying(DWORD BinkPlayer)
{
	BinkVideo* video = *reinterpret_cast<BinkVideo**>(BinkPlayer + 0x30);
	if(video &&
		(*reinterpret_cast<int*>(BinkPlayer + 0x20)) &&
		((*reinterpret_cast<int*>(BinkPlayer + 0x18)) ||
		*reinterpret_cast<DWORD*>(reinterpret_cast<DWORD>(video->vid) + 0x08) > *reinterpret_cast<DWORD*>(reinterpret_cast<DWORD>(video->vid) + 0x0C)))
		return 1;

	return 0;
}

int __fastcall BinkPlayerPlayGotoNextFrame(DWORD BinkPlayer)
{
	BinkVideo* video = *reinterpret_cast<BinkVideo**>(BinkPlayer + 0x30);
	reinterpret_cast<void(__stdcall*)(void*)>(BinkNextFrame)(video->vid);
	return 1;
}

int __fastcall BinkPlayerPlayWaitNextFrame(DWORD BinkPlayer)
{
	BinkVideo* video = *reinterpret_cast<BinkVideo**>(BinkPlayer + 0x30);
	while(BinkPlayerIsPlaying(BinkPlayer) && reinterpret_cast<int(__stdcall*)(void*)>(BinkWait)(video->vid))
    {
		if(IsG1) BinkPlayerPlayHandleEvents_G1(BinkPlayer);
		else if(IsG2) BinkPlayerPlayHandleEvents_G2(BinkPlayer);
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	return 1;
}

int __fastcall BinkPlayerPlayDoFrame(DWORD BinkPlayer)
{
	BinkVideo* video = *reinterpret_cast<BinkVideo**>(BinkPlayer + 0x30);
	if(video->updateVolume)
    {
		float volume = video->globalVolume * video->videoVolume;
		//if(NewBinkSetVolume) reinterpret_cast<void(__stdcall*)(void*, int, DWORD)>(BinkSetVolume)(video->vid, 0, static_cast<DWORD>(volume * 65536.f));
		//else reinterpret_cast<void(__stdcall*)(void*, DWORD)>(BinkSetVolume)(video->vid, static_cast<DWORD>(volume * 65536.f));
		video->updateVolume = false;
	}
	reinterpret_cast<void(__stdcall*)(void*)>(BinkDoFrame)(video->vid);
	return 1;
}

int __fastcall BinkPlayerPlayFrame_G1(DWORD BinkPlayer)
{
	BinkVideo* video = *reinterpret_cast<BinkVideo**>(BinkPlayer + 0x30);
	if(BinkPlayerIsPlaying(BinkPlayer))
	{
		BinkPlayerPlayHandleEvents_G1(BinkPlayer);
		if(BinkPlayerIsPlaying(BinkPlayer))
		{
            BinkPlayerPlayDoFrame(BinkPlayer);
			{
				DWORD vidWidth = *reinterpret_cast<DWORD*>(reinterpret_cast<DWORD>(video->vid) + 0x00);
				DWORD vidHeight = *reinterpret_cast<DWORD*>(reinterpret_cast<DWORD>(video->vid) + 0x04);
				if(!video->texture || video->width != vidWidth || video->height != vidHeight)
				{
					video->useBGRA = true;
					video->width = vidWidth;
					video->height = vidHeight;
					if(video->texture)
					{
						video->texture->Release();
						video->texture = nullptr;
					}
					video->textureData = new unsigned char[vidWidth * vidHeight * 4];

					DDSURFACEDESC2 ddsd;
					ZeroMemory(&ddsd, sizeof(ddsd));
					ddsd.dwSize = sizeof(ddsd);
					ddsd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
					ddsd.ddsCaps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_VIDEOMEMORY;
					ddsd.ddsCaps.dwCaps2 = DDSCAPS2_HINTDYNAMIC;
					if(Direct3DDeviceCreated)
					{
						ddsd.dwWidth = vidWidth;
						ddsd.dwHeight = vidHeight;
					}
					else
					{
						ddsd.dwWidth = UTIL_power_of_2(vidWidth);
						ddsd.dwHeight = UTIL_power_of_2(vidHeight);
					}
					ddsd.ddpfPixelFormat.dwSize = sizeof(ddsd.ddpfPixelFormat);
					ddsd.ddpfPixelFormat.dwFlags = DDPF_RGB | DDPF_ALPHAPIXELS;
					ddsd.ddpfPixelFormat.dwRGBBitCount = 32;
					ddsd.ddpfPixelFormat.dwRBitMask = 0x00FF0000;
					ddsd.ddpfPixelFormat.dwGBitMask = 0x0000FF00;
					ddsd.ddpfPixelFormat.dwBBitMask = 0x000000FF;
					ddsd.ddpfPixelFormat.dwRGBAlphaBitMask = 0xFF000000;
					HRESULT hr = (*reinterpret_cast<LPDIRECTDRAW7*>(0x929D54))->CreateSurface(&ddsd, &video->texture, nullptr);
					if(FAILED(hr))
					{
						*reinterpret_cast<int*>(BinkPlayer + 0x20) = 0;
						return 0;
					}

					video->scaleTU = 1.0f / ddsd.dwWidth;
					video->scaleTV = 1.0f / ddsd.dwHeight;
				}
				
				if(Direct3DDeviceCreated)
				{
					bool TempVideoBuffer_Lock(unsigned char*& data, INT & pitch, UINT width, UINT height);
					void TempVideoBuffer_Unlock(IDirectDrawSurface7 * tex);
					void TempVideoBuffer_Discard();
					if(video->texture->IsLost() == DDERR_SURFACELOST)
						video->texture->Restore();

					unsigned char* videoBuffer;
					INT srcPitch = vidWidth * 4;
					if(TempVideoBuffer_Lock(videoBuffer, srcPitch, vidWidth, vidHeight))
					{
						int res = reinterpret_cast<int(__stdcall*)(void*, void*, int, DWORD, DWORD, DWORD, DWORD)>(BinkCopyToBuffer)
							(video->vid, videoBuffer, srcPitch, vidHeight, 0, 0, (video->useBGRA ? 3 : 4) | 0x80000000L);
						if(!res)
							TempVideoBuffer_Unlock(video->texture);
						else
							TempVideoBuffer_Discard();
					}
					else
					{
						*reinterpret_cast<int*>(BinkPlayer + 0x20) = 0;
						return 0;
					}
				}
				else
				{
					int srcPitch = vidWidth * 4;
					reinterpret_cast<void(__stdcall*)(void*, void*, int, DWORD, DWORD, DWORD, DWORD)>(BinkCopyToBuffer)
						(video->vid, video->textureData, vidWidth * 4, vidHeight, 0, 0, (video->useBGRA ? 3 : 4));
					if(video->texture->IsLost() == DDERR_SURFACELOST)
						video->texture->Restore();

					DDSURFACEDESC2 ddsd;
					ZeroMemory(&ddsd, sizeof(ddsd));
					ddsd.dwSize = sizeof(ddsd);
					HRESULT hr = video->texture->Lock(nullptr, &ddsd, DDLOCK_NOSYSLOCK | DDLOCK_WAIT | DDLOCK_WRITEONLY, nullptr);
					if(FAILED(hr))
					{
						*reinterpret_cast<int*>(BinkPlayer + 0x20) = 0;
						return 0;
					}

					if(ddsd.lPitch == srcPitch)
						memcpy(ddsd.lpSurface, video->textureData, srcPitch * vidHeight);
					else
					{
						unsigned char* dstData = reinterpret_cast<unsigned char*>(ddsd.lpSurface);
						unsigned char* srcData = video->textureData;
						for(DWORD h = 0; h < vidHeight; ++h)
						{
							memcpy(dstData, srcData, srcPitch);
							dstData += ddsd.lPitch;
							srcData += srcPitch;
						}
					}
					video->texture->Unlock(nullptr);
				}

				DWORD zrenderer = *reinterpret_cast<DWORD*>(0x8C5ED0);
				int oldZWrite = reinterpret_cast<int(__thiscall*)(DWORD)>(*reinterpret_cast<DWORD*>
                    (*reinterpret_cast<DWORD*>(zrenderer) + 0x68))(zrenderer);
				reinterpret_cast<void(__thiscall*)(DWORD, int)>(*reinterpret_cast<DWORD*>
                    (*reinterpret_cast<DWORD*>(zrenderer) + 0x6C))(zrenderer, 0); // No depth-writes
				int oldZCompare = reinterpret_cast<int(__thiscall*)(DWORD)>(*reinterpret_cast<DWORD*>
                    (*reinterpret_cast<DWORD*>(zrenderer) + 0x70))(zrenderer);
				int newZCompare = 0; // Compare always
				reinterpret_cast<void(__thiscall*)(DWORD, int&)>(*reinterpret_cast<DWORD*>
                    (*reinterpret_cast<DWORD*>(zrenderer) + 0x74))(zrenderer, newZCompare);
				int oldAlphaFunc = reinterpret_cast<int(__thiscall*)(DWORD)>(*reinterpret_cast<DWORD*>
                    (*reinterpret_cast<DWORD*>(zrenderer) + 0x8C))(zrenderer);
				int oldFilter = reinterpret_cast<int(__thiscall*)(DWORD)>(*reinterpret_cast<DWORD*>
                    (*reinterpret_cast<DWORD*>(zrenderer) + 0x54))(zrenderer);
				reinterpret_cast<void(__thiscall*)(DWORD, int)>(*reinterpret_cast<DWORD*>
                    (*reinterpret_cast<DWORD*>(zrenderer) + 0x50))(zrenderer, video->scaleVideo ? 1 : 0); // Bilinear filter
				int oldFog = reinterpret_cast<int(__thiscall*)(DWORD)>(*reinterpret_cast<DWORD*>
                    (*reinterpret_cast<DWORD*>(zrenderer) + 0x2C))(zrenderer);
				reinterpret_cast<void(__thiscall*)(DWORD, int)>(*reinterpret_cast<DWORD*>
                    (*reinterpret_cast<DWORD*>(zrenderer) + 0x28))(zrenderer, 0); // No fog

                DWORD SetTextureStageState = *reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zrenderer) + 0x148);
				// Disable alpha blending
				reinterpret_cast<void(__thiscall*)(DWORD, int, int)>(0x7185C0)(zrenderer, 26, 0);
				reinterpret_cast<void(__thiscall*)(DWORD, int, int)>(0x7185C0)(zrenderer, 27, 0);
				reinterpret_cast<void(__thiscall*)(DWORD, int, int)>(0x7185C0)(zrenderer, 15, 0);
				// Disable clipping
				reinterpret_cast<void(__thiscall*)(DWORD, int, int)>(0x7185C0)(zrenderer, 136, 0);
				// Disable culling
				reinterpret_cast<void(__thiscall*)(DWORD, int, int)>(0x7185C0)(zrenderer, 22, 1);
				// Set texture clamping
				reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zrenderer, 0, 12, 3);
				reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zrenderer, 0, 13, 3);
				// 0 stage AlphaOp modulate
				reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zrenderer, 0, 3, 3);
				// 1 stage AlphaOp disable
				reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zrenderer, 1, 3, 0);
				// 0 stage ColorOp modulate
				reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zrenderer, 0, 0, 3);
				// 1 stage ColorOp disable
				reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zrenderer, 1, 0, 0);
				// 0 stage AlphaArg1/2 texure/diffuse
				reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zrenderer, 0, 4, 3);
				reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zrenderer, 0, 5, 1);
				// 0 stage ColorArg1/2 texure/diffuse
				reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zrenderer, 0, 1, 3);
				reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zrenderer, 0, 2, 1);
				// 0 stage TextureTransformFlags disable
				reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zrenderer, 0, 23, 0);
				// 0 stage TexCoordIndex 0
				reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zrenderer, 0, 10, 0);

				// Set fullscreen viewport
				int gWidth = *reinterpret_cast<int*>(zrenderer + 0x984);
				int gHeight = *reinterpret_cast<int*>(zrenderer + 0x988);
				reinterpret_cast<void(__thiscall*)(DWORD, int, int, int, int)>(*reinterpret_cast<DWORD*>
                    (*reinterpret_cast<DWORD*>(zrenderer) + 0x134))(zrenderer, 0, 0, gWidth, gHeight);

				// Set video texture
				reinterpret_cast<void(__thiscall*)(DWORD, int, LPDIRECTDRAWSURFACE7)>(0x718150)(zrenderer, 0, video->texture);
				*reinterpret_cast<DWORD*>(zrenderer + 0x80E38 + (/*TEX0*/0 * 4)) = 0x00000000;

				{
					struct D3DTLVERTEX
					{
						float sx;
						float sy;
						float sz;
						float rhw;
						DWORD color;
						DWORD specular;
						float tu;
						float tv;
					};
					D3DTLVERTEX vertices[8];

					float scale = std::min<float>(static_cast<float>(gWidth) / static_cast<float>(video->width), static_cast<float>(gHeight) / static_cast<float>(video->height));
					int dstW = std::min<int>(static_cast<int>(video->width * scale), gWidth);
					int dstH = std::min<int>(static_cast<int>(video->height * scale), gHeight);
					int dstX = std::max<int>((gWidth / 2) - (dstW / 2), 0);
					int dstY = std::max<int>((gHeight / 2) - (dstH / 2), 0);
					if(!video->scaleVideo)
					{
						dstX = (gWidth / 2) - (video->width / 2);
						dstY = (gHeight / 2) - (video->height / 2);
						dstW = video->width;
						dstH = video->height;
					}

					float minx = static_cast<float>(dstX) - 0.5f;
					float miny = static_cast<float>(dstY) - 0.5f;
					float maxx = static_cast<float>(dstW) + minx;
					float maxy = static_cast<float>(dstH) + miny;

					float minu = 0.f;
					float maxu = static_cast<float>(video->width) * video->scaleTU;
					float minv = 0.f;
					float maxv = static_cast<float>(video->height) * video->scaleTV;

					vertices[0].sx = -0.5f;
					vertices[0].sy = -0.5f;
					vertices[0].sz = 0.f;
					vertices[0].rhw = 1.f;
					vertices[0].color = 0x00000000;
					vertices[0].specular = 0x00000000;
					vertices[0].tu = 0.f;
					vertices[0].tv = 0.f;

					vertices[1].sx = static_cast<float>(gWidth) - 0.5f;
					vertices[1].sy = -0.5f;
					vertices[1].sz = 0.f;
					vertices[1].rhw = 1.f;
					vertices[1].color = 0x00000000;
					vertices[1].specular = 0x00000000;
					vertices[1].tu = 0.f;
					vertices[1].tv = 0.f;

					vertices[2].sx = static_cast<float>(gWidth) - 0.5f;
					vertices[2].sy = static_cast<float>(gHeight) - 0.5f;
					vertices[2].sz = 0.f;
					vertices[2].rhw = 1.f;
					vertices[2].color = 0x00000000;
					vertices[2].specular = 0x00000000;
					vertices[2].tu = 0.f;
					vertices[2].tv = 0.f;

					vertices[3].sx = -0.5f;
					vertices[3].sy = static_cast<float>(gHeight) - 0.5f;
					vertices[3].sz = 0.f;
					vertices[3].rhw = 1.f;
					vertices[3].color = 0x00000000;
					vertices[3].specular = 0x00000000;
					vertices[3].tu = 0.f;
					vertices[3].tv = 0.f;

					vertices[4].sx = minx;
					vertices[4].sy = miny;
					vertices[4].sz = 0.f;
					vertices[4].rhw = 1.f;
					vertices[4].color = 0xFFFFFFFF;
					vertices[4].specular = 0xFFFFFFFF;
					vertices[4].tu = minu;
					vertices[4].tv = minv;

					vertices[5].sx = maxx;
					vertices[5].sy = miny;
					vertices[5].sz = 0.f;
					vertices[5].rhw = 1.f;
					vertices[5].color = 0xFFFFFFFF;
					vertices[5].specular = 0xFFFFFFFF;
					vertices[5].tu = maxu;
					vertices[5].tv = minv;

					vertices[6].sx = maxx;
					vertices[6].sy = maxy;
					vertices[6].sz = 0.f;
					vertices[6].rhw = 1.f;
					vertices[6].color = 0xFFFFFFFF;
					vertices[6].specular = 0xFFFFFFFF;
					vertices[6].tu = maxu;
					vertices[6].tv = maxv;

					vertices[7].sx = minx;
					vertices[7].sy = maxy;
					vertices[7].sz = 0.f;
					vertices[7].rhw = 1.f;
					vertices[7].color = 0xFFFFFFFF;
					vertices[7].specular = 0xFFFFFFFF;
					vertices[7].tu = minu;
					vertices[7].tv = maxv;

					LPDIRECT3DDEVICE7 d3d7Device = *reinterpret_cast<LPDIRECT3DDEVICE7*>(0x929D5C);
					d3d7Device->DrawPrimitive(D3DPT_TRIANGLEFAN, D3DFVF_TLVERTEX, reinterpret_cast<LPVOID>(vertices + 0), 4, 0);
					d3d7Device->DrawPrimitive(D3DPT_TRIANGLEFAN, D3DFVF_TLVERTEX, reinterpret_cast<LPVOID>(vertices + 4), 4, 0);
				}

				reinterpret_cast<void(__thiscall*)(DWORD, int)>(*reinterpret_cast<DWORD*>
                    (*reinterpret_cast<DWORD*>(zrenderer) + 0x28))(zrenderer, oldFog);
				reinterpret_cast<void(__thiscall*)(DWORD, int)>(*reinterpret_cast<DWORD*>
                    (*reinterpret_cast<DWORD*>(zrenderer) + 0x50))(zrenderer, oldFilter);
				reinterpret_cast<void(__thiscall*)(DWORD, int&)>(*reinterpret_cast<DWORD*>
                    (*reinterpret_cast<DWORD*>(zrenderer) + 0x88))(zrenderer, oldAlphaFunc);
				reinterpret_cast<void(__thiscall*)(DWORD, int&)>(*reinterpret_cast<DWORD*>
                    (*reinterpret_cast<DWORD*>(zrenderer) + 0x74))(zrenderer, oldZCompare);
				reinterpret_cast<void(__thiscall*)(DWORD, int)>(*reinterpret_cast<DWORD*>
                    (*reinterpret_cast<DWORD*>(zrenderer) + 0x6C))(zrenderer, oldZWrite);
				reinterpret_cast<void(__thiscall*)(DWORD, int, void*, void*)>(*reinterpret_cast<DWORD*>
                    (*reinterpret_cast<DWORD*>(zrenderer) + 0xD8))(zrenderer, 0, nullptr, nullptr);
			}
            BinkPlayerPlayGotoNextFrame(BinkPlayer);
            BinkPlayerPlayWaitNextFrame(BinkPlayer);
		}
	}
	return 1;
}

int __fastcall BinkPlayerPlayFrame_G2(DWORD BinkPlayer)
{
	BinkVideo* video = *reinterpret_cast<BinkVideo**>(BinkPlayer + 0x30);
	if(BinkPlayerIsPlaying(BinkPlayer))
	{
		BinkPlayerPlayHandleEvents_G2(BinkPlayer);
		if(BinkPlayerIsPlaying(BinkPlayer))
		{
            BinkPlayerPlayDoFrame(BinkPlayer);
			{
				DWORD vidWidth = *reinterpret_cast<DWORD*>(reinterpret_cast<DWORD>(video->vid) + 0x00);
				DWORD vidHeight = *reinterpret_cast<DWORD*>(reinterpret_cast<DWORD>(video->vid) + 0x04);
				if(!video->texture || video->width != vidWidth || video->height != vidHeight)
				{
					video->useBGRA = true;
					video->width = vidWidth;
					video->height = vidHeight;
					if(video->texture)
					{
						video->texture->Release();
						video->texture = nullptr;
					}
					video->textureData = new unsigned char[vidWidth * vidHeight * 4];

					DDSURFACEDESC2 ddsd;
					ZeroMemory(&ddsd, sizeof(ddsd));
					ddsd.dwSize = sizeof(ddsd);
					ddsd.dwFlags = DDSD_CAPS | DDSD_WIDTH | DDSD_HEIGHT | DDSD_PIXELFORMAT;
					ddsd.ddsCaps.dwCaps = DDSCAPS_TEXTURE | DDSCAPS_VIDEOMEMORY;
					ddsd.ddsCaps.dwCaps2 = DDSCAPS2_HINTDYNAMIC;
					if(Direct3DDeviceCreated)
					{
						ddsd.dwWidth = vidWidth;
						ddsd.dwHeight = vidHeight;
					}
					else
					{
						ddsd.dwWidth = UTIL_power_of_2(vidWidth);
						ddsd.dwHeight = UTIL_power_of_2(vidHeight);
					}
					ddsd.ddpfPixelFormat.dwSize = sizeof(ddsd.ddpfPixelFormat);
					ddsd.ddpfPixelFormat.dwFlags = DDPF_RGB | DDPF_ALPHAPIXELS;
					ddsd.ddpfPixelFormat.dwRGBBitCount = 32;
					ddsd.ddpfPixelFormat.dwRBitMask = 0x00FF0000;
					ddsd.ddpfPixelFormat.dwGBitMask = 0x0000FF00;
					ddsd.ddpfPixelFormat.dwBBitMask = 0x000000FF;
					ddsd.ddpfPixelFormat.dwRGBAlphaBitMask = 0xFF000000;
					HRESULT hr = (*reinterpret_cast<LPDIRECTDRAW7*>(0x9FC9EC))->CreateSurface(&ddsd, &video->texture, nullptr);
					if(FAILED(hr))
					{
						*reinterpret_cast<int*>(BinkPlayer + 0x20) = 0;
						return 0;
					}

					video->scaleTU = 1.0f / ddsd.dwWidth;
					video->scaleTV = 1.0f / ddsd.dwHeight;
				}

				if(Direct3DDeviceCreated)
				{
					bool TempVideoBuffer_Lock(unsigned char*& data, INT & pitch, UINT width, UINT height);
					void TempVideoBuffer_Unlock(IDirectDrawSurface7 * tex);
					void TempVideoBuffer_Discard();
					if(video->texture->IsLost() == DDERR_SURFACELOST)
						video->texture->Restore();

					unsigned char* videoBuffer;
					INT srcPitch = vidWidth * 4;
					if(TempVideoBuffer_Lock(videoBuffer, srcPitch, vidWidth, vidHeight))
					{
						int res = reinterpret_cast<int(__stdcall*)(void*, void*, int, DWORD, DWORD, DWORD, DWORD)>(BinkCopyToBuffer)
							(video->vid, videoBuffer, srcPitch, vidHeight, 0, 0, (video->useBGRA ? 3 : 4) | 0x80000000L);
						if(!res)
							TempVideoBuffer_Unlock(video->texture);
						else
							TempVideoBuffer_Discard();
					}
					else
					{
						*reinterpret_cast<int*>(BinkPlayer + 0x20) = 0;
						return 0;
					}
				}
				else
				{
					int srcPitch = vidWidth * 4;
					reinterpret_cast<void(__stdcall*)(void*, void*, int, DWORD, DWORD, DWORD, DWORD)>(BinkCopyToBuffer)
						(video->vid, video->textureData, vidWidth * 4, vidHeight, 0, 0, (video->useBGRA ? 3 : 4));
					if(video->texture->IsLost() == DDERR_SURFACELOST)
						video->texture->Restore();

					DDSURFACEDESC2 ddsd;
					ZeroMemory(&ddsd, sizeof(ddsd));
					ddsd.dwSize = sizeof(ddsd);
					HRESULT hr = video->texture->Lock(nullptr, &ddsd, DDLOCK_NOSYSLOCK | DDLOCK_WAIT | DDLOCK_WRITEONLY, nullptr);
					if(FAILED(hr))
					{
						*reinterpret_cast<int*>(BinkPlayer + 0x20) = 0;
						return 0;
					}

					if(ddsd.lPitch == srcPitch)
						memcpy(ddsd.lpSurface, video->textureData, srcPitch * vidHeight);
					else
					{
						unsigned char* dstData = reinterpret_cast<unsigned char*>(ddsd.lpSurface);
						unsigned char* srcData = video->textureData;
						for(DWORD h = 0; h < vidHeight; ++h)
						{
							memcpy(dstData, srcData, srcPitch);
							dstData += ddsd.lPitch;
							srcData += srcPitch;
						}
					}
					video->texture->Unlock(nullptr);
				}

				DWORD zrenderer = *reinterpret_cast<DWORD*>(0x982F08);
				int oldZWrite = reinterpret_cast<int(__thiscall*)(DWORD)>(*reinterpret_cast<DWORD*>
                    (*reinterpret_cast<DWORD*>(zrenderer) + 0x80))(zrenderer);
				reinterpret_cast<void(__thiscall*)(DWORD, int)>(*reinterpret_cast<DWORD*>
                    (*reinterpret_cast<DWORD*>(zrenderer) + 0x84))(zrenderer, 0); // No depth-writes
				int oldZCompare = reinterpret_cast<int(__thiscall*)(DWORD)>(*reinterpret_cast<DWORD*>
                    (*reinterpret_cast<DWORD*>(zrenderer) + 0x90))(zrenderer);
				int newZCompare = 0; // Compare always
				reinterpret_cast<void(__thiscall*)(DWORD, int&)>(*reinterpret_cast<DWORD*>
                    (*reinterpret_cast<DWORD*>(zrenderer) + 0x94))(zrenderer, newZCompare);
				int oldAlphaFunc = reinterpret_cast<int(__thiscall*)(DWORD)>(*reinterpret_cast<DWORD*>
                    (*reinterpret_cast<DWORD*>(zrenderer) + 0xAC))(zrenderer);
				int oldFilter = reinterpret_cast<int(__thiscall*)(DWORD)>(*reinterpret_cast<DWORD*>
                    (*reinterpret_cast<DWORD*>(zrenderer) + 0x6C))(zrenderer);
				reinterpret_cast<void(__thiscall*)(DWORD, int)>(*reinterpret_cast<DWORD*>
                    (*reinterpret_cast<DWORD*>(zrenderer) + 0x68))(zrenderer, video->scaleVideo ? 1 : 0); // Bilinear filter
				int oldFog = reinterpret_cast<int(__thiscall*)(DWORD)>(*reinterpret_cast<DWORD*>
                    (*reinterpret_cast<DWORD*>(zrenderer) + 0x2C))(zrenderer);
				reinterpret_cast<void(__thiscall*)(DWORD, int)>(*reinterpret_cast<DWORD*>
                    (*reinterpret_cast<DWORD*>(zrenderer) + 0x28))(zrenderer, 0); // No fog

                DWORD SetTextureStageState = *reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(zrenderer) + 0x17C);
				// Disable alpha blending
				reinterpret_cast<void(__thiscall*)(DWORD, int, int)>(0x644EF0)(zrenderer, 26, 0);
				reinterpret_cast<void(__thiscall*)(DWORD, int, int)>(0x644EF0)(zrenderer, 27, 0);
				reinterpret_cast<void(__thiscall*)(DWORD, int, int)>(0x644EF0)(zrenderer, 15, 0);
				// Disable clipping
				reinterpret_cast<void(__thiscall*)(DWORD, int, int)>(0x644EF0)(zrenderer, 136, 0);
				// Disable culling
				reinterpret_cast<void(__thiscall*)(DWORD, int, int)>(0x644EF0)(zrenderer, 22, 1);
				// Set texture clamping
				reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zrenderer, 0, 12, 3);
				reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zrenderer, 0, 13, 3);
				// 0 stage AlphaOp modulate
				reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zrenderer, 0, 3, 3);
				// 1 stage AlphaOp disable
				reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zrenderer, 1, 3, 0);
				// 0 stage ColorOp modulate
				reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zrenderer, 0, 0, 3);
				// 1 stage ColorOp disable
				reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zrenderer, 1, 0, 0);
				// 0 stage AlphaArg1/2 texure/diffuse
				reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zrenderer, 0, 4, 3);
				reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zrenderer, 0, 5, 1);
				// 0 stage ColorArg1/2 texure/diffuse
				reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zrenderer, 0, 1, 3);
				reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zrenderer, 0, 2, 1);
				// 0 stage TextureTransformFlags disable
				reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zrenderer, 0, 23, 0);
				// 0 stage TexCoordIndex 0
				reinterpret_cast<void(__thiscall*)(DWORD, int, int, int)>(SetTextureStageState)(zrenderer, 0, 10, 0);

				// Set fullscreen viewport
				int gWidth = *reinterpret_cast<int*>(zrenderer + 0x98C);
				int gHeight = *reinterpret_cast<int*>(zrenderer + 0x990);
				reinterpret_cast<void(__thiscall*)(DWORD, int, int, int, int)>(*reinterpret_cast<DWORD*>
                    (*reinterpret_cast<DWORD*>(zrenderer) + 0x168))(zrenderer, 0, 0, gWidth, gHeight);

				// Set video texture
				reinterpret_cast<void(__thiscall*)(DWORD, int, LPDIRECTDRAWSURFACE7)>(0x650500)(zrenderer, 0, video->texture);
				*reinterpret_cast<DWORD*>(zrenderer + 0x82E50 + (/*TEX0*/0 * 4)) = 0x00000000;

				{
					struct D3DTLVERTEX
					{
						float sx;
						float sy;
						float sz;
						float rhw;
						DWORD color;
						DWORD specular;
						float tu;
						float tv;
					};
					D3DTLVERTEX vertices[8];

					float scale = std::min<float>(static_cast<float>(gWidth) / static_cast<float>(video->width), static_cast<float>(gHeight) / static_cast<float>(video->height));
					int dstW = std::min<int>(static_cast<int>(video->width * scale), gWidth);
					int dstH = std::min<int>(static_cast<int>(video->height * scale), gHeight);
					int dstX = std::max<int>((gWidth / 2) - (dstW / 2), 0);
					int dstY = std::max<int>((gHeight / 2) - (dstH / 2), 0);
					if(!video->scaleVideo)
					{
						dstX = (gWidth / 2) - (video->width / 2);
						dstY = (gHeight / 2) - (video->height / 2);
						dstW = video->width;
						dstH = video->height;
					}

					float minx = static_cast<float>(dstX) - 0.5f;
					float miny = static_cast<float>(dstY) - 0.5f;
					float maxx = static_cast<float>(dstW) + minx;
					float maxy = static_cast<float>(dstH) + miny;

					float minu = 0.f;
					float maxu = static_cast<float>(video->width) * video->scaleTU;
					float minv = 0.f;
					float maxv = static_cast<float>(video->height) * video->scaleTV;

					vertices[0].sx = -0.5f;
					vertices[0].sy = -0.5f;
					vertices[0].sz = 0.f;
					vertices[0].rhw = 1.f;
					vertices[0].color = 0x00000000;
					vertices[0].specular = 0x00000000;
					vertices[0].tu = 0.f;
					vertices[0].tv = 0.f;

					vertices[1].sx = static_cast<float>(gWidth) - 0.5f;
					vertices[1].sy = -0.5f;
					vertices[1].sz = 0.f;
					vertices[1].rhw = 1.f;
					vertices[1].color = 0x00000000;
					vertices[1].specular = 0x00000000;
					vertices[1].tu = 0.f;
					vertices[1].tv = 0.f;

					vertices[2].sx = static_cast<float>(gWidth) - 0.5f;
					vertices[2].sy = static_cast<float>(gHeight) - 0.5f;
					vertices[2].sz = 0.f;
					vertices[2].rhw = 1.f;
					vertices[2].color = 0x00000000;
					vertices[2].specular = 0x00000000;
					vertices[2].tu = 0.f;
					vertices[2].tv = 0.f;

					vertices[3].sx = -0.5f;
					vertices[3].sy = static_cast<float>(gHeight) - 0.5f;
					vertices[3].sz = 0.f;
					vertices[3].rhw = 1.f;
					vertices[3].color = 0x00000000;
					vertices[3].specular = 0x00000000;
					vertices[3].tu = 0.f;
					vertices[3].tv = 0.f;

					vertices[4].sx = minx;
					vertices[4].sy = miny;
					vertices[4].sz = 0.f;
					vertices[4].rhw = 1.f;
					vertices[4].color = 0xFFFFFFFF;
					vertices[4].specular = 0xFFFFFFFF;
					vertices[4].tu = minu;
					vertices[4].tv = minv;

					vertices[5].sx = maxx;
					vertices[5].sy = miny;
					vertices[5].sz = 0.f;
					vertices[5].rhw = 1.f;
					vertices[5].color = 0xFFFFFFFF;
					vertices[5].specular = 0xFFFFFFFF;
					vertices[5].tu = maxu;
					vertices[5].tv = minv;

					vertices[6].sx = maxx;
					vertices[6].sy = maxy;
					vertices[6].sz = 0.f;
					vertices[6].rhw = 1.f;
					vertices[6].color = 0xFFFFFFFF;
					vertices[6].specular = 0xFFFFFFFF;
					vertices[6].tu = maxu;
					vertices[6].tv = maxv;

					vertices[7].sx = minx;
					vertices[7].sy = maxy;
					vertices[7].sz = 0.f;
					vertices[7].rhw = 1.f;
					vertices[7].color = 0xFFFFFFFF;
					vertices[7].specular = 0xFFFFFFFF;
					vertices[7].tu = minu;
					vertices[7].tv = maxv;

					LPDIRECT3DDEVICE7 d3d7Device = *reinterpret_cast<LPDIRECT3DDEVICE7*>(0x9FC9F4);
					d3d7Device->DrawPrimitive(D3DPT_TRIANGLEFAN, D3DFVF_TLVERTEX, reinterpret_cast<LPVOID>(vertices + 0), 4, 0);
					d3d7Device->DrawPrimitive(D3DPT_TRIANGLEFAN, D3DFVF_TLVERTEX, reinterpret_cast<LPVOID>(vertices + 4), 4, 0);
				}

				reinterpret_cast<void(__thiscall*)(DWORD, int)>(*reinterpret_cast<DWORD*>
                    (*reinterpret_cast<DWORD*>(zrenderer) + 0x28))(zrenderer, oldFog);
				reinterpret_cast<void(__thiscall*)(DWORD, int)>(*reinterpret_cast<DWORD*>
                    (*reinterpret_cast<DWORD*>(zrenderer) + 0x68))(zrenderer, oldFilter);
				reinterpret_cast<void(__thiscall*)(DWORD, int&)>(*reinterpret_cast<DWORD*>
                    (*reinterpret_cast<DWORD*>(zrenderer) + 0xA8))(zrenderer, oldAlphaFunc);
				reinterpret_cast<void(__thiscall*)(DWORD, int&)>(*reinterpret_cast<DWORD*>
                    (*reinterpret_cast<DWORD*>(zrenderer) + 0x94))(zrenderer, oldZCompare);
				reinterpret_cast<void(__thiscall*)(DWORD, int)>(*reinterpret_cast<DWORD*>
                    (*reinterpret_cast<DWORD*>(zrenderer) + 0x84))(zrenderer, oldZWrite);
				reinterpret_cast<void(__thiscall*)(DWORD, int, void*, void*)>(*reinterpret_cast<DWORD*>
                    (*reinterpret_cast<DWORD*>(zrenderer) + 0x10C))(zrenderer, 0, nullptr, nullptr);
			}
            BinkPlayerPlayGotoNextFrame(BinkPlayer);
            BinkPlayerPlayWaitNextFrame(BinkPlayer);
		}
	}
	return 1;
}

int __fastcall BinkPlayerPlayInit_G1(DWORD BinkPlayer, DWORD _EDX, int frame)
{
	BinkVideo* video = *reinterpret_cast<BinkVideo**>(BinkPlayer + 0x30);
	if(!video)
		return 0;

    if(!reinterpret_cast<int(__thiscall*)(DWORD, int)>(0x469600)(BinkPlayer, frame))
    {
        *reinterpret_cast<int*>(BinkPlayer + 0x20) = 0;
        return 1;
    }

	if(frame > 0)
		reinterpret_cast<void(__stdcall*)(void*, int, int)>(BinkGoto)(video->vid, frame, 0);

	return 1;
}

int __fastcall BinkPlayerPlayInit_G2(DWORD BinkPlayer, DWORD _EDX, int frame)
{
	BinkVideo* video = *reinterpret_cast<BinkVideo**>(BinkPlayer + 0x30);
	if(!video)
		return 0;

    if(!reinterpret_cast<int(__thiscall*)(DWORD, int)>(0x46E8E0)(BinkPlayer, frame))
    {
        *reinterpret_cast<int*>(BinkPlayer + 0x20) = 0;
        return 1;
    }

	if(frame > 0)
		reinterpret_cast<void(__stdcall*)(void*, int, int)>(BinkGoto)(video->vid, frame, 0);

	return 1;
}

int __fastcall BinkPlayerPlayDeinit_G1(DWORD BinkPlayer)
{
	DWORD BackView = *reinterpret_cast<DWORD*>(BinkPlayer + 0x5C);
    if(BackView)
        reinterpret_cast<void(__thiscall*)(DWORD, int)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(BackView) + 0x20))(BackView, 1);

	return reinterpret_cast<int(__thiscall*)(DWORD)>(0x469650)(BinkPlayer);
}

int __fastcall BinkPlayerPlayDeinit_G2(DWORD BinkPlayer)
{
	DWORD BackView = *reinterpret_cast<DWORD*>(BinkPlayer + 0x5C);
    if(BackView)
        reinterpret_cast<void(__thiscall*)(DWORD, int)>(*reinterpret_cast<DWORD*>(*reinterpret_cast<DWORD*>(BackView) + 0x24))(BackView, 1);

	return reinterpret_cast<int(__thiscall*)(DWORD)>(0x46E930)(BinkPlayer);
}

int __fastcall BinkPlayerOpenVideo_G1(DWORD BinkPlayer, DWORD _EDX, zSTRING_G1 videoName)
{
    DWORD zCOption = *reinterpret_cast<DWORD*>(0x869694);
	zSTRING_G1& directoryRoot = reinterpret_cast<zSTRING_G1&(__thiscall*)(DWORD, int)>(0x45FC00)(zCOption, 23);
	std::string pathToVideo = std::string(directoryRoot.ToChar(), directoryRoot.Length()) +
		std::string(videoName.ToChar(), videoName.Length());
	if(pathToVideo.find(".BIK") == std::string::npos && pathToVideo.find(".BK2") == std::string::npos)
		pathToVideo.append(".BIK");

	reinterpret_cast<void(__stdcall*)(DWORD, DWORD)>(BinkSetSoundSystem)(BinkOpenWaveOut, 0);
	void* videoHandle = reinterpret_cast<void*(__stdcall*)(const char*, DWORD)>(BinkOpen)(pathToVideo.c_str(), 0);
	if(videoHandle)
	{
		reinterpret_cast<void(__stdcall*)(void*, int)>(BinkSetSoundOnOff)(videoHandle, 1);
		//if(NewBinkSetVolume) reinterpret_cast<void(__stdcall*)(void*, int, DWORD)>(BinkSetVolume)(videoHandle, 0, 65536);
		//else reinterpret_cast<void(__stdcall*)(void*, DWORD)>(BinkSetVolume)(videoHandle, 65536);
		*reinterpret_cast<BinkVideo**>(BinkPlayer + 0x30) = new BinkVideo(videoHandle);
	}

	BinkVideo* video = *reinterpret_cast<BinkVideo**>(BinkPlayer + 0x30);
	if(video)
	{
        video->globalVolume = BinkPlayerReadGlobalVolume(zCOption);
        video->scaleVideo = BinkPlayerReadScaleVideos(zCOption);

		// We are passing directly zSTRING so the memory will be deleted inside this function
		reinterpret_cast<int(__thiscall*)(DWORD, zSTRING_G1)>(0x469280)(BinkPlayer, videoName);
		return 1;
	}

	videoName.Delete();
	return 0;
}

int __fastcall BinkPlayerOpenVideo_G2(DWORD BinkPlayer, DWORD _EDX, zSTRING_G2 videoName)
{
    DWORD zCOption = *reinterpret_cast<DWORD*>(0x8CD988);
	zSTRING_G2& directoryRoot = reinterpret_cast<zSTRING_G2&(__thiscall*)(DWORD, int)>(0x465260)(zCOption, 24);
	std::string pathToVideo = std::string(directoryRoot.ToChar(), directoryRoot.Length()) +
		std::string(videoName.ToChar(), videoName.Length());
	if(pathToVideo.find(".BIK") == std::string::npos && pathToVideo.find(".BK2") == std::string::npos)
		pathToVideo.append(".BIK");

	reinterpret_cast<void(__stdcall*)(DWORD, DWORD)>(BinkSetSoundSystem)(BinkOpenWaveOut, 0);
	void* videoHandle = reinterpret_cast<void*(__stdcall*)(const char*, DWORD)>(BinkOpen)(pathToVideo.c_str(), 0);
	if(videoHandle)
	{
		reinterpret_cast<void(__stdcall*)(void*, int)>(BinkSetSoundOnOff)(videoHandle, 1);
		//if(NewBinkSetVolume) reinterpret_cast<void(__stdcall*)(void*, int, DWORD)>(BinkSetVolume)(videoHandle, 0, 65536);
		//else reinterpret_cast<void(__stdcall*)(void*, DWORD)>(BinkSetVolume)(videoHandle, 65536);
		*reinterpret_cast<BinkVideo**>(BinkPlayer + 0x30) = new BinkVideo(videoHandle);
	}

	BinkVideo* video = *reinterpret_cast<BinkVideo**>(BinkPlayer + 0x30);
	if(video)
	{
        video->globalVolume = BinkPlayerReadGlobalVolume(zCOption);
        video->scaleVideo = BinkPlayerReadScaleVideos(zCOption);

		// We are passing directly zSTRING so the memory will be deleted inside this function
		reinterpret_cast<int(__thiscall*)(DWORD, zSTRING_G2)>(0x46E560)(BinkPlayer, videoName);
		return 1;
	}

	videoName.Delete();
	return 0;
}

int __fastcall BinkPlayerCloseVideo_G1(DWORD BinkPlayer)
{
	BinkVideo* video = *reinterpret_cast<BinkVideo**>(BinkPlayer + 0x30);
	if(!video)
		return 0;

	delete[] video->textureData;
	if(video->texture)
	{
		video->texture->Release();
		video->texture = nullptr;
	}
	reinterpret_cast<void(__stdcall*)(void*)>(BinkClose)(video->vid);

	delete video;
	*reinterpret_cast<DWORD*>(BinkPlayer + 0x30) = 0;
	return reinterpret_cast<int(__thiscall*)(DWORD)>(0x4693F0)(BinkPlayer);
}

int __fastcall BinkPlayerCloseVideo_G2(DWORD BinkPlayer)
{
	BinkVideo* video = *reinterpret_cast<BinkVideo**>(BinkPlayer + 0x30);
	if(!video)
		return 0;

	delete[] video->textureData;
	if(video->texture)
	{
		video->texture->Release();
		video->texture = nullptr;
	}
	reinterpret_cast<void(__stdcall*)(void*)>(BinkClose)(video->vid);

	delete video;
	*reinterpret_cast<DWORD*>(BinkPlayer + 0x30) = 0;
	return reinterpret_cast<int(__thiscall*)(DWORD)>(0x46E6D0)(BinkPlayer);
}

void RegisterBinkPlayerHooks()
{
    HMODULE BinkWDLL;
	if((BinkWDLL = LoadLibraryA("Bink2W32.dll")) != nullptr || (BinkWDLL = GetModuleHandleA("BinkW32.dll")) != nullptr)
	{
		NewBinkSetVolume = true;
		BinkOpenWaveOut = reinterpret_cast<DWORD>(GetProcAddress(BinkWDLL, "_BinkOpenWaveOut@4"));
		BinkSetSoundSystem = reinterpret_cast<DWORD>(GetProcAddress(BinkWDLL, "_BinkSetSoundSystem@8"));
		BinkSetSoundOnOff = reinterpret_cast<DWORD>(GetProcAddress(BinkWDLL, "_BinkSetSoundOnOff@8"));
		BinkSetVolume = reinterpret_cast<DWORD>(GetProcAddress(BinkWDLL, "_BinkSetVolume@12"));
		if(!BinkSetVolume)
		{
			BinkSetVolume = reinterpret_cast<DWORD>(GetProcAddress(BinkWDLL, "_BinkSetVolume@8"));
			if(IsG1 && *reinterpret_cast<BYTE*>(0x43A942) != 0xE9)
				NewBinkSetVolume = false;
			else
				NewBinkSetVolume = false;
		}
		BinkOpen = reinterpret_cast<DWORD>(GetProcAddress(BinkWDLL, "_BinkOpen@8"));
		BinkDoFrame = reinterpret_cast<DWORD>(GetProcAddress(BinkWDLL, "_BinkDoFrame@4"));
		BinkNextFrame = reinterpret_cast<DWORD>(GetProcAddress(BinkWDLL, "_BinkNextFrame@4"));
		BinkWait = reinterpret_cast<DWORD>(GetProcAddress(BinkWDLL, "_BinkWait@4"));
		BinkPause = reinterpret_cast<DWORD>(GetProcAddress(BinkWDLL, "_BinkPause@8"));
		BinkClose = reinterpret_cast<DWORD>(GetProcAddress(BinkWDLL, "_BinkClose@4"));
		BinkGoto = reinterpret_cast<DWORD>(GetProcAddress(BinkWDLL, "_BinkGoto@12"));
		BinkCopyToBuffer = reinterpret_cast<DWORD>(GetProcAddress(BinkWDLL, "_BinkCopyToBuffer@28"));
		BinkSetFrameRate = reinterpret_cast<DWORD>(GetProcAddress(BinkWDLL, "_BinkSetFrameRate@8"));
		BinkSetSimulate = reinterpret_cast<DWORD>(GetProcAddress(BinkWDLL, "_BinkSetSimulate@4"));
	}

	if(IsG1)
	{
		OverWrite(0x7D1014, reinterpret_cast<DWORD>(&BinkPlayerPlayHandleEvents_G1));
		HookJMP(0x4223D0, reinterpret_cast<DWORD>(&BinkPlayerPlayHandleEvents_G1));

		OverWrite(0x7D1008, reinterpret_cast<DWORD>(&BinkPlayerSetSoundVolume));
		HookJMP(0x43CB80, reinterpret_cast<DWORD>(&BinkPlayerSetSoundVolume));

		OverWrite(0x7D1004, reinterpret_cast<DWORD>(&BinkPlayerToggleSound_G1));
		HookJMP(0x43CB30, reinterpret_cast<DWORD>(&BinkPlayerToggleSound_G1));

		OverWrite(0x7D0FF4, reinterpret_cast<DWORD>(&BinkPlayerPause));
		HookJMP(0x43C960, reinterpret_cast<DWORD>(&BinkPlayerPause));

		OverWrite(0x7D0FF8, reinterpret_cast<DWORD>(&BinkPlayerUnpause));
		HookJMP(0x43C980, reinterpret_cast<DWORD>(&BinkPlayerUnpause));

		OverWrite(0x7D1000, reinterpret_cast<DWORD>(&BinkPlayerIsPlaying));
		HookJMP(0x43C9B0, reinterpret_cast<DWORD>(&BinkPlayerIsPlaying));

		OverWrite(0x7D100C, reinterpret_cast<DWORD>(&BinkPlayerPlayGotoNextFrame));
		HookJMP(0x43C760, reinterpret_cast<DWORD>(&BinkPlayerPlayGotoNextFrame));

		OverWrite(0x7D1010, reinterpret_cast<DWORD>(&BinkPlayerPlayWaitNextFrame));
		HookJMP(0x43C770, reinterpret_cast<DWORD>(&BinkPlayerPlayWaitNextFrame));

		OverWrite(0x7D14C4, reinterpret_cast<DWORD>(&BinkPlayerPlayFrame_G1));
		HookJMP(0x43C7B0, reinterpret_cast<DWORD>(&BinkPlayerPlayFrame_G1));

		OverWrite(0x7D14C0, reinterpret_cast<DWORD>(&BinkPlayerPlayInit_G1));
		HookJMP(0x43B460, reinterpret_cast<DWORD>(&BinkPlayerPlayInit_G1));

		OverWrite(0x7D14C8, reinterpret_cast<DWORD>(&BinkPlayerPlayDeinit_G1));
		HookJMP(0x43BFB0, reinterpret_cast<DWORD>(&BinkPlayerPlayDeinit_G1));

		OverWrite(0x7D14B8, reinterpret_cast<DWORD>(&BinkPlayerOpenVideo_G1));
		HookJMP(0x43A660, reinterpret_cast<DWORD>(&BinkPlayerOpenVideo_G1));

		OverWrite(0x7D0FE4, reinterpret_cast<DWORD>(&BinkPlayerCloseVideo_G1));
		HookJMP(0x43B1D0, reinterpret_cast<DWORD>(&BinkPlayerCloseVideo_G1));
	}
	else if(IsG2)
	{
		OverWrite(0x82F07C, reinterpret_cast<DWORD>(&BinkPlayerPlayHandleEvents_G2));
		HookJMP(0x422CA0, reinterpret_cast<DWORD>(&BinkPlayerPlayHandleEvents_G2));

		OverWrite(0x82F070, reinterpret_cast<DWORD>(&BinkPlayerSetSoundVolume));
		HookJMP(0x440930, reinterpret_cast<DWORD>(&BinkPlayerSetSoundVolume));

		OverWrite(0x82F06C, reinterpret_cast<DWORD>(&BinkPlayerToggleSound_G2));
		HookJMP(0x4408E0, reinterpret_cast<DWORD>(&BinkPlayerToggleSound_G2));

		OverWrite(0x82F05C, reinterpret_cast<DWORD>(&BinkPlayerPause));
		HookJMP(0x440710, reinterpret_cast<DWORD>(&BinkPlayerPause));

		OverWrite(0x82F060, reinterpret_cast<DWORD>(&BinkPlayerUnpause));
		HookJMP(0x440730, reinterpret_cast<DWORD>(&BinkPlayerUnpause));

		OverWrite(0x82F068, reinterpret_cast<DWORD>(&BinkPlayerIsPlaying));
		HookJMP(0x440760, reinterpret_cast<DWORD>(&BinkPlayerIsPlaying));

		OverWrite(0x82F074, reinterpret_cast<DWORD>(&BinkPlayerPlayGotoNextFrame));
		HookJMP(0x440510, reinterpret_cast<DWORD>(&BinkPlayerPlayGotoNextFrame));

		OverWrite(0x82F078, reinterpret_cast<DWORD>(&BinkPlayerPlayWaitNextFrame));
		HookJMP(0x440520, reinterpret_cast<DWORD>(&BinkPlayerPlayWaitNextFrame));

		OverWrite(0x82F5A4, reinterpret_cast<DWORD>(&BinkPlayerPlayFrame_G2));
		HookJMP(0x440560, reinterpret_cast<DWORD>(&BinkPlayerPlayFrame_G2));

		OverWrite(0x82F5A0, reinterpret_cast<DWORD>(&BinkPlayerPlayInit_G2));
		HookJMP(0x43F080, reinterpret_cast<DWORD>(&BinkPlayerPlayInit_G2));

		OverWrite(0x82F5A8, reinterpret_cast<DWORD>(&BinkPlayerPlayDeinit_G2));
		HookJMP(0x43FD20, reinterpret_cast<DWORD>(&BinkPlayerPlayDeinit_G2));

		OverWrite(0x82F598, reinterpret_cast<DWORD>(&BinkPlayerOpenVideo_G2));
		HookJMP(0x43E0F0, reinterpret_cast<DWORD>(&BinkPlayerOpenVideo_G2));

		OverWrite(0x82F04C, reinterpret_cast<DWORD>(&BinkPlayerCloseVideo_G2));
		HookJMP(0x43EDF0, reinterpret_cast<DWORD>(&BinkPlayerCloseVideo_G2));
	}
}
