# PHASE 4.5 — 시간축 재구성 계층 · Temporal Upscaling과 Frame Generation

> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 잔여 구현·수용 계획

판정 기준은 위 master의 커밋된 소스다. 작업 중인 로컬 코드 변경은 포함하지 않는다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/TemporalReconstructionPlan.md)

## 2026-10-23 TR3 수용

현재 master `0509753a380866e8dd96bc648fd71c5c5d8792e2`와 보존된 로컬 변경에서 view별 history·SSGI reset·실패 복구를 완료했다. `Begin`의 invalid camera 입력은 기존 history를 보존하고 SSGI는 reset 이전 recording 수락을 세대로 격리한다. Debug/Release 빌드, view state 21검사·native GPU 16비교 및 각 13개 제품 캡처/6쌍 비교가 통과했다. TR3 5/5·5인일 수용. [실행 보고서](../analysis/TemporalTr3History20261023.html).

3개 view 상태와 2개 SSGI 카메라의 교차 실행·독립 reset을 확인했다. SDK 전체 품질·route·해상도·비용은 아래 TR2/TU 잔여 항목에서 계속 추적한다. 기존 10월 20일 master-only 정리는 이 TR3 갱신의 기준과 구분한다.

## 2026-10-23 TR2 수용

기존 temporal snapshot·history·SSGI와 캡처 도구를 활용해 멀티뷰·전체 route·해상도 검증을 완료했다. Debug/Release 각각 native 7-route를 3개 해상도에서 검사하고, FSR/DLSS/XeSS의 5개 품질×3개 실제 viewport를 단독·교차 view로 실행했다. 구성별 SDK 90fixture와 전체 픽셀 6,930비교가 통과했고 동일 Game view의 단독·교차 실행 간 픽셀 차이는 0이다. TR2 4/4·5인일 수용. [실행 보고서](../analysis/TemporalTr2Resolution20261023.html).

SDK는 `ThirdParty/Temporal`에 고정 버전으로 설치하고 검증용 opt-in 빌드로 사용한다. TU0~4의 복잡 제품 장면·성능·배포와 FG 검증은 계속 해당 항목에서 추적한다.

## 남은 작업과 완료 판정

- TR0·TU0~4: sealed 입력·모션 소유권·history reset·품질/AA·fallback을 전체 실제 material route와 SDK 품질 행렬에서 수용한다. TR1/TU5의 완료 구현을 다시 만들지 않는다.
- FG/TFG9: 실제 Player generated output identity·물리 표시 cadence/latency·resize/device loss/teardown 및 장치별 비용을 검증한다. requested/configured/active count와 CPU Present 반환을 실제 생성/표시 증거로 바꾸지 않는다.
- 현재 portable renderFeatures·startup precedence·camera cut revision·profiler provenance/continuous presenter 기록을 기준으로 persistence/cook·실패 복원·계측 무해화를 수용한다.

## 적용할 구조와 경계

`sealed camera/render inputs → temporal provider → successful submission history → Player presenter/provenance`

위 경로는 소스에서 확인한 구현 기반이다. 아래 요구의 수용 결과를 뜻하지 않는다. 구현을 다시 만드는 항목과 제품 검증을 혼합하지 않는다.

## 추적할 잔여 항목

| 추적 ID | 소유 단위 (잔여 작업은 위 목록) | 대시보드 보존 상태 |
|---|---|---|
| `TR0` · PHASE 4.5 | 계측 무해화 계약 — 측정 표면의 해상도·프레임 종류 선언 | progress |
| `TR2` · PHASE 4.5 | 렌더 해상도 ≠ 표시 해상도 계약·jitter 시퀀스·멀티뷰 품질 수용 | done |
| `TU0` · PHASE 4.5 | 벤더 중립 Upscaler 인터페이스·기능 질의·폴백 사슬 | progress |
| `TU1` · PHASE 4.5 | 무-upscale 기준 경로·A/B 대조군 | progress |
| `TU2` · PHASE 4.5 | FSR 백엔드 — 벤더 중립 베이스라인 | progress |
| `TU3` · PHASE 4.5 | DLSS Super Resolution 백엔드 (NGX) | progress |
| `TU4` · PHASE 4.5 | XeSS 백엔드 | progress |
| `FG0` · PHASE 4.5 | present 소유권·proxy swapchain·frame pacing 계약 | progress |
| `FG1` · PHASE 4.5 | 지연 마커 — Reflex·Anti-Lag 2, 게임 루프 경유 | progress |
| `FG2` · PHASE 4.5 | FSR 3 Frame Generation 백엔드 — 벤더 중립 베이스라인 | progress |
| `FG3` · PHASE 4.5 | DLSS Frame Generation 백엔드 | progress |
| `TFG9` · PHASE 4.5 | 지원 행렬 전수·폴백 사슬·계측 단정·성능/지연 판정 | progress |

