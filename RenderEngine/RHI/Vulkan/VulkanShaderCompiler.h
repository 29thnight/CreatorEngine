#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include "../RHIShaderBlob.h"

#include <string>
#include <string_view>

struct _D3D_SHADER_MACRO;

// 패스 셰이더 컴파일 — Vulkan (5d).
//
// ── 이 파일이 5d 의 첫 실측 결함이다 ──
//
// "그리드까지 남은 것이 정확히 둘"(5c-4c)은 **서비스·인코더 호출 표**로 센
// 값이었고, 그 자에는 파이프라인 **생성 입력**이 안 보인다. 5d 를 실제로
// 밟자 바로 드러났다: 그리드는 `DX12ShaderCompiler::CompileFile(..., "vs_5_0")`
// 로 DXBC 를 굽는데 `VulkanPipelineCache` 는 SPIR-V 를 먹는다. 같은 자로
// "그리드는 DX12 심볼 0"이라고도 적었는데, 그 파일 13행에
// `#include "DX12ShaderCompiler.h"` 가 있다 — **호출 표를 세는 자는
// include 와 정적 함수를 못 본다.**
//
// DX12ShaderCompiler.h 가 예고해 둔 자리이기도 하다: "Vulkan 이 들어오면
// 옆에 VulkanShaderCompiler 가 생기고 상위는 어느 쪽도 모른다."
//
// ── 왜 런타임 dxc 인가 ──
//
// 골격의 삼각형은 SPIR-V 를 미리 구워 헤더에 박았다(빌드가 SDK 를 요구하면
// 안 되므로). 패스 셰이더는 그럴 수 없다 — DX12 쪽이 런타임 컴파일이라
// (D3DCompile), 미리 굽는 순간 두 백엔드의 셰이더 수명이 갈리고 "패스 코드를
// 한 줄도 안 고친다"가 셰이더 쪽에서 깨진다. 그래서 dxcompiler.dll 을
// **런타임에 손으로 싣는다** — vulkan-1.dll 을 손으로 싣는 것과 같은 이유,
// 같은 방식이다(없으면 계약된 실패를 돌려준다).
//
// ★ Windows SDK 의 dxcompiler 는 SPIR-V CodeGen 이 꺼져 있다. VULKAN_SDK
//   쪽을 먼저 찾는 이유다 — 스크립트(build_vk_shaders.ps1)가 같은 판단을
//   이미 적어 두었다.
namespace VulkanShaderCompiler
{
    /// SPIR-V 모드. 켜져 있으면 `DX12ShaderCompiler::CompileFile` 이 이리로
    /// 흘린다 — 패스는 DX12 이름을 그대로 부르지만 나오는 것은 SPIR-V 다.
    ///
    /// ★ **과도기의 표시다.** 패스가 백엔드 이름의 컴파일러를 직접 부르는
    ///   것 자체가 5d 가 실측한 경계 결함이고, 호출부를 중립 이름으로 갈아
    ///   끼우는 것은 M 트랙(셰이더 파이프라인 재설계)의 몫이다. 그때까지
    ///   전환은 이 스위치가 든다.
    void SetActive(bool active);
    bool IsActive();

    /// DefaultPassShader/<name> 을 dxc 로 SPIR-V 컴파일한다.
    ///
    /// `dx12Target` 은 "vs_5_0" 같은 DX12 쪽 문자열을 그대로 받는다 — 호출부
    /// (패스)를 안 고치기 위해서다. dxc 는 5_0 을 안 받으므로 6_0 으로 올려
    /// 굽는다. 레지스터 시프트는 `VulkanBindingModel.h` 의 그 한 벌이다.
    bool CompileFile(std::string_view name, const char* entryPoint, const char* dx12Target,
        const _D3D_SHADER_MACRO* defines, RHIShaderBlob& outBlob, std::string& outError);
}

#endif
