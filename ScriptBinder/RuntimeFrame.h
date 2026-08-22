#pragma once
// 시뮬레이션 프레임의 단일 소유자 (E3-7).
//
// 왜 필요한가
// ──────────
// Editor와 Player가 각자 프레임 루프를 들고 있었는데, 재생 중 구간은 순서까지
// 글자 그대로 같았다. 관리(CLR) 틱을 감싸는 두 함수는 주석만 다르고 본문이
// 완전히 동일했다. 복제된 순서는 한쪽만 고치면 조용히 갈라지고, 그러면
// "에디터에서 되는데 빌드하면 안 된다"가 된다 — 이 저장소가 가장 비싸게 치르는
// 종류의 버그다. 그래서 순서를 한 곳이 소유한다.
//
// 무엇이 여기 없는가
// ────────────────
// Editor 훅(SceneManager::Editor — 선택·프리뷰 상태 머신)과 표시(presentation)는
// 각 Host의 몫이다. 편집 모드(재생 중이 아닌 상태)의 루프도 Editor에만 있다 —
// 그것은 런타임에 없는 상태다.
namespace Runtime
{
    /// 이번 프레임에 쓸 delta를 정한다. 일시정지면 0이다.
    ///
    /// ⚠ "delta 0은 일시정지에서만"이라는 규약을 지키는 유일한 자리다. 두 Host가
    ///   각자 `isPaused ? 0.0 : elapsed`를 적고 있었는데, 한쪽이 조건을 바꾸면
    ///   물리와 스크립트가 서로 다른 시간을 보게 된다.
    ///
    /// `double`로 돌려주는 것은 두 Host의 `m_frameDeltaTime`이 `double`이기 때문이다.
    /// `float`로 좁히는 지점을 옛 코드와 같은 자리(각 소비 지점)에 그대로 둔다 —
    /// 여기서 미리 좁히면 결과는 같지만 좁힘의 책임이 조용히 옮겨 온다.
    double ResolveFrameDelta();

    /// 재생 중 한 프레임. `Time->Tick` 콜백 안에서, 입력 갱신 뒤에 부른다.
    ///
    /// 순서: Initialization → InputEvents → (일시정지면 Pausing에서 끝)
    ///       → pre-physics 관리 틱 → Physics → GameLogic → post-physics 관리 틱
    ///
    /// 관리 틱 둘은 씬 구조 변경이 대기 중이면 건너뛴다. 재생을 누른 프레임은 아직
    /// 옛 씬이 활성이라, 여기서 돌리면 곧 접힐 스크립트가 Awake를 한 번 실행해
    /// 스폰·사운드 같은 부작용이 두 번 일어난다. 앞뒤 짝이 같은 조건이어야 한다.
    void TickSimulationFrame(float deltaSeconds);
}
