# 애니메이션 스케줄러 · LOD · CPU 버짓 재설계 (PHASE 13)

2026-08-11 수립 · 2026-08-19 설계 개정 · **2026-09-20 S0 완료·S0.5 공용 풀 이관**.

목표는 애니메이션 평가를 Pose 값·태스크 레시피·도달성 실행으로 바꾸고,
그 위에 가시성 LOD와 프레임 CPU 버짓을 얹는 것이다.

1. 시간·progress·키프레임 이벤트는 포즈 평가 빈도와 분리한다.
2. 공유 모델 generation은 불변이다. 가변 재생 상태는 인스턴스가 소유한다.
3. 빠른 경로를 기본값으로 유지하고, 에디터 프리뷰는 L0로 평가한다.

**현재 상태:** S0·S0.5 완료. S0.5의 공용 thread_pool·job_scheduler와 DataSystem·썸네일·Animation·Foliage·AI 이관은 Debug/Release 빌드·제품 회귀, Release DX12 화면·썸네일·AI/BT 회귀를 통과했다.
통일을 우선하며 성능 비교는 완료 조건에서 제외한다. 현재 검증·잔여 범위는 §9를 따른다.
S1~S7은 미완료, S8 VM은 중단이며 이 페이즈 완료 범위에서 제외한다.
아래 현황이 8월 조사보다 우선한다. 과거 공수는 재산정 전 참고치이며 남은 공수가 아니다.

## 1. 현재 코드 기준선 (2026-09-20)

### 1.1 호출 사슬

```text
Runtime::TickSimulationFrame
 └ SceneManager::GameLogic
    ├ Scene::Update → AnimatorSystem::Update → Controller FSM
    ├ InternalAnimationUpdateEvent → AnimationJob::Update
    │  ├ Animator당 작업 하나 → job_group → 공용 job_scheduler/enkiTS
    │  ├ 불변 ModelAssetGeneration → 인스턴스 포즈·이벤트 큐
    │  └ join → Scene::PublishAnimatorPose + 소켓 Transform 반영
    └ Scene::LateUpdate
 이후 RuntimeFrame::TickManagedPostPhysics → FlushScriptMessages → C#
 렌더 발행: ProxyCommand → PrimitiveRenderProxy → EnhancedDrawItem → 스키닝
```

구현은 `Engine/SceneRuntime/AnimationJob.cpp`, 헤더·소유자는 아직 RenderEngine의
`AnimationJob.h`·`RenderScene`이다. 렌더는 불변 프레임 packet을 별도 스레드에서 소비한다.
S6에서 소유 경계를 정리하되, 애니메이션 join은 packet 발행 전에 끝나야 한다.

### 1.2 정확성 — 기존 결함과 S0 잔여를 구분한다

| 항목 | 9-20 정찰 결과 | S0 처리 |
|---|---|---|
| R1 공유 Skeleton/Animation/Bone 쓰기 | typed generation 전환으로 과거 경로 소멸. 공유 데이터는 const, 출력은 Animator 소유 | 불변 generation 계약 유지·다수 인스턴스 제품 회귀 필요 |
| R2 map 삽입·빈 키 OOB | 본 인덱스 채널 표와 누락 검사로 제거 | 새 큐/캐시를 만드는 작업 아님 |
| R3 잘못된 전체 return | continue로 처리 중 | 유지 |
| R4 keys[-1] | 실제 ModelAnimationSampler는 유효 구간 안에서 탐색 | 호출 없는 CurrentKeyIndex 잔재 삭제 |
| R5 초기 상태 선택 | 연산자 우선순위 오류가 살아 있었음 | 첫 유효 비-AnyState 선택, 선택 클립 동기화·시간 초기화 |
| R6 워커 C# 직호출 | 이미 QueueScriptMessage → RuntimeFrame flush 사용 | 기존 게임 스레드 전달 경계 유지. 새 이벤트 큐를 중복 도입하지 않음 |
| S0-L 레이어 포즈 | 제외·비활성·채널 부재에서 영행렬/이전 레이어 버퍼가 섞일 수 있었음 | 평가된 활성 채널만 합성, 모두 제외하면 이전 로컬 유지. 최초 로컬·팔레트는 identity. 로컬·팔레트·소켓을 같은 선택값에서 산출 |
| S0-S 블렌딩 소켓 | 단일 컨트롤러의 nextClip이 있으면 소켓 staging을 건너뛰었음 | 최종 블렌드 포즈에서 소켓 계산. 다중 레이어는 합성 뒤에만 계산. 실제 부착물 위치 회귀 통과 |
| S0-E 이벤트 구간 | 래핑 후 progress 비교만으로 여러 루프·정지·역재생을 구별할 수 없음 | 래핑 전 구간 보존. 정방향 (이전,현재], 역방향 [현재,이전), 정지는 0회. 루프 경계·반복 횟수·클립 내 시간순 보존 |

