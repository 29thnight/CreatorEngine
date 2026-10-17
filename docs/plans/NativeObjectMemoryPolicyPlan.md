# 네이티브 객체 GC 및 에셋 소유권 정책 (PHASE 25)

> **상태: 미래 계획 · 현재 리팩토링 이후 · 구현 미착수.**
> 2026-10-07 결정: Scene·Entity·Component는 네이티브 GC 관리 객체로 전환하고,
> 스크립트 계층의 CoreCLR GC와 병행한다. 에셋 매니저의 자원 객체는 현행 스마트
> 포인터를 유지하거나 ownership_cpp의 검증된 소유권 계약을 단계적으로 적용한다.
> GC 대상과 책임 분리는 결정했으며, SGCL의 제품 채택과 ownership_cpp의 적용 API는
> 검증 후 확정한다. 이번 작업은 계획 등록이며 엔진 구현 변경이 아니다.

작성: 2026-10-07. 소스 기준: `3afe1daaee7b75f644ac10b12d96fb684a0e74c8`와 해당 시점 작업 트리.
PHASE 25는 미래 계획 식별자이며 PHASE 24 완료를 일괄 선행 조건으로 뜻하지 않는다.
착수 시점·공수는 현재 리팩토링 결과와 라이브러리 성숙도를 다시 조사해 산정한다.
현재 실행 공수·기성·진행률에는 포함하지 않는다.

## 1. 확정한 대상과 책임

| 대상 | 목표 정책 | 수명 정책의 소유자 |
|---|---|---|
| Scene·Entity·Component | 네이티브 GC 객체. 씬 소속과 도달 가능성은 별도 계약 | 네이티브 SceneRuntime |
| C# 스크립트 인스턴스·래퍼 | CoreCLR GC 유지. 네이티브 GC와 독립적으로 수집 | 관리 런타임과 스크립트 생명주기 서비스 |
| 에셋 매니저의 모델·재질·텍스처 등 자원 객체 | 현행 스마트 포인터 유지 또는 ownership_cpp 단계 적용 | 에셋 서비스와 명시적 소비자 소유권 |
| GPU 자원·COM·외부 SDK 자원 | 기존 명시적 해제 및 실제 사용 완료 계약 유지 | RHI·해당 SDK adapter |
| 대형 payload·Transform/Hierarchy 저장소·작업 임시 데이터 | 연속 저장소·풀·아레나 등 용도별 할당 유지 | 해당 저장소와 작업 스코프 |

Component가 GC 객체라는 이유로 참조하는 에셋이나 GPU 메모리까지 GC 힙으로 옮기지 않는다.
SceneManager와 DataSystem 같은 서비스 자체도 이번 GC 대상에 자동 포함하지 않는다.
GC 객체 안의 스마트 포인터 필드는 허용하되, 논리적 제거 시 해제할 자원 참조를 명확히 한다.

핵심 규칙은 **엔진이 생성·활성·제거 정책을 결정하고, 각 GC는 자기 힙을 회수한다**는 것이다.
C# 래퍼의 수거 시점이 씬 객체의 Destroy를 결정하게 만들지 않는다. 스크립트가 없는
네이티브 전용 실행도 같은 객체 수명 규칙을 사용해야 한다.

## 2. 현재 구현 기준선

- `Engine/SceneRuntime/Scene.h`: `m_Entities`는 `vector<unique_ptr<Entity>>`이며
  AddEntity·ReleaseSlot·AttachExistingEntity가 고유 소유권의 삽입·이동을 전제로 한다.
- `Engine/SceneRuntime/Entity.h`: `m_components`는 `vector<unique_ptr<Component>>`다.
  컴포넌트 등록·제거와 수명 훅은 이 고유 소유 구조를 전제로 다시 점검해야 한다.
- `Engine/SceneRuntime/SceneManager.h`: Scene raw pointer 목록과 active/pending 포인터가 있다.
  착수 시 생성·삭제 호출을 추적해 서비스가 보유할 Scene root를 명시한다.
- `Engine/SceneRuntime/DetachedEntityTransfer.h`: DDOL·씬 이동 중 Entity를 `unique_ptr`로
  보존한다. GC 전환 후에는 이동 스코프의 임시 강한 root로 같은 보존 의미를 구현한다.
