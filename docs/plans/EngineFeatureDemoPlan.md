# 엔진 피처 데모 계획 · CQB FPS (PHASE 25)

> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 잔여 구현·수용 계획

판정 기준은 위 master의 커밋된 소스다. 작업 중인 로컬 코드 변경은 포함하지 않는다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/EngineFeatureDemoPlan.md)

## 남은 작업과 완료 판정

- 실제 패키지에서 렌더 설정·커서 락·CCT 점프·레이어 쓰기·normal map 규약을 다시 확인하고 남은 콘텐츠 제작/촬영 범위를 확정한다. 과거 W8 미착수라는 선행은 제거한다.
- 데모 장면·자산 라이선스/원본·재현 가능한 패키지와 캡처를 보존한다. 데모는 엔진 전 기능 수용을 대체하지 않는다.

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

<span id="0-결정-요약"></span>
<span id="1-데모-정의"></span>
<span id="11-무대"></span>
<span id="12-플레이-3분"></span>
<span id="13-기술-모드-같은-레벨-토글"></span>
<span id="2-데모가-소비하는-트랙"></span>
<span id="21-선행-필수--없으면-데모가-성립하지-않는다"></span>
<span id="22-이미-완성--데모가-그대로-소비한다"></span>
<span id="23-데모가-쓰지-않는-트랙"></span>
<span id="3-리팩토링-완료-후에도-없는-것--갭-5종"></span>
<span id="4-데모-제작-단계"></span>
<span id="5-산출물"></span>
<span id="6-자산-운영-규약"></span>
<span id="7-리스크"></span>
<span id="8-실측-근거"></span>
<span id="sec-0-결정-요약"></span>
<span id="sec-1-데모-정의"></span>
<span id="sec-2-데모가-소비하는-트랙"></span>
<span id="sec-3-리팩토링-완료-후에도-없는-것--갭-5종"></span>
<span id="sec-4-데모-제작-단계"></span>
<span id="sec-5-산출물"></span>
<span id="sec-6-자산-운영-규약"></span>
<span id="sec-7-리스크"></span>
<span id="sec-8-실측-근거"></span>
<span id="갭-①-패키지-렌더-설정이-렌더러에-도달하지-않는다--최우선"></span>
<span id="갭-②-커서-락--fps의-전제"></span>
<span id="갭-③-cct-점프가-끊겨-있다"></span>
<span id="갭-④-물리-레이어-쓰기-api"></span>
<span id="갭-⑤-노멀맵-그린-채널-규약--무료-자산-도입의-관문"></span>
<span id="갭-외--명시적-비범위"></span>

</details>
