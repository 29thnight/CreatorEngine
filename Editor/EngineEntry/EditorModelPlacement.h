#pragma once

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <mathematics/vector3.hpp>

namespace Editor
{
    // EditorMain owns startup/shutdown and pumps scene changes on the game thread.
    // Workers only prepare assets; requests never retain a Scene* or Entity*.
    class ModelPlacement final
    {
    public:
        static ModelPlacement& Get();
        void Initialize();
        void Shutdown();
        void Execute(std::uint32_t sceneId, std::string path,
            std::optional<math::vector3> position = std::nullopt);
        void Tick();
        void DrawStatus();
        void PrintStatus() const;
        bool IsIdle() const;

    private:
        struct Request;
        struct Impl;
        class Command;
        ModelPlacement();
        ~ModelPlacement();
        std::shared_ptr<Request> Enqueue(std::uint32_t sceneId, const std::string& path,
            const std::optional<math::vector3>& position, bool gameMode);
        void Cancel(const std::shared_ptr<Request>& request);
        std::unique_ptr<Impl> m_impl;
    };
}
