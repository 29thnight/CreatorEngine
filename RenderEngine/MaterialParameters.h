#pragma once
#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

// 머티리얼 상수 기술 — 백엔드·셰이더 언어 중립.
//
// ── 왜 이 파일이 남았나 ──
//
// 옛 셰이더 시스템(ShaderResourceSystem·ShaderPSO·ShaderDSL)을 걷어낼 때
// 이 자료만 살렸다. 이유는 하나다 — **호출자가 있다**.
//
//   · 게임 스크립트가 Material::TrySetValue("Param", "lerpValue", ...) 로
//     상수를 쓴다(Dynamic_CPP/Assets/Script 8곳).
//   · C# 인터롭이 Api_Mesh_SetMaterialFloat/Int 로 같은 자리를 연다.
//
// 그 호출들은 지금 아무 그림도 바꾸지 않는다. 값이 CPU 맵에 쌓였다가
// 머티리얼 파일로 직렬화될 뿐, GPU 로 올리는 코드가 저장소에 0곳이다
// (Material::ApplyShaderParams 는 호출자가 0이었다). 그래도 지우지 않은
// 것은, **이것이 셰이더 저작 언어가 다시 물릴 이음매**이기 때문이다.
//
// ── 무엇이 이 자료를 채우는가 ──
//
// 지금은 아무도 채우지 않는다. m_cbMeta 가 늘 널이라 TrySet/TryGet 은
// 전부 false 를 돌려준다. 예전에는 D3D11 리플렉션이 자산 셰이더(.shader)
// 를 훑어 채웠는데, 그 자산 경로 전체를 폐기했다.
//
// ★ 다음에 셰이더 언어(HLSL 직접이든 ShaderLab 류든)를 들일 때 채울 곳이
//   여기다. 채우는 쪽만 새로 쓰면 스크립트·C# 호출부는 그대로 살아난다 —
//   그것이 이 타입을 백엔드 타입(D3D_SHADER_VARIABLE_TYPE 같은 것)으로
//   두지 않고 중립으로 둔 이유다.

namespace MaterialParam
{
    /// 상수의 자료형. 옛 코드는 D3D_SHADER_VARIABLE_TYPE 을 그대로 들고
    /// 있었는데, 그러면 이 헤더가 DX11 헤더를 끌고 온다.
    enum class VarType : uint8_t
    {
        Unknown = 0,
        Float,
        Int,
        Bool,
        Matrix,
    };

    /// 상수 버퍼 안의 변수 하나.
    struct VariableDesc
    {
        std::string name{};
        uint32_t    offset{ 0 };
        uint32_t    size{ 0 };
        VarType     type{ VarType::Unknown };
    };

    /// 상수 버퍼 하나.
    struct CBEntry
    {
        std::string               name{};
        uint32_t                  slot{ 0 };
        uint32_t                  size{ 0 };
        std::vector<VariableDesc> variables{};
    };

    /// 이름 → 상수 버퍼. Material 이 약참조(포인터)로 들고 본다.
    using CBTable = std::unordered_map<std::string, CBEntry>;
}

