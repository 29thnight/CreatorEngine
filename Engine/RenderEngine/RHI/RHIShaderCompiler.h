#pragma once

#include "RHIShaderBlob.h"
#include "RHIShaderPermutation.h"
#include "RHIShaderReflection.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

enum class RHIShaderBinary : std::uint8_t
{
    Dxil,
    SpirV,
};

struct RHIShaderCompileOptions
{
    // 적분·감축처럼 부동소수점 재결합이 결과 계약을 깨는 셰이더만 켠다.
    bool strictMath{};
    // Generated material modules can live in the runtime cache. Their common
    // modules are resolved here; Slang reports the actual dependency files.
    std::vector<std::filesystem::path> includeDirectories;
    // Fine pixel-quad derivatives require explicit SPIR-V DerivativeControl.
    // Request the capability only for hosts that use ddx_fine/ddy_fine.
    bool fineDerivatives{};
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
    struct VerifiedShader
    {
        RHIShaderBlob bytecode;
        RHIShaderReflection reflection;
        std::string dependencyIdentity;
        // Files Slang actually read for this module, excluding the root source.
        // Callers with their own caches key on exactly these, not a directory scan.
        std::vector<std::filesystem::path> dependencies;
    };

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

    // How many compiles may run at once (one Slang session slot each). Callers
    // that fan out compiles size their worker count with this.
    std::size_t MaxParallelCompiles();

    struct ModuleReuseState;

    // Inside this scope, compiles on the current thread that share source text and
    // options reuse one parsed Slang module (a material's stages per backend do).
    // The scope also pins one compiler session slot, so other threads compile in
    // parallel on other slots. Keep it short: nothing is re-read while it is open.
    class ModuleReuseScope final
    {
    public:
        ModuleReuseScope();
        ~ModuleReuseScope();

        ModuleReuseScope(const ModuleReuseScope&) = delete;
        ModuleReuseScope& operator=(const ModuleReuseScope&) = delete;

    private:
        ModuleReuseState* m_state;
        ModuleReuseState* m_previous;
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

    // Reuses a validated bytecode/layout pair when current dependencies match.
    // Reflection-only misses still extract layout from the linked program.
    bool ReflectFile(std::string_view name, std::string_view entryPoint,
        std::string_view targetProfile, RHIShaderBinary output,
        const RHIShaderPermutation& permutation,
        RHIShaderReflection& outReflection, std::string& outError,
        RHIShaderCompileOptions options = {});

    // Compile, reflection and dependency identity come from one linked program.
    // Failure leaves the previous verified result intact.
    bool VerifyFile(std::string_view name, std::string_view entryPoint, std::string_view targetProfile,
        RHIShaderBinary output, const RHIShaderPermutation& permutation,
        VerifiedShader& outShader, std::string& outError, RHIShaderCompileOptions options = {});

    inline bool ReflectFile(std::string_view name, std::string_view entryPoint,
        std::string_view targetProfile, RHIShaderBinary output,
        RHIShaderReflection& outReflection, std::string& outError,
        RHIShaderCompileOptions options = {})
    {
        const RHIShaderPermutation empty;
        return ReflectFile(name, entryPoint, targetProfile, output, empty,
            outReflection, outError, options);
    }

    // Short-lived snapshot mutex only; never waits for the compiler or renderer.
    struct Progress
    {
        bool active{};
        bool recompiling{};
        std::uint64_t revision{};
        std::uint64_t completedRequests{};
        std::uint64_t activeRequests{};
        // Name/entry/phase belong to the most recently updated active request.
        // Completing another parallel request cannot hide the remaining work.
        std::string name, entryPoint, phase, lastError;
    };
    Progress GetProgress();

    Stats GetStats();
    void ResetStats();
    void ClearMemoryCache();
}
