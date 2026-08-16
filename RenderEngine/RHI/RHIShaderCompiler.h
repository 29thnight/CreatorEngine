#pragma once
#ifndef DYNAMICCPP_EXPORTS

#include "RHIShaderBlob.h"

#include <cstdint>
#include <string>
#include <string_view>

enum class RHIShaderBinary : std::uint8_t
{
    Dxil,
    SpirV,
};

// 퍼뮤테이션 프레임워크(M2)가 서기 전까지 쓰는 중립 define 표현이다.
// D3D_SHADER_MACRO와 같은 널 종료 배열 규약을 쓰되 D3D 헤더에는 의존하지 않는다.
struct RHIShaderDefine
{
    const char* Name{};
    const char* Definition{};
};

struct RHIShaderCompileOptions
{
    // 적분·감축처럼 부동소수점 재결합이 결과 계약을 깨는 셰이더만 켠다.
    bool strictMath{};
};

struct RHIShaderCompileRequest
{
    std::string_view name;
    std::string_view entryPoint;
    std::string_view targetProfile;
    RHIShaderBinary output{ RHIShaderBinary::Dxil };
    const RHIShaderDefine* defines{};
    RHIShaderCompileOptions options{};
};

// 컴파일러 구현과 소비자 사이의 유일한 계약. 패스는 이 인터페이스의 서비스
// 진입점만 부르고 DXC/COM/DX12/Vulkan 타입을 알지 않는다.
class IRHIShaderCompiler
{
public:
    virtual ~IRHIShaderCompiler() = default;
    virtual bool Compile(const RHIShaderCompileRequest& request,
        RHIShaderBlob& outBlob, std::string& outError) = 0;
};

namespace RHIShaderCompiler
{
    struct Stats
    {
        std::uint64_t memoryHits{};
        std::uint64_t diskHits{};
        std::uint64_t compiles{};
        std::uint64_t failures{};
    };

    // 패스 초기화가 실행되는 현재 스레드의 산출 형식을 정한다. Vulkan 초기화
    // 스코프가 SpirV로 바꾸고 빠져나오면 이전 값을 복원한다.
    RHIShaderBinary GetOutput();
    void SetOutput(RHIShaderBinary output);

    class ScopedOutput final
    {
    public:
        explicit ScopedOutput(RHIShaderBinary output);
        ~ScopedOutput();

        ScopedOutput(const ScopedOutput&) = delete;
        ScopedOutput& operator=(const ScopedOutput&) = delete;

    private:
        RHIShaderBinary m_previous;
    };

    // targetProfile은 기존 HLSL 프로필(vs_5_0 등)을 받는다. DXC가 요구하는
    // SM6 프로필로 서비스 내부에서 올리므로 호출부가 백엔드별 문자열을 모른다.
    bool CompileFile(std::string_view name, std::string_view entryPoint,
        std::string_view targetProfile, const RHIShaderDefine* defines,
        RHIShaderBlob& outBlob, std::string& outError,
        RHIShaderCompileOptions options = {});

    inline bool CompileFile(std::string_view name, std::string_view entryPoint,
        std::string_view targetProfile, RHIShaderBlob& outBlob, std::string& outError,
        RHIShaderCompileOptions options = {})
    {
        return CompileFile(name, entryPoint, targetProfile, nullptr, outBlob, outError,
            options);
    }

    Stats GetStats();
    void ResetStats();
    void ClearMemoryCache();
}

#endif // !DYNAMICCPP_EXPORTS
