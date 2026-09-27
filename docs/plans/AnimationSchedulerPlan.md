# 애니메이션 스케줄러 · LOD · CPU 버짓 재설계 (PHASE 13)

2026-08-11 수립 · 2026-08-19 설계 개정 · **2026-09-24 S2′ 구현 완료, 최종 검증은 §15**.

목표는 애니메이션 평가를 Pose 값·태스크 레시피·도달성 실행으로 바꾸고,
그 위에 가시성 LOD와 프레임 CPU 버짓을 얹는 것이다.

1. 시간·progress·키프레임 이벤트는 포즈 평가 빈도와 분리한다.
2. 공유 모델 generation은 불변이다. 가변 재생 상태는 인스턴스가 소유한다.
3. 빠른 경로를 기본값으로 유지하고, 에디터 프리뷰는 L0로 평가한다.

**현재 상태(2026-09-27):** S0~S7 완료(중간 S2′·S3′·S3.5·S3.6 포함, S6 검증은 §45, S7 검증은 §46). S7 HUD·회귀 세트와 Debug/Release 제품·Release DX12 픽셀 회귀, 실제 에디터 HUD 육안 확인을 마쳤다. S0.5의 공용 thread_pool·job_scheduler와 DataSystem·썸네일·Animation·Foliage·AI 이관은 Debug/Release 빌드·제품 회귀, Release DX12 화면·썸네일·AI/BT 회귀를 통과했다.
통일을 우선하며 성능 비교는 완료 조건에서 제외한다. 현재 검증·잔여 범위는 §9를 따른다.
2026-09-24 PHASE 14 P1b/P2의 안전한 워커 계측 경계를 확인하고 S2′ 채널 캐시·버퍼 재사용(§11), TRS 포즈·전이 블렌드(§12), 키 커서(§13), dense mask·짧은 채널 분기(§14), 가중치·비균등 스케일·배속 핸들·평가 저장소 재사용(§15)을 구현했다. 단일 Walk 성능은 S2d보다 개선되지 않았다.
§47의 AnimationGraph 확장 AG0~AG5가 남았다. S8 VM은 중단이며 이 페이즈 완료 범위에서 제외한다.
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
| E1 채널 접근 | generation이 클립별 본 인덱스 표를 게시 전에 베이크, 평가자는 불변 span 참조(§11) | S2′ 첫 슬라이스 |
| E2 키 탐색 | 제품은 현재/다음 클립별 인스턴스 커서·인접 구간 확인·이진 탐색(§13). 선형 탐색은 독립 진단 기준선으로 유지 | 제품 비용 증가 원인 분리·후속 최적화 |
| E3 포즈/블렌드 | 실제 본 수 SoA TRS 샘플링·전이 블렌드, 가중 masked/additive 본 커널, 비균등 스케일(§12·§15) | S3′ 인스턴스 저장소 이관·S3.5 태스크 실행 |
| E4 마스크 | 슬롯별 본 인덱스 float weight 조회. 새 저작은 이름 기반, 옛 7영역 마스크는 읽기·명시적 전환 경로 유지(§15) | S3.5 태스크 레시피 연결 |
| E5 할당 | 채널·globals·Animator 목록·계측·이벤트 정렬/스크립트 목록은 예열 뒤 저장소 재사용(§11·§15) | `job_group` 제출·팔레트 할당은 S6·S3.5 |
| E6 크기 | S3′ 진행으로 Animator/Controller 고정 행렬 제거. 시스템 인스턴스의 행렬·SoA 포즈는 실본수 할당, Animator 시간·커서·평가 임시 저장소도 이관. Controller 상태는 잔존 | S3′ 나머지 이관·조밀 데이터 배치 |
| 팔레트 | 프록시 발행마다 512행렬 할당/복사, draw.boneCount도 512 | S3.5 실제 본 수·프레임 저장소 |
| E7 본 순회 | 부모 선행 평탄 순회는 이미 구현 | 다시 구현하지 않음 |
| 본 투영 | topology/generation 바인딩 캐시·변경 로컬만 기록 | S3.6 관측 집합 + 팔레트 변경 독립 추적 |
| E8·E9 스케줄링 | 공용 enkiTS, Animator당 작업 하나, LOD/버짓 없음 | S3.5·S4·S5·S6 |
| E10 배속 파라미터 | 상태별 index+version 핸들로 재사용, 구조/이름 변경 때 재해석(§15) | S3′ 인스턴스 저장소 이관 |

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
- **S3.6 완료(§37):** 팔레트 프록시 dirty는 본 Transform 기록과 독립적으로
  추적한다. 관측 본 0개에서도 스킨 포즈가 전진하는 제품·화면 회귀를 추가했다.

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
| S1 | 계측 기선 · 완료 | Release 10/50/100체 제품 경로 QPC, 3회×120표본 (§10). 워커 Profiler 마커는 PHASE 14 P2에서 연결 완료 | 비용 곡선·측정 조건·예산 후보/적용 한계 기록 | 완료 |
| S2′ | 데이터 정지 작업 + Pose 타입 · 완료 (§15) | SoA TRS·블렌드/가중 마스크/additive 본 커널·generation/clip 채널 캐시·dense mask·키 커서·평가 임시 저장소 재사용·배속 핸들. 부모 선행 평탄 순회는 선행 완료 | 본별 이름 탐색·배속 파라미터 선형 탐색·행렬 분해 블렌드 제거, 예열 후 평가 임시 저장소 재사용, S1/S2d 대비 재측정. 제출/팔레트 할당은 S6/S3.5 | 완료 |
| S3′ | AnimInstance 시스템 소유 · 완료 (§16~§26) | 가변 재생 데이터를 주소 고정 연속 페이지로 이관, Animator는 세대 핸들. 실본수 prev/curr 버퍼. legacy Bone 쓰기·전역 m_currAnimator 제거는 선행 완료 | 고정 포즈 배열과 시간·커서·레이어/FSM 실행 상태 이관, DDOL·게임 스레드 수명 계약, DX12 Editor/Player 제품 회귀 통과. Vulkan은 후속 통합 검증 | 완료·기존 3일 재산정 전 수치 |
| **S3.5** | **태스크 레시피 + Executor · 완료 (§27~§36)** | `AnimTaskList`(POD 플랫, 앞선 인덱스 의존) · Update/Execute 2패스 분리 · **도달성 실행** · 워커별 포즈 풀 + 최종 출력 소유권 이전 · **팔레트 프레임 아레나**(§2.2⑤) · 렌더 명령 페이로드 {offset,count} · 뷰별 벌크 업로드와 GPU 3×4 전달 · 샘플·전이 블렌드·FK·마스크·가산 합성 태스크 | 비활성 상태머신 가지의 포즈 계산 0회(스냅샷으로 판정) · 인스턴스 1 + 작업자 최대 3 포즈 버퍼 · 예열 뒤 포즈 저장소 성장/팔레트 아레나 할당 0 (§36) | 완료·기존 3일 재산정 전 수치 |
| **S3.6** | **관측 본 물질화 · 완료 (§37)** | 관측 집합·조상 경로 온디맨드 FK·자동 승격·소켓 통합. 팔레트 변경 알림을 localWrites 조건에서 분리 | 관측 본 0에서도 스킨 팔레트 전진·비관측 본 기록 0·부착물 왕복 검사 | 완료·기존 2일 재산정 전 수치 |
| **S4 완료 (§43)** | Significance + **강등 사다리** | 거리·프러스텀·화면비 배선(§2.2⑥) · **L0~L7 레시피 변환 구현**(§2.2⑦) · **인러셜라이제이션**(L4 전제·승격 복귀) · 보간 · 재가시 복귀 · 에디터 L0 고정 | 화면 밖 캐릭터의 포즈 실행 0 · 100체 제품 실행 비용의 측정 오차 10% 범위 내 비증가 · 강등/승격 첫 출력 연속성 (§43). 전체 프레임 지연은 S5/S6에서 별도 관리 | 완료·기존 4일 재산정 전 수치 |
| **S5 완료 (§44)** | CPU 버짓 + 스케줄러 | **태스크 종류별 EMA 비용 모델** · 강등 등급 선택 알고리즘(계산형, §2.2⑧) · 이중 히스테리시스 · `EngineSettings.asset` 설정값 | Release 100체 기본 4ms 예산 120프레임×5회 초과 0회 · 예측 대 태스크 실측 중앙 오차 각 15% 이내 · 1→2ms 변화에 평균 등급 단조 반응. 1ms는 비용 하한 아래의 포화 조건으로 별도 기록 | 완료·기존 3일 재산정 전 수치 |
| **S6 완료 (§45)** | Job 청크화·계층 정리 | 공용 job_scheduler의 인덱스 배치로 최대 워커×2 청크 · 재사용 저장소 · 기존 이벤트 전달 유지 · AnimationScheduler 개명·SceneRuntime 소유 이주 | Release 8워커의 10/50/100체 제출 Job 10/16/16 · 실제 평가 수 유지 · RenderEngine의 스케줄러 헤더·호출 잔존 0 (§45) | 완료·기존 2일 재산정 전 수치 |
| **S7 완료 (§46)** | HUD + 회귀 | ProfilerWindow 버짓 패널(등록/평가/강등 등급 분포 · 버짓 대비 실측 ms · **태스크 실행 스냅샷**: 도달성·실행 순서·버퍼 소유) · 검증 씬을 회귀 세트에 편입(pwsh) | Debug/Release 제품·Release DX12 픽셀 회귀 통과. 실제 Editor 창에서 L0 실행·L7 강등/스킵·태스크 행과 저장소 소유 표시 확인 | 완료·기존 2일 재산정 전 수치 |
| AG0 | Graph 기준선·자산 계약 | 기존 씬/Editor/C#·레시피 재작성·신원/버전/실패 정책 확정 (§47.3) | 제품·Player·S7 스냅샷 기준선과 호환 정책 기록 | **2일** |
| AG1 | Clip PoseSource 어댑터 | State root 및 구 `AnimationIndex` 로드 어댑터 (§47.3) | 구 씬·포즈·이벤트·LOD/버짓 동등성 | **4일** |
| AG2a | 불변 Graph 정의 | 노드/상태/파라미터 schema, 자산 신원·저장·cook·검증 (§47.3) | 공유 정의의 직렬화/재로드·잘못된 참조 실패 | **5일** |
| AG2b | 인스턴스 바인딩 | 기존 AnimInstance에 노드/FSM 상태·재바인딩, Controller 저작값 변환 (§47.3) | 100체 상태 격리·DDOL·재게시·Player | **5일** |
| AG3 | ParameterId·전이 바인딩 | C# 이름 표면 유지, 타입별 ID 값과 컴파일 조건 (§47.3) | 이름/타입 변경·트리거 1회·핫 경로 이름 탐색 0 | **4일** |
| AG4a | Blend1D/2D | 샘플 시간·가중치·Pose 태스크 확장 (§47.3) | 경계·역방향·다중 클립 이벤트·LOD/버짓 회귀 | **4일** |
| AG4b | 레이어 노드 | 현 Controller 마스크/가산을 LayerBlend/BoneMask/Additive 소스로 옮김 (§47.3) | 기존 레이어 결과·버퍼 상한·L2/L3 재작성 동등성 | **3일** |
| AG4c | Slot/one-shot | 임시 재생·중단·교차 페이드와 이름 기반 `Play` 표면 (§47.3) | 이벤트 중복·L4 전이·Editor/C#/Player 제품 경로 | **5일** |
| AG5 | 후속 노드 계약·Compiler 판정 | PoseCache/일반 IK·Root Motion/Notify의 구현 분리와 Graph compile/VM 비용·배포 근거 (§47.3) | 후속 별도 공수 원장과 실측; VM 자동 재개 없음 | **4일** |
| **S8** | 바이트코드 VM · 중단 | 현재 완료 범위에서 제외. 재개하려면 별도 실측 근거 필요 | 현 페이즈 완료 기준에 포함하지 않음 | 합산 제외 |

과거 합계 **27.5일**(S8 제외)은 당시 계획 공수이며 선행 완료분의 실공수나 현재 잔여 공수가 아니다. **이번에 추가한 AG0~AG5의 신규 계획 공수는 36인일**(2+4+5+5+4+4+3+5+4)이다. S7 미완 게이트의 잔여 공수는 기존 2일 전체 추정과 구분해 아직 재산정하지 않았으므로, 27.5일과 36일을 현재 총잔여 공수처럼 합산하지 않는다. §47.3의 자산/cook·구 씬 전환·Editor 접점은 AG0 기준선 이후 재견적한다.
대시보드의 PHASE 13 가중 집계는 **기존 추정 행 20.5일 + 신규 36일 = 56.5일**이다. 완료된 S2′·S3′의 `days: null` 두 행과 중단된 S8은 공수 분모에서 제외된다. 56.5일은 표의 계획량이며 남은 작업일이나 실투입 공수가 아니다.
초판 18.5일 대비 +9일 — S2′ +1 · **S3.5 +3** · **S3.6 +2** · S4 +1 · **S0.5 +2**(9-15 편입).

Vulkan 제품·백엔드 교차 판정은 후속 통합 검증에 모아 수행한다. 각 슬라이스의
DX12 제품 검증과 구분하며 S3′의 완료 조건으로 삼지 않는다.

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
- **AG0~AG5는 PHASE 13 확장 완료 범위**다. S7의 시각 게이트와 스냅샷 기준선을 먼저 닫고 AG0부터 순서대로 진행한다. AG2·AG4는 §47.3처럼 독립 빌드 슬라이스로 나눈다.

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
7. **Graph 확장** — AG0~AG4c가 기존 씬/Prefab·C# 표면·이벤트/소켓·LOD/버짓을 보존하며 불변 Graph 정의, 시스템 소유 노드 상태, Clip/Blend/Layer/Slot Pose 출력을 기존 `AnimTaskList`에 연결한다. AG5의 후속 노드·Compiler/VM 판정과 별도 공수 원장을 남긴다. VM 구현, 일반 IK·PoseCache·Root Motion/Notify 구현은 이 기준에 포함하지 않는다(§47).

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

## 10. S1 제품 경로 비용 기선 (2026-09-20)

테스트 모델은 CreatorRobot을 유지했다. 모델 해시는 §8과 같다. Release x64, VS18/v145,
Intel Xeon W-2223 3.60GHz, 엔진 공용 워커 8개. 실행 중 빌드·다른 Player는 없었다.
독립 프로세스 3회, 인원별 30회 준비 뒤 120표본씩, 합계 1,080표본이다.

### 계측 경계

- 엔진 공용 enkiTS 워커 8개, Animator당 job 하나인 현재 실행기를 그대로 측정한다.
- AnimationMeasurementScope는 호출 스레드에서만 sample을 등록한다. worker는 job별로
  분리된 슬롯에 기록하고 그룹 wait 완료 뒤 소유 스레드가 합산한다. 기존 Profiler의
  TLS EventBuffer에 worker가 쓰지 않는다. 측정 비활성 때 QPC 호출과 timing 배열 할당은 없다.
- prepareUs: animator snapshot과 job/capture 구성. submitUs: 공용 scheduler 제출.
  waitUs: 제출 반환부터 이 그룹 회수까지 호출자 경과 시간.
- workerSumUs: 각 worker 콜백 경과 시간의 합계(운영체제 CPU 사용 시간이 아님).
  workerSpanUs: 첫 콜백 시작부터 마지막 콜백 종료까지다. 둘 다 wait/submit과 겹친다.
- publishUs: 실제 Scene::PublishAnimatorPose 호출, socketUs: join 뒤 소켓/부착물 반영.
  updateUs: 이 단계들과 계측 집계를 포함하는 AnimationJob::Update 전체 경과 시간.
- syncUs: Scene::SyncDerivedState. renderCommitUs: Scene::UpdateRenderData.
  paletteUs는 실제 ProxyCommand 생성자의 512행렬 버퍼 할당·초기화·복사만 재며
  renderCommitUs의 부분집합이다. GPU 업로드 시간이 아니다.
- cpuFrameUs = updateUs + syncUs + renderCommitUs. 병렬 콜백 합계나 paletteUs를
  여기에 다시 더하지 않는다. 시뮬레이션 전체 프레임 시간이나 GPU 시간으로 해석하지 않는다.
- 명령은 paused Play에서 제품 모델 인스턴스·컨트롤러·본·메시·소켓을 생성한다.
  Walk, dt=1/60, 30회 준비 후 120회 측정. 인스턴스 초기 위상은 분산한다.
  씬 생성/에셋 로딩/cleanup/원자료 직렬화는 시간 구간에서 제외한다.
- 실제 dirty commit으로 만든 palette batch를 매회 계측 뒤 캡처·폐기하고 superseded로
  계수한다. 동기 명령 실행 중 메모리가 누적되지 않는다. GPU 소비·렌더 경합은 이 기선에 없다.
- 모델 본 54개 중 암묵적인 루트를 제외한 씬 본 53개, 메시 4개, 소켓 1개/인스턴스.
  모든 표본에서 평가 인원·씬 본 바인딩 수·palette copy 수/bytes와 non-finite 포즈를 검사한다.
- 100체 실측 준비 중 MakeSocket의 복제 이름 (10) 제한을 발견했다. Scene pose binding과
  같은 RemoveSuffixNumberTag를 써서 actor subtree 안에서 찾고, string_view 길이도 보존한다.
