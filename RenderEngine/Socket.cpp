#include "Socket.h"
#include "GameObject.h"
#include "Scene.h"
#include "SceneManager.h"

Socket::Socket()
{
	AttachObjects.clear();
	m_activeSceneChangedEventHandle = activeSceneChangedEvent.AddLambda([&]
	{
		for (auto& ID : AttachObejctIndex)
		{
			GameObject* gameObject = GameObject::FindAttachedID(ID);
			if (gameObject)
			{
				AttachObjects.push_back(gameObject);
			}
		}
	});

}
Socket::~Socket()
{
	activeSceneChangedEvent -= m_activeSceneChangedEventHandle;
}

void Socket::AttachObject(GameObject* Object)
{
	AttachObejctIndex.push_back(Object->GetInstanceID());
	Object->m_attachedSoketID = Object->GetInstanceID();
	AttachObjects.push_back(Object);
}

void Socket::DetachObject(GameObject* Object)
{
	for (int i = 0; i < AttachObejctIndex.size(); ++i)
	{
		if (Object->GetInstanceID() == AttachObejctIndex[i])
		{
			AttachObejctIndex.erase(AttachObejctIndex.begin() + i);
			AttachObjects.erase(AttachObjects.begin() + i);
		}
	}
}

void Socket::Update()
{

	for (auto& obj : AttachObjects)
	{
		auto mat = transform.GetLocalMatrix();
		obj->m_transform.SetLocalMatrix(mat);
	}

}
