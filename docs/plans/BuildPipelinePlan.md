# 빌드 파이프라인 신설과 엔진 계층 전면 개편 (BuildPipelinePlan)

> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 잔여 구현·수용 계획

판정 기준은 위 master의 커밋된 소스다. 작업 중인 로컬 코드 변경은 포함하지 않는다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/BuildPipelinePlan.md)

## 남은 작업과 완료 판정

2026-10-23 master 통합 재점검: `a985ee7e1`의 상류 변경과 기존 작업을 통합한 실행·호출자 수정은 [동기화 수용 기록](../analysis/MasterSyncRecheck20261023.md)을 따른다. 이전 실행 기록의 지문은 보존하고 이번 통합 작업본의 제품 검증과 구분한다.

> 2026-10-23 후속: BuildTool의 이전 monolithic 모델 경로/동반 파일2개 가정을 현재 streaming generation 계약으로 이주했다. 필수 파일·canonical 경로·소유 레코드와 병합 전후 파일 재고/SHA-256을 대조한다. 188검사와 전체20모델·73재질의 실제 출력691파일 판독은 통과했다. fixture는 cook 전에 모델/메시·Walk·Graph/텍스처·stable layer를 검사한다. Player의 정상 종료에서는 메시 분할 캐시가 가진 파일 lease도 회수한다. 새 SDK·AnimationPlayer 제품 결과는 [애니메이션 쿠커 후속 기록](../analysis/AnimationCookAcceptance20261023.md)을 따르며 아래 CI·Shipping·증분/재현성 전체 수용과 구분한다.

- 진행 중인 build/cook 단계와 B5 게임 CI 레그를 clean 입력부터 Player 실행까지 닫는다. 이미 연결된 SDK·공통 설정·Host 분리는 재구현하지 않는다.
- cook DAG·증분 캐시 무효화·취소/실패 복구·Shipping 의존 격리와 패키지 재현성을 실제 산출물로 검증한다. 오디오 선행 차단은 AU 종료에 따라 해제한다.

## 적용할 구조와 경계

`project/build settings → build/cook orchestration → cooked manifest/pak → Player bootstrap`

위 경로는 소스에서 확인한 구현 기반이다. 아래 요구의 수용 결과를 뜻하지 않는다. 구현을 다시 만드는 항목과 제품 검증을 혼합하지 않는다.

## 추적할 잔여 항목

| 추적 ID | 소유 단위 (잔여 작업은 위 목록) | 대시보드 보존 상태 |
|---|---|---|
| `12.5-1` · PHASE 12.5 | B1 — 빌드 기술 단일화 (구성 축소 + props 승격) | progress |
| `12.5-2` · PHASE 12.5 | B2 — CreatorBuildTool EXE·단일 패키징 구현 | progress |
| `12.5-3` · PHASE 12.5 | B3 — 셰이더 쿡 + 스테이징 | todo |
| `12.5-4` · PHASE 12.5 | B4 — C# 빌드 편입 + Managed 배치 | progress |
| `12.5-5` · PHASE 12.5 | B5 — CI 게임 레그 | todo |
| `12.5-8` · PHASE 12.5 | L3' — 게임 프로젝트화 (구 L3 승계) | todo |

대시보드 상태는 인계용 식별 정보다. 위 구현 기반에서 이미 충족한 하위 요구는 재작성하지 않고, 남은 제품/실패/비용 수용을 수행한다.

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

<span id="0-논지--심판이-없으면-개편도-없다"></span>
<span id="1-실측--다섯-방향"></span>
<span id="11-빌드-기술이-아홉-벌"></span>
<span id="12-매크로-경계는-창을-끄지-의존을-끊지-않는다"></span>
<span id="13-에셋-흐름--b2-model-cookpak은-닫혔고-셰이더-쿡은-아직-비어-있다"></span>
<span id="14-플레이어는-3세대-전-에디터의-사본이다"></span>
<span id="15-ci는-게임을-모른다"></span>
<span id="16-참고--두-상용-엔진의-답과-우리가-취할-것"></span>
<span id="2-목표"></span>
<span id="20-결정--게임-빌드는-유니티식이다-2026-08-10-개정"></span>
<span id="2026-10-07-최근-병합-반영"></span>
<span id="21-계층--다섯-층"></span>
<span id="22-빌드-축--구성-2개와-런타임-모드-2026-08-10-개정"></span>
<span id="23-파이프라인--여섯-단계와-한-오케스트레이터"></span>
<span id="24-산성-테스트--모든-슬라이스의-심판"></span>
<span id="3-실행--슬라이스"></span>
<span id="4-하지-않을-것"></span>
<span id="5-위험"></span>
<span id="6-완료-기준-수치"></span>
<span id="7-문서-관계"></span>
<span id="editorcore-개편과의-접점"></span>
<span id="sec-0-논지--심판이-없으면-개편도-없다"></span>
<span id="sec-1-실측--다섯-방향"></span>
<span id="sec-2-목표"></span>
<span id="sec-2026-10-07-최근-병합-반영"></span>
<span id="sec-3-실행--슬라이스"></span>
<span id="sec-4-하지-않을-것"></span>
<span id="sec-5-위험"></span>
<span id="sec-6-완료-기준-수치"></span>
<span id="sec-7-문서-관계"></span>
<span id="트랙-b--심판-세우기-선행"></span>

</details>
