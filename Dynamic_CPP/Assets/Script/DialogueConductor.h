#pragma once
#include "Core.Minimal.h"
#include "ModuleBehavior.h"
#include "DialogueConductor.generated.h"

class PlayerDialogueUI;
class LetterboxController;
// 두 명(1P, 2P) 말풍선을 중앙에서 번갈아 제어하는 컨덕터
class DialogueConductor : public ModuleBehavior
{
public:
   ReflectDialogueConductor
    [[ScriptReflectionField]]
    MODULE_BEHAVIOR_BODY(DialogueConductor)
    virtual void Awake() override {}
    virtual void Start() override {}
    virtual void FixedUpdate(float) override {}
    virtual void Update(float tick) override;
    virtual void LateUpdate(float) override {}
    virtual void OnDisable() override {}
    virtual void OnDestroy() override {}

    enum class Speaker : int { P1 = 0, P2 = 1 };
    void MaybeStartOrTickAutoExit(float tick);

    // 필수 바인딩
    void BindBubbles(PlayerDialogueUI* p1Bubble, PlayerDialogueUI* p2Bubble);
    void SetParticipants(std::shared_ptr<GameObject> p1, std::shared_ptr<GameObject> p2);

    // ===== 시퀀스 구성 방식 1: 번갈아 자동 생성 =====
    // [start, endExclusive) 범위를 firstSpeaker부터 번갈아 붙인다.
    void SetAlternatingRange(int startInclusive, int endExclusive, Speaker firstSpeaker);

    // ===== 시퀀스 구성 방식 2: 하드코딩/임의 지정 =====
    void ClearSequence();
    void AddStep(Speaker who, int textureId); // textureId == 다이얼로그 인덱스

    // ===== 재생 옵션 =====
    [[Property]] 
    float autoPlayDelay{ 1.5f };
    [[Property]] 
    bool  waitAtLast{ true };      // 마지막에서 입력 대기
    [[Property]] 
    bool  hideWhenReset{ true };   // 시퀀스 재설정 시 버블 잠깐 숨김

    [[Property]] 
    bool  autoExitAfterFinish{ true }; // 시퀀스 끝나면 자동으로 나가기
    [[Property]] 
    float autoExitDelay{ 1.0f };       // 나가기까지 대기 시간(초)

    void BindLetterbox(LetterboxController* c) { m_letterbox = c; }

    // 재생 제어
    void ResetPlayback();   // 커서/타이머 리셋 + (옵션) 숨김

    LetterboxController* m_letterbox = nullptr;
    bool  m_exiting = false;
    float m_exitTimer = 0.f;

private:
    bool IsAnyAJustPressed();
    void StepShow(); // 현재 커서의 (누가/무엇을) 표출 후 커서++

private:
    struct Step { Speaker who; int textureId; };

    PlayerDialogueUI* m_p1Bubble = nullptr;
    PlayerDialogueUI* m_p2Bubble = nullptr;

    std::weak_ptr<GameObject> m_p1;
    std::weak_ptr<GameObject> m_p2;

    std::vector<Step> m_sequence;
    int   m_cursor = 0;
    float m_autoTimer = 0.f;

    bool m_prevA0 = false;
    bool m_prevA1 = false;

};
