#include "Socket.h"
#include "Entity.h"
#include "Scene.h"
#include "SceneManager.h"
#include "MathematicsInterop.h"

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
	


	DirectX::XMMATRIX mat = MathematicsInterop::ToDirectX(
		transform.GetLocalMatrix());

	// ��ġ ���� (����� 4��° ��)
	DirectX::XMVECTOR pos = mat.r[3]; // DirectX::XMVECTOR(x, y, z, 1)

	// ������ ���ŵ� ȸ�� ��ĸ� ����
	// ȸ�� ����� 3x3 �κ��ε�, �� �� ���͸� ����ȭ�ϸ� ������ ���� ����
	DirectX::XMVECTOR right = DirectX::XMVector3Normalize(mat.r[0]);
	DirectX::XMVECTOR up = DirectX::XMVector3Normalize(mat.r[1]);
	DirectX::XMVECTOR forward = DirectX::XMVector3Normalize(mat.r[2]);

	DirectX::XMMATRIX rotOnly =
	{
		right,
		up,
		forward,
		DirectX::XMVectorSet(0, 0, 0, 1) // No translation yet
	};

	// ���� ���: ȸ�� + ��ġ
	DirectX::XMMATRIX finalMat = rotOnly;
	finalMat.r[3] = pos; // ��ġ ����
	for (auto& obj : AttachObjects)
	{
		DirectX::XMVECTOR scaleVec = MathematicsInterop::ToDirectXDirection(
			obj->Transform_().GetWorldScale());
		DirectX::XMMATRIX scaleMat = DirectX::XMMatrixScalingFromVector(scaleVec);
		DirectX::XMMATRIX localWithScale = scaleMat * finalMat;
		obj->Transform_().SetLocalMatrix(
			MathematicsInterop::FromDirectX(localWithScale));
	}


	/*for (auto& obj : AttachObjects)
	{
		auto mat = transform.GetLocalMatrix();
		
		obj->Transform_().SetLocalMatrix(mat);
	}*/


}
