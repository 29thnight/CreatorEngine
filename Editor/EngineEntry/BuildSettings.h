#pragma once

#include "RenderBackend.h"

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

    RenderBackend GetRenderBackend() const noexcept { return renderBackend; }
    void SetRenderBackend(RenderBackend value) noexcept { renderBackend = value; }

private:
    std::string projectName{};
    std::wstring startupSceneName{ L"SampleScene" };
    RenderBackend renderBackend{ RenderBackend::DX12 };
};
