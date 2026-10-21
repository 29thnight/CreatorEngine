# 텍스처 파이프라인 — 임포트 · cook · 런타임 소비

> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 잔여 구현·수용 계획

판정 기준은 위 master의 커밋된 소스다. 작업 중인 로컬 코드 변경은 포함하지 않는다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/TexturePipelinePlan.md)

## 남은 작업과 완료 판정

- T0~T2의 DirectXTex 임포트 정책·cook GPU-ready CECT·typed 소비 구조는 이미 구현됐다. 남은 범위는 지원 포맷/색공간/밉/압축·source-free 소비와 전체 콘텐츠/제품 route 수용이다.
- T1b 및 import/reimport/overwrite·실패 게시/복원·동시 저장/종료·UI 조작의 미수용 항목을 현재 D/R Editor/Shipping 산출물로 확인한다. 과거 일부 Inspector/CECT sampling 통과를 모든 콘텐츠 완료로 확대하지 않는다.
- sampler wrap/filter는 참조별 설정, image recipe는 색/압축/밉 의미를 가진다. 모바일/Web 실타깃 없는 T3 대체 코덱은 중단 상태로 활성 잔여에서 제거한다.

## 적용할 구조와 경계

`DataSystem → resolver snapshot → typed request → descriptor/payload owner → frame pin → RHI retirement`

위 경로는 소스에서 확인한 구현 기반이다. 아래 요구의 수용 결과를 뜻하지 않는다. 구현을 다시 만드는 항목과 제품 검증을 혼합하지 않는다.

## 추적할 잔여 항목

| 추적 ID | 소유 단위 (잔여 작업은 위 목록) | 대시보드 보존 상태 |
|---|---|---|
| `12-0` · PHASE 12 | T0 잔여 — 입력·파일 경합 확장 검증 | progress |
| `12-1` · PHASE 12 | T1a 잔여 — 입력 포맷·코퍼스 확장 수용 | progress |
| `12-2` · PHASE 12 | T2 잔여 — CECT GPU sampling·전체 소비자·모델 | progress |
| `12-3` · PHASE 12 | T1b — DirectXTex 품질·추가 포맷 판단 | todo |

대시보드 상태는 인계용 식별 정보다. 위 구현 기반에서 이미 충족한 하위 요구는 재작성하지 않고, 남은 제품/실패/비용 수용을 수행한다.

## master 소스 근거

모든 행의 파일·행 번호·Git blob은 고정 master에서 대조했다. 소스 존재·배선 확인과 실행 성공을 구분한다.

