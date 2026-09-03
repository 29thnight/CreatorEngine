#pragma once
#include <filesystem>
#include <string>
#include <string_view>

// 패스 셰이더 소스 — 백엔드 중립.
//
// ── 왜 파일로 뺐나 ──
//
// 패스 25곳이 HLSL 을 R"(...)" 문자열로 들고 있었다(실측 3,955줄). 그 형태는
// 세 가지를 막는다:
//
//   · 개발자가 셰이더를 편집기로 못 연다. 에디터의 콘텐츠 브라우저가 .hlsl 을
//     이미 알아보는데(FileType::Shader) 정작 엔진 셰이더는 파일이 아니었다.
//   · #include 가 안 된다. 그래서 공통 조각이 필요한 자리는 문자열을 손으로
//     이어 붙이고 있었다(kCommonHlsl + body). 파일이 되면 진짜 include 다.
//   · 컴파일 시점을 고를 수 없다. 문자열은 런타임 컴파일이 전제인데, DX12 는
//     D3DCompiler 가 Windows 에 있어 그것이 되지만 **Vulkan 은 OS 가 주는
//     컴파일러가 없다**(골격이 실측했다). 소스가 파일이어야 빌드 때 굽는
//     길이 열린다.
//
// ── 이 파일이 중립인 이유 ──
//
// ★ '읽는 일'은 백엔드와 무관하고 '컴파일하는 일'만 갈린다. 그래서 둘을
//   갈라 둔다 — V5(셰이더 컴파일 중립화)가 갈아 끼울 것은 컴파일 쪽이고,
//   이쪽은 그대로 남는다. 컴파일은 RHIShaderCompiler 서비스가 맡는다.

namespace RHIShaderSource
{
    /// 패스 셰이더가 사는 폴더 이름. 자산 루트(Assets/Shaders/) 아래다.
    ///
    /// ★ 자산 루트 아래인 것이 중요하다. 플레이어는 pak 을 풀어서 쓰는데
    ///   PakHelper 가 Assets/ 를 통째로 재귀 수집하므로, 여기 두면 배포가
    ///   자동으로 따라온다. 실행 파일 옆에 두면 그 경로가 따로 필요해진다.
    inline constexpr const char* kFolder = "DefaultPassShader";

    /// 이름을 실제 경로로. 확장자까지 포함한 이름을 준다("GBuffer.slang").
    /// 확장자가 Slang front-end 선택도 정한다(RHIShaderCompiler::IsSlangSource).
    std::filesystem::path Resolve(std::string_view name);

    /// 소스를 읽는다. 실패하면 false 와 사람이 읽을 이유를 준다 —
    /// 파일이 없다는 것은 배포가 잘못됐다는 뜻이고, 그 말이 그대로 나와야 한다.
    bool Load(std::string_view name, std::string& outText, std::string& outError);
}

