# TR0 — 실제 BASE-0 population과 RG6 소비자

## 범위

이 후속은 앞선 저장 행 metadata 검사에서 실제 비용 population 수집으로 진행한다.
SDK 활성 Editor Debug/Release 재빌드와 각 구성의 native C++ 회귀 3종이 통과했다. 렌더러 진단 스냅샷과 명령 API에
구조화된 native CPU recording 비용·표본 수·자기 제출의 시간축 신원을 추가했다.
정본은 `Artifacts/PR169/tr0-base0-workload-20261011/checkpoint.json`이고,
최종 판정은 passed=true이며, 아래 결과는 현재 바이너리와 원래 명령 응답을 대조한 실행 증거다.

제품 dependency-order 경로를 각 구성 독립 두 프로세스로 실행한다. 첫 프로세스는
100회 graph capture, 두 번째는 2회이며 각 프로세스에서 서로 다른 GPU 프레임의
실제 CPU recording/GPU pass 합계 표본을 100개 수집한다. RG6 소비자의 공용
population admission을 실제 저장 행에 적용한다. 테스트용 CPU 0을 대입하지 않는다.

fixture는 현재 authored plane·camera·directional light와 상수 재질로 구성한 작은
격리 프로젝트다. 역사적 lattice `LX_CookFixture` 또는 전체 legacy/product RG6 A/B와
동일한 workload라고 주장하지 않는다. 전체 frame time, master 성능 차이, 제품 예산,
실제 Player generated 출력, UI screenshot 및 외부 기구 수용은 이 검사 범위 밖이다.

## 구현과 용량

BASE-0 하네스에 30GiB floor·bounded matrix 사전 추정·각 캡처 전 용량 확인을 연결했다.
모든 oracle과 정상 종료·프로세스 간 비교 이후 구성별 첫 capture만 보존하며,
공동 Debug/Release 비교에는 이 두 대표를 사용한다. 나머지 로그·manifest·판정·해시는
보존하고 raw 제거 ledger에 재생 불가를 기록한다. `-KeepRawCaptures`는 pending A/B 등
추가 비교가 필요한 경우에만 사용한다. 바이너리 전체 사본을 만들지 않는다.

작은 fixture에 Shaders 폴더가 없으면 DefaultPassShader가 잘못 복사되는 오류를
수정했다. 최초 실패의 명령 결과·stdout/stderr를 `Debug-first-failed-shader-layout`에
보존했으며 재시도에서 해당 fixture 경로를 바로잡았다.

기존 하네스는 scene.switch의 요청 접수를 완료로 오인했다. 이제 cold model을 준비한 뒤
scene.load.status의 Ready를 요구하고, 실패/취소/stale는 즉시 실패한다. 제작 중 fixture의
누락 모델 신원과 inline texture 준비 실패는 별도 실패 로그에 보존했다. 최종 fixture는
명시 모델 GUID와 상수 재질을 사용하고 생성 도구를 저장해 재현 가능하게 했다.
manifest의 실제 환경 경로·해시가 환경 증거이며 fixture에 들어간 HDR 파일을 사용했다는
추정으로 대체하지 않는다.

Debug 실제 수집에서 텍스트 CPU 상태의 try-lock이 거의 항상 busy여서 비용 표본을
수집할 수 없음을 확인했다. 최종 경로는 렌더 스레드가 짧은 debugMutex로 발행하는
snapshot에 nativeRecordMs·samples·provenance를 묶어 보존한다. API의 nativeRecord를
소비하고 각 저장 행을 원래 명령 응답과 대조한다. GPU 측정 창과 CPU 제출 신원은
독립이며, 전체 CPU frame time으로 이름을 바꾸지 않는다. codec/wire 형식은 바꾸지 않는다.

첫 전체 Debug 시도는 두 프로세스의 UI 상태가 달라 64×64와 332×182의 입력 신원
비교에서 실패했다. native graph 자가검사가 같은 프로젝트에 UI 설정을 저장한 것이
원인이므로 이제 별도 GraphFixtureProject에서 실행한다. 비교 임계값이나 입력 신원
검사를 완화하지 않았다. 서로 다른 해상도의 실패 대표 두 캡처는 보존하고, 해당 실패의
나머지 raw 1,122개는 제거했다. 기록은 Debug-selftest-ui-contamination에 남겼다.

