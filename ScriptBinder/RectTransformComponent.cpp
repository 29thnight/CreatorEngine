#include "RectTransformComponent.h"
#include "GameObject.h"
#include "DeviceState.h"
#include <unordered_set>

// 생성자
RectTransformComponent::RectTransformComponent()
{
    // 초기화가 필요한 경우 여기에 작성합니다.
    m_name = "RectTransformComponent";
    m_typeID = type_guid(RectTransformComponent);
}

Mathf::Vector2 RectTransformComponent::GetAnchoredPosition() const
{
    const auto parentRect = ResolveParentRect();
    return CalculateDisplayPosition(parentRect);
}

void RectTransformComponent::SetAnchoredPosition(const Mathf::Vector2& position)
{
    const auto parentRect = ResolveParentRect();
    m_anchoredPosition.x = ToRaw(position.x, parentRect.x, parentRect.width, m_anchorMin.x, m_anchorMax.x, m_pivot.x);
    m_anchoredPosition.y = ToRaw(position.y, parentRect.y, parentRect.height, m_anchorMin.y, m_anchorMax.y, m_pivot.y);
    m_isDirty = true;
}

void RectTransformComponent::SetAnchorMin(const Mathf::Vector2& anchorMin)
{
    const auto parentRect = ResolveParentRect();
    const auto display = CalculateDisplayPosition(parentRect);
    m_anchorMin = anchorMin;
    m_isDirty = true;
    m_anchoredPosition.x = ToRaw(display.x, parentRect.x, parentRect.width, m_anchorMin.x, m_anchorMax.x, m_pivot.x);
    m_anchoredPosition.y = ToRaw(display.y, parentRect.y, parentRect.height, m_anchorMin.y, m_anchorMax.y, m_pivot.y);
}

void RectTransformComponent::SetAnchorMax(const Mathf::Vector2& anchorMax)
{
    const auto parentRect = ResolveParentRect();
    const auto display = CalculateDisplayPosition(parentRect);
    m_anchorMax = anchorMax;
    m_isDirty = true;
    m_anchoredPosition.x = ToRaw(display.x, parentRect.x, parentRect.width, m_anchorMin.x, m_anchorMax.x, m_pivot.x);
    m_anchoredPosition.y = ToRaw(display.y, parentRect.y, parentRect.height, m_anchorMin.y, m_anchorMax.y, m_pivot.y);
}

void RectTransformComponent::SetSizeDelta(const Mathf::Vector2& size)
{
    const auto parentRect = ResolveParentRect();
    const auto display = CalculateDisplayPosition(parentRect);
    m_sizeDelta = size;
    m_anchoredPosition.x = ToRaw(display.x, parentRect.x, parentRect.width, m_anchorMin.x, m_anchorMax.x, m_pivot.x);
    m_anchoredPosition.y = ToRaw(display.y, parentRect.y, parentRect.height, m_anchorMin.y, m_anchorMax.y, m_pivot.y);
    m_isDirty = true;
}

void RectTransformComponent::SetPivot(const Mathf::Vector2& pivot)
{
    const auto parentRect = ResolveParentRect();
    const auto display = CalculateDisplayPosition(parentRect);
    m_pivot = pivot;
    m_isDirty = true;
    m_anchoredPosition.x = ToRaw(display.x, parentRect.x, parentRect.width, m_anchorMin.x, m_anchorMax.x, m_pivot.x);
    m_anchoredPosition.y = ToRaw(display.y, parentRect.y, parentRect.height, m_anchorMin.y, m_anchorMax.y, m_pivot.y);
}

Mathf::Rect RectTransformComponent::ResolveParentRect() const
{
    Mathf::Rect parentRect{ 0.f, 0.f,
        DirectX11::DeviceStates->g_ClientRect.width,
        DirectX11::DeviceStates->g_ClientRect.height };

    if (m_pOwner)
    {
        if (GameObject::IsValidIndex(m_pOwner->m_parentIndex))
        {
            if (auto* parentObj = GameObject::FindIndex(m_pOwner->m_parentIndex))
            {
                if (auto* parentRT = parentObj->GetComponent<RectTransformComponent>())
                {
                    parentRect = parentRT->GetWorldRect();
                }
            }
        }
    }

    return parentRect;
}

Mathf::Vector2 RectTransformComponent::CalculateDisplayPosition(const Mathf::Rect& parentRect) const
{
    return {
        ToDisplay(m_anchoredPosition.x, parentRect.x, parentRect.width, m_anchorMin.x, m_anchorMax.x, m_pivot.x),
        ToDisplay(m_anchoredPosition.y, parentRect.y, parentRect.height, m_anchorMin.y, m_anchorMax.y, m_pivot.y)
    };
}

