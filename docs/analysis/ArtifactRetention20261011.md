# 검증 산출물 보존과 재현 가능성

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 현재 도구의 계약

Tools/regression/ArtifactRetention.ps1은 capture 전에 실제 drive 여유 공간을 확인한다. Assert-ArtifactCapacity의 기본 최소치는 30GiB다. 이는 모든 workload의 충분한 예산이 아니라 하한이며 대형 capture는 예상 raw 크기를 별도로 더해야 한다.

Remove-ArtifactPixels는 지정 root 아래 .f32만 선택한다. root의 조상과 자식 reparse point를 거부하고 keep directory를 제외한다. 먼저 경로·논리 길이를 ledger에 기록한 뒤 삭제하고, 실제 drive free-space 전후 차이를 결과에 남긴다. hardlink를 포함하는 파일 길이 합계는 물리 회수량과 다르다.

## 성공 rollover의 소유자

Complete-ArtifactRetention은 family 이름을 제한하고 같은 부모의 이전 등록 run만 대상으로 삼는다. previous marker의 passed가 실제 boolean true, family 일치, pinned가 boolean false인지 확인한다. KeepRawCaptures는 pin으로 취급한다. 실패·다른 family·부모 밖 run을 성공 사본처럼 정리하지 않는다.

중요하게도 이 함수는 호출 시 새 run을 passed=true로 기록한다. oracle·정상 종료·필요한 audit를 수행하는 책임은 호출자에게 있다. 함수 이름이나 marker만으로 검증이 실제 실행되었다고 추론할 수 없다. 새 acceptance 이전에 이전 증거를 지우는 호출 순서는 계약 위반이다.

## 역사적 결과

2026-10-11 보고의 회수 GiB, 실행 수와 checkpoint는 당시 storage ledger에 속한다. 이 재작성은 삭제나 정리를 하지 않았으며 그 값들을 현재 여유 공간으로 재인증하지 않는다. 보존된 JSON/JSONL·이미지·profile은 역사적 증거 그대로다. raw가 삭제된 run은 기존 verdict를 읽을 수 있어도 pixel replay는 불가능하다.

후속 자료는 [TemporalMeasurementSurfaces20261011](TemporalMeasurementSurfaces20261011.md), [TemporalBase0ConsumerWorkload20261011](TemporalBase0ConsumerWorkload20261011.md), [TemporalRg6ABAcceptance20261011](TemporalRg6ABAcceptance20261011.md)에 연결된다. 각 보고의 장치·revision·보존 범위를 함께 확인해야 한다.

## 구현 근거

- [Tools/regression/ArtifactRetention.ps1](../../Tools/regression/ArtifactRetention.ps1)
- [Tools/regression/ArtifactRetentionTests.ps1](../../Tools/regression/ArtifactRetentionTests.ps1)
