# 애니메이션 스케줄러 · LOD · CPU 버짓 재설계 (PHASE 13)

2026-08-11 수립. 애니메이션 런타임을 전수 추적한 결과, 본 행렬 계산이 **문자열
키 트리 탐색 위에서, 공유 애셋에 쓰기 경합을 일으키며, 화면 밖 캐릭터까지
전부 풀틱**으로 돌고 있다. 이 페이즈는 언리얼 **Animation Budget Allocator**
구도를 이 엔진의 기존 인프라(WorkerPools · gCPUProfiler · 미배선 LOD 유틸)에
맞춰 이식한다. 목표 사슬:

```
AnimInstance(포즈 상태 분리) → Significance(거리·화면·가시성)
 → AnimationScheduler(CPU 버짓 배분) → 틱 정책(매 프레임 / 1/2 / 1/4 / 동결)
 → Job 배치(청크 분할) → 포즈 산출 → 이벤트 큐(메인 스레드 발화) → 팔레트
```

원칙 하나를 먼저 박는다: **시간은 항상 전진하고, 건너뛰는 것은 포즈 계산뿐이다.**
`curAnimationProgress` 기반 게임플레이 판정(`endAnimation` 등)과 키프레임
이벤트는 틱 레이트와 무관하게 보존된다 — 언리얼과 같은 결정이다.

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
| R3 | **죽은 참조 하나가 나머지 전부를 건너뛴다.** 애니메이터 순회 중 `if (animator == nullptr) return;`(`:72`) — `continue`가 아니라 `return`. 컨트롤러 검사(`:79-83`)도 동일 | AnimationJob.cpp:72,82 |
| R4 | **`CurrentKeyIndex`가 -1을 반환하면 `keys[-1]`.** 비루프 클립이 duration에 클램프될 때 time이 마지막 키 시간을 넘으면 -1이 그대로 인덱스로 쓰인다 | AnimationJob.cpp:20-32,654-660 |
| R5 | **연산자 우선순위 버그.** `if (!StateVec.size() >= 2)` = `(!size) >= 2` = 항상 거짓 — 기본 상태 확정 분기가 죽어 있다 | AnimationController.cpp:60 |
| R6 | **워커 스레드에서 C# 키프레임 이벤트 발화.** `InvokeEvent`(`:156,280`)가 스레드풀 람다 안에서 매니지드 콜백 진입 — `ClrHost.h:134` 주석이 자인하는 위험 | AnimationJob.cpp:156,280 |

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
| 논블로킹 폴링 선례 | DX12PSOManager.cpp:548-568 | (참고) 비동기 결과 소비 패턴 |

---

## 2. 설계 — Animation Budget Allocator 구도의 이식

### 2.1 원리 (언리얼에서 가져오는 것)

1. 애니메이션 전체에 **프레임당 CPU 시간 상한(ms)** 을 준다.
2. 인스턴스마다 **significance**(중요도)를 계산한다 — 거리·화면 크기·가시성.
3. 인스턴스마다 **실측 비용의 지수이동평균(EMA)** 을 유지한다.
4. 매 프레임 `Σ(이번에 틱할 인스턴스의 기대 비용)`이 버짓을 넘으면
   significance 낮은 쪽부터 **틱 레이트를 강등**(1/2 → 1/4 → 동결)하고,
   여유가 지속되면 승격한다. 진동은 히스테리시스로 막는다.
5. 틱을 건너뛴 프레임은 **포즈 보간**(전/현 포즈 lerp)으로 메운다.

### 2.2 구성 요소

**① AnimInstance — 핫 데이터의 분리.** `Animator`/`AnimationController`에서
포즈·시간·커서를 떼어낸 인스턴스 상태. 스케줄러가 SoA 배열로 소유한다.

```
AnimInstance {
    시간 상태(클립별 elapsed · progress)      ← Animator/Controller에서 이관
    키 커서: 채널별 마지막 키 인덱스           ← curKey의 올바른 자리 (E2·R1 해소)
    포즈 버퍼 prev/curr — 실제 본 수만큼        ← 512 고정 은퇴 (E6) · 보간의 전제
    significance · 현재 LOD · 다음 평가 프레임
    비용 EMA(ms) · 마지막 실측(ms)
}
```

`Animator`는 FSM·파라미터·소켓 목록만 남는 얇은 컴포넌트가 된다. 공유 애셋
(`Skeleton`·`Animation`·`Bone`)에는 **런타임 쓰기 0** — R1이 구조적으로 소멸.

**② 채널 테이블 베이크.** (Skeleton × Animation)당 1회, 로드 시:
`본 인덱스 → NodeAnimation*` 평탄 배열. 레이어용으로 `본 인덱스 → 기여
컨트롤러 비트마스크`도 함께 굽는다(E1·E4 소멸). 순회는 `m_bones` 평탄 배열을
부모 선행 순서로 도는 단일 루프(E7 소멸 — 부모 선행이 아니면 베이크 시 정렬).

