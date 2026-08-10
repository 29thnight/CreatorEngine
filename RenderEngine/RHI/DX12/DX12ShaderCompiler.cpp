#ifndef DYNAMICCPP_EXPORTS
#include "DX12ShaderCompiler.h"
#include "../RHIShaderSource.h"

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
    Microsoft::WRL::ComPtr<ID3DBlob>& outBlob, std::string& outError)
{
    std::string source;
    if (!RHIShaderSource::Load(name, source, outError)) return false;

    // ★ pSourceName 에 실제 경로를 준다. 두 가지가 여기에 달려 있다 —
    //   인클루드가 이 경로를 기준으로 풀리고, 컴파일러 진단에 파일 이름이
    //   찍힌다. 널을 주면 오류가 "(2,15): error X3000" 처럼 어느 파일인지
    //   없이 나온다.
    const std::string sourceName = RHIShaderSource::Resolve(name).string();

    Microsoft::WRL::ComPtr<ID3DBlob> errors;
    const HRESULT hr = D3DCompile(source.c_str(), source.size(), sourceName.c_str(),
        defines, D3D_COMPILE_STANDARD_FILE_INCLUDE, entryPoint, target, 0, 0,
        &outBlob, &errors);

    if (FAILED(hr))
    {
        outError = std::string(name) + " (" + entryPoint + "/" + target + ") 컴파일 실패: ";
        if (errors) outError += static_cast<const char*>(errors->GetBufferPointer());
        else        outError += DX12ShaderHrToString(hr);
        return false;
    }

    return true;
}

#endif
