# Desloppify C++ 분석 후 엔진 개선 과제 — 2026-10-29

## 결론과 범위

Desloppify 전체 C++ 스캔의 11단계는 완료됐다. 검출 결과를 현재 코드와 대조한 대표 사례에서는 즉시 수정할 제품 결함을 확정하지 못했다. 엔진에서 우선 확인할 사항은 **Editor 모델 재적재 작업의 실패가 종료 및 완료 처리에서 관찰되는 경로**다. 그 밖의 발견은 호출 계약을 확인하거나 분석기의 정확도를 보완한 후 다시 판정한다.

이 문서는 대표 진단의 코드 대조와 후속 작업을 기록한다. 28,610건 전체를 개별 검토한 결과가 아니다. 스캔 중 일부 cppcheck 실행은 시간 초과됐고, 분석 상태는 스캔 당시의 작업 트리를 반영한다. 빌드, 제품 실행, 성능 측정은 이 분석에서 수행하지 않았다.

## 엔진 개선 후보

### E1. Editor 모델 재적재 작업의 실패 관찰 경로 확인

- **현상:** [`EditorAssetDatabase::Stop`](../../Editor/EngineEntry/Assets/EditorAssetDatabase.cpp#L848)은 `m_modelReloads`의 각 `job.wait()`에서 `std::exception`을 잡고 기록 없이 다음 작업으로 진행한다. [`job_handle::wait`](../../Engine/Contracts/Concurrency/Creator.Concurrency.Jobs.ixx#L73)의 계약은 작업 오류를 보고한다고 명시한다. 평상시 [`TickAutomaticChanges`](../../Editor/EngineEntry/Assets/EditorAssetDatabase.cpp#L1289)는 `is_complete()`인 핸들을 제거한다.
- **이미 있는 처리:** [`ScheduleModelReload`](../../Editor/EngineEntry/Assets/EditorAssetDatabase.cpp#L1085)와 [`requestModel`](../../Editor/EngineEntry/Assets/EditorAssetDatabase.cpp#L1359)의 작업 본문은 `std::exception`을 잡아 각각 자동 재적재·준비 실패를 로그에 남긴다. 따라서 종료 catch가 빈 것만으로 해당 예외가 유실된다고 판단하지 않는다.
- **남은 확인:** 두 작업의 본문과 오류 기록 자체 밖에서 생긴 작업 오류가 `wait()`에 전달되는지, 완료 핸들을 `is_complete()`만으로 제거할 때 오류를 다른 곳에서 관찰하는지, `Stop()` 호출 스레드와 종료 순서가 무엇인지 추적한다. 두 작업의 모든 실패가 기존 로그에서 관찰된다면 추가 로그를 넣지 않는다.
- **개선 조건:** 실제로 누락되는 실패 경로가 확인되면 해당 경계에서 작업 종류와 실패를 한 번만 기록하고, 종료 중 캡처 해제와 작업 drain 순서를 유지한다. 같은 예외를 작업 본문과 종료 경계에서 중복 기록하지 않는다.
- **완료 판정:** 정상 종료 및 확인된 실패 경로에서 모델 작업 종료·기존 참조 유지·오류 관찰 여부를 관련 회귀 검사로 확인한다. 오류 주입이 가능하지 않다면 정적 근거와 실행하지 못한 범위를 명시한다.

### E2. 길이 0인 fixed_array 원소 접근 계약 점검

- [`fixed_array<T, 0>`](../../Engine/Containers/fixed_array/fixed_array.h#L370)의 `data()`는 `nullptr`이고 `operator[]`, `front()`, `back()`은 이를 역참조한다. 크기 0인 배열에서 이 함수들을 호출하면 널 역참조가 발생한다. `at()`은 예외를 던진다.
- 빈 컨테이너의 원소 접근은 유효한 사용이 아니므로 이 구현만으로 정상 호출 경로의 버그나 추가 방어 코드의 필요성을 단정하지 않는다. 실제 호출자가 정적 크기 0을 허용하는지와 기존 컨테이너 계약·회귀 검사를 먼저 확인한다.
- **완료 판정:** 유효하지 않은 호출이 실제 경로에 있으면 호출 지점의 계약을 수정한다. 그런 경로가 없다면 기존 계약과 검사 근거를 기록하고 구현은 유지한다.

## 코드 대조로 제외한 대표 진단

| 진단 | 판정과 근거 |
|---|---|
| `dynamic_array::emplace`가 반환 후 무효인 포인터를 반환 | [`emplace(position, ...)`](../../Engine/Containers/dynamic_array/dynamic_array.h#L575)은 `emplace(size_t, ...)`가 반환한 현재 배열 원소 참조의 주소를 돌려준다. 후자는 교환 후 `p_[pos]`를 반환한다. 해당 경고는 오탐이다. |
| Physics 회귀 검사의 접촉 이벤트가 매달린 참조 | [`Contacts()`](../../Engine/SceneRuntime/ScenePhysicsSimulation.h#L54)는 `m_contacts`를 가리키는 span을 반환한다. [`physics_b2_probe`](../../Tools/regression/physics_b2_probe.cpp#L87)의 참조는 검증 구간에서 변경되지 않는 그 원소를 가리킨다. 해당 경고는 오탐이다. |
| `dynamic_array` 이동 대입의 `noexcept` 위반 | [`operator=`](../../Engine/Containers/dynamic_array/dynamic_array.h#L350)의 할당 경로는 `!propagate_move`일 때만 컴파일되고 그때 `noexcept(propagate_move)`는 false다. 해당 경고는 오탐이다. |
| `publicKeyToken` 하드코딩 비밀 | [Editor](../../Editor/EngineEntry/Bootstrap/App.cpp#L85)와 [Player](../../Player/PlayerApp.cpp#L37)의 값은 Windows Common Controls 매니페스트의 공개 토큰이다. 비밀 유출 경고는 오탐이다. |
| 모든 빈 catch를 오류 은폐로 분류 | 예를 들어 [Vulkan 파이프라인 완료 처리](../../Engine/RenderEngine/RHI/Vulkan/VulkanPipelineCache.cpp#L1135)는 대기 예외를 잡은 뒤 `CompleteGraphics`에서 작업 진단을 처리한다. 각 catch의 소유권과 후속 경로를 확인해야 한다. |

## 분석 체계 개선 과제

이 과제들은 엔진 결함의 확정이 아니라, 다시 실행할 분석 결과를 신뢰할 수 있게 만들기 위한 선행 작업이다.

1. **실제 C++ 빌드 설정 연결:** Visual Studio 프로젝트의 include 경로, 정의, 언어 옵션과 번역 단위를 반영하는 `compile_commands.json` 또는 동등한 입력을 마련한다. 생성 방식이 모듈과 프로젝트 구성을 보존하는지 확인한다. 현 스캔의 누락 include 정보와 미사용 판정은 단독 수정 근거로 쓰지 않는다.
2. **분석 대상 일치:** 원본 C++ 파일, `.ixx`와 테스트는 포함하고, 외부 라이브러리와 `obj`, `x64` 등 생성 산출물은 제외한다. 스캔 당시 `obj`·`x64` 파일에도 진단이 기록됐다. 파일 발견, cppcheck, 보안, 테스트 연결이 같은 대상 목록을 사용하는지 확인한다.
3. **테스트 연결 집계 수정:** 현재 검사기는 영역 분류의 전체 파일 목록을 생산 파일로 가져와 37,980개로 집계했다. 이는 C++ 파일 수가 아니다. C++ 대상 목록으로 제한하고 Visual Studio 프로젝트 및 실제 테스트 실행 경로를 연결한 뒤, 추정과 실행 커버리지를 따로 보고한다.
4. **cppcheck 시간 초과 추적:** 파일별 실패 목록과 원인을 남기고, 모듈 및 대형 번역 단위에서 실제 컴파일 설정으로 재실행한다. 현재 스캔은 시간 초과를 보고하지만 실패 파일의 전체 목록을 보존하지 않았다.
5. **`.ixx` 지원 검증:** 현재 로컬 설치의 파일 발견·cppcheck 호출 대상에 `.ixx`를 추가했으며 결과에 `.ixx` 진단이 기록됐다. 이는 C++ 모듈의 import 그래프와 의미 분석이 정확하다는 증거는 아니다. 작은 모듈 fixture로 발견·참조 그래프·진단 위치를 검증하고, 분석기 업데이트 시 로컬 보완이 사라지는 점을 해결한다.
6. **오탐 분류:** cppcheck 정보 메시지와 실제 오류를 분리한다. 보안 결과에 누락 include·미사용 함수가 섞이는 원인을 보정하고, 수정 여부는 각 항목의 코드와 실제 호출 경로를 대조해 결정한다.
7. **인코딩:** 테스트 연결 검사에서 UTF-8 디코딩 실패가 난 12개 파일의 실제 인코딩과 저장소 정책을 확인한 뒤 분석 입력 처리를 정한다. 진단을 맞추기 위해 소스 인코딩을 일괄 변경하지 않는다.

## 스캔 결과와 사용 제한

- 결과: [상태 파일](../../.desloppify/full-cxx.json), [실행 로그](../../.desloppify/full-cxx-scan-retry.log). 둘 다 로컬 분석 산출물이며 `.desloppify`는 Git 추적 대상이 아니다.
- 저장된 열린 항목 28,610건 중 cppcheck 21,479건, 테스트 연결 2,598건, 참조자 없음 1,713건, 보안 분류 372건이다. cppcheck 항목에는 정보 9,817건과 스타일 10,036건이 포함된다. 원시 단계 합계 120,125건과 상태 파일의 항목 수는 서로 다른 집계다.
- `.ixx`에 연결된 항목은 9,747건이다. 같은 실험 사본의 중복과 분석기 파싱 오류가 포함되므로 각각을 엔진 결함으로 세지 않는다.
- 분석기는 strict 18.3/100을 표시했으나 주관적 평가 20개가 미평가 0점이고 위 집계 문제와 시간 초과가 있다. 이 점수나 항목 수를 엔진 품질·수정 완료 목표로 사용하지 않는다.
- 다음 분석의 완료 기준은 스캔 명령 종료뿐 아니라 대상 범위, 실제 빌드 설정, 파일별 도구 성공/실패, 인코딩, 대표 오탐 검증까지 확인하는 것이다.