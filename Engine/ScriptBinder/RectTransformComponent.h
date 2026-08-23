#pragma once
#include "Core.Mathf.h"     // Vector2, Rect 등 수학 관련 클래스 포함
#include "Component.h"      // Component 클래스 포함
#include "AnchorPreset.h"

// Unity의 RectTransform과 유사한 동작을 하는 UI용 트랜스폼 컴포넌트입니다.
//
// ─────────────────────────────────────────────────────────────────────────
// 좌표 규약 (PHASE 7-2에서 확정)
//
//   원점은 좌상단, y는 아래로 증가한다(y-down).
//
// 근거: AnchorPreset 표가 TopLeft=(0,0), BottomLeft=(0,1)로 y-down이고,
// 실제 저작 데이터도 화면 상단 요소가 작은 y를 갖는다. 이전 주석 일부가
// "(0,0)은 좌측 하단"이라 적혀 y-up처럼 읽혔으나 구현·데이터 어디에도
// 근거가 없었다(분석 문서 F-5). 세 곳 중 주석만 틀렸으므로 주석을 고쳤다.
//
// 앵커 비율도 같은 방향이다 — anchorMin.y=0이 부모의 위쪽 변, 1이 아래쪽 변.
// pivot도 마찬가지로 (0,0)이 좌상단이다.
// ─────────────────────────────────────────────────────────────────────────
class Entity;

// 앵커 프리셋 하나의 실제 값. 표는 아래 GetAnchorPresetTable() 한 곳에만 있다.
struct AnchorPresetEntry
{
    AnchorPreset preset;
    Mathf::Vector2 anchorMin;
    Mathf::Vector2 anchorMax;
    const char* label;
};

// 프리셋 → 앵커 값 표(16종). 인스펙터가 별도 표를 복제해 두고 있었고 양쪽 모두
// 스트레치 6종이 3종으로 뭉개져 있었다(분석 문서 F-6). 출처를 하나로 모은다.
const AnchorPresetEntry* GetAnchorPresetTable();
size_t GetAnchorPresetCount();
const AnchorPresetEntry& GetAnchorPresetEntry(AnchorPreset preset);

class RectTransformComponent : public meta::identity<RectTransformComponent, Component>
{
   public:
   static consteval auto reflect()
   {
       return meta::schema<Self>(
           meta::field<&Self::m_anchorMin>,
           meta::field<&Self::m_anchorMax>,
           meta::field<&Self::m_anchoredPosition>,
           meta::field<&Self::m_sizeDelta>,
           meta::field<&Self::m_pivot>);
   }
public:
    RectTransformComponent();
    virtual ~RectTransformComponent() = default;

    // 부모의 월드 사각형을 기준으로 자신의 위치와 크기를 계산한다.
    //
    // 자식으로 내려가지 않는다 — 순회는 드라이버(Scene::LayoutUINode)가 맡는다.
    // 예전에는 이 안에서 자식 재귀를 돌았고, 그래서 같은 갱신이 드라이버 2곳과
    // 여기까지 세 군데에 흩어져 순서와 횟수를 추론할 수 없었다(분석 문서 F-9).
    // 게다가 그 재귀가 병렬 순회 중에도 불려 thread_local 순환 가드로는 막을 수 없는
    // 교차 스레드 접근이 있었다.
    //
    // 반환값은 worldRect가 실제로 바뀌었는지다. 드라이버가 이 값으로 자식의 dirty를
    // 결정한다 — "부모 rect가 변하면 자식도 다시 계산한다"는 규칙의 구현이다(F-10).
    bool UpdateLayout(const Mathf::Rect& parentRect);

    // 다음 레이아웃 때 다시 계산하게 만든다. 드라이버가 부모 변경을 전파할 때 쓴다.
    void MarkDirty() { m_isDirty = true; }

    // 최상위 UI가 부모로 삼는 화면 rect. 렌더 레이아웃의 중심 원점 규약
    // (-width/2,-height/2,width,height)이 적용된 최종 값이다.
    static Mathf::Rect GetScreenRootRect();

    // 최상위 캔버스를 화면 크기에 맞춰 직접 구동한다(PHASE 7-1).
    //
    // 최상위 캔버스는 부모에 RectTransform이 없어서 프레임 레이아웃 드라이버가
    // 통째로 건너뛴다. 그래서 worldRect가 로드 시점 값으로 동결되고, 화면 크기가
    // 바뀌어도 UI 전체가 저작 해상도에 남아 있었다(분석 문서 F-1).
    //
    // 앵커 계산 대신 값을 직접 넣는 이유는 uGUI와 같다 — 캔버스 rect는 화면이 정하는
    // 것이지 자기 앵커로 계산할 대상이 아니다. 실제로 이 데이터의 캔버스는
    // 점 앵커(0.5) + 고정 sizeDelta라, 앵커 계산에 맡기면 화면이 줄어도 크기가
    // 그대로 남고 위치만 어긋난다.
    //
    // 인자는 GetScreenRootRect()가 준 최종 rect이고, scale은 이 캔버스가 자식들에게
    // 물려줄 배율(Canvas::ComputeScaleFactor의 결과)이다.
    // UpdateLayout과 마찬가지로 반환값은 rect가 바뀌었는지이며, 자식 순회는 하지 않는다.
    bool DriveAsCanvasRoot(const Mathf::Rect& screenRootRect, float scale);

    // World Space Canvas 루트. sizeDelta/pivot을 Canvas 로컬 평면의 rect로
    // 해석하고 Transform은 건드리지 않는다.
    bool DriveAsWorldCanvasRoot();

