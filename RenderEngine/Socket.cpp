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

	// 위치 추출 (행렬의 4번째 열)
	XMVECTOR pos = mat.r[3]; // XMVECTOR(x, y, z, 1)

	// 스케일 제거된 회전 행렬만 추출
	// 회전 행렬은 3x3 부분인데, 각 축 벡터를 정규화하면 스케일 제거 가능
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

	// 최종 행렬: 회전 + 위치
	XMMATRIX finalMat = rotOnly;
	finalMat.r[3] = pos; // 위치 적용
	for (auto& obj : AttachObjects)
	{
		DirectX::XMVECTOR scaleVec = obj->m_transform.GetWorldScale(); 
		DirectX::XMMATRIX scaleMat = DirectX::XMMatrixScalingFromVector(scaleVec);
		DirectX::XMMATRIX localWithScale = scaleMat * finalMat;
		obj->m_transform.SetLocalMatrix(localWithScale);
	}


	/*for (auto& obj : AttachObjects)
	{
		auto mat = transform.GetLocalMatrix();
		
		obj->m_transform.SetLocalMatrix(mat);
	}*/


}
