# 모델 첫 표시의 렌더 스레드 멈춤 추적·최적화

2026-10-21. Debug x64 Editor 재빌드 후 CreatorRobot과 Aria를 실제 DX12 씬에서 계측했다.

최종 실행에서 렌더 스레드 최장 CPU 범위는 **Robot 우선 1,372.208 → 925.832ms**, **Aria 우선 961.856 → 777.571ms**였다. 각각 약 32.5%, 19.2% 짧아졌다. 두 최종 실행에서 관측한 최장 구간은 모두 1초 미만이지만, 수백 ms의 첫 표시 준비 비용은 남아 있다.

배치 완료 대기는 첫 Robot **4.847 → 6.339초**, 첫 Aria **3.611 → 3.989초**로 늘었다. 이번 변경은 필수 페이지 검증을 씬 게시 전에 준비해 렌더 스레드의 긴 동기 작업을 줄이는 변경이다. 모델 전체 로드 시간이 빨라졌다고 주장하지 않는다.

## 원인 추적

기존 프로파일의 MaterialSceneCasterSnapshot은 이름과 달리 단순 목록 복사만 수행하지 않는다. 내부에서 메시 GPU 준비와 첫 클러스터 상주 준비를 호출하고, 필수 root 페이지가 없으면 렌더 스레드에서 읽기·해시·속성 검증까지 끝낸다.

세부 추적을 추가한 실행에서 클러스터 페이지 1,311건의 비용은 다음과 같았다.

| 단계 | 전체 CPU 범위 합계 |
|---|---:|
| 페이지 읽기 | 46.838ms |
| SHA-256 확인 | 34.042ms |
| 페이지 형식·정점 속성 검증 | **1,036.496ms** |

위 수치는 여러 모델·프레임에 걸친 합계로, 단일 프레임 시간과 합산하면 안 된다. 이 추적 실행은 배치 4회와 캡처 저장을 마쳤으나 종료 후 slang-compiler.dll의 0xc0000409 오류로 프로세스가 실패했다. 캡처는 complete=true, 손실 0이어서 원인 진단에만 사용했고, 성공한 전후 비교 표본에서는 제외했다. Windows 오류 이벤트도 보존했다.

## 최종 변경

1. [패킷 Word 읽기](../Engine/RenderEngine/Assets/QuantizedGeometry.h)는 범위 검사를 유지하면서 little-endian 호스트에서 memcpy로 비정렬 32비트 값을 읽는다. 다른 endian에서는 기존 바이트 조립을 유지한다.
2. [페이지 속성 검증](../Engine/RenderEngine/Assets/ClusterTopologyPage.h)은 속성 오프셋, 검사할 scalar 목록, skin 유무와 경계 반경을 페이지당 한 번 계산한다. 유한값·값 범위·법선·접선·인증된 위치 경계·본 인덱스·가중치 검사는 유지한다.
3. [PreparedRootPageReader](../Engine/RenderEngine/Assets/PreparedClusterPages.h)는 기존 인증 reader를 감싼다. 메타데이터 조회만으로 payload를 읽지 않는다. 명시적인 사전 준비 호출에만 Stream이 필수로 고정하는 root 페이지를 검증해 보관한다. 추가 LOD/비필수 페이지는 기존 스트리밍과 은퇴 경로를 유지한다.
4. 준비 캐시는 immutable archive별로 소유하고 메시당 최대 32MiB까지만 보관한다. 세대가 다른 reader는 캐시를 공유하지 않는다. 상한 밖의 페이지는 기존 reader로 처리한다. 검증에 실패한 바이트는 캐시에 넣지 않는다. mutex를 잡은 상태에서 I/O·검증·바이트 복사를 수행하지 않는다.
5. [모델 배치 준비](../Editor/EngineEntry/EditorModelPlacement.cpp)는 실제 노드가 참조하는 메시만 대상으로 root 페이지를 준비한다. 원본 CPU geometry 전체를 요구하거나 collider 비활성 정책을 바꾸지 않는다.
6. 페이지 준비와 재질/배치 recipe 준비를 같은 job_group의 독립 작업으로 실행한다. 의존성으로 선행 모델 준비를 기다리며 작업 스레드에서 다른 작업 완료를 기다리지 않는다. 게임 스레드는 그룹 전체 완료를 확인한 뒤 오류를 관측하고 게시한다. 한 작업이 실패하면 준비 결과를 폐기한다.
7. 취소 정리와 종료 대기도 두 작업의 공통 완료 토큰을 기준으로 한다. recipe의 ready 신호만 보고 아직 실행 중인 페이지 준비 입력을 해제하지 않는다. 재시도 시에도 이전 그룹이 끝난 뒤 새 준비를 제출한다.

