#include "RectTransformComponent.h"
#include "RHI/ScreenSizedResource.h"
#include "Entity.h"
#include <iterator>
#include <unordered_set>

// 생성자
RectTransformComponent::RectTransformComponent()
{
    // 초기화가 필요한 경우 여기에 작성합니다.
}

void RectTransformComponent::SetAnchoredPosition(const Mathf::Vector2& position)
{
    m_anchoredPosition = position;
    m_isDirty = true;
}

Mathf::Vector2 RectTransformComponent::GetWorldPivotPosition() const
{
    return CalculateWorldPivotPosition(ResolveParentRect());
}

void RectTransformComponent::SetWorldPivotPosition(const Mathf::Vector2& position)
{
    const auto parentRect = ResolveParentRect();
    m_anchoredPosition.x = FromWorldPivot(position.x, parentRect.x, parentRect.width,
        m_anchorMin.x, m_anchorMax.x, m_pivot.x, m_layoutScale);
    m_anchoredPosition.y = FromWorldPivot(position.y, parentRect.y, parentRect.height,
        m_anchorMin.y, m_anchorMax.y, m_pivot.y, m_layoutScale);
    m_isDirty = true;
}

Mathf::Vector2 RectTransformComponent::GetScreenPosition() const
{
    const auto worldPivot = GetWorldPivotPosition();
    const auto screenRoot = GetScreenRootRect();
    return { worldPivot.x - screenRoot.x, worldPivot.y - screenRoot.y };
}

void RectTransformComponent::SetScreenPosition(const Mathf::Vector2& position)
{
    const auto screenRoot = GetScreenRootRect();
    SetWorldPivotPosition({ position.x + screenRoot.x, position.y + screenRoot.y });
}

void RectTransformComponent::SetAnchorMin(const Mathf::Vector2& anchorMin)
{
    const auto parentRect = ResolveParentRect();
    const auto worldPivot = CalculateWorldPivotPosition(parentRect);
    m_anchorMin = anchorMin;
    m_isDirty = true;
    m_anchoredPosition.x = FromWorldPivot(worldPivot.x, parentRect.x, parentRect.width, m_anchorMin.x, m_anchorMax.x, m_pivot.x, m_layoutScale);
    m_anchoredPosition.y = FromWorldPivot(worldPivot.y, parentRect.y, parentRect.height, m_anchorMin.y, m_anchorMax.y, m_pivot.y, m_layoutScale);
}

void RectTransformComponent::SetAnchorMax(const Mathf::Vector2& anchorMax)
{
    const auto parentRect = ResolveParentRect();
    const auto worldPivot = CalculateWorldPivotPosition(parentRect);
    m_anchorMax = anchorMax;
    m_isDirty = true;
    m_anchoredPosition.x = FromWorldPivot(worldPivot.x, parentRect.x, parentRect.width, m_anchorMin.x, m_anchorMax.x, m_pivot.x, m_layoutScale);
    m_anchoredPosition.y = FromWorldPivot(worldPivot.y, parentRect.y, parentRect.height, m_anchorMin.y, m_anchorMax.y, m_pivot.y, m_layoutScale);
}

void RectTransformComponent::SetSizeDelta(const Mathf::Vector2& size)
{
    m_sizeDelta = size;
    m_isDirty = true;
}

void RectTransformComponent::SetPivot(const Mathf::Vector2& pivot)
{
    const auto parentRect = ResolveParentRect();
    const auto worldPivot = CalculateWorldPivotPosition(parentRect);
    m_pivot = pivot;
    m_isDirty = true;
    m_anchoredPosition.x = FromWorldPivot(worldPivot.x, parentRect.x, parentRect.width, m_anchorMin.x, m_anchorMax.x, m_pivot.x, m_layoutScale);
    m_anchoredPosition.y = FromWorldPivot(worldPivot.y, parentRect.y, parentRect.height, m_anchorMin.y, m_anchorMax.y, m_pivot.y, m_layoutScale);
}