- 단일 모델/한 컨트롤러/한 소켓의 warm CPU 기선이다. 다중 레이어·콜드 로딩·GPU 경합·
  다른 CPU·LOD 강등 품질은 포함하지 않는다. 기존 100체 제품 정확성 회귀를 따로 실행한다.

### Release 결과

단위 ms. 각 인원별 360표본을 합친 중앙값(p50)과 p95다.

| 인원 | 전체 경로 p50 / p95 | worker 합산 p50 | owner 대기 p50 / p95 | 본 반영 p50 | 팔레트 할당·복사 p50 | 전송 바이트/표본 |
|---:|---:|---:|---:|---:|---:|---:|
| 10 | 0.324 / 3.761 | 0.100 | 0.050 / 3.436 | 0.034 | 0.101 | 1,310,720 |
| 50 | 1.930 / 8.047 | 0.453 | 0.116 / 5.792 | 0.182 | 0.768 | 6,553,600 |
| 100 | 8.741 / 12.694 | 0.946 | 0.198 / 2.213 | 0.507 | 5.729 | 13,107,200 |

세 실행의 전체 경로 p50 범위는 10체 0.307–0.335, 50체 1.844–2.001,
100체 8.526–9.027ms다. 상세 단계별 평균/p50/p95와 소스 해시는
[측정 요약 JSON](../analysis/AnimationBaseline20260920.json)에 보존했다.

- 100체의 worker 합산 중앙값은 0.946ms, 첫 시작~마지막 완료 구간은 0.148ms다.
  전체 경로 8.741ms 중 팔레트 할당·복사 중앙값이 5.729ms다. 스레드 수를 더 늘리는
  선택의 근거는 없으며 S3.5의 실제 본 수/인스턴스별 공유 팔레트 저장소가 주요 개선 대상이다.
- 100체는 매 표본 400개 × 512행렬 = 12.5MiB를 복제한다. 이 값은 CPU snapshot
  payload 크기이며 GPU 전송량을 계측한 값이 아니다. 실제 본은 54개다.
- 작은 부하에서도 대기 p95가 크다. wait에는 큐 대기·wake·완료 통지 및 호출자 재스케줄링이
  함께 들어가므로 원인을 특정하지 않는다. 평균/중앙값만 보고 안정적인 프레임이라고
  판정하지 않는다. PHASE 14의 안전한 worker 수집 경계가 다음 선행 과제다.
- **S5 초기 예산 후보는 평가 작업 합산 QPC 2.0ms**로 잡는다. 100체 단일 레이어의
  합산 p95 1.155ms에 약 73% 여유가 있는 출발값이다. 이는 운영체제 CPU 사용 시간이나
  전체 애니메이션 프레임 2ms 보장이 아니다. 본/팔레트 게시 비용과 대기 꼬리는 별도다.
  다중 레이어·태스크 레시피별 비용을 PHASE 14 이후 재측정해 S5에서 설정·강등에 배선한다.
  이번에는 EngineSetting이나 런타임 강등 기본값을 변경하지 않았다.

### 검증과 재현

- Editor/Player Debug·Release 빌드 exit 0. 기존 LNK4229/LNK4020와 관리 코드 분석 경고는
  남아 있으며 경고 없는 빌드로 선언하지 않는다.
- Release 3회·Debug 1회 모두 10/50/100체 × 120표본에서 평가 수·본 53개/체·팔레트
  4개/체 경로 단정을 통과했다. Debug 시간은 성능 기선에 섞지 않는다.
- 기존 애니메이션 제품 회귀도 Debug/Release 각각 `actors=100 rounds=12 checks=69514
  managedThreadErrors=0 model=CreatorRobot`으로 통과했다. 신규 대규모 인스턴스 검사는
  MakeSocket의 (10) 제한을 실제로 검출한 뒤 수정본에서 통과했다.
- 재현: `Tools/regression/measure-animation-baseline.ps1 -Configuration Release -Repeats 3`.
  원자료는 `Build/Obj/Phase13S1/Release-final/run-*/results.jsonl`, 구성별 검증은
  같은 Phase13S1의 `Debug-final`, `Product-Debug`, `Product-Release`에 있다.
- **S1 기선 확보 완료. 다음 순서는 PHASE 14 P1b/P2 → PHASE 13 S2′~S3.5**다.
  현재 비용을 줄이는 코드는 넣지 않았고 백엔드별 성능 비교도 수행하지 않았다.

## 11. S2′ 첫 슬라이스 — 채널 캐시·평가 버퍼 재사용 (2026-09-24)

PHASE 14의 워커 등록·봉인 페이지 전달·불변 캡처가 연결되어 선행 조건을 충족했다.
S2′를 한 번에 교체하지 않고 채널 접근과 평가용 임시 저장소부터 옮긴다.

- `ModelAssetGeneration`이 게시되기 전에 클립별 `bone index → track` 표를 한 번 만든다.
  캐시 신원은 generation 객체와 그 내부 clip index다. generation은 이동·복사 불가이며
  표와 트랙을 함께 소유한다. 워커는 기존 `shared_ptr<const ModelAssetGeneration>`을
  작업 끝까지 보유하고 `AnimationTracks()`의 읽기 전용 span만 소비한다.
- `UpdatePose`의 현재/다음 채널 표 생성과 `UpdateLayer`의 채널 유무 표 재생성을 제거한다.
  없는 클립과 비활성 컨트롤러의 span은 비워 이전 레이어 staging이 섞이지 않게 한다.
- 전역 포즈 행렬은 Animator별 `m_poseGlobals`를 재사용한다. 부모 선행 순회가 매번
  모든 슬롯을 덮어쓴다. 레이어별 채널 span 저장소도 재사용하고 바인딩 해제·교체 때 비운다.
  바인딩 시에는 버퍼를 미리 할당하지 않고, 실제 첫 평가에서 필요한 크기를 준비한다.
  Animator마다 하나의 평가 job만 실행되고 owner가 그룹을 기다리는 기존 수명 계약을 유지한다.
  임시 할당을 지속 보유로 바꾸므로 globals는 Animator당 `vector capacity × 64B`를 유지한다.
  CreatorRobot 54본으로 처음 준비하면 3,456B/체이며, 더 작은 모델로 재바인딩해도 용량은
  재사용한다. 채널 표는 인스턴스마다 복제하지 않는다.
- 제품 회귀에 캐시를 거치지 않는 기존 샘플러와의 포즈 대조, 워밍업 후 버퍼 재사용,
  채널 표의 클립/본 대응과 바인딩 해제·재연결 검사를 추가했다.

**이번에 남기는 범위:** SoA TRS·행렬 분해 없는 블렌딩, 키 커서, dense mask,
배속 핸들, Animator 목록·controller 캡처·이벤트 정렬의 할당 제거는 다음 S2′ 슬라이스다.
512본 팔레트 할당·복사는 변경하지 않았으며 S3′~S3.5에서 처리한다.

**검증:** 변경 전·후 최신 Release Editor 빌드와 CreatorRobot Walk 10/50/100체
각 3회×120표본을 확보했다. 변경 후 Debug Editor 빌드도 통과했다. 기존 C4244·LNK4229 등
경고는 남아 있어 경고 없는 빌드로 선언하지 않는다. 독립 재생 규칙 검사는 Debug·Release
각 45항목, 제품 회귀도 두 구성 모두 100체×12회·134,810검사·관리 스레드 오류 0건으로
통과했다. Release DX12 화면 회귀는 13캡처·264검사를 통과했다. 최신 Debug 실행 파일의
프로파일러 Workers 게이트는 워커 8개 모두 이벤트를 기록하고 최근 8프레임의 AnimationJob
56건·SceneActivated 1건을 확인했다. **첫 슬라이스의 구현·정확성 검증 완료, S2′ 전체는 진행 중이다.**
원본은 `Build/Obj/Phase13S2a/`에 보존하며 측정 JSON에 실행 파일과 주요 소스 해시를 기록한다.
최종 코드의 빌드는 `lazy-release-build.log`·`lazy-debug-build.log`, 제품 검사는
`Product-Final-Release`·`Product-Final-Debug`, 화면 검사는 `Visual-Final-Release`,
워커 연결 검사는 `Profiler-Final-Workers`다. 최종 측정(`Lazy-Release`)의 소스·런타임 DLL
해시를 현재 파일과 대조했고 모두 일치했다.

### 최종 코드 3회 비교

기존 S1과 같은 고정 조건이다: CreatorRobot Walk, 8워커, 30프레임 예열 뒤 120표본,
프로파일러 녹화를 요청하지 않은 paused Play의 동기 CPU 경로. 두 버전 각각 새 프로세스
3회, 인원별 360표본의 nearest-rank 중앙값이다. 워커 합산은 대기와 겹치는 벽시계 시간이고,
전체 경로는 표본마다 `update + sync + renderCommit`을 합친다. GPU 처리·프레젠테이션은 제외한다.

| 인원 | 워커 합산 전→후 ms | 전체 CPU 경로 전→후 ms | owner wait p95 전→후 ms |
|---:|---:|---:|---:|
| 10 | 0.0990 → 0.0907 | 0.3355 → 0.3808 | 1.8407 → 5.9703 |
| 50 | 0.4579 → 0.4272 | 2.1584 → 2.1337 | 3.8772 → 7.6578 |
| 100 | 0.9480 → 0.8848 | 9.9443 → 10.0087 | 8.0110 → 3.9623 |

50·100체 워커 합산 중앙값은 각각 약 6.7% 감소했다. 전체 경로는 50·100체에서 거의 같고
10체에서는 증가했다. 대기 p95와 실행별 팔레트 비용 변동도 크므로 **전체 프레임 성능
개선으로 판정하지 않는다.** 워커 평가 비용 감소와 전체 경로·GPU 처리량을 구별한다.
실행별 값·소스 해시·원본 위치는 [측정 요약](../analysis/AnimationCacheBaseline20260924.json)에 있다.

초기 구현은 바인딩 때 globals를 선할당했고, 최초 3회 비교에서 50체 전체 경로가
2.1584→2.9307ms로 증가했다. 추가 1회에서도 2.725ms여서, 사용하지 않는 Animator에도
버퍼를 만드는 선할당을 제거하고 첫 평가에서 준비하도록 정리했다. 최종 수치는 위 표다.
구체적인 힙 배치·캐시 효과를 원인으로 확정하지 않는다. 초기 구현 3회와 확인 1회도
측정 JSON의 `intermediateEager`·`eagerConfirmation` 및 원본 폴더에 보존했다.

## 12. S2′ 두 번째 슬라이스 — TRS 포즈·전이 블렌드 (2026-09-24)

- `Animation::LocalTransform`은 translation·rotation·3축 scale 값이며,
  `Animation::LocalPose`는 각 성분을 별도 배열에 저장하는 본 인덱스 SoA다.
  새 엔진 타입·네임스페이스·메서드는 PascalCase, 필드는 `m_camelCase`를 따른다.
- `SampleLocalTransform`은 기존 위치·회전·스케일 키 탐색을 재사용한다.
  `UpdatePose`의 첫 순회가 샘플·블렌드 결과를 포즈 값으로 만들고 두 번째 부모 선행
  순회가 행렬 변환·FK·팔레트·소켓을 계산한다. 전이마다 두 행렬을 만들고 두 번
  분해하던 `BlendPose`는 제거했다. 독립 기준선인 `SampleLocal`의 행렬 산술은 유지한다.
- `Animator::m_sampledPose` 하나를 컨트롤러별 평가가 순서대로 재사용한다.
  첫 평가에서만 실제 본 수로 준비하고, 바인딩 해제·교체 때 내용은 비우되 용량은
  유지한다. 현재/다음 클립과 공유 generation에는 가변 포즈를 쓰지 않는다.
  채널 없는 슬롯은 TRS 항등으로 초기화하고, FK의 기존 채널 부재 규칙은 유지한다.
- 블렌드 양 끝은 입력 TRS를 그대로 반환한다. 중간 회전은 정규화한 쿼터니언의
  최단 호 slerp, 위치·스케일은 lerp다. 0 스케일도 전이할 수 있고 음수 스케일의
  축·부호를 보존한다. 이는 분해 실패 때 현재 포즈에 멈추거나 반사 축을 다시
  선택하던 행렬 방식의 한계를 없앤 동작이다. 양수 스케일의 기존 경로와는 오차
  허용 범위 내 동등성을 검사한다.
- 값 타입은 비균등 스케일을 표현하지만 **자산 샘플러는 아직 X축 균등 스케일 규칙**을
  유지한다. 비균등 클립 재생 지원까지 완료한 것으로 세지 않는다. 레이어의
  마지막 유효 채널 선택·마스크 제외 시 이전 로컬 보존, 이벤트·owner 반영 계약도 유지한다.

**남은 범위:** dense mask·masked/additive 포즈 커널·키 커서·배속 핸들·나머지 프레임
할당은 S2′ 후속이다. Controller 행렬 staging과 Animator의 512본 행렬 두 벌은 S3′,
팔레트 프레임 저장소는 S3.5에서 이관한다. 현재 TRS scratch는 추가 저장소이므로
기존 고정 팔레트 메모리를 줄였다고 주장하지 않는다. 공용 enkiTS 실행기는 그대로다.

**검증:** Editor Debug/Release 빌드 성공(기존 C4244·LNK4229 경고 유지).
독립 재생·포즈 검사는 양 구성 각 250항목, CreatorRobot 제품 회귀는 양 구성 각각
100체×12회·138,254검사·관리 스레드 오류 0건으로 통과했다. 제품 회귀에 4시점×5블렌드
비율의 기존 행렬 기준 대조, 실제 본 수·TRS 저장소 재사용, Step 경계·기본값 검사를 추가했다.
독립 검사는 쿼터니언 반대 부호·단위 길이, 비균등·0·음수 스케일, 포즈 복사·초기화도 다룬다.
Release DX12 화면 회귀는 13캡처·264검사 통과. 이전 §11 최종 화면과 대조하면
11캡처는 display.f32가 완전히 같고, blend0/half는 각 412,800색상 성분 중 4/12개만
최대 1/255 차이다. 변경 전후 모두 같은 CreatorRobot을 썼다.
원본은 `Build/Obj/Phase13S2b/`의 `Release-build.log`·`Debug-build.log`,
`Product-Release`·`Product-Debug`, `Visual-Release`, `visual-before-after.json`이다.
Player 빌드·실행과 Vulkan 화면 검증은 이번 슬라이스에서 수행하지 않았다.
**두 번째 슬라이스의 구현·정확성 검증 완료이며, S2′ 전체는 계속 진행 중이다.**

### 단일 클립 비용 확인

§11 최종 S2a 기선과 같은 CreatorRobot Walk·8워커·30예열+120표본을 새 Release
프로세스 3회씩 비교했다(인원별 360표본 nearest-rank 중앙값). 컴파일·링크가 끝난 후
계측했으며 CPU 범위·프로파일러 녹화 미요청·GPU/프레젠테이션 제외 조건은 §11과 같다.

| 인원 | 워커 합산 S2a→S2b ms | 전체 CPU 경로 S2a→S2b ms |
|---:|---:|---:|
| 10 | 0.0907 → 0.0964 | 0.3808 → 0.3114 |
| 50 | 0.4272 → 0.4578 | 2.1337 → 2.0379 |
| 100 | 0.8848 → 0.9602 | 10.0087 → 8.5808 |

워커 합산 중앙값은 약 6.3/7.2/8.5% 증가했다. **단일 클립 평가 비용이 감소한 결과가
아니다.** 전체 CPU 경로 감소에는 대기·팔레트 비용 변동도 섞여 있으며 TRS 변경의
성능 향상으로 귀속하지 않는다. 이 시나리오는 전이를 실행하지 않으므로 행렬 분해
제거의 블렌딩 속도 향상 역시 미측정이다. 다음 키 탐색·마스크·저장소 작업은 이
비용과 실제 전이 조건을 함께 비교해야 한다. 512본 팔레트 복사량은 여전히 100체당
12.5MiB/표본이다. [측정 요약](../analysis/AnimationTrsBaseline20260924.json)에 실행별
수치·주요 소스 및 런타임 DLL 해시를 기록했고 원본은 `Build/Obj/Phase13S2b/Baseline-Release`다.

## 13. S2′ 세 번째 슬라이스 — 인스턴스 키 커서 (2026-09-24)

- `Animation::TrackKeyCursor`는 위치·회전·스케일의 직전 보간 구간 인덱스를 각각
  저장한다. `ClipSamplingCursor`는 실제 본 수만큼 이 값을 보유하며 클립 변경 때
  초기화한다. 공유 generation과 키 배열에는 가변 상태를 추가하지 않는다.
- Animator의 평가 슬롯마다 현재·다음 클립용 커서를 분리했다. 컨트롤러 없는 재생은
  첫 슬롯을 사용한다. 컨트롤러 순서·클립·재생 시각이 달라져도 커서는 힌트일 뿐이며
  매번 실제 키 시각으로 구간을 검증한다. 세대 재바인딩 때 모든 슬롯을 비우고,
  실제 평가에서만 준비한다. 같은 본 수의 재생·재바인딩에서는 내부 용량을 재사용한다.
- `FindKeyInterval`은 현재 구간과 바로 앞뒤 구간을 일정 횟수로 확인한다.
  그 밖의 시점 이동·루프·긴 프레임 간격은 이진 탐색으로 처리한다. 키 배열은 자산
  게시 전 유한·오름차순으로 검증되며, 탐색 비용은 평상시 O(1), 큰 이동은 O(log 키 수)다.
  재생 방향 플래그나 부동소수점 시각의 단조 증가 가정에 의존하지 않는다.
