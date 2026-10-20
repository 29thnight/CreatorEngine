# 네트워크 준비형 런타임 · Replication 기반 계획 (PHASE 20)

> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 잔여 구현·수용 계획

판정 기준은 위 master의 커밋된 소스다. 작업 중인 로컬 코드 변경은 포함하지 않는다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/NetworkFrameworkPlan.md)

## 남은 작업과 완료 판정

- ND/N0~N10의 활성 범위: 고정 simulation tick·전용 managed hook, wire identity/schema·replication·bounded command/snapshot 교환을 loopback으로 검증한 뒤 transport를 붙인다.
- 이미 구현된 물리 fixed step·로컬 EntityHandle·ContactStream을 네트워크 simulation/replication 완료로 확대하지 않는다. EntityHandle/typeID를 wire ID로 사용하지 않고 reflgen 순회만 Archive 간 공유한다.

## 적용할 구조와 경계

`current scene/physics/managed lifecycle provides local state; replication must add its own wire identity boundary`

위 경로는 소스에서 확인한 구현 기반이다. 아래 요구의 수용 결과를 뜻하지 않는다. 구현을 다시 만드는 항목과 제품 검증을 혼합하지 않는다.

## 추적할 잔여 항목

| 추적 ID | 소유 단위 (잔여 작업은 위 목록) | 대시보드 보존 상태 |
|---|---|---|
| `ND-a` · PHASE 20 | 계약 헤더 6종 동결 · 변경 절차 · 경계 게이트 | todo |
| `ND-b` · PHASE 20 | 확장점 2종 — 엔티티 수명 · 쓰기 dirty | todo |
| `N0` · PHASE 20 | 기준선·금지 타입·telemetry 실패 게이트 | todo |
| `N1` · PHASE 20 | Archive·Parser 경계 밀봉 | progress |
| `N2` · PHASE 20 | Network ID와 schema 정본 | todo |
| `N3` · PHASE 20 | Fixed Simulation Clock + OnSimulationTick | todo |
| `N4` · PHASE 20 | Loopback Transport와 메시지 프레이밍 | todo |
| `N5` · PHASE 20 | Spawn/Despawn과 Full Snapshot | todo |
| `N6` · PHASE 20 | Dirty Delta·Quantization·Interest | todo |
| `N7` · PHASE 20 | 실제 Transport A/B 판정과 adapter | todo |
| `N10` · PHASE 20 | Hardening·관측·soak | todo |

대시보드 상태는 인계용 식별 정보다. 위 구현 기반에서 이미 충족한 하위 요구는 재작성하지 않고, 남은 제품/실패/비용 수용을 수행한다.

## master 소스 근거

모든 행의 파일·행 번호·Git blob은 고정 master에서 대조했다. 소스 존재·배선 확인과 실행 성공을 구분한다.

