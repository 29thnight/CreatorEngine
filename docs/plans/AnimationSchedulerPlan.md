# 애니메이션 스케줄러 · LOD · CPU 버짓 재설계 (PHASE 13)

2026-08-11 수립 · **2026-08-19 개정**(§1.5 대조 실측 추가, §2 설계 재구성, §3 단계 재배치).

애니메이션 런타임을 전수 추적한 결과, 본 행렬 계산이 **문자열
키 트리 탐색 위에서, 공유 애셋에 쓰기 경합을 일으키며, 화면 밖 캐릭터까지
전부 풀틱**으로 돌고 있다.

초판은 언리얼 **Animation Budget Allocator** 구도의 이식만을 목표로 했다.
8-19 개정에서 언리얼·유니티·Lumina 세 엔진과 대조한 결과(§1.5), 버짓만으로는
부족하다는 것이 드러났다 — **평가 엔진 자체가 한 세대 뒤**이고, 버짓은 그
위에 얹히는 상위 장치다. 그래서 이 페이즈는 두 겹이 된다:

```
[아래층 — 평가 엔진 현대화]
  Pose(SoA TRS 값) → 태스크 레시피 기록 → 도달성 실행(워커별 버퍼 풀)
                                            ↓
[위층 — 배분]                      레시피 강등 LOD(L0~L7)
  Significance(거리·화면·가시성) → 태스크 EMA 기반 CPU 버짓 배분
                                            ↓
  관측 본만 Transform 투영 → 프레임 팔레트 아레나 → ProxyCommand
```

원칙 둘을 먼저 박는다.

1. **시간은 항상 전진하고, 건너뛰는 것은 포즈 계산뿐이다.**
   `curAnimationProgress` 기반 게임플레이 판정(`endAnimation` 등)과 키프레임
   이벤트는 틱 레이트와 무관하게 보존된다 — 언리얼과 같은 결정이다.
2. **빠른 경로가 기본값이다.** 유니티는 같은 문제를 옵트인 탈출구(Optimize
   Game Objects · `StringToHash` · culling mode)로 풀었고, 그래서 "알면 빠르고
   모르면 느린" 시스템이 됐다(§1.5). 이 설계의 최적화는 전부 기본 동작이며
   저작자가 알아야 켜지는 스위치를 두지 않는다.

---

## 1. 지금 무엇이 있는가 — 실측 (2026-08-11)

### 1.1 호출 사슬

```
SceneManager::GameLogic (SceneManager.cpp:205-224)
 ├ Scene::Update → RegistryTick → Animator::Update        · FSM 전이만, 단일 스레드
 ├ InternalAnimationUpdateEvent.Broadcast
 │   └ AnimationJob::Update (ScriptBinder\AnimationJob.cpp:55-313)
 │       애니메이터 1개 = 태스크 1개 → 전용 ThreadPool(8) → NotifyAllAndWait (fork-join)
 └ Scene::LateUpdate → UpdateRenderData → ProxyCommand
       Animator::m_FinalTransforms → m_palleteMap memcpy 32KB (ProxyCommand.cpp:106)
```

3-2G 이후 렌더 입력은 게임 스레드가 애니메이션·RenderScene delta 갱신을 끝낸 뒤
immutable frame packet으로 발행하고, 전용 RenderThread가 bounded latest-wins queue에서
소비한다. PresentationThread는 완료 display snapshot만 표시한다. 따라서 애니메이션
fork-join은 packet 밀봉 전에 끝난다는 순서만 유지하며 렌더 스레드와 3자 배리어로
락스텝하지 않는다. 이 골격(전용 단계 + fork-join)은 유지할 가치가 있고, 문제는 그
안에서 도는 내용물이다.

### 1.2 정확성 결함 — 지혈 대상 (R1~R6)

| # | 결함 | 근거 |
|---|---|---|
| R1 | **공유 애셋 동시 쓰기 레이스.** `Skeleton`·`Animation`은 `DataSystem::Models` 캐시로 같은 모델의 모든 인스턴스가 공유하는데(`ComponentFactory.cpp:262`), 워커 8스레드가 `Animation::curKey`(`AnimationJob.cpp:368,419` → `Animation.h:48`)와 `Bone::m_global/localTransform`(`AnimationJob.cpp:377-378,422-423`)에 동시 기록. 같은 모델 2체면 레이스. `curKey`는 읽는 곳도 없다(죽은 캐시) | AnimationJob.cpp · Animation.h:48 |
| R2 | **`operator[]`가 공유 애셋을 오염 + OOB.** `UpdateBlendBone`이 다음 클립에는 `find` 없이 `nextanimation->m_nodeAnimations[boneName]`(`:366`) — 채널이 없으면 빈 `NodeAnimation`이 **애셋 map에 삽입**되고(워커에서 map 구조 변경) `calculAni`가 빈 `m_positionKeys[0]`을 읽는다(`:651`) | AnimationJob.cpp:366,651 |
| R3 | ~~**죽은 참조 하나가 나머지 전부를 건너뛴다.** 애니메이터 순회 중 `if (animator == nullptr) return;` — `continue`가 아니라 `return`~~ **→ 8-19 확인: 해소됨.** K2 리팩터(`shared_ptr` 관찰 → 프레임-로컬 raw 포인터)가 `continue`로 고쳤고 "적대 리뷰 발견 1"로 근거를 남겼다 | AnimationJob.cpp:100-116 |
| R4 | **`CurrentKeyIndex`가 -1을 반환하면 `keys[-1]`.** 비루프 클립이 duration에 클램프될 때 time이 마지막 키 시간을 넘으면 -1이 그대로 인덱스로 쓰인다 | AnimationJob.cpp:20-32,654-660 |
| R5 | **연산자 우선순위 버그.** `if (!StateVec.size() >= 2)` = `(!size) >= 2` = 항상 거짓 — 기본 상태 확정 분기가 죽어 있다 | AnimationController.cpp:60 |
| R6 | **워커 스레드에서 C# 키프레임 이벤트 발화.** `InvokeEvent`(`:156,280`)가 스레드풀 람다 안에서 매니지드 콜백 진입 — `ClrHost.h:134` 주석이 자인하는 위험 | AnimationJob.cpp:156,280 |

**8-19 재확인 — S0는 미착수다.** R3만 다른 작업의 부수 효과로 해소됐고
**R1·R2·R4·R5·R6은 전부 살아 있다**(`AnimationJob.cpp:396,398,400,449` ·
`AnimationController.cpp:60`). 특히 **R2는 워커 스레드에서 공유 애셋 `std::map`에
노드를 삽입하면서 빈 벡터를 인덱싱**하는 미정의 동작이고, 캐릭터가 여러 마리
나오는 씬에서 재현되는 종류다 — 이 페이즈의 다른 어떤 항목보다 우선한다.

### 1.3 구조적 비효율 (E1~E10)

비용 구조: `O(애니메이터 × 본 × (std::map 문자열 탐색 + 키프레임 선형 스캔))`.
본 수와 무관해야 할 상수 인자가 전부 최악의 선택으로 박혀 있다.

