# RG4 의존성 wave·병렬 기록 — 2026-10-03 완료

ExplicitVersioned의 살아남은 RAW/WAR/WAW edge에서 dependency wave와 deterministic critical path를 계산한다. 제거된 패스의 wave는 -1이다. critical path는 가장 긴 의존 패스 수이며 CPU/GPU 시간 추정이 아니다. DiagnosticSnapshot은 기존 자원·버전·edge 원인과 함께 wave와 critical path를 제공한다. native dump의 reason 값은 RAW=0, WAR=1, WAW=2다.

RecordParallel은 기존 연속 compiled-order 단위의 워커 배치를 유지한다. 각 패스의 모든 slice를 같은 recording wave에 넣고, 선행 패스의 join과 워커별 커맨드 리스트 append 순서를 함께 만족시킨다. 워커 리스트는 한 번 열어 전체 wave를 기록한 뒤 닫는다. GPU 제출은 기존 워커 순서로 compiled order를 보존한다. dependencyWaveCount와 recordingWaveCount를 구분한다. 후자는 커맨드 target의 append 제약으로 더 커질 수 있다.

단일 워커와 비용 임계값 fallback은 한 recording wave의 순차 경로를 사용한다. DeclarationOrder와 ExplicitSingleWriter의 기존 병렬 경로는 유지한다. Job 또는 callback 실패 시 뒤 wave를 기록하지 않고 기존 CloseAll 폐기 경로로 반환한다.

## 검증 범위

- Debug/Release 빌드: Build/rg4-Debug-after-final-build.log, Build/rg4-Release-after-final-build.log.
- native 24 shuffle: A→B→C와 독립 D, culled pass, critical path 및 실제 snapshot의 edge 원인 dump.
- 실제 DX12 GPU: 즉시 실행과 1/2/4 워커, split producer join, 비용 fallback, compiled order, readback 일치와 encoder drop 0.
- 현재 소스와 바이너리 해시를 묶은 Debug/Release 독립 프로세스 기준선, RG3 변경 전후 및 구성 간 이미지 대조.

제품 기본은 DeclarationOrder다. 제품 접근 선언 이관은 RG5, 기본 경로 전환은 RG6다. IBL 1024/4096과 기존 이미지 상한을 유지한다. MAT-9 품질·성능 게이트는 열려 있고, Lattice 재생은 선택 확장, Vulkan 비교는 PHASE 4.9다. 성능 개선은 이 슬라이스의 완료 주장에 포함하지 않는다.
## 기준선 재정렬과 실패 기록

착수 HEAD는 8bfd0be5다. 기존 RG3 artifact 이후의 현재 HEAD는 Entity layer 안정 ID를 요구한다. 최초 Debug-regression-v1은 native RG4를 통과했지만, legacy fixture의 m_layer/m_collisionType을 현재 로더가 거부해 캡처 전에 실패했다. current-fixture/Project의 고정 씬 네 Entity만 Default 안정 ID 1로 옮겼다. 원본 fixture는 보존했고 재질·기하·카메라 선언은 유지했다. fixture-precheck-v1은 씬 로드 및 정상 종료 코드 0을 확인했다.

RG4 소스를 보존한 뒤 현재 HEAD의 RG4 적용 전 소스를 재빌드해 Debug-before-v1/Release-before-v1을 확보했다. 이후 RG4를 복원해 동일 fixture로 Debug-regression-v2/Release-regression-v2를 검증했다. 과거 RG3 해시를 현재의 변경 전 증거로 재사용하지 않는다.

Release 최초 링크와 재시도는 MSVC C1001/LNK1000 내부 오류로 실패했다. 두 벤더 C 소스의 /GL 제외 시도에서도 오류가 다른 C++ 소스로 이동해 설정 변경을 모두 되돌렸다. 기존 CreatorEditor.runtime.iobj/ipdb를 Release-ltcg-cache-before-refresh에 보존하고 원래 Release 설정으로 전체 LTCG 링크를 다시 실행했다. 169,061 함수 전체 코드 생성이 통과했고 후속 변경 전 증분 링크도 통과했다. 캐시 재사용을 제외한 이후 오류는 재발하지 않았지만, 컴파일러 내부의 정확한 원인은 확인하지 않았다. 최종 소스 변경에 프로젝트 최적화 설정 변경은 없다.

source-after에서 파일을 복원한 첫 after-build는 파일 시각 보존으로 재컴파일을 생략해 중단했다. after-final-build는 세 파일의 시각을 갱신해 실제 재컴파일한 최종 로그다. 완료 기록은 최종 native RG4 marker와 현재 바이너리·소스 해시를 요구하며 중단된 빌드 로그를 증거로 사용하지 않는다.

## 최종 결과

Debug/Release after-final 빌드와 native 검사·고정 장면 회귀를 모두 통과했다.

- Build/Obj/RenderRG4/{Debug,Release}-regression-v2/result.json: 구성별 독립 프로세스 2개×캡처 2개, artifact/static fixture 게이트 통과, 소스 변경·실패 0, 정상 종료 코드 0, replayExtensionsRequested=false.
- 각 dx12-0/graph-fixtures.results.jsonl: RG4_WAVES_OK shuffles=24, 실제 RG4_DIAGNOSTIC wave/critical path/resource-version-edge dump, RG4_GPU_OK immediate workers=1/2/4 fallback split join compiled-order pixels=0 drops=0. 기존 RG1~RG3 검사도 통과했다.
- Debug-before-after-v1, Release-before-after-v1, Debug-Release-v1/comparison.json: 각각 16개 이미지 maxError=0·exceededPixels=0. 구성 간 환경·자산·tuning identity도 일치했다.
- before-baseline-phase-complete-v1.json은 현재 HEAD의 RG4 적용 전 기준선 해시를 적용 전에 확인한 기록이다. Before-Debug-Release-v1도 16개 이미지 오차 0이다.
- current-baseline-phase-complete-v1.json은 최종 현재 소스·executable/runtime DLL 해시를 재검증한 두 구성 기준선이다. rg4-completion-v1.json은 이 기준선과 native RG4·변경 전후 대조를 결합한 완료 기록이다.

두 설정 모두 IBL 1024/4096과 기존 이미지 상한을 유지했다. 다음은 RG5 제품 접근 선언 이관이며 성능 개선·MAT-9·선택 재생·Vulkan 수용을 이번 완료에 포함하지 않는다.