float RectTransformComponent::ToDisplay(float raw, float parentOrigin, float parentSize, float anchorMin, float anchorMax, float pivot)
{
    const float anchorCenter = anchorMin + (anchorMax - anchorMin) * pivot;
    return parentOrigin + parentSize * anchorCenter + raw;
}

float RectTransformComponent::ToRaw(float display, float parentOrigin, float parentSize, float anchorMin, float anchorMax, float pivot)
{
    const float anchorCenter = anchorMin + (anchorMax - anchorMin) * pivot;
    return display - (parentOrigin + parentSize * anchorCenter);
}

// 레이아웃 업데이트 함수: 가장 핵심적인 로직입니다.
void RectTransformComponent::UpdateLayout(const Mathf::Rect& parentRect)
{
    // isDirty 플래그를 확인하여 변경이 있을 때만 계산을 수행합니다.
    if (!m_isDirty) return;

    // 1. anchoredPosition, sizeDelta, pivot을 기반으로 offsetMin과 offsetMax를 계산합니다.
    // offsetMin: 앵커의 좌측 하단에서 UI 요소의 좌측 하단까지의 거리
    // offsetMax: 앵커의 우측 상단에서 UI 요소의 우측 상단까지의 거리
    Mathf::Vector2 offsetMin = m_anchoredPosition - Mathf::Vector2(m_sizeDelta.x * m_pivot.x, m_sizeDelta.y * m_pivot.y);
    Mathf::Vector2 offsetMax = m_anchoredPosition + Mathf::Vector2(m_sizeDelta.x * (1.0f - m_pivot.x), m_sizeDelta.y * (1.0f - m_pivot.y));

    // 2. 부모의 Rect와 앵커를 사용하여 UI 요소의 월드 좌표를 계산합니다.
    m_worldRect.x = parentRect.x + (parentRect.width * m_anchorMin.x) + offsetMin.x;
    m_worldRect.y = parentRect.y + (parentRect.height * m_anchorMin.y) + offsetMin.y;

    float xMax = parentRect.x + (parentRect.width * m_anchorMax.x) + offsetMax.x;
    float yMax = parentRect.y + (parentRect.height * m_anchorMax.y) + offsetMax.y;

    m_worldRect.width = xMax - m_worldRect.x;
    m_worldRect.height = yMax - m_worldRect.y;

    // 3. 계산이 완료되었으므로 isDirty 플래그를 false로 설정합니다.
    m_isDirty = false;

    if (m_pOwner)
    {
        static thread_local std::unordered_set<GameObject*> visited;
        if (!visited.insert(m_pOwner).second)
            return;

        for (auto childIndex : m_pOwner->m_childrenIndices)
        {
            if (GameObject* child = GameObject::FindIndex(childIndex))
            {
                if (auto* rect = child->GetComponent<RectTransformComponent>())
                {
					rect->m_isDirty = true; // 자식 RectTransform도 업데이트 필요
                    rect->UpdateLayout(m_worldRect);
                }
            }
        }

        visited.erase(m_pOwner);
    }
}

