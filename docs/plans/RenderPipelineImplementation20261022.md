# 렌더링 파이프라인 구조 개선 구현 기록

기준: `master`의 `ea87639ece7b6068456940ddddd83a9d23f86c58`.
이 문서는 별도 PR의 실제 구현·검증 상태를 기록한다. 작업 중인 항목을 완료로 표시하지 않는다.

## 수용 범위와 현재 상태

| 범위 | 현재 구현 | 남은 작업 |
| --- | --- | --- |
| 불변 GraphProgram / 프레임 바인딩 | 작업 중 | 선언 재사용, 정확한 바인딩 검증, 동적 recording 비용 분리, live 통합 |
| 뷰별 준비 입력과 패스 상태 | 작업 중 | 공유 Scene 수집 결과와 뷰별 불변 입력 분리, 패스 인스턴스 격리, 수명 회귀 |
| 타입이 있는 기능 합성 | 작업 중 | 리소스 형식·extent·선택 기능 계약, 세 기여 지점, 엔진·Editor 노드 통합 |
| 뷰별 승인·갱신 정책 | 1차 구현 | 전체 live 경로 통합 검토, 실제 호스트 갱신·부하 검증 |
| subresource / 다중 큐 / aliasing | 작업 중 | 범위별 hazard·backend barrier, 공유 읽기 증명, happens-before 수명, 실패 회귀 |
| CPU executor / GPU queue / GPU 표시 | 작업 중 | 정책 분리, 번역·제출 경계, 완료 집합, Vulkan GPU 표시와 소비 lease |

## 구현된 동작

`LiveViewScheduler`는 고정 크기 저장소로 픽셀 작업을 선택한다. 기본 Game은 연속 갱신,
Scene은 최대 60 Hz, Material Preview는 입력 revision 변경 시 갱신한다. Host는 요청의
`updatePolicy`로 이를 바꿀 수 있다. Game을 먼저 승인하고, 기본 250 ms 동안 대기한 다른
뷰에는 다음 credit을 부여해 기아를 막는다. 진단 capture와 명시적 camera cut·scene·resize
변경은 갱신 제한을 통과한다. 실패하거나 표시 lease가 막힌 시도는 갱신을 소비하지 않는다.

RT 입력 승인은 두 뷰의 credit을 한꺼번에 예약하던 조건을 제거했다. 한 credit만 있으면
입력을 소비하고, 각 backend의 뷰 루프가 총 GPU 제출 상한 2 안에서 선택한다. Scene 수명
delta 적용·병합·종료 drain과 GPU/Host 완료 회수는 픽셀 갱신 정책과 별도로 유지한다.

## 검증 기록

- Linux / GCC 13 / C++20: `LiveViewSchedulingTests.cpp` 컴파일 및 실행 통과.
  우선순위, 한 credit 승인, 총 제출 상한, 독립 주기, dirty 재시도, revision·scene·resize·cut,
  진단 강제 갱신, 시계 역행, 지속 부하에서의 기아 방지, 잘못된 입력 경계를 확인했다.
- 변경한 Scene 경로의 `git diff --check` 통과.
- Windows Editor 빌드, DX12/Vulkan GPU validation, 실제 픽셀·수명·성능 검증: 미실행.
  현재 환경은 Linux이며 Windows SDK와 GPU 호스트가 없다. 위 단위 검증으로 이를 대체하지 않는다.

재현:

```sh
g++-13 -std=c++20 -Wall -Wextra -Werror Tools/regression/LiveViewSchedulingTests.cpp -o /tmp/creator-live-view-scheduling
/tmp/creator-live-view-scheduling
```

## 중단·재개 경계

이 PR은 진행 중인 구현의 체크포인트다. 첫 커밋에는 뷰별 승인 정책과 실제 live 호출 경로,
독립 회귀만 포함한다. 위 표의 나머지 구현과 통합 검증이 완료되기 전에는 전체 완료로 보지 않는다.
GPU 자원은 기존 생산자 완료·소비자 lease·device-loss 증명 계약을 유지하며,
지원되지 않는 최적화에 대해 검증 조건을 제거해서 활성화하지 않는다.