첫 구현의 Word/오프셋 최적화만으로는 긴 프레임을 충분히 줄이지 못했다. 이후 순차 사전 준비는 렌더 구간을 줄였지만 배치 대기를 늘려, 최종적으로 재질 준비와 병렬로 실행하도록 보완했다. 미세 페이지 추적 코드는 최종 codec에서 제거했고, 최종 worker의 ModelPlacement.PrepareRootPages 범위와 기존 렌더 준비 범위로 확인했다.

## 전후 비교 조건

- 같은 Dynamic_CPP 프로젝트·GLB·Debug x64·DX12, Debug Layer on / GPU Validation off 조건이다. 파일·OS·shader 캐시는 지우지 않았다.
- 각 프로세스에서 두 모델을 순차 배치한 다음 같은 순서로 재배치했다. Robot 우선과 Aria 우선을 모두 실행했다. 모든 배치 사이에 새 씬을 만들고 동일한 카메라·조명을 설정했다.
- 이전 runtime으로 기준선 두 실행도 이번 작업 중 다시 측정했다. 최종 병렬 빌드 측정은 그 뒤에 실행했다. 이전 runtime 비교 후에는 새 runtime을 복원하고 해시를 확인했다.
- 성능 실행 중 별도 빌드·캡처 분석·기능 검사를 실행하지 않았다. 프로파일 분석과 취소 검사는 성능 실행이 모두 끝난 뒤 수행했다.
- 각 순서 1쌍의 표본이다. 이전 보고서의 최장 1.569초 / 1.199초와 이번 기준선이 다른 것처럼 실행 변동이 있다. 안정된 평균 향상률이나 모든 상황에서 1초 미만이라는 보장은 아니다.
- RenderThreadFrame은 CPU 범위다. GPU 시간, 표시된 FPS, 첫 정상 화면까지의 지연과 동일하지 않다. 순서별 표는 해당 프로세스의 4회 배치 전체에서 관측한 최장 범위이며 개별 모델의 고유 비용으로 해석하지 않는다.

## 렌더 스레드 비교

| 최장 CPU 범위 | Robot 우선 전 → 후 | Aria 우선 전 → 후 |
|---|---:|---:|
| RenderThreadFrame | **1,372.208 → 925.832ms** | **961.856 → 777.571ms** |
| MaterialSceneCasterSnapshot | 890.169 → 326.772ms | 703.688 → 277.827ms |
| MeshClusterResidencyPrepare | **287.485 → 5.546ms** | **302.884 → 2.683ms** |
| ModelPlacement.Tick | 2.335 → 2.663ms | 3.115 → 2.196ms |

클러스터 상주 준비의 전체 범위 합계도 Robot 우선 885.444 → 25.374ms, Aria 우선 925.002 → 19.996ms로 줄었다. 필수 페이지의 인증은 생략하지 않고 worker 준비로 이동했다. 최종 worker 사전 준비의 최장 범위는 각각 854.327ms / 714.331ms였다. 여러 중첩 범위나 서로 다른 스레드의 시간을 더해 프레임 시간으로 해석하면 안 된다.

## 씬 배치 완료까지의 대기

요청 생성부터 씬 적용 완료까지의 벽시계 시간이다. 렌더 스레드 프레임 시간과 별도로 비교한다.

| 실행 순서 / 모델 | 수정 전 | 최종 수정 후 |
|---|---:|---:|
| Robot 우선, 첫 Robot | 4.847초 | 6.339초 |
| 같은 실행, 두 번째 Aria | 3.206초 | 3.681초 |
| 같은 실행, Robot 재배치 | 1.052초 | 1.817초 |
| 같은 실행, Aria 재배치 | 0.272초 | 0.271초 |
| Aria 우선, 첫 Aria | 3.611초 | 3.989초 |
| 같은 실행, 두 번째 Robot | 4.753초 | 4.868초 |
| 같은 실행, Aria 재배치 | 0.275초 | 0.272초 |
| 같은 실행, Robot 재배치 | 1.023초 | 1.625초 |

사전 검증이 게시 조건에 추가되므로 특히 Robot 재배치의 완료 대기가 늘었다. 병렬 준비는 순차 준비 실행의 첫 Robot 7.185초를 6.339초로 줄였지만 기준선보다 빠르지는 않았다. 초기 텍스처 admission 대기와 재질 준비 시간도 실행마다 달라, 총 차이를 모두 한 단계의 효과로 분해하지 않는다. 첫 표시까지의 전체 지연을 이 표로 확정하지 않는다.

