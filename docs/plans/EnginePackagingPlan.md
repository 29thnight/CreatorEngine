# 엔진 코어 분리와 패키지화 — 실행 설계

> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 종료/승계 문서

판정 기준은 위 master의 커밋된 소스다. 작업 중인 로컬 코드 변경은 포함하지 않는다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/EnginePackagingPlan.md)

## 남은 작업과 완료 판정

**단일 소유 계획:** [EngineLayerSeparationPlan](EngineLayerSeparationPlan.md). 이 문서는 이전 진입 링크를 보존하는 인계 문서다.

- 현재 물리 프로젝트 경계의 잔여는 EngineLayerSeparationPlan, 빌드/패키지 제품화는 BuildPipelinePlan, 설치/런처는 EngineDistributionAndLauncherPlan을 따른다. 옛 P1~P5의 완료된 의존 절단을 다시 계획하지 않는다.

## 적용할 구조와 경계

`project/build settings → build/cook orchestration → cooked manifest/pak → Player bootstrap`

위 경로는 소스에서 확인한 구현 기반이다. 아래 요구의 수용 결과를 뜻하지 않는다. 구현을 다시 만드는 항목과 제품 검증을 혼합하지 않는다.

## master 소스 근거

모든 행의 파일·행 번호·Git blob은 고정 master에서 대조했다. 소스 존재·배선 확인과 실행 성공을 구분한다.

- [Editor/EngineEntry/GameBuilderSystem.h:5](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineEntry/GameBuilderSystem.h#L5) — `class GameBuilderSystem : public Singleton<GameBuilderSystem>`
- [BuildTool/Program.cs:5](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/BuildTool/Program.cs#L5) — `public static async Task<int> Main(string[] args)`
- [Engine/RuntimeHost/EngineBootstrap.h:74](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RuntimeHost/EngineBootstrap.h#L74) — `inline void InitializeShutdownTrace()`
- [Engine/RuntimeHost/EngineLaunchConfig.h:11](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RuntimeHost/EngineLaunchConfig.h#L11) — `using RuntimeContentPrepare = bool (*)(const EnginePaths& paths) noexcept;`
- [Engine/SceneRuntime/SceneManager.cpp:91](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/SceneManager.cpp#L91) — `"Player 씬 source identity 해석 실패: " + path);`
- [Engine/SceneRuntime/PrefabUtility.cpp:397](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/PrefabUtility.cpp#L397) — `value = Authoring::DecodeCookedDocumentTextEnvelope(`

## 검증 경계

이번 정리는 master 소스·문서 전체의 소유권/충돌/링크/HTML 검증이다. 엔진 빌드·GPU 픽셀·SDK/장치·성능·실제 GUI 수용을 새로 실행한 결과가 아니다. 과거 통과는 해당 소스/바이너리 신원에만 유효하다. 남은 제품 수용은 위 요구를 실제 산출물로 닫아야 한다.

<details><summary>이전 절 링크 호환</summary>

이전 절의 완료 이력·구안은 제거했다. 이 링크로 들어온 경우 위의 남은 작업과 단일 소유 계획을 따른다.

<span id="-순서를-뒤집는다--패키지를-먼저-세운다"></span>
<span id="-파일을-옮기기-전에-의존을-역전해야-한다"></span>
<span id="1-먼저-정정--프로젝트는-이미-나뉘어-있다"></span>
<span id="2-진짜-문제--코어가-코어가-아니다"></span>
<span id="21-범인은-소수-파일에-몰려-있다"></span>
<span id="3-목표-계층"></span>
<span id="4-절단-순서"></span>
<span id="41-지형-패키지--실측과-순서-2026-08-08"></span>
<span id="5-완료-기준"></span>
<span id="p1--부트스트랩을-위로-12간선"></span>
<span id="p2--설정을-아래로-7간선"></span>
<span id="p3--에디터-전용을-셸로-3--6간선"></span>
<span id="p4--렌더가-게임플레이를-놓는다-23간선"></span>
<span id="p5--패키지-경계-세우기"></span>
<span id="sec-1-먼저-정정--프로젝트는-이미-나뉘어-있다"></span>
<span id="sec-2-진짜-문제--코어가-코어가-아니다"></span>
<span id="sec-3-목표-계층"></span>
<span id="sec-4-절단-순서"></span>
<span id="sec-41-지형-패키지--실측과-순서-2026-08-08"></span>
<span id="sec-5-완료-기준"></span>
<span id="규모"></span>

</details>
