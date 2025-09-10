#include "UIGageTest.h"
#include "ImageComponent.h"
#include "pch.h"

void UIGageTest::Start()
{
	m_imageComponent = GetOwner()->GetComponent<ImageComponent>();

	if (!m_imageComponent)
	{
		std::cout << "UIGageTest: ImageComponent not found!" << std::endl;
		return;
	}

	m_imageComponent->SetFloat2("centerUV", centerUV);
	m_imageComponent->SetFloat("radiusUV", radiusUV);
	m_imageComponent->SetFloat("percent", percent);
	m_imageComponent->SetFloat("startAngle", startAngle);
	m_imageComponent->SetInt("clockwise", clockwise);
	m_imageComponent->SetFloat("featherAngle", featherAngle);
	m_imageComponent->SetFloat4("tint", tint);
}

void UIGageTest::Update(float tick)
{
	if (!m_imageComponent)
		return;

	m_imageComponent->SetFloat2("centerUV", centerUV);
	m_imageComponent->SetFloat("radiusUV", radiusUV);
	m_imageComponent->SetFloat("percent", percent);
	m_imageComponent->SetFloat("startAngle", startAngle);
	m_imageComponent->SetInt("clockwise", clockwise);
	m_imageComponent->SetFloat("featherAngle", featherAngle);
	m_imageComponent->SetFloat4("tint", tint);
}

