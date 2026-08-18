#include "Socket.h"
#include "GameObject.h"
#include "Scene.h"
#include "SceneManager.h"

Socket::Socket()
{
	AttachObjects.clear();

}
Socket::~Socket()
{
	SceneManagers->activeSceneChangedEvent -= m_activeSceneChangedEventHandle;
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
	


	XMMATRIX mat = transform.GetLocalMatrix();

	// ��ġ ���� (����� 4��° ��)
	XMVECTOR pos = mat.r[3]; // XMVECTOR(x, y, z, 1)

	// ������ ���ŵ� ȸ�� ��ĸ� ����
	// ȸ�� ����� 3x3 �κ��ε�, �� �� ���͸� ����ȭ�ϸ� ������ ���� ����
	XMVECTOR right = XMVector3Normalize(mat.r[0]);
	XMVECTOR up = XMVector3Normalize(mat.r[1]);
	XMVECTOR forward = XMVector3Normalize(mat.r[2]);

	XMMATRIX rotOnly =
	{
		right,
		up,
		forward,
		XMVectorSet(0, 0, 0, 1) // No translation yet
	};

	// ���� ���: ȸ�� + ��ġ
	XMMATRIX finalMat = rotOnly;
	finalMat.r[3] = pos; // ��ġ ����
	for (auto& obj : AttachObjects)
	{
		DirectX::XMVECTOR scaleVec = obj->Transform_().GetWorldScale(); 
		DirectX::XMMATRIX scaleMat = DirectX::XMMatrixScalingFromVector(scaleVec);
		DirectX::XMMATRIX localWithScale = scaleMat * finalMat;
		obj->Transform_().SetLocalMatrix(localWithScale);
	}


	/*for (auto& obj : AttachObjects)
	{
		auto mat = transform.GetLocalMatrix();
		
		obj->Transform_().SetLocalMatrix(mat);
	}*/


}