- `Engine/SceneRuntime/ScriptObjectRegistry.h`: index/generation 핸들과 raw Entity 슬롯이 있다.
  식별·무효화 계약을 보존하면서 슬롯의 약한 관찰과 조회 중 생존 보장을 설계해야 한다.
- `Engine/RenderEngine/DataSystem.h`: `DataContainer<T>`는 `shared_ptr<T>` 기반이다.
  이 자원 캐시 계약은 GC 객체 이관과 분리하여 유지·검증한다.

위 내용은 현재 선언과 계약의 정적 조사다. 전체 소유자·콜백·작업 참조 인벤토리는
착수 게이트 M0에서 확정하며, 현재 타입 교체만으로 이관 가능하다고 판단하지 않는다.

## 3. 네이티브 객체 GC와 논리적 생명주기

### 3.1 생존 참조와 root

- 로드된/활성 Scene은 네이티브 서비스가 명시적 root로 보유한다.
- Scene→Entity, Entity→Component 및 객체 간 필드는 각각 강한 생존 참조인지,
  소속·식별만 위한 약한 핸들인지 분류한다. 역참조를 일괄 강한 참조로 만들지 않는다.
- Editor selection·Undo·이벤트 구독·비동기 작업·DDOL 이송은 관찰 핸들 또는 기간이
  명확한 root 스코프를 사용한다. 오래된 Scene을 보존하는 root 경로를 진단 가능하게 한다.
- SGCL 사용 시 `tracked_ptr`를 놓을 수 있는 위치를 준수한다. `std` 컨테이너나
  unmanaged 서비스 저장소는 `root_ptr` 또는 동등하게 등록된 저장소 계약이 필요하다.
  `unique_ptr`를 `tracked_ptr`로 일괄 치환하는 접근은 사용하지 않는다.

### 3.2 제거와 메모리 회수를 분리

목표 순서: `Alive → DestroyRequested → 작업 중단·수명 훅·등록 해제 → Detached/핸들 무효 → GC 회수`.
구체적인 훅 순서와 재진입 규칙은 현행 생명주기 계약을 재확인하여 확정한다.

Destroy는 GameThread의 제어된 경계에서 부모·자식·컴포넌트 규칙에 따라 처리한다.
tick, 물리, 렌더 프록시, 이벤트, 스크립트 등록을 정리하고 진행 중 작업의 접근을 안전하게
끝내며, 더 이상 필요한 에셋 소유권을 해제한다. 이미 제거된 객체는 GC 참조가 남아 있어도
게임플레이 API에서 사용할 수 없다. 마지막 생존 참조가 사라진 뒤 저장 공간을 수거한다.

GC 소멸자에서 gameplay 훅, CLR 호출, GameThread 전용 API 또는 GPU 즉시 해제를 실행하지
않는다. 네이티브 소멸자는 thread-safe 저장 공간 정리로 제한하고, thread-affine 정리는
논리적 제거 경계 또는 별도 안전한 퇴역 큐가 담당한다. Scene 언로드·종료에서 이 순서를
우회하는 경로와 GC 전에 서비스가 먼저 사라지는 경로를 금지한다.

## 4. C# GC 병행 및 계층 경계

기본 C# Entity/Component 래퍼는 세대 핸들로 네이티브 객체를 관찰하며 생존 root를 자동
소유하지 않는다. Resolve는 세대·논리적 상태를 검사하고 호출 동안 네이티브 객체의 생존을
보장한다. 현재의 핸들 ABI를 먼저 보존하고 확장이 필요할 때만 버전을 명시한다.

스크립트가 객체의 생존을 보장해야 하는 기능에는 해제 시점이 명시된 lease/root API를
별도로 둔다. lease는 메모리 생존을 유지하며 명시적 Destroy를 무효화하지 않는다.
Component→C# 인스턴스 등록은 네이티브 생명주기가 해제하고, 콜백·대기 작업·reload epoch의
임시 참조는 종료·취소·리로드 때 반환한다. finalizer는 누락된 lease의 보조 회수만 담당하며
네이티브 논리적 파괴의 정본으로 사용하지 않는다.

