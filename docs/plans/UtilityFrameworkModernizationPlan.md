# 유틸리티 프레임워크 현대화 — 표준 회귀와 죽은 코드 정리 (PHASE 15)

> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 잔여 구현·수용 계획

판정 기준은 위 master의 커밋된 소스다. 작업 중인 로컬 코드 변경은 포함하지 않는다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/UtilityFrameworkModernizationPlan.md)

## 남은 작업과 완료 판정

- 15-S/U/H/C의 실제 잔여 표준화·불필요 코드·HashingString·우산 헤더를 호출자/빌드 경계로 좁혀 정리한다. 이미 제거된 Core.Assert/Mathf·DirectX 수학 별칭을 다시 이관하지 않는다.
- 전체 식별자 비교와 헤더 self-containment·ODR·D/R 소비자 빌드를 검증한다. math 제품 픽셀/Physics 수용은 MathematicsMigrationPlan에 단일화한다.

## 적용할 구조와 경계

`math value types → Transform/Camera snapshots → render and physics boundary conversion`

위 경로는 소스에서 확인한 구현 기반이다. 아래 요구의 수용 결과를 뜻하지 않는다. 구현을 다시 만드는 항목과 제품 검증을 혼합하지 않는다.

## 추적할 잔여 항목

| 추적 ID | 소유 단위 (잔여 작업은 위 목록) | 대시보드 보존 상태 |
|---|---|---|
| `15-S` · PHASE 15 | 트랙 S — 표준 회귀 | progress |
| `15-U` · PHASE 15 | 트랙 U — 죽은 코드 정리 | progress |
| `15-H` · PHASE 15 | 트랙 H — HashingString 재설계 | progress |
| `15-C` · PHASE 15 | 트랙 C — 우산 헤더 해체 | progress |

대시보드 상태는 인계용 식별 정보다. 위 구현 기반에서 이미 충족한 하위 요구는 재작성하지 않고, 남은 제품/실패/비용 수용을 수행한다.

## master 소스 근거

모든 행의 파일·행 번호·Git blob은 고정 master에서 대조했다. 소스 존재·배선 확인과 실행 성공을 구분한다.

- [Engine/SceneRuntime/Transform.h:90](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/Transform.h#L90) — `math::vector3 GetPosition() const;`
- [Engine/RenderEngine/Camera.h:25](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Camera.h#L25) — `math::matrix4x4 CalculateProjection() const;`
- [Engine/Utility_Framework/Mathematics.Intersect.h:18](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/Mathematics.Intersect.h#L18) — `#include <mathematics/intersect.hpp>`

## 검증 경계

이번 정리는 master 소스·문서 전체의 소유권/충돌/링크/HTML 검증이다. 엔진 빌드·GPU 픽셀·SDK/장치·성능·실제 GUI 수용을 새로 실행한 결과가 아니다. 과거 통과는 해당 소스/바이너리 신원에만 유효하다. 남은 제품 수용은 위 요구를 실제 산출물로 닫아야 한다.

<details><summary>이전 절 링크 호환</summary>

이전 절의 완료 이력·구안은 제거했다. 이 링크로 들어온 경우 위의 남은 작업과 단일 소유 계획을 따른다.

<span id="0-전략-요약"></span>
<span id="1-실측--2026-08-14-전수-조사"></span>
<span id="11-표준-라이브러리-네임스페이스-침범--최우선"></span>
<span id="110-coremathfh--별칭-계층-평가-2026-08-17-추가-실측"></span>
<span id="12-소비자가-실제로-0인-것"></span>
<span id="13-소비자가-1곳뿐인-것-표준-대체-후보"></span>
<span id="14-살아-있는-것--표준-대체-검토-대상"></span>
<span id="15-문서와-구현의-불일치"></span>
<span id="16-껍데기-헤더"></span>
<span id="17-우산-헤더"></span>
<span id="18-hashingstring-2026-08-14-분석"></span>
<span id="19-plf_colonyh--배치가-잘못됐다-2026-08-14-추가-실측"></span>
<span id="2-트랙-구조"></span>
<span id="3-단계별"></span>
<span id="4-게이트-관례"></span>
<span id="5-이-계획이-다루지-않는-것"></span>
<span id="6-진행-상태"></span>
<span id="h0-인스펙터-편집-복구-기능-파손"></span>
<span id="h1-data-ub-제거"></span>
<span id="h2--일관성--data-봉인"></span>
<span id="h3-값-반환-제거-성능-회수"></span>
<span id="h4-stdhash-특수화--조회-경로"></span>
<span id="h5-빈-문자열-정책-통일--결정-합법-2026-09-05"></span>
<span id="m0-소비자-0-헬퍼-삭제--assimp-의존-소멸--구현-완료-전체-솔루션-게이트-전-2026-08-25"></span>
<span id="m1-using-namespace-directx-제거--구현-및-빌드-완료-2026-08-25"></span>
<span id="m2-static-전역-8개-정리--구현-및-엔진-빌드-완료-2026-08-25"></span>
<span id="m3-easing--tween-분리--구현-완료-runtime-gate-전-2026-08-25"></span>
<span id="m4-정본-통일--dead-wrapper-정리-완료-전체-솔루션-게이트-전-2026-08-25"></span>
<span id="m5-엔진-내부-자체-구현--기각-기록-2026-08-17-2026-08-25-외부-정본-이주로-승계"></span>
<span id="s0-namespace-std-침범-제거--순서-밖-최우선"></span>
<span id="s1-thread--stdjthread"></span>
<span id="s2-countingsemaphore--stdcounting_semaphore"></span>
<span id="sec-0-전략-요약"></span>
<span id="sec-1-실측--2026-08-14-전수-조사"></span>
<span id="sec-2-트랙-구조"></span>
<span id="sec-3-단계별"></span>
<span id="sec-4-게이트-관례"></span>
<span id="sec-5-이-계획이-다루지-않는-것"></span>
<span id="sec-6-진행-상태"></span>
<span id="u0-소비자-0인-것-제거"></span>
<span id="u1-껍데기명명-정리"></span>
<span id="u2-문서-구현-불일치-해소"></span>
<span id="u3-소비자-1곳-유틸-개별-판정"></span>
<span id="u4-plf_colonyh-배치-이동"></span>
<span id="진행-상태-2026-09-05"></span>
<span id="트랙-c--우산-헤더-해체"></span>
<span id="트랙-h--hashingstring"></span>
<span id="트랙-m--mathf-별칭-계층"></span>
<span id="트랙-s--표준-회귀"></span>
<span id="트랙-u--죽은-코드-정리"></span>

</details>
