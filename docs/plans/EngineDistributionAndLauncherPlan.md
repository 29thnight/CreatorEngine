# 엔진 배포 · Launcher · 프로젝트 관리 (PHASE 23)

> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 잔여 구현·수용 계획

판정 기준은 위 master의 커밋된 소스다. 작업 중인 로컬 코드 변경은 포함하지 않는다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/EngineDistributionAndLauncherPlan.md)

## 남은 작업과 완료 판정

- DL0~11: 프로젝트/엔진 버전 계약, Launcher 관리, 불변 distribution/provenance, MSI 설치·복구·제거·side-by-side, 서명 update/rollback을 연결한다.
- 기존 SDK/Player 패키징 구현을 출발점으로 build/cook·스크립트 디버깅과 clean VM·다중 프로젝트/버전·watch soak를 검증한다. 로컬 패키지 성공을 설치 제품 완료로 대체하지 않는다.

## 적용할 구조와 경계

`project/build settings → build/cook orchestration → cooked manifest/pak → Player bootstrap`

위 경로는 소스에서 확인한 구현 기반이다. 아래 요구의 수용 결과를 뜻하지 않는다. 구현을 다시 만드는 항목과 제품 검증을 혼합하지 않는다.

## 추적할 잔여 항목

| 추적 ID | 소유 단위 (잔여 작업은 위 목록) | 대시보드 보존 상태 |
|---|---|---|
| `DL0` · PHASE 23 | 제품·설치·프로젝트 기준선과 실패 게이트 | todo |
| `DL1` · PHASE 23 | 프로젝트 descriptor·identity·--project Host 계약 | todo |
| `DL2` · PHASE 23 | efsw 다중 root·main-thread reconcile 수명 | todo |
| `DL3` · PHASE 23 | Editor ProjectSession·lock·root lifecycle | todo |
| `DL4` · PHASE 23 | Launcher 프로젝트 관리 vertical slice | todo |
| `DL5` · PHASE 23 | 버전별 불변 엔진 distribution·provenance | progress |
| `DL6` · PHASE 23 | WiX MSI 설치·복구·제거·side-by-side | todo |
| `DL7` · PHASE 23 | 서명 update·migration·rollback·trust | todo |
| `DL8` · PHASE 23 | Launcher build/cook 연동 | todo |
| `DL9` · PHASE 23 | clean-VM E2E·다중 프로젝트/버전·watch soak | todo |
| `DL10` · PHASE 23 | 릴리스 운영·진단·최종 외부 종속 감사 | todo |
| `DL11` · PHASE 23 | 게임 스크립트 디버깅 계약 (최적화·심볼 축 분리) | todo |

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

<span id="0-결정-요약"></span>
<span id="1-현재-소스-기준선-2026-08-27-재확인"></span>
<span id="10-의존-관계와-배정"></span>
<span id="11-이미-있는-이음새"></span>
<span id="11-최종-완료-기준"></span>
<span id="12-설치형-제품을-막는-현재-가정"></span>
<span id="12-위험과-기각한-대안"></span>
<span id="13-갱신-규칙"></span>
<span id="13-파일-감시-기준선"></span>
<span id="14-아직-없는-것"></span>
<span id="2-제품-경계와-비목표"></span>
<span id="21-목표"></span>
<span id="22-비목표"></span>
<span id="3-프로세스와-소유권"></span>
<span id="4-설치데이터-디렉터리-계약"></span>
<span id="5-프로젝트-descriptor-정본"></span>
<span id="6-efsw-다중-root-authoring-감시-계약"></span>
<span id="61-api-방향"></span>
<span id="62-소비자별-정책"></span>
<span id="63-efsw-검증-범위"></span>
<span id="7-launcher-기능-계약"></span>
<span id="71-프로젝트-목록"></span>
<span id="72-엔진-버전"></span>
<span id="73-실행과-session-lock"></span>
<span id="74-빌드"></span>
<span id="8-msi업데이트신뢰-경계"></span>
<span id="81-msi-제품-모델"></span>
<span id="82-다운로드와-업데이트"></span>
<span id="83-프로젝트-신뢰"></span>
<span id="9-실행-계획"></span>
<span id="dl0--제품설치프로젝트-기준선과-실패-게이트-p0-2일"></span>
<span id="dl1--프로젝트-descriptoridentity--project-host-계약-p0-4일"></span>
<span id="dl10--릴리스-운영진단최종-외부-종속-감사-p1-3일"></span>
<span id="dl11--게임-스크립트-디버깅-계약-p1-2일"></span>
<span id="dl2--efsw-다중-rootmain-thread-reconcile-수명-p0-3일"></span>
<span id="dl3--editor-projectsessionlockroot-lifecycle-p0-4일"></span>
<span id="dl4--launcher-프로젝트-관리-vertical-slice-p0-5일"></span>
<span id="dl5--버전별-불변-엔진-distribution-layoutprovenance-p0-4일"></span>
<span id="dl6--wix-msi-설치복구제거side-by-side-p0-5일"></span>
<span id="dl7--서명-updatemigrationrollbacktrust-p0-4일"></span>
<span id="dl8--launcher-buildcook-연동-p1-4일"></span>
<span id="dl9--clean-vm-e2e다중-프로젝트버전watch-soak-p0-5일"></span>
<span id="sec-0-결정-요약"></span>
<span id="sec-1-현재-소스-기준선-2026-08-27-재확인"></span>
<span id="sec-10-의존-관계와-배정"></span>
<span id="sec-11-최종-완료-기준"></span>
<span id="sec-12-위험과-기각한-대안"></span>
<span id="sec-13-갱신-규칙"></span>
<span id="sec-2-제품-경계와-비목표"></span>
<span id="sec-3-프로세스와-소유권"></span>
<span id="sec-4-설치데이터-디렉터리-계약"></span>
<span id="sec-5-프로젝트-descriptor-정본"></span>
<span id="sec-6-efsw-다중-root-authoring-감시-계약"></span>
<span id="sec-7-launcher-기능-계약"></span>
<span id="sec-8-msi업데이트신뢰-경계"></span>
<span id="sec-9-실행-계획"></span>

</details>
