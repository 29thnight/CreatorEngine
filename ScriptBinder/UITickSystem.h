#pragma once
#include "ClassProperty.h"
#include <vector>

class SpriteSheetComponent;
class UIButton;
class TextComponent;
class Canvas;
class ImageComponent;

// PHASE(SceneGraphRedesignPlan) 트랙 C3 · 레인 2(UI계) — SpriteSheetComponent·
// TextComponent 가상 Update 오버라이드의 시스템 이관.
//
// ── 무엇을 대신하는가 ──
//
// AnimatorSystem(ScriptBinder/AnimatorSystem.h)과 완전히 같은 패턴이다. 예전에는
// 두 컴포넌트가 Component::Update를 오버라이드해 LifecycleRegistry의 오버라이드
// 감지 마스크(Bit_Update)에 걸리고, Scene::RegisterComponent가
// SystemSchedule::SubscribeImplicit(component, Phase::Update)로 Scene 하나뿐인
// m_schedule.UpdateList()에 스크립트·다른 컴포넌트와 섞어 넣었다. 여기서는 이
// 시스템 전용 조밀 벡터 두 개(타입별로 분리 — 멤버가 다르고 같은 벡터에 섞으면
// 다운캐스트가 필요해진다)를 두고 한 번에 순회한다. 두 컴포넌트를 한 시스템에
// 같이 담은 이유는 "같은 프레임에 계산된 RectTransform 월드 rect를 읽어 화면
// 배치(pos/scale/stretch)를 계산한다"는 같은 성격의 UI 틱이기 때문이다(과제
// 배정 — 레인 2가 두 컴포넌트를 함께 받는다).
//
// ── 등록/해지 훅 선택 근거 ──
//
// Awake/OnDestroy(Component.h 8훅 축, 컴포넌트당 1회 게이트)가 아니라
// OnAddedToScene/OnRemovingFromScene(6단계 축, 게이트 없음)에 건다.
// SpriteSheetComponent::Awake/OnDestroy, TextComponent::Awake/OnDestroy는
// RenderScene 커맨드 등록·UIManager 캔버스-연결 등록용으로 그대로 두고(과제
// 지시대로 손대지 않는다), 이 시스템 등록은 별도 훅 쌍을 쓴다 — 이유는
// AnimatorSystem.h와 동일: DDOL(DontDestroyOnLoad) 오브젝트가 씬을 건널 때
// Awake는 Component::State_AwakeCalled 비트로 컴포넌트 평생 1회만 불리므로
// (Scene::RegistryDrainAwakeAndStart) 재부착 시 다시 불리지 않는다 — 이 시스템
// 등록을 Awake에 걸면 최초 생성 씬의 등록부에서만 존재하고 새 씬으로 넘어간 뒤
// 영원히 틱을 못 받는 결함이 생긴다. OnAddedToScene/OnRemovingFromScene은
// 게이트가 없어 씬에 들고 날 때마다(최초 생성 때도, DDOL Detach/Attach 때도)
// 매번 불린다(Scene::DetachEntityHierarchy·AttachExistingEntity·
// AttachExistingEntityHierarchy가 각각 무조건 호출, Scene.cpp 확인).
// 실제 파괴 경로 중 프레임 끝 정규 경로(Scene::FlushPendingDestroy)도, 저장소
// 유일의 즉시 소멸 경로(PrefabUtility::ApplyComponentDiff)도 OnDestroy 직전에
// OnRemovingFromScene을 먼저 부르므로(PrefabUtility.cpp 확인), 이 시스템에서
// 빠지는 시점이 항상 실 파괴보다 먼저다 — 죽은 포인터를 틱할 창이 없다.
//
// ── 실행 시점(호출 위치)은 이 시스템의 소관 밖 ──
//
// SpriteSheetComponent::TickLayout·TextComponent::TickLayout은 둘 다
// RectTransformComponent::GetWorldRect()(TextComponent는 GetLayoutScale()도)를
// 읽는다 — 즉 UI 레이아웃(Scene::UpdateUILayout) 계산 이후에 돌아야 값이
// 맞다. Scene::Update 실측(Scene.cpp): AllUpdateWorldMatrix()가 매번 맨 먼저
// UpdateUILayout()을 부르고(부모→자식 의존 사슬이라 직렬 우선), Scene::Update는
// 그 AllUpdateWorldMatrix()를 RegistryTick(UpdateList) 앞뒤로 두 번 부른다
// (앞: 레이아웃 계산, 뒤: LateUpdate/렌더 준비 이전 재계산). 옛 구현에서 두
// 컴포넌트의 Update(tick)는 정확히 그 사이 — 즉 앞쪽 UpdateUILayout 이후,
// 뒤쪽 UpdateUILayout 이전 — 에서 RegistryTick(UpdateList)를 통해 돌았다.
// 이 시스템의 Update(tick) 호출도 같은 창(RegistryTick(UpdateList) 이후,
// 두 번째 AllUpdateWorldMatrix 이전)에 있어야 한다 — 정확한 삽입 위치는
// 통합 보고서 참고(Scene.cpp는 트랙 S2가 동시 편집 중이라 이 파일에서 직접
// 배선하지 않는다).
//
// ── 레인 UI 확장 — Canvas·ImageComponent ──
//
// 같은 패턴으로 Canvas·ImageComponent도 편입한다. Canvas::Update는
// CanvasOrder 변경 감지 하나뿐이고, ImageComponent::Update는
// RefreshTransformFromRect() 하나뿐이다(SpriteSheet::TickLayout과 동형).
// 등록/해지 훅 근거·실행 시점 제약은 위와 동일 — Scene.cpp의 별도 직접
// 호출부(Scene::InternalPauseUpdateForUI)가 ImageComponent::Update를 타입
// 포인터로 직접 부르는 문제는 이 파일이 아니라 통합 보고서에서 다룬다.
class UITickSystem : public Singleton<UITickSystem>
{
    friend class Singleton<UITickSystem>;
    UITickSystem() = default;
    ~UITickSystem() = default;

public:
    // 중복 등록은 조용히 무시한다(멱등) — AnimatorSystem과 같은 이유(SystemSchedule의
    // 암묵 경로와 달리 이 등록부는 프레임마다 실제 로직을 도는 조밀 벡터라, 중복이
    // 그대로 들어가면 같은 컴포넌트가 한 프레임에 두 번 틱하는 실질 오류가 된다).
    void RegisterSpriteSheet(SpriteSheetComponent* component);
    // 등록되어 있지 않으면 조용히 무시한다.
    void UnregisterSpriteSheet(SpriteSheetComponent* component);

