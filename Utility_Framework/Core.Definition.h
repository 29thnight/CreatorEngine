#pragma once
#define WIN32_LEAN_AND_MEAN 
// header
#include <comdef.h>
#include <dxgi1_4.h>
#include <d3d11.h>
#include <d3d11_3.h>
#include <d2d1_3.h>
#include <d2d1effects_2.h>
#include <DirectXColors.h>
#include <dwrite.h>
#include <dwrite_3.h>
#include <d3dcompiler.h>
#include <d3dcommon.h>
#include <directxtk/DDSTextureLoader.h>
#include <directxtk/WICTextureLoader.h>
#include <DirectXTex.h>
#include <DirectXMath.h>
#include <directxtk/simplemath.h>
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#include <Xinput.h>
using namespace Microsoft::WRL;
//STL
#include <array>
#include <algorithm>
#include <filesystem>
namespace file = std::filesystem;
#include <fstream>
#include <functional>
#include <new>
#include <memory>
#include <mutex>
#include <unordered_map>
#include <ranges>
using namespace std::views;
constexpr inline decltype(auto) foreach = std::ranges::for_each;
#include <stack>
#include <string>
#include <tuple>
#include <utility>
#include <vector>
#include <cstdint>
//Custom
#include "ClassProperty.h"
#include "TypeDefinition.h"
#include "DirectXHelper.h"
#include "LinkedListLib.hpp"
//#include "flatbuffers/flatbuffers.h"

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