- Linear의 정확한 키 시각은 앞 구간, Step은 해당 키 값, 범위 밖 시각은 처음/마지막
  구간이라는 기존 규칙을 유지한다. 보간 산술·X축 균등 스케일·이벤트 시간 전진도 유지한다.
  기존 선형 탐색 샘플러는 독립 대조 경로로 남긴다. `EvaluateGenerationPose`의 진단
  샘플은 별도 커서를 사용하여 실제 재생 중인 현재/다음 클립의 탐색 상태를 바꾸지 않는다.

**범위:** dense mask·masked/additive 커널·비균등 스케일 클립 지원·배속 핸들·나머지
프레임 할당은 후속이다. 컨트롤러 행렬 staging·512본 팔레트·공용 enkiTS 실행기는
변경하지 않았다. 커서는 활성 클립/평가 슬롯별 저장소이며 모든 애니메이션 클립을
인스턴스마다 복제하는 캐시가 아니다.

**검증:** Editor Debug/Release 빌드 성공(기존 C4244·LNK4229 경고 유지).
독립 재생·포즈·구간 탐색 검사는 Debug/Release 각 7,219항목 통과.
8,192키에서 동일 구간·인접 구간과 큰 이동의 비교 횟수도 단정한다. 제품 검사는
합성 Linear/Step 트랙과 CreatorRobot의 모든 클립·키 경계에서 기존 선형 샘플의
행렬 바이트를 대조하고, 정지·역재생·시점 이동·슬롯 재배치·커서 재사용/무효화를 검증했다.
양 구성 모두 100체×12회·231,982검사·관리 스레드 오류 0건으로 통과했다.
Release DX12 화면 회귀는 13캡처·264검사 통과이며, §12의 13개 `display.f32`와
모두 바이트 단위로 동일했다. 원본은 `Build/Obj/Phase13S2c/`의 빌드 로그,
`Product-Release`·`Product-Debug`, `Visual-Release`, `visual-before-after.json`이다.
테스트 모델은 CreatorRobot을 유지했고 Player·Vulkan은 이번에 실행하지 않았다.

### 비용 확인과 미해결 범위

컴파일·링크가 끝난 후 이전 §12 S2b와 같은 Walk·8워커·30예열+120표본을 새 Release
프로세스 3회로 계측했다. 인원별 360표본의 nearest-rank 중앙값이며 CPU 범위·녹화
미요청·GPU/프레젠테이션 제외 조건은 이전과 같다.

| 인원 | 워커 합산 S2b→S2c ms | 전체 CPU 경로 S2b→S2c ms |
|---:|---:|---:|
| 10 | 0.0964 → 0.1096 | 0.3114 → 0.3495 |
| 50 | 0.4578 → 0.5214 | 2.0379 → 2.1413 |
| 100 | 0.9602 → 1.0605 | 8.5808 → 10.0797 |

**제품 성능 향상으로 판정하지 않는다.** 워커 합산 중앙값은 약 13.7/13.9/10.4% 증가했고,
전체 경로의 대기·팔레트 비용도 함께 변했다. 이 차이의 원인은 분리하지 못했다.
원본 Walk의 53개 회전 채널은 각각 29키임을 확인했다. 이를 참고한 별도 **합성**
53트랙·29회전키 샘플러 비교에서는, 같은 프로세스에서 선형/커서 순서를 바꿔 7회씩
실행했을 때 중앙값이 93.9508→69.3062ms(약 26.2% 감소)였다. 이 실험은 제품 워커·
팔레트 경로를 포함하지 않으므로 제품 측정의 비용 증가를 설명하거나 대체하지 않는다.
구간 비교 횟수와 샘플러 수준의 개선은 확인했지만 **제품 시간 개선은 후속 검증·최적화
항목으로 남긴다.** TRS/키 커서의 인스턴스 저장소도 추가된 상태이며, 고정 512본 팔레트
복사량은 여전히 100체당 12.5MiB/표본이다.

실행별 수치·소스/런타임 DLL 해시·격리 비교 조건은
[측정 요약](../analysis/AnimationCursorBaseline20260924.json)에 기록했다.
제품 원본은 `Build/Obj/Phase13S2c/Baseline-Release`, 격리 비교 소스·결과는 같은
폴더의 상위 디렉터리에 있는 `sampler-bench.cpp`·`sampler-bench.log`다.
**세 번째 슬라이스의 구현·정확성 검증 완료이며 S2′ 전체는 계속 진행 중이다.**

## 14. S2′ 네 번째 슬라이스 — dense layer mask·짧은 채널 분기 (2026-09-24)

### 제품 워커 비용 분리

§13의 제품 시간 증가를 확인하기 위해 같은 Walk·8워커·30예열+120표본을
새 Release 프로세스 3회씩 다시 측정했다. 먼저 커서 코드 상태에서 제품
`AnimationJob`의 샘플러 호출만 일시적으로 기존 선형 탐색으로 바꾸어 비교했고,
실험 후 그 임시 변경은 복원했다. 합성 샘플러에서는 커서가 빨랐지만 실제
100체 워커 합산은 커서 1.0641ms, 임시 선형 호출 0.9775ms였다. 따라서
이 모델의 제품 경로에서는 커서 샘플링 비용이 이전 회귀의 주요 부분이었다.

비어 있거나 키가 하나인 채널에서 매번 커서에 0을 쓰던 동작을 없앴다.
키가 2개 이상인 채널의 구간 검증·시점 이동 규칙은 그대로다. 이 수정만
들어간 중간 빌드는 100체 0.9794ms였으나, 이후 dense mask 코드를 포함한
최종 컴파일에서는 1.0598ms였다. 코드 생성·인스턴스 배치가 달라졌으므로
빈 채널 쓰기 제거 하나의 지속적인 제품 성능 효과로 해석하지 않는다.

현재 실제 재생은 한 트랙의 위치·회전·스케일 채널이 모두 32키 이하면 기존
선형 샘플러를 사용하고, 어느 하나라도 길면 인스턴스 커서를 사용한다.
CreatorRobot `Walk`의 최장 채널은 29키다. 32키 경계는 이 제품 표본에
맞춘 시작값이며 다른 클립에서 최적인지는 아직 검증하지 않았다.

| 인원 | 커서 이전 상태 → 최종 분기 워커 합산 p50 | 커서 이전 상태 → 최종 분기 전체 CPU p50 |
|---:|---:|---:|
| 10 | 0.1105 → 0.0986ms | 0.3790 → 0.3037ms |
| 50 | 0.5243 → 0.4712ms | 2.1790 → 1.9004ms |
| 100 | 1.0641 → 0.9954ms | 9.5318 → 8.5591ms |

각 값은 인원별 360표본의 nearest-rank 중앙값이다. 100체 워커 합산은
§13 상태보다 약 6.5% 낮지만 S2b 0.9602ms보다 약 3.7% 높다.
전체 CPU 경로에는 스케줄러 대기·Scene 게시·고정 512본 팔레트 복사가 포함되고
변동성이 크다. 단일 Walk의 개선으로 다른 클립·레이어 성능을 일반화하지 않는다.

### dense mask 범위

다중 컨트롤러 합성은 슬롯마다 **본 인덱스 → 0/1 float weight** 배열을
준비하고, 본 순회 안에서는 그 값을 직접 읽는다. `AvatarMask`의 저작 필드와
`BoneMask` 이름·활성 상태는 Editor가 직접 수정하므로, 슬롯별 스냅샷을
매 평가에서 정확히 비교해 변경 시에만 배열을 다시 굽는다. 컨트롤러 슬롯 순서,
마스크 교체, 세대 재바인딩도 기존 가중치를 잘못 재사용하지 않는다.
이름 마스크의 첫 일치 항목·첫 null에서 탐색 중단이라는 현재 규칙, 휴머노이드
상·하체 플래그, 마스크 없는 레이어, 마지막 활성 레이어 우선순위를 유지한다.
세대 재바인딩 때 마스크 캐시를 비운다. 평상시 이름 비교는 본 수×마스크 수의
레이어 내부 조회 대신 마스크 스냅샷 길이에만 비례한다.

이것은 **dense 조회의 첫 슬라이스**다. 휴머노이드 저작은 아직 7개 `BoneRegion`을
사용하며 비연속 가중치와 `BlendMasked` 커널은 구현하지 않았다. 따라서
§2.2②의 `BoneRegion` 은퇴와 임의 weight 저작 완료로 표시하지 않는다.
다중 레이어의 실측 성능도 이번 단일 Walk 기선으로 주장하지 않는다.

**검증:** Editor Release/Debug 빌드 통과(기존 C4244·LNK4229 경고).
독립 재생 검사 각 7,219항목, CreatorRobot 제품 100체×12회 각 231,987검사·
관리 스레드 오류 0건, Release DX12 화면 13캡처·264검사 통과.
최종 13개 `display.f32`는 §13과 바이트 단위로 동일하다. 제품 검사는
휴머노이드 플래그의 직접 편집, 이름 마스크의 활성/이름 변경, 첫 null 항목,
세대 해제 시 캐시 무효화를 추가했다. 테스트 모델은 변경하지 않았다.
Player·Vulkan은 이번에 실행하지 않았다.

숫자·원본 실행·각 빌드의 소스/런타임 해시는
[측정 요약](../analysis/AnimationMaskBaseline20260924.json)과
`Build/Obj/Phase13S2d/`에 있다. **네 번째 슬라이스 구현·정확성 검증 완료,
S2′ 전체는 진행 중이다.** 남은 S2′ 작업은 비연속 masked/additive 블렌드,
비균등 스케일 클립, 배속 파라미터 핸들, Animator 목록·컨트롤러 캡처·이벤트
정렬 등의 프레임 할당 축소다.

## 15. S2′ 마무리 — 가중 레이어·스케일·배속 핸들·임시 저장소 (2026-09-24)

**구현 범위:** `LocalPose`의 TRS 값을 컨트롤러 레이어 staging과 Animator 합성에
사용한다. 본별 `BlendMasked`는 0/1 끝점을 그대로 보존하고 중간 가중치는
translation/scale 선형 보간·rotation 구면 보간으로 합성한다. 이전 합성값을
다음 프레임의 기저로 다시 쓰지 않아 0.5 마스크가 프레임마다 드리프트하지 않는다.
`MakeAdditive`/`ApplyAdditive`는 참조 포즈 대비 본별 TRS 델타 커널로 제공하며,
태스크 레시피에 연결하는 작업은 S3.5다. 스케일 샘플러는 3축 저작값을 모두
사용하고, Step 경계·역방향 탐색·0/음수 스케일을 독립 검사했다.

새 AvatarMask는 이름별 0~1 가중치로 저작한다. 옛 `isHumanoid`/`BoneRegion`
저장분은 읽기 호환으로만 유지한다. Editor의 전환 버튼은 모델 generation에
결합된 7영역 결과를 이름별 가중치로 옮기며, 본 이름이 맞지 않는 불완전한
트리에는 일부만 반영하지 않는다. 새 저작 UI에서는 7영역 모드를 선택할 수
없다. 레이어 평가의 본 순회는 베이크된 float 배열만 읽는다.

배속 파라미터는 상태별 index+version+name 핸들로 접근한다. 값 변경은 인덱스로
읽고, 추가·삭제·이름 변경 시 다시 해석한다. Animator 목록·워커 계측·이벤트
정렬·이벤트 수신 ScriptComponent 목록은 예열된 저장소를 재사용한다. 진단용
포즈 샘플은 실제 재생의 키 커서·합성 포즈·소켓 staging을 바꾸지 않는다.
`job_group` 제출 메모리는 S6, 고정 512본 팔레트 발행은 S3.5 범위다.

**검증:** Editor Debug/Release 최종 빌드, 독립 재생 각 7,231검사,
CreatorRobot 제품 100체×12회 각 233,196검사·관리 스레드 오류 0건,
Release DX12 13캡처·264검사 통과. 제품 검사는 0.5 마스크의 반복 평가,
구형 마스크의 이름별 전환 전후 포즈, 배속 핸들의 삭제·재생성, 3축 스케일,
진단 샘플의 상태 보존을 포함한다. 이전 S2d와 `display.f32` 바이트 동일
캡처는 숨김 장면 1/13개다. 다른 캡처의 변경은 최대 68개 float 값,
절댓값 최대 0.0156863이며 화면 게이트 허용 범위 안이다. 원인은 별도
A/B로 분리하지 않았으므로 바이트 파리티를 주장하지 않는다.

같은 CreatorRobot Walk·8워커·인원별 3회×(30예열+120표본)의 최종 Release
재측정에서 워커 합산 중앙값은 10/50/100체 **0.1072/0.5295/1.0723ms**,
전체 CPU 경로는 **0.4793/2.5263/11.1474ms**다. S2d의 워커 합산
0.0986/0.4712/0.9954ms보다 높다. 다만 100체 전체 CPU 실행별 중앙값이
9.451→11.739→12.370ms로 상승해 시스템 상태 변화와 코드 비용을 이 표본만으로
분리할 수 없다. 단일 Walk로 가중 다중 레이어의 성능을 대표하지 않는다.
CreatorRobot에는 스케일 키가 없어 비균등 스케일은 제품 게이트의 합성 3축
트랙으로 확인했다. 테스트 모델은 바뀌지 않았고 Player·Vulkan은 실행하지
않았다. 측정 조건·원본 로그·해시는
[S2′ 완료 기록](../analysis/AnimationS2Completion20260924.json)에 있다.
**S2′ 완료. 다음 구현 단계는 S3′ 인스턴스 저장소 이관과 S3.5 레시피다.**

## 16. S3′ 진행 — 고정 포즈 배열 제거와 인스턴스 핸들 (2026-09-24)

`AnimationController::m_LocalTransforms[512]`는 S2′의 `m_layerPose`와 중복이어서
제거했다. 레이어 합성·제품 게이트는 `m_layerPose`를 정본으로 읽는다.
`Animator`의 로컬/최종 512본 배열도 제거하고 `AnimatorSystem` 소유의
`AnimInstanceHandle {slot,generation}`으로 교체했다. 시스템은 유효 인스턴스를
조밀한 인덱스로 관리하고 슬롯 재사용 시 generation을 올린다. 인스턴스 객체는
포인터 안정성을 위해 별도 할당한다. 현재/이전 SoA 포즈는 실제 본 수,
로컬/최종 스키닝 행렬은 현재 512 상한으로 크기를 맞춘다. 진단 표본은 라이브
포즈·팔레트를 저장/복구한다. `ProxyCommand`는 기존 렌더 계약인 512행렬 버퍼를
항등 행렬로 채운 뒤 유효 팔레트만 복사한다. 프레임 아레나·가변 전달은 S3.5다.

Debug/Release Editor 빌드와 독립 재생 각 7,231검사, 제품 100체×12회
각 233,300검사, Release DX12 시각 13캡처/264검사가 통과했다. 제품 게이트에는
핸들 ABA 방지와 실본수 및 512본 초과 포즈 버퍼 검사가 포함됐다.
이 표본은 성능 비교가 아니며 Player·Vulkan은 검증하지 않았다.

**S3′는 아직 완료가 아니다.** 이 시점에는 Controller의 시간/진행률/레이어
포즈와 Animator의 클립 선택·블렌드 제어 값이 컴포넌트 소유였다.
Controller 재생값은 §18, Animator의 비직렬화 제어값은 §19에서 이관했다.
`std::vector<std::unique_ptr<AnimInstance>>`는 인덱스는 조밀하지만 데이터 자체가
연속 배열은 아니다. Controller의 가변 재생 상태는 후속 §18에서 인스턴스로
옮겼다. 잡 실행 중 등록/해지 경계와 DDOL 이동을 제품 경로에서 확인하고
조밀 배치를 재검토해야 한다.

## 17. S3′ 후속 — Animator 재생 시간·커서·평가 저장소 이관 (2026-09-24)

`Animator`의 단일 클립 재생 시간, 현재/다음 클립 키 커서, 전역 행렬·샘플 포즈,
레이어 트랙/마스크 캐시, 이벤트 순서/스크립트 임시 저장소, 본 리전 파생 캐시를
`AnimInstance`로 옮겼다. 클립 generation 재바인딩 시 이 저장소를 같은 경계에서
비우고, 다시 예열된 평가에서는 용량을 재사용한다. `AnimationController`의 전이
시간·진행률·레이어 포즈는 이 시점에는 Controller 소유였고 §18에서 이관했다.

Debug/Release Editor 빌드, 제품 CreatorRobot 100체×12회 각 233,300검사와
Release DX12 13캡처/264검사가 통과했다. 제품 게이트는 조밀 슬롯 이동 후
재생 시간 유지, 슬롯 재사용 때 상태 초기화, 커서/평가 임시 저장소 재사용 및
진단 표본의 라이브 커서 보존을 검사한다. 독립 재생 게이트는 이번 슬라이스에서
변경하지 않은 샘플러만 검증하므로 재실행하지 않았다. 성능 비교와
DDOL·Player·Vulkan 실행은 아직 하지 않았다.

## 18. S3′ 후속 — Controller 재생 상태의 인스턴스 소유 (2026-09-24)

