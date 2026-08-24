#pragma once
#define WIN32_LEAN_AND_MEAN

// ── 엔진 버전 (2026-08-24) ──
//
// 콜라보레이터가 릴리스 때 손으로 올리는 수동 버전이다. 예전에는 CI가
// 커밋마다 version.txt(git SHA)를 갱신하고 EngineVersion.h를 생성해 주입했다
// — 그 체계("Update version file [skip ci]" 커밋·생성 헤더·게이트의 Ensure)는
// PHASE 22(버전별 불변 엔진·Launcher)의 수동 버전 방향에 맞춰 은퇴했다.
// 크래시 리포트가 이 값을 남긴다.
#define ENGINE_VERSION "0.1.0"

#ifndef YAML_CPP_API
#define YAML_CPP_API __declspec(dllimport)
#endif /* YAML_CPP_STATIC_DEFINE */

// header
// ★ <d3d11.h> · <d3d11_3.h> · <d3d11_4.h>가 여기 있었다 (2026-08-10).
//
//   모든 번역 단위에 DX11을 뿌리던 자리다. 이것이 켜져 있는 한 어느 파일이든
//   ID3D11*을 무심코 쓸 수 있어서, 경계를 세워도 다음 코드가 다시 넘어왔다.
//
//   DX11 디바이스가 사라진 뒤(DeviceResources 은퇴) 실제로 필요한 곳을 세니
//   넷이었고, 넷 다 자기가 직접 든다:
//     · Texture.h                  — DirectXTex의 DX11 가드(__d3d11_h__) 때문
//     · ShaderPSO.h                — <d3d11shader.h> (셰이더 리플렉션, 디바이스 불요)
//     · ImGuiDrawHelperMeshRenderer.cpp — 같은 리플렉션
//     · EnhancedApiOverheadBench.cpp    — DX11 대비 실측(비교 대상이라 있어야 한다)
//
//   dxgi는 남는다. DXGI는 DX11이 아니라 어댑터·스왑체인 계층이고 DX12가 쓴다.
#include <dxgi1_4.h>
#include <dxgi1_6.h>
#include <DirectXColors.h>
#include <d3dcompiler.h>
#include <d3dcommon.h>
#include <DirectXTex.h>
#include <DirectXMath.h>
#include <windows.h>
#include <wincodec.h>
#include <dxgidebug.h>
#include <comdef.h>
#include <wrl/client.h>
#include <directxtk12/SimpleMath.h>
#include <Psapi.h>

using namespace Microsoft::WRL;
//STL
#include <array>
#include <algorithm>
#include <fstream>
#include <functional>
#include <new>
#include <memory>
#include <mutex>
#include <queue>
#include <random>
#include <unordered_map>
#include <deque>
#include <set>
#include <stack>
#include <numeric> 
#include <cmath>
#include <string>
#include <tuple>
#include <future>
#include <utility>
#include <vector>
#include <cstdint>
#include <ranges>
#include <latch>
#include <cstddef>
#include <iterator>
#include <memory_resource>
//Custom
#include "ClassProperty.h"
#include "TypeDefinition.h"
#include "DirectXHelper.h"
#include "LinkedListLib.hpp"
//#include "flatbuffers/flatbuffers.h"
#include "plf_colony.h"

#ifndef _in
#define _in
#define _out
#define _inout
#define _in_out
#define _in_opt
#define _out_opt
#define _inout_opt
#define unsafe
#endif // !_in

#undef min
#undef max
#undef GetObject
