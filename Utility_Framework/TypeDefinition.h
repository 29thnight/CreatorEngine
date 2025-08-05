#pragma once
#include "BaseTypeDef.h"
#include <Unknwnbase.h>
#include <combaseapi.h>
#include <DirectXMath.h>
#include <typeindex>
#include <nlohmann/json.hpp>
#include <filesystem>

namespace file = std::filesystem;
#pragma warning(disable: 26819)

typedef DECIMAL decimal;
typedef FILE* File;

using int2 = DirectX::XMINT2;
using int3 = DirectX::XMINT3;
using float2 = DirectX::XMFLOAT2;
using float3 = DirectX::XMFLOAT3;
using float4 = DirectX::XMFLOAT4;

using json = nlohmann::json;

namespace DirectX11
{
    struct Sizef
    {
        float width;
        float height;
    };
};