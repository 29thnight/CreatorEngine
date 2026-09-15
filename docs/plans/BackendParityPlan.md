# 백엔드 패리티 계획 (PHASE 4.9)

**신설 2026-09-15 · 미착수 · 선행 PHASE 4 · 소유: DX12/Vulkan 교차 판정**

> **이 계획이 존재하는 이유 한 줄.** PHASE 4 의 판정을 DX12 로 좁히면서
> ([`PBRWiringStabilizationPlan.md`](PBRWiringStabilizationPlan.md) §20, 2026-09-15 사용자
> 결정) 미룬 것들을 **잃지 않고** 한자리에 모은다. 미룬 것은 **판정**이지 **배선**이
> 아니다 — RHI 중립 어휘와 Vulkan 백엔드 구현은 PHASE 4 에서도 계속 양쪽을 채운다.

---

## 0. 범위 선언 — 무엇이 이 계획 것이고 무엇이 아닌가

| | 소유 | 근거 |
|---|---|---|
| DX12/Vulkan 제품 프레임 캡처 1:1 픽셀 대응 | **이 계획** | 시각 고정이 선행이라 PHASE 4 에서 성립하지 않았다 |
| `render.pbr.compare` 를 판정으로 되돌리기 | **이 계획** | 지금은 `gated:false` 로 수만 남긴다 |
| PBR 게이트의 vulkan 회차 복구 | **이 계획** | `-Backend dx12,vulkan` 기본값 복귀 |
| `vk.*` 4종(DX12/Vulkan 대조) 복구 | **이 계획** | `-IncludeVulkanSelfTest` 기본값 복귀 · §2.5 |
| **DX12 전용 deferred 검사 신설** | **이 계획** | 위를 끄면서 생긴 구멍이다 · §2.5 |
| Vulkan 기동 창 `gCubeMap` 결함 | **이 계획** | PHASE 4 와 무관한 별건인데 게이트를 끝까지 못 가게 막았다 |
| RHI 중립 어휘(enum·변환표) | PHASE 4 (와 이후 모든 페이즈) | 어휘 구멍은 그 자리에서 막는다. 여기로 미루면 빚이 된다 |
| 셰이더 언어·재질 의미 | PHASE 4 | 이미 닫혔다 |

**착수 전 한 줄.** 이 계획은 아직 **공수 산정이 없다**. 착수할 때 §1 의 선행 조건부터
실측하고 슬라이스를 끊는다 — 추정으로 표를 채우지 않는다.

---

## 1. 선행 조건 — 시각 고정이 먼저다

교차 백엔드 픽셀 판정은 **지금 하네스로는 성립하지 않는 질문**이다. PHASE 4 §15 의 실측:

- 같은 백엔드끼리도 Δt 만 벌어지면 허용치를 넘는다. 원인 셋을 짚었다 —
  구름 그림자, 포그의 직전 프레임 혼합(`mPreviousFrameBlendFactor`), SSGI 누적.
- 시간차 3.11s 가 58k 인데 5.68s 교차가 242k 다. **시간만으로 다 설명되지 않는 잔차**가
  있지만, 시각을 고정하기 전에는 그 잔차를 분리할 수 없다.
- **캡처는 시뮬레이션 시각을 고정할 수단이 없다** — `time.*` 명령이 존재하지 않는다.

그래서 PHASE 4 는 허용치를 늘려 초록으로 만드는 길을 거부했다. 그 판단은 유지한다.

> **P0. 시뮬레이션 시각 고정 수단을 만든다.** 프레임을 고정 Δt 로 전진시키고 캡처가
> 그 시각을 명시적으로 잡을 수 있어야 한다. 이것이 없으면 이 계획의 나머지는
> 착수할 수 없다.

시각을 고정한 뒤에야 "잔차가 진짜 백엔드 차이인가"를 물을 수 있다.

---

## 2. Vulkan 기동 창의 `gCubeMap` — 별건 제품 결함

`vulkan-primitives` 가 GPU 검증으로 실패한다. `gCubeMap`(Set 0, Binding 100)이
**기록되지 않은 디스크립터 셋**으로 그려진다.

- **기제.** `VulkanEncoder::SetBindings` 가 이미지 뷰 생성(`GetOrCreateCubeView`)에
  실패하면 pending 바인딩을 넣지 않고 그대로 돌아가는데, `Draw` 는 진행한다.
- **창.** 경고는 stdout 앞쪽 구간에만 몰리고 그 뒤로 멎는다. 캡처 1(frame 2040)만 그
  창 안이고, 캡처 2~5(frame 2076 이후)는 전부 통과한다.
- **PBR 배선과 무관하다.** 경고는 sampler fixture 적재 **전에** 나고, sampler 축이
  갈리는 캡처들은 모두 통과한다.