`native root → CLR strong handle → native root`의 교차 순환은 각 GC가 단독으로 회수할 수
없다. 기본 경계를 비소유 핸들로 두고, 예외 강한 참조는 등록·해제 주체 및 종료 경계를
문서화한다. reload와 종료 시 bridge root를 감사하고 양쪽 힙의 회수를 독립 확인한다.
C# GC의 컴팩션은 네이티브 힙에 적용되지 않으며, SGCL의 STW 부재가 CLR 정지를 없애지 않는다.

## 5. 에셋 스마트 포인터와 ownership_cpp 적용 정책

현행 스마트 포인터는 기본 경로로 유지한다. ownership_cpp는 계속 확장 설계 중인 후보이며,
특정 버전의 API나 보장 범위를 엔진 계약으로 먼저 고정하지 않는다. 착수 시 고정 커밋의
소유자·차용자·약한 관찰·이동·해제·allocator·동시성 계약을 확인한다.

적용 목표는 자원 API에서 **누가 보유하고, 누가 잠시 빌리고, 어떤 사건이 사용을 끝내는지**를
객체와 타입으로 드러내는 것이다. shared ownership이 필요한 자원까지 고유 소유로 바꾸거나,
타입 표기만으로 동시성·GPU 완료·외부 raw alias의 안전성이 보장됐다고 판단하지 않는다.

한 자원 유형과 소비 경로부터 동등한 의미로 비교한다. 캐시·비동기 로드·세대 교체·재질
의존·씬 언로드·Editor Undo의 수명과 public API를 검증한다. 기존 세대 보존과 GPU completion
퇴역은 해당 소비자 전환이 수용되기 전 제거하지 않는다. 도입이 성숙도·이식성·비용 기준에
못 미치면 스마트 포인터를 유지하고 필요한 소유권 facade만 엔진에 구현할 수 있다.

## 6. 단계와 수용 게이트

| 단계 | 작업 | 수용 조건 |
|---|---|---|
| M0 | 착수 시 코드·root·소유권·할당 조사 및 기준선 | Scene/Entity/Component 전체 참조와 자원 교차 경계, 수명 훅·작업·DDOL·직렬화 인벤토리. 동일 workload와 시간·메모리 예산 확정 |
| M1 | SGCL 독립 Windows 호환성 및 collector 통합 실험 | **VS 2026(v18), MSVC v145** Debug/Release 필수 통과. 실제 C++ 설정·다중 TU·DLL collector 단일성·TLS·job stack/fiber·종료 검증. clang-cl 결과는 별도 보조 증거 |
| M2 | 객체·핸들·논리적 수명 및 bridge 계약 구현 | root 누락/초과, 약한 슬롯 Resolve 생존, 교차 root 순환, Destroy/취소/reload/종료 오류 주입 회귀 통과 |
| M3 | 제한된 Scene→Entity→Component 수직 슬라이스 | 생성→등록→실행→Destroy→root 해제→회수까지 엔진 소비 경로 통과. 씬 이동·DDOL·prefab·save/load·Play/Stop 의미 보존 |
| M4 | 실제 객체 계층 단계 확대 및 혼합 GC 측정 | 대표 장면·Editor/Player에서 정확성, frame p95/p99 및 최악 지연, CPU·메모리·회수 지연 예산 충족. CLR GC와 동시 수집 포함 |
| A0 | 자원 소유권 계약 및 ownership_cpp 고정 버전 평가 | 소유/차용 실패 모드, ABI·allocator·동시성·종료와 현행 동등 의미 검증. 수용 여부와 적용 자원 유형 결정 |
| A1 | 선택 자원·소비자의 ownership_cpp 파일럿 | load/reload/unload·비동기·세대 pin·GPU 퇴역 및 Debug/Release 검증. 할당·atomic·retained bytes·frame 비용 기준 충족 |
| M5 | 제품 정책 확정 및 회귀·진단 통합 | 대상 Scene/Entity/Component 이관, 모든 승인된 소비자/구성 검증, root 진단·운영 예산·rollback 문서. 자원별 유지/이관 선택 명시 |

M0→M1→M2→M3→M4→M5를 작은 빌드 가능한 슬라이스로 진행한다. A0/A1은 M0 이후 독립
평가할 수 있으며 ownership_cpp 채택을 객체 GC 도입의 필수 조건으로 만들지 않는다.
SGCL 필수 게이트 실패 시 구현을 전면 확대하지 않고 원인 해결 또는 대체 GC를 평가한다.
**GC 객체 정책의 확정과 SGCL 구현체 채택의 확정을 구분한다.**