`AnimationController`의 레이어 `LocalPose`, 현재/다음 클립 시간, 현재/이전
진행률, 전이 블렌드 시간을 `AnimInstance::ControllerPlayback`으로 옮겼다.
Controller는 고유 ID와 Animator 연결을 통해 인스턴스의 재생 상태를
찾는다. 상태 슬롯은 별도 할당해 목록 재정렬·슬롯 증설 중에도 참조 주소가
안정적이다. Controller가 목록에서 잠시 빠져도 `shared_ptr`가 살아 있으면
상태를 유지하고, 실제 소멸 후 다음 포즈 틱에서 약한 참조를 확인해 슬롯을
회수한다. 본 순회는 예열된 레이어 상태 포인터를 사용하며, 모델 generation
해제 시 이 임시 포인터를 비운다.

Editor Release/Debug 빌드와 CreatorRobot 제품 100체×12회 각 233,304검사·
관리 스레드 오류 0건, Release DX12 화면 13캡처·264검사가 통과했다. 제품
검사는 재정렬 뒤 상태 주소/시간 유지, 일시 분리·재부착, 소멸한 Controller의
상태만 회수하는 경로를 추가했다. 독립 샘플러 검사는 변경 범위 밖이라
재실행하지 않았다. 이번 슬라이스는 상태 소유권 이관이며 성능 비교는 하지
않았다. 원본 실행은 `Build/Obj/Phase13S3d/`에 있다.

**S3′ 전체는 진행 중이다.** Controller의 FSM 선택·전이 플래그와 Animator의
직렬화된 클립 인덱스는 이 시점에는 컴포넌트에 있고, 비직렬화 제어값은 §19,
실행 선택값은 §20에서 이관했다. 인스턴스 저장소의 데이터는 실제 연속 배열이
아니다. 잡 실행 중
등록/해지 및 DDOL 이동, Player·Vulkan
제품 경로는 별도 검증이 필요하다.

## 19. S3′ 후속 — Animator 비직렬화 제어 상태 이관 (2026-09-24)

`Animator`의 블렌드 계수, 다음 클립 인덱스, 블렌드 활성 여부, 일시 정지
잔여 시간과 누적 시간을 `AnimInstance::AnimatorPlaybackControl`로 옮겼다.
Controller의 전이 갱신, AnimationJob의 포즈 평가와 일시 정지/재개, 시각
검사 진입점이 모두 이 인스턴스 값을 사용한다. 본 순회에서는 블렌드 계수를
한 번 읽어 재사용한다. 인스턴스가 재활용되면 다섯 값은 기본값으로 다시
시작한다.

`m_AnimIndex`와 `m_AnimIndexChosen`은 씬 리플렉션으로 저장되는 필드다.
이번에는 저장 형식과 로드 경로를 바꾸지 않았으므로 컴포넌트에 남겼다.
이 둘을 옮기려면 저작 선택과 런타임 선택을 분리하고 기존 씬을 다시 읽는
호환 게이트가 필요하다.

Editor Release/Debug 빌드, CreatorRobot 제품 100체×12회 각 233,306검사·
관리 스레드 오류 0건, Release DX12 화면 13캡처·264검사가 통과했다. 제품
검사는 조밀 슬롯 이동 중 제어값 보존, 재사용 슬롯 초기화, 일시 정지 시간
누적과 재개 시 1회 적용, 전이의 다음 클립/블렌드 상태 게시를 확인한다.
테스트 모델은 변경하지 않았다. 성능 비교와 DDOL·Player·Vulkan 실행은
하지 않았다. 원본 실행은 `Build/Obj/Phase13S3e/`에 있다.

**S3′ 전체는 진행 중이다.** 직렬화 클립 인덱스의 저작/실행 분리는 §20에서
진행했다. 남은 구현은 Controller FSM·전이 상태 이관과 실제 연속 데이터
배치 검토다.
잡 실행 중 등록/해지 및 DDOL 이동, Player·Vulkan 제품 검증도 남았다.

## 20. S3′ 후속 — 직렬화 클립 인덱스와 실행 선택값 분리 (2026-09-24)

현재 선택 클립의 **실행 정본**을 `AnimInstance::selectedClipIndex`로 옮겼다.
`Animator::m_AnimIndex`는 저작 선택값으로 남기고, `m_AnimIndexChosen`은 기존
씬 YAML 키를 유지하는 리플렉션 호환 필드로만 남겼다. 직렬화 직전에는 실행
선택값을 호환 필드에 쓰고, 역직렬화 후에는 그 필드를 인스턴스에 복원한다.
리플렉션·Prefab 편집이 호환 필드를 바꿀 때도 `OnPropertyChanged`가 인스턴스에
반영한다. `SetAnimation`, Controller 전이, AnimationJob, `animator.status`는
실행 정본을 사용한다. 진단용 포즈 샘플은 실행 선택값을 저장/복원한다.

Editor Release/Debug 빌드, CreatorRobot 제품 100체×12회 각 234,512검사·
관리 스레드 오류 0건, Release DX12 화면 13캡처·264검사가 통과했다. 제품
검사는 기존 두 YAML 키의 값·왕복, 리플렉션 편집, 저작 선택에서 실행 선택으로
갱신, 진단 샘플 뒤 선택값 복원을 포함한다. 추가로 기존 `Test1.creator`를
Release 에디터에서 열고 별도 파일로 저장·재개방했으며 명령은 모두 성공하고
두 키가 유지됐다. 그 씬의 `animator.status`는 활성 Animator 0개를 반환했으므로
이 확인은 **저장 호환 범위**이고, 재생 경로 증명은 CreatorRobot 게이트에
한정한다. 테스트 모델은 변경하지 않았고 성능 비교·DDOL·Player·Vulkan
실행은 하지 않았다. 원본 실행은 `Build/Obj/Phase13S3f/`에 있다.

**S3′ 전체는 진행 중이다.** Controller FSM·전이 상태의 실행 소유권,
실제 연속 데이터 배치, 잡 실행 중 등록/해지 및 DDOL 이동 검증이 남았다.

## 21. S3′ 후속 — Controller FSM·전이 실행 상태 이관 (2026-09-24)

Controller의 현재/다음 상태, 활성 전이, 현재/다음 클립 인덱스, 전이·블렌드
플래그와 애니메이션 종료 플래그를 `AnimInstance::ControllerPlayback`으로 옮겼다.
전이 평가와 포즈 평가는 같은 Controller ID의 슬롯을 읽는다. 레이어 순서를
바꾸거나 잠시 분리해도 슬롯의 FSM 상태는 Controller 정체성을 따라 유지되고,
실제 소멸 후 슬롯을 회수한다. 상태 또는 전이를 삭제할 때는 슬롯의 보류
포인터와 블렌드 제어를 함께 정리한다.

`m_curState`는 기존 씬 YAML 키를 위한 리플렉션 호환 필드로 남겼다. 상태
선택과 전이 완료 시 실행 상태를 반영하고, 직렬화 직전에도 슬롯의 현재
상태를 이 필드에 복사한다. 기존 역직렬화는 상태 이름으로 `SetCurState`를
호출하므로 새 슬롯에 같은 상태를 복원한다.

Editor Debug/Release 빌드와 CreatorRobot 제품 100체×12회 각 234,516검사,
관리 스레드 오류 0건, Release DX12 화면 13캡처/264검사가 통과했다. 제품
검사는 Controller별 FSM 격리, 레이어 재정렬·소멸 후 상태 유지, 실제 Animator
직렬화의 `m_curState` 키와 실행 상태 동기화, 진행 중 전이의 보류 상태와
전이 삭제 시 참조 정리를 확인한다. 테스트 모델은 변경하지 않았고 성능
비교는 하지 않았다. 원본 실행은 `Build/Obj/Phase13S3g/`에 있다.

**S3′ 전체는 진행 중이다.** 시스템 인스턴스 저장소는 조밀 인덱스를 쓰지만
데이터는 `unique_ptr`로 분산 할당되어 실제 연속 배열이 아니다. 잡 실행 중
등록/해지와 DDOL 이동, Player·Vulkan 제품 경로도 아직 검증하지 않았다.

## 22. S3′ 후속 — 주소 고정 연속 페이지 저장소 (2026-09-24)

`AnimInstance`의 개별 힙 할당을 64개 레코드씩 연속 할당하는 시스템 소유
페이지로 바꿨다. 살아 있는 레코드의 조밀 목록은 포인터만 swap-and-pop하고,
빈 레코드는 페이지 안에서 초기화해 다시 사용한다. 따라서 페이지 증설이나
다른 Animator의 소멸로 Controller·워커가 참조하는 생존 레코드 주소가
바뀌지 않는다. 세대 핸들의 ABA 방지와 슬롯 재사용 기본값은 유지했다.

단일 `std::vector<AnimInstance>`는 증설 시 살아 있는 레코드를 옮긴다.
현재 `GetInstance()`가 반환한 참조를 Controller와 작업이 사용하므로 주소
이동을 허용할 수 없다. 페이지는 **각 64개 내부가 연속**이고 페이지 간은
연속이 아니다. 조밀 목록에도 포인터 간접 참조가 남고 Controller 슬롯은
아직 개별 할당이다. 이 저장소 변경의
목표는 개별 할당 제거와 주소 안정성을 함께 만족하는 것이며 성능 개선이나
전 인스턴스의 단일 연속 배열은 주장하지 않는다.
페이지는 시스템 수명 동안 유지되므로 메모리 사용량은 동시 Animator 수의
최고치에 맞춰 늘어난다. 축소 정책은 별도 계측 뒤 결정한다.

CreatorRobot 제품 회귀에는 100체 생성으로 페이지를 증설한 뒤 첫 레코드
주소가 유지되는지, 같은 페이지의 연속 주소가 실제로 나타나는지, 다른
인스턴스를 제거하고 슬롯을 재사용해도 생존자의 주소·재생값과 세대가
보존되는지를 추가했다. Editor Debug/Release 빌드와 CreatorRobot 제품
100체×12회 각 234,517검사·관리 스레드 오류 0건, Release DX12 화면
13캡처/264검사가 통과했다. 테스트 모델은 변경하지 않았고 성능 비교는
하지 않았다. 원본 실행은 `Build/Obj/Phase13S3h/`에 있다.

**S3′ 전체는 진행 중이다.** Controller 슬롯의 배치와 잡 실행 중
등록/해지·DDOL 이동, Player·Vulkan 제품 경로의 검증은 아직 남았다.

## 23. S3′ 후속 — Controller 재생 슬롯의 연속 페이지 (2026-09-24)

`ControllerPlayback`을 담는 `AnimInstance::ControllerSlot`의 개별 할당을
4개씩 연속인 페이지로 바꿨다. 활성 Controller의 조밀 목록은 슬롯 포인터만
재정렬하므로 레이어 순서 변경과 페이지 증설 중에도 전이 상태의 주소가
유지된다. 만료된 약참조는 포즈 틱에서 회수하고, 슬롯의 포즈·시간·FSM 값을
기본값으로 초기화한 뒤 다음 Controller에 재사용한다. ID 조회 인덱스와
Controller의 실제 소멸을 구분하는 기존 계약은 유지했다.

Animator 인스턴스와 Controller 슬롯은 모두 **각 페이지 내부만 연속**이다.
Controller 페이지는 해당 Animator 인스턴스가 살아 있는 동안 최고 동시
레이어 수만큼 유지된다. 전체 레코드의 단일 연속 배열이나 성능 개선은
주장하지 않는다.

제품 회귀에는 한 Animator에서 10개 Controller 슬롯을 만들고 페이지를
확장한 뒤 주소·재생값이 유지되는지, 중간 슬롯의 만료 회수와 조밀 인덱스
수정, 같은 레코드의 기본값 재사용, 모든 약참조 만료 뒤 조회 인덱스가
비는지를 추가했다. Editor Debug/Release 빌드, CreatorRobot 제품
100체×12회 각 234,521검사·관리 스레드 오류 0건, Release DX12 화면
13캡처/264검사가 통과했다. 테스트 모델은 변경하지 않았고 성능 비교는
하지 않았다. 원본 실행은 `Build/Obj/Phase13S3i/`에 있다.

**S3′ 전체는 진행 중이다.** 잡 실행 중 등록/해지와 DDOL 이동,
Player·Vulkan 제품 경로 검증이 남았다.

## 24. S3′ 후속 — DDOL Animator의 잡 등록 수명 (2026-09-24)

`AnimationJob`은 씬 언로드 이벤트에서 Animator 등록부를 비운다. 기존
`Animator::OnInitialized` 등록은 새 씬에 다시 붙은 DDOL Animator에 대해
재실행되지 않으므로, 객체와 인스턴스가 살아 있어도 다음 씬의 포즈 잡에서
빠질 수 있었다. `AnimationJob` 등록·해지를 `AnimatorSystem`과 같은
`OnAddedToScene`/`OnRemovingFromScene` 경계로 옮겼다. 씬 이동 때 기존
컴포넌트·`AnimInstance`·Controller 재생 슬롯은 유지하고 잡 등록만 다시 만든다.

CreatorRobot 제품 회귀에서 Animator 하나를 DDOL로 지정해 실제 씬 전환을
수행하고, 동일 컴포넌트·인스턴스·재생 슬롯과 시간값, 두 등록부의 재등록,
전환 뒤 포즈 발행, 최종 파괴 뒤 해지를 확인했다. Editor Debug/Release
실행 파일 빌드와 제품 100체×12회 각 234,528검사·관리 스레드 오류 0건이
통과했다. Release DX12 화면은 13캡처/264검사가 통과했다. 첫 두 실행은
초기 라이브 프레임이 60초 제한을 넘겨 전체 게이트가 실패했지만 모든
캡처·픽셀 검사는 통과했다. 대기 제한을 120초로 늘린 독립 실행에서는
초기 프레임이 63.4초에 완료되어 전체 게이트도 통과했다. 테스트 모델과
성능 비교는 변경하지 않았다. 실행 결과는 `Build/Obj/Phase13S3j/`에 있다.

**S3′ 전체는 진행 중이다.** 이 검사는 씬 전환 경계의 등록·해지를 증명한다.
잡 실행 중 다른 스레드에서 등록·해지하거나 컴포넌트를 파괴하는 경우의
안전성, Player·Vulkan 제품 경로는 아직 검증하지 않았다.

## 25. S3′ 후속 — 등록부의 게임 스레드 소유 계약 (2026-09-24)

실제 호출 경로에서 Animator 등록·해지는 씬의 편입·이탈 훅으로만 오고,
`SceneManager::GameLogic`의 `AnimationJob::Update`는 모든 평가 잡을
`completion.wait()`로 회수한 뒤 포즈를 게시하고 반환한다. 실제 컴포넌트
해제는 같은 게임 스레드의 뒤쪽 `Scene::EndFramePass`에서 일어난다.
따라서 이 경로에서 잡 실행 중 등록부 변경이나 해제는 겹치지 않는다.
`m_animatorMutex`만으로 raw `Animator*`의 수명을 보장한다고 보지 않는다.

이 규칙을 `AnimationJob`과 `AnimatorSystem`의 등록·해지·Update 입구에
소유 스레드 검사로 명시했다. 다른 스레드 호출은 등록부를 바꾸기 전에
`std::logic_error`로 거부한다. 제품 회귀는 두 등록부의 직접 호출과
`Animator::OnAddedToScene`/`OnRemovingFromScene` 훅의 잘못된 스레드
호출을 시험하고, 두 등록부가 원래 상태를 유지하는지 확인한다.

원격 Audio Phase 22 커밋 `2530ca03`을 fast-forward로 반영하고,
겹친 프로젝트 설정·대시보드의 로컬 변경을 보존했다. 이 상태에서 Editor
Debug/Release 실행 파일 빌드와 CreatorRobot 100체×12회 각 234,529검사,
Release DX12 화면 13캡처/264검사가 통과했다. Player Release 빌드와
기존 cooked fixture를 이용한 재로드·정상 종료 게이트도 통과했다.
Player 게이트는 패키지와 최신 바이너리를 합친 생명주기 검사이며
CreatorRobot 애니메이션의 Player 제품 화면을 증명하지 않는다.
성능 비교는 하지 않았다. 실행 결과는 `Build/Obj/Phase13S3k/`에 있다.

이 시점에는 CreatorRobot Player 제품 경로가 남아 §26에서 검증했다.
지원하지 않는 다른 스레드의 씬·컴포넌트 직접 파괴까지 안전하다는 주장은
하지 않는다. Vulkan 제품 화면은 후속 통합 검증 범위이고, 팔레트 프레임
저장소·태스크 레시피는 별도 S3.5 범위다.

## 26. S3′ 후속 — CreatorRobot Player 제품 경로 (2026-09-24)

격리된 `AnimationPlayer.creator`에 CreatorRobot의 Walk, 카메라, 조명을
저작했다. 고정된 엔진 배포본으로 모델 20개의 세대 캐시를 다시 만든 뒤 Release
Player 패키지를 쿠킹·검증했다. 기존 프로젝트의 다른 씬·프리팹과 구형 오디오
스탬프는 이 검사의 입력에서 제외했다. 재현 절차는
`Tools/regression/build-animation-player-fixture.ps1`와
`Tools/regression/verify-animation-player.ps1`에 고정했다.

제품 실행 중 드러난 쿠킹 경계도 수정했다. Player의 에셋 identity epoch 헤더가
CEDO를 저작 YAML로 읽지 않게 하고, 모델의 canonical `sidecar.meta`만 패키지에
포함한다. 쿠커가 내보내는 모델 세대 레코드와 sidecar는 CEDO로 변환하고,
변환된 sidecar의 SHA-256을 레코드의 fingerprint에 반영한다. 런타임 리더는
저작 YAML과 CEDO를 각각 읽는다. BuildTool의 패키지 입력 검사와 AssetPacker의
필터를 같은 규칙으로 맞췄다.

