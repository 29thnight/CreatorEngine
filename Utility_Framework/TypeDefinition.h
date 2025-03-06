#pragma once
#include <Unknwnbase.h>
#include <combaseapi.h>
#include <iostream>
#include <DirectXMath.h>
#include <typeindex>
#include <nlohmann/json.hpp>
#pragma warning(disable: 26819)

typedef unsigned char byte;
typedef unsigned short ushort;
typedef unsigned long ulong;
typedef DECIMAL decimal;
typedef FILE* File;

using int2 = DirectX::XMINT2;
using int3 = DirectX::XMINT3;
using float2 = DirectX::XMFLOAT2;
using float3 = DirectX::XMFLOAT3;
using float4 = DirectX::XMFLOAT4;

#define alfloat3 alignas(16) float3
#define alfloat4 alignas(16) float4
#define alflag alignas(16) flag
#define alstruct struct alignas(16) 

using uint8  = uint8_t;
using int8   = int8_t;
using uint16 = uint16_t;
using int16  = int16_t;
using uint32 = uint32_t;
using int32  = int32_t;
using uint64 = uint64_t;
using int64  = int64_t;
using guid = uint64_t;
using flag = uint32;
using mask = uint32;
using constant = uint32;
using bool32 = uint32;
using json = nlohmann::json;

enum class MouseKey : int
{
	LEFT = 0,
	RIGHT,
	MIDDLE,
	MAX = 3
};

enum class ControllerButton : int
{
    A = 0,
	B,
	X,
	Y,
	DPAD_UP,
	DPAD_DOWN,
	DPAD_LEFT,
	DPAD_RIGHT,
	START_BUTTON,
	BACK_BUTTON,
	LEFT_SHOULDER,
	RIGHT_SHOULDER,
	MAX = 12
};

struct BitFlag
{
public:
	BitFlag() = default;
	BitFlag(flag flag) noexcept
		: m_flag(flag)
	{
	}

	BitFlag& operator= (flag flag) noexcept
	{
		m_flag = flag;
		return *this;
	}

    operator flag() const
    {
        return m_flag;
    }
    //helper functions
    constexpr void Set(flag flag) noexcept
    {
        m_flag |= 1U << flag;
    }
    constexpr void Clear(flag flag) noexcept
    {
        m_flag &= ~(1U << flag);
    }
    constexpr void Toggle(flag flag) noexcept
    {
        m_flag ^= 1U << flag;
    }
    constexpr bool32 Test(flag flag) const noexcept
    {
        return m_flag & (1U << flag);
    }

    BitFlag& operator|= (flag flag) noexcept
	{
		m_flag |= flag;
		return *this;
	}

	BitFlag& operator&= (flag flag) noexcept
	{
		m_flag &= flag;
		return *this;
	}

    BitFlag& operator^= (flag flag) noexcept
    {
        m_flag ^= flag;
		return *this;
    }

private:
    flag m_flag{};
};