// 앵커 프리셋을 설정하는 헬퍼 함수
void RectTransformComponent::SetAnchorPreset(AnchorPreset preset)
{
    switch (preset)
    {
        // 상단
        case AnchorPreset::TopLeft:      { m_anchorMin = {0.0f, 0.0f}; m_anchorMax = {0.0f, 0.0f}; break; }
        case AnchorPreset::TopCenter:    { m_anchorMin = {0.5f, 0.0f}; m_anchorMax = {0.5f, 0.0f}; break; }
        case AnchorPreset::TopRight:     { m_anchorMin = {1.0f, 0.0f}; m_anchorMax = {1.0f, 0.0f}; break; }

        // 중단
        case AnchorPreset::MiddleLeft:   { m_anchorMin = {0.0f, 0.5f}; m_anchorMax = {0.0f, 0.5f}; break; }
        case AnchorPreset::MiddleCenter: { m_anchorMin = {0.5f, 0.5f}; m_anchorMax = {0.5f, 0.5f}; break; }
        case AnchorPreset::MiddleRight:  { m_anchorMin = {1.0f, 0.5f}; m_anchorMax = {1.0f, 0.5f}; break; }

        // 하단
        case AnchorPreset::BottomLeft:   { m_anchorMin = {0.0f, 1.0f}; m_anchorMax = {0.0f, 1.0f}; break; }
        case AnchorPreset::BottomCenter: { m_anchorMin = {0.5f, 1.0f}; m_anchorMax = {0.5f, 1.0f}; break; }
        case AnchorPreset::BottomRight:  { m_anchorMin = {1.0f, 1.0f}; m_anchorMax = {1.0f, 1.0f}; break; }

        // 가로 스트레치
        case AnchorPreset::StretchLeft:   { m_anchorMin = {0.0f, 0.5f}; m_anchorMax = {1.0f, 0.5f}; break; }
        case AnchorPreset::StretchCenter: { m_anchorMin = {0.0f, 0.5f}; m_anchorMax = {1.0f, 0.5f}; break; } // Middle과 동일
        case AnchorPreset::StretchRight:  { m_anchorMin = {0.0f, 0.5f}; m_anchorMax = {1.0f, 0.5f}; break; } // Middle과 동일

        // 세로 스트레치
        case AnchorPreset::StretchTop:    { m_anchorMin = {0.5f, 0.0f}; m_anchorMax = {0.5f, 1.0f}; break; }
        case AnchorPreset::StretchMiddle: { m_anchorMin = {0.5f, 0.0f}; m_anchorMax = {0.5f, 1.0f}; break; } // Top과 동일
        case AnchorPreset::StretchBottom: { m_anchorMin = {0.5f, 0.0f}; m_anchorMax = {0.5f, 1.0f}; break; } // Top과 동일

        // 전체 스트레치
        case AnchorPreset::StretchAll:   { m_anchorMin = {0.0f, 0.0f}; m_anchorMax = {1.0f, 1.0f}; break; }
    }
    m_isDirty = true;
}

static inline float SolveSize(float worldSize, float parentSize, float aMin, float aMax)
{
    return worldSize - parentSize * (aMax - aMin);
}

static inline float SolveAnchored(float worldMin, float parentMin, float parentSize,
    float aMin, float size, float pivot)
{
    // worldMin = parentMin + parentSize*aMin + (anchored - size*pivot)
    // anchored = (worldMin - parentMin - parentSize*aMin) + size*pivot
    return (worldMin - parentMin - parentSize * aMin) + size * pivot;
}

void RectTransformComponent::SetAnchorsPivotKeepWorld(const Mathf::Vector2& newAnchorMin, const Mathf::Vector2& newAnchorMax, const Mathf::Vector2& newPivot, const Mathf::Rect& newParentRect)
{
    // 1) 현재 보이는 위치/크기를 보전(타겟 worldRect)
    const Mathf::Rect desired = m_worldRect;

    // 2) 새 앵커/피벗을 먼저 적용
    m_anchorMin = newAnchorMin;
    m_anchorMax = newAnchorMax;
    m_pivot = newPivot;

    // 3) 역산하여 sizeDelta/anchoredPosition 재설정 (x/y 축별 독립)
    m_sizeDelta.x = SolveSize(desired.width, newParentRect.width, m_anchorMin.x, m_anchorMax.x);
    m_sizeDelta.y = SolveSize(desired.height, newParentRect.height, m_anchorMin.y, m_anchorMax.y);

    m_anchoredPosition.x = SolveAnchored(desired.x, newParentRect.x, newParentRect.width,
        m_anchorMin.x, m_sizeDelta.x, m_pivot.x);
    m_anchoredPosition.y = SolveAnchored(desired.y, newParentRect.y, newParentRect.height,
        m_anchorMin.y, m_sizeDelta.y, m_pivot.y);

    m_isDirty = true;
    UpdateLayout(newParentRect); // 부모 Rect로 다시 worldRect 갱신
}

void RectTransformComponent::SetParentKeepWorldPosition(GameObject* newParent)
{
    // 새 부모의 Rect를 얻고, 없으면 화면 전체 Rect를 부모로 가정
    Mathf::Rect newParentRect{ 0, 0,
        DirectX11::DeviceStates->g_ClientRect.width,
        DirectX11::DeviceStates->g_ClientRect.height };

    if (newParent)
    {
        if (auto* pr = newParent->GetComponent<RectTransformComponent>())
            newParentRect = pr->GetWorldRect();
    }

    // 현재 앵커/피벗을 유지한 채로 역산 적용 (원하면 여기서 preset/pivot도 바꿀 수 있음)
    SetAnchorsPivotKeepWorld(m_anchorMin, m_anchorMax, m_pivot, newParentRect);
}