S0-E의 0 키와 1 키는 서로 다른 저작 이벤트다. 루프 경계에서 끝의 1과 다음
시작의 0을 각각 전달한다. 같은 시각은 저작 순서가 우선한다. 인스턴스 사이의
워커 완료 순서를 전역 이벤트 순서로 보장하지 않는다. 0길이/비유한 시간은
안전한 0 포즈 시간과 빈 이벤트 구간으로 처리한다.

**검증 기록:** `Tools/regression/verify-animation-playback.ps1`의 Debug·Release
격리 회귀 각각 45검사 통과. 제품이 사용하는 `AnimationPlayback.h`의 시간 진행·
이벤트 열거·레이어 선택을 직접 검사한다. `verify-legacy-skeleton-retirement.ps1`
통과(접촉 2/2, 창구 밖 0/0; 남은 접촉은 구 씬 키 읽기).
추가한 `verify-animation-product.ps1`은 Debug/Release 전체 Editor 빌드 후 각각
`ANIMATION_PRODUCT_OK actors=100 rounds=12 checks=80314 managedThreadErrors=0`을 냈다.
실제 재생 전환·generation·AnimationJob·RuntimeFrame·C# 콜백과 씬 본/소켓 부착물을
검사했다. 이어서 `verify-animation-visual.ps1`의 Debug/Release DX12 실제 화면 비교도
각각 13캡처·264검사 통과했다. **S0 완료**이며 상세 근거는 §7.2에 기록한다.

### 1.3 남은 비용

| 축 | 현재 | 후속 |
|---|---|---|
| E1 채널 접근 | 문자열 map 대신 본 인덱스 표, 평가마다 재생성 | S2′ generation·clip 키로 베이크 |
| E2 키 탐색 | 유효 구간 선형 탐색 | S2′ 이진 탐색·인스턴스 커서 |
| E3 포즈/블렌드 | matrix4x4, 블렌드마다 decompose | S2′ SoA TRS와 커널 |
| E4 마스크 | BoneRegion 또는 이름 선형 조회 | S2′ dense weight |
| E5 할당 | Animator 목록·controller 캡처·채널 표·globals·이벤트 정렬 | S2′ 재사용 저장소 |
| E6 크기 | Animator 512행렬 두 벌(64KiB), Controller 한 벌(32KiB) | S3′ 실제 본 수 버퍼 |
| 팔레트 | 프록시 발행마다 512행렬 할당/복사, draw.boneCount도 512 | S3.5 실제 본 수·프레임 저장소 |
| E7 본 순회 | 부모 선행 평탄 순회는 이미 구현 | 다시 구현하지 않음 |
| 본 투영 | topology/generation 바인딩 캐시·변경 로컬만 기록 | S3.6 관측 집합 + 팔레트 변경 독립 추적 |
| E8·E9 스케줄링 | 공용 enkiTS, Animator당 작업 하나, LOD/버짓 없음 | S3.5·S4·S5·S6 |
| E10 배속 파라미터 | mutex·이름 조회 | S2′ 핸들화 |

`m_currAnimator`, legacy Bone 쓰기, `AnimationController.h`의 NodeEditor 직접
의존은 이미 사라졌다. 이를 S3′·S8의 남은 작업으로 다시 세지 않는다.

### 1.4 기반과 의존

- 공용 실행 기반은 `thread_pool` + `job_scheduler`다. DataSystem·썸네일·AnimationJob·Foliage·AI가 사용한다. 엔진 부팅/종료가 워커 수명을 소유하고 소비자는 자기 그룹만 기다린다. 의존 작업은 기다리는 동안 워커를 점유하지 않는다. 계약은 [JobSchedulerDesign](../design/JobSchedulerDesign.md), 검증은 §9를 따른다.
- `AssetLoadJob` 삭제는 `317ab497`에서 완료됐다. S0.5 남은 구현에 중복 산입하지 않는다.
- 현재 단계 마커는 `SceneManager::GameLogic`의 InternalAnimationUpdateEvent 전체를
  감싼다. S1은 Release 10/50/100체에서 계산·대기·본 투영·팔레트 전달을 분리한다.
  독립 QPC 측정은 먼저 가능하나, 워커 프로파일러 마커는
  [ProfilingCapturePlan](ProfilingCapturePlan.md) §0.5.5의 안전한 수집 경계 이후다.