Release 배포본·패키지 생성과 BuildTool 46검사가 통과했다. 독립 Player 스모크는
화면 게시 2회, 관리 스크립트 타입 1개, 저작 텍스트 파서 호출 0회, 모델 세대
게시 실패 0건, 정상 종료를 확인했다. 이어 같은 패키지의 명령 서비스에서
`CreatorRobot`의 `Walk`를 조회해 54본·스킨 메시 4개, 프레임 112→322,
시간 및 팔레트 digest 변화를 확인하고 정상 종료했다. 원본 근거는
`Build/Obj/Phase13S3l/FinalFixture2/`와
`Build/Obj/Phase13S3l/PlayerGateFinal3/verification.json`에 있다.
이 검사는 DX12 Player의 실제 씬 로드·렌더 프레임 게시·애니메이션 포즈 진행을
증명한다. 화면 픽셀별 포즈 비교나 성능 개선은 주장하지 않는다.

**S3′ 완료.** 시스템 소유 인스턴스, 재생 상태 이관, 등록 수명과 DX12
Editor/Player 제품 경로를 확인했다. Vulkan 제품·백엔드 교차 판정은 앞서 정한
후속 통합 검증에서 수행한다. 다음 구현 단계는 S3.5 팔레트 프레임 저장소·
태스크 레시피다. 다른 스레드의 씬·컴포넌트 직접 파괴는 지원 계약에 넣지 않았다.

## 27. S3.5 첫 슬라이스 — 배치 팔레트 저장소와 실제 본 수 (2026-09-24)

기존 `MeshUpdate`는 스킨 메시마다 512개 행렬의 별도 배열을 할당·복사하고
그리기에도 512본을 넘겼다. 이제 프록시 명령 큐가 배치별 팔레트 아레나를
소유하고, Animator 키와 실제 행렬 내용이 같은 포즈는 배치 안에서 한 번만
복사한다. 명령과 렌더 프록시는 아레나 참조 및 실제 본 수의 offset/count를
보유한다. 큐에서 배치를 떼어 낼 때 아레나를 밀봉해 포인터를 안정화하고,
프록시·보류 명령의 마지막 참조가 해제된 아레나만 다음 배치에 재사용한다.
빈 포즈의 기존 바인드 포즈 대체 경로는 512개 항등 행렬을 유지한다.

Debug/Release Editor와 Release Player 빌드, 두 구성의 CreatorRobot
제품 회귀 100체×12회 각 234,529검사·관리 스레드 오류 0건이 통과했다.
Release DX12 화면 회귀는
13캡처/264검사가 통과했다. 30프레임 예열 뒤 별도 baseline commandlet의
100체·54본·스킨 메시 400개 조건에서 표본마다 팔레트 복사 100회,
345,600바이트, 아레나 생성·용량 확장 이벤트 0회를 확인했다. 10체와
50체 조건도 동일한 복사 횟수·바이트·확장 검사를 통과했다. 기존 cooked
CreatorRobot 패키지에 최신 Player 바이너리를 덮어 실행한 게이트에서도
Walk 54본·스킨 메시 4개, 프레임 진행과 팔레트 digest 변화, 정상 종료를
확인했다. 최종 baseline은 관련 소스 16개의 해시를 기록한
`Build/Obj/Phase13S35a/PalettePoolFinal/baseline.json`에 있고, 나머지
원본 결과는 `Build/Obj/Phase13S35a/`에 있다. 이 표본은 명령
배치의 CPU 저장소 검사이며 제품 경로 전체의 힙 할당 0이나 성능 향상을
뜻하지 않는다. 성능 비교는 하지 않았다.

**S3.5는 진행 중이다.** 현재 명령은 아레나 수명 참조도 보유하므로
계획의 순수 `{offset,count}` 표현까지 이르지 않았고, GPU 패스는 여전히
자체 팔레트 저장소에 전치·복사한다. 단일 벌크 업로드와 3×4 행렬 표현,
`AnimTaskList`/Executor의 Update·Execute 분리와 도달성 실행, 워커별
포즈 버퍼 풀·steal-in-place 및 그 완료 기준 검증이 남았다. Vulkan 제품
검증은 앞서 결정한 후속 통합 게이트에서 수행한다.

## 28. S3.5 후속 — 평면 태스크 목록과 출력 도달성 실행 (2026-09-24)

`AnimInstance`가 재사용하는 `animation::task_list`를 추가했다. 태스크 레코드는
POD이며 컴포넌트·자산 포인터를 담지 않는다. 의존은 같은 목록의 앞선 인덱스만
허용한다. 출력에서 역방향으로 필요한 태스크를 표시하고 기록 순서대로 실행하므로
실행 시 위상 정렬과 재귀 탐색이 없다. 제품 회귀에는 도달하지 않는 샘플 태스크가
실행되지 않는 경우와 앞선 인덱스 규칙을 어긴 의존이 거부되는 경우를 넣었다.

현재 `AnimationJob`은 한 Animator 작업 안에서 `PreparePose`가 시간·재생 진행과
이벤트 큐잉을 처리하고 태스크를 기록한 뒤 `ExecutePose`가 도달 가능한 태스크의
포즈 계산을 수행한다. 비활성 Controller는 기록하지 않는다. 기존 Controller
순서를 의존 체인으로 보존하고, 다중 레이어 합성은 마지막 샘플 뒤에 놓는다.
`sample_pose`는 기존 샘플링·블렌드·FK를 한 번에 호출하는 과도기 태스크다.
따라서 아직 `SampleClip`/`Blend`/`BlendMasked`를 독립 태스크로 분해하거나,
모든 Animator의 Update와 Execute를 별도 병렬 구간으로 나누지는 않았다.

Debug/Release Editor와 Release Player 빌드, 두 구성의 CreatorRobot 제품
100체×12회 각 234,531검사·관리 스레드 오류 0건, Release DX12 화면
13캡처/264검사가 통과했다. 10/50/100체
각 120표본의 기준 측정에서도 팔레트 복사는 표본당 각각 10/50/100회,
예열 뒤 아레나 생성·용량 확장 이벤트는 0회였다. 기존 검증된 cooked 패키지에
새 Player 바이너리를 적용한 Walk 진행·팔레트 변화·정상 종료 게이트도 통과했다.
원본 결과는 `Build/Obj/Phase13S35b/`에 있다. 이 실행의 시간값으로 성능
개선을 주장하지 않는다.

**S3.5는 진행 중이다.** 독립 샘플·블렌드 태스크와 실제 비활성 상태머신 가지의
포즈 계산 0회 판정, 별도 Update/Execute 병렬 구간, 워커별 포즈 버퍼 풀과
steal-in-place, GPU 팔레트 벌크 업로드·3×4 표현 및 전체 무할당 판정이 남았다.
Vulkan 제품 검증은 후속 통합 게이트에서 수행한다.

## 29. S3.5 후속 — 샘플·블렌드·FK 태스크 분리 (2026-09-24)

과도기 `sample_pose` 태스크를 `sample_clip` 두 입력, `blend`, `materialize`
태스크로 나눴다. 현재·다음 클립은 서로 다른 재사용 `LocalPose` 버퍼에
샘플링하고, 블렌드는 현재 버퍼를 제자리 갱신한다. 다음 클립에만 있는 채널은
기존처럼 채택하지 않는다. `materialize`는 선택된 로컬 포즈에서 FK·팔레트·
소켓 값을 만들며, Controller별 결과를 모두 만든 뒤 기존 다중 레이어 합성을
수행한다. 앞선 인덱스 의존 두 개가 블렌드의 양쪽 입력을 가리킨다. 제품
회귀에서는 두 입력이 기록 순서로 실행되고 출력과 무관한 샘플은 실행되지
않는 태스크 목록도 확인한다.

Debug/Release Editor와 Release Player 빌드, 두 구성의 CreatorRobot 제품
100체×12회 각 234,531검사·관리 스레드 오류 0건이 통과했다. Release DX12 화면 회귀는 13캡처/264검사가
통과했고, 이전 §28 실행의 원본 float 캡처 91개와 새 캡처 91개가 모두
바이트 단위로 일치했다. 10/50/100체 각 120표본의 기준 측정과 기존
cooked 패키지에 최신 Player 바이너리를 적용한 Walk 진행·정상 종료 게이트도
통과했다. 결과는 `Build/Obj/Phase13S35c/`에 있다. 단일 실행의 CPU
시간값으로 성능 개선을 주장하지 않는다.

**S3.5는 계속 진행 중이다.** 현재 `blend`는 전이 블렌드이며 마스크
합성은 아직 `UpdateLayer` 안에 있다. 가산 커널은 재생 설정에 연결되지
않았다. 모든 Animator의 Update/Execute를
각각 별도 병렬 구간으로 나누는 작업과 워커별 포즈 버퍼 풀·steal-in-place,
실제 비활성 상태머신 가지 비용 0과 포즈 버퍼 상한 판정도 남았다. GPU
팔레트 벌크 업로드·3×4 전달 및 제품 경로 무할당 판정 역시 남았다. Vulkan
제품 검증은 후속 통합 게이트에서 수행한다.

## 30. S3.5 후속 — Animator 전체의 준비/실행 병렬 구간 (2026-09-24)

`AnimationJob::Update`는 프레임의 Animator 스냅샷과 모델 generation 참조를
먼저 고정한 뒤, 전 Animator의 시간·이벤트·평면 태스크 레시피를 준비하는
`Update` 작업 그룹을 제출한다. 그룹 완료를 기다린 다음 준비에 성공한
Animator만 `Execute` 그룹에 넣어 샘플·블렌드·FK·팔레트를 실행한다. 두
그룹의 완료 후에만 메인 스레드가 Scene 포즈와 소켓을 게시한다. 이벤트는
기존처럼 게임 스레드 전달 큐에 쌓여 조인 뒤 전달된다. 첫 그룹이 실패하면
둘째 그룹은 제출하지 않는다.

`jobs`는 Animator 논리 작업 수를 유지하고, `updatePassJobs`와
`executePassJobs`는 실제 두 그룹의 제출 수를 나타낸다. 기존 `waitUs`와
`workerSumUs`는 양쪽 구간의 합이며, 두 구간을 각각 관찰할 수 있도록 대기와
워커 시간도 별도 기록한다. `workerSpanUs`는 두 그룹 사이의 barrier까지
포함하므로 이전 단일 그룹 표본과 직접 비교할 수 없다. 계측 워커 시간은
겹쳐 실행되며 프레임 시간에 더할 수 없다.

Debug/Release Editor와 Release Player 빌드, 두 Editor 구성의 CreatorRobot
제품 100체×12회 각 234,531검사·관리 스레드 오류 0건이 통과했다. Release
기준 측정 10/50/100체 각 120표본에서
두 구간 모두 Animator 수만큼 실행되고 팔레트 아레나 확장 0건을 확인했다.
Release DX12 화면 회귀 13캡처/264검사가 통과했으며, §29의 원본 float
캡처 91개와 새 캡처 91개가 바이트 단위로 일치했다. 기존 검증된 cooked
패키지에 새 Player 실행 파일을 적용한 Walk 재생·정상 종료 게이트도
통과했다. 결과는 `Build/Obj/Phase13S35d/`에 있다. 단일 기준 측정으로
성능 개선을 주장하지 않는다.

**S3.5는 계속 진행 중이다.** 마스크·가산 합성의 태스크 분리, 워커별 포즈
버퍼 풀과 `steal-in-place`, 실제 비활성 상태머신 가지 비용·포즈 버퍼 상한
판정, GPU 팔레트 벌크 업로드·3×4 전달 및 제품 경로 무할당 판정이 남았다.
Vulkan 제품 검증은 후속 통합 게이트에서 수행한다.

## 31. S3.5 후속 — 마스크 레이어의 평면 태스크 분리 (2026-09-25)

다중 Controller의 일괄 `compose_layers`를 `prepare_composite` → 활성 레이어별
`blend_masked` → `materialize_composite` 태스크로 나눴다. 앞선 인덱스 의존으로
기존 Controller 순서를 유지한다. 비활성 Controller에는 마스크 합성 태스크를
기록하지 않는다. 준비 단계는 본 인덱스 마스크 캐시를 갱신하고 이번 프레임의
선택 비트를 초기화한다. 각 마스크 태스크는 처음 기여하는 본만 항등 로컬 포즈에서
시작하고, 마지막 태스크가 선택 결과를 Scene 로컬 행렬·스킨 팔레트·소켓으로
물질화한다. 어느 레이어도 기여하지 않은 본의 이전 로컬·포즈는 유지한다.
새 포즈 버퍼 대신 기존 `AnimInstance::pose`를 합성 결과로 사용하고, 선택 비트만
인스턴스 재사용 저장소에 추가했다.

현재 Controller에는 가산 재생 모드나 참조 포즈 저작 계약이 없다.
`MakeAdditive`/`ApplyAdditive` 본별 커널은 이미 있지만 제품 태스크로 기록할
입력이 없으므로 가산 실행을 이번 변경에 포함하지 않았다. 이를 연결할 때는
참조 포즈와 직렬화·Editor 저작 의미를 먼저 확정해야 한다.

Debug/Release Editor와 Release Player 빌드, 두 Editor 구성의 CreatorRobot
제품 100체×12회 각 234,533검사·관리 스레드 오류 0건이 통과했다. 제품
검사는 레이어 순서·비활성 레이어 제외·부분 마스크·소켓·팔레트를 포함한다.
Release DX12 화면 회귀 13캡처/264검사가 통과했고, §30의 원본 float
캡처 91개와 새 캡처 91개가 모두 바이트 단위로 일치했다. 기존 검증된
cooked 패키지에 최신 Player를 적용한 Walk 진행·정상 종료 게이트도
통과했다. 단일 Controller 기준 측정 10/50/100체 각 120표본에서는
팔레트 아레나 확장 0건을 확인했다. 결과는 `Build/Obj/Phase13S35e/`에
있다. 이 단일 측정으로 성능 개선을 주장하지 않는다.

**S3.5는 계속 진행 중이다.** 가산 태스크 연결, 워커별 포즈 버퍼 풀과
`steal-in-place`, 실제 비활성 상태머신 가지 비용·포즈 버퍼 상한 판정,
GPU 팔레트 벌크 업로드·3×4 전달 및 제품 경로 무할당 판정이 남았다.
Vulkan 제품 검증은 후속 통합 게이트에서 수행한다.

## 32. S3.5 후속 — 가산 레이어의 재생 계약과 태스크 연결 (2026-09-25)

Controller에 기본값 `false`인 가산 모드를 추가하고 scene 직렬화와 Editor
레이어 설정에 연결했다. 모드를 켠 레이어의 참조 포즈는 **현재 클립의 시작 시점
(tick 0)**으로 정의한다. 전이 중에는 현재·다음 클립의 시작 포즈를 재생 포즈와
같은 블렌드 값으로 합친다. 현재 클립에 채널이 없는 본은 기여하지 않는다.
마스크 가중치 0인 본은 참조 포즈 샘플링도 하지 않는다. 기본 모드와 기존 scene은
종전의 마스크 합성 경로를 유지한다.

활성 가산 레이어는 `make_additive` → `apply_additive`를 순서대로 기록한다.
첫 태스크는 레이어 포즈가 물질화된 뒤 그 저장소를 참조 포즈 대비 TRS 델타로
재사용하고, 둘째 태스크는 선택된 기저 포즈에 본별 마스크 가중치를 적용한다.
따라서 별도 가산 포즈 버퍼는 만들지 않는다. 클립 시작 기준 이외의 참조 포즈
저작은 이번 계약에 포함하지 않는다.

Debug/Release Editor와 Release Player 빌드, 두 Editor 구성의 CreatorRobot
제품 100체×12회 각 234,538검사·관리 스레드 오류 0건이 통과했다. 제품
검사는 가산 태스크 순서, 시작 포즈 대비 결과, 저장 키, 반복 프레임 무누적을
포함한다. Release DX12 일반 재생 화면 회귀 13캡처/264검사가 통과했고,
§31의 원본 float 캡처 91개와 새 캡처 91개가 바이트 단위로 일치했다.
기존 검증된 cooked 패키지에 최신 Player를 적용한 기본 Walk 재생·정상 종료
게이트가 통과했다. 단일 Controller 기준 측정 10/50/100체 각 120표본에서
팔레트 아레나 확장 0건을 확인했다. 결과는 `Build/Obj/Phase13S35f/`에 있다.
새 가산 모드의 DX12 화면 픽셀·별도 Player 씬과 성능 개선은 검증하지 않았다.

**S3.5는 계속 진행 중이다.** 워커별 포즈 버퍼 풀·`steal-in-place`, 비활성
상태머신 가지 비용과 포즈 버퍼 상한 판정, 순수 `{offset,count}` 팔레트 명령,
GPU 벌크 업로드·3×4 전달 및 제품 경로 무할당 판정이 남았다. Vulkan 제품
검증은 후속 통합 게이트에서 수행한다.

## 33. S3.5 후속 — 비활성 분기 실측과 사용하지 않는 이전 포즈 제거 (2026-09-25)

`ExecutePose`에서 실제로 유효한 클립을 샘플링한 횟수를 인스턴스별로
기록한다. 미선택 `AnyState`가 있는 단일 Controller는 1회, 활성 Controller
두 개는 2회, 둘 중 하나를 비활성화하면 1회여야 한다. 평면 레시피에
태스크가 없다는 정적 확인에 더해 실행 경로의 샘플 호출을 판정한다.

