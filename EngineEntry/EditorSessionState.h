#pragma once

#include "TerrainBuffers.h"

#include <atomic>
#include <memory>

class Camera;

class EditorSessionState final
{
public:
    static EditorSessionState& Get() noexcept
    {
        static EditorSessionState instance;
        return instance;
    }

    bool IsGameViewHidden() const noexcept
    {
        return m_gameViewHidden.load(std::memory_order_acquire);
    }
    void ToggleGameViewHidden() noexcept
    {
        m_gameViewHidden.store(!IsGameViewHidden(), std::memory_order_release);
    }

    TerrainBrush* FindTerrainBrush() noexcept { return m_terrainBrush.get(); }
    TerrainBrush& GetOrCreateTerrainBrush()
    {
        if (!m_terrainBrush) m_terrainBrush = std::make_unique<TerrainBrush>();
        return *m_terrainBrush;
    }

    /// 에디터 씬 뷰 카메라 — Editor 세션이 소유한다(E4-5). Core는 Host가 뷰
    /// 요청에 실어 준 카메라만 알고, 씬 오버레이 뷰 판정도 Host 선언이다.
    /// 생성은 EditorMain::Initialize, 반납은 EditorMain::Finalize가 한다 —
    /// 반납은 ShutdownLive의 CameraManagement->Finalize()보다 먼저여야 한다.
    Camera* EditorCamera() const noexcept { return m_editorCamera.get(); }
    void SetEditorCamera(std::shared_ptr<Camera> camera) noexcept
    {
        m_editorCamera = std::move(camera);
    }

private:
    EditorSessionState() = default;

    std::atomic_bool m_gameViewHidden{ false };
    std::unique_ptr<TerrainBrush> m_terrainBrush;
    std::shared_ptr<Camera> m_editorCamera;
};