- 본 캐시의 신원은 `{ModelId, SkeletonId, generation}`이며
  `Animator::GetSkeletonSerial()`이 현재 그 신원을 해시한다. 삭제된 Skeleton::m_serial을 쓰지 않는다.
- **S3.6 선행:** `Scene::PublishAnimatorPoseImpl`은 현재 localWrites > 0일 때
  팔레트 프록시 dirty를 발행한다. 비관측 본 기록을 줄이기 전에 팔레트 변경을
  별도로 추적해야 한다. 관측 본이 0이어도 스킨 포즈가 움직이는 회귀가 필요하다.

### 1.5 다른 엔진 대조 — 2026-08-19 당시 기록 (현행 판정은 §1.1~1.4)

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
(`ModelAssetGeneration`의 skeleton·animations)에는 **런타임 쓰기 0** — 쓸 수 있는 곳이 인스턴스
슬롯뿐이므로 R1이 "고쳤다"가 아니라 **발생 불가**가 된다.

**키 검색도 여기서 바뀐다.** 프레임 간 시간이 단조 증가하므로 직전 키 인덱스를
인스턴스에 두면 대부분 O(1)이 된다(현재는 매 호출 `keys[0]`부터 선형 스캔, 그것도
position/rotation/scale 각각 — E2). 커서를 **공유 애셋이 아니라 인스턴스에** 두는
것이 R1 해소와 같은 수정이다.

**② 채널 테이블 베이크.** (Skeleton × Animation)당 1회, 로드 시:
`본 인덱스 → NodeAnimation*` 평탄 배열. 레이어용으로 `본 인덱스 → 기여
컨트롤러 비트마스크`도 함께 굽는다(E1·E4 소멸). 순회는 `m_bones` 평탄 배열을
부모 선행 순서로 도는 단일 루프(E7 소멸 — 부모 선행이 아니면 베이크 시 정렬).

베이크 캐시는 `{ModelId, SkeletonId, generation, AnimationId}`로 식별한다. 현재 본 투영 캐시의 `GetSkeletonSerial()` 해시를 참고하되, 클립까지 구별하고 새 generation 게시 시 이전 캐시가 재사용되지 않게 한다.

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
공유 Bone은 이미 은퇴했다. 남은 Entity Transform 소비 누락을 다루는 지점이다 —
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
| `AnimationController::m_LocalTransforms[512]` | 32KiB / 컨트롤러 |
| `ProxyCommand`의 `make_shared<math::matrix4x4[]>(MAX_BONES)` + 32KiB copy | 프록시 발행마다 힙 할당 |

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

**⑩ 이벤트 전달.** 현재 `QueueScriptMessage`의 보호된 큐와 join 이후
게임 스레드 flush를 유지한다. Update는 매 프레임 래핑 전 시간 구간으로
이벤트를 판정하며, 포즈 Execute를 건너뛰어도 이벤트를 다음 평가로 미루지 않는다.
S0의 구간·순서 계약은 §1.2를 따른다. S6의 큐 구조 변경은 계측 필요성이
있을 때 같은 전달 계약을 보존하며 진행한다.

### 2.3 이 엔진에 맞춘 결정

- **새 코드의 자리는 `Engine/SceneRuntime/AnimationScheduler.{h,cpp}`.** 헤더는
  RenderEngine에, 구현은 SceneRuntime에 두는 현 `AnimationJob`의 계층 기형을
  반복하지 않는다. `RenderScene`은 팔레트(결과)만 소비하고 스케줄러를 소유하지
  않는다 — PHASE 5(커플링 절단)의 방향과 일치.
- **공용 enkiTS로 통일한다(9-20 확정).** 전용 풀 비교를 S6의 선택 조건에서 제거했다. AnimationJob도 S0.5에서 이관하며 S6은 청크 크기·작업 저장소·소유 계층 최적화에 집중한다.
- **팔레트 전달 계약의 *형태*는 유지, *출처*는 아레나로.** `ProxyCommand` →
  `PrimitiveRenderProxy::m_finalTransforms` → EnhancedDrawItem → 스키닝이라는 사슬과 DX12 셰이더 계약은 **수정 없음**. 바뀌는
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