`posePrev`는 기존 구현에서 매 프레임 `pose`를 복사하고 크기만 유지했으며,
실행·게시·보간 경로에 읽는 소비자가 없었다. 이 중복 포즈 저장소와 복사를
제거했다. 과거 §2.2①·§3 S3′의 prev/curr 표기는 당시 설계·구현 기록이다.
포즈 보간이 필요한 S4에서는 실제 소비 경로와 함께 이전 포즈 수명을 다시
정해야 한다.

Debug/Release Editor와 Release Player 빌드, 두 Editor 구성의 CreatorRobot
제품 100체×12회 각 234,640검사·관리 스레드 오류 0건이 통과했다. 제품 검사는
미선택 AnyState가 있는 단일 Controller 1회, 활성 Controller 두 개 2회,
하나를 비활성화한 뒤 1회의 실제 클립 샘플 호출을 확인한다. Release DX12
일반 재생 화면 회귀 13캡처/264검사가 통과했고 §32의 float 캡처 91개와
바이트 단위로 일치했다. 최신 Release Player로 기존 검증된 cooked 패키지를
실행한 Walk 진행·정상 종료 게이트도 통과했다. 결과는
`Build/Obj/Phase13S35g/`에 있다. 성능 개선은 주장하지 않는다.

이 변경만으로 인스턴스당 포즈 버퍼 4개 상한은 달성되지 않는다. 현재도
`pose`, 현재/다음 샘플, Controller별 `layerPose`가 별도 저장소라 두 레이어의
전이에서는 5개가 존재할 수 있다. 다음에는 레이어별 보존 포즈를 실행
구간의 재사용 저장소로 이관하고 `steal-in-place` 수명 규칙을 확정해야 한다.
Vulkan 제품 검증은 후속 통합 게이트에서 수행한다.

## 34. S3.5 후속 — 레이어 포즈 저장소 공유와 즉시 합성 (2026-09-25)

기존 레시피는 모든 Controller를 샘플·물질화한 뒤 레이어를 합성해,
`ControllerPlayback`마다 `layerPose`를 보유했다. 이제 합성 준비를 먼저
기록하고, 각 레이어의 샘플·물질화 직후 마스크 또는 가산 합성을 기록한다.
앞선 인덱스 의존이 해당 레이어의 합성 완료 전에 다음 샘플이 공유 저장소를
덮어쓰지 못하게 한다. `ControllerPlayback`의 포즈와 임시 포인터 표를 제거하고
`AnimInstance::layerScratchPose` 하나를 모든 레이어가 재사용한다. 다중 레이어의
중간 FK도 생략하고 최종 합성 포즈에서 팔레트·소켓을 한 번 계산한다.

활성화됐지만 `m_useLayer=false`이고 전이하지 않는 Controller는 시간과
이벤트만 진행하며 포즈 태스크는 기록하지 않는다. 인스턴스가 보유한
`LocalPose` 저장소는 최종 포즈, 현재·다음 클립 샘플, 공유 레이어 임시 포즈
네 개다. 할당된 용량을 세는 제품 검사에서 여섯 활성 레이어는 4개 이하,
다중 레이어 이후 단일 Controller 전이는 정확히 4개를 확인한다.

Debug/Release Editor와 Release Player 빌드, 두 Editor 구성의 CreatorRobot
제품 100체×12회 각 234,646검사·관리 스레드 오류 0건이 통과했다.
Release DX12 일반 재생 화면 13캡처/264검사가 통과했고 §33의 float 캡처
91개와 새 결과가 바이트 단위로 일치했다. 최신 Release Player로 기존
검증된 cooked 패키지를 실행한 Walk 진행·정상 종료 게이트도 통과했다.
원본은 `Build/Obj/Phase13S35h/`에 있다.

**S3.5는 계속 진행 중이다.** 포즈 버퍼 수 상한은 일반 제품 실행의
인스턴스 소유 `LocalPose`에서 확인했지만 워커별 풀과 일반화된
`steal-in-place` 실행기는 아직 없다. 순수 `{offset,count}` 팔레트 명령,
GPU 벌크 업로드·3×4 전달 및 전체 무할당 판정도 남았다. 성능 개선은
주장하지 않는다. Vulkan 제품 검증은 후속 통합 게이트에서 수행한다.

## 35. S3.5 후속 — 작업자별 포즈 저장소와 레이어 소유권 이전 (2026-09-25)

최종 `pose`만 `AnimInstance`에 남기고, 현재·다음 샘플과 레이어 임시 포즈는
실행 스레드의 `thread_local` 3슬롯 풀로 옮겼다. 공유 enkiTS가 이미 소유한
작업자(또는 대기에 참여한 호출 스레드)가 Animator 작업 사이에 용량을
재사용한다. 새 스레드를 만들지 않는다. 단일 Controller는 최종 포즈를
인스턴스에 게시하고, 다중 Controller는 샘플 입력의 마지막 소비 지점에서
현재/레이어 버퍼를 맞바꿔 본별 복사 없이 다음 합성 태스크에 넘긴다.

진단용 단일 포즈 평가는 호출 스레드의 풀을 사용하지만 별도 키 커서를 쓰고
기존처럼 인스턴스의 최종 포즈·팔레트·선택 클립을 복원한다. 제품 검사는
100개 Animator의 작업자 풀 신원과 TRS 저장소 주소를 기록해, 같은 작업자에
배정된 작업이 예열된 용량을 재사용하는지 확인한다. 인스턴스 1슬롯과
작업자 풀 최대 3슬롯을 합한 포즈 저장소 상한 4개도 다중 레이어·전이에서
다시 확인한다. 주소 기록은 비교에만 쓰며 저장소를 역참조하지 않는다.

Debug/Release Editor와 Release Player 빌드, 두 Editor 구성의 CreatorRobot
제품 100체×12회 각 234,746검사·관리 스레드 오류 0건이 통과했다.
Release DX12 일반 재생 화면 13캡처/264검사가 통과했고 §34의 float 캡처
91개와 새 결과가 바이트 단위로 일치했다. 최신 Release Player로 기존
검증된 cooked 패키지를 실행한 Walk 진행·정상 종료 게이트도 통과했다.
원본은 `Build/Obj/Phase13S35i/`에 있다.

**S3.5는 계속 진행 중이다.** 최종 포즈는 인스턴스에 복사하므로 모든
태스크 출력이 풀 버퍼의 소유권을 이전하는 일반 실행기는 아니다. 순수
`{offset,count}` 팔레트 명령, GPU 벌크 업로드·3×4 전달 및 전체 무할당
판정이 남았다. 성능 개선은 주장하지 않는다. Vulkan 제품 검증은 후속
통합 게이트에서 수행한다.

## 36. S3.5 완료 — 최종 포즈 이전과 벌크 스키닝 업로드 (2026-09-25)

단일 Controller 레시피의 최종 출력은 작업자 `current` 포즈의 TRS 저장소를
인스턴스 최종 포즈와 맞바꿔 게시한다. 작업자는 이전 인스턴스 저장소를 다음
Animator에 재사용한다. 다중 Controller는 공유 레이어 포즈의 즉시 합성과
최종 출력 수명을 유지한다. 활성 클립에 없는 채널은 직전 인스턴스 포즈를
보존하며, 빈 레시피에는 출력 교환을 하지 않는다. 제품 검사는 출력 버퍼의
소유권 이전과 이전 저장소의 작업자 재사용을 확인한다.

스킨 명령의 렌더 페이로드는 `{paletteOffset,boneCount}`다. 생산 중인 명령의
임시 아레나 참조는 큐가 명령을 배치로 회수할 때 배치 소유로 옮기고 지운다.
배치 병합은 오프셋을 재기준화하며 지연 명령은 적용 시점까지 필요한 소유권을
유지한다. CPU 프레임 아레나는 실제 본 수의 4×4 행렬을 보관한다. 렌더 씬은
불투명·전방 draw의 팔레트를 한 번 모아 48바이트 3×4 행으로 패킹하고,
렌더링하는 뷰마다 GPU 버퍼를 한 번 갱신한다. GBuffer·Forward·Shadow·
WireFrame은 공통 버퍼의 오프셋을 사용한다. 따라서 §2.2⑤의 3×4 표현은
GPU 전달 경계에 적용하며 CPU 아레나의 기존 행렬 표현은 유지한다.

Release 전체 빌드와 Debug 전체 빌드, 독립 재생 7,231검사, 양 구성의
CreatorRobot 제품 100체×12회 각 234,746검사·관리 스레드 오류 0건이
통과했다. 새 엔진 배포본으로 Release Player 패키지를 쿠킹·스모크 검증한
뒤 Walk 진행·정상 종료 게이트가 통과했다. Release DX12 화면은 가산
합성을 포함한 14캡처/284검사를 통과했고 기존 일반 재생 11포즈의
float 캡처 77개는 §35 결과와 바이트 단위로 일치했다. 네 종류의 스킨
정점 셰이더도 DXIL로 각각 컴파일했다. 10/50/100체 측정은 각각 30프레임
예열 뒤 120프레임에서 포즈 TRS 저장소 성장과 CPU 팔레트 아레나 할당이
모두 0임을 검사했다. 원본은 `Build/Obj/Phase13S35j/`에 있다.

**S3.5 완료 판정은 이 범위에 한정한다.** `job_group`의 Animator별 작업
제출 객체·벡터 할당은 S6의 청크화·재사용 저장소 작업이다. GPU 업로드
저장소 자체의 전체 힙 할당 0이나 성능 향상은 이 측정으로 주장하지 않는다.
가산 모드는 DX12 Editor 화면으로 확인했으며 가산 Controller가 들어간 별도
Player 씬은 아직 검증하지 않았다. Vulkan 제품·백엔드 교차 검증은 앞서
결정한 후속 통합 게이트에서 수행한다.

## 37. S3.6 완료 — 관측 본 물질화 (2026-09-25)

`AnimInstance`의 로컬 포즈와 최종 스킨 팔레트를 계속 정본으로 둔다.
`Scene::PublishAnimatorPoseImpl`은 비본 자식, 소켓, 에디터 선택, 명시 고정,
직접 조회로 승격된 본을 관측하고 조상 사슬을 닫는다. 본 이름과 비본 자식
여부는 skeleton serial·topology 바인딩 시 캐시하며, 변하는 선택·소켓·고정
상태는 게시 시 반영한다. 관측되지 않은 본의 로컬 행렬은 투영하지 않는다.
공간 해석기 역시 관측되지 않은 애니메이션 본의 하위 범위를 건너뛰어,
관측된 부모가 움직여도 숨은 자손의 월드 행렬을 기록하지 않는다.

`BoneComponent::GetWorldTransform()`과 본의 Transform 월드 조회는 현재
포즈의 본·조상 로컬을 해당 경로에만 투영하고 월드를 계산한다. 이미 확립된
바인딩에서 조회 비용은 본 전체 수가 아닌 계층 깊이에 비례한다. 첫 조회는
런타임 관측 상태로 자동 승격되며 로그를 남기므로 이후 애니메이션 게이트에서
상시 투영한다. 바인딩 전 조회나 topology 변경 시에는 일반 게시 경로로
바인딩을 다시 만든다. `m_bPinned`만 저작 설정으로 저장하고 자동 승격과
인덱스·serial·owner 캐시는 저장하지 않는다.

최종 스킨 팔레트 변경은 본 로컬 기록 수와 별도로 비교해 프록시 payload를
갱신한다. 따라서 관측 본이 0개여도 스킨 포즈는 전진한다. Debug/Release
전체 빌드, 독립 재생 7,231검사, 양 구성의 CreatorRobot 제품 100체×12회
각 234,754검사·관리 스레드 오류 0건을 통과했다.
여기에는 직접 조회 승격·비본 자식·고정·선택, 관측 부모 아래 숨은 본의 월드
기록 0, 기존 소켓 부착물 월드 위치 왕복 검사가 들어 있다. Release DX12
화면은 16캡처/318검사를 통과했고, 마지막 두 포즈는 관측 본 0·투영 0·로컬
기록 0인 상태에서 스킨 깊이와 팔레트가 달라지는 것을 검사한다. 최신
Release Player를 검증된 cooked Walk 패키지에 적용한 진행·정상 종료
게이트도 통과했다. 10/50/100체 각각 30프레임 예열 뒤 120표본 측정은
정상 수집했고 포즈 저장소 성장·CPU 팔레트 아레나 할당 0을 유지했다.
원본은 `Build/Obj/Phase13S36/`에 있다.

이 한 번의 Release 측정만으로 성능 향상을 주장하지 않는다. Vulkan 제품·
백엔드 교차 검증은 앞서 결정한 후속 통합 게이트에서 수행한다.

## 38. S4 진행 — 중요도 입력과 첫 강등 레시피 (2026-09-25)

게임 실행 중 각 Animator의 저작 반경(`m_QualityRadius`)으로 근사한 경계와
씬에 등록된 활성 카메라의 거리·프러스텀(직교 카메라는 화면 상자)·투영 높이를 평가한다. 여러 카메라 중 최대
투영 높이를 사용한다. 카메라가 없거나 에디터 미리보기이면 L0이며,
`m_ForceFullQuality`로 게임플레이상 반드시 정확해야 하는 Animator도 L0에
고정할 수 있다. 현재 자동 선택은 투영 높이 0.12/0.08/0.05를 경계로
L0/L1/L2/L3, 모든 카메라에서 보이지 않으면 L7이다. 반경은 실제 스킨
경계가 아니라 저작자가 지정하는 근사치이므로 보수적으로 설정해야 한다.

L2는 비기본 가산 레이어의 샘플부터 합성까지, L3은 비기본 마스크 레이어까지
태스크 레시피에서 제거한다. 첫 유효 레이어는 기본 포즈로 남기며, 제어기
상태·시간·이벤트 준비는 원래 경로로 진행한다. L7은 포즈 실행 잡을 제출하지
않고 이전 포즈를 유지한다. 재가시 첫 프레임은 L0로 전부 평가한다.
§39에서 두 본 IK 태스크를 추가했다. §40에서 명시적 품질 단계의 L1 감쇠와
태스크 생략을 검증했고, §41에서 카메라 자동 선택에도 적용했다.
BoneTransform 태스크는 아직 없다.
L7에서도 준비 패스와 장면 게시 비용은 남으므로 전체 애니메이션 CPU 비용이
0이라는 뜻은 아니다.

Debug/Release 전체 빌드, 독립 레시피·정책 재생 7,239검사, 양 구성의 CreatorRobot 제품
100체×12회 각 234,763검사·관리 스레드 오류 0건, Release DX12 화면 16캡처/318검사가
통과했다. 제품 검사에는 강제 L7에서 시간 전진·포즈 샘플 0·팔레트 유지,
복귀 첫 프레임 L0 평가, 풀 품질 고정 우선순위, 실제 카메라 이동에 따른
L3/L0/L7 선택과 직교 화면 밖 판정을 포함한다. 검증 원본은
`Build/Obj/Phase13S4/`에 둔다. Release 10/50/100체 각각 120표본에서
포즈 저장소 성장·CPU 팔레트 아레나 할당 0을 유지했다. 최신 Release
Player의 cooked Walk 진행·정상 종료 게이트도 통과했다. 이 검사는 기본 품질의 회귀 및 강제
강등 경로 검증이며 카메라 거리 변화의 연속 화면 품질이나 단계별
비용 단조성을 입증하지 않는다.

**S4는 완료가 아니다.** L4의 블렌드 스냅은 팝 방지 인러셜라이제이션과
함께 구현해야 하므로 아직 자동 선택에서 제외한다. L5 저디테일 부모 선행
본 프리픽스와 bind-local 유지, L6 저빈도 평가와 포즈 보간, 등급 간 강등·
승격의 화면 및 비용 검증도 남았다. L1의 자동 IK 생략은 §41에서 한 제품
장면의 연속 화면 게이트 뒤 활성화했으나 단계별 비용 단조성은 미검증이다.
Vulkan 교차 검증은 결정대로 후속 통합 게이트다.

## 39. 수동 목표 두 본 IK — 첫 구현 (2026-09-27)

`Animator`가 시작·중간·끝 본 이름, 월드 목표·pole, 가중치, 활성 여부,
게임플레이 필수 여부를 씬에 저장한다. 본 이름은 스켈레톤 세대와 저작 이름이
바뀔 때만 인덱스로 다시 해석한다. 메인 스레드는 매 프레임 목표를 Animator
월드의 역행렬로 포즈 공간에 옮겨 값 스냅샷을 만들고, IK 계산은 Scene·물리
객체를 직접 조회하지 않는다. `AnimTaskList`의 `TwoBoneIK` 태스크는 최종 레이어
합성 뒤에 연속 실행된다. 해석적 솔버는 두 구간 길이를 보존하고 도달 불가능한
목표는 뻗을 수 있는 범위로 제한한다. pole이 퇴화하면 이전 팔꿈치 평면을
우선 사용한다.

첫 구현의 머티리얼라이즈 경로는 FK·스킨 팔레트·소켓 staging을 함께 계산해,
IK가 바꾼 로컬 회전을 전체 경로로 다시 확정했다. §40에서 변경한 본과 자손만
재계산하도록 줄였다. 구성한 세 본 중 재생 채널이 없는 본이 있거나 다중 레이어의
선택 본이 아닌 경우는 적용하지 않는다. 수동 월드 목표만 제공하며 지면
raycast와 발 접지 상태 머신은 별도 소비자로 남긴다.

