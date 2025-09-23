#pragma once
#include "Core.Mathf.h"     // Vector2, Rect 등 수학 관련 클래스 포함
#include "Component.h"      // Component 클래스 포함
#include "AnchorPreset.h"
#include "RectTransformComponent.generated.h"

// Unity의 RectTransform과 유사한 동작을 하는 UI용 트랜스폼 컴포넌트입니다.
class GameObject;
class RectTransformComponent : public Component
{
public:
   ReflectRectTransformComponent
    [[Serializable(Inheritance:Component)]]
    RectTransformComponent();
    virtual ~RectTransformComponent() = default;

    // 레이아웃을 업데이트합니다. 부모의 월드 좌표계 사각형을 기준으로 자신의 위치와 크기를 계산합니다.
    void UpdateLayout(const Mathf::Rect& parentRect);

    // --- Getters & Setters ---
    const Mathf::Vector2& GetAnchorMin() const { return m_anchorMin; }
    void SetAnchorMin(const Mathf::Vector2& anchorMin);

    const Mathf::Vector2& GetAnchorMax() const { return m_anchorMax; }
    void SetAnchorMax(const Mathf::Vector2& anchorMax);

    Mathf::Vector2 GetAnchoredPosition() const;
    void SetAnchoredPosition(const Mathf::Vector2& position);

    const Mathf::Vector2& GetSizeDelta() const { return m_sizeDelta; }
    void SetSizeDelta(const Mathf::Vector2& size);

    const Mathf::Vector2& GetPivot() const { return m_pivot; }
    void SetPivot(const Mathf::Vector2& pivot);

    const Mathf::Rect& GetWorldRect() const { return m_worldRect; }

    // 앵커 프리셋을 설정하는 헬퍼 함수
    void SetAnchorPreset(AnchorPreset preset);

    // 부모를 교체(또는 앵커/피벗 변경)하면서도 현재 worldRect를 유지하도록 역산해서 재설정
    void SetAnchorsPivotKeepWorld(const Mathf::Vector2& newAnchorMin,
        const Mathf::Vector2& newAnchorMax,
        const Mathf::Vector2& newPivot,
        const Mathf::Rect& newParentRect);

    // newParent의 RectTransform(또는 화면 Rect)을 기준으로
    // 현재 worldRect를 유지한 채 부모를 바꾸고 싶을 때 호출
    void SetParentKeepWorldPosition(GameObject* newParent);

	bool IsDirty() const { return m_isDirty; }

private:
    // 부모 RectTransform의 사각형을 기준으로 한 최소/최대 앵커 위치 (0.0 ~ 1.0 비율)
    [[Property]]
    Mathf::Vector2 m_anchorMin = { 0.5f, 0.5f };
    [[Property]]
    Mathf::Vector2 m_anchorMax = { 0.5f, 0.5f };

    // 앵커로부터의 상대적인 위치 오프셋
    [[Property]]
    Mathf::Vector2 m_anchoredPosition = { 0.f, 0.f };

    // 앵커들이 한 점에 모여 있을 때의 크기(width, height) 또는
    // 앵커들이 떨어져 있을 때의 여백(margin) (left, top, right, bottom)
    [[Property]]
    Mathf::Vector2 m_sizeDelta = { 100.f, 100.f };

    // 자기 자신의 사각형 내에서의 중심점 (0.0 ~ 1.0 비율)
    // (0,0)은 좌측 하단, (1,1)은 우측 상단
    [[Property]]
    Mathf::Vector2 m_pivot = { 0.5f, 0.5f };

    // 계산된 월드 좌표계 상의 최종 사각형
    [[Property]]
    Mathf::Rect m_worldRect;

    // 레이아웃이 변경되었는지 여부
    bool m_isDirty = true;

    Mathf::Rect ResolveParentRect() const;
    Mathf::Vector2 CalculateDisplayPosition(const Mathf::Rect& parentRect) const;
    static float ToDisplay(float raw, float parentOrigin, float parentSize, float anchorMin, float anchorMax, float pivot);
    static float ToRaw(float display, float parentOrigin, float parentSize, float anchorMin, float anchorMax, float pivot);
};
