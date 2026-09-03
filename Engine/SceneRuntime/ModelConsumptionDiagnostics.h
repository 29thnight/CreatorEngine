#pragma once
// PHASE 3.75 MBC10 — 모델 소비 경로의 **읽기 전용** 관측.
//
// MBC7~MBC9까지 제품 경로(MeshRenderer 해석·씬 인스턴스화·Animator 틱)는 stdout에
// `[mesh.resolve]`·`[model.instantiate]`·`[anim.tick]` 토큰을 무조건 찍었고 게이트가
// 그 줄을 세었다. 계획서 §5.2는 정상 제품 경로의 무조건 출력을 제거 대상으로 적었다 —
// 제품은 계수만 올리고(원자, 잠금 없음), 진단 명령(`assets.modeldiag`)이 스냅샷을
// 읽어 낸다. 스냅샷은 관측일 뿐 아무 상태도 바꾸지 않는다.
//
// 계수는 프로세스 수명 동안 단조 증가한다(리셋 없음 — 리셋도 상태 변경이다). 게이트는
// 절대값이 아니라 시나리오 안의 기대 하한/상한으로 판정한다.

#include <cstdint>
#include <string>
#include <string_view>

struct ModelConsumptionSnapshot final
{
    std::uint64_t meshResolveGeneration{ 0 };   // MeshRenderer가 generation 메시에 결합한 횟수
    std::uint64_t meshResolveFailed{ 0 };       // UUIDv8 모델인데 generation/메시 해석 실패
    std::uint64_t instantiateGeneration{ 0 };   // ModelSceneInstantiation 성공(루트 엔티티 생성)
    std::uint64_t instantiateRejected{ 0 };     // 게시 계약 위반으로 아무것도 만들지 않음
    std::uint64_t tickGeneration{ 0 };          // 애니메이터별 1회 — typed generation 틱 진입
    std::uint64_t tickNone{ 0 };                // 애니메이터별 1회 — 스켈레톤 없어 틱 불가
    std::string lastInstantiated{};             // 마지막으로 인스턴스화한 모델 이름
};

namespace ModelConsumptionDiagnostics
{
    void NoteMeshResolved() noexcept;
    void NoteMeshResolveFailed() noexcept;
    void NoteInstantiated(std::string_view modelName);
    void NoteInstantiateRejected() noexcept;
    void NoteTickPath(bool generation) noexcept;

    [[nodiscard]] ModelConsumptionSnapshot Snapshot();
}