`Required` 제약이 활성화되면 화면 밖에서도 Animator를 L0로 유지한다.
첫 구현에서는 일반 IK의 자동 L1 삭제를 IK 연속 화면 검증 전까지 켜지 않았다.
§41에서 한 CreatorRobot 장면의 화면 게이트를 통과한 뒤 활성화했다.
두 본 위치·도달 범위·퇴화 입력의 독립 검사와 CreatorRobot 제품 경로의
목표 접근·팔레트/소켓 일치·비활성 복귀·씬 직렬화 검사 원본은
`Build/Obj/Phase13IK/`에 둔다. Debug/Release 전체 빌드와 독립 재생
7,246검사, 양 구성의 CreatorRobot
제품 100체×12회 각 234,769검사·관리 스레드 오류 0건을 통과했다. Release
DX12 화면 16캡처/318검사와 최신 cooked Player Walk 진행·정상 종료도
확인했다. 10/50/100체 기본 경로 각 120표본에서 포즈 저장소 성장과 CPU
팔레트 아레나 할당은 0이었다. 이는 기본 경로의 회귀 확인이며 IK 사용 시
성능 개선이나 전환 시 시각 품질을 입증하지 않는다. Vulkan은 후속 통합 게이트다.

## 40. S4 L1 준비 — IK 자손 재계산과 가중치 감쇠 (2026-09-27)

두 본 IK 보정 뒤 전체 FK를 반복하지 않고 변경 시작 본과 그 자손만 부모 선행
순서로 다시 계산한다. 무관한 가지의 전역 자세·스킨 팔레트·소켓은 첫 계산 결과를
유지한다. 첫 FK와 IK 없는 경로는 그대로이며 자손 판정 저장소는 인스턴스가
재사용한다.

명시적 품질 단계 지정으로 L0↔L1을 시험할 때 일반 IK 가중치를 0.15초에 걸쳐
감쇠·복원한다. L1에서 가중치가 정확히 0이 된 뒤에만 IK 스냅샷과 태스크를
생략한다. `Required`는 감쇠와 L7 지정을 우회해 L0 전체 가중치를 유지한다.
화면 밖에서는 가중치를 보존한다. 이 슬라이스에서는 카메라 기반 자동 선택이
IK를 생략하지 않았다. 기존 DX12 화면 회귀 16캡처/318검사는 IK 전환 화면을
포함하지 않아, §41에서 별도 연속 화면 게이트를 추가했다.

Debug/Release 전체 솔루션 빌드, 독립 재생 각 7,252검사, CreatorRobot 제품
100체×12회 각 234,783검사·관리 스레드 오류 0건, Release DX12 기존 화면
16캡처/318검사와 최신 Player의 cooked Walk 진행·정상 종료 게이트가 통과했다.
제품 경로는 강제 L1의 3프레임 감쇠·태스크 제거,
강제 L0 복원, `Required` 우선순위, 팔레트·소켓 일치를 확인했다. 기존 포즈 버퍼
검사의 `== 4` 조건은 계약인 최대 4개(`<= 4`)로 바로잡았다. 검증 원본은
`Build/Obj/Phase13IKL1/`에 둔다. 성능 개선이나 자동 L1의 화면 품질은 주장하지 않는다.

## 41. S4 L1 자동 선택 — 두 본 IK 연속 화면 게이트 (2026-09-27)

카메라가 선택한 L1~L3에서도 선택적 두 본 IK에 §40의 0.15초 가중치 감쇠를
적용한다. 0에 도달하면 IK 스냅샷과 태스크가 레시피에서 빠지고, L0 복귀 시
세 프레임에 걸쳐 복원한다. `Required` 제약과 풀 품질 고정은 L0 전체 가중치를
유지한다. 평가 중인 클립 시각과 목표가 같을 때 가중치만 바뀌도록 CreatorRobot
손 체인을 고정한 DX12 제품 화면 게이트를 추가했다.

게임 카메라·모델·Walk 자세를 고정하고 품질 반경만 바꿔 자동 L0/L1을 선택한
8캡처/114검사가 통과했다. L1 3프레임에서 가중치 1→2/3→1/3→0,
IK 태스크 1→1→1→0을 관측했고 L0 복귀에서 역순으로 돌아왔다.
렌더된 소켓 마커의 전체 이동은 22.85픽셀이며 한 프레임에 전체 차이가
한 번에 나타나지 않았다. 원래 자세·복원 자세의 팔레트와 마커 위치가 각각
일치했고, 마커를 가린 뒤에도 스킨 깊이 77픽셀이 달랐다. 실제 캡처·접촉 시트와
판정 원본은 `Build/Obj/Phase13IKAuto/Visual-Release-Centered/`에 둔다.

Debug/Release 전체 솔루션 빌드, 독립 재생 각 7,252검사, 양 구성 CreatorRobot
100체×12회 각 234,783검사·관리 스레드 오류 0건, 기존 DX12 화면
16캡처/318검사, 최신 Release Player cooked Walk 진행·정상 종료가 통과했다.
검증 원본은 `Build/Obj/Phase13IKAuto/`에 둔다. 이 게이트는 한 손 체인과 고정
목표의 팝을 확인한다. 다른 목표·카메라 속도와 L0~L7 단계별 실제 비용 단조성은
아직 검증하지 않았으며, L4~L6·BoneTransform도 남아 있다. S4는 진행 중이고
Vulkan은 후속 통합 게이트에서 검증한다.

## 42. S4 L4 전이 레시피와 승격 복귀 (2026-09-27)

단일 Controller 전이의 L4 레시피에서 `Blend`를 중간값 기준으로 한쪽
`SampleClip`에 연결한다. 선택되지 않은 샘플과 블렌드는 출력에서 도달할 수
없으므로 실제 실행 샘플이 2개에서 1개로 줄어든다. 다음 클립을 선택할 때도
기존 교집합 채널 마스크와 현재 클립의 물질화 계약을 유지한다. 재생 시간·이벤트
진행은 레시피 변환 전에 끝난다. 다중 Controller 전이는 이 슬라이스에서 제외했다.

블렌드를 생략한 L4에서 L0~L3으로 승격하면 직전 출력과 새 전체 블렌드의 로컬 포즈 차이를
인스턴스별 additive offset으로 잡고, 0.15초 smoothstep 감쇠로 현재 샘플에
적용한다. 첫 승격 프레임은 직전 포즈와 같고 마지막 프레임은 전체 블렌드에
합류한다. 이는 속도 추정 없이 위치 연속성을 확보하는 첫 인러셜 복귀다.
카메라 자동 선택은 아직 L0~L3/L7만 사용한다. L4는 제품 회귀의 명시적
품질 지정으로만 활성화했다. L4의 자동 임계값·전이 중 한 단계 상향 보정,
다중 레이어 전이, 실제 화면 팝·비용 단조성 게이트는 후속 검증 후 켠다.

최종 소스의 Debug/Release 전체 솔루션 빌드가 통과했다. 독립 재생은 각
7,252검사, CreatorRobot 제품 경로는 각 100체×12회·234,790검사·관리 스레드
오류 0건을 통과했다. 제품 회귀는 L4에서 현재/다음 한쪽 샘플만 실행하고,
승격 첫 프레임의 포즈 유지와 150ms 뒤 전체 블렌드 합류, 전이가 없는 L4에서
불필요한 복귀를 시작하지 않는 조건을 확인한다. 기존 DX12 화면 회귀는
16캡처/318검사를 통과했고 Release cooked Player Walk 게이트도 통과했다.
검증 원본은 `Build/Obj/Phase13S4L4-*`에 둔다. L4 자체의 화면 팝과 자동
임계값은 이 게이트가 아직 판정하지 않는다. Vulkan은 후속 통합 검증이다.

## 43. S4 마무리 — L0~L7 선택·복귀·비용 (2026-09-27)

§38~§42는 각 시점의 진행 기록이다. 최종 자동 선택은 투영 높이
0.12/0.08/0.05/0.035/0.02/0.01을 경계로 L0~L6을 선택하고, 모든
카메라에서 보이지 않으면 L7이다. 에디터 미리보기·카메라 없는 씬과
`m_ForceFullQuality`, 필수 IK/BoneTransform은 L0이다. L5/L6은 저작한
`m_LowDetailBoneCount`가 유효할 때만 선택하고, 없으면 L4로 제한한다.
전이 중에는 자동 등급을 한 단계 올린다.

L1에서 선택적 `TwoBoneIK`와 로컬 TRS `BoneTransform` 보정은 150ms에
걸쳐 감쇠한 뒤 레시피에서 빠진다. L2/L3는 가산·마스크 레이어를,
L4는 현재/다음 클립 중 한 샘플과 크로스페이드 계산을 제거한다. 다중
Controller에도 기본 레이어의 L4~L6을 적용하며, 합성 출력의 직전 포즈를
보존해 승격을 봉합한다. L5는 부모 선행 순서의 저작 본 프리픽스만 샘플하고
잘린 본은 역바인드로 복원한 bind-local을 쓴다. 피부·소켓의 FK는 전
계층을 유지한다. 자동 L5 진입과 L4/L5 승격은 직전 게시 포즈에서
150ms additive offset으로 합류한다.

L6은 재생 시간·이벤트를 매 프레임 전진시키면서 투영 높이 0.005 이상이면
2틱마다, 그 아래에서는 4틱마다 샘플한다. 중간 출력은 두 평가 결과의
로컬·스킨·전역 **행렬을 보간**한다. 원거리에서 비용을 낮추기 위한
선형 행렬 보간이므로 중간 행렬은 완전한 강체 회전이 아닐 수 있다.
가까워져 승격할 때 게시한 로컬 행렬을 TRS로 복원하고 인러셜 복귀를
시작한다. 모델 세대를 다시 바인딩하면 바인드·L6 캐시와 복구 상태를
무효화한다. L7은 포즈 실행 잡 0개로 동결하고 재가시 첫 프레임은 L0로
전부 평가한다. 준비 패스와 장면 게시 비용까지 0이라는 뜻은 아니다.

`measure-animation-quality.ps1`의 Release CreatorRobot 100체, 등급별
예열 30프레임 뒤 120프레임×3회 QPC 결과에서 포즈 실행 워커 시간의
반복 중앙값(마이크로초)은 L0~L7 순서로
1161/1212/1233/1195/1181/1175/1035/0이다. L0~L4의 단일 Walk에는
선택적 태스크가 없어 사실상 같은 작업이며 차이는 10% 측정 허용폭 안이다.
L5→L6의 감소와 L7의 실행 0은 확인했다. 전체 `AnimationJob::Update`
벽시계 시간은 스케줄러 대기 변동 때문에 등급별 단조성을 입증하지
못했다. 이 구분은 S5 비용 모델과 S6 청크화에서 유지한다. 원본은
`Build/Obj/Phase13S4Finish-Quality-Final/quality-cost.json`이다.

제품 회귀는 자동 L5 진입 첫 출력, L4/L5/L6 승격 첫 출력과 150ms 합류,
L6 2/4틱 샘플·중간 팔레트 보간, 다중 Controller, 세대 캐시 해제,
BoneTransform의 L1 감쇠·필수 L0를 확인한다. VS18/v145 Debug·Release
전체 솔루션 빌드, 독립 재생 각 7,254검사, 양 구성 CreatorRobot 제품
100체×12회 각 234,818검사·관리 스레드 오류 0건이 통과했다. 기존 DX12 화면
16캡처/318검사와 IK 연속 화면 8캡처/114검사, Release cooked Player
Walk 진행·정상 종료를 다시 통과했다. 원본은 `Build/Obj/Phase13S4Finish-*`에
있다. 화면 게이트는 고정 Walk 손 IK와
기본 재생 장면이므로 다양한 IK 목표·카메라 이동 속도에서의 지각 품질은
후속 회귀 확장 범위로 남긴다. Vulkan 교차 검증은 앞서 결정한 후속
통합 게이트에서 수행한다.

## 44. S5 완료 — 태스크 비용 모델과 CPU 예산 (2026-09-27)

`AnimationJob::Update`는 모든 Animator의 시간·이벤트·전체 태스크 레시피를
준비한 뒤 기존 Update/Execute 배리어에서 예산을 배분한다. 엔진 설정
`animation.cpuBudgetMs`의 기본값은 4.0ms이고,
`animation.promotionGraceFrames`는 8프레임,
`animation.hysteresis`는 0.05다. 설정은 `EngineSettings.asset`에서 읽고
에디터 저장 시 다른 필드와 함께 보존된다. 에디터 프리뷰·강제 품질·필수
IK/BoneTransform·재가시 첫 프레임은 기존 L0 계약을 우선한다. 화면 정책은
각 인스턴스가 **허용하는 최대 강등 등급**을 정하고, 스케줄러는 중요도
내림차순으로 L0부터 그 한계까지 살아남는 레시피의 비용을 계산해 예산에
맞는 첫 등급을 고른다. 승격은 여유가 연속된 프레임에 지속되어야 하며,
인접한 품질 등급의 투영 크기 경계에는 진입·이탈 5% 히스테리시스를
둔다. 카메라가 여러 등급을 한꺼번에 가로지르면 즉시 승격한다.

비용 EMA는 인스턴스별이 아니라 `task_kind`별이다. 샘플링 비용은 활성
저작 본 수로, 전 계층 FK·게시 비용은 전체 본 수로 정규화한다. 워커
QPC가 스레드 선점까지 세는 문제 때문에 단일 비정상 표본이 다음 프레임
등급을 흔들지 않도록 EMA 입력 폭을 직전 기대값의 0.5~1.5배로 제한한다.
L6은 2/4틱 샘플과 중간 보간 비용의 평균을 합산하고, 다수 캐릭터의
후속 샘플 위상을 분산한다. `AnimationFrameMetrics`에는 예산·예측·실측
태스크 시간, 초과 여부, 등급별 개수를 기록한다. 이 시간은 태스크 본문
QPC 합계이며 전체 Update 벽시계·스케줄러 대기·장면 게시 비용과
혼동하지 않는다. 그 비용과 Animator당 1개 Job은 S6의 청크화 대상이다.

Release CreatorRobot 100체에서 `measure-animation-budget.ps1`로 예열
30프레임 뒤 120프레임씩 독립 3회, 기본값 반영 후 1회, 최종 소스에서
1회를 측정했다. 기본 4.0ms는 **0/600프레임 초과**, 예측 대 태스크
실측의 프레임별 절대 오차 중앙값은 회차별 6~9%였다. 1.0→2.0ms 두 배
변경 시 평균 등급은 다섯 회 모두 좋아졌다. 1.0ms는 이 장면의 포즈 비용
하한 부근이라 487/600프레임을 초과했다.
이는 스케줄러가 고칠 수 없는 포화 상태로, 기본 예산의 완료 판정에
섞지 않는다. 원본은 `Build/Obj/Phase13S5/Budget-41be1b953fd7428a86c1eaca7109f529`,
`Budget-22199e326a0f4a88af42b1405d6cc3fc`,
`Budget-4e68252500804e618f0f0cb0f11dc73a`,
`Budget-d0f4b361803c45ec9712217b4f84220b`,
`Budget-1296d6e542644c66894827364a6c3a6e`다. OS 선점 표본이
포함된 단일 프레임의 95백분위 오차는 15%를 넘으므로 완료 기준의
예측 오차는 중앙값으로 판정하고 원본 p95도 함께 보존한다.

Debug/Release Editor 제품 재생 회귀는 CreatorRobot 100체×12회 각
234,824검사·관리 스레드 오류 0건을 통과했다. 계측 프로브가 노출한
종료 시 렌더 스레드의 해제된 광원 프록시 접근은 Editor 자산·장면
해체 전에 렌더 스레드를 join하도록 종료 순서를 바로잡아 재현이
사라졌다. Vulkan 제품 교차 검증은 결정대로 후속 통합 게이트에 둔다.

## 45. S6 완료 — 워커 수 기준 청크와 SceneRuntime 소유 (2026-09-27)

`AnimationJob`을 `Engine/SceneRuntime/AnimationScheduler.{h,cpp}`로 옮겼다.
`SceneManager`가 스케줄러를 소유하고 `Animator`의 씬 편입·이탈 훅이
직접 등록한다. `RenderScene`의 실행기 멤버·등록 전달·헤더 참조를 제거했다.
렌더 계층은 게시된 팔레트만 소비하며, RenderEngine 소스·프로젝트에서
`AnimationJob`/`AnimationScheduler` 헤더·호출 참조는 0건이다. 리소스 창의
Animator 수는 SceneRuntime 등록부에서 읽는다.

Update와 Execute는 여전히 별도 제출·join 구간이고, 그 사이의 전체 레시피
예산 결정과 그 뒤의 직렬 포즈·소켓 게시 순서를 유지한다. 각 제출은
`min(대상 수, 워커 수×2)`개의 인덱스 청크다. 공용 `job_scheduler::submit_indexed`
는 인덱스별 `std::function` 벡터를 만들지 않고 한 콜백을 enkiTS 배치에
전달한다. 각 청크는 여러 Animator를 처리하며, 실패한 Animator가 있어도
같은 청크의 나머지를 시도한 뒤 첫 예외를 배리어에서 전달한다. 작업·타이밍·
예산 순서·Execute 인덱스 벡터는 스케줄러가 보유해 프레임 사이 재사용한다.
워커별 포즈 저장소와 컴포넌트별 측정 슬롯은 기존 계약을 유지한다.

