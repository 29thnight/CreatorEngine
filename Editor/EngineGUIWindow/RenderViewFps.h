#pragma once
#include <cstdint>

namespace editor
{
    // Counts completed renders for one view, independently of UI redraw frequency.
    class RenderViewFps
    {
      public:
        float Update(uint64_t count, uint64_t viewId, uint64_t sceneEpoch, bool ready, double now)
        {
            if (!m_initialized || !ready || viewId != m_viewId || sceneEpoch != m_sceneEpoch ||
                count < m_count || now < m_sampleTime || now - m_sampleTime > 2.0)
            {
                m_initialized = ready;
                m_viewId = viewId;
                m_sceneEpoch = sceneEpoch;
                m_count = count;
                m_sampleTime = now;
                m_fps = 0.f;
            }
            else if (now - m_sampleTime >= 0.5)
            {
                m_fps = static_cast<float>(static_cast<double>(count - m_count) / (now - m_sampleTime));
                m_count = count;
                m_sampleTime = now;
            }
            return m_fps;
        }

      private:
        uint64_t m_count{};
        uint64_t m_viewId{};
        uint64_t m_sceneEpoch{};
        double m_sampleTime{};
        float m_fps{};
        bool m_initialized{};
    };
}
