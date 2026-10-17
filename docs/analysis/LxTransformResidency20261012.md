# LX 변환 결과의 활성 장면 수명 — 2026-10-12

사용 중인 월드 변환 결과를 256개·32MiB FIFO 한도로 퇴출하던 정책을 제거했다. LX 단일 커널·작업 목록·메시별 출력 UAV는 그대로 사용한다. 호환 경로, 이전 커널, A:B 선택지는 추가하지 않았다.

## 수명과 메모리

- 업로드 recording마다 캐시 세대를 한 번 진행한다. 빈 장면도 `SceneHost::Prepare`에서 진행하므로 삭제·장면 전환 뒤 미사용 출력이 남지 않는다.
- 같은 정적 입력·커널·world·bone key가 다시 사용되면 활성 상태를 갱신한다. GPU 완료를 기다리는 결과도 활성 상태를 유지한다. 실제 재사용은 기존 완료점·SRV 상태 검사를 통과한 결과만 허용한다.
- 최근 두 recording에서 사용하지 않았고 외부 참조가 없으며 GPU 완료를 기다리지 않는 출력의 캐시 참조를 회수한다. 제출된 프레임·모션 소비자는 각자의 소유권으로 결과를 계속 보존한다. 참조 중인 출력은 회수하지 않는다.
- 정적 입력의 256개·128MiB 값은 미사용 입력을 정리하는 기준이다. 활성 출력이 참조하는 입력은 이 기준을 넘어도 보관한다. 입력 한도를 이유로 보관을 포기하던 분기는 제거했다.
- 활성 장면의 메모리는 실제 입력·변환 출력 크기에 따라 증가한다. 사용이 끝난 출력 소유자와 재사용 가능한 GPU 버퍼 풀은 별개다. 기존 버퍼 풀의 최대 64개 보관 정책은 바꾸지 않았다.
- 작업 목록 4096개·packed input 256MiB 제한은 그대로다. 이번 변경은 장면 규모 제한 확대나 CPU 탐색 최적화가 아니다. cooked geometry·셰이더 바인딩 ABI는 변경하지 않았다.

## Debug / Release GPU 검증

최종 `Debug-Accepted`, `Release-Accepted`는 실행 전후 소스·바이너리 동일, 정상 quit·exit 0, GPU validation 문제·dropped message 0이다. 기존 CPU meshlet 72/72, GPU 그림자 목록·payload 검사, 스키닝·RenderGraph·LX HDR 픽셀 검사와 단일 dispatch 6개 사례도 통과했다.

512개의 서로 다른 정적 입력과 525,312 포인트를 사용했다. 출력은 42,024,960 bytes로 기존 32MiB 한도를 넘는다. 모든 포인트를 readback하여 독립적인 world 위치 기준과 비교했다.

| 단계 | 변환 패스 | 준비 패스 | native dispatch |
|---|---:|---:|---:|
| 최초 변환 | 1 | 1 | 1 |
| 변경 없는 다음 프레임 | 0 | 0 | 0 |
| 변경 없는 다음 프레임 | 0 | 0 | 0 |
| 입력 하나 교체·world 변경 | 1 | 1 | 1 |

추가로 evaluator 완료 통지를 지연한 5개 recording에서 활성 결과 4개를 유지했다. 완료 통지를 반영한 프레임은 native dispatch 0이었다. 미사용 장면으로 세대를 진행했을 때 외부에서 참조하는 출력 하나만 보존했고, 그 참조를 해제한 뒤 캐시 출력 소유자·출력 bytes는 0이었다. 미사용 정적 입력도 256개·128MiB 기준 이내로 정리됐다.

## 실제 에디터의 512개 장면

Release, GPU validation off, native real 프레임, 현재 소스·runtime SHA 고정, 독립 프로세스 1회다. 실제 모델 강제 재임포트와 원자적 512개 복제 명령을 사용했다. 최초 변환은 카메라 LOD·그림자 LOD0의 **1024개 작업·1,001,984 포인트**, 변환·준비 패스 각각 1개, native dispatch 1회였다.

