#pragma once

#include "TerrainBuffers.h"

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

private:
    EditorSessionState() = default;

    std::atomic_bool m_gameViewHidden{ false };
    std::unique_ptr<TerrainBrush> m_terrainBrush;
};
