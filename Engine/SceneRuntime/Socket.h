#pragma once
#include "Core.Minimal.h"
#include "Transform.h"
#include <mathematics/matrix4x4.hpp>
class Entity;

// A socket owns pose data, not an Entity component. It does not participate in
// scene registration or GC and can be read by a fenced animation worker.
class SocketTransform
{
public:
    const math::matrix4x4& GetLocalMatrix() const noexcept { return m_local; }
    void SetLocalMatrix(const math::matrix4x4& value, TransformWriteReason) noexcept
    {
        m_local = value;
    }
private:
    math::matrix4x4 m_local{ math::matrix4x4::identity() };
};
class Socket
{
public:
	Socket();
    ~Socket() = default;
    void ReleaseManagedResources();

    std::string m_name;
    std::string m_ObjectName;
    int GameObjectIndex = -1;
    math::matrix4x4 m_offset{ math::matrix4x4::identity() };
    math::matrix4x4 m_boneMatrix{};
    SocketTransform transform;

    Core::DelegateHandle m_activeSceneChangedEventHandle{};

    std::vector<HashedGuid> AttachObejctIndex;
    std::vector<Entity*> AttachObjects;
    void AttachObject(Entity* Object);
    void DetachObject(Entity* Object);
    void DetachAllObject();
    void Update();
};

