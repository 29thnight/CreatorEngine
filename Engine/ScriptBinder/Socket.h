#pragma once
#include "Core.Minimal.h"
#include "Transform.h"
class Entity;
class Socket
{
public:
	Socket();
    ~Socket();

    std::string m_name;
    std::string m_ObjectName;
    int GameObjectIndex = -1;
    Mathf::xMatrix m_offset = DirectX::SimpleMath::Matrix::Identity;
    Mathf::xMatrix m_boneMatrix{};
    Transform transform;

    Core::DelegateHandle m_activeSceneChangedEventHandle{};

    std::vector<HashedGuid> AttachObejctIndex;
    std::vector<Entity*> AttachObjects;
    void AttachObject(Entity* Object);
    void DetachObject(Entity* Object);
    void DetachAllObject();
    void Update();
};

