# CreatorRobot Debug 재측정 10 · 2026-10-19

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](../../../MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- 제품 live graph는 명시적 버전과 dependency order를 사용한다. 선언 metadata 재사용과 dependency recording은 현재 구현 기반이다.
- 소프트웨어 RHI의 frozen recording은 독립 target의 병렬 native 번역과 조기 sealed-batch handoff를 지원한다. 직렬 전용 번역·별도 C# SRP 저작 전환을 후속 필수 작업으로 두지 않는다.
- queue/alias/hazard·reader join·retirement를 보존하고 실제 overlap·fallback·whole-frame 비용으로 RG8 채택을 판정한다.

### 해당 구현에서 확인한 선언·소유 경계

- [Engine/RenderEngine/MaterialGraphShaderMeta.h:20](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/MaterialGraphShaderMeta.h#L20) — `static_assert(std::is_constructible_v<own::shared_owner<const LX::Runtime::ShaderGeneration>,`
- [Engine/RenderEngine/MaterialGraphShaderMeta.h:21](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/MaterialGraphShaderMeta.h#L21) — `own::shared_owner<const GeneratedMaterialShader>>);`
- [Engine/RenderEngine/MaterialGraphShaderMeta.h:22](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/MaterialGraphShaderMeta.h#L22) — `static_assert(!std::is_constructible_v<own::shared_owner<const LX::Runtime::ShaderGeneration>,`
- [Engine/RenderEngine/MaterialGraphShaderMeta.cpp:54](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/MaterialGraphShaderMeta.cpp#L54) — `throw std::runtime_error("Unknown Graph color space");`
- [Engine/RenderEngine/MaterialGraphShaderMeta.cpp:60](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/MaterialGraphShaderMeta.cpp#L60) — `if (!std::isfinite(narrowed))`
- [Engine/RenderEngine/MaterialGraphShaderMeta.cpp:61](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/MaterialGraphShaderMeta.cpp#L61) — `throw std::runtime_error("Graph default exceeds finite float32 range");`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

[측정 보고서](../../../measurements/20261019-remeasure-10/report.html) · [RenderGraphDeclare 내부 분석](../../../measurements/20261019-remeasure-10/declare-analysis/index.html)

- 측정 HEAD: `946ef4fa9b79f4e74ad09a03d79aa117277ded87`.
- 로컬 최적화는 제외했다. 원격 소스의 정의 헤더 누락을 `MaterialGraphShaderMeta.h` include 한 줄로 보정했다. `measured-source.patch`에 남겼다.
- CreatorRobot Debug 플레이, 워커 8개, Editor 전경, Scene+Game 1186×514, 워밍업 5초, 기록 ON/OFF 각 15초. ETW ON 수집과 ETW OFF 반복 수집을 구분한다.
- Game CPU 중앙값 11.687ms, 반복 11.706ms. Debug 120프레임 CPU 예산 8.333ms 미달. 완료 뷰/초는 화면 FPS가 아니다.
- 비교 기준은 9차 역사 수집이며 로컬 변경을 포함한다. 차이를 PR만의 인과 효과로 단정하지 않는다.
- Debug 빌드, DX12 Scene/Volume 회귀, 플레이 정상 종료, 렌더 출력 6종 일치, ETW 이벤트/버퍼 손실 0을 확인했다. 정확성 probe는 DebugLayer ON, GPUValidation OFF였다.

## 자료 범위

수치·검증·출처와 전체 선언 스택 집계 JSON을 게시한다. `provenance.json`은 게시 파일과 로컬 원본의 SHA256을 보관한다. `after/identity.json`과 `repeat/identity.json`은 측정 바이너리의 해시를 보관한다.

전체 ETL·ceprof·125MB 타임라인 및 바이너리/PDB는 Git 제외 대상인 `Artifacts/remeasure-10-20261019`에 남아 있다. 이 게시본만으로 전체 원본 타임라인을 재구성할 수는 없다.

`declare-analysis/scopes.py`와 `export.ps1`은 저장소 루트에서 실행했던 분석 절차이며 로컬 Artifacts 원본이 필요하다. 스코프 분해는 Game 396뷰, ETW 표본 스택은 Scene+Game을 포함한 렌더 TID 26056의 DeclareAll 호출 경로다. Inclusive 비중을 합산하거나 per-frame 절감량으로 환산하지 않는다.

</details>
