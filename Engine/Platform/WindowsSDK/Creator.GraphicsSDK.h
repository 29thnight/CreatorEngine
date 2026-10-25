#pragma once
#if defined(CE_CONTRACT_STANDALONE)
#include "Creator.WinAPI.h"
#else
import "Creator.WinAPI.h";
#endif

#include <dxgi1_6.h>
#include <dxgidebug.h>
#include <d3d12.h>
#include <d3dcompiler.h>
#include <d3dcommon.h>
#include <d3d11shader.h>
#include <wincodec.h>
#include <DXProgrammableCapture.h>
#include <DirectXPackedVector.h>