**③ Significance 평가.** 프레임 시작에 인스턴스 전수:

```
significance = f( 카메라 거리(CalculateLODDistance — 첫 배선),
                  화면 투영 높이 비율,
                  프러스텀 포함 여부(GetFrustum — FoliageComponent 방식) )
```

멀티카메라에서는 **활성 카메라들에 대한 최대값**을 쓴다(씬뷰+게임뷰 동시 표시
구조 고려). 에디터 프리뷰(게임 미시작)는 significance 고정 1.0 — 저작 중인
캐릭터가 강등되는 일은 없어야 한다.

**④ Animation LOD 정책표.**

| LOD | 조건 | 포즈 평가 | 보간 | 소켓 | 이벤트·시간 |
|---|---|---|---|---|---|
| 0 | 근거리·주요 | 매 프레임 | — | 매 프레임 | 정상 |
| 1 | 중거리 | 1/2 프레임 | prev↔curr lerp | 평가 프레임만 | 시간 전진 · 누적 구간 발화 |
| 2 | 원거리 | 1/4 프레임 | 없음(계단) | 평가 프레임만 | 〃 |
| 3 | 화면 밖 | **동결** | — | 동결 | 시간만 전진 · 이벤트는 계속 발화 |

LOD3 복귀(재가시) 시 즉시 풀 평가 1회로 포즈를 맞춘다. 블렌드(전이) 중인
인스턴스는 한 단계 상향 보정 — 전이 팝이 가장 눈에 띄는 아티팩트다.

**⑤ CPU 버짓.** 설정값(기본치는 S1 실측 후 확정, `EngineSetting` 편입).
알고리즘: significance 내림차순 정렬 → LOD 정책이 주는 기본 주기로 이번 프레임
평가 대상을 뽑으며 기대 비용(EMA)을 누적 → 버짓 초과 지점부터 주기 강등.
승격은 여유가 N프레임 지속 + 경계 significance ±ε의 이중 히스테리시스.

**⑥ Job 배치.** 애니메이터 1개=태스크 1개(E9) 대신, 이번 프레임 평가 대상을
**워커 수에 맞춘 청크**로 분할해 던진다. 청크 안에서 인스턴스별 QPC 측정 →
EMA 갱신. fork-join 위치는 현행 유지(`InternalAnimationUpdateEvent` 단계) —
파이프라인 재배치는 이 페이즈 밖.

**⑦ 이벤트 큐.** 워커는 `(animator, event, progress구간)`을 락프리 큐에 적재만
하고, join 직후 **메인 스레드에서 일괄 발화**(R6 소멸). C# 경계는 메인 스레드
고정. 틱을 건너뛴 프레임의 이벤트는 다음 평가 때 진행 구간 `[pre, cur]`로
일괄 판정되므로 유실되지 않는다.

### 2.3 이 엔진에 맞춘 결정

- **새 코드의 자리는 `ScriptBinder\AnimationScheduler.{h,cpp}`.** 헤더는
  RenderEngine에, 구현은 ScriptBinder에 두는 현 `AnimationJob`의 계층 기형을
  반복하지 않는다. `RenderScene`은 팔레트(결과)만 소비하고 스케줄러를 소유하지
  않는다 — PHASE 4(커플링 절단)의 방향과 일치.
- **전용 풀 vs `WorkerPools`는 실측으로 결정한다(S6).** 전용 8스레드는 코어
  점유가 낭비지만, 전역 풀은 UI 렌더 데이터·지형 로드와 경합한다. S1 계측이
  양쪽 비용을 보여준 뒤에 정한다 — 지금 단정하지 않는다.
- **팔레트 전달 계약은 유지.** `m_palleteMap` + 더티 플래그 + `ProxyCommand`
  복사 경로는 건드리지 않되, 복사 크기만 실제 본 수로 줄인다(E6). DX12
  스키닝 패스는 수정 없음.
- **`Mesh::SelectLOD`(메시 LOD)는 이 페이즈 밖.** 같은 significance 입력을
  쓰게 될 미래의 소비자로만 기록해 둔다 — 여기서 배선하면 페이즈가 렌더
  LOD로 번진다.

---

## 3. 단계

