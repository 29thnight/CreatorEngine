#pragma once
#include "BaseTypeDef.h"
#include <Unknwnbase.h>
#include <combaseapi.h>
#include <typeindex>
#include <nlohmann/json.hpp>
#include <filesystem>
#include <execution>

namespace file = std::filesystem;
#pragma warning(disable: 26819)

typedef DECIMAL decimal;
typedef FILE* File;

using json = nlohmann::json;

// Sizef가 namespace DirectX11에 있었다 (2026-08-10에 옮겼다).
// float 둘을 든 평범한 값 타입이라 백엔드와 아무 상관이 없다.
namespace Core
{
    struct Sizef
    {
        float width;
        float height;
    };
};

namespace System 
{
    interface IInitializable
    {
        virtual void Initialize() = 0;
    };
}

namespace core
{
    template<typename T>
    concept IsHaveBeginEnd = requires(T a) { a.begin(); a.end(); };

	template<typename PP, IsHaveBeginEnd T>
    inline static void for_each(PP policy, T& container, auto invoke_fn)
    {
		std::for_each(policy, container.begin(), container.end(), invoke_fn);
    }
}
