#pragma once

#include "RenderBackend.h"
#include "TemporalProductSettings.h"

#include <string>
#include <utility>

struct BuildSettings
{
    const std::wstring& GetStartupSceneName() const noexcept { return startupSceneName; }
    void SetStartupSceneName(std::wstring value) { startupSceneName = std::move(value); }

    // 패키징·제목표시줄·About 세 곳이 같은 값을 부른다. 비워 두면
    // EditorSettingsStore가 프로젝트 루트 폴더 이름으로 메운다 —
    // build.ps1의 (Split-Path -Leaf $projectRoot)와 같은 규칙이다.
    const std::string& GetProjectName() const noexcept { return projectName; }
    void SetProjectName(std::string value) { projectName = std::move(value); }

    // Player 패키징 선택일 뿐 에디터 실행 백엔드와 무관하다(에디터는 빌드 고정 DX12).
    // 에디터는 시작 때 이 값을 검사하지 않는다. 알아볼 수 없는 값은 원문만 기억하고
    // 패키징하는 순간에 거절한다.
    RenderBackend GetRenderBackend() const noexcept { return renderBackend; }
    bool HasRecognizedRenderBackend() const noexcept { return unrecognizedRenderBackend.empty(); }
    const std::string& GetUnrecognizedRenderBackend() const noexcept { return unrecognizedRenderBackend; }
    void SetRenderBackend(RenderBackend value) noexcept
    {
        renderBackend = value;
        unrecognizedRenderBackend.clear();
    }
    void SetUnrecognizedRenderBackend(std::string value)
    {
        unrecognizedRenderBackend = value.empty() ? std::string("(empty)") : std::move(value);
    }

    // Independent of Debug/Release compiler optimization. Export requires a matching
    // prebuilt EngineShipping=false/true distribution; it never rebuilds the engine.
    bool IsDevelopmentBuild() const noexcept { return developmentBuild; }
    void SetDevelopmentBuild(bool value) noexcept { developmentBuild = value; }
    TemporalProductSettings& PlayerRenderFeatures() noexcept { return playerRenderFeatures; }
    const TemporalProductSettings& PlayerRenderFeatures() const noexcept { return playerRenderFeatures; }

private:
    TemporalProductSettings playerRenderFeatures{};
    bool developmentBuild{ true };
    std::string projectName{};
    std::wstring startupSceneName{ L"SampleScene" };
    RenderBackend renderBackend{ RenderBackend::DX12 };
    std::string unrecognizedRenderBackend{};
};
