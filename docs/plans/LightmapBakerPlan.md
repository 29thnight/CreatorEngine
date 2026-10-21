# 라이트맵 베이커 재작성 (PHASE 4.7 · 트랙 L)

> 2026-10-20 정리 · master `27de3f5a02aadde7456aab680db3184c36ee735e` · 잔여 구현·수용 계획

판정 기준은 위 master의 커밋된 소스다. 작업 중인 로컬 코드 변경은 포함하지 않는다. 완료된 구현 단계·날짜별 실행 일지는 제거했다. 대시보드는 SRP 저작 전환 취소에 해당하는 항목만 제거하고, 나머지 상태·공수·링크를 보존한다.

[전체 문서 대조·충돌 판정](MasterPlanAudit20261020.html) · [정리 전 원문](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/docs/plans/LightmapBakerPlan.md)

## 남은 작업과 완료 판정

- L1~L7: UV1 xatlas·SAH BVH·DX12 직접광·비차단 bake·progressive 간접광·dilate/seam을 단계별 결과로 수용한다.
- 병렬화는 정확한 bake 출력과 단계별 비용 기준선 뒤 판정한다. 기존 L0 및 Q0 queue 기반 구현은 다시 만들지 않는다.

## 적용할 구조와 경계

`lightmapping interface → scene resources; future baker owns UV/BVH/bake orchestration`

위 경로는 소스에서 확인한 구현 기반이다. 아래 요구의 수용 결과를 뜻하지 않는다. 구현을 다시 만드는 항목과 제품 검증을 혼합하지 않는다.

## 추적할 잔여 항목

| 추적 ID | 소유 단위 (잔여 작업은 위 목록) | 대시보드 보존 상태 |
|---|---|---|
| `L1` · PHASE 4.7 | UV1 언랩 — xatlas 벤더링 | todo |
| `L2` · PHASE 4.7 | BVH 재작성 — SAH + nth_element · off-by-one 수정 | todo |
| `L3` · PHASE 4.7 | 직접광 패스 — DX12 RHI 재작성 · 디스패치 낭비 제거 | todo |
| `L4` · PHASE 4.7 | 백그라운드 베이킹 — 에디터를 막지 않는다 | todo |
| `L5` · PHASE 4.7 | 간접광 — progressive 누적 | todo |
| `L6` · PHASE 4.7 | dilate·seam 처리 — padding 원인 수정 | todo |
| `L7` · PHASE 4.7 | 병렬화 판정 — 재고 나서 연다 | blocked |

대시보드 상태는 인계용 식별 정보다. 위 구현 기반에서 이미 충족한 하위 요구는 재작성하지 않고, 남은 제품/실패/비용 수용을 수행한다.

## master 소스 근거

모든 행의 파일·행 번호·Git blob은 고정 master에서 대조했다. 소스 존재·배선 확인과 실행 성공을 구분한다.

- [Engine/RenderEngine/Interfaces/LightMapping.h:6](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Interfaces/LightMapping.h#L6) — `struct [[reflgen::reflect]] LightMapping`

## 검증 경계

이번 정리는 master 소스·문서 전체의 소유권/충돌/링크/HTML 검증이다. 엔진 빌드·GPU 픽셀·SDK/장치·성능·실제 GUI 수용을 새로 실행한 결과가 아니다. 과거 통과는 해당 소스/바이너리 신원에만 유효하다. 남은 제품 수용은 위 요구를 실제 산출물로 닫아야 한다.

<details><summary>이전 절 링크 호환</summary>

이전 절의 완료 이력·구안은 제거했다. 이 링크로 들어온 경우 위의 남은 작업과 단일 소유 계획을 따른다.

<span id="-실측--지금-큐가-하나뿐이다"></span>
<span id="0-왜-이-페이즈인가--gi가-있어도-라이트맵을-쓴다"></span>
<span id="1-삭제된-구현--측정-2026-08-25"></span>
<span id="11-무엇이-있었나"></span>
<span id="12-구조--코루틴-프레임-페이싱"></span>
<span id="13-알고리즘--여덟-군데"></span>
<span id="14-조용히-틀리는-결함-다섯"></span>
<span id="15-살릴-것과-버릴-것"></span>
<span id="2-병렬화-설계"></span>
<span id="21-병렬-축-전수"></span>
<span id="22--순서--낭비-제거가-병렬화보다-먼저다"></span>
<span id="23-a-스레드-모델"></span>
<span id="23-백그라운드-베이킹--에디터를-막지-않는다"></span>
<span id="24-병렬화가-이득이-아닌-경우"></span>
<span id="3-설계-결정"></span>
<span id="31-uv1-언랩은-xatlas-로"></span>
<span id="32-uv1-은-옵셔널-스트림이다"></span>
<span id="33-텍셀-밀도가-정본이다"></span>
<span id="4-이행--슬라이스"></span>
<span id="5-완료-기준"></span>
<span id="6-리스크"></span>
<span id="7-다른-계획과의-관계"></span>
<span id="l0-기준선--삭제된-구현-복구계측"></span>
<span id="l1-uv1-언랩--xatlas-벤더링"></span>
<span id="l2-bvh-재작성--sah--nth_element"></span>
<span id="l3-직접광-패스--dx12-rhi-재작성"></span>
<span id="l4-백그라운드-베이킹--에디터를-막지-않는다"></span>
<span id="l5-간접광--progressive-누적"></span>
<span id="l6-dilateseam-처리"></span>
<span id="l7-병렬화-판정--여기서-비로소-잰다"></span>
<span id="sec-0-왜-이-페이즈인가--gi가-있어도-라이트맵을-쓴다"></span>
<span id="sec-1-삭제된-구현--측정-2026-08-25"></span>
<span id="sec-2-병렬화-설계"></span>
<span id="sec-3-설계-결정"></span>
<span id="sec-4-이행--슬라이스"></span>
<span id="sec-5-완료-기준"></span>
<span id="sec-6-리스크"></span>
<span id="sec-7-다른-계획과의-관계"></span>
<span id="재사용할-수-있는-것--없는-것"></span>
<span id="필요한-것-셋"></span>

</details>
