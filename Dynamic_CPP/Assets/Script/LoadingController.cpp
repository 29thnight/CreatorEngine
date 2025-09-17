#include "LoadingController.h"
#include "ImageComponent.h"
#include "TextComponent.h"
#include "GameInstance.h"
#include "pch.h"

void LoadingController::Start()
{
	m_loadingImage = GetComponent<ImageComponent>();
	int childIndex = GetOwner()->m_childrenIndices[0];
	GameObject* child = GameObject::FindIndex(childIndex);
	if (child)
	{
		m_loadingText = child->GetComponent<TextComponent>();
	}
}

void LoadingController::Update(float tick)
{
	if (/*!GameInstance::GetInstance()->IsLoadSceneComplete()*/true)
	{
		if (m_loadingImage)
		{
			//�̹��� ȸ��
			m_loadingImage->rotate += m_rotateDegree * tick;
		}
		if (m_loadingText)
		{
			//�� 3������ �þ�� �ٽ� �ʱ�ȭ
			m_dotTimer -= tick;
			if (m_dotTimer <= 0.f)
			{
				m_dotTimer += 0.5f;                // �帮��Ʈ ���̷��� += ���
				m_dotIdx = (m_dotIdx + 1) & 3;     // 0~3 ��ȯ (mod 4)

				static const char* kFrames[4] = {
					"Loading", "Loading.", "Loading..", "Loading..."
				};
				m_loadingText->SetMessage(kFrames[m_dotIdx]);
			}
		}
	}
}