### 단편화·지연·회수 측정

SGCL은 비이동 GC이므로 동일 생존량의 dense/sparse survivor, 장수/단명 혼합, 실제 Component
타입 분포, 반복 씬 로드·언로드, DDOL, 스크립트 reload, 타입 전환과 메모리 압박을 비교한다.
requested live bytes·slot live bytes·페이지 점유율·managed commit·빈 chunk 재사용 reserve·
실제 반환 지연을 분리한다. commit/live 비율만을 순수 단편화 수치로 부르지 않는다.
OS private commit·working set·CLR heap·GPU 메모리는 출처와 중첩을 밝히고 단순 합산하지 않는다.

GC에 전체 STW가 없더라도 heap mutex·강제 수집 대기·thread exit·OS commit 및 collector의
CPU/대역폭 경쟁을 측정한다. 게임 프레임에서 동기 강제 수집을 기본 정책으로 삼지 않는다.
반복 후 정상상태 메모리가 예산 안에서 안정되고, root 누수와 미해제 자원 없이 회수돼야 한다.
현재 기준선과 대표 제품 workload가 확정되기 전 임의의 성능 개선 배수나 수용 수치를 약속하지 않는다.

## 7. 이행·후퇴와 기존 계획 연결

- 현행 unique ownership 경로를 이관 기준선으로 보존한다. GC/기존 backend는 빌드 또는
  실험 구성으로 분리하며 같은 객체를 두 방식으로 동시에 소유하지 않는다.
- 저장된 asset GUID·entity ID와 파일 형식은 allocator 주소나 GC root에 의존하지 않는다.
  pointer 필드 리플렉션·clone·역직렬화는 새 생존 참조를 재구성하도록 검증한다.
- [EngineLayerSeparationPlan.md](EngineLayerSeparationPlan.md)의 native Core→Host/Editor
  의존 방향을 보존한다. CLR이 native 객체 수명 서비스의 주인이 되지 않는다.
- [MemoryProfilerPlan.md](MemoryProfilerPlan.md)의 MP2 소유자 계측, MP3 관리 객체 그래프,
  MP4 GPU 객체와 출처를 공유하되 해당 단계의 완료를 이 계획 완료로 대신하지 않는다.
- [SimulationEffectContractPlan.md](SimulationEffectContractPlan.md)의 취소·대기·효과 스코프와
  같은 Destroy/lease 계약을 사용한다. PHASE 24 저작 언어 도입을 일괄 기다리지 않는다.
- 기존 자원 소유권·씬 그래프·생명주기 완료 기록을 소급해 GC 이관 완료로 표시하지 않는다.

## 8. 기존 실험 증거와 한계

2026-10-07 독립 `SGCLWindowsProbe` 결과: SGCL 커밋
`154efa921f5c8b9a2a16b0a8662502683c561b99`는 v18 MSVC 19.51 C++20 Debug/Release 및
C++23 Debug에서 `MayContainTracked<Array<65536>>` 관련 빌드 오류가 있었다. v18 bundled
clang-cl 22.1.3은 `<array>` 선행 include 조건으로 Debug/Release 기능 테스트를 통과했다.
순환 회수·weak 만료·thread exit·다중 mutator와 collector의 소멸 수 80,006 일치를 확인했다.
이는 MSVC 제품 수용, 전체 SGCL 모듈 수용, 단편화 성능 또는 실제 CoreCLR 병행 증거가 아니다.

재현 프로젝트: `C:/Users/lance/source/SGCLWindowsProbe/README.md`와 구성별 로그.
이 경로는 현재 장비의 실험 자료다. 착수 시 고정 커밋·재현 스크립트·결과 manifest를
저장소의 회귀 도구/증거 경계에 편입해 다른 장비에서도 재현해야 한다.

참고: [SGCL](https://github.com/pebal/sgcl),
[GC 구현 설명](https://github.com/pebal/sgcl/blob/154efa921f5c8b9a2a16b0a8662502683c561b99/docs/garbage_collector/how-it-works.md),
[ownership_cpp](https://github.com/29thnight/ownership_cpp).
ownership_cpp는 진행 중 설계이며 사용 시점에 문서·소스·테스트 보장을 다시 확인한다.