| # | 항목 | 근거 |
|---|---|---|
| E1 | 본 채널 조회가 `std::map<std::string, NodeAnimation>` — 본마다 `find`+`operator[]` 2회, 매 프레임 | Animation.h:40 · AnimationJob.cpp:355,365-366,409,418 |
| E2 | 키프레임 탐색이 매 프레임 처음부터 선형 스캔. `curKey` 캐시는 공유 객체에 저장(무용 + R1의 원인) | AnimationJob.cpp:20-32 |
| E3 | 블렌딩이 본마다 `XMMatrixDecompose` ×2 — 채널 단계(SRT)에서 섞으면 공짜인 것을 행렬 분해로 되사고 있다 | AnimationJob.cpp:628-645 |
| E4 | 레이어 경로(`UpdateBoneLayer`)가 본×컨트롤러로 같은 `find`를 중복 — "이 본을 누가 움직이나"는 클립 조합이 바뀔 때만 변하는 불변 정보 | AnimationJob.cpp:469-479 |
| E5 | 매 프레임 힙 할당: 애니메이터 목록 재구축(`:61-67`) · 애니메이터당 `weak_ptr` 벡터 복사(`:73-77`, 캡처된 `shared_ptr`이 이미 생존 보장 — 순수 낭비) · `std::function` 할당(`:84`) | AnimationJob.cpp |
| E6 | 512본 고정: `Animator` 인라인 배열 2벌 64KB(`Animator.h:80-81`) + 컨트롤러당 2벌 64KB(`AnimationController.h:36-37`) + 팔레트 memcpy 상시 32KB(`ProxyCommand.cpp:30,106`). 본 30개짜리도 동일 | — |
| E7 | 본 트리를 포인터 추적 재귀로 순회 — `Skeleton::m_bones` 평탄 배열이 이미 있는데 쓰지 않는다 | AnimationJob.cpp:336-457 |
| E8 | **LOD·가시성·거리 개념 0.** 화면 밖·원거리도 풀틱. `Mesh::SelectLOD`(Mesh.cpp:139-221)와 `Camera::CalculateLODDistance`(Camera.cpp:174-177)는 **구현만 있고 호출자 0** | grep 전수 확인 |
| E9 | 전용 8스레드 상비 풀(`:36`) — 전역 `WorkerPools`와 별개로 코어를 점유, 태스크 입도는 애니메이터 1개(불균형) | AnimationJob.cpp:36,312 |
| E10 | 배속 파라미터 조회가 매 프레임 뮤텍스 + 문자열 선형 탐색 — 기본값 `"None"`이라 매칭 불가인 경우조차 매번 | AnimationState.cpp:72-79 · Animator.h(FindParameter) |

그 외: `m_currAnimator`가 클래스 멤버가 아니라 **파일 전역 변수**(`AnimationJob.cpp:13`),
`RenderEngine\AnimationJob.h`(헤더) ↔ `ScriptBinder\AnimationJob.cpp`(구현)의 계층
기형, 소켓 갱신 코드 3중 복제(`:290-306,428-445,602-619`).

### 1.4 이미 있는데 안 쓰는 것 — 이 설계가 딛는 기존 자산

| 자산 | 위치 | 쓰임 |
|---|---|---|
| 전역 워커 풀 `WorkerPools` | Utility_Framework\WorkerPool.h:16-68 | 전용 풀 은퇴 후보지 (S6에서 실측 비교) |
| CPU 프로파일러 `gCPUProfiler` + `PROFILE_CPU_SCOPE` | ImGuiHelper\Profiler.h | 버짓 실측·HUD의 기반 |
| `TimeSystem` (QPC) | Utility_Framework\TimeSystem.h | 인스턴스별 비용 측정 |
| `Camera::CalculateLODDistance` · `GetFrustum` | Camera.cpp:174,121 | Significance 입력 (첫 배선) |
| 게임 스레드 프러스텀 컬링 선례 | FoliageComponent.cpp:244-316 | 가시성 판정 패턴 재사용 |
| `RenderScene::AnimatorMap` 등록 패턴 | RenderScene.h:153,184 | 인스턴스 등록의 골격 |
| **`Skeleton::m_serial`** (인스턴스 유일 일련번호) | RenderEngine\Skeleton.h | **베이크 캐시의 무효화 키**(②) — Lumina의 `BindPoseGeneration`과 같은 역할을 이미 갖고 있다 |
| **`BoneComponent`** (`m_boneIndex`·`m_resolvedSerial`) | ScriptBinder\BoneComponent.h | **관측 본 투영의 주소 지정**(④) — SceneGraph 트랙 E7-b의 결과를 그대로 승계 |
| **`ProxyCommand` 불변 스냅샷 경계** | ScriptBinder\ProxyCommand.cpp | **팔레트 아레나의 종점**(⑤) — UE식 리테인드 프록시라 경계가 이미 있다 |
| 논블로킹 폴링 선례 | DX12PSOManager.cpp:548-568 | (참고) 비동기 결과 소비 패턴 |

### 1.5 다른 엔진 대조 — 세대 판정 (2026-08-19)

언리얼·유니티·Lumina(`C:\Users\lance\Downloads\LuminaEngine-main`, 소스 직독)와
평가 모델을 대조했다. **평가 엔진의 세대**로 줄을 세우면:

| 세대 | 특징 | 해당 |
|---|---|---|
| 1세대 | 계층을 재귀로 걸으며 행렬을 즉시 곱한다. 전부 계산, 스킵 없음 | **CreatorEngine** |
| 2세대 | TRS 포즈 + 그래프 평가, 병렬화, LOD/스킵 장치 | Unity, Unreal(AnimGraph) |
| 3세대 | 그래프를 컴파일해 **레시피만 기록**하고 필요한 체인만 나중에 실행 | Lumina, Unreal AnimNext(실험적) |

**축별 대조 (요지):**

| 축 | Unity | Unreal | Lumina | **CreatorEngine (현재)** |
|---|---|---|---|---|
| 포즈 표현 | TRS 스트림 → Transform | `FTransform`(TRS) | `FPose` SoA TRS | **`XMMATRIX` 시종일관** |
| 뼈의 정체 | GameObject Transform | 평탄 배열 인덱스 | 평탄 배열 인덱스 | **GameObject + `BoneComponent`** |
| 클립 조회 | 커브 + 사전 바인딩 | 트랙 배열 + ACL 압축 | 채널 + (스켈레톤,generation) 해석 캐시 | **`map<string>` 본마다 조회** |
| 키 검색 | 커서 | 압축 포맷 조회 | 타임스탬프 배열 | **선형 스캔, 커서 없음** |
| 블렌드 | TRS lerp/slerp | TRS lerp/slerp | TRS 커널 | **행렬 decompose ×2 → 재조립** |
| 평가 스킵 | 컬링 모드 | URO + LOD | URO + 스켈레톤 LOD + 도달성 | **없음** |
| 스키닝 업로드 | — | 3×4 팩, 실본수 | 3×4 팩, 실본수, lazy 재팩 | **512 고정 32KB memcpy, 매 프레임 힙 할당** |

