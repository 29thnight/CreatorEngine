# DLSS Native AA 첫 이동 잔상

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- camera cut revision·성공한 submission의 history·provider 설정과 profiler provenance를 보존한다. saved intent와 실제 지원/활성 결과를 구분한다.
- SDK/CPU Present 반환은 생성 프레임 신원이나 물리 표시 시각이 아니다. 과거 일부 품질 통과를 현재 모든 route·장치·FG/지연 수용으로 확대하지 않는다.

이 문서는 연구/운영/표현 가이드 성격이므로, 아래 파일은 관련 엔진 경계의 근거다. 문서의 제안 전체가 구현됐다는 증거로 해석하지 않는다.

### 해당 구현에서 확인한 선언·소유 경계

- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp:216](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp#L216) — `const auto started = std::chrono::steady_clock::now();`
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp:230](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp#L230) — `std::chrono::steady_clock::now() - started).count();`
- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp:307](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp#L307) — `const uint64_t generation = std::exchange(token.captureGeneration, 0);`
- [Engine/RenderEngine/Camera.h:18](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Camera.h#L18) — `class [[reflgen::reflect]] Camera`
- [Engine/RenderEngine/Camera.h:47](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Camera.h#L47) — `static constexpr math::vector3 kForward = math::vector3::unit_z();`
- [Engine/RenderEngine/Camera.h:48](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Camera.h#L48) — `static constexpr math::vector3 kRight = math::vector3::unit_x();`
- [Player/PlayerMain.cpp:56](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Player/PlayerMain.cpp#L56) — `auto* window = WindowsCoreWindow::GetForCurrentInstance();`
- [Player/PlayerMain.cpp:82](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Player/PlayerMain.cpp#L82) — `const Authoring::TextParseTelemetrySnapshot parseTelemetry = Authoring::GetTextParseTelemetry();`
- [Player/PlayerMain.cpp:83](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Player/PlayerMain.cpp#L83) — `std::printf("[runtime.text-parser] calls=%llu\n", static_cast<unsigned long long>(parseTelemetry.calls));`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

## 원인 분리와 수정

남은 DLSS 품질 모드를 검사하던 중 Native AA의 첫 이동 프레임이 기존 HDR/display 잔상 기준을 초과했다. 이후 모드 행렬을 중단하고 이 실패를 먼저 수정했다. Quality에만 지정돼 있던 preset J를 Native AA의 `dlaaPreset`에도 크기 질의/dispatch 양쪽에서 지정했다. Balanced/Performance/Ultra Performance의 기본 설정은 유지한다.

고정된 실제 RT warmup 34회에서 기존 기본 모델과 J의 17프레임 × SDK 입력 11종 raw SHA256이 모두 같았다. 현재/이전 jitter, SDK 시간, reset 및 품질 값도 동일했다. 이 입력에서 모델 선택이 결과 차이를 만든다는 근거이며, SDK 내부 구현 원인을 단정하는 것은 아니다. 폴백이나 판정 허용치 변경은 없다.

Debug 첫 이동 HDR 평균 잔상은 0.1578491 → 0.00010984, p95는 0.3663462 → 0.00019231이다. Display 평균은 0.2337381 → 0.00157984, p95는 0.4782609 → 0.01449275이다. 기준 평균 0.10/p95 0.25는 유지했다.

## 수용 범위

DX12 RTX 2080 Ti, Native AA, display/render 760×312, 고정 SDK 60 Hz, 불투명 이동/사라짐/회복 fixture에 한정한다. 최종 제품 실행 및 비교 결과는 `Artifacts/PR169/dlss-qualities-20261011/checkpoint.json`에 기록한다. 전체 PHASE 4.5 완료 또는 TU3 전체 품질 수용을 뜻하지 않는다.

Native AA startup, Balanced/Performance/Ultra Performance, 다른 해상도, 실제 모델/재질/얇은 형상, 가변 시간, reset/resize, 깜빡임/성능 검사가 남는다. J의 깜빡임 교환 조건은 이전 Quality 분석과 동일하게 남아 있다.

## 저장 관리

기존 실패 전체 1개는 원인 분석용으로 보존한다. 수정 성공 원본은 입력 비교 및 Debug/Release 비교가 끝난 뒤 구성별 첫 이동 대표만 남기며 판정/로그/manifest와 삭제 ledger는 보존한다. 전체 바이너리 사본은 만들지 않는다.

최종 Debug/Release 빌드 및 34캡처 통과. 구성 간 17프레임 × 12 attachment 해시 동일, 구성별 history 506단정 통과, GPU validation 메시지/문제/누락 0, 정상 종료 0. 비교 완료 후 성공 raw 1.44 GiB를 회수했고 여유 85.71 GiB를 확인했다. 전체 시퀀스 raw는 삭제돼 과거 전체 픽셀 재생은 불가하며 대표 frame-1과 판정/해시를 보존했다.

</details>
