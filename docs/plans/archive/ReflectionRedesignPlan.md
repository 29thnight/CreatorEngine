# 컴파일타임 리플렉션 전환 — 헤더툴 은퇴와 typed 방문 계약 (PHASE 18)

> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 종료/승계 문서

판정 기준은 위 master의 커밋된 소스다. 작업 중인 로컬 코드 변경은 포함하지 않는다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](../MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/archive/ReflectionRedesignPlan.md)

## 남은 작업과 완료 판정

**단일 소유 계획:** [SerializationPlan](../SerializationPlan.md). 이 문서는 이전 진입 링크를 보존하는 인계 문서다.

- 이 문서의 완료·대체된 구현 단계는 활성 계획에서 제거했다. 남은 후속은 아래 단일 소유 계획을 따른다.
- 현행 서술 정본은 reflgen::schema_of/type_descriptor다. 과거 reflect()/meta::schema 생성 체계를 신규 목표로 사용하지 않는다.

## 적용할 구조와 경계

`reflgen schema → authoring document/cooked archive → pak → runtime consumer`

위 경로는 소스에서 확인한 구현 기반이다. 아래 요구의 수용 결과를 뜻하지 않는다. 구현을 다시 만드는 항목과 제품 검증을 혼합하지 않는다.

## master 소스 근거

모든 행의 파일·행 번호·Git blob은 고정 master에서 대조했다. 소스 존재·배선 확인과 실행 성공을 구분한다.

- [Engine/Utility_Framework/AuthoringCookedDocument.h:14](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/AuthoringCookedDocument.h#L14) — `inline constexpr std::array<std::byte, 4> kCookedDocumentMagic{`
- [Engine/Utility_Framework/ReflgenAuthoringSerializers.h:26](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/ReflgenAuthoringSerializers.h#L26) — `concept ReflgenRecord = std::is_class_v<T> && reflgen::reflectable<T>;`
- [Engine/SceneRuntime/SceneManager.h:21](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/SceneManager.h#L21) — `class Scene;`

## 검증 경계

이번 정리는 master 소스·문서 전체의 소유권/충돌/링크/HTML 검증이다. 엔진 빌드·GPU 픽셀·SDK/장치·성능·실제 GUI 수용을 새로 실행한 결과가 아니다. 과거 통과는 해당 소스/바이너리 신원에만 유효하다. 남은 제품 수용은 위 요구를 실제 산출물로 닫아야 한다.

<details><summary>이전 절 링크 호환</summary>

이전 절의 완료 이력·구안은 제거했다. 이 링크로 들어온 경우 위의 남은 작업과 단일 소유 계획을 따른다.

<span id="-ct0--기준선안전망-2026-08-16-잔여-2건"></span>
<span id="-ct1--즉효-저위험-2026-08-16"></span>
<span id="-ct10--사강-코드-일소-2026-08-17-감사-dead-목록-이행"></span>
<span id="-ct11--팩토리-접합-factoryregistry--type-필드-2026-08-17-싱글턴-재확인-후속"></span>
<span id="-ct11-b--registry-구조-효율화-2026-08-17"></span>
<span id="-ct11-c--metaaliashmetautilityh-흡수-은퇴-2026-08-17"></span>
<span id="-ct2--죽은-갈래-일소-2026-08-16-f13eaab7"></span>
<span id="-ct3--헤더-전파-절단-2026-08-16"></span>
<span id="-ct4--신-코어--어댑터--typeid-교체-2026-08-17-2커밋-분리"></span>
<span id="-ct5--본대-이전-2026-08-17-단일-스윕"></span>
<span id="-ct6-a--typed-직렬화기--런타임-브리지-2026-08-17"></span>
<span id="-ct6-b--인스펙터-속성-소비-2026-08-17-스코프-조정"></span>
<span id="-ct6-c--인스펙터-typed-draw--ab-토글-2026-08-17-df551178"></span>
<span id="-ct6-d--팩토리-분기-소멸--ondeserialized-후크-2026-08-17"></span>
<span id="-ct7--은퇴정산-2026-08-17--phase-18-완결"></span>
<span id="-ct7-b--crtp-정체성-베이스-2026-08-17-사용자-결정"></span>
<span id="-ct7-c--enumregistry-은퇴매크로-0종-2026-08-17-사용자-점검-요청"></span>
<span id="-ct8--함수형-describe--canonical-변수-treflection-2026-08-17-사용자-지시"></span>
<span id="-ct9--metaschema-로컬-스키마identity-단일-원본라이브러리-분리-2026-08-17-사용자-확정안"></span>
<span id="-ct9-b--열거형-자급화--구-코드-존치-전수-감사-2026-08-17"></span>
<span id="1-결정-사항-분석에서-확정된-것"></span>
<span id="2-실측-요약-변경-대상의-크기"></span>
<span id="3-트랙-ct--슬라이스"></span>
<span id="4-순서와-의존"></span>
<span id="5-완료-기준"></span>
<span id="6-함정-착수-전-필독"></span>
<span id="sec-1-결정-사항-분석에서-확정된-것"></span>
<span id="sec-2-실측-요약-변경-대상의-크기"></span>
<span id="sec-3-트랙-ct--슬라이스"></span>
<span id="sec-4-순서와-의존"></span>
<span id="sec-5-완료-기준"></span>
<span id="sec-6-함정-착수-전-필독"></span>
<span id="원계획-ct4-착수-전-설계--이행-기록-위해-보존"></span>
<span id="원계획-ct6-착수-전-설계--이행-기록-위해-보존"></span>
<span id="원계획-ct7-착수-전-설계--이행-기록-위해-보존"></span>

</details>