- [Engine/RenderEngine/DataSystem.h:30](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/DataSystem.h#L30) — `own::shared_owner<const Texture> descriptor;`
- [Engine/RenderEngine/AssetDepot/AssetRequest.h:108](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/AssetDepot/AssetRequest.h#L108) — `own::shared_owner<const T> asset{};`
- [Engine/RenderEngine/AssetDepot/TextureAssetRuntime.h:69](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/AssetDepot/TextureAssetRuntime.h#L69) — `own::shared_owner<const experiment::cooked::ArtifactByteSource> byteSource{};`
- [Engine/RenderEngine/AssetDepot/ModelAssetRuntime.h:18](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/AssetDepot/ModelAssetRuntime.h#L18) — `std::uint64_t resolverRevision{};`
- [Engine/Utility_Framework/Ownership.h:5](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/Ownership.h#L5) — `#include "../../ThirdParty/ownership_cpp/include/own/ownership.hpp"`

## 검증 경계

이번 정리는 master 소스·문서 전체의 소유권/충돌/링크/HTML 검증이다. 엔진 빌드·GPU 픽셀·SDK/장치·성능·실제 GUI 수용을 새로 실행한 결과가 아니다. 과거 통과는 해당 소스/바이너리 신원에만 유효하다. 남은 제품 수용은 위 요구를 실제 산출물로 닫아야 한다.

<details><summary>이전 절 링크 호환</summary>

이전 절의 완료 이력·구안은 제거했다. 이 링크로 들어온 경우 위의 남은 작업과 단일 소유 계획을 따른다.

<span id="1-현재-결정과-범위"></span>
<span id="10-2026-10-09-병합-반영과-상태-판정"></span>
<span id="11-2026-10-10-editor-임포트-수명-후속-검증"></span>
<span id="12-2026-10-10-실제-editor-씬-재로드-후속-검증"></span>
<span id="13-2026-10-10-imagecomponent-재임포트씬-재로드-종단-수용"></span>
<span id="14-2026-10-10-spriterenderer-종단-소비-후속-수용"></span>
<span id="15-2026-10-10-inspector-실제-버튼취소파일-선택-수용"></span>
<span id="16-2026-10-10-리소스-편집-자동-처리-후속"></span>
<span id="17-2026-10-10-자동-처리-inspector-실제-입력-수용"></span>
<span id="18-2026-10-10-release-자동-inspector와-저장-대기-경합-수용"></span>
<span id="19-2026-10-10-저장-대기-중-외부-설정-충돌-수용"></span>
<span id="2-역사적-기준선--현재-상태나-새-검증-결과가-아님"></span>
<span id="20-2026-10-10-수용-단위-분리와-진행률-반영"></span>
<span id="21-2026-10-10-debug-파일-실패복구-ui-후속-수용"></span>
<span id="21-당시의-파이프라인"></span>
<span id="22-2026-10-10-sidecar-실패-진단-수정debug-후속-검증"></span>
<span id="22-당시-코퍼스-2026-09-04"></span>
<span id="23-2026-10-10-진단-수정-release-후속-수용"></span>
<span id="23-당시-성능과-계산값-2026-09-04"></span>
<span id="24-2026-10-10-저장-대기-중-metadata-삭제손상-후속-수용"></span>
<span id="25-2026-10-10-metadata-경합과-종료-flush-후속-수용"></span>
<span id="26-2026-10-10-실제-editor의-5분-자동-저장-경합-반복-수용"></span>
<span id="27-2026-10-10-파싱임시-파일-작성-이후-원본-비교-보강"></span>
<span id="28-2026-10-10-최신-runtime의-실제-inspector-입력종료-반복"></span>
<span id="29-2026-10-10-renderprofile-게시-경합-실패-재현--미수용"></span>
<span id="3-기존-경계와-보존-계약"></span>
<span id="30-2026-10-10-조건부-게시-수정중간-종료-복구-수용"></span>
<span id="31-2026-10-10-조건부-게시-수정-후-실제-ctrlv종료-수용"></span>
<span id="32-2026-10-10-저장-대기-중-wm_close-경로-수용"></span>
<span id="33-2026-10-10-inspector-내부-분리ui-배율-한정-수용"></span>
<span id="34-2026-10-10-ui-후속-보류실제-shipping-파이프라인-검증"></span>
<span id="35-2026-10-10-실제-shipping-cect-누락잘림-거부와-복원"></span>
<span id="36-2026-10-10-cect-typed-무결성새-bc-형식-실제-dx12-sampling"></span>
<span id="37-2026-10-10-동일-cect의-dx12vulkan-gpu-sampling"></span>
<span id="4-라이브러리-및-산출물-정책"></span>
<span id="5-구현-슬라이스"></span>
<span id="6-소유와-의존성"></span>
<span id="7-검증-계획--pr-166-미실행-기록과-후속-실행-증거-분리"></span>
<span id="8-완료-조건과-미검증-항목"></span>
<span id="9-하지-않는-것"></span>
<span id="sec-1-현재-결정과-범위"></span>
<span id="sec-10-2026-10-09-병합-반영과-상태-판정"></span>
<span id="sec-11-2026-10-10-editor-임포트-수명-후속-검증"></span>
<span id="sec-12-2026-10-10-실제-editor-씬-재로드-후속-검증"></span>
<span id="sec-13-2026-10-10-imagecomponent-재임포트씬-재로드-종단-수용"></span>
<span id="sec-14-2026-10-10-spriterenderer-종단-소비-후속-수용"></span>
<span id="sec-15-2026-10-10-inspector-실제-버튼취소파일-선택-수용"></span>
<span id="sec-16-2026-10-10-리소스-편집-자동-처리-후속"></span>
<span id="sec-17-2026-10-10-자동-처리-inspector-실제-입력-수용"></span>
<span id="sec-18-2026-10-10-release-자동-inspector와-저장-대기-경합-수용"></span>
<span id="sec-19-2026-10-10-저장-대기-중-외부-설정-충돌-수용"></span>
<span id="sec-2-역사적-기준선--현재-상태나-새-검증-결과가-아님"></span>
<span id="sec-20-2026-10-10-수용-단위-분리와-진행률-반영"></span>
<span id="sec-21-2026-10-10-debug-파일-실패복구-ui-후속-수용"></span>
<span id="sec-22-2026-10-10-sidecar-실패-진단-수정debug-후속-검증"></span>
<span id="sec-23-2026-10-10-진단-수정-release-후속-수용"></span>
<span id="sec-24-2026-10-10-저장-대기-중-metadata-삭제손상-후속-수용"></span>
<span id="sec-25-2026-10-10-metadata-경합과-종료-flush-후속-수용"></span>
<span id="sec-26-2026-10-10-실제-editor의-5분-자동-저장-경합-반복-수용"></span>
<span id="sec-27-2026-10-10-파싱임시-파일-작성-이후-원본-비교-보강"></span>
<span id="sec-28-2026-10-10-최신-runtime의-실제-inspector-입력종료-반복"></span>
<span id="sec-29-2026-10-10-renderprofile-게시-경합-실패-재현--미수용"></span>
<span id="sec-3-기존-경계와-보존-계약"></span>
<span id="sec-30-2026-10-10-조건부-게시-수정중간-종료-복구-수용"></span>
<span id="sec-31-2026-10-10-조건부-게시-수정-후-실제-ctrlv종료-수용"></span>
<span id="sec-32-2026-10-10-저장-대기-중-wm_close-경로-수용"></span>
<span id="sec-33-2026-10-10-inspector-내부-분리ui-배율-한정-수용"></span>
<span id="sec-34-2026-10-10-ui-후속-보류실제-shipping-파이프라인-검증"></span>
<span id="sec-35-2026-10-10-실제-shipping-cect-누락잘림-거부와-복원"></span>
<span id="sec-36-2026-10-10-cect-typed-무결성새-bc-형식-실제-dx12-sampling"></span>
<span id="sec-37-2026-10-10-동일-cect의-dx12vulkan-gpu-sampling"></span>
<span id="sec-4-라이브러리-및-산출물-정책"></span>
<span id="sec-5-구현-슬라이스"></span>
<span id="sec-6-소유와-의존성"></span>
<span id="sec-7-검증-계획--pr-166-미실행-기록과-후속-실행-증거-분리"></span>
<span id="sec-8-완료-조건과-미검증-항목"></span>
<span id="sec-9-하지-않는-것"></span>
<span id="t0--임포트-설정과-저작-연결-구현-적용--부분-검증"></span>
<span id="t1a--directxtex-cook-트랜스코딩-구현-적용--부분-검증"></span>
<span id="t1b--품질-및-추가-포맷-판단-품질성능-측정-대기"></span>
<span id="t2--cooked-런타임-소비-연결-구현-적용--부분-검증"></span>
<span id="t3--모바일web-제품-타깃-부재로-중단"></span>
<span id="terrain-데이터는-색-이미지가-아니다"></span>
<span id="이번-구현의-정책과-남는-범위"></span>

</details>