> 2026-09-20 정정: AnimationController.h의 NodeEditor/json 직접 의존은 이미 제거됐다. VM으로만 지울 수 있는 간선으로 세지 않는다. S8은 중단하며 향후 별도 근거가 있을 때 재제안한다.

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
| S0 | 결함 지혈 · 완료 | §1.2의 R5·S0-L·S0-E 구현, 기존 불변 자산·게임 스레드 이벤트 전달 유지. 블렌딩 소켓·비활성 메시 그리기 수정 | Debug/Release 격리·100체/CLR/소켓·DX12 화면 회귀, 전체 Editor 빌드 통과 (§7) | 완료 |
| S0.5 | 공용 실행 기반 통일 | thread_pool·job_scheduler·job_handle·job_group, 엔진 수명 소유, DataSystem·썸네일·Animation·Foliage·AI 이관 | 그룹 독립 대기·의존·종료 drain·제품 회귀(§9). 성능 비교 제외 | 기존 2일·재산정 필요 |
| S1 | 계측 기선 | Release 10/50/100체 계산·대기·투영·팔레트 비용 QPC 측정. 워커 수집은 PHASE 14 안전 경계 이후 | 비용 곡선·측정 조건·버짓 기본값 근거 기록 | 1일 |
| S2′ | 데이터 정지 작업 + Pose 타입 | SoA TRS·블렌드 커널·generation/clip 채널 캐시·dense mask·키 커서·할당 제거·배속 핸들. 부모 선행 평탄 순회는 완료 | 문자열 조회·프레임 할당·행렬 분해 블렌드 0, S1 대비 재측정 | 기존 4일·재산정 필요 |
| S3′ | AnimInstance 시스템 소유 | 가변 재생 데이터를 조밀 배열로 이관, Animator는 핸들. 실본수 prev/curr 버퍼. legacy Bone 쓰기·전역 m_currAnimator 제거는 이미 완료 | Animator 64KiB·Controller 32KiB 고정 포즈 배열 제거 | 기존 3일·재산정 필요 |
| **S3.5** | **태스크 레시피 + Executor** | `AnimTaskList`(POD 플랫, 앞선 인덱스 의존) · Update/Execute 2패스 분리 · **도달성 실행** · 워커별 포즈 풀 + steal-in-place · **팔레트 프레임 아레나**(§2.2⑤) · `ProxyCommand`를 {offset,count}로 | 비활성 상태머신 가지의 포즈 계산 0회(스냅샷으로 판정) · 인스턴스당 살아 있는 포즈 버퍼 ≤ 4 · 팔레트 힙 할당 0 | 3일 |
| **S3.6** | 관측 본 물질화 | 관측 집합·온디맨드 FK·자동 승격·소켓 통합. 팔레트 변경 알림을 localWrites 조건에서 분리 | 관측 본 0에서도 스킨 팔레트 전진·비관측 본 기록 0·부착물 왕복 검사 | 2일 |
| S4 | Significance + **강등 사다리** | 거리·프러스텀·화면비 배선(§2.2⑥) · **L0~L7 레시피 변환 구현**(§2.2⑦) · **인러셜라이제이션**(L4 전제·승격 복귀) · 보간 · 재가시 복귀 · 에디터 L0 고정 | 화면 밖 캐릭터의 포즈 계산 비용 ≈ 0 · **등급별 비용이 단조 감소**(계측으로 판정) · 강등/승격 전환 팝 없음 | 4일 |
| S5 | CPU 버짓 + 스케줄러 | **태스크 종류별 EMA 비용 모델** · 강등 등급 선택 알고리즘(계산형, §2.2⑧) · 이중 히스테리시스 · `EngineSetting` 설정값 | 100체 씬에서 버짓 상한 준수(초과 프레임 1% 미만) · **예측 비용 대 실측 비용 오차 15% 이내** · 버짓 2배 변화에 등급 분포가 단조 반응 | 3일 |
| S6 | Job 청크화·계층 정리 | 공용 job_scheduler 위 청크 분할·재사용 저장소 · 기존 이벤트 전달 유지 · AnimationScheduler 개명·SceneRuntime 소유 이주 | 태스크 수 = O(워커 수) · RenderEngine 애니메이션 헤더 잔존 0 | 2일 |
| S7 | HUD + 회귀 | ProfilerWindow 버짓 패널(등록/평가/강등 등급 분포 · 버짓 대비 실측 ms · **태스크 실행 스냅샷**: 도달성·실행 순서·버퍼 소유) · 검증 씬을 회귀 세트에 편입(pwsh) | 패널에서 강등이 실시간 관측됨 · 회귀 세트 통과 | 2일 |
| **S8** | 바이트코드 VM · 중단 | 현재 완료 범위에서 제외. 재개하려면 별도 실측 근거 필요 | 현 페이즈 완료 기준에 포함하지 않음 | 합산 제외 |