    // ── 레이아웃 배율(PHASE 7-3) ──
    //
    // 이 노드의 anchoredPosition·sizeDelta에 곱해지는 값이다. 캔버스 루트가 정하고
    // 계층을 따라 그대로 내려간다.
    //
    // uGUI는 캔버스 rect를 기준 해상도 단위로 두고 렌더 시점에 배율을 곱하지만,
    // 여기서는 rect를 화면 픽셀로 유지한 채 오프셋에만 배율을 곱한다. 부모 rect가
    // 이미 배율이 반영된 픽셀이라 (기준크기 x 배율 = 화면크기) 두 방식은 수식이
    // 같은 결과를 낸다. worldRect는 중심 원점의 화면 픽셀을 유지하고, 입력은
    // UIButton 경계에서 좌상단 원점 좌표를 이 좌표계로 한 번 변환한다.
    float GetLayoutScale() const { return m_layoutScale; }
    void SetLayoutScale(float scale);

    // --- Getters & Setters ---
    const Mathf::Vector2& GetAnchorMin() const { return m_anchorMin; }
    void SetAnchorMin(const Mathf::Vector2& anchorMin);

    const Mathf::Vector2& GetAnchorMax() const { return m_anchorMax; }
    void SetAnchorMax(const Mathf::Vector2& anchorMax);

    // 부모 앵커 기준점에서 이 rect의 pivot까지의 로컬 오프셋.
    //
    // 예전 구현은 이 이름으로 중심 원점의 월드 pivot 좌표를 반환/입력했다. 그래서
    // 같은 anchoredPosition을 가진 TopLeft와 BottomRight가 서로 다른 앵커를 따라가지
    // 않고 같은 화면 위치에 겹쳤다. 직렬화 필드 m_anchoredPosition이 원래부터 갖고
    // 있던 의미와 공개 API를 일치시킨다.
    const Mathf::Vector2& GetAnchoredPosition() const { return m_anchoredPosition; }
    void SetAnchoredPosition(const Mathf::Vector2& position);

    // 중심 원점 레이아웃 좌표계에서의 pivot 위치. Scene View처럼 월드 픽셀 단위로
    // 드래그하는 소비자가 anchoredPosition과 혼동하지 않도록 명시적으로 분리한다.
    Mathf::Vector2 GetWorldPivotPosition() const;
    void SetWorldPivotPosition(const Mathf::Vector2& position);

    // 좌상단 (0,0), 우하단 (screenWidth,screenHeight)인 화면 픽셀 좌표.
    // Camera.WorldToScreenPoint의 반환 규약과 직접 맞물리는 API다.
    Mathf::Vector2 GetScreenPosition() const;
    void SetScreenPosition(const Mathf::Vector2& position);

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
    void SetParentKeepWorldPosition(Entity* newParent);

	bool IsDirty() const { return m_isDirty; }

private:
    // 부모 RectTransform의 사각형을 기준으로 한 최소/최대 앵커 위치 (0.0 ~ 1.0 비율)
    Mathf::Vector2 m_anchorMin = { 0.5f, 0.5f };
    Mathf::Vector2 m_anchorMax = { 0.5f, 0.5f };

    // 앵커로부터의 상대적인 위치 오프셋
    Mathf::Vector2 m_anchoredPosition = { 0.f, 0.f };

    // 앵커들이 한 점에 모여 있을 때의 크기(width, height). 앵커가 떨어져 있으면
    // 부모의 anchor span에 더해지는 크기 차이(size delta)다.
    Mathf::Vector2 m_sizeDelta = { 100.f, 100.f };

    // 자기 자신의 사각형 내에서의 중심점 (0.0 ~ 1.0 비율)
    // (0,0)은 좌측 상단, (1,1)은 우측 하단 (파일 상단 좌표 규약 참고)
    Mathf::Vector2 m_pivot = { 0.5f, 0.5f };

    // 계산된 월드 좌표계 상의 최종 사각형.
    //
    // 직렬화하지 않는다(PHASE 7-2). 위 다섯 값과 부모 rect에서 전부 유도되는
    // 파생값인데, 저장해 두면 로드 시점에 "이미 계산된 것처럼 보이는" 상태가 되어
    // 저작 해상도의 결과가 그대로 굳는다(분석 문서 F-2). 실제로 이것이 7-1 이전
    // 고정 해상도 증상의 절반이었다 — 드라이버가 안 도는 노드도 값이 있어서
    // 문제가 드러나지 않았다.
    //
    // 기존 파일에 남은 m_worldRect 키는 무해하다. 역직렬화가 등록된 프로퍼티만
    // 조회하므로 모르는 키는 읽히지 않는다 — 에셋을 고칠 필요가 없다.
    Mathf::Rect m_worldRect;

    // 레이아웃이 변경되었는지 여부
    bool m_isDirty = true;

    // 캔버스에서 물려받은 배율. 파생값이라 직렬화하지 않는다(m_worldRect와 같은 이유).
    float m_layoutScale = 1.f;

    // 부동소수 오차를 감안한 rect 비교. "바뀌었는가" 판정을 한 곳에서 한다.
    static bool NearlyEqual(const Mathf::Rect& a, const Mathf::Rect& b);

    // 부모 RectTransform의 worldRect, 없으면 화면 전체 rect.
    Mathf::Rect ResolveParentRect() const;

    // 부모 RectTransform의 배율, 없으면 1.
    float ResolveParentScale() const;

    Mathf::Vector2 CalculateWorldPivotPosition(const Mathf::Rect& parentRect) const;
    static float ToWorldPivot(float anchored, float parentOrigin, float parentSize,
        float anchorMin, float anchorMax, float pivot, float scale);
    static float FromWorldPivot(float worldPivot, float parentOrigin, float parentSize,
        float anchorMin, float anchorMax, float pivot, float scale);
};