8워커 Release CreatorRobot 계측에서 10/50/100체의 Update·Execute 제출
Job은 각각 **10/16/16개**, 실제 평가 Animator는 **10/50/100개**였다.
Debug/Release 100체×12회 제품 회귀는 각 234,824검사·관리 스레드 오류
0건으로 통과했다. Release 100체 기본 4ms 예산은 120프레임 초과 0건,
예측 오차 중앙값 8%였다. 공용 스케줄러 Debug/Release 인덱스 배치·예외·
캡처 해제·배리어 변이 회귀가 통과했고, 새 Release Player 바이너리로
cooked 애니메이션 씬 실행도 `ANIMATION_PLAYER_OK samples=2`를 통과했다.
원본은 `Build/Obj/Phase13S6/{Baseline-Debug,Baseline-Release,Product-Debug,
Product-Release,Player-Release}`와
`Build/Obj/Phase13S5/Budget-4452e5eb3ef04ea58221a055edfd3a1f`다.
Player 링크의 LNK4020은 일부 디버그 타입 기호가 손상됐다는 경고로,
실행 검증과 별개로 빌드 기호 품질을 추후 점검해야 한다. 전체 프레임 지연의
 성능 향상은 이 수치만으로 주장하지 않는다. S7은 HUD·회귀 세트 편입,
 Vulkan은 결정대로 후속 통합 검증이다.

## 46. S7 완료 — 실시간 예산 HUD와 제품 회귀 묶음 (2026-09-27)

ProfilerWindow에 라이브 Animation Budget 페이지를 연결했다. 스케줄러는
화면이 열려 있는 동안 요청을 갱신하고 500ms 동안 갱신이 없으면 캡처를
멈춘다. 캡처 프레임의 작업 배리어 뒤에
읽기 전용 스냅샷을 게시한다. 등록·실제 평가·강등 수, L0~L7 분포,
예산/예측/실측 CPU 비용, Animator별 등급·비용·사유를 표시한다. 이
페이지는 선택된 `.ceprof` 파일과 독립적인 현재 씬 자료임을 화면에 명시한다.
예산으로 추가 강등된 경우와 화면 중요도에 따른 강등은 사유를 구분한다.

선택한 Animator의 최종 레시피는 워커가 실제 방문한 인덱스를 기록한다.
배리어 뒤에 태스크 종류·의존·클립·도달성·실행 순서·논리적 버퍼 소유자를
복사하며, 워커 풀/저장소와 인스턴스 포즈 저장소의 주소·버퍼 수를 함께
보여 준다. 미도달 태스크에는 실행 순서를 부여하지 않는다. HUD가 꺼지면
이 인덱스 기록과 스냅샷 복사 비용이 발생하지 않는다.

`Tools/regression/verify-animation-s7.ps1`은 독립 레시피 회귀,
CreatorRobot 100체 제품 씬(관리 코드 이벤트 순서·개수·스레드와 소켓
부착물 월드 위치 검사 포함), 1/2/4ms CPU 예산 120프레임 검증을 묶는다.
픽셀 회귀는 `-IncludeVisual`을 지정한 경우 이 기능 검사 뒤에 실행한다.
제품 회귀에는 HUD의 100체 집계, L7 강등 사유, 실제 실행 순서와
버퍼 소유 검사도 포함한다.

Debug/Release Editor 빌드, 두 설정의 독립 레시피 회귀 각 7,260검사,
100체×12회 제품 회귀 각 234,834검사·관리 스레드 오류 0건이 통과했다.
예산 120프레임의
1/2/4ms 초과 프레임은 각각 105/1/1, 예측 오차 중앙값은 각각
약 15/11/9%다. 기본 4ms의 1/120 초과는 계획의 1% 미만 기준을
충족한다. 원본은
`Build/Obj/Phase13S7/Regression-ee15e8f48d8948018accb362fd2a1aaa`에
있다. 이 수치는 HUD 연결의 성능 향상을 뜻하지 않는다.

Release DX12 픽셀 회귀는 16개 포즈 캡처·318검사, IK 회귀는
8개 캡처·114검사로 통과했다. 접촉 시트도 확인했다. 원본은
`Build/Obj/Phase13S7/{Visual-Release-20260927,IKVisual-Release-20260927}`다.
이 검사는 게임 뷰 포즈·부착물이며 새 Profiler HUD의 배치나 갱신을
보여 주지 않는다. 별도로 Release Editor 실제 창의 SampleScene에
CreatorRobot을 배치해 HUD를 확인했다. 편집 프리뷰에서 등록 1·평가 1·L0,
예측/실측 비용과 Sample clip → Materialize → Output의 실제 실행 순서,
`worker current -> instance pose` 버퍼 소유가 갱신됐다. 실행 모드에서
로봇을 카메라 밖 X=100으로 옮기자 L7 1·강등 1·`Outside camera`·
`Skipped`가 나타났다. 한 행일 때 과도한 빈 표 높이, 잘린 비용/결과/버퍼
소유 열은 수정하고 최종 빌드에서 전체 문구를 확인했다. Editor Release
실행 파일을 다시 빌드해 통과했다. Vulkan은 기존 결정대로 후속 통합
검증에서 수행한다.

이 육안 검증 중 File 메뉴의 `Load Scene`으로 `Test1.creator`를 열면
`SceneManager::DrainSceneLoads`의 소유 스레드 검사에서 에디터가 종료되는
별도 결함을 발견했다. HUD 검증은 Content Browser에서 모델을 직접 배치해
완료했으며, 씬 열기 경로의 수정은 별도 작업이다.

## 47. Phase 13 AnimationGraph 확장 계획 및 공수 (2026-09-27)

첨부 제안의 목표는 **Animator의 C#/Editor 사용성은 유지하고, 상태가 클립
인덱스 대신 Pose를 생성하는 그래프를 가리키게 하는 것**이다. 이 절은 후속
설계·착수 순서를 고정한다. 2026-09-27 확정: AG0~AG5를 PHASE 13 확장
완료 범위에 편입한다. S7만 끝나서는 PHASE 13을 닫지 않는다. S8
바이트코드 VM은 여전히 중단 상태다.

### 47.1 현재 코드와 제안의 차이

| 제안 항목 | 현행 코드 판정 | 후속 경계 |
|---|---|---|
| Animator façade, C# `SetBool/Float/Int/Trigger` | `ScriptCore/Animator.cs`와 `ClrHost.cpp`에 이미 있음. `Play`/`CrossFade`는 없음 | 기존 이름 기반 API 유지. ID 기반 C++ 내부 경로는 별도 추가 |
| 자산/인스턴스 분리 | 클립·스켈레톤은 불변 `ModelAssetGeneration`; 재생 시간·FSM·커서·포즈는 시스템 소유 `AnimInstance`/`ControllerPlayback`으로 이관 완료 | 공유 **Graph 정의**는 아직 없고, `AnimationController`·`AnimationState`·`ConditionParameter`는 Animator의 가변 저작 객체 |
| Pose/작업 실행 | SoA `LocalPose`, `AnimTaskList`의 평면 선행 인덱스 의존·출력 도달성, 작업자별 포즈 저장소, Update/Execute 분리 완료 | 새 프론트엔드가 같은 태스크 계약으로 Pose를 생성해야 함 |
| 레이어·가산·마스크·IK | Controller 레이어의 masked/additive 태스크와 수동 두 본 IK·본 보정 태스크가 있음 | 그래프 노드·슬롯·일반 IK 저작 모델은 없음 |
| State와 Parameter | `AnimationState::AnimationIndex`가 직렬화·전이·레시피를 묶음. 배속 파라미터만 index/version 캐시; `TransCondition`은 포인터/이름 및 객체 순회, C# 호출은 이름 탐색 | `State.rootPoseSource`와 타입별 `ParameterId`/컴파일된 조건으로 확장 |
| 이벤트 | 클립 이벤트는 Animator별 오버라이드이고 포즈 결과와 별도로 시간 구간에서 판정해 기존 C# 메시지 큐로 전달 | 블렌드 스페이스·슬롯에서 활성 클립별 커서/중복 정책 정의 필요. 새 이벤트 큐를 중복 도입하지 않음 |

첨부가 지적한 `Animator`/`AnimationController`의 시간·블렌드 필드는
S3′에서 시스템 소유 인스턴스로 옮겼다. 다만 직접 클립 재생용
`AnimInstance` 제어값과 Controller별 `ControllerPlayback`은 별도 경로로
남아 있으므로 Graph 바인딩 때 의미를 통합해야 한다. TRS Pose, Pose 버퍼 풀,
레이어 합성, 작업 병렬화는 S2′~S6에서 이미 구현했다. 반대로
`AnimationGraphAsset`이 존재하거나 `AnimationState`가 Pose를 반환한다는 판정은
현재 코드에 맞지 않는다. `AnimTaskList`도 **프레임별 실행 레시피**이지 저장된
Graph Asset 또는 Graph Compiler 결과물이 아니다.

### 47.2 후속 구조 결정

```text
Editor/씬 저작 → Graph 정의(불변, 버전·자산 신원·노드 ID)
                         ↓ 바인딩/검증
Animator façade → AnimInstance(기존 시스템 소유 세대 핸들)
                   ├ Parameter 값·FSM/노드 재생 상태·슬롯 상태
                   └ 프레임별 AnimTaskList → 기존 Executor → LocalPose/팔레트
```

1. `Animator`는 씬 컴포넌트와 C# 표면으로 남긴다. 새
   `AnimationGraphInstance`가 별도의 포즈·시간 저장소를 소유하지 않는다.
   기존 `AnimInstance`에 Graph 바인딩과 노드별 런타임 슬롯을 넣어 주소 고정
   페이지·DDOL·등록/해지 계약을 재사용한다.
2. Graph 정의는 불변 공유 자산이다. 노드 연결, State/Transition, Parameter
   스키마, 마스크, 클립 **자산 참조**만 둔다. 재생 시간·트리거·포즈 버퍼·커서는
   들어가지 않는다. `AnimationIndex`는 기존 씬을 읽는 호환 필드로만 쓰고,
   바인딩 시 안정적인 클립 신원과 generation을 검증한다. 모델 재게시·Graph
   변경 시 노드/파라미터 ID와 캐시 무효화 정책을 먼저 확정한다.
3. State는 `rootPoseSource`를 가리킨다. 최초 소스는 Clip 하나이며 기존
   `AnimationIndex` 씬을 이 소스로 변환한다. 이후 Blend1D/2D와 Slot을 붙인다.
   `AnimationController`는 이행 기간의 씬 저작/편집 컨테이너이자
   StateMachine 프론트엔드로 남기고, 실행기에서 직접 클립 인덱스를 읽는
   경로를 단계적으로 없앤다.
4. 노드의 출력 계약은 `PoseHandle` 같은 **레시피 내 작업 인덱스**다. 기존
   `AnimTaskList`/작업자 포즈 풀을 그대로 소비한다. 별도 `std::vector<Pose>`를
   노드마다 만들지 않는다. Graph Compiler의 첫 산출물은 이 레시피를 기록하는
   바인딩/위상 검증 단계다. 바이트코드·VM은 그래프 순회 비용 실측 또는 자산
   배포 계약의 필요성이 확인될 때 별도로 재개한다.
5. 파라미터 정의는 Graph의 타입/이름/안정 ID, 값은 인스턴스가 소유한다.
   외부 문자열 API와 기존 씬 키는 보존한다. 내부 핫 경로는 ID로 읽으며,
   구조 변경 시 version을 올려 바인딩을 다시 한다. 이름 충돌·타입 변경·없는
   파라미터는 조용한 기본값 대신 저작/바인딩 진단으로 판정한다. 트리거는
   현재 `AnimatorSystem::Update`의 전이 판정 뒤 reset 의미를 보존한다.
6. 전이 조건은 먼저 타입 검사된 ID/비교값 배열로 바꾼다. 임의 표현식
   bytecode는 AND/OR/괄호 저작 요구와 측정이 확인된 뒤 도입한다.
   Root Motion과 Notify는 게임 스레드 적용/전달 정책을 별도로 정하되,
   기존 clip-event의 루프·역재생·순서·join 후 C# 전달 계약을 회귀 기준으로 둔다.

### 47.3 PHASE 13 확장 이행 슬라이스와 공수

공수는 **인일(person-day)의 구현·빌드·지정된 제품 회귀를 합친 점 추정**이다.
AG2의 자산/인스턴스와 AG4의 세 노드 계열은 각각 독립 빌드 슬라이스로
나눴다. S2′~S6 기반을 재구현하는 비용은 세지 않는다.

| ID | 구현 | 검증/완료 판정 | 공수 |
|---|---|---|---:|
| AG0 | 현행 씬/Prefab/Editor/C# 호출·자산 신원·태스크 재작성 규칙의 기준선 고정. Graph/노드 ID와 직렬화 버전·실패 정책 설계 | 기존 100체·레이어·이벤트·소켓·DDOL·Player 및 S7 스냅샷 기준선. 단순 정적 조사와 실제 제품 실행을 구분 기록 | 2일 |
| AG1 | Clip PoseSource와 State root 연결; 구 `AnimationIndex` 로드 어댑터. 기존 단일 클립/전이가 동일 `AnimTaskList`를 생성 | 구 씬 저장/재로드, clip 교체·generation 변경, 포즈/이벤트/소켓 동등성, L0~L7 태스크 도달성 및 버짓 동등성 | 4일 |
| AG2a | 노드/State/Parameter 스키마와 불변 Graph 정의를 자산 신원·저장·cook 경계에 연결 | 같은 정의의 저장·재로드·Player 사용, 누락 클립/순환/스키마 오류 진단, 공유 정의 런타임 쓰기 0 | 5일 |
| AG2b | 기존 `AnimInstance`에 노드/FSM 상태와 Graph 바인딩·재바인딩. 현 Controller 저작값 변환; 별도 인스턴스 저장소 신설 금지 | 같은 Graph를 공유하는 100체에서 상태·트리거·커서 격리, Editor 재저작/모델 재게시, DDOL·해지/재등록, cooked Player | 5일 |
| AG3 | 타입별 ParameterId와 전이 조건 바인딩; 기존 C# 이름 표면 연결 | 구조 변경·이름 변경·타입 오류·트리거 1회 소비, managed 호출/게임 스레드 경계, 핫 경로 이름 탐색 0 | 4일 |
| AG4a | Blend1D/2D 소스와 가중 샘플·시간/커서 정책을 태스크로 기록 | 경계·역방향·다중 클립 이벤트, 비활성 분기 샘플 0, L0~L7·버짓 비용 갱신 | 4일 |
| AG4b | Controller 마스크/가산을 LayerBlend/BoneMask/Additive 노드로 이전 | 현 씬 레이어 포즈·소켓·이벤트 동등성, 버퍼 상한·L2/L3 재작성·화면 회귀 | 3일 |
| AG4c | Slot/one-shot 재생·중단·교차 페이드와 C#/C++ `Play` 표면 | 이벤트 중복/누락·L4 전이 복귀, Editor/C#/cooked Player 제품 경로 | 5일 |
| AG5 | PoseCache/일반 IK·Root Motion/Notify의 구현 계약과 후속 슬라이스 공수 확정; Graph compile/VM 타당성 측정 | 자산 배포·런타임/Editor 분리와 기존 태스크 실행기 대비 바인딩/실행 비용 기록. VM 착수는 별도 결정 | 4일 |
| **합계** | **AG0~AG5 확장 계획 공수** | **AG5는 계약·계측 범위이며 나열된 후속 기능 구현은 별도** | **36일** |

산정 축: AG0~AG1은 기존 `AnimationState::AnimationIndex`와
`AppendClipTasks`의 구 씬 어댑터, AG2a/b는 `Animator` 수동 직렬화·
`ModelAssetGeneration`/cook·시스템 소유 페이지와 재게시 수명,
AG3은 `ConditionParameter`/`TransCondition`·C ABI·C# 호출,
AG4a~c는 레이어·LOD L2~L4의 현 태스크 모양 의존과 제품 픽셀/이벤트
게이트, AG5는 구현 전에 결정할 부가 기능과 Compiler의 실측을 각기 센다.
AG0에서 자산 포맷·Editor 충돌 및 baseline 실행 시간을 확인하고
AG2a/b·AG4c 추정을 갱신한다. **Full Graph Editor/Lattice 창 연결,
PoseCache·일반 IK·Root Motion·Notify의 구현, VM/바이트코드,
Vulkan 교차 검증은 36일에 포함하지 않는다.**

**선행·충돌:** PHASE 13은 S7과 AG0~AG5의 위 완료 판정을 모두 충족해야 닫는다. AG 착수는
S7 스냅샷 기준선을 고정한 뒤 한다. `AnimationScheduler.cpp`의
`AppendClipTasks`/`PreparePose`, `AnimationController.cpp`,
`AnimationState.h`, `Animator` 직렬화, `ScriptCore/Animator.cs`,
`ClrHost.cpp`, Editor Animator 창이 직접 접점이다. 현재 L2/L3 레이어 제거와
L4 블렌드 스냅은 특정 태스크 모양/Controller 슬롯을 가정하므로 AG1부터
레시피 의미를 보존하거나 재작성 규칙을 함께 일반화해야 한다.
Lattice의 Animator 창 이관은 Graph 저작 UI에 앞서는 별도 LX 게이트이며,
이 설계가 UI 이관 완료를 뜻하지 않는다. Vulkan 제품 교차 판정도 기존
후속 통합 게이트에 남는다.
