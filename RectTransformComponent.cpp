#include "RectTransformComponent.h"

// 생성자
RectTransformComponent::RectTransformComponent()
{
    // 초기화가 필요한 경우 여기에 작성합니다.
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

    // TODO: 이 RectTransform의 변경이 자식 RectTransform들에게도 영향을 주므로,
    // 자식들의 레이아웃 업데이트를 여기서 촉발시켜야 합니다.
    // for (auto& child : children)
    // {
    //     child->GetComponent<RectTransformComponent>()->UpdateLayout(m_worldRect);
    // }
}

// 앵커 프리셋을 설정하는 헬퍼 함수
void RectTransformComponent::SetAnchorPreset(AnchorPreset preset)
{
    switch (preset)
    {
        // 상단
        case AnchorPreset::TopLeft:      { m_anchorMin = {0.0f, 1.0f}; m_anchorMax = {0.0f, 1.0f}; break; }
        case AnchorPreset::TopCenter:    { m_anchorMin = {0.5f, 1.0f}; m_anchorMax = {0.5f, 1.0f}; break; }
        case AnchorPreset::TopRight:     { m_anchorMin = {1.0f, 1.0f}; m_anchorMax = {1.0f, 1.0f}; break; }

        // 중단
        case AnchorPreset::MiddleLeft:   { m_anchorMin = {0.0f, 0.5f}; m_anchorMax = {0.0f, 0.5f}; break; }
        case AnchorPreset::MiddleCenter: { m_anchorMin = {0.5f, 0.5f}; m_anchorMax = {0.5f, 0.5f}; break; }
        case AnchorPreset::MiddleRight:  { m_anchorMin = {1.0f, 0.5f}; m_anchorMax = {1.0f, 0.5f}; break; }

        // 하단
        case AnchorPreset::BottomLeft:   { m_anchorMin = {0.0f, 0.0f}; m_anchorMax = {0.0f, 0.0f}; break; }
        case AnchorPreset::BottomCenter: { m_anchorMin = {0.5f, 0.0f}; m_anchorMax = {0.5f, 0.0f}; break; }
        case AnchorPreset::BottomRight:  { m_anchorMin = {1.0f, 0.0f}; m_anchorMax = {1.0f, 0.0f}; break; }

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
