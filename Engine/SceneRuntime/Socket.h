#pragma once
#include "Core.Minimal.h"
#include "Transform.h"
#include <mathematics/matrix4x4.hpp>
class Entity;
class Socket
{
public:
	Socket();
    ~Socket();

    std::string m_name;
    std::string m_ObjectName;
    int GameObjectIndex = -1;
    math::matrix4x4 m_offset{ math::matrix4x4::identity() };
    math::matrix4x4 m_boneMatrix{};
    Transform transform;

    Core::DelegateHandle m_activeSceneChangedEventHandle{};

    std::vector<HashedGuid> AttachObejctIndex;
    std::vector<Entity*> AttachObjects;
    void AttachObject(Entity* Object);
    void DetachObject(Entity* Object);
    void DetachAllObject();
    void Update();
};

