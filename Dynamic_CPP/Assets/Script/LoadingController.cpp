#include "LoadingController.h"
#include "ImageComponent.h"
#include "TextComponent.h"
#include "GameInstance.h"
#include "GameManager.h"
#include "pch.h"

void LoadingController::Start()
{
	m_loadingImage = GetComponent<ImageComponent>();
	int childIndex = GetOwner()->m_childrenIndices[0];
	GameObject* child = GameObject::FindIndex(childIndex);
	if (child)
	{
		m_loadingText = child->GetComponent<TextComponent>();
		int koriChildIndex = child->m_childrenIndices[0];
		GameObject* koriChild = GameObject::FindIndex(koriChildIndex);
		if (koriChild)
		{
			m_koriIcon = koriChild->GetComponent<ImageComponent>();
		}
	}

	GameObject* gmObj = GameObject::Find("GameManager");
	if (gmObj)
	{
		m_gameManager = gmObj->GetComponent<GameManager>();
		if (m_gameManager)
		{
			int loadSceneType = GameInstance::GetInstance()->GetAfterLoadSceneIndex();
			m_gameManager->m_nextSceneIndex = loadSceneType;
			m_gameManager->LoadNextScene();
			GameInstance::GetInstance()->SetAfterLoadSceneIndex(); // 초기화
		}
	}
}

void LoadingController::Update(float tick)
{
	//if (!GameInstance::GetInstance()->IsLoadSceneComplete())
	//{
	//	if (m_loadingImage)
	//	{
	//		//이미지 회전
	//		m_loadingImage->rotate += m_rotateDegree * tick;
	//	}
	//	if (m_loadingText)
	//	{
	//		//점 3개까지 늘어나고 다시 초기화
	//		m_dotTimer -= tick;
	//		if (m_dotTimer <= 0.f)
	//		{
	//			m_dotTimer += 0.5f;                // 드리프트 줄이려면 += 사용
	//			m_dotIdx = (m_dotIdx + 1) & 3;     // 0~3 순환 (mod 4)

	//			static const char* kFrames[4] = {
	//				"Loading", "Loading.", "Loading..", "Loading..."
	//			};
	//			m_loadingText->SetMessage(kFrames[m_dotIdx]);
	//		}
	//	}
	//}
	//else
	//{
	//	m_loadingImage->SetEnabled(false);
	//	m_koriIcon->SetEnabled(false);
	//	m_loadingText->SetMessage("Load Complete");
	//}

		// 한 글자씩 나타날 기준 문자열
	static const std::string kBase = "Loading...";
	// 미리 만들어 두는 프레임들: "", "L", "Lo", "Loa", ... , "Loading..."
	static std::vector<std::string> kFrames;

	if (kFrames.empty()) {
		kFrames.reserve(kBase.size() + 1);
		for (size_t i = 0; i <= kBase.size(); ++i) {
			kFrames.emplace_back(kBase.substr(0, i));
		}
	}

	if (!GameInstance::GetInstance()->IsLoadSceneComplete())
	{
		if (m_loadingImage)
		{
			// 이미지 회전
			m_loadingImage->rotate += m_rotateDegree * tick;
		}

		if (m_loadingText)
		{
			// 글자 수를 하나씩 늘렸다가 끝나면 다시 처음으로
			m_dotTimer -= tick;
			if (m_dotTimer <= 0.f)
			{
				m_dotTimer += 0.1f; // 속도 조절(한 글자당 0.1초). 원하면 노출 간격 조절
				// 0 ~ kFrames.size()-1 순환
				m_dotIdx = (m_dotIdx + 1) % static_cast<int>(kFrames.size());

				m_loadingText->SetMessage(kFrames[m_dotIdx]);
			}
		}
	}
	else
	{
		if (m_loadingImage)  m_loadingImage->SetEnabled(false);
		if (m_koriIcon)      m_koriIcon->SetEnabled(false);
		if (m_loadingText)   m_loadingText->SetMessage("Load Complete");
	}
}