과거 합계 **27.5일**(S8 제외)은 계획 당시 공수다. 선행 완료분과 S0 검증 범위가 달라져 **현재 잔여 공수로 사용하지 않는다**. S1 기선 이후 다시 산정한다.
초판 18.5일 대비 +9일 — S2′ +1 · **S3.5 +3** · **S3.6 +2** · S4 +1 · **S0.5 +2**(9-15 편입).

**착수 제약:**

- S0·S0.5·S1의 독립 QPC 기선은 착수 가능하다. DataSystem·썸네일·대시보드의 동시 변경을 대조하고, 워커 프로파일러 수집은 PHASE 14의 안전한 전달 경계를 선행한다.
- **S0.5의 공용 기반을 S6에서도 그대로 사용한다.** AnimationJob 실행기 교체는 선행하고, 평가 단위·버퍼 소유 변경은 S2′~S3.5와 S6에서 진행한다.
- S2′ 이후는 `AnimationJob.cpp`를 크게 다시 쓰므로 **동시 세션 주의 대상**
  (공유 워크트리 — 착수 직전 HEAD 재대조, 한 슬라이스 한 커밋).
- **S3.5는 S2′(Pose 값 타입)에 의존한다.** 포즈가 값이 아니면 태스크의
  입출력을 정의할 수 없다.
- **S4의 강등 사다리는 S3.5(레시피)에 의존한다.** 레시피가 없으면 L1~L4가
  표현 불가이고 L5~L7만 남는다 — 그건 기존 3세대 엔진과 같은 수준이다.
- **S3.6은 SceneGraph 페이즈와 경계가 걸친다** — §5 참조.
- **S8은 중단·완료 범위 제외**다. 재개는 별도 제안으로 결정한다.

---

## 4. 페이즈 완료 기준

1. **정확성** — 공유 `ModelAssetGeneration`에 런타임 쓰기 0.
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
| PHASE 5 (커플링 절단) | `AnimationJob`의 RenderEngine 헤더/SceneRuntime 구현 기형이 S6에서 해소. Controller의 NodeEditor 직접 간선은 이미 제거됐다. S6은 헤더·소유 이주 후 래칫 게이트 통과 필요 |
| PHASE 9 (생명주기) | 스케줄러는 `InternalAnimationUpdateEvent` 델리게이트 단계를 그대로 쓴다 — 델리게이트 은퇴가 이 단계에 오면 그때 이관(이 페이즈에서 선제 이동 안 함) |
| MultiCameraRenderPlan | significance는 활성 카메라 최대값 — 뷰별 시간축 상태 원칙(잔상 교훈)과 충돌 없음(포즈는 뷰 무관 단일) |
| PHASE 12.5 (빌드) | 무관 — 파일 충돌만 회피 |
| BehaviorTreeManagedPlan (9-8) | BT가 애니메이터 파라미터를 만지는 경로는 `SetParameter`(뮤텍스) 유지 — 계약 변화 없음 |

## 6. 리스크

- **이벤트 구간 수정(S0-E).** C# 전달은 기존 join 후 게임 스레드 큐를 유지한다. 같은 프레임
  안이므로 관찰 가능한 차이는 "이벤트 핸들러가 그 순간의 본 행렬을 읽는 경우"
  뿐 — 오히려 완성된 포즈를 읽게 되어 개선이다. 회귀 세트로 발화 순서·개수를
  고정해 두고 간다.
- **틱 강등의 게임플레이 영향.** 시간·progress·이벤트는 항상 전진하므로 판정
  로직은 무손상. 위험은 시각뿐이고 LOD 경계·보간이 그 완충이다. 그래도
  히트박스가 본을 따라가는 게임플레이가 있다면 해당 캐릭터에 강등 면제
  플래그(significance 고정)를 준다 — 설계에 포함.
- **본 Transform 소비자와 팔레트 dirty 결합.** 공유 Bone 타입은 은퇴했다. S3.6은 Entity/BoneComponent 조회·소켓 소비와 localWrites에 결합된 렌더 dirty를 분리해야 한다. 관측 집합의 누락은 온디맨드 FK·자동 승격·왕복 검사로 검증한다.

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