대시보드 상태는 인계용 식별 정보다. 위 구현 기반에서 이미 충족한 하위 요구는 재작성하지 않고, 남은 제품/실패/비용 수용을 수행한다.

## master 소스 근거

모든 행의 파일·행 번호·Git blob은 고정 master에서 대조했다. 소스 존재·배선 확인과 실행 성공을 구분한다.

- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp:92](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp#L92) — `struct TemporalFixtureExchange`
- [Engine/RenderEngine/Camera.h:43](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Camera.h#L43) — `void NotifyCameraCut() noexcept { ++m_cameraCutRevision; }`
- [Player/PlayerMain.cpp:124](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Player/PlayerMain.cpp#L124) — `StopPresentation();`

## 검증 경계

이번 정리는 master 소스·문서 전체의 소유권/충돌/링크/HTML 검증이다. 엔진 빌드·GPU 픽셀·SDK/장치·성능·실제 GUI 수용을 새로 실행한 결과가 아니다. 과거 통과는 해당 소스/바이너리 신원에만 유효하다. 남은 제품 수용은 위 요구를 실제 산출물로 닫아야 한다.

<details><summary>이전 절 링크 호환</summary>

이전 절의 완료 이력·구안은 제거했다. 이 링크로 들어온 경우 위의 남은 작업과 단일 소유 계획을 따른다.

<span id="0-이-페이즈가-생긴-이유"></span>
<span id="1-정본-경계"></span>
<span id="10-미해결-결정"></span>
<span id="11-변경-이력"></span>
<span id="2-현재-코드의-사실--2026-09-14-실측"></span>
<span id="2026-10-07-최근-병합-반영"></span>
<span id="2026-10-09-벤더-구현-착수"></span>
<span id="2026-10-09-실제-입력렌더-그래프player-배선-확장"></span>
<span id="2026-10-09-제품-설정히스토리profiler-소스-보완"></span>
<span id="2026-10-10-pr-169-실행-증거-보완"></span>
<span id="2026-10-11-dlss-첫-이동-잔상-후속"></span>
<span id="2026-10-11-phase-45-종료-검증"></span>
<span id="21-시간축-입력이-없다"></span>
<span id="22-ui-합성-위치는-유리하다"></span>
<span id="23-present는-셸이-독점하고-에디터-뷰포트는-present를-타지-않는다"></span>
<span id="24-해상도-이원화는-방금-데인-축이다"></span>
<span id="3-계층-분리-결정"></span>
<span id="31-세-계층은-소유-대상이-다르다"></span>
<span id="32-자체-tsr을-만들지-않는다"></span>
<span id="33-fg는-q0를-선행으로-받지-않는다"></span>
<span id="4-벤더-지원-행렬"></span>
<span id="41-업스케일-축"></span>
<span id="42-프레임-생성-축"></span>
<span id="43-두-축에서-같은-결론"></span>
<span id="44-선택은-벤더-id가-아니라-기능-질의로"></span>
<span id="5-작업-표--71일--선행-base-0"></span>
<span id="51-선행-입력--base-0-phase-43-소유-잔여-4인일"></span>
<span id="52-트랙-tr--시간축-기반-21인일"></span>
<span id="53-트랙-tu--업스케일-20인일"></span>
<span id="54-트랙-fg--프레임-생성-26인일"></span>
<span id="55-통합-게이트--4인일"></span>
<span id="6-실행-순서"></span>
<span id="7-계측-무해화--이-페이즈의-첫-단정"></span>
<span id="8-완료-기준"></span>
<span id="9-금지하는-재혼합"></span>
<span id="sec-0-이-페이즈가-생긴-이유"></span>
<span id="sec-1-정본-경계"></span>
<span id="sec-10-미해결-결정"></span>
<span id="sec-11-변경-이력"></span>
<span id="sec-2-현재-코드의-사실--2026-09-14-실측"></span>
<span id="sec-2026-10-07-최근-병합-반영"></span>
<span id="sec-2026-10-09-벤더-구현-착수"></span>
<span id="sec-2026-10-09-실제-입력렌더-그래프player-배선-확장"></span>
<span id="sec-2026-10-09-제품-설정히스토리profiler-소스-보완"></span>
<span id="sec-2026-10-10-pr-169-실행-증거-보완"></span>
<span id="sec-2026-10-11-dlss-첫-이동-잔상-후속"></span>
<span id="sec-2026-10-11-phase-45-종료-검증"></span>
<span id="sec-3-계층-분리-결정"></span>
<span id="sec-4-벤더-지원-행렬"></span>
<span id="sec-5-작업-표--71일--선행-base-0"></span>
<span id="sec-6-실행-순서"></span>
<span id="sec-7-계측-무해화--이-페이즈의-첫-단정"></span>
<span id="sec-8-완료-기준"></span>
<span id="sec-9-금지하는-재혼합"></span>
<span id="트랙-fg"></span>
<span id="트랙-tr"></span>
<span id="트랙-tu"></span>
<span id="페이즈"></span>

</details>