**기능 공백 (우리에게 0인 것):** 블렌드 스페이스 · 애디티브 · 인러셜라이제이션
· 싱크 그룹 · 루트 모션 · IK · 리타게팅 · 애님 커브 · 클립 압축 · 스켈레톤 LOD.

**Lumina에서 확인한 핵심 구조 (3세대의 실체):**

- `FPose`가 SoA TRS 값이고, 행렬은 `ToSkinningMatrices`에서 **마지막에 한 번**만.
- 애님그래프를 바이트코드로 컴파일(`EAnimOp` 20종, `kAnimBytecodeVersion`).
  런타임에 "그래프" 자료구조가 없다 — `TVector<uint8>` 선형 스캔.
- **포즈 레지스터가 포즈를 담지 않는다.** `int16` 태스크 인덱스를 담는 SSA
  이름표라 VM 실행 중 포즈 메모리가 0.
- 프레임이 2단계: Update(로직만, 포즈 수학 0회) → Execute(출력에서 **도달 가능한
  체인만** 실행, 워커별 포즈 풀 + steal-in-place).
- **태스크 리스트가 프론트엔드 독립 계약**이다 — 단일 클립 경로와 그래프 VM
  경로가 같은 `FAnimTaskList`를 채우고 하나의 Execute 패스가 소비한다.
- 애셋(불변 공유)과 인스턴스(레지스터 파일)가 완전 분리 — 우리 R1이 **구조적으로
  발생 불가**한 형태.

