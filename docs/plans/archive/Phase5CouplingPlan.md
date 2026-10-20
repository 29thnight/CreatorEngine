# PHASE 5 구조적 커플링 제거 — 실행 설계 (5-1)

> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 종료/승계 문서

판정 기준은 위 master의 커밋된 소스다. 작업 중인 로컬 코드 변경은 포함하지 않는다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](../MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/archive/Phase5CouplingPlan.md)

## 남은 작업과 완료 판정

**단일 소유 계획:** [EngineLayerSeparationPlan](../EngineLayerSeparationPlan.md). 이 문서는 이전 진입 링크를 보존하는 인계 문서다.

- 이 문서의 완료·대체된 구현 단계는 활성 계획에서 제거했다. 남은 후속은 아래 단일 소유 계획을 따른다.

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

<span id="1-경계-원칙"></span>
<span id="2-실측-현황-2026-08-06-기준"></span>
<span id="3-절단-전략--4개-카테고리"></span>
<span id="4-실행-순서"></span>
<span id="5-진행-2026-08-09-기준"></span>
<span id="a-죽은-include-제거-빠름-위험-낮음"></span>
<span id="b-렌더-데이터-헤더의-소속-정정--renderengineinterfaces"></span>
<span id="c-행위-커플링--인터페이스이동-본체-슬라이스-반복"></span>
<span id="d-검증"></span>
<span id="sec-1-경계-원칙"></span>
<span id="sec-2-실측-현황-2026-08-06-기준"></span>
<span id="sec-3-절단-전략--4개-카테고리"></span>
<span id="sec-4-실행-순서"></span>
<span id="sec-5-진행-2026-08-09-기준"></span>
<span id="발견된-구조-부채-slice-a-실행-중-실측"></span>

</details>
