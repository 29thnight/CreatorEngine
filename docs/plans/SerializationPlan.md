# 직렬화 이원화 — 저작 텍스트 · 런타임 쿠킹 바이너리 (PHASE 17)

> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 잔여 구현·수용 계획

판정 기준은 위 master의 커밋된 소스다. 작업 중인 로컬 코드 변경은 포함하지 않는다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/SerializationPlan.md)

## 남은 작업과 완료 판정

- D0~D6의 authoring/cooked 분리·ryml/reflgen 전환은 활성 계획에서 제거한다. asset identity는 UUIDv8/CEMF의 구현 방향을 따른다.
- 남은 I7/제품 패키지·typed Scene/Prefab bootstrap 및 실제 parser-free 소비 검증은 AssetDepot/BuildPipeline 소유로 연결한다. 과거 시작 씬 결함을 현재 미해결이라고 재선언하지 않고 현재 패키지로 확인한다.

## 적용할 구조와 경계

`reflgen schema → authoring document/cooked archive → pak → runtime consumer`

위 경로는 소스에서 확인한 구현 기반이다. 아래 요구의 수용 결과를 뜻하지 않는다. 구현을 다시 만드는 항목과 제품 검증을 혼합하지 않는다.

## 추적할 잔여 항목

| 추적 ID | 소유 단위 (잔여 작업은 위 목록) | 대시보드 보존 상태 |
|---|---|---|
| `I7` · PHASE archive-model-import | cooked 포맷·속도 (경로 규약 제외) | progress |

대시보드 상태는 인계용 식별 정보다. 위 구현 기반에서 이미 충족한 하위 요구는 재작성하지 않고, 남은 제품/실패/비용 수용을 수행한다.

## master 소스 근거

모든 행의 파일·행 번호·Git blob은 고정 master에서 대조했다. 소스 존재·배선 확인과 실행 성공을 구분한다.

- [Engine/Utility_Framework/AuthoringCookedDocument.h:14](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/AuthoringCookedDocument.h#L14) — `inline constexpr std::array<std::byte, 4> kCookedDocumentMagic{`
- [Engine/Utility_Framework/ReflgenAuthoringSerializers.h:26](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/ReflgenAuthoringSerializers.h#L26) — `concept ReflgenRecord = std::is_class_v<T> && reflgen::reflectable<T>;`
- [Engine/SceneRuntime/SceneManager.h:21](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/SceneManager.h#L21) — `class Scene;`

## 검증 경계

이번 정리는 master 소스·문서 전체의 소유권/충돌/링크/HTML 검증이다. 엔진 빌드·GPU 픽셀·SDK/장치·성능·실제 GUI 수용을 새로 실행한 결과가 아니다. 과거 통과는 해당 소스/바이너리 신원에만 유효하다. 남은 제품 수용은 위 요구를 실제 산출물로 닫아야 한다.

<details><summary>이전 절 링크 호환</summary>

이전 절의 완료 이력·구안은 제거했다. 이 링크로 들어온 경우 위의 남은 작업과 단일 소유 계획을 따른다.

<span id="1-지금-무엇이-있는가--측정-2026-08-16"></span>
<span id="11-데이터-규모--절대량은-작고-문제는-반복이다"></span>
<span id="12-런타임player이-에디터와-같은-비용을-낸다"></span>
<span id="13-리플렉션-yaml-계층--코어는-좁고-새어나간-표면이-넓다"></span>
<span id="14-포맷이-이미-3갈래다"></span>
<span id="15-meta는-sidecar의-비용만-내고-가치는-못-얻는다"></span>
<span id="16-2026-08-22-재감사--네트워크-경계를-막는-현재-표면"></span>
<span id="17-2026-08-30-착수-전-재실측--1116의-절반이-낡았다"></span>
<span id="18-2026-09-02-현재-구현-재감사--코드-진척과-수용-관문을-분리한다"></span>
<span id="19-2026-09-10-폐쇄-판정--backlog-넷을-실측으로-닫고-게이트가-하네스를-재고-있던-것을-고쳤다"></span>
<span id="2-결함-목록"></span>
<span id="3-설계-결정"></span>
<span id="31-저작-포맷--rapidyaml-기반-yaml-12로-통일"></span>
<span id="32-런타임-포맷--리플렉션-기반-커스텀-바이너리-쿠킹-산출물--캐시"></span>
<span id="33-documentarchive-격리--소유권과-소비-정책을-분리"></span>
<span id="34-meta--역사적-uuidv4-전환-기록-최종-정본은-phase-375-uuidv8"></span>
<span id="35-json-트랙-정리--nlohmann-제거"></span>
<span id="36-쿠킹과-pak-매니페스트"></span>
<span id="361-파생물은-콘텐츠가-아니다--content--derived-분리-2026-08-25-결정"></span>
<span id="37-네트워크-이음새--포맷-공유가-아니라-schema-visitor-공유"></span>
<span id="4-이행--슬라이스"></span>
<span id="5-완료-기준"></span>
<span id="6-하지-않을-것"></span>
<span id="7-다른-계획과의-관계"></span>
<span id="8-리스크"></span>
<span id="sec-1-지금-무엇이-있는가--측정-2026-08-16"></span>
<span id="sec-2-결함-목록"></span>
<span id="sec-3-설계-결정"></span>
<span id="sec-4-이행--슬라이스"></span>
<span id="sec-5-완료-기준"></span>
<span id="sec-6-하지-않을-것"></span>
<span id="sec-7-다른-계획과의-관계"></span>
<span id="sec-8-리스크"></span>
<span id="로컬-수용-환경과-고부하-모델-코퍼스-복구-2026-09-0102"></span>

</details>