★ **게이트의 `wait` 를 늘려 이 창을 피하지 않는다.** 그러면 재는 척만 하게 된다.
고칠 것은 "실패한 바인딩을 안고 draw 가 진행한다" 는 것 자체다 — 실패했으면
fail-closed 로 draw 를 생략하고 이유를 남기는 편이 PHASE 4 §2 의 규약과도 맞는다.

---

## 2.5. `vk.*` 4종을 끄면서 생긴 구멍 — 갚을 순서의 맨 앞

`vk.shadow`·`vk.gbuffer`·`vk.forward`·`vk.deferred` 는 **이름이 범위를 속인다.**
vulkan 단독 검사가 아니라 **DX12/Vulkan 대조** 검사다 — `RunVulkanGBufferTest` 안에
`dx12Capture` 와 `vkCapture` 가 나란히 있고, 각 로그의 첫 줄이 그렇게 말한다.

```
── Shadow 패스 — DX12/Vulkan depth-array·mesh 대조 ──
── 제품 GBuffer — DX12/Vulkan Standard Material batch b2·MRT 대조 ──
── Forward+ 패스 — DX12/Vulkan P2d-e legacy retirement + required assets 대조 ──
── Deferred 패스 — DX12/Vulkan GBuffer consume·fullscreen 대조 ──
```

그래서 끄면 **DX12 팔도 함께 꺼진다.** 2026-09-15 결정은 그것을 알고 내렸고,
아래가 실제로 어두워진 것이다.

| 어두워진 것 | DX12 전용 대체 | 상태 |
|---|---|---|
| Shadow depth-array·mesh | `dx12.shadowquality` | **축이 다르다**(경사 편향·캐스케이드 블렌딩) |
| GBuffer MRT5·texture·sampler·mesh | `dx12.gbuffer` | PBR 게이트가 이미 돌린다 |
| Forward+ legacy retirement·required assets | `dx12.forwardshade` | PBR 게이트가 이미 돌린다 |
| | `dx12.forward` | 존재하나 **PBR 게이트가 안 건다** — 값싼 보강 후보 |
| **Deferred GBuffer consume·fullscreen** | **없다** | `dx12.deferred` 라는 명령이 존재하지 않는다 |

> **P1. DX12 전용 deferred 검사를 세운다.** 마지막 줄이 이 결정의 실제 비용이다.
> 이 계획이 착수하면 여기부터 갚거나, 그 전에라도 따로 세울 수 있다 — 교차 백엔드가
> 아니라 DX12 단독 검사라서 §1 의 시각 고정을 기다릴 필요가 없다.

★ 미룬 축을 적을 때는 **무엇을 잃었는지까지** 적는다. "미뤘다" 만 적으면 뒷사람이
되돌릴 근거를 잃는다. 게이트의 축 회계도 이 손실을 문장으로 들고 있다.

---

## 3. 미룬 축 목록 — 게이트가 지금 무엇을 안 재고 있나

`verify-pbr-wiring-baseline.ps1` 은 재지 않은 축을 PASS 로 보고하지 않는다. 축 회계가
`skipped`(이번 기계에 조건이 없다)와 `deferred`(다른 계획이 소유한다)를 가르며, 아래
둘이 `deferred` 로 찍힌다.

| 축 | 회계에 찍히는 이름 | 되돌리는 법 |
|---|---|---|
| 제품 프레임 캡처의 vulkan 회차 | `vulkan/제품 캡처` | `-Backend dx12,vulkan` |
| 교차 백엔드 픽셀 비교 | `compare` | 위와 같음(둘 다 돌면 자동으로 켜진다) |
| `vk.*` 4종 DX12/Vulkan 대조 | `vk/* 백엔드 대조` | `-IncludeVulkanSelfTest` |

★ 미룬 축은 **인자만 보면 알 수 있으므로 실행 맨 앞에서 등록한다.** 처음에는 해당
블록에 닿았을 때 등록했는데, 게이트가 그 앞에서 죽으면 미룬 축이 통째로 사라졌다 —
무엇을 안 쟀는지는 실패했을 때야말로 알아야 한다.

**코드를 지우지 않았다.** `render.pbr.compare` 명령도, vulkan 회차 코드도 그대로 있다.
스위치 하나로 되돌아온다.

---

## 4. 이 계획이 착수할 때 가져갈 기록 (PHASE 4 가 남긴 것)

미루는 것은 "확인한 적 없다" 가 아니다. 2026-09-14 W7 실측에서 두 백엔드는 실제로
같은 답을 냈다.

```
vulkan-samplermodes: draws 18 · bindings 18 · distinct 3 · validation 0 · 충돌/생략 0
세 신원 값이 dx12 와 같다:
  2677747970098898095 · 2987389514037885135 · 13823872880576982764
```

미루는 것은 **그 사실을 매 실행 자동으로 다시 확인하는 일**이다.

관련 기록: PHASE 4 §14(W8 밀봉) · §15(W9 픽셀 판정과 시각 고정) · §19(W7 sampler,
vulkan 기동 창) · §20(이 결정).