void RectTransformComponent::SetLayoutScale(float scale)
{
    constexpr float kMinScale = 0.01f;
    const float clamped = scale > kMinScale ? scale : kMinScale;

    if (std::abs(m_layoutScale - clamped) < 1e-6f) return;

    m_layoutScale = clamped;
    m_isDirty = true;
}

float RectTransformComponent::ResolveParentScale() const
{
	if (m_pOwner)
    {
		const Entity::Index parentIndex = m_pOwner->GetParentIndex();
		if (Entity::IsValidIndex(parentIndex))
        {
			if (auto* parentObj = m_pOwner->OwnerSceneFindIndex(parentIndex))
			{
				if (auto* parentRT = parentObj->GetComponent<RectTransformComponent>())
				{
					return parentRT->GetLayoutScale();
				}
			}
        }
    }

    // 캔버스 밑에 없는 UI는 물려받을 배율이 없다. 1을 쓰면 지금까지의 동작 그대로다.
    return 1.f;
}


Mathf::Rect RectTransformComponent::GetScreenRootRect()
{
    const float width = static_cast<float>(ScreenResizeBus::Get().GetWidth());
    const float height = static_cast<float>(ScreenResizeBus::Get().GetHeight());
    return { -width * 0.5f, -height * 0.5f, width, height };
}