| ID | 슬라이스 | 내용 | 완료 기준 | 공수 |
|---|---|---|---|---|
| S0 | 결함 지혈 | R1~R6. `curKey` 죽은 캐시 삭제 · R2 `find`+스킵 · R3 `continue` · R4 클램프 · R5 괄호 · R6 간이 이벤트 큐(수집→join 후 발화). `Bone` 트랜스폼 쓰기는 소비자 grep 후 제거 또는 S3로 이관 명시 | 같은 모델 다수 씬에서 공유 애셋 쓰기 grep 0 (curKey·map 삽입) · 이벤트가 메인 스레드에서만 발화 | 1.5일 |
| S1 | 계측 기선 | `PROFILE_CPU_SCOPE("Animation")` 단계 계측 + 인스턴스별 QPC. 캐릭터 N체(10/50/100) 비용 곡선 실측 → 버짓 기본값 근거 확보. 검증 씬 신설 | 비용 곡선 수치가 이 문서 §1에 추가 기록됨 | 1일 |
| S2 | 데이터 정지 작업 | 채널 테이블·레이어 비트마스크 베이크(E1·E4) · 평탄 순회(E7) · 키 커서: 이진 탐색 + 인스턴스 캐시(E2) · SRT 채널 블렌드(E3) · 매 프레임 할당 제거(E5) · 배속 파라미터 핸들화(E10) | 핫 패스에서 문자열 조회 0 · 프레임당 힙 할당 0 · S1 대비 비용 곡선 재실측 개선 확인 | 3일 |
| S3 | AnimInstance 분리 | 핫 데이터 이관 · 포즈 prev/curr 실본수 버퍼 · 팔레트 복사 실본수화 · `Bone` 런타임 쓰기 완전 소멸 · 파일 전역 `m_currAnimator` 은퇴 | 공유 애셋 런타임 쓰기 0 · `Animator` sizeof에서 64KB 배열 2벌 제거 | 3일 |
| S4 | Significance + LOD | 거리·프러스텀·화면비 배선(§2.2③) · LOD 정책표 구현 · 보간 · 재가시 복귀 · 에디터 모드 예외 | 화면 밖 캐릭터의 포즈 계산 비용 ≈ 0 (계측으로 판정) · LOD 전환 팝 없음(전이 상향 보정 동작) | 3일 |
| S5 | CPU 버짓 + 스케줄러 | EMA 비용 모델 · 버짓 적합 알고리즘 · 이중 히스테리시스 · `EngineSetting` 설정값 | 100체 씬에서 버짓 상한 준수(초과 프레임 1% 미만) · 버짓 2배 변화에 틱 분포가 단조 반응 | 3일 |
| S6 | Job 배치 전환 | 청크 분할 · 전용 풀 vs WorkerPools 실측 비교 후 결정 · 이벤트 큐 정식화(락프리) · `AnimationScheduler`로 개명·이주(계층 정위치) | 태스크 수 = O(워커 수) · RenderEngine에 애니메이션 헤더 잔존 0 | 2일 |
| S7 | HUD + 회귀 | ProfilerWindow 버짓 패널(등록/틱/강등/동결 수 · 버짓 대비 실측 ms · LOD 분포) · 검증 씬을 회귀 세트에 편입(pwsh) | 패널에서 강등이 실시간 관측됨 · 회귀 세트 통과 | 2일 |

합계 ≈ 18.5일. S0·S1은 즉시 착수 가능하고 다른 페이즈와 충돌하지 않는다.
S2 이후는 `AnimationJob.cpp`를 크게 다시 쓰므로 **동시 세션 주의 대상**
(공유 워크트리 — 착수 직전 HEAD 재대조).

---

## 4. 페이즈 완료 기준

1. **정확성** — 공유 애셋(`Skeleton`·`Animation`·`Bone`)에 런타임 쓰기 0.
   같은 모델 100체 씬에서 크래시·포즈 오염 없음. 키프레임 이벤트는 메인
   스레드에서만, 유실·중복 없이(회귀 세트로 판정).
2. **비례성** — 애니메이션 비용이 (평가한 인스턴스 × 실제 본 수)에 비례.
   문자열 조회·프레임당 힙 할당·행렬 분해 블렌드 0.
3. **버짓** — 설정 상한을 넘는 프레임 1% 미만. 화면 밖 캐릭터 비용 ≈ 0.
   버짓 값 변경이 틱 분포에 단조 반영.
4. **관측** — HUD 패널에서 인스턴스별 LOD·비용·강등 사유가 보인다.

## 5. 다른 계획과의 관계

| 계획 | 관계 |
|---|---|
| PHASE 4 (커플링 절단) | `AnimationJob`의 RenderEngine 헤더/ScriptBinder 구현 기형이 S6에서 해소 — 간선 감소에 기여, 래칫 게이트 통과 필수 |
| PHASE 9 (생명주기) | 스케줄러는 `InternalAnimationUpdateEvent` 델리게이트 단계를 그대로 쓴다 — 델리게이트 은퇴가 이 단계에 오면 그때 이관(이 페이즈에서 선제 이동 안 함) |
| MultiCameraRenderPlan | significance는 활성 카메라 최대값 — 뷰별 시간축 상태 원칙(잔상 교훈)과 충돌 없음(포즈는 뷰 무관 단일) |
| PHASE 12 (빌드) | 무관 — 파일 충돌만 회피 |
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
  런타임 값을 읽고 있을 수 있다 — S0에서 grep 전수 후 S3 이관 목록에 명시.
- **공유 워크트리 동시 커밋.** `AnimationJob.cpp`·`Animator.h`는 게임 스크립트
  가 넓게 물고 있는 헤더 — 슬라이스 착수 직전 HEAD 재대조, 한 슬라이스 한 커밋.