RG6의 population admission은 표본 수·실프레임 provenance·CPU/GPU 축·측정 GPU 신원·
query coverage와 GPU 프레임의 증가 순서를 검사한다. 중간 행 generated 오표기,
해상도 누락, display FPS 축 혼합, NaN CPU, GPU 누락과 population 누락·중복·역순을
변이로 검사한다. 기존 전체 cutover 게이트도 이 공용 admission을 사용한다.

## 최종 실행 결과

- SDK 활성 Debug/Release 빌드 성공, 구성별 display-diagnostics·temporal-provenance·profiler-measurement 회귀 통과(asserts enabled).
- 각 구성 독립 프로세스 2개, 총 4개 모두 정상 종료. 각 프로세스 서로 다른 native CPU 측정 100개와 GPU 프레임 100개, 총 실제 비용 표본 400개.
- 그래프 캡처 총 204개(구성별 100+2). 모든 manifest/입력 신원/원래 명령 응답 결합 검사 통과. 최종 extent는 모두 332×182이며 전경 깊이 픽셀 33,646개다.
- 구성 내부 반복 비교와 Debug/Release 공통 비교 통과. 공통 비교의 17 attachment 모두 maxError=0, RMSE=0. GPU validation 문제·query 누락 0.
- population 변이 15개 × 4프로세스 = 60개 거부. 별도 artifact 변이 29개씩 4프로세스에서 거부.
- 기존 텍스트 상태는 각 프로세스 97/101/99/101회 busy였지만 typed nativeRecord로 100개씩 정상 수집했다. CPU 비용은 자기 제출의 graph recording이며 전체 CPU frame time이 아니다. GPU pass sum도 wall frame time과 구분한다.

| 구성/프로세스 | CPU graph recording 중앙값(ms) | GPU pass sum 중앙값(ms) |
|---|---:|---:|
| Debug/0 | 3.89615 | 2.30783 |
| Debug/1 | 3.80985 | 2.30595 |
| Release/0 | 2.37685 | 2.23826 |
| Release/1 | 2.34195 | 2.24062 |

서로 비동기인 두 측정 축을 더해 전체 frame time으로 해석하지 않는다. 이 정적 작은 fixture 결과는 master 성능 개선이나 제품 예산 충족의 증거가 아니다.

성공 구성별 정리에서 raw 1,133개씩 제거해 각각 약 0.95GiB를 회수했다.
최종 정리에서 불필요한 실패 raw 32개도 제거했다. 성공 대표 2개와 UI 설정 오염으로
해상도가 달랐던 실패 대표 2개만 raw로 남겼다. 로그·manifest·판정·profile·입력은 보존했고,
재생 불가 ledger와 storage-end.json을 저장했다. 바이너리 전체 사본은 0개다.
최종 드라이브 실측 여유는 93.21GiB다. 삭제한 파일의 논리 크기를 회수량으로 합산하지 않는다.

최초 실패들은 수정 후 fresh 실행으로 대체했고 allAttemptsPassed=false를 유지한다.
현재 checkpoint의 runtime/source hash가 최신 정본이며 앞선 tr0-surfaces-sdk 체크포인트는
그 당시 바이너리의 역사적 증거다.

## 잔여 수용

TR0는 progress·기성 0, PHASE 4.5는 12/71 유지한다. 이번에 종료한 항목은 현재 제품의
native BASE-0/RG6 저장 population 소비자다. 다음 수용은 legacy/product 전체 RG6 A/B와
미포함 workload, UI 육안/화면, 실제 Player generated 신원과 외부 기구 계측이다.
제품 전체 frame/예산 및 master 비교도 별도 증거가 필요하다.


2026-10-11 후속: 현재 소스 legacy/product SDK D/R 전체 RG6 행렬(8프로세스·800표본·408캡처)은 [TemporalRg6ABAcceptance20261011](TemporalRg6ABAcceptance20261011.md)에서 수용됐다. raw 대표 4개와 최소 진단 바이너리만 보존, 실측 여유 92.37GiB. UI/actual generated/외부 계측/제품 예산은 잔여다. 이전 체크포인트의 미확인 표기는 당시 상태로 유지한다.
