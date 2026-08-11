#ifndef DYNAMICCPP_EXPORTS
#include "DX12ShaderCompiler.h"
#include "../RHIShaderSource.h"

// ★ ComPtr 을 이 파일이 쓴다. 헤더는 d3dcompiler.h 만 물고 있어서, 유니티
//   빌드에서 같은 묶음의 옆 파일이 wrl 을 넣어 주는 동안 가려져 있었다
//   (비유니티 점검에서 드러났다 — §7.2.3 뒤의 규칙).
#include <wrl/client.h>

#include <sstream>

namespace
{
    // 유니티 빌드에서 익명 네임스페이스가 파일 간 합쳐지므로 이름을 고유하게 둔다.
    std::string DX12ShaderHrToString(HRESULT hr)
    {
        std::ostringstream oss;
        oss << "HRESULT 0x" << std::hex << static_cast<unsigned long>(hr);
        return oss.str();
    }
}

bool DX12ShaderCompiler::CompileFile(std::string_view name, const char* entryPoint,
    const char* target, const D3D_SHADER_MACRO* defines,
    RHIShaderBlob& outBlob, std::string& outError)
{
    std::string source;
    if (!RHIShaderSource::Load(name, source, outError)) return false;

    // ★ pSourceName 에 실제 경로를 준다. 두 가지가 여기에 달려 있다 —
    //   인클루드가 이 경로를 기준으로 풀리고, 컴파일러 진단에 파일 이름이
    //   찍힌다. 널을 주면 오류가 "(2,15): error X3000" 처럼 어느 파일인지
    //   없이 나온다.
    const std::string sourceName = RHIShaderSource::Resolve(name).string();

    Microsoft::WRL::ComPtr<ID3DBlob> blob;
    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    const HRESULT hr = D3DCompile(source.c_str(), source.size(), sourceName.c_str(),
        defines, D3D_COMPILE_STANDARD_FILE_INCLUDE, entryPoint, target, 0, 0,
        &blob, &errors);

    if (FAILED(hr))
    {
        outError = std::string(name) + " (" + entryPoint + "/" + target + ") 컴파일 실패: ";
        if (errors) outError += static_cast<const char*>(errors->GetBufferPointer());
        else        outError += DX12ShaderHrToString(hr);
        return false;
    }

    // ★ 여기서 백엔드 타입이 끝난다. 밖으로 나가는 것은 바이트뿐이다.
    outBlob.Assign(blob->GetBufferPointer(), blob->GetBufferSize());
    return true;
}

#endif