- [Engine/SceneRuntime/Scene.h:39](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Scene.h#L39) — `class Component;`
- [ScriptCore/Component.cs:22](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/Component.cs#L22) — `public abstract class Component`
- [Engine/RuntimeHost/EngineLaunchConfig.h:14](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RuntimeHost/EngineLaunchConfig.h#L14) — `struct EngineLaunchConfig`

## 검증 경계

이번 정리는 master 소스·문서 전체의 소유권/충돌/링크/HTML 검증이다. 엔진 빌드·GPU 픽셀·SDK/장치·성능·실제 GUI 수용을 새로 실행한 결과가 아니다. 과거 통과는 해당 소스/바이너리 신원에만 유효하다. 남은 제품 수용은 위 요구를 실제 산출물로 닫아야 한다.

<details><summary>이전 절 링크 호환</summary>

이전 절의 완료 이력·구안은 제거했다. 이 링크로 들어온 경우 위의 남은 작업과 단일 소유 계획을 따른다.

<span id="0-결정-요약"></span>
<span id="1-현재-소스-기준선-2026-09-04-재실측"></span>
<span id="10-외부-후보의-공식-근거"></span>
<span id="11-네트워크-계층은-아직-없다-변동-없음"></span>
<span id="12-포맷-타입-누출은-닫혔다--n1의-판정-조건이-이미-참이다"></span>
<span id="13-정체성이-네-종류인데-계약이-분리되지-않았다"></span>
<span id="14-고정-simulation-tick은-여전히-없다--다만-착지점이-한-곳으로-좁혀졌다"></span>
<span id="15-사용할-수-있는-좋은-선례가-있다-변동-없음-경로-확인"></span>
<span id="2-범위와-비범위"></span>
<span id="21-이번-계획이-해결하는-것"></span>
<span id="22-이번-계획이-의도적으로-하지-않는-것"></span>
<span id="3-목표-프로젝트와-의존-방향"></span>
<span id="31-모듈-책임"></span>
<span id="4-핵심-계약"></span>
<span id="41-archive는-공통-스키마의-서로-다른-소비자다"></span>
<span id="42-로컬-핸들과-원격-정체성"></span>
<span id="43-안정적인-network-schema"></span>
<span id="44-실제-고정-simulation-tick"></span>
<span id="45-스레드와-수명"></span>
<span id="46-상태-입력-사건의-전송-정책"></span>
<span id="47-version과-보안"></span>
<span id="5-실행-페이즈"></span>
<span id="51-순서-요약"></span>
<span id="6-이양-경계--인터페이스-계약과-확장점"></span>
<span id="61-전제--1-기준선은-갱신-완료다"></span>
<span id="610-이-절-고유의-함정"></span>
<span id="62-완전-이양의-조건"></span>
<span id="63-확장점-3종--붙일-자리는-이미-다-있다"></span>
<span id="64-동결-계약-헤더"></span>
<span id="65-이양-경계표"></span>
<span id="66-사내-1회-선행--nd-a와-nd-b"></span>
<span id="67-착수-순서"></span>
<span id="68-접점은-0이-되지-않는다--남는-셋"></span>
<span id="69-이-절이-판정하지-않는-것"></span>
<span id="7-검증-게이트"></span>
<span id="71-source-boundary"></span>
<span id="72-simulation"></span>
<span id="73-replication"></span>
<span id="74-성능운영"></span>
<span id="8-완료-기준"></span>
<span id="9-리스크와-함정"></span>
<span id="n0--기준선과-실패-게이트"></span>
<span id="n1--archiveparser-경계-밀봉-2026-09-04-실측-실질-충족"></span>
<span id="n10--hardening관측soak"></span>
<span id="n2--network-id와-schema-정본"></span>
<span id="n3--fixed-simulation-clock-위임-대상-아님--사전-작업--선행-w8"></span>
<span id="n4--loopback-transport와-메시지-프레이밍"></span>
<span id="n5--spawndespawn과-full-snapshot"></span>
<span id="n6--dirty-deltaquantizationinterest"></span>
<span id="n7--실제-transport-결정과-adapter"></span>
<span id="n8--client-predictionreconciliation-선택"></span>
<span id="n9--headless-server-target"></span>
<span id="sec-0-결정-요약"></span>
<span id="sec-1-현재-소스-기준선-2026-09-04-재실측"></span>
<span id="sec-10-외부-후보의-공식-근거"></span>
<span id="sec-2-범위와-비범위"></span>
<span id="sec-3-목표-프로젝트와-의존-방향"></span>
<span id="sec-4-핵심-계약"></span>
<span id="sec-5-실행-페이즈"></span>
<span id="sec-6-이양-경계--인터페이스-계약과-확장점"></span>
<span id="sec-7-검증-게이트"></span>
<span id="sec-8-완료-기준"></span>
<span id="sec-9-리스크와-함정"></span>
<span id="배선-지점-6곳"></span>
<span id="선행-관리-측-오버라이드-감지--phase-95-w8이-소유한다"></span>
<span id="실측한-배치-2026-09-04"></span>
<span id="아직-정하지-않은-것"></span>
<span id="왜-기존-훅을-옮기지-않고-신설하는가"></span>
<span id="축-간-계약--scope-클럭--postphysics-잔류-규칙--프레임-내-읽기쓰기-방향-2026-09-15"></span>

</details>
