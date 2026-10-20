# 태스크 스케줄러 단일화 — enkiTS 공용 실행 기반 (PHASE 13 S0.5)

> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 잔여 구현·수용 계획

판정 기준은 위 master의 커밋된 소스다. 작업 중인 로컬 코드 변경은 포함하지 않는다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/TaskSchedulerUnificationPlan.md)

## 남은 작업과 완료 판정

- 공용 thread_pool/job_scheduler, DataSystem/애니메이션/PSO/씬 로드·명령 기록 이관은 완료된 기반이다. 별도 자체 풀 이관 계획을 재등록하지 않는다.
- 현재 dependency recording·bounded spin/진단을 기준으로 실제 workload의 대기/실행/계측 비용과 shutdown·취소 회귀만 검증한다. VM·추가 풀·대체 스케줄러 채택은 별도 실측 후 결정한다.

## 적용할 구조와 경계

`Animator → shared definition/instance → scheduled pose tasks → palette → render frame`

위 경로는 소스에서 확인한 구현 기반이다. 아래 요구의 수용 결과를 뜻하지 않는다. 구현을 다시 만드는 항목과 제품 검증을 혼합하지 않는다.

## master 소스 근거

모든 행의 파일·행 번호·Git blob은 고정 master에서 대조했다. 소스 존재·배선 확인과 실행 성공을 구분한다.

- [Engine/SceneRuntime/Animator.h:108](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Animator.h#L108) — `class [[reflgen::reflect]] Animator : public meta::identity<Animator, Component>`
- [Engine/SceneRuntime/AnimationScheduler.cpp:1867](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/AnimationScheduler.cpp#L1867) — `std::uint32_t AppendClipTasks(animation::task_list& tasks,`
- [ScriptCore/Animator.cs:14](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/ScriptCore/Animator.cs#L14) — `public sealed class Animator : NativeComponent`

## 검증 경계

이번 정리는 master 소스·문서 전체의 소유권/충돌/링크/HTML 검증이다. 엔진 빌드·GPU 픽셀·SDK/장치·성능·실제 GUI 수용을 새로 실행한 결과가 아니다. 과거 통과는 해당 소스/바이너리 신원에만 유효하다. 남은 제품 수용은 위 요구를 실제 산출물로 닫아야 한다.

<details><summary>이전 절 링크 호환</summary>

이전 절의 완료 이력·구안은 제거했다. 이 링크로 들어온 경우 위의 남은 작업과 단일 소유 계획을 따른다.

<span id="0-2026-09-20-현행-결정"></span>
<span id="1-당시-구조와-측정-2026-09-15--현행-차이는-0-우선"></span>
<span id="10-dx12-pso-비동기-컴파일-이관-2026-09-20"></span>
<span id="11-corethreadpool-인스턴스--3개-중-1개는-죽어-있다"></span>
<span id="11-씬-로딩-2곳-이관-2026-09-20"></span>
<span id="12-dx12vulkan-명령-기록-이관-2026-09-20"></span>
<span id="12-스레드-풀이-사실-3벌이다"></span>
<span id="13-player-실제-실행씬-전환종료-2026-09-20"></span>
<span id="13-stdasync-5곳--성격이-갈린다"></span>
<span id="14-stdthread--51건-중-실제-생성은-18곳"></span>
<span id="2-측정--무엇이-실제로-서-있는가"></span>
<span id="21-확인된-것--enkits가-p50-32-빠르다"></span>
<span id="22-반증된-것--자체-풀의-배리어는-정직하다"></span>
<span id="23-측정-불가--꼬리-지연"></span>
<span id="3-판정--무엇을-옮기고-무엇을-남기는가"></span>
<span id="31-옮긴다"></span>
<span id="32-남긴다--그리고-그-이유"></span>
<span id="33-animationjob은-s6이-가져간다--순서-제약"></span>
<span id="34-정확성과-스케줄러-교체의-경계"></span>
<span id="35-보류--dx12vulkan-커맨드-풀"></span>
<span id="4-단계--phase-13-어디에-붙는가"></span>
<span id="5-게이트-설계-규율"></span>
<span id="6-미해결--미측정"></span>
<span id="7-관련-문서"></span>
<span id="8-s05-②-공용-풀-이관-2026-09-20"></span>
<span id="9-공용-스케줄러-구현-2026-09-20"></span>
<span id="phase-13-s05--태스크-스케줄러-기반-정비-2일-p0-즉시-착수-가능"></span>
<span id="phase-13-s6--job-배치-전환-2일-p1-s35-의존"></span>
<span id="phase-13-s6-이후--자체-풀-은퇴"></span>
<span id="sec-0-2026-09-20-현행-결정"></span>
<span id="sec-1-당시-구조와-측정-2026-09-15--현행-차이는-0-우선"></span>
<span id="sec-10-dx12-pso-비동기-컴파일-이관-2026-09-20"></span>
<span id="sec-11-씬-로딩-2곳-이관-2026-09-20"></span>
<span id="sec-12-dx12vulkan-명령-기록-이관-2026-09-20"></span>
<span id="sec-13-player-실제-실행씬-전환종료-2026-09-20"></span>
<span id="sec-2-측정--무엇이-실제로-서-있는가"></span>
<span id="sec-3-판정--무엇을-옮기고-무엇을-남기는가"></span>
<span id="sec-4-단계--phase-13-어디에-붙는가"></span>
<span id="sec-5-게이트-설계-규율"></span>
<span id="sec-6-미해결--미측정"></span>
<span id="sec-7-관련-문서"></span>
<span id="sec-8-s05-②-공용-풀-이관-2026-09-20"></span>
<span id="sec-9-공용-스케줄러-구현-2026-09-20"></span>
<span id="페이즈-미배정--dx12vulkan-커맨드-풀"></span>

</details>
