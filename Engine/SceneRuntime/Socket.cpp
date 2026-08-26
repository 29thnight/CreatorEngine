#include "Socket.h"
#include "Entity.h"
#include "Scene.h"
#include "SceneManager.h"
#include <mathematics/transform.hpp>

Socket::Socket()
{
	AttachObjects.clear();

}
Socket::~Socket()
{
	SceneManagers->activeSceneChangedEvent -= m_activeSceneChangedEventHandle;
}

void Socket::AttachObject(Entity* Object)
{
	AttachObejctIndex.push_back(Object->GetInstanceID());
	Object->m_attachedSoketID = Object->GetInstanceID();
	AttachObjects.push_back(Object);
}

void Socket::DetachObject(Entity* Object)
{
	for (int i = 0; i < AttachObejctIndex.size(); ++i)
	{
		if (Object->GetInstanceID() == AttachObejctIndex[i])
		{
			AttachObejctIndex.erase(AttachObejctIndex.begin() + i);
			Object->m_attachedSoketID = -1;
			AttachObjects.erase(AttachObjects.begin() + i);
		}
	}
}

void Socket::DetachAllObject()
{
	for (auto& obj : AttachObjects)
	{
		if (obj)
			obj->m_attachedSoketID = -1;
	}

	AttachObjects.clear();
	AttachObejctIndex.clear();
}


void Socket::Update()
{
	


	const math::matrix4x4& mat = transform.GetLocalMatrix();
	const math::vector3 position = mat.translation();
	const math::vector3 right = math::normalize(mat.right());
	const math::vector3 up = math::normalize(mat.up());
	const math::vector3 forward = math::normalize(mat.forward());
	const math::matrix4x4 finalMat{
		right.x, right.y, right.z, 0.0f,
		up.x, up.y, up.z, 0.0f,
		forward.x, forward.y, forward.z, 0.0f,
		position.x, position.y, position.z, 1.0f };
	for (auto& obj : AttachObjects)
	{
		const math::matrix4x4 scaleMat =
			math::scaling_matrix(obj->Transform_().GetWorldScale());
		obj->Transform_().SetLocalMatrix(scaleMat * finalMat);
	}


	/*for (auto& obj : AttachObjects)
	{
		auto mat = transform.GetLocalMatrix();
		
		obj->Transform_().SetLocalMatrix(mat);
	}*/


}
