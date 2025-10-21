#include "TestShader.h"
#include "pch.h"
#include "MeshRenderer.h"
#include "Material.h"
void TestShader::Start()
{
	auto m = GetOwner()->GetComponent<MeshRenderer>();
	m->m_Material = m->m_Material->Instantiate(m->m_Material, "clonebomb");
}

void TestShader::Update(float tick)
{
	t += tick * timeScale;
	auto m = GetOwner()->GetComponent<MeshRenderer>();
	m->m_Material->TrySetValue("Param", "lerpValue", &t, sizeof(float));
	m->m_Material->TrySetValue("Param", "maxScale", &maxScale, sizeof(float));
	m->m_Material->TrySetValue("Param", "scaleFrequency", &scaleFrequency, sizeof(float));
	m->m_Material->TrySetValue("Param", "rotFrequency", &rotFrequency, sizeof(float));
	m->m_Material->TrySetValue("FlashBuffer", "flashStrength", &t, sizeof(float));
	m->m_Material->TrySetValue("FlashBuffer", "flashFrequency", &flashFrequency, sizeof(float));
}

