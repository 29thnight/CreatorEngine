#pragma once

#include <cstdint>
#include "Windows/EditorWindowBody.h"
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
        /// 진행 중인 적재 목록 본문. 셸이 창 프레임 안에서 부른다.
        void DrawStatus();
        /// 표시할 요청이 하나라도 있는가. 창의 존재 조건이다.
        bool HasVisible() const;
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

        // 진행 창 본문의 수명(PHASE 21 W3). 본문은 `Get()` 으로 다시
        // 들어오므로 `this` 를 물지 않지만, 수명을 타입이 드는 것은
        // 같다 — `Shutdown` 에서 이름을 풀던 한 줄을 대신한다.
        ::editor::windows::window_body_binding m_statusBody;
    };
}
