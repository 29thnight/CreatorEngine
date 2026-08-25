#pragma once

#include <DirectXMath.h>
#include <mathematics/matrix4x4.hpp>
#include <mathematics/scalar.hpp>
#include <mathematics/transform.hpp>
#include <mathematics/vector3.hpp>

// Mathematics 값과 아직 DirectXMath를 소비하는 렌더 경계 사이의 임시 bridge.
// 숨은 암시 변환을 만들지 않는다. 특히 vector3의 w 의미는 point/direction
// 함수 이름으로 호출부가 선택하게 한다.
namespace MathematicsInterop
{
    inline DirectX::XMMATRIX ToDirectX(const math::matrix4x4& value) noexcept
    {
        return DirectX::XMMATRIX(
            value.m[0][0], value.m[0][1], value.m[0][2], value.m[0][3],
            value.m[1][0], value.m[1][1], value.m[1][2], value.m[1][3],
            value.m[2][0], value.m[2][1], value.m[2][2], value.m[2][3],
            value.m[3][0], value.m[3][1], value.m[3][2], value.m[3][3]);
    }

    inline math::matrix4x4 FromDirectX(DirectX::FXMMATRIX value) noexcept
    {
        DirectX::XMFLOAT4X4 stored{};
        DirectX::XMStoreFloat4x4(&stored, value);
        return math::matrix4x4{
            stored._11, stored._12, stored._13, stored._14,
            stored._21, stored._22, stored._23, stored._24,
            stored._31, stored._32, stored._33, stored._34,
            stored._41, stored._42, stored._43, stored._44};
    }

    inline math::vector3 FromDirectX3(DirectX::FXMVECTOR value) noexcept
    {
        DirectX::XMFLOAT3 stored{};
        DirectX::XMStoreFloat3(&stored, value);
        return math::vector3{stored.x, stored.y, stored.z};
    }

    inline DirectX::XMVECTOR ToDirectXPoint(const math::vector3& value) noexcept
    {
        return DirectX::XMVectorSet(value.x, value.y, value.z, 1.f);
    }

    inline DirectX::XMVECTOR ToDirectXDirection(const math::vector3& value) noexcept
    {
        return DirectX::XMVectorSet(value.x, value.y, value.z, 0.f);
    }
}

static_assert(sizeof(math::matrix4x4) == sizeof(DirectX::XMFLOAT4X4));
static_assert(sizeof(math::vector3) == sizeof(DirectX::XMFLOAT3));