**유니티 대조에서 얻은 교훈 (§0 원칙 2의 근거):** 유니티는 바이트코드 없이도
핸들 기반 평탄 그래프(`PlayableHandle` = {index, version})로 위상 정렬·캐시
지역성·애셋/인스턴스 분리를 확보했다. 유니티가 실제로 치른 대가는 *바이트코드
부재*가 아니라 **뼈 = Transform 결정**이고, 탈출구 다섯 중 넷(Optimize Game
Objects · culling mode · Playables · Animation C# Jobs)이 전부 Transform 계층
우회다. 그리고 그 탈출구들은 **기본 꺼짐 · 수동 · 알아야 씀**이다.

우리는 뼈 = GameObject 노선에 이미 서 있으므로(SceneGraph 페이즈, `BoneComponent`
작성 완료) 같은 청구서를 받는다 — 저작 자산 기준 **Bone 노드 744개**(Test1.creator
61 · 플레이어 프리팹마다 ~54)에 `Scene::UpdateModelRecursive` 순회가 **프레임당
3회**. §2.2 ④(관측 본 물질화)가 이 청구서를 기본 동작으로 지운다.

---

## 2. 설계 — 포즈 정본 · 관측 본 · 레시피 강등

### 2.0 다섯 개의 결정 (2026-08-19 개정)

우리 고유 조건은 **유니티 노선의 저작 계층(뼈 = GameObject) + 언리얼 노선의
렌더 계층(리테인드 프록시)** 이라는 하이브리드다. 지금은 이 하이브리드 때문에
**양쪽 청구서를 동시에** 내고 있다 — 전 뼈 Transform 갱신(유니티 비용)과
512행렬 프록시 스냅샷(언리얼 비용). 다섯 결정은 그 중복을 없애면서 3세대에
서는 것을 목표로 한다.

| # | 결정 | Unity | UE / Lumina | **우리** |
|---|---|---|---|---|
| ① | 포즈의 정본 | Transform이 정본 | 포즈 버퍼가 정본, 뼈는 씬에 없음 | **포즈가 정본, 뼈 GameObject는 읽기 전용 창구** |
| ② | LOD 강등 축 | 컬링 on/off | 틱 레이트 + 본 프리픽스 | **레시피(태스크) 단위 강등 L0~L7** |
| ③ | 그래프 표현 | 핸들 그래프 | 바이트코드 VM | 프론트엔드 중립 레시피 (VM은 S8 후행) |
| ④ | GPU 전달 | Transform → 스키닝 | 컴포넌트 배열 → 게더 | **프레임 아레나 → 프록시 (컴포넌트에 배열 없음)** |
| ⑤ | 인스턴스 데이터 소재 | Animator 컴포넌트 | ECS 컴포넌트 | **시스템 소유 조밀 저장소, 컴포넌트는 핸들만** |

**Lumina와 같은 선상이되 다른 지점** — 같은 세대(포즈=값 · 레시피 기록 · 도달성
실행 · 병렬 2패스)에 서면서, 뼈가 씬에 있다는 우리 조건을 부채가 아니라 설계
축으로 바꾸고(①), 버짓·LOD에서는 한 발 앞선다(②).

§2.2의 구성 요소는 **의존 순서**로 나열한다 — ①~⑤가 아래층(평가 엔진),
⑥~⑧이 위층(배분), ⑨~⑩이 실행·경계다.

### 2.1 원리 (언리얼 ABA에서 가져오는 것)

1. 애니메이션 전체에 **프레임당 CPU 시간 상한(ms)** 을 준다.
2. 인스턴스마다 **significance**(중요도)를 계산한다 — 거리·화면 크기·가시성.
3. 인스턴스마다 **실측 비용의 지수이동평균(EMA)** 을 유지한다.
4. 매 프레임 `Σ(이번에 틱할 인스턴스의 기대 비용)`이 버짓을 넘으면
   significance 낮은 쪽부터 **강등**하고, 여유가 지속되면 승격한다.
   진동은 히스테리시스로 막는다.
5. 틱을 건너뛴 프레임은 **포즈 보간**(전/현 포즈 lerp)으로 메운다.

**개정에서 달라지는 것(8-19).** 언리얼 ABA의 강등 축은 *틱 레이트 하나*다.
우리는 태스크 레시피를 갖게 되므로 강등 축이 여덟 단이 되고, **틱 레이트
강등은 그 사다리의 마지막 두 단(L6·L7)으로 내려간다**(§2.2 ⑦). 그리고 비용
추정도 인스턴스 EMA 하나가 아니라 **태스크별 EMA의 합**이 되어, 버짓 적합이
추정이 아니라 계산이 된다(§2.2 ⑧).

### 2.2 구성 요소

**① Pose 값 타입 + AnimInstance — 핫 데이터의 분리.**

먼저 **포즈를 값으로 만든다.** 지금 우리 코드에는 "포즈"라는 값이 존재하지
않는다 — 재귀가 돌면서 결과를 곧바로 `animator.m_FinalTransforms[]`에 쓴다.
포즈가 값이 아니면 (a) 블렌드가 연산자가 될 수 없고(그래서 `BlendAni`가 행렬을
분해했다 조립한다 — E3), (b) **태스크 리스트가 성립하지 않는다**(태스크의
입출력이 곧 포즈이므로). 즉 이 타입은 E3의 해답이자 ③의 전제다.

```cpp
struct Pose {                          // SoA, 로컬 공간
    std::vector<Vector3>    t;
    std::vector<Quaternion> r;
    std::vector<Vector3>    s;         // 비균등 스케일 지원
};

Blend(A, B, alpha, Out)                // 분해 없음 — TRS끼리 lerp/slerp
BlendMasked(A, B, alpha, weights, Out)
MakeAdditive(Src, Skeleton, OutDelta) / ApplyAdditive(Base, Delta, alpha, Out)
ToSkinningMatrices(Pose, Skeleton, Out)   // 행렬은 마지막에 딱 한 번
```

> 비균등 스케일은 지금 표현 자체가 불가능하다 — `calculAni`가
> `m_scaleKeys[i].m_scale.x` 한 축만 읽어 균등 스케일로 가정한다
> (`AnimationJob.cpp`). 이 타입이 그 제약도 함께 푼다.

**AnimInstance**는 `Animator`/`AnimationController`에서 포즈·시간·커서를 떼어낸
인스턴스 상태이고, **시스템이 조밀 배열로 소유한다**(결정 ⑤). 컴포넌트는
핸들만 든다 — 틱이 이미 시스템으로 이관된 트랙 C3의 결과와 결이 맞는다.

```cpp
class Animator : public Component {
    AnimInstanceHandle m_instance;     // 이게 전부. 배열도 포즈도 없다.
};

class AnimationSystem {
    std::vector<AnimInstance>  m_instances;   // SoA, 조밀
    std::vector<AnimTaskList>  m_recipes;     // 용량 프레임 간 재사용
    PoseBufferPool             m_pools;       // 워커별
    FramePaletteArena          m_arena;       // ⑤
};

AnimInstance {
    Pose pose;  Pose posePrev;                 // 실제 본 수만큼 (E6) · 보간의 전제
    시간 상태(클립별 elapsed · progress)        ← Animator/Controller에서 이관
    키 커서: 채널별 마지막 키 인덱스            ← curKey의 올바른 자리 (E2·R1 해소)
    인러셜라이저(전이 봉합, ⑦ L4의 전제)
    significance · 현재 강등 등급 · 다음 평가 프레임
    태스크별 비용 EMA(ms) · 마지막 실측(ms)     ← ⑧이 소비
}
```

`Animator`는 FSM·파라미터·소켓 목록만 남는 얇은 컴포넌트가 된다. 공유 애셋
(`Skeleton`·`Animation`·`Bone`)에는 **런타임 쓰기 0** — 쓸 수 있는 곳이 인스턴스
슬롯뿐이므로 R1이 "고쳤다"가 아니라 **발생 불가**가 된다.

**키 검색도 여기서 바뀐다.** 프레임 간 시간이 단조 증가하므로 직전 키 인덱스를
인스턴스에 두면 대부분 O(1)이 된다(현재는 매 호출 `keys[0]`부터 선형 스캔, 그것도
position/rotation/scale 각각 — E2). 커서를 **공유 애셋이 아니라 인스턴스에** 두는
것이 R1 해소와 같은 수정이다.

**② 채널 테이블 베이크.** (Skeleton × Animation)당 1회, 로드 시:
`본 인덱스 → NodeAnimation*` 평탄 배열. 레이어용으로 `본 인덱스 → 기여
컨트롤러 비트마스크`도 함께 굽는다(E1·E4 소멸). 순회는 `m_bones` 평탄 배열을
부모 선행 순서로 도는 단일 루프(E7 소멸 — 부모 선행이 아니면 베이크 시 정렬).

베이크 캐시의 키는 **`Skeleton::m_serial`**을 그대로 쓴다 — 스켈레톤 인스턴스
마다 유일한 일련번호로 이미 존재하고(`RenderEngine\Skeleton.h`), 해제된 주소가
재할당돼도 캐시가 거짓 적중하지 않는다. Lumina가 같은 목적으로 둔
`FSkeletonResource::BindPoseGeneration`과 같은 역할이며, 우리는 이미 갖고 있다.

본 마스크는 **`BoneRegion` 7분할(Root·Spine·Neck·양팔·양다리)을 은퇴**하고
**이름 기반 dense per-bone weight 배열**로 대체한다. 현 휴머노이드 경로는 팔
전체가 켜지거나 꺼지는 해상도밖에 없어 저작 표현력이 부족하고, 런타임에서는
가중치 배열을 인덱스로 읽기만 하면 되므로 오히려 싸다.

---

**③ 태스크 레시피 — 프론트엔드 중립 계약.** 프레임을 두 패스로 가른다.

```
Update 패스 : 상태머신 전이 · 시간 전진 · 파라미터 평가.  포즈 수학 0회.
              → AnimTaskList에 {SampleClip, Blend, BlendMasked, ApplyAdditive,
                                MakeAdditive, BoneTransform, TwoBoneIK, Output} 기록
Execute 패스: 출력 태스크에서 도달 가능한 체인만 실행.
              포즈 버퍼는 워커별 풀에서 대여, 마지막 소비자면 제자리 덮어쓰기.
```

태스크는 POD 플랫 구조로 두고 의존은 **같은 리스트 내 앞선 태스크의 인덱스**로
표현한다 — 기록 순서가 곧 유효한 실행 순서라 런타임 위상 정렬이 없다.

**계약이 프론트엔드와 무관하다**는 것이 이 항목의 핵심이다:

```
[프론트엔드]                          [계약]           [백엔드]
AnimationController(현 상태머신) ─┐
단일 클립 재생 ───────────────────┼→ AnimTaskList ─→ TaskExecutor ─→ Pose
바이트코드 VM (S8, 후행) ─────────┘   ↑
                                  Degrade(L) — ⑦의 강등이 여기 얹힌다
```

강등이 프론트엔드와 무관해지므로, S8에서 바이트코드 VM을 얹어도 Executor·Pose·
버퍼 풀·강등 사다리는 **한 줄도 바뀌지 않는다**. Lumina가 단일 클립 경로와
그래프 VM 경로를 하나의 태스크 리스트로 받는 것이 이 구조의 선례다.

지금의 "블렌딩 중이면 두 클립을 항상 전부 계산 · 컨트롤러가 3개면 3개 전부
계산"이 도달성 실행으로 사라진다.

---

**④ 관측 본 물질화 (Observed-Bone Materialization).** 뼈 GameObject를
**저장소가 아니라 투영면**으로 재정의한다.

> 포즈의 정본은 `AnimInstance::Pose` 하나뿐이다. 뼈 GameObject의 Transform은
> 그 포즈를 **읽기 전용으로 비추는 창**이고, **관측되는 뼈만 비춘다.**

**"관측된다"의 정의** — 구조 변경 시에만 재계산하는 정적 성질:

1. 뼈가 아닌 자식이 붙어 있다 (무기 · 이펙트 · 콜라이더)
2. 소켓이 걸려 있다
3. 에디터에서 선택됐거나 기즈모 대상이다
4. `BoneComponent::m_bPinned`로 명시 고정됐다 (게임플레이가 직접 읽는 뼈)

```
비관측 뼈: Transform 갱신 0회. 씬에는 존재하고 계층도 살아 있다.
관측 뼈  : Pose에서 FK로 월드 행렬 계산 → Transform에 투영.
```

이것은 유니티의 *exposed transforms*(Optimize Game Objects)와 같은 개념이지만
**옵트인 탈출구가 아니라 기본 동작이고 자동 산출**이다(§0 원칙 2).

**탈출 밸브 — 숨은 소비자를 조용히 깨뜨리지 않는다.** 비관측 뼈의 트랜스폼을
누가 읽으면 `BoneComponent::GetWorldTransform()`이 그 자리에서 포즈로부터
조상 사슬만 타고 계산한다(**O(깊이)이지 O(본수)가 아니다**). 그리고 그 호출이
해당 뼈를 관측 집합으로 **자동 승격**시켜 다음 프레임부터 상시 투영된다.
§6의 "`Bone` 트랜스폼의 숨은 소비자" 리스크가 설계로 해소되는 지점이다 —
전환에 진단 장치를 함께 둔다는 원칙(`diagnostic-with-transition`)의 적용.

**기대 효과.** 744 뼈 노드 × 프레임당 3회 순회 → 관측 본은 통상 캐릭터당 2~5개
(무기 손 · 이펙트 소켓)이므로 투영 대상이 두 자릿수 배로 준다.

**기존 작업을 폐기하지 않는다.** `BoneComponent`의 `m_boneIndex` +
`m_resolvedSerial` 캐시는 그대로 투영 경로의 주소 지정에 쓰인다. 이 결정은
SceneGraph 페이즈의 결과를 **완성**하는 것이지 되돌리는 것이 아니다.

---

**⑤ 팔레트 프레임 아레나 — 컴포넌트에는 배열이 없다.**

```
Execute → Pose
        → PackRenderBones (3×4 행, 실본수)      ← 병렬, 평가된 인스턴스만
        → 프레임 팔레트 아레나 (선형 할당자, 프레임 끝 리셋)
        → ProxyCommand는 {arenaOffset, boneCount}만 든다
        → RenderScene이 아레나를 한 번에 벌크 업로드
```

삭제 대상:

| 대상 | 크기 |
|---|---|
| `Animator::m_FinalTransforms[512]` · `m_localTransforms[512]` | 64KB / 인스턴스 |
| `AnimationController::m_FinalTransforms[512]` · `m_LocalTransforms[512]` | 64KB / 컨트롤러 |
| `ProxyCommand`의 `make_shared<xMatrix[]>(MAX_BONES)` + 32KB memcpy | 매 프레임 힙 할당 |

렌더가 UE식 리테인드 프록시라 **불변 단방향 경계가 이미 있고**, 애니메이션
출력이 그 위에 얹히기만 하면 된다. 유니티는 이 경계가 없어 Transform을 거쳐야
하고 그래서 write-back이 병목이 된다 — 우리 하이브리드가 오히려 유리한 지점이다.
DX12 스키닝 패스의 셰이더 계약은 변경 없음(복사 크기와 출처만 바뀐다).

---

**⑥ Significance 평가.** 프레임 시작에 인스턴스 전수:

```
significance = f( 카메라 거리(CalculateLODDistance — 첫 배선),
                  화면 투영 높이 비율,
                  프러스텀 포함 여부(GetFrustum — FoliageComponent 방식) )
```

멀티카메라에서는 **활성 카메라들에 대한 최대값**을 쓴다(씬뷰+게임뷰 동시 표시
구조 고려). 에디터 프리뷰(게임 미시작)는 significance 고정 1.0 — 저작 중인
캐릭터가 강등되는 일은 없어야 한다.

**⑦ 레시피 강등 사다리 (구 LOD 정책표 대체).**

기존 3세대 엔진의 LOD 축은 둘뿐이다 — 얼마나 자주 도나(틱 레이트), 몇 개 본을
도나(스켈레톤 LOD). 레시피가 있으면 **무엇을 도나**라는 세 번째 축이 열린다.
각 등급은 `AnimTaskList`에 대한 **순수 변환**이다.

| 등급 | 레시피 변환 | 잃는 것 |
|---|---|---|
| L0 | 없음 | — |
| L1 | `TwoBoneIK` · `BoneTransform` 제거 → DepA 패스스루 | 발 IK · 시선 보정 |
| L2 | `ApplyAdditive` 제거 | 호흡 · 흔들림 애디티브 |
| L3 | `BlendMasked` → DepA 패스스루 | 상체 레이어 |
| L4 | `Blend(α)` → `α<0.5 ? DepA : DepB` 스냅 | 크로스페이드 |
| L5 | `ActiveBoneCount` = 저디테일 프리픽스 | 손가락 · 트위스트 · 얼굴 본 |
| L6 | 틱 1/2 → 1/4 + 포즈 보간 | 갱신 빈도 |
| L7 | 동결 (화면 밖) | — |

**세 가지 성질이 이 사다리를 성립시킨다:**

1. **단조성** — 등급이 오를수록 태스크 집합이 진부분집합이다. 비용이 단조 감소.
2. **계산 가능성** — 태스크별 비용 EMA를 합하면 강등 후 비용이 정확히 나온다(⑧).
3. **의미론적 순서** — 멀리 있는 캐릭터에서 먼저 버릴 것은 발 IK이지 갱신
   빈도가 아니다. 기존 LOD는 이 구분을 못 했다.

**L5의 전제 — 부모 선행 정렬.** 본 프리픽스 절단이 성립하려면 `m_bones`가
부모 선행(parents-first) 순서여야 한다. 앞 N개가 그 자체로 유효한 부분 계층이
되기 때문이다(Lumina `LowDetailBoneCount`와 같은 근거). ②의 베이크 단계에서
정렬한다. 잘린 본은 바인드 포즈 로컬을 유지하고 **FK는 전 계층을 계속 돌아**
스키닝과 부착물이 유효하게 남는다. 자동 산출은 금지 — 본 순서는 임포터
의존이라 임의 절단은 시각적으로 중요한 본을 얼릴 수 있다. **저작값만 쓴다.**

**L7 복귀(재가시) 시** 즉시 L0 풀 평가 1회로 포즈를 맞춘다.

**L4의 정직한 리스크 — 팝.** 블렌드 스냅은 전이 중이면 눈에 띈다. 완화 둘:
(a) L4는 화면 투영 높이가 임계 이하일 때만 허용, (b) 승격 시 인러셜라이제이션
으로 복귀. 그래서 **인러셜라이제이션은 "있으면 좋은 것"이 아니라 이 사다리의
전제**다(S4에 포함). 전이 중인 인스턴스의 한 단계 상향 보정도 그대로 유지한다.

**에디터 예외.** ⑥의 significance 고정 1.0에 더해 **강등 등급도 L0 고정**이다.
저작 중인 캐릭터에서 IK나 레이어가 조용히 빠지면 저작이 성립하지 않는다.

**⑧ CPU 버짓 — 추정이 아니라 계산.** 설정값(기본치는 S1 실측 후 확정,
`EngineSetting` 편입).

비용 EMA를 **인스턴스가 아니라 태스크 종류별**로 유지한다. 그러면 임의 등급의
강등 후 비용이 계산으로 나온다:

```
cost(inst, L) = Σ cost_ema(task.Type, boneCount) over tasks surviving Degrade(L)
```

알고리즘: significance 내림차순 정렬 → 각 인스턴스의 강등 사다리를 비용
오름차순으로 놓고, 버짓을 채울 때까지 **등급을 낮춰 가며** 누적 → 초과 지점
이후는 그 인스턴스의 다음 등급을 채택. 승격은 여유가 N프레임 지속 + 경계
significance ±ε의 이중 히스테리시스.

> **언리얼 ABA와 갈리는 지점.** ABA는 인스턴스 EMA 하나로 "레이트를 절반으로
> 낮추면 비용도 대략 절반"을 **가정**한다. 레시피가 costed item의 목록이면
> 그 가정이 필요 없다 — 강등이 레시피에 대한 순수 함수이므로 결과 비용이
> 정확히 계산된다. 이것이 §2.0 ②를 "한 발 앞선다"고 적은 근거다.

**⑨ Job 배치.** 애니메이터 1개=태스크 1개(E9) 대신, 이번 프레임 평가 대상을
**워커 수에 맞춘 청크**로 분할해 던진다. 청크 안에서 인스턴스별 QPC 측정 →
EMA 갱신. fork-join 위치는 현행 유지(`InternalAnimationUpdateEvent` 단계) —
파이프라인 재배치는 이 페이즈 밖.

Update 패스와 Execute 패스는 **각각 별도의 병렬 구간**이다(Lumina와 같은 구도).
Update는 인스턴스별로 자기 컴포넌트만 만지므로 병렬 안전하고, Execute는
레시피 하나가 한 스레드에서 완결된다. 두 패스 사이에 배리어가 필요한 이유는
강등 결정(⑧)이 **모든 레시피의 기대 비용을 모아 놓고** 내려야 하기 때문이다 —
이것이 이 페이즈에서 유일하게 정당한 배리어다.

**⑩ 이벤트 큐.** 워커는 `(animator, event, progress구간)`을 락프리 큐에 적재만
하고, join 직후 **메인 스레드에서 일괄 발화**(R6 소멸). C# 경계는 메인 스레드
고정. 틱을 건너뛴 프레임의 이벤트는 다음 평가 때 진행 구간 `[pre, cur]`로
일괄 판정되므로 유실되지 않는다.

### 2.3 이 엔진에 맞춘 결정

- **새 코드의 자리는 `ScriptBinder\AnimationScheduler.{h,cpp}`.** 헤더는
  RenderEngine에, 구현은 ScriptBinder에 두는 현 `AnimationJob`의 계층 기형을
  반복하지 않는다. `RenderScene`은 팔레트(결과)만 소비하고 스케줄러를 소유하지
  않는다 — PHASE 5(커플링 절단)의 방향과 일치.
- **전용 풀 vs `WorkerPools`는 실측으로 결정한다(S6).** 전용 8스레드는 코어
  점유가 낭비지만, 전역 풀은 UI 렌더 데이터·지형 로드와 경합한다. S1 계측이
  양쪽 비용을 보여준 뒤에 정한다 — 지금 단정하지 않는다.
- **팔레트 전달 계약의 *형태*는 유지, *출처*는 아레나로.** `ProxyCommand` →
  `m_palleteMap` → 스키닝이라는 사슬과 DX12 셰이더 계약은 **수정 없음**. 바뀌는
  것은 (a) 복사 크기가 실제 본 수가 되고 (b) 출처가 컴포넌트 인라인 배열에서
  프레임 아레나 슬라이스가 되는 것뿐이다(⑤·E6).
- **`Mesh::SelectLOD`(메시 LOD)는 이 페이즈 밖.** 같은 significance 입력을
  쓰게 될 미래의 소비자로만 기록해 둔다 — 여기서 배선하면 페이즈가 렌더
  LOD로 번진다.
- **뼈 = GameObject 노선은 유지한다.** SceneGraph 페이즈가 이미 그 방향으로
  결정·구현했고(`BoneComponent`), ④는 그 결정을 배신하지 않고 **비용만**
  제거한다. 뼈를 씬에서 걷어내는 안(UE/Lumina 노선)은 이 페이즈에서 기각.
- **바이트코드 VM은 S8로 후행한다(기각 아님).** 구버전 씬 미호환이 이미 결정된
  사항이므로 저작 자산 파괴는 더 이상 반대 근거가 아니다. 그래도 순서는
  뒤여야 한다 — 근거는 아래 표.

**바이트코드 VM을 S8에 두는 근거 — 무엇이 무엇을 사는가:**

| 이득 | 태스크 시스템(S3.5) | 바이트코드 VM(S8) |
|---|---|---|
| 비활성 가지 스킵 | **O** | |
| 포즈 버퍼 풀링 (E6) | **O** | |
| 인스턴스 단위 병렬 실행 | **O** | |
| 그래프 순회 비용(포인터 추적 → 선형 스캔) | | O |
| 위상 정렬이 컴파일 타임으로 | | O |
| 애셋/인스턴스 분리 **강제** (R1) | 규약으로 | **문법으로** |
| 파라미터가 인덱스 (E10) | 규약으로 | **문법으로** |
| 런타임이 에디터를 모름 | | **O** |

**처리량 이득은 거의 전부 태스크 시스템에서 나오고, 바이트코드가 사는 것은
구조적 강제다.** 구조적 이득은 늦게 와도 값이 그대로지만, 처리량 병목은 먼저
뚫지 않으면 이후 모든 측정이 무의미하다. 그리고 ③의 계약 덕분에 S8은
프론트엔드 교체로 끝난다.

> S8이 실제로 지우는 것 하나를 기록해 둔다 — 지금 `ScriptBinder\AnimationController.h`가
> **`imgui-node-editor/imgui_node_editor.h`와 `nlohmann/json.hpp`를 include하고
> `NodeEditor* m_nodeEditor`를 멤버로 들고 직렬화까지 한다.** 런타임 상태머신이
> 에디터 노드 에디터를 물고 있어 이 헤더를 include하는 모든 TU가 따라 들어온다.
> 바이트코드로 가면 런타임이 노드를 참조할 방법 자체가 없어져 이 간선이 소멸한다
> (PHASE 5 래칫 게이트에 기여).

**기각 목록:**

| 기각 | 이유 |
|---|---|
| 뼈 GameObject 폐지 | SceneGraph 페이즈 결정과 충돌. ④가 비용만 제거한다 |
| 유니티식 옵트인 탈출구 | "알면 빠르고 모르면 느린" 설계(§0 원칙 2). 관측 집합은 자동 산출·기본 동작 |
| 바이트코드 VM 선행 | 태스크 시스템 없이 얹으면 비활성 가지를 여전히 전부 계산 |
| GPU에서 FK 수행 | 부모 의존 사슬이라 레벨별 웨이브프론트 분할 필요. 이득 대비 위험 과다 — 기록만 |
| 휴머노이드 리타게팅 | 별도 페이즈. `BoneRegion` 7분할은 ②에서 dense weight로 대체 |
| 클립 압축(ACL류) | 별도 페이즈. S1 계측이 애셋 크기·캐시 미스를 병목으로 지목하면 그때 |

---

## 3. 단계

| ID | 슬라이스 | 내용 | 완료 기준 | 공수 |
|---|---|---|---|---|
| S0 | 결함 지혈 | R1~R6. `curKey` 죽은 캐시 삭제 · R2 `find`+스킵 · R3 `continue` · R4 클램프 · R5 괄호 · R6 간이 이벤트 큐(수집→join 후 발화). `Bone` 트랜스폼 쓰기는 소비자 grep 후 제거 또는 S3′로 이관 명시(최종 소멸은 S3.6) | 같은 모델 다수 씬에서 공유 애셋 쓰기 grep 0 (curKey·map 삽입) · 이벤트가 메인 스레드에서만 발화 | 1.5일 |
| S1 | 계측 기선 | `PROFILE_CPU_SCOPE("Animation")` 단계 계측 + 인스턴스별 QPC. 캐릭터 N체(10/50/100) 비용 곡선 실측 → 버짓 기본값 근거 확보. 검증 씬 신설 | 비용 곡선 수치가 이 문서 §1에 추가 기록됨 | 1일 |
| S2′ | 데이터 정지 작업 + **Pose 타입** | **`Pose`(SoA TRS) 신설 + 블렌드 커널 4종(§2.2①)** · 채널 테이블 베이크(E1·E4, 키는 `Skeleton::m_serial`) · **마스크를 `BoneRegion` 7분할 → 이름 기반 dense weight로 교체** · 부모 선행 정렬 + 평탄 순회(E7) · 키 커서: 이진 탐색 + **인스턴스** 캐시(E2) · 매 프레임 할당 제거(E5) · 배속 파라미터 핸들화(E10) | 핫 패스에서 문자열 조회 0 · 프레임당 힙 할당 0 · **행렬 분해 블렌드 0** · S1 대비 비용 곡선 재실측 개선 확인 | 4일 |
| S3′ | AnimInstance 분리 (**시스템 소유**) | 핫 데이터 이관 · **`AnimationSystem`이 조밀 배열로 소유, `Animator`는 핸들만** · 포즈 prev/curr 실본수 버퍼 · `Bone` 런타임 쓰기 완전 소멸 · 파일 전역 `m_currAnimator` 은퇴 | 공유 애셋 런타임 쓰기 0 · `Animator`·`AnimationController` sizeof에서 64KB 배열 제거 | 3일 |
| **S3.5** | **태스크 레시피 + Executor** | `AnimTaskList`(POD 플랫, 앞선 인덱스 의존) · Update/Execute 2패스 분리 · **도달성 실행** · 워커별 포즈 풀 + steal-in-place · **팔레트 프레임 아레나**(§2.2⑤) · `ProxyCommand`를 {offset,count}로 | 비활성 상태머신 가지의 포즈 계산 0회(스냅샷으로 판정) · 인스턴스당 살아 있는 포즈 버퍼 ≤ 4 · 팔레트 힙 할당 0 | 3일 |
| **S3.6** | **관측 본 물질화** | 관측 집합 산출(4조건) · 구조 변경 시에만 재계산 · 비관측 본 Transform 갱신 정지 · `GetWorldTransform()` 온디맨드 FK + **자동 승격** · 소켓 경로를 관측 집합으로 통합(현 3중 복제 제거) | 744 뼈 노드 씬에서 프레임당 Transform 기록 수가 관측 본 수와 일치 · 부착물 위치 왕복 검사 통과 | 2일 |
| S4 | Significance + **강등 사다리** | 거리·프러스텀·화면비 배선(§2.2⑥) · **L0~L7 레시피 변환 구현**(§2.2⑦) · **인러셜라이제이션**(L4 전제·승격 복귀) · 보간 · 재가시 복귀 · 에디터 L0 고정 | 화면 밖 캐릭터의 포즈 계산 비용 ≈ 0 · **등급별 비용이 단조 감소**(계측으로 판정) · 강등/승격 전환 팝 없음 | 4일 |
| S5 | CPU 버짓 + 스케줄러 | **태스크 종류별 EMA 비용 모델** · 강등 등급 선택 알고리즘(계산형, §2.2⑧) · 이중 히스테리시스 · `EngineSetting` 설정값 | 100체 씬에서 버짓 상한 준수(초과 프레임 1% 미만) · **예측 비용 대 실측 비용 오차 15% 이내** · 버짓 2배 변화에 등급 분포가 단조 반응 | 3일 |
| S6 | Job 배치 전환 | 청크 분할 · 전용 풀 vs WorkerPools 실측 비교 후 결정 · 이벤트 큐 정식화(락프리) · `AnimationScheduler`로 개명·이주(계층 정위치) | 태스크 수 = O(워커 수) · RenderEngine에 애니메이션 헤더 잔존 0 | 2일 |
| S7 | HUD + 회귀 | ProfilerWindow 버짓 패널(등록/평가/강등 등급 분포 · 버짓 대비 실측 ms · **태스크 실행 스냅샷**: 도달성·실행 순서·버퍼 소유) · 검증 씬을 회귀 세트에 편입(pwsh) | 패널에서 강등이 실시간 관측됨 · 회귀 세트 통과 | 2일 |
| **S8** | **바이트코드 VM (선택·후행)** | 상태머신·그래프를 명령 스트림으로 컴파일 · 레지스터 파일(스칼라/포즈-태스크-인덱스) · 버전 스탬프 · 에디터 컴파일러 분리 · 핀↔레지스터 디버그 오버레이 | **Executor·Pose·버퍼 풀·강등 사다리 무변경** · 런타임에서 `imgui_node_editor` 간선 0 · 파라미터 조회에 문자열·뮤텍스 0 | 6~8일 |

합계 ≈ **25.5일** (S8 제외) / **31.5~33.5일** (S8 포함).
초판 18.5일 대비 +7일 — S2′ +1 · **S3.5 +3** · **S3.6 +2** · S4 +1.

**착수 제약:**

- S0·S1은 **즉시 착수 가능**하고 다른 페이즈와 충돌하지 않는다.
- S2′ 이후는 `AnimationJob.cpp`를 크게 다시 쓰므로 **동시 세션 주의 대상**
  (공유 워크트리 — 착수 직전 HEAD 재대조, 한 슬라이스 한 커밋).
- **S3.5는 S2′(Pose 값 타입)에 의존한다.** 포즈가 값이 아니면 태스크의
  입출력을 정의할 수 없다.
- **S4의 강등 사다리는 S3.5(레시피)에 의존한다.** 레시피가 없으면 L1~L4가
  표현 불가이고 L5~L7만 남는다 — 그건 기존 3세대 엔진과 같은 수준이다.
- **S3.6은 SceneGraph 페이즈와 경계가 걸친다** — §5 참조.
- **S8은 S3.5 이후 언제든**. ③의 계약 덕분에 백엔드를 건드리지 않는다.

---

## 4. 페이즈 완료 기준

1. **정확성** — 공유 애셋(`Skeleton`·`Animation`·`Bone`)에 런타임 쓰기 0.
   같은 모델 100체 씬에서 크래시·포즈 오염 없음. 키프레임 이벤트는 메인
   스레드에서만, 유실·중복 없이(회귀 세트로 판정).
2. **비례성** — 애니메이션 비용이 (평가한 인스턴스 × 실제 본 수 × 실행된
   태스크)에 비례. 문자열 조회·프레임당 힙 할당·행렬 분해 블렌드 0.
   **512 고정 배열 잔존 0.**
3. **불필요한 일을 하지 않음** — 비활성 상태머신 가지의 포즈 계산 0회.
   비관측 뼈의 Transform 기록 0회. 두 항목 모두 계측으로 판정한다.
4. **버짓** — 설정 상한을 넘는 프레임 1% 미만. 화면 밖 캐릭터 비용 ≈ 0.
   버짓 값 변경이 등급 분포에 단조 반영. **예측 비용과 실측 비용의 오차
   15% 이내**(계산형 버짓이 성립했다는 판정 기준).
5. **관측** — HUD 패널에서 인스턴스별 강등 등급·비용·강등 사유가 보이고,
   태스크 스냅샷으로 "이 프레임에 무엇이 실행되고 무엇이 건너뛰어졌는가"가
   재구성이 아니라 **기록**으로 확인된다.
6. **저작 무손상** — 에디터 프리뷰는 항상 L0. 뼈에 부착한 오브젝트(무기·
   이펙트)의 월드 위치가 개편 전후로 동일(회귀 세트의 왕복 검사).

## 5. 다른 계획과의 관계

| 계획 | 관계 |
|---|---|
| **SceneGraph 재설계 (트랙 E)** | **S3.6(관측 본 물질화)이 경계에 걸친다.** 뼈 GameObject의 계약을 "포즈 저장소"에서 "읽기 전용 투영면"으로 바꾸는 결정이므로, `SceneGraphRedesignPlan.md`에도 한 줄 남겨야 한다. `BoneComponent`(E7-b)와 `Scene::UpdateModelRecursive`의 Bone 분기가 직접 대상 — **폐기가 아니라 완성**이다(`m_boneIndex`·`m_resolvedSerial` 캐시는 투영 경로에서 계속 쓰인다) |
| PHASE 5 (커플링 절단) | `AnimationJob`의 RenderEngine 헤더/ScriptBinder 구현 기형이 S6에서 해소. **추가로 S8이 `AnimationController.h` → `imgui_node_editor` 간선을 소멸시킨다** — 둘 다 래칫 게이트 통과 필수 |
| PHASE 9 (생명주기) | 스케줄러는 `InternalAnimationUpdateEvent` 델리게이트 단계를 그대로 쓴다 — 델리게이트 은퇴가 이 단계에 오면 그때 이관(이 페이즈에서 선제 이동 안 함) |
| MultiCameraRenderPlan | significance는 활성 카메라 최대값 — 뷰별 시간축 상태 원칙(잔상 교훈)과 충돌 없음(포즈는 뷰 무관 단일) |
| PHASE 12.5 (빌드) | 무관 — 파일 충돌만 회피 |
| BehaviorTreeManagedPlan (9-8) | BT가 애니메이터 파라미터를 만지는 경로는 `SetParameter`(뮤텍스) 유지 — 계약 변화 없음 |

## 6. 리스크

- **이벤트 발화 시점 이동(R6 수정).** 워커 즉시 → join 후 일괄. 같은 프레임
  안이므로 관찰 가능한 차이는 "이벤트 핸들러가 그 순간의 본 행렬을 읽는 경우"
  뿐 — 오히려 완성된 포즈를 읽게 되어 개선이다. 회귀 세트로 발화 순서·개수를
  고정해 두고 간다.
- **틱 강등의 게임플레이 영향.** 시간·progress·이벤트는 항상 전진하므로 판정
  로직은 무손상. 위험은 시각뿐이고 LOD 경계·보간이 그 완충이다. 그래도
  히트박스가 본을 따라가는 게임플레이가 있다면 해당 캐릭터에 강등 면제
  플래그(significance 고정)를 준다 — 설계에 포함.
- **`Bone` 트랜스폼의 숨은 소비자.** 에디터 본 시각화 등이 공유 `Bone`의
  런타임 값을 읽고 있을 수 있다 — S0에서 grep 전수 후 S3′ 이관 목록에 명시.
  **S3.6이 이 리스크를 설계로 흡수한다**: 비관측 본 조회 시 온디맨드 FK +
  자동 승격이므로, grep이 놓친 소비자도 조용히 깨지지 않고 스스로 드러난다.

- **관측 본 집합의 정확성 (S3.6 최대 리스크).** 빠뜨리면 무기가 손을 안
  따라간다. 완화 셋 — (a) 자동 승격 밸브, (b) 회귀 세트에 **"소켓 부착물
  월드 위치 왕복"** 검사 추가, (c) 승격 발생 시 로그를 남겨 저작 시점에
  누락된 조건을 드러낸다. 전환에는 진단 장치를 함께 둔다는 원칙의 적용
  (`diagnostic-with-transition` — 정적 분석 두 번이 틀렸고 왕복 검사와
  폴백 로그가 둘 다 정정했던 사례).

- **L4(블렌드 스냅)의 팝.** 화면 투영 높이 임계 이하에서만 허용하고 승격 시
  인러셜라이제이션으로 복귀한다. 인러셜라이제이션이 S4에서 빠지면 L4는
  사다리에서 제외해야 한다 — 순서 의존이므로 S4 안에서 함께 착수한다.

- **강등 예측의 신뢰도.** 태스크별 EMA가 본 수에 대해 선형이라는 가정 위에
  선다. `BlendMasked`처럼 마스크 가중치 분포에 따라 비용이 달라지는 태스크는
  가정이 흔들릴 수 있다 — S5 완료 기준에 **예측 대 실측 오차 15% 이내**를
  넣어 둔 이유다. 초과하면 해당 태스크 종류만 인스턴스별 EMA로 내린다.

- **성능 판정은 Release로만.** Debug는 같은 조건에서 25배 느리고 규모별
  개선 방향까지 뒤집는다. 회귀 세트가 Debug exe를 쓰므로 S1·S2′·S5의 비용
  곡선은 **반드시 Release 바이너리로** 재실측한다(`perf-measure-release-only`).
- **공유 워크트리 동시 커밋.** `AnimationJob.cpp`·`Animator.h`는 게임 스크립트
  가 넓게 물고 있는 헤더 — 슬라이스 착수 직전 HEAD 재대조, 한 슬라이스 한 커밋.
