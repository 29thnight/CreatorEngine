#pragma once
#include "ClassProperty.h"
#include <vector>

class PlayerInputComponent;

// 트랙 C3 — PlayerInputComponent 가상 Update 오버라이드의 시스템 이관.
//
// AnimatorSystem/UITickSystem과 같은 패턴이다(조밀 벡터 + 6단계 훅 등록·해지 +
// 옛 RegistryTick 가드 3종 복제). UI 계열과 한 시스템에 담지 않은 이유는 성격이
// 다르기 때문이다 — UITickSystem은 "레이아웃 계산 이후에 화면 배치를 잡는" 틱이고
// 이쪽은 입력 액션맵을 CLR로 넘기는 틱이다. 실행 창도 서로 묶일 이유가 없다.
//
// ★ 이 컴포넌트에는 다른 것들에 없는 소비자가 하나 더 있다 —
//   Scene::InternalPauseUpdateForUI(일시정지 전용 우회로)가 PlayerInputComponent를
//   타입 포인터로 직접 틱한다. 게임이 멈추면 GameLogic()이 통째로 스킵되는데
//   입력은 살아 있어야 하기 때문이다. 그래서 틱 본문의 이름(TickInput)이 이
//   시스템과 그 우회로가 **공유하는 계약**이다 — 이름을 바꾸면 두 곳을 함께
//   고쳐야 한다. (가상 Update로 두면 그 우회로 호출이 기반의 빈 함수에 조용히
//   붙어 일시정지 중 입력이 죽는다 — C3 2차에서 실제로 겪은 실패 양식이다.)
class PlayerInputSystem : public Singleton<PlayerInputSystem>
{
    friend class Singleton<PlayerInputSystem>;
    PlayerInputSystem() = default;
    ~PlayerInputSystem() = default;

public:
    // 중복 등록은 조용히 무시한다(멱등) — 중복이 들어가면 한 프레임에 두 번
    // 틱하는 실질 오류가 된다.
    void Register(PlayerInputComponent* component);
    void Unregister(PlayerInputComponent* component);

    void Update(float tick);

    size_t GetCount() const noexcept { return m_inputs.size(); }

private:
    std::vector<PlayerInputComponent*> m_inputs;
};

static auto PlayerInputSystems = PlayerInputSystem::GetInstance();
