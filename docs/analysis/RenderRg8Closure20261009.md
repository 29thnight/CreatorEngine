# RG8 DX12 종료 및 기본 OFF 채택 판정 — 2026-10-09

**후속 구조 재감사로 제품 완료 판정 철회:** [RG7·RG8 감사](RenderRg7Rg8StructuralAudit20261009.md)에서 패스별 생성/제출·중복 상태 계획·overlap 없는 SSAO 배치·RG7 조합 거부를 확인했다. RG8은 progress·기성 0이다. 아래 수치와 정확성 회귀는 보존하지만, 이 문서의 done/PHASE 4.3 잔여 RG9만이라는 결론은 더 이상 유효하지 않다.

RG8의 큐 배치·실행·인계·수명·제품 SSAO compute 파일럿과 성능/메모리 비교를 완료한다.
**성능 개선을 수용한 것은 아니다. 기본 경로(0)를 유지하고 owned graphics(1),
owned graphics + SSAO compute(2)는 opt-in으로 남긴다.** RG8 done·기성 16인일이며
PHASE 4.3은 RG9만 남는다. Q0·RG7을 재개방하거나 이 결과를 Vulkan/L4 수용으로 확대하지 않는다.

## 구현 및 정확성 증거

- 기존 [실행·제품 소비 기록](RenderRg8QueueExecution20261009.md)의 VS 2026(v18/v145)
  Debug/Release 빌드, 구성별 native/frame/profiler 135·queue plan 24·전체 RenderGraph 회귀를 유지한다.
  큐별 calibration, 지연 producer/consumer, frame retirement, 제출 거절/격리/회수,
  세 뷰·재질 편집·씬 교체·리사이즈는 그 기록의 범위다.
- 이번 Release 제품 비교는 실제 Editor와 기존 Dynamic_CPP/TestShadow를 사용한다.
  1496×692, graph draw 7개, 동일 카메라/조명/변환/포즈/재질 입력이다.
  순방향 0→1→2와 역방향 2→1→0을 각각 독립 프로세스로 실행했다.
  RG7 aliasing과 lifetime extension은 OFF로 고정해 두 최적화를 섞지 않았다.
- 각 프로세스 8회, 총 48캡처의 7개 attachment를 기준 캡처와 비교했다.
  **336개 출력 비교 최대 절대 오차 0**, 입력 일치, query overflow/drop 0, exit 0이다.
  이 캡처의 진단 readback은 일반 GPU 성능 표본에서 제외했다.
- 최신 Release 바이너리의 GPU validation 활성 native/queue plan/전체 RenderGraph 회귀 결과는
  `Build/Verification/Phase43/RG8AdoptionNativeRelease20261009`에 보관한다.
  native/frame/profiler 135·queue plan 24·전체 회귀 통과, validation 오류 0·exit 0이다.
  이전 Debug 결과와 이번 Release 재실행을 구분하며 이번 턴의 새 Debug 빌드를 주장하지 않는다.

## 성능 및 메모리 비교

별도 6개 Release Editor 프로세스에서 모드당/순서당 **32개의 서로 다른 일반 GPU 프레임**,
총 192프레임을 수집했다. PBR 진단 readback 패스 없는 프레임만 사용했고
validation은 성능 측정에서 OFF다. 큐별 clockValid, busy≤span,
span violation/query overflow/drop 0을 확인했다. 아래 시간은 calibrated GPU queue span이며
CPU 프레임 시간/FPS 또는 패스 시간 합계가 아니다.

| 실행 순서 | 모드 | GPU span 중앙값 ms | p95 ms | GPU busy 중앙값 ms | 표본 최대 DXGI 사용량 MiB |
|---|---:|---:|---:|---:|---:|
| 순방향 | 0 기본 | 2.4274 | 2.9553 | 2.4151 | 1632.43 |
| 순방향 | 1 owned graphics | 5.5393 | 15.8976 | 2.7264 | 1450.09 |
| 순방향 | 2 SSAO compute | 9.8182 | 11.6859 | 4.0490 | 1453.50 |
| 역방향 | 2 SSAO compute | 5.1881 | 12.4304 | 2.4864 | 1496.12 |
| 역방향 | 1 owned graphics | 5.5567 | 16.5724 | 2.3972 | 1498.71 |
| 역방향 | 0 기본 | 2.6086 | 3.6465 | 2.5974 | 1498.75 |