## 검증

- 최종 Debug x64 Editor 빌드 성공: Build/render-load-parallel-final-build-20261021.log.
- 최종 두 실행 모두 4완료·0실패·0재시도·새 Recovered 0개, 모든 명령 성공, 프로세스 exit=0.
- 최종 DX12 캡처 8개 모두 finite=true, 332×144. Robot draw 4개·유효 깊이 픽셀 324개, Aria draw 3개·525개로 기준선과 일치했다.
- 비교에 사용한 네 캡처 모두 complete=true, unacked=0, droppedEvents=0. 원시 캡처의 다른 손실 카운터도 0이었다.
- [클러스터 회귀 검사](../Tools/regression/cluster_topology_page_probe.cpp): 8개 레이아웃·52개 검사, 비정렬 Word 9개·범위 거절 3개, topology 손상 13개·잘림 416개·속성 손상 5개 통과. CPU geometry의 정확한 위치/인덱스, signed zero, 잘림 96개·손상 8개도 통과했다.
- 새 준비 캐시는 lazy 조회, root만 사전 준비, 재사용, 비필수 페이지의 기존 읽기, 바이트 상한, 인증 실패 후 재시도, 취소, 세대별 격리를 검사해 통과했다. 실제 엔진과 같은 /Od Debug 조건의 standalone probe를 사용했다.
- 실제 Editor의 undo·씬 전환 검사 통과: cancelled=2, completed=0, failed=0, 저장 씬의 모델 유출 0개.
- 원본 두 GLB의 SHA256은 이전 기록과 일치한다. 계측으로 바뀐 active.workspace는 실행 전 파일로 복원했다. 기존 다른 작업은 유지했고 커밋하지 않았다.

## 남은 비용과 제한

최장 렌더 프레임이 여전히 약 0.78~0.93초이므로 매끄러운 실시간 프레임 수준은 아니다. MaterialSceneCasterSnapshot에도 약 278~327ms가 남아 있고, 기록 계획과 효과 준비 등 다른 비용도 존재한다. 다음에는 이 남은 구간의 pipeline 준비·GPU 자원 준비를 더 세분화하고, recording의 자원 수명과 정확한 입력 게시 조건을 유지하면서 선행 준비 또는 프레임 분산을 검토해야 한다.

새 CPU 캐시는 archive 수명 동안 유지되며 상한은 메시별 32MiB다. 전체 프로젝트의 캐시 사용량을 재설계한 변경은 아니다. 상한을 넘는 필수 페이지가 있는 다른 모델의 첫 프레임 동작은 이번 두 모델 결과로 보장하지 않는다. Release/Vulkan 성능을 계측한 결과도 아니다.

초기 trace-before-robot과 미세 최적화만 적용한 after-robot은 종료 시 오류가 있었으므로 최종 성공 표본에 포함하지 않았다. 최종 준비/병렬 빌드와 이전 runtime 비교 실행은 정상 종료했다. 초기 종료 오류의 일반적인 재현 원인이나 해결을 이번 성능 변경으로 확정하지 않는다.

## 증거 위치

Build/render-load-20261021/ 아래에 다음 자료를 보존했다.

- baseline-robot/, baseline-aria/: 새로 측정한 이전 runtime의 성공 기준선.
- parallel-robot/, parallel-aria/: 최종 성공 실행. commands/results/stdout/execution, 실제 DX12 출력, CEPROF·summary·slow 분석.
- comparison.json, identity.json: 시간·범위·렌더 검증 요약 및 소스/실행 파일/GLB 해시.
- page-probe.log, cancel-final/: codec·cache 회귀 검사와 실제 취소 검증.
- trace-before-robot/, trace-before-shutdown-event.json: 원인 추적 및 종료 실패 증거.
- after-robot/, prepared-robot/, prepared-aria/, final-robot/, final-aria/: 중간 구현 계측. 최종 병렬 빌드 결과와 구분한다.
- measure.ps1, analyze_capture.py, check-cancel.ps1: 재현 도구.

기준선 runtime SHA256: 22FF24E957267C274803263B1256E5B9E8C108CE5DEB97B990C050983C13FDF7.

최종 runtime SHA256: 863E028DE2B06B125B53094E7359E86952D188ECE2BB93E303DE75DE7E0D233D.

이전 단계: [텍스처 시작 대기 최적화 보고서](TextureStartupOptimizationComparison-20261021.md).
