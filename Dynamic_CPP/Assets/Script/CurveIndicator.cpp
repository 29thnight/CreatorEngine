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
			MeshRenderer* render = object->GetComponent<MeshRenderer>();
			render->m_Material = render->m_Material->Instantiate(render->m_Material, "IndicatorMaterial");
			// 메터리얼의 쉐이더를 변경 후 투명도나 색상 변경.

			indicators.push_back(object);
			indicatorInitScale.push_back(Mathf::Vector3(object->m_transform.scale)); // 베이스로 할 스케일 저장
		}
	}
}

void CurveIndicator::Update(float tick)
{
	if(!enableIndicator)
		return;
	Vector3 direction = endPos - startPos;
	float length = direction.Length();
	int repeatCount = static_cast<int>(length / indicatorInterval);

	Vector3 pB = (direction / 2) + startPos;
	pB.y += heightPos;
	Vector3 pA = startPos;

	//float count = std::min((int)indicators.size(), repeatCount);

	int index = 0;
	for (auto& indicator : indicators) {
		indicator->SetEnabled(true);
		//float t = index / count;
		float d = indicatorInterval * index;
		float t = d / length;
		Vector3 p0 = Lerp(pA, pB, t);
		Vector3 p1 = Lerp(pB, endPos, t);
		Vector3 p01 = Lerp(p0, p1, t);
		indicator->m_transform.SetPosition(p01);
		float alpha = sinf(t);
		float indicatorScale = alpha * 3;
		indicatorScale = std::min(std::max(indicatorScale, 0.7f), 1.f);
		indicator->m_transform.SetScale(indicatorInitScale[index] * indicatorScale);
		indicator->GetComponent<MeshRenderer>()->m_Material->m_materialInfo.m_baseColor.w = alpha;
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