    void RegisterText(TextComponent* component);
    void UnregisterText(TextComponent* component);

    // UIButton(C3 3차) — 같은 UI 틱 성격이라 같은 시스템에 담는다. 하는 일은
    // RectTransform 월드 rect로 클릭박스를 다시 잡는 것이라, SpriteSheet/Text와
    // 동일하게 "레이아웃 계산 이후" 창에 있어야 한다.
    void RegisterButton(UIButton* component);
    void UnregisterButton(UIButton* component);

    // Canvas(레인 UI) — CanvasOrder 변경 감지 하나뿐인 가벼운 틱이지만, 같은
    // 가상 Update 오버라이드 이관 대상이라 같은 시스템에 담는다. RectTransform
    // 월드 rect를 읽지 않으므로 "레이아웃 계산 이후" 제약과는 무관하다.
    void RegisterCanvas(Canvas* component);
    void UnregisterCanvas(Canvas* component);

    // ImageComponent(레인 UI) — SpriteSheet/Text와 동형이다(RectTransform 월드
    // rect를 읽어 pos/scale을 다시 잡는 것뿐). 같은 "레이아웃 계산 이후" 창에
    // 있어야 한다.
    void RegisterImage(ImageComponent* component);
    void UnregisterImage(ImageComponent* component);

    // 등록된 SpriteSheetComponent·TextComponent·UIButton·Canvas·ImageComponent
    // 전부를 한 번에 틱한다. 옛 개별 Update(tick)와 동일한 가드(owner 없음/파괴
    // 표시/비활성 스킵)를 이 시스템이 대신 적용한다 — 예전에는 Scene::RegistryTick이
    // 공통으로 해주던 가드다.
    //
    // 순회 순서: SpriteSheet → Text → Button(기존 셋, 그대로) → Canvas → Image.
    // 다섯 타입 모두 서로 다른 컴포넌트의 이번 프레임 틱 결과를 읽지 않는다
    // (각자 자기 소유자의 RectTransform만 보거나, Canvas처럼 아무 것도 안 본다)
    // — 따라서 다섯 사이의 순서는 정확성에 영향을 주지 않는다. 신규 둘의
    // 내부 순서는 "컨테이너(Canvas) 먼저, 리프(Image) 나중"으로 잡았다 —
    // Scene의 UI 레이아웃 계산(UpdateUILayout)이 부모→자식 순으로 도는 것과
    // 같은 직관을 유지하기 위해서일 뿐, 이 시스템 안에서 강제되는 의존성은 없다.
    void Update(float tick);

    size_t GetSpriteSheetCount() const noexcept { return m_spriteSheets.size(); }
    size_t GetTextCount() const noexcept { return m_texts.size(); }
    size_t GetButtonCount() const noexcept { return m_buttons.size(); }
    size_t GetCanvasCount() const noexcept { return m_canvases.size(); }
    size_t GetImageCount() const noexcept { return m_images.size(); }

private:
    std::vector<SpriteSheetComponent*> m_spriteSheets;
    std::vector<TextComponent*> m_texts;
    std::vector<UIButton*> m_buttons;
    std::vector<Canvas*> m_canvases;
    std::vector<ImageComponent*> m_images;
};

static auto UITickSystems = UITickSystem::GetInstance();