예열 뒤 출력 **80,315,280 bytes(약 76.6MiB)**를 유지했다. 2.1229초 동안 CPU 렌더 프레임은 91개 진행했고 변환·정적 업로드 카운터 증가량은 모두 0이었다. GPU realFrameId 518~614의 native 변환 영수증은 0개이며, 완결된 `.ceprof`에서 이 구간의 실제 GPU 프레임 92개를 확인했다. 두 구간 끝 프레임이 기록에 존재하고, 이 프레임들은 LX 결과 재사용을 소비하며 변환·준비 패스가 없다. CPU 프레임 수·GPU ID 차이·실제로 기록된 GPU 프레임 수는 서로 다른 값이며 동일하게 세지 않는다.

최초 변환 GPU 패스 시간 9.5522ms, 해당 뷰 전체 CPU 기록·제출 시간 306.6642ms는 **단일 표본**이다. NVML 전후 표본은 P8, graphics 615MHz, memory 405MHz이며 커널 순간 클럭을 증명하지 않는다. 성능 개선·변동률 수용·장면 예산 통과를 판정하지 않는다. 기존 측정과의 A:B 비교도 하지 않았다.

완료되기 전의 결과는 재사용하지 않으므로 초기 여러 recording에서 중복 변환이 생길 수 있다. 이번 실행의 안정 구간 변환 카운터는 2052였다. 검증한 것은 최초 프레임의 단일 dispatch와 예열 후 재변환 제거이며, 초기 전체 구간에서 변환 작업이 한 번씩만 발생한다는 주장은 하지 않는다.

## 실패 검토와 보존

- 첫 프로젝트 준비에서 원본의 큰 `Library` 캐시까지 복사되는 것을 발견해 GPU 실행 전에 중단했다. 검증 디렉터리 안의 부분 복사본만 정리했고 실제 디스크 여유 증가 22.023GiB를 기록했다. 이후 기존 검증의 약 15MB 프로젝트를 사용했다.
- 첫 Debug 전체 실행의 캐시 GPU 검사는 통과했지만 `DirectionalLightGizmo` 텍스처가 COPY_DEST 상태인 채 SRV로 사용되는 GPU validation 오류 1건 때문에 전체 수용을 거부했다. 실패 로그·캡처·review를 보존했다. 이 텍스처 문제를 이번 캐시 수정으로 고쳤다고 판단하지 않는다. 완료 대기 수명·미사용 입력 정리 보강 후 최종 Debug/Release 실행은 오류 0이며 해당 오류는 재현되지 않았다.
- 성공 캡처는 구성·검사 가족별 대표를 유지하고 모든 oracle·정상 종료 후 retention을 완료했다. 이전 성공 픽셀을 정리한 경우 판정·retention ledger는 남지만 해당 픽셀 replay는 불가능하다. 로그·identity·판정·원시 `.ceprof`·power CSV는 보존한다.

## 증거와 재현

- [최종 통합 판정](../../Build/Verification/LxTransformResidency-20261012/final-verdict.json)
- [Debug GPU 판정](../../Build/Verification/LxTransformResidency-20261012/Debug-Accepted/transform-verdict.json)
- [Release GPU 판정](../../Build/Verification/LxTransformResidency-20261012/Release-Accepted/transform-verdict.json)
- [실제 장면 재사용 판정](../../Build/Verification/LxTransformResidency-20261012/Production512/residency-verdict.json)
- [최초 변환 측정](../../Build/Verification/LxTransformResidency-20261012/Production512/performance-verdict.json)
- [소스 검사](../../Build/Verification/LxTransformResidency-20261012/source-audit.json) · [변경 SHA](../../Build/Verification/LxTransformResidency-20261012/scope-manifest.json)

`verify-lx-transform-dispatch.ps1`로 Debug/Release를 검사한다. 실제 장면 검사는 `measure-lx-transform-dispatch.ps1 -Counts 512 -Iterations 1 -RequireWarmReuse`로 실행한다. 이어 `verify-lx-transform-performance.py --iterations 1 --counts 512`와 `verify-lx-transform-residency.py`를 같은 측정 디렉터리에 실행한다. 프로젝트·출력 디렉터리는 명시하며 출력은 새 디렉터리를 사용한다.

다음 우선순위는 최초 변환 CPU 비용의 작업 수집·입력 복사·할당·descriptor·그래프 기록 분리 측정이다. Vulkan 실행 수용은 예정대로 Phase 4.9다.