## 7. S0 실행 기록 (2026-09-20)

- 구현: 초기 상태 선택·시간 초기화, 다중 레이어의 staging/최종 포즈 분리,
  활성·채널·마스크 조건 합성, 래핑 전 이벤트 구간과 정방향/역방향·반복 전달.
- 격리 회귀: Debug/Release 각각 `ANIMATION_PLAYBACK_OK checks=45`.
- 제품 컴파일: AnimationJob.cpp·AnimationEventBridge.cpp·AnimationController.cpp·
  Animator.cpp 각각 Debug/Release x64 exit 0. 다른 작업의 산출물과 겹치지 않는
  `Build/Obj/Phase13S0/Selected-{Debug,Release}`에 생성했다.
- 재현: `Tools/regression/verify-animation-playback.ps1 -CompileProduct`.
  컴파일은 개별 TU 검사이며 전체 Editor 링크·실행을 뜻하지 않는다.
- 초기 전체 non-unity 컴파일 시도는 수정하지 않은 ComponentFactory·EntityAuthoringRead·
  PrefabUtility·ClrHost 등의 include 의존 오류로 exit 1이었다. 이 시도와 변경 TU의
  성공을 구분한다. 원래 unity 제품 빌드의 성공/실패는 이 결과로 판정하지 않는다.
- 기존 Skeleton 은퇴 검사 통과. 대시보드 JS 파싱·12항목/상태 검사 통과,
  이번 변경 전 대시보드와 비교해 PHASE 13 밖의 내용은 동일함을 확인했다.

### 7.1 제품 실행 검증

- Debug/Release x64 **전체 CreatorEditor 빌드·링크 exit 0**. VS18/v145,
  기본 unity 설정을 사용했다. 앞의 개별 non-unity 실패와 구분한다.
- `Tools/regression/verify-animation-product.ps1 -Configuration Debug` 및
  `-Configuration Release` 모두 exit 0. 각각
  `ANIMATION_PRODUCT_OK actors=100 rounds=12 checks=80314 managedThreadErrors=0 model=Gunner_F_Mythic`.
- 새 `animation.playback.probe`는 Commandlet 전용이다. 새 빈 씬에서 실제 `play` 전환이
  확정된 뒤 시작한다. 테스트를 위해 재생 상태 비트를 직접 바꾸거나 관리 큐 전달
  규약을 우회하지 않는다.
- 같은 불변 generation을 공유하는 100 Animator를 생명주기로 등록하고, 서로 다른
  재생 시각의 병렬 팔레트를 12회 순차 제품 표본과 대조했다. 씬 BoneComponent 투영도
  확인했다. 종료 시 테스트 객체를 제거하고 등록 수 0을 단정한다.
- 실제 C# 인스턴스가 수신 횟수·순서를 기록한다. worker join 직후에는 0건이고
  `RuntimeFrame`의 post-physics flush 뒤에 전달된다. 정방향 다중 루프·역재생·속도 0·
  비루프 끝 도달/반복 틱에서 기대 횟수와 순서가 일치했고, 생성 게임 스레드 밖 수신은 0건.
- 활성/비활성 레이어, 전체 humanoid 마스크 제외, 유효 클립이 없는 레이어,
  이름 마스크 제외, 이전 local과 현재 parent를 합친 팔레트·소켓을 검사했다.
- 추가 수정: 단일 컨트롤러의 블렌딩도 최종 포즈로 소켓을 갱신한다.
  회귀는 소켓 staging을 영행렬로 오염시킨 뒤 새 포즈·commit·부착물 위치를 확인하므로
  예전 `nextClip == nullptr` 조건이 돌아오면 실패한다.
- 일반 명령 표 회귀 통과: 골든/현재 **133개 동일**. 신규 Commandlet이 일반 명령으로
  노출되지 않는다. 대시보드 PHASE 13 밖의 내용 보존 검사도 유지한다.
- 실행 결과: `Build/Obj/Phase13S0/Product-{Debug,Release}/results.jsonl`.
  이 결과의 실행 시간은 기동·CLR·검사 비용을 포함하므로 S1 성능 기선으로 쓰지 않는다.
- 이 테스트는 팔레트/씬/CLR 제품 경로를 검사하며 GPU 픽셀을 대조하지 않는다.
  당시 잔여였던 스킨 메시·소켓 부착물 화면 회귀는 아래 §7.2에서 완료했다.

