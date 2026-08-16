#pragma once
#include "Core.Minimal.h"
#include "Transform.h"
class GameObject;
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
    std::vector<GameObject*> AttachObjects;
    void AttachObject(GameObject* Object);
    void DetachObject(GameObject* Object);
    void DetachAllObject();
    void Update();
};

