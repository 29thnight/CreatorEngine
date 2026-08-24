#pragma once

#include "TerrainBuffers.h"
#include "EditorCameraRig.h"

#include <atomic>
#include <memory>

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

    /// 에디터 씬 뷰 카메라 — Editor 세션만 고유 소유한다. 전역 게임 카메라
    /// registry나 RenderCore 슬롯에 등록하지 않고 매 프레임 값 snapshot만 보낸다.
    EditorCameraRig* CameraRig() const noexcept { return m_cameraRig.get(); }
    Camera* EditorCamera() const noexcept
    {
        return m_cameraRig ? &m_cameraRig->GetCamera() : nullptr;
    }
    void SetCameraRig(std::unique_ptr<EditorCameraRig> rig) noexcept
    {
        m_cameraRig = std::move(rig);
    }

private:
    EditorSessionState() = default;

    std::atomic_bool m_gameViewHidden{ false };
    std::unique_ptr<TerrainBrush> m_terrainBrush;
    std::unique_ptr<EditorCameraRig> m_cameraRig;
};
