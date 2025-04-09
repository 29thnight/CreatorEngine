#pragma once
#include "imgui.h"
#include "Core.Minimal.h"

namespace Log
{
    inline std::string MatrixToString(const DirectX::XMMATRIX& matrix)
    {
        using namespace DirectX;
        XMFLOAT4X4 m;
        XMStoreFloat4x4(&m, matrix); // XMMATRIX → XMFLOAT4X4 변환

        std::ostringstream oss;
        oss << std::fixed << std::setprecision(3); // 소수점 3자리까지 출력
        oss << "[ " << m._11 << ", " << m._12 << ", " << m._13 << ", " << m._14 << " ]\n";
        oss << "[ " << m._21 << ", " << m._22 << ", " << m._23 << ", " << m._24 << " ]\n";
        oss << "[ " << m._31 << ", " << m._32 << ", " << m._33 << ", " << m._34 << " ]\n";
        oss << "[ " << m._41 << ", " << m._42 << ", " << m._43 << ", " << m._44 << " ]\n";

        return oss.str();
    }
}

