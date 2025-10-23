#pragma once
#define WIN32_LEAN_AND_MEAN

#ifndef YAML_CPP_API
#define YAML_CPP_API __declspec(dllimport)
#endif /* YAML_CPP_STATIC_DEFINE */

// header
#ifndef DYNAMICCPP_EXPORTS
#include <dxgi1_4.h>
#include <dxgi1_6.h>
#include <d3d11.h>
#include <d3d11_3.h>
#include <d3d11_4.h>
#include <DirectXColors.h>
#include <d3dcompiler.h>
#include <d3dcommon.h>
#include <directxtk/DDSTextureLoader.h>
#include <directxtk/WICTextureLoader.h>
#include <DirectXTK/SpriteBatch.h>
#include <DirectXTex.h>
#include <DirectXMath.h>
#include <windows.h>
#include <wincodec.h>
#include <dxgidebug.h>
#endif // !DYNAMICCPP_EXPORTS
#include <comdef.h>
#include <wrl/client.h>
#include <directxtk/simplemath.h>
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
#ifndef DYNAMICCPP_EXPORTS
#include "DirectXHelper.h"
#endif // !DYNAMICCPP_EXPORTS
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
