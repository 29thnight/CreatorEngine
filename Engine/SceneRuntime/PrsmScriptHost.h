#pragma once
// PrSM 스크립트 호스트 — CoreCLR(ClrHost)를 대체해 가는 엔진 측 스크립트 런타임.
//
// ── 실행 모델 (PrSM v1 정책 D-001·D-047) ──
//
// 에디터: 프로젝트의 PrSM 소스를 prsmc 로 컴파일(emit-module-ir)하고, 그 IR 을
// prsm_jit.dll(LLVM ORC)로 같은 프로세스에서 **활성화 전에 전부** 구체화한다. 모듈이
// 참조하는 외부 심볼은 이 호스트가 명시한 것(ce_* 런타임, 태스크 브리지, 바인딩
// 검증기, reflgen 이 생성한 엔진 바인딩)뿐이고, 그 밖의 참조는 JIT 이 코드를 받기 전에
// 거부한다. Player 의 AOT 정적 링크는 같은 모듈 ABI 를 쓰는 별도 단계다.
//
// ── 소유와 스레드 ──
//
// 스크립트 컴포넌트 인스턴스는 PrSM GC(GCCE 도메인) 객체다. 엔진 컴포넌트
// (PrsmScriptComponent)는 인스턴스 id 만 들고, GC 루트·세대 핸들·태스크는 이 호스트가
// 쥔다. 모든 진입은 게임 스레드에서만 한다. 엔진 → 스크립트 호출마다 ScriptScope(GC 가
// 그 안에서는 돌지 않는다, strict FP 환경)와 바인딩 호출 문맥을 세운다.
//
// 틱은 ClrHost 와 같은 규약이다: 프레임당 물리 앞·뒤 두 지점, 스크립트 수와 무관하게
// 고정된 진입 횟수. 순회는 생성 순서이고 결정적이다.
//
// 이 헤더는 PrSM ABI 헤더를 끌어오지 않는다 — Editor 등 다른 모듈이 include 해도
// PRSM_HOST_BUILD 링크 규약에 묶이지 않게 하기 위해서다.
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

class Entity;

namespace prsm_engine
{
    std::uint64_t CurrentTaskOwnerHandle() noexcept;
}

// 엔진 생명주기 단계 → PrSM checked_invoke 연산. ScriptLifecyclePhase 와 달리
// 활성 축(Enable/Disable)도 담는다 — 이 호스트는 컴포넌트 하나가 단독으로 구동한다.
enum class PrsmLifecycle : std::uint8_t
{
    Initialized,
    AddedToScene,
    Enable,
    BeginSimulation,
    EndSimulation,
    Disable,
    RemovingFromScene,
    Uninitializing,
};

struct PrsmInstanceId
{
    std::uint32_t index{ 0 };
    std::uint32_t generation{ 0 };   // 0 = 무효

    bool IsValid() const { return 0 != generation; }
};

class PrsmScriptHost
{
public:
    static PrsmScriptHost& Get();

    // 게임 스레드에서 한 번. 도구·소스가 없으면 조용히 비활성으로 남는다(ClrHost 와 같다).
    bool Initialize();
    void Shutdown();

    // 프로젝트 소스를 다시 컴파일해 세대를 교체한다. 살아 있는 인스턴스가 있으면 거부한다
    // (v1: 상태 이전 리로드는 PrSM 리로드 코디네이터를 붙이는 다음 단계다).
    bool Rebuild();

    bool IsReady() const;
    const std::string& LastDiagnostics() const;
    std::vector<std::string> ComponentTypeNames() const;
    bool HasComponentType(std::string_view name) const;

    // 인스턴스를 만들고 기본값을 넣은 뒤(construct) 저장된 @serialize 값을 되돌린다.
    // 생명주기 훅은 부르지 않는다. 실패하면 무효 id.
    PrsmInstanceId Create(Entity& owner, std::string_view typeName, const std::vector<std::string>& fieldData);

    // 한 단계를 전달한다. 실패한 인스턴스는 격리되어 이후 훅·틱을 받지 않는다.
    // BeginSimulation 은 OnSimulate 코루틴을 시작하고, EndSimulation 은 그 소유자의
    // 태스크 정리를 먼저 끝낸 뒤(D-041) 훅을 부른다.
    bool Dispatch(PrsmInstanceId id, PrsmLifecycle phase);

    // 남은 태스크를 정리하고 루트·핸들을 놓는다. 훅은 부르지 않는다.
    void Destroy(PrsmInstanceId id);

    bool IsLive(PrsmInstanceId id) const;
    bool IsFailed(PrsmInstanceId id) const;

    // @serialize 필드의 현재 값. 형식은 "타입id|정확한 비트(16진)|이름".
    std::vector<std::string> CaptureFields(PrsmInstanceId id) const;

    void TickPrePhysics(float deltaSeconds);
    // 틱 → 코루틴 폴링 → GC 증분 단계 순서.
    void TickPostPhysics(float deltaSeconds);

    std::size_t LiveInstanceCount() const;

    PrsmScriptHost(const PrsmScriptHost&) = delete;
    PrsmScriptHost& operator=(const PrsmScriptHost&) = delete;

private:
    PrsmScriptHost();
    ~PrsmScriptHost();

    friend std::uint64_t prsm_engine::CurrentTaskOwnerHandle() noexcept;

    struct Impl;
    std::unique_ptr<Impl> m_impl;
};
