#include "LoadingController.h"
#include "ImageComponent.h"
#include "TextComponent.h"
#include "pch.h"

void LoadingController::Start()
{
	m_loadingImage = GetComponentPtr<ImageComponent>();
	int childIndex = GetOwner()->m_childrenIndices[0];
	GameObject* child = GameObject::FindIndex(childIndex);
	if (child)
	{
		m_loadingText = child->GetComponent<TextComponent>();
	}
}

void LoadingController::Update(float tick)
{
}