### 7.2 실제 렌더링 회귀

- `verify-animation-visual.ps1`은 `FT_Primitives`의 조명·카메라를 사용하고,
  Gunner 스킨 메시 두 개와 오른손 소켓의 붉은 큐브를 제품 경로로 그린다.
  Commandlet 전용 `animation.visual.probe`가 확정된 일시정지 Play 상태에서
  `DrainPendingLifecycle → SyncDerivedState → AnimationJob.Update(0) →
  SyncDerivedState → UpdateRenderData`를 호출한다. 일시정지 프레임은 spatial
  갱신을 생략하므로 고정 포즈 검사에서 이 정상 게시 경계를 명시적으로 사용한다.
- `render.pbr.capture ... game controlled`의 **실제 DX12 GPU readback**을 비교한다.
  캡처마다 새 렌더 프레임을 사용하며, 7개 attachment의 유한값·렌더 오류를 확인한다.
  baseColor/depth/normal/display에서 같은 포즈의 반복, 블렌드 0/100%, 전체 레이어,
  비활성 레이어, 전체 마스크 제외, 숨김 후 복귀의 동등성을 검사한다.
- 클립 시각 변경·블렌드 중간·상체 마스크는 본 팔레트와 실제 스킨 깊이가 달라야 한다.
  모델 월드 행렬은 고정하고, 붉은 큐브 주변을 제외한 픽셀로 판정하여 오브젝트 이동만으로
  통과할 수 없게 했다. 소켓 위치와 draw의 월드 위치, 화면에 투영한 위치의 붉은 픽셀도 확인한다.
- 이 검사로 메시 비활성 플래그가 `BuildDrawPool`에서 무시되는 제품 결함을 찾았다.
  수정 전 `Visual-Release-2`에서는 숨긴 캐릭터가 기본 자세로 계속 그려졌다.
  `poolMesh`가 비활성 프록시를 제외하도록 수정했으며, 비활성 배경 메시 제외와
  숨김 시 draw 0개, 다시 표시했을 때 포즈·소켓 복귀를 함께 검사한다.
- Debug/Release 전체 Editor 빌드·링크 exit 0. 두 구성 모두
  `ANIMATION_VISUAL_OK captures=13 checks=264`이며, 실제 캡처 모음도 직접 확인했다.
  근거: `Build/Obj/Phase13S0/Visual-Release-3` 및 `Visual-Debug-1`의
  `visual-summary.json`과 `animation-contact-sheet.png`.
  캡처 원본은 각 포즈 폴더의 `.f32`와 `manifest.json`이다. **S0 완료**로 반영한다.
  이 검사는 DX12 정확성 회귀이며 Vulkan 패리티·S1 성능 기선·S4 LOD 승격 검증은 포함하지 않는다.

## 8. S0.5 공용 WorkerPool 이관 — 중간 기록 (2026-09-20)

> 아래는 job_scheduler 도입 이전 결과다. 전용 풀 유지·우선순위 비교 선행 결정은 §9로 대체했다.

- ① AssetLoadJob 삭제와 ② enkiTS 공용 풀 이관까지 완료했다. 외부 스레드 제출,
  대기 없는 비동기 완료, callback capture 수명, 예외 전달, 종료 drain을 보존한다.
  구현·근거는 [TaskSchedulerUnificationPlan](TaskSchedulerUnificationPlan.md) §8을 따른다.
- 독립 검사 Debug/Release 각각 `WORKER_POOL_OK checks=10257`. 실제 어댑터에서
  대기를 제거한 변이도 두 구성 모두 독립 완료 계수 단정으로 검출했다.
- 반복 실행 중 발견한 전달 작업의 조기 해제 경합을 수정했다. enkiTS가 제출 함수에서
  아직 읽는 객체를 완료 측이 먼저 해제하지 않도록 양쪽 참조를 유지한다. 게시/후속 읽기
  구간을 넓힌 ASan 집중 검사 273개 통과, 조기 해제 변이는 `AddPinnedTaskInt`에서
  `heap-use-after-free`로 실패했다. 재현: `verify-worker-pool-lifetime.ps1`.
- 수명 경합 수정 후 Debug/Release 전체 Editor 빌드·링크 exit 0. 두 구성에서 실제 번들 32건과 외부 제출 64건을
  함께 실행해 완료 수 일치, 제출 스레드에서 실행된 작업 0건을 확인했다.
  `enkiTS.dll`은 기존 배포 경로의 `Runtime/Common`에 포함됐다.
