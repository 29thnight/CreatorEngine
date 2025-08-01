#pragma once
#include "GameObject.h"
#include "DLLAcrossSingleton.h"
#include "Core.Minimal.h"

class GameObjectPool : public DLLCore::Singleton<GameObjectPool>
{
private:
    friend class DLLCore::Singleton<GameObjectPool>;
    GameObjectPool() = default;
    ~GameObjectPool() = default;

public:
    template<typename T, typename... Args>
    T* GameObjectAllocate(Args&&... args)
    {
        return m_pool.allocate_element(std::forward<Args>(args)...);
    }

    template<typename T>
    void GameObjectDeallocate(T* obj)
    {
        m_pool.deallocate_element(obj);
    }

private:
    MemoryPool<GameObject, 4096> m_pool;
};

static auto GameObjectPoolInstance = GameObjectPool::GetInstance();

namespace ObjectPool
{
    template<typename T, typename... Args>
    T* Allocate(Args&&... args)
    {
        return GameObjectPoolInstance->GameObjectAllocate<T>(std::forward<Args>(args)...);
    }

    template<typename T>
    void Deallocate(T * obj)
    {
        GameObjectPoolInstance->GameObjectDeallocate(obj);
    }
}
