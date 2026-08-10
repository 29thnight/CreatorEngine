#pragma once
#ifndef DYNAMICCPP_EXPORTS
#include <d3dcompiler.h>
#include <wrl/client.h>

#include <string>
#include <string_view>

// 패스 셰이더 컴파일 — DX12.
//
// ★ 패스마다 있던 컴파일 헬퍼를 하나로 모은 것이다. 25곳이 같은 일을
//   조금씩 다르게 하고 있었다 — 오류 문자열 모양이 제각각이라 실패했을 때
//   어느 패스인지 알기 어려웠고, HRESULT 만 남기는 자리도 있었다.
//
// ★ 이것이 V5(셰이더 컴파일 중립화)가 갈아 끼울 자리다. 소스를 읽는 일
//   (RHIShaderSource)은 중립이고 여기만 백엔드에 묶인다 — Vulkan 이 들어오면
//   옆에 VulkanShaderCompiler 가 생기고 상위는 어느 쪽도 모른다.
//
// ★ 인클루드가 된다. 문자열 시절에는 인클루드 핸들러가 없어 공통 조각을
//   손으로 이어 붙였는데(kCommonHlsl + body), 소스가 파일이 되면서
//   D3D_COMPILE_STANDARD_FILE_INCLUDE 가 소스 파일 위치를 기준으로 풀어 준다.

namespace DX12ShaderCompiler
{
    /// DefaultPassShader/<name> 을 읽어 컴파일한다.
    ///
    /// defines 는 널이어도 된다. 실패하면 false 와, 컴파일러가 낸 진단을
    /// 파일 이름과 함께 담은 문자열을 준다.
    bool CompileFile(std::string_view name, const char* entryPoint, const char* target,
        const D3D_SHADER_MACRO* defines,
        Microsoft::WRL::ComPtr<ID3DBlob>& outBlob, std::string& outError);

    /// defines 가 없는 흔한 경우.
    inline bool CompileFile(std::string_view name, const char* entryPoint, const char* target,
        Microsoft::WRL::ComPtr<ID3DBlob>& outBlob, std::string& outError)
    {
        return CompileFile(name, entryPoint, target, nullptr, outBlob, outError);
    }
}

#endif
