#include "CurveIndicator.h"
#include "pch.h"
#include "MeshRenderer.h"
#include "Material.h"
using namespace Mathf;

void CurveIndicator::Start()
{
	for (auto& index : GetOwner()->m_childrenIndices) {
		auto object = GameObject::FindIndex(index);
		if (object) {
			indicators.push_back(object);
		}
	}
}

void CurveIndicator::Update(float tick)
{
	if(!enableIndicator)
		return;

	Vector3 pB = ((endPos - startPos) / 2) + startPos;
	pB.y += heightPos;
	Vector3 pA = startPos;

	float count = (float)indicators.size();

	int index = 0;
	timer += tick;
	for (auto& indicator : indicators) {
		float t = index / count * 1.5f;
		Vector3 p0 = Lerp(pA, pB, t);
		Vector3 p1 = Lerp(pB, endPos, t);
		Vector3 p01 = Lerp(p0, p1, t);
		indicator->m_transform.SetPosition(p01);
		indicator->GetComponent<MeshRenderer>()->m_Material->m_materialInfo.m_baseColor.w = sinf(timer) * 0.5f + 0.5f;//->TrySetFloat("TestCB", "timer", timer);
		index++;
	}
}

void CurveIndicator::SetIndicator(Mathf::Vector3 start, Mathf::Vector3 end, float height)
{
	startPos = start;
	endPos = end;
	startPos.y += 0.5f;
	endPos.y += 0.5f;
	heightPos = height;
}

void CurveIndicator::EnableIndicator(bool enable)
{
	enableIndicator = enable;
	for (auto& indicator : indicators) {
		indicator->SetEnabled(enable);
	}
}