- 실제 썸네일 Release 회귀 56검사 통과: 디코딩·게시·손상 파일 실패·수정 후 무효화·
  재게시·예산 축출. 늦은 완료 폐기는 이번 실행에서 0건으로 별도 입증하지 않았다.
- **테스트 모델 변경 반영:** 기존 Gunner 대신 이미 교체된 `CreatorRobot.glb`를 사용한다.
  모델의 SHA-256은 `58D779AFDE1A7FBD13332999C8B11890FB7E10FF2036CA6AE3645D14BEDBA402`.
  기존 §7의 Gunner 결과는 당시 기록으로 남긴다. 새 모델은 이름으로 Walk/Run을 선택하며,
  소켓 화면 표식은 초록색이다. Release DX12 화면 회귀 `captures=13 checks=264` 통과,
  실제 캡처 모음도 확인했다.
- CreatorRobot의 제품 애니메이션 검사도 Debug/Release 각각
  `ANIMATION_PRODUCT_OK actors=100 rounds=12 checks=69514 managedThreadErrors=0 model=CreatorRobot`.
  모델의 본 구성 변경으로 검사 수가 달라졌으며, 기존 Gunner의 80314개 결과와 혼용하지 않는다.
- 실행 근거: `Build/Obj/Phase13S05/Pool-{Debug,Release}`, `SubmissionLifetime`,
  `Product-Debug`, `Product-Release-Final`, `Animation-Debug`, `Animation-Release-Final`,
  `Thumbnails-Release-Final`, `Visual-Release-Final`. 화면 회귀는 정확성 검사이며 성능 측정이 아니다.
  일반 명령 표 골든은 133개 동일하다(`Registry-Release`).
- **남은 S0.5:** ③ 공용 풀과 AnimationJob 풀의 동시 경합 조건에서 우선순위 실측 후
  자체 풀 기본값 변경, ④ Foliage/AI `std::async` 이관. 잔존 5곳을 계획된 3곳으로
  줄이는 게이트는 아직 미실행이다. AnimationJob 풀 교체는 계속 S6 범위다.

## 9. S0.5 공용 실행 기반 통일 (2026-09-20)

§8 이후 사용자 결정에 따라 AnimationJob도 지금 공용 기반으로 옮겼다.
전용 풀 비교와 동시 경합 우선순위 측정은 이번 완료 조건에서 제외한다.

- 공용 타입: `thread_pool`, `job_scheduler`, `job_group`, `job_handle`.
  엔진 부팅/종료에서 지속 워커를 소유한다. 각 소비자는 자기 그룹을 기다린다.
- 이관: DataSystem·BrowserThumbnailCache·AnimationJob·Foliage·AI.
  AnimationJob의 Animator당 작업 단위와 join 뒤 포즈·소켓 게시 경계를 유지한다.
- 구 전용 풀과 WorkerPool API는 제거했다. AI는 Entity/Component 파괴·DDOL 이송,
  CLR 종료 전에 회수한다. 공용 풀은 씬 해체 후 종료한다.
- 독립 Debug/Release 각각 10332검사, 배리어 제거 변이 검출. ASan 348검사와
  제출 중 조기 해제 변이 검출. 제품 검증은
  [TaskSchedulerUnificationPlan §9](TaskSchedulerUnificationPlan.md#9-공용-스케줄러-구현-2026-09-20)에 기록한다.
- 테스트 모델은 CreatorRobot이다. §7의 Gunner 결과와 혼용하지 않는다.
- Debug/Release 전체 Editor 빌드·제품 검사 통과: 각 번들 32/32·외부 읽기 64·
  Foliage 73, 애니메이션 100체×12회·69514검사·관리 스레드 오류 0.
  Release DX12 13캡처·264검사, 썸네일 56검사, AI registry·BT 실행/씬 교체,
  일반 명령 133개 골든도 통과했다. Player Release는 컴파일·링크 확인까지이며
  PDB 기호 경고와 실행 미검증 범위는 부속 계획 §9에 남겼다. **S0.5 완료**다.
- PSO 비동기 컴파일은 공용 스케줄러로 후속 이관했다(부속 계획 §10).
  씬 로드 2곳도 공용 스케줄러로 후속 이관했다(부속 계획 §11). DX12/Vulkan 명령 기록도 공용 Job 그룹으로 이관했다(부속 계획 §12).
  S1 이후 평가 비용 최적화·LOD·버짓·AnimationScheduler 도메인 소유 이주는 남아 있다.