Mathf::Rect RectTransformComponent::ResolveParentRect() const
{
    Mathf::Rect parentRect = GetScreenRootRect();

    if (m_pOwner)
    {
		const Entity::Index parentIndex = m_pOwner->GetParentIndex();
		if (Entity::IsValidIndex(parentIndex))
        {
			if (auto* parentObj = m_pOwner->OwnerSceneFindIndex(parentIndex))
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

Mathf::Vector2 RectTransformComponent::CalculateWorldPivotPosition(const Mathf::Rect& parentRect) const
{
    return {
        ToWorldPivot(m_anchoredPosition.x, parentRect.x, parentRect.width, m_anchorMin.x, m_anchorMax.x, m_pivot.x, m_layoutScale),
        ToWorldPivot(m_anchoredPosition.y, parentRect.y, parentRect.height, m_anchorMin.y, m_anchorMax.y, m_pivot.y, m_layoutScale)
    };
}

float RectTransformComponent::ToWorldPivot(float anchored, float parentOrigin, float parentSize,
    float anchorMin, float anchorMax, float pivot, float scale)
{
    const float anchorReference = anchorMin + (anchorMax - anchorMin) * pivot;
    return parentOrigin + parentSize * anchorReference + anchored * scale;
}

float RectTransformComponent::FromWorldPivot(float worldPivot, float parentOrigin, float parentSize,
    float anchorMin, float anchorMax, float pivot, float scale)
{
    const float anchorReference = anchorMin + (anchorMax - anchorMin) * pivot;
    const float safeScale = scale > 0.f ? scale : 1.f;
    return (worldPivot - (parentOrigin + parentSize * anchorReference)) / safeScale;
}

// 레이아웃 업데이트 함수: 가장 핵심적인 로직입니다.
bool RectTransformComponent::UpdateLayout(const Mathf::Rect& parentRect)
{
    // isDirty 플래그를 확인하여 변경이 있을 때만 계산을 수행합니다.
    if (!m_isDirty) return false;

    // Unity RectTransform과 같은 세 값으로 분해한다.
    //   size       = 부모의 anchor span + sizeDelta
    //   worldPivot = pivot으로 보간한 anchor reference + anchoredPosition
    //   rectMin    = worldPivot - size*pivot
    //
    // 예전 offsetMin/offsetMax 식과 대수적으로 같지만, anchoredPosition을 전역 좌표로
    // 오해하기 쉬웠다. pivot 위치와 크기를 먼저 구하면 정방향/역방향 변환이 같은
    // 정의를 공유하고, stretch anchor에서도 각 항의 의미가 명확하다.
    const float s = m_layoutScale;
    const Mathf::Vector2 size{
        parentRect.width * (m_anchorMax.x - m_anchorMin.x) + m_sizeDelta.x * s,
        parentRect.height * (m_anchorMax.y - m_anchorMin.y) + m_sizeDelta.y * s
    };
    const Mathf::Vector2 worldPivot = CalculateWorldPivotPosition(parentRect);

    Mathf::Rect computed{};
    computed.x = worldPivot.x - size.x * m_pivot.x;
    computed.y = worldPivot.y - size.y * m_pivot.y;
    computed.width = size.x;
    computed.height = size.y;

    const bool changed = !NearlyEqual(m_worldRect, computed);
    m_worldRect = computed;

    // 3. 계산이 완료되었으므로 isDirty 플래그를 false로 설정합니다.
    m_isDirty = false;

    return changed;
}

bool RectTransformComponent::NearlyEqual(const Mathf::Rect& a, const Mathf::Rect& b)
{
    constexpr float kEpsilon = 0.01f;
    return std::abs(a.x - b.x) < kEpsilon &&
           std::abs(a.y - b.y) < kEpsilon &&
           std::abs(a.width - b.width) < kEpsilon &&
           std::abs(a.height - b.height) < kEpsilon;
}

bool RectTransformComponent::DriveAsCanvasRoot(const Mathf::Rect& screenRootRect, float scale)
{
    // 배율이 바뀌면 rect가 같아도 자식들은 다시 계산해야 한다. SetLayoutScale이
    // 그때 m_isDirty를 세워 주므로 아래 조기 반환에 자연히 반영된다.
    SetLayoutScale(scale);

    // 화면 크기가 그대로면 아무것도 하지 않는다. 매 프레임 도는 경로라
    // 변화가 없을 때의 비용이 0이어야 한다.
    if (NearlyEqual(m_worldRect, screenRootRect) && !m_isDirty)
    {
        return false;
    }

    m_worldRect = screenRootRect;

    m_isDirty = false;
    return true;
}

bool RectTransformComponent::DriveAsWorldCanvasRoot()
{
    SetLayoutScale(1.f);

    Mathf::Rect localRect{};
    localRect.x = -m_sizeDelta.x * m_pivot.x;
    localRect.y = -m_sizeDelta.y * m_pivot.y;
    localRect.width = m_sizeDelta.x;
    localRect.height = m_sizeDelta.y;

    if (NearlyEqual(m_worldRect, localRect) && !m_isDirty) return false;

    m_worldRect = localRect;
    m_isDirty = false;
    return true;
}

// ── 앵커 프리셋 표 ──
//
// uGUI의 4x4 프리셋 격자를 그대로 옮긴 16종이다. 좌표는 y-down(파일 상단 규약).
//
// 이름이 곧 정의다: Stretch<방향>은 "그 변(또는 축)에 정렬한 채 나머지 축으로 늘린다".
// StretchLeft는 좌측에 붙여 세로로, StretchTop은 상단에 붙여 가로로 늘린다.
// 이전 구현은 두 스트레치 그룹이 각각 한 값으로 뭉개져 있어 6종이 3종처럼 동작했고
// (StretchLeft/Center/Right가 전부 같은 값), 그룹 주석의 가로·세로도 이름과 반대로
// 달려 있었다(분석 문서 F-6). 값을 이름에 맞춰 복원한다.
//
// 프리셋은 직렬화되지 않는다 — 저장되는 것은 m_anchorMin/Max뿐이라 기존 에셋에는
// 영향이 없다. 바뀌는 것은 인스펙터 버튼이 찍어 주는 값이다.
namespace
{
    constexpr AnchorPresetEntry kAnchorPresets[] = {
        // 점 앵커 3x3
        { AnchorPreset::TopLeft,       {0.0f, 0.0f}, {0.0f, 0.0f}, "TopLeft"       },
        { AnchorPreset::TopCenter,     {0.5f, 0.0f}, {0.5f, 0.0f}, "TopCenter"     },
        { AnchorPreset::TopRight,      {1.0f, 0.0f}, {1.0f, 0.0f}, "TopRight"      },
        { AnchorPreset::MiddleLeft,    {0.0f, 0.5f}, {0.0f, 0.5f}, "MiddleLeft"    },
        { AnchorPreset::MiddleCenter,  {0.5f, 0.5f}, {0.5f, 0.5f}, "MiddleCenter"  },
        { AnchorPreset::MiddleRight,   {1.0f, 0.5f}, {1.0f, 0.5f}, "MiddleRight"   },
        { AnchorPreset::BottomLeft,    {0.0f, 1.0f}, {0.0f, 1.0f}, "BottomLeft"    },
        { AnchorPreset::BottomCenter,  {0.5f, 1.0f}, {0.5f, 1.0f}, "BottomCenter"  },
        { AnchorPreset::BottomRight,   {1.0f, 1.0f}, {1.0f, 1.0f}, "BottomRight"   },

        // 세로로 늘리고 가로 위치만 다름
        { AnchorPreset::StretchLeft,   {0.0f, 0.0f}, {0.0f, 1.0f}, "StretchLeft"   },
        { AnchorPreset::StretchCenter, {0.5f, 0.0f}, {0.5f, 1.0f}, "StretchCenter" },
        { AnchorPreset::StretchRight,  {1.0f, 0.0f}, {1.0f, 1.0f}, "StretchRight"  },

        // 가로로 늘리고 세로 위치만 다름
        { AnchorPreset::StretchTop,    {0.0f, 0.0f}, {1.0f, 0.0f}, "StretchTop"    },
        { AnchorPreset::StretchMiddle, {0.0f, 0.5f}, {1.0f, 0.5f}, "StretchMiddle" },
        { AnchorPreset::StretchBottom, {0.0f, 1.0f}, {1.0f, 1.0f}, "StretchBottom" },

        // 양축 전체
        { AnchorPreset::StretchAll,    {0.0f, 0.0f}, {1.0f, 1.0f}, "StretchAll"    },
    };

    static_assert(std::size(kAnchorPresets) == static_cast<size_t>(AnchorPreset::StretchAll) + 1,
        "AnchorPreset 열거형과 프리셋 표의 개수가 어긋났다");

    // 표가 열거형 순서대로 놓였는지. 인덱스로 바로 찾는 빠른 경로가 이 전제에 기댄다.
    constexpr bool IsPresetTableOrdered()
    {
        for (size_t i = 0; i < std::size(kAnchorPresets); ++i)
        {
            if (static_cast<size_t>(kAnchorPresets[i].preset) != i) return false;
        }
        return true;
    }
    static_assert(IsPresetTableOrdered(), "프리셋 표가 열거형 순서와 다르다");

    // 값이 서로 겹치지 않는지. 이번에 고친 결손이 정확히 이것이었다 —
    // 스트레치 6종이 3종 값을 나눠 써서 버튼 절반이 같은 동작을 했고,
    // 개수는 16개 그대로라 아무 검사에도 걸리지 않았다(분석 문서 F-6).
    constexpr bool AreAnchorPresetsDistinct()
    {
        for (size_t i = 0; i < std::size(kAnchorPresets); ++i)
        {
            for (size_t j = i + 1; j < std::size(kAnchorPresets); ++j)
            {
                const auto& a = kAnchorPresets[i];
                const auto& b = kAnchorPresets[j];
                if (a.anchorMin.x == b.anchorMin.x && a.anchorMin.y == b.anchorMin.y &&
                    a.anchorMax.x == b.anchorMax.x && a.anchorMax.y == b.anchorMax.y)
                {
                    return false;
                }
            }
        }
        return true;
    }
    static_assert(AreAnchorPresetsDistinct(), "프리셋 값이 겹친다 — 서로 다른 버튼이 같은 앵커를 찍는다");
}

const AnchorPresetEntry* GetAnchorPresetTable() { return kAnchorPresets; }
size_t GetAnchorPresetCount() { return std::size(kAnchorPresets); }

const AnchorPresetEntry& GetAnchorPresetEntry(AnchorPreset preset)
{
    const size_t index = static_cast<size_t>(preset);
    if (index < std::size(kAnchorPresets) && kAnchorPresets[index].preset == preset)
        return kAnchorPresets[index];

    // 표가 열거형 순서와 어긋난 경우를 대비한 선형 탐색. static_assert가 개수만
    // 보장하므로 순서까지는 여기서 흡수한다.
    for (const auto& entry : kAnchorPresets)
    {
        if (entry.preset == preset) return entry;
    }
    return kAnchorPresets[static_cast<size_t>(AnchorPreset::MiddleCenter)];
}

// 앵커 프리셋을 설정하는 헬퍼 함수
void RectTransformComponent::SetAnchorPreset(AnchorPreset preset)
{
    const auto& entry = GetAnchorPresetEntry(preset);
    m_anchorMin = entry.anchorMin;
    m_anchorMax = entry.anchorMax;
    m_isDirty = true;
}

// 역산도 UpdateLayout과 같은 식을 쓴다 — 배율은 오프셋에만 곱해져 있으므로
// 되돌릴 때도 오프셋에서만 나눈다(PHASE 7-3).
static inline float SolveSize(float worldSize, float parentSize, float aMin, float aMax, float scale)
{
    // worldSize = parentSize*(aMax-aMin) + scale*size
    const float safeScale = scale > 0.f ? scale : 1.f;
    return (worldSize - parentSize * (aMax - aMin)) / safeScale;
}

static inline float SolveAnchored(float worldMin, float parentMin, float parentSize,
    float aMin, float size, float pivot, float scale)
{
    // worldMin = parentMin + parentSize*aMin + scale*(anchored - size*pivot)
    // anchored = (worldMin - parentMin - parentSize*aMin)/scale + size*pivot
    const float safeScale = scale > 0.f ? scale : 1.f;
    return (worldMin - parentMin - parentSize * aMin) / safeScale + size * pivot;
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
    m_sizeDelta.x = SolveSize(desired.width, newParentRect.width, m_anchorMin.x, m_anchorMax.x, m_layoutScale);
    m_sizeDelta.y = SolveSize(desired.height, newParentRect.height, m_anchorMin.y, m_anchorMax.y, m_layoutScale);

    m_anchoredPosition.x = SolveAnchored(desired.x, newParentRect.x, newParentRect.width,
        m_anchorMin.x, m_sizeDelta.x, m_pivot.x, m_layoutScale);
    m_anchoredPosition.y = SolveAnchored(desired.y, newParentRect.y, newParentRect.height,
        m_anchorMin.y, m_sizeDelta.y, m_pivot.y, m_layoutScale);

    m_isDirty = true;
    UpdateLayout(newParentRect); // 부모 Rect로 다시 worldRect 갱신
}

void RectTransformComponent::SetParentKeepWorldPosition(Entity* newParent)
{
    // 새 부모의 Rect를 얻고, 없으면 화면 rect를 부모로 가정
    Mathf::Rect newParentRect = GetScreenRootRect();

    if (newParent)
    {
        if (auto* pr = newParent->GetComponent<RectTransformComponent>())
        {
            newParentRect = pr->GetWorldRect();
            // 서로 다른 Canvas scale 사이로 재부모화할 때 옛 scale로 역산하면 다음
            // 레이아웃에서 새 scale이 적용되는 순간 위치와 크기가 튄다.
            SetLayoutScale(pr->GetLayoutScale());
        }
        else
        {
            SetLayoutScale(1.f);
        }
    }
    else
    {
        SetLayoutScale(1.f);
    }

    // 현재 앵커/피벗을 유지한 채로 역산 적용 (원하면 여기서 preset/pivot도 바꿀 수 있음)
    SetAnchorsPivotKeepWorld(m_anchorMin, m_anchorMax, m_pivot, newParentRect);
}
