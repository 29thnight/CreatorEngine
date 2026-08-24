#pragma once

#include "RHIShaderBlob.h"
#include "RHIShaderPermutation.h"
#include "RHIShaderReflection.h"

#include <cstdint>
#include <string>
#include <string_view>

enum class RHIShaderBinary : std::uint8_t
{
    Dxil,
    SpirV,
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
    const RHIShaderPermutation* permutation{};
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
    virtual bool Reflect(const RHIShaderCompileRequest& request,
        RHIShaderReflection& outReflection, std::string& outError) = 0;
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
        std::string_view targetProfile, const RHIShaderPermutation& permutation,
        RHIShaderBlob& outBlob, std::string& outError,
        RHIShaderCompileOptions options = {});

    inline bool CompileFile(std::string_view name, std::string_view entryPoint,
        std::string_view targetProfile, RHIShaderBlob& outBlob, std::string& outError,
        RHIShaderCompileOptions options = {})
    {
        const RHIShaderPermutation empty;
        return CompileFile(name, entryPoint, targetProfile, empty, outBlob, outError,
            options);
    }

    // 컴파일 cache와 reflection 수명을 섞지 않는다. 같은 요청으로 linked program의
    // target layout만 읽으며 output은 명시해 DXIL/SPIR-V를 직접 대조할 수 있다.
    bool ReflectFile(std::string_view name, std::string_view entryPoint,
        std::string_view targetProfile, RHIShaderBinary output,
        const RHIShaderPermutation& permutation,
        RHIShaderReflection& outReflection, std::string& outError,
        RHIShaderCompileOptions options = {});

    inline bool ReflectFile(std::string_view name, std::string_view entryPoint,
        std::string_view targetProfile, RHIShaderBinary output,
        RHIShaderReflection& outReflection, std::string& outError,
        RHIShaderCompileOptions options = {})
    {
        const RHIShaderPermutation empty;
        return ReflectFile(name, entryPoint, targetProfile, output, empty,
            outReflection, outError, options);
    }

    Stats GetStats();
    void ResetStats();
    void ClearMemoryCache();
}

