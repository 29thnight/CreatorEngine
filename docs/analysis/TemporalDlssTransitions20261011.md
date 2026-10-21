# DLSS 해상도·품질 전환 및 history reset 검증

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

2026-10-11. 실행 근거: `Artifacts/PR169/dlss-transitions-20261011`.

## 수정

기존 제품에서 Balanced→Quality 전환의 첫 reset HDR이 같은 상태의 camera-cut reset과 최대 0.30114746, 최종 display가 0.12549022 달랐다. 여덟 번째 프레임과 1280×720 host resize에서도 차이가 있었다. 깊이·baseColor·SSGI 및 SDK 직전 HDR은 각 비교 쌍에서 동일했다. 원래 실패 시퀀스와 diagnosis.json을 보존했다.

엔진은 PostChain에서 수동 노출을 사용하지만 DLSS에는 exposure texture를 전달하지 않아 SDK 자동 노출이 선택됐다. `Temporal.Exposure` Pass에서 현재 PostChain 노출값을 R32Float 1×1 텍스처로 초기화하고, `Temporal.Upscale`의 명시적 읽기 의존성과 SDK exposure 입력으로 연결했다. tone mapping이 꺼져 있으면 1을 사용한다. DLSS 이외 provider는 이 Pass를 생성하지 않는다. SDK 내부 자동 노출 알고리즘을 단정한 것이 아니라 입력 계약을 엔진 노출 정책과 일치시킨 수정이다. 이 노출 수정은 reset 정책을 변경하지 않는다. Performance/Ultra Performance의 후속 preset 수정은 아래에 별도로 기록한다.

중간 실행에서 clear 최적화 값 불일치 경고가 나와 리소스 생성 값과 실제 clear 값을 일치시켰다. 이 경고의 로그·manifest는 남기고 픽셀은 제거했다.

Release에서는 요청 당시 최신 씬 발행 프레임보다 오래된 대기 패킷에 품질 변경이 적용돼, 관측 대기 시작 전에 첫 reset이 지나가는 캡처 경합을 발견했다. 진단 capture status에 렌더 스레드가 요청 경계를 통과했음을 뜻하는 ready를 추가했다. 검사는 ready 이후 전환을 발행하고 해당 응답을 근거에 저장한다. 시간 지연이나 재시도만으로 실패를 감추지 않는다.

## 검사 범위

Editor Game 뷰, DX12 RTX 2080 Ti, 고정 SDK 60Hz, 정적 cube/floor 장면이다. 각 품질에서 Quality(대상이 Quality면 Balanced)에서 전환한 첫/여덟 번째 프레임, host 960×640 및 1280×720으로 resize한 첫/여덟 번째 프레임을 같은 최종 상태의 camera-cut reset과 비교한다. 원래 host 크기는 1400×900이며 검사 후 복원한다.

각 쌍은 depth/baseColor/SSGI/HDR을 1e-6, display를 1/255+1e-7 이내로 비교한다. 카메라 위치·60도 수직 FOV·표시 aspect, 실제 DLSS 품질, receipt generation, reset revision, 첫 제출/직전 제출, GPU validation, 정상 종료를 검사한다. 모든 품질 간 20방향 전환 전수나 임의 해상도 전수를 뜻하지 않는다.

## 최종 결과

SDK 활성 Debug/Release에서 다섯 품질 전환·resize 10실행, 120캡처, 60비교를 통과했다. Quality/Native AA/Balanced는 노출·ready 수정 빌드, Performance/Ultra Performance는 해당 두 모드 preset만 추가 변경한 후속 빌드의 결과다. 전부 동일한 최종 바이너리로 재실행했다는 의미는 아니다. 각 실행 해시와 pre-preset-builds.json으로 구분한다. 각 비교의 depth/baseColor/SSGI/HDR/display 최대 차이는 모두 0이며, D/R의 비교 대상과 참조 픽셀 해시도 동일하다. configuration별 각 실행의 history 계약 506단정과 GPU validation 메시지/문제/누락 0, 정상 종료 0을 확인했다.

최종 빌드의 Quality/Balanced/Performance/Ultra Performance startup D/R 8실행·136캡처 및 변경한 Performance/Ultra Performance warmed D/R 4실행·68캡처도 통과했다. 각 모드·종류별 D/R 17×12 attachment 해시가 동일하다. Native AA의 과거 startup 결과는 이전 자동 노출 빌드의 기록이며 이번 startup 회귀에는 포함하지 않았다.

원래 실패와 최종 Debug 첫 품질 전환은 입력 버퍼 6종 및 카메라가 동일했다. `exposure-input-comparison.json` 참조. ready 누락을 포함한 13종 변이도 실제 보존 캡처를 기반으로 모두 거부했다. 전환 시점의 전체 화면 HDR 차이를 없애기 위해 오차 허용치나 초기 reset을 면제하지 않았다.

근거: `checkpoint.json`, `mutations.json`, 각 실행의 `pixels.json`, `recovery.json`, `launch.json`, `validation.json`, `history-contract.json`, 빌드 로그. 원래 화질 실패, clear 경고, 첫 Release 캡처 timeout도 별도 기록으로 남아 있다.

## 저장 및 잔여

삭제 ledger의 실제 회수량 합계 6.625 GiB, 최종 여유 82.84 GiB다. 성공은 구성/품질/검사 가족별 대표 1개만 남기고 원래 노출 실패 및 Performance/Ultra Performance 기본 모델 실패 시퀀스 3개를 보존했다. 중간 clear 경고의 픽셀은 제거했다. 로그·판정·manifest·해시는 유지하며 삭제된 전체 시퀀스의 raw 재생은 불가능하다.

수직 FOV 변경, 멀티카메라/뷰 수명, 가변 SDK 시간, 실제 애니메이션/알파/얇은 형상, 다른 해상도·GPU, 수동 노출 입력의 전체 화질/성능 수용, 실제 FG 및 물리 지연은 남는다. 새 1×1 노출 Pass의 CPU/GPU 비용을 성능 개선으로 판정하지 않았다. TR2/TR3/TU3는 progress, PHASE 4.5 기성 12/71을 유지한다.



Performance/Ultra Performance는 명시적 노출만으로 반복 reset 차이가 사라지지 않았다. 기본 모델의 첫 전환 HDR 최대 차이는 각각 0.83984375/0.67333984였다. 두 모드도 Query/Dispatch에서 preset J를 선택하도록 변경했다. 각 모드의 변경 전후 12프레임 × 입력 6종과 카메라를 비교해 동일함을 확인했다. J의 reset 재현성 수용과 일반 화질 우수성은 구분하며, SDK 헤더가 설명하는 깜빡임 tradeoff와 성능은 실제 장면에서 추가 수용해야 한다.

</details>
