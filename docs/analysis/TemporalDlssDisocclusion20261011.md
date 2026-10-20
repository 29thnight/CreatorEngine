# DLSS Quality 첫 이동 잔상 수정 — 2026-10-11

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- camera cut revision·성공한 submission의 history·provider 설정과 profiler provenance를 보존한다. saved intent와 실제 지원/활성 결과를 구분한다.
- SDK/CPU Present 반환은 생성 프레임 신원이나 물리 표시 시각이 아니다. 과거 일부 품질 통과를 현재 모든 route·장치·FG/지연 수용으로 확대하지 않는다.

### 해당 구현에서 확인한 선언·소유 경계

- [Engine/RenderEngine/RHI/Temporal/DlssTemporalAdapter.cpp:44](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/RHI/Temporal/DlssTemporalAdapter.cpp#L44) — `if (message) std::fprintf(stderr, "[Streamline:%u] %s\n", static_cast<unsigned>(type), message);`
- [Engine/RenderEngine/RHI/Temporal/DlssTemporalAdapter.cpp:430](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/RHI/Temporal/DlssTemporalAdapter.cpp#L430) — `DlssTemporalAdapter::DlssTemporalAdapter() : m_implementation(std::make_unique<Implementation>()) {}`
- [Engine/RenderEngine/RHI/Temporal/DlssTemporalAdapter.cpp:436](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/RHI/Temporal/DlssTemporalAdapter.cpp#L436) — `::ce::contracts::assert_that(`
- [Engine/RenderEngine/RHI/Temporal/DlssTemporalAdapter.h:72](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/RHI/Temporal/DlssTemporalAdapter.h#L72) — `enum class DlssLatencyMarker : uint8_t`
- [Engine/RenderEngine/RHI/Temporal/DlssTemporalAdapter.h:78](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/RHI/Temporal/DlssTemporalAdapter.h#L78) — `enum class DlssDX12ProxyKind : uint8_t { Device, Factory, Swapchain };`
- [Engine/RenderEngine/RHI/Temporal/DlssTemporalAdapter.h:79](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/RHI/Temporal/DlssTemporalAdapter.h#L79) — `enum class DlssPacingOwner : uint8_t { Unspecified, Streamline, ApplicationWaitableObject };`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

## 판정

**이전 턴의 재현 실패는 수정·검증 완료. PHASE 4.5 전체 완료는 아니다.**
정본: `Artifacts/PR169/dlss-ghost-20261011/checkpoint.json`.
SDK 활성 최종 Debug/Release 빌드에서 warmed 및 startup 4개 독립 프로세스·68개 캡처가 통과했다.
각 구성의 제품 바이너리 해시를 실행 기록과 대조했다. PHASE 4.5는 12/71을 유지한다.

## 원인 분리

1. 기존 검증기는 GT가 관측한 warmup 횟수를 셌다. 렌더 스레드의 실제 제출 수와 달라 실행마다 이동 시작의 jitter 위상이 달라졌다. 따라서 SDK 전환 여부와 성공/실패의 상관관계를 원인으로 볼 수 없었다.
2. 렌더 스레드의 제출 원장과 명시적인 진단 시작 reset을 추가했다. warmed는 실제 non-reset 34회, startup은 1회 후 기준 자세를 유지한다. 캡처 admission을 기다리는 동안 히스토리를 추가 진행하지 않는다. 원장은 최대 128개로 제한하며 일반 렌더링의 warmup/jitter 정책은 바꾸지 않는다.
3. 고정 조건의 기본 모델 실패는 **원래 실패의 raw 289개와 SHA256이 전부 같았다**. 검사 장면을 다른 것으로 바꾸지 않았다.
4. 같은 조건에서 Quality preset만 J로 바꿨다. 기준+측정 17프레임의 SDK 입력 11종(색상·깊이·모션·마스크·HDR 입력 등), 현재/이전 jitter, SDK clock이 모두 동일한 상태에서 품질 실패가 사라졌다. SDK 내부 모델 구현의 결함을 규명했다는 뜻은 아니며, 이번 통제 비교가 확인한 원인 범위는 **기본 모델 선택의 해당 입력 응답**이다.

## 제품 수정

`DlssTemporalAdapter.cpp`에서 Quality 모드의 preset J를 크기 질의와 실행 옵션에 동일하게 지정했다. 다른 품질 모드는 독립된 기본 설정을 유지한다. FSR 폴백, 매 프레임 reset, 모션 무효화 또는 판정 허용치 완화로 잔상을 숨기지 않았다.

NVIDIA의 [DLSS 가이드](https://raw.githubusercontent.com/NVIDIA/DLSS/main/doc/DLSS_Programming_Guide_Release.pdf) §3.12는 J가 잔상을 줄이는 대신 깜빡임 측면의 교환 조건이 있음을 명시한다. 따라서 이 변경을 전체 장면의 화질 개선이나 TU3 전체 완료로 확대하지 않는다. 세부 텍스처·얇은 형상·실제 모델의 깜빡임과 성능 비교는 남아 있다.

## 측정 결과

DX12 RTX 2080 Ti, 창 1280×720, 실제 display 760×312/render 507×208, Quality, 명시적 SDK 60 Hz 입력, 불투명 이동 fixture 범위다. 물리 표시 간격 측정이 아니다.

| 첫 이동 프레임 | 기존 기본 모델 | J — 최종 Debug와 Release 동일 |
|---|---:|---:|
| HDR 평균 잔상 | 0.2781983 | 0.00022424 |
| HDR p95 | 0.5026933 | 0.00038476 |
| display 평균 잔상 | 0.3817974 | 0.00075870 |
| display p95 | 0.6086957 | 0.01449275 |

기존 허용치는 평균 0.10/p95 0.25로 그대로다. warmed와 startup 모두 기준 프레임+이동/사라짐/회복 16프레임을 검사했다. 각각 D/R 17프레임×12 attachment 해시가 동일하다. 독립 Debug 재실행도 동일했다. 모든 최종 실행은 validation 문제·메시지·누락 0, 정상 종료 0이다.

히스토리 회귀는 구성별 506개 단정·실패 0. 렌더 스레드 warmup 경계에서 정지/재개와 일반 native 경로를 포함한다. warmup 기록 축소, 현재 jitter 변조, 이전 jitter 변조, 고정 warmup 메타데이터 삭제의 4변이는 모두 거부했다. 현재 SDK 실행기는 `--require-exact-warmup`을 요구하므로 과거 가변 warmup 형식으로 조용히 후퇴하지 않는다. 과거 자료의 읽기 호환성은 유지했다.

## 남은 범위

이번 첫 이동 잔상 종료 차단은 해결됐다. TU3의 다른 품질·해상도, 실제 모델/재질/얇은 형상, 가변 시간, reset/resize 및 깜빡임/성능 행렬은 미완료다. TR0 UI/실제 생성 프레임 계측, FG 수용, 다른 GPU 및 외부 물리 지연 계측도 이번 결과로 대체하지 않는다.

## 저장 관리

최종 구성별 warmed/startup 대표 frame-1 총 4개와 고정 조건의 기본 모델 실패 전체 1개를 보존했다. 원래 실패의 중복 raw는 289개 해시 일치 확인 후 제거했다. 중간 성공 원본도 비교/변이 검사 완료 뒤 제거했다. 모든 판정·manifest·로그·해시는 보존하며, 삭제 ledger가 과거 전체 픽셀 재생 불가를 명시한다.

실제 **3.42 GiB 회수**, 종료 여유 **86.59 GiB**, 30 GiB 하한 유지. 전체 바이너리 사본은 만들지 않았다. 앞으로 SDK 품질 실행기는 configuration/provider/quality/warmed-or-startup별 성공을 관리하고, 추가 비교용 `KeepRawCaptures`가 없으면 성공 후 첫 이동 대표만 보존한다. 저장 정책 회귀 검사도 통과했다.

</details>