1↔2는 compute 분산 효과, 0↔1/2는 실행 구조 변경을 포함한 비교다.
2의 중앙값은 순방향에서 1보다 높고 역방향에서 낮지만, 두 순서 모두 0보다 높다.
따라서 compute 분산의 일관된 이득과 기본 경로를 대체할 성능 이득을 확보하지 못했다.
이 결과만으로 특정 fence/allocator/driver 비용을 원인으로 확정하지 않는다.

DXGI memory sampler는 프로세스 시작/씬 준비/일반 측정/종료를 포함해 약 100ms 간격으로 조회했다.
6프로세스의 유효 표본 1846개, 일반 프레임 관측 시간 창 내 표본 844개다.
두 범위의 최대 사용량은 이번 실행에서 동일했다. 창은 GPU 프레임 조회 시각으로 경계를 잡았으며
정확한 GPU timestamp별 메모리 상관관계가 아니다. 순방향의 감소가 역방향에서 재현되지 않아
일관된 메모리 절감은 주장하지 않는다. 표본 최대 DXGI budget usage는 정확한 physical residency,
자원별 committed bytes 또는 조회 사이의 순간 최대치를 뜻하지 않는다.

진단 캡처 CPU record 중앙값은 순방향 0/1/2에서 0.7958/17.7386/17.8940ms,
역방향 0/1/2에서 0.7969/18.0589/13.8936ms였다.
readback과 캡처 실행을 포함하므로 일반 제품 CPU 비용/FPS로 사용하지 않는다.

## 종료 판정과 재현 자료

기존 정확성/큐 수명 수용과 이번 동일 입력의 픽셀·GPU critical path·메모리 비교로
RG8의 bounded DX12 구현 및 채택 판정을 닫는다. 개선 미입증으로 **기본 OFF**다.
이는 측정 결과에 따른 미채택이며 성능 목표를 통과했다고 표시하는 것이 아니다.
추가 최적화나 확대 workload의 채택 검토는 후속 변경의 근거를 갖춰 별도 진행한다.
RG9의 range/subresource·split barrier/fallback·읽기 전용 Inspector는 아직 미착수다.

- 캡처: `Build/Verification/Phase43/RG8Adoption20261009Final`.
- 일반 프레임/메모리/집계: `Build/Verification/Phase43/RG8Normal20261009`,
  특히 `adoption-measurements.json`, 최신 Release native 결과까지 결합한 `closure-result.json`과
  각 모드의 `normal-frames.json`, `memory-continuous.jsonl`.
- 재현: `Tools/regression/measure-rg8-adoption.ps1`의 기본 캡처 실행과 `-TimingOnly` 실행 후
  `Tools/regression/audit-rg8-adoption.py`로 입력/336출력/192프레임/메모리/바이너리를 함께 감사한다.
- 두 시리즈 exe SHA256: `B88F346F3310919A2C52C0DB384885C59FA233AD65F3201FDB75F758F7241561`.
- runtime SHA256: `1929E18D1BBECB39D8969C132DAA48B51D2EB45D3F8F3F7219A118ED36CF76BD`.
- Git HEAD는 `329fad205f6560fc4462354b9287d64a68049c8a`이며 누적 로컬 변경이 적용된 바이너리다.
  HEAD만으로 실행 소스를 대표하지 않는다. native 결과의 소스 해시와 기존 빌드 기록을 함께 보관한다.
- 최초 `RG8Adoption20261009` 실행은 하네스가 polling URL을 덮어써 route.unknown으로 실패했다.
  하네스 수정 뒤 전체 두 순서로 재실행했으며 최초 실패는 수용 표본에서 제외하고 보존했다.
  기존 Release 재질 게시 실패의 원인 미확정 기록도 이전 보고서에 유지한다.
- 대시보드 JavaScript 전체 파싱/렌더 및 진행률 finite 검사는 통과했다.
  `verify-plan-dashboard.ps1`의 별도 문자열/shape 스캐너는 기존 JSON 형식 RTP 14행을
  잘못 판독해 실패한다. HEAD 기준과 갱신 후 모두 동일한 140 string/14 shape 오류이며
  새 오류가 아니다. 두 감사 로그를 일반 측정 디렉터리에 보존하고 전체 스크립트 통과로 주장하지 않는다.
