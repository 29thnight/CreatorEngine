# Output Log의 제한된 저장소와 증분 뷰

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 등록 경로를 2026-10-15에 대조했다. 이 개정에서는 빌드·실행·화면 검증을 하지 않았다. 과거 결과와 현재 구현의 존재를 구분한다.

## 저장과 표시를 분리하는 이유

`LogStore`는 엔진 로그를 모으는 thread-safe 값 저장소이고 `OutputLogView`는 ImGui에 의존하지 않는 표시 모델이다. `LogSink`가 spdlog 입력을 저장소로 넘기고 `OutputLogWindow`가 증분 결과를 읽어 그린다. UI 창이 닫혀 있어도 로그 저장·제한 정책이 적용된다. 이 저장소는 디스크의 영구 로그 파일이나 무제한 이벤트 이력이 아니다.

기본 한도는 occurrence 500개, group 500개, 소유 문자열 8MiB다. group은 level, logger name, message, source file/function/line의 전체 동일성으로 묶인다. hash가 같아도 전체 필드를 비교하므로 충돌만으로 다른 메시지를 합치지 않는다. 원문은 group이 한 벌 소유하고 occurrence는 sequence, group ID, timestamp를 참조한다. 용량을 넘는 오래된 항목과 더 이상 참조되지 않는 그룹을 퇴거시킨다. 너무 큰 단일 입력은 거부 카운터에 기록한다.

## 카운터와 동시성

Append/Clear/snapshot/delta는 저장소 mutex 아래에서 일관된 복사본을 만든다. group ID와 sequence는 Clear 뒤에도 단조 증가하고 clear generation이 바뀐다. 따라서 지워진 행과 새 행이 같은 배열 위치에 있다고 선택을 계승하면 안 된다.

상태바의 totals는 Clear 이후 만들어진 로그의 누계다. trace/debug/info는 messages, warn은 warnings, err/critical은 errors이며 off는 제외한다. 이미 퇴거된 로그와 oversized로 저장 거부된 입력도 누계에 포함된다. 현재 필터에 보이는 행 수나 메모리에 남은 occurrence 수와 totals를 같게 만들지 않는다. 사용자에게 누계, 저장된 수, 퇴거·거부 수를 구분해 보여 주어야 한다.

## 증분 읽기와 UI 선택

cursor는 revision, clearGeneration, lastSequence를 가진다. `ReadDeltaSince`는 변화가 없으면 optional 결과를 생략한다. 최초 읽기, Clear 이후 또는 제거 이력이 cursor보다 오래되면 full resynchronization을 반환한다. 나머지는 변경/제거 group, 새 occurrence, oldest sequence와 카운터를 전송한다. UI는 오래된 행을 trim하고 group 변경을 병합하며 필요한 때만 필터 결과를 재구축한다.

선택은 group ID와 sequence로 유지하고 필터·Clear·퇴거로 대상이 없어지면 해제한다. collapse는 group 단위이며 첫 occurrence 순서에 대응하는 group ID 순서다. 비collapse는 occurrence sequence 순서다. severity와 message/logger 검색을 적용하며 검색의 대소문자 처리는 ASCII 기반이다. 전체 Unicode case folding을 제공한다고 설명하지 않는다.

행 표시용 문자열은 줄바꿈·공백/NUL 등을 정리하지만 상세용 원문은 보존한다. 폭 맞춤 ellipsis는 UTF-8 경계에서 자르고 실제 측정 폭에 맞춘다. ImGui clipper가 보이지 않는 줄을 건너뛰는 것과 저장소의 용량 제한은 독립적인 최적화다.

## 수용 경계

`verify-log-storage.ps1`과 저장소/뷰 계약을 함께 대조한다. 검증할 핵심은 hash 충돌, 동일 text의 다른 source/level, oversized 입력, 연속 Clear, 느린 cursor의 resync, 선택된 행 퇴거, collapse/filter 전환, 다중 producer다. 별도 시각 수용은 긴 UTF-8 메시지의 truncation·복사·스크롤·색 대비를 확인한다.

과거 누적 로그 폭주 사례가 있었다는 사실은 현재 bounded 저장소의 누수 증거가 아니다. 반대로 한도가 있다고 파일 로그, spdlog producer, 팝업 detail 또는 allocator의 전체 메모리가 같은 8MiB에 갇힌다는 뜻도 아니다. 이번 개정은 실행·스트레스 측정 없이 현재 소유권과 알고리즘을 확인했다.

## 구현 근거

- [Engine/Utility_Framework/LogStore.h](../../Engine/Utility_Framework/LogStore.h)
- [Engine/Utility_Framework/LogSink.h](../../Engine/Utility_Framework/LogSink.h)
- [Engine/Utility_Framework/LogGroup.h](../../Engine/Utility_Framework/LogGroup.h)
- [Editor/EngineGUIWindow/OutputLogView.h](../../Editor/EngineGUIWindow/OutputLogView.h)
- [Editor/EngineGUIWindow/OutputLogWindow.cpp](../../Editor/EngineGUIWindow/OutputLogWindow.cpp)
- [Editor/EngineGUIWindow/MenuBarWindow.cpp](../../Editor/EngineGUIWindow/MenuBarWindow.cpp)
- [Tools/regression/verify-log-storage.ps1](../../Tools/regression/verify-log-storage.ps1)
