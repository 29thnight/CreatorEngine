# 문자열 전환표

2026-10-28 기준 작업 트리 조사. 이 문서는 전환 기준이며 전환 완료를 뜻하지 않는다. 외부 계약과 데이터의 실제 인코딩을 먼저 확인하고, 생산자부터 소비자까지 한 단위로 변경한다.

## 용도별 타입

| 현재 용도 | 대상 | 경계와 적용 기준 |
| --- | --- | --- |
| 엔진이 소유하는 사용자 표시·편집 텍스트, 내부 표준이 UTF-16인 값 | `ce::string`, 비소유는 `ce::string_view` | UTF-16 코드 단위다. ImGui에 표시할 때 값별 UTF-8 캐시를, 편집할 때 위젯별 입력 상태를 사용한다. |
| 이미 UTF-8인 로그, 문서, JSON, 셰이더 텍스트, ImGui ID 및 UTF-8 프로토콜 | `ce::utf8_string`, 비소유는 `ce::utf8_string_view` | 생산자와 소비자가 UTF-8인지 확인한다. ImGui에 그대로 전달하며 UTF-16으로 왕복하지 않는다. 외부 라이브러리가 표준 타입을 요구하면 경계에서 어댑터를 사용한다. |
| ASCII로 정의된 식별자·숫자 텍스트 | `ce::utf8_string` 또는 `ce::utf8_string_view` | ASCII는 UTF-8의 부분집합이다. 고정 리터럴은 수명과 길이를 유지하고 불필요한 소유 복사를 만들지 않는다. |
| 인코딩이 정해지지 않은 `std::string`, 임의 바이트·바이너리 | 인코딩 확인 전에는 문자열로 일괄 치환하지 않음 | 임의 바이트를 UTF-8 텍스트로 가정하지 않는다. 바이너리는 해당 바이트 컨테이너와 길이 기반 API를 사용한다. |
| Windows `std::wstring`, `std::wstring_view`, `std::filesystem::path::native()` | Windows/파일시스템 경계에서는 기존 타입 유지 또는 명시적 어댑터 | Windows의 `wchar_t`와 `char16_t`는 별도 타입이다. 포인터 재해석으로 치환하지 않는다. 경로는 파일시스템 API의 경로 타입과 수명을 유지한다. |
| `std::u8string`, `std::u8string_view` | UTF-8임을 확인한 뒤 `ce::utf8_string`/view | `char8_t`와 `char`는 별도 타입이다. 길이와 수명을 보존하는 명시적 바이트 경계를 마련한다. |
| `std::u16string`, `std::u16string_view` | `ce::string`/view 후보 | UTF-16 텍스트인지, embedded NUL과 잘못된 서로게이트를 허용하는지 먼저 확인한다. |
| fmt/spdlog, JSON/YAML, OS, IPC, 파일, 플러그인과 공개 ABI | 계약에 맞는 경계 타입 유지 | 라이브러리의 요구 타입·인코딩·길이·소유권을 유지한다. 내부 타입 변경과 외부 계약 변경을 분리한다. |

## 표준 API 대응표

| 기존 | 대응 | 적용 조건 |
| --- | --- | --- |
| `std::string` / `std::string_view` | UTF-8이면 `ce::utf8_string` / `ce::utf8_string_view` | 같은 타입이 아니므로 외부 `std::string_view` 경계는 `{data(), size()}`로 구성한다. |
| `std::u16string` / `std::u16string_view` | `ce::string` / `ce::string_view` | 내부 표현은 `char16_t`다. |
| `size`, `empty`, `data`, `c_str`, `reserve`, `clear`, `assign`, `append`, `push_back`, `erase`, `insert` | 해당 `ce` API | 인자 타입과 NUL 종단, 포함된 NUL을 사용하는 호출을 확인한다. |
| `substr`, `starts_with`, `compare`, `npos` | 해당 문자열의 `view()` API | 반환 view는 소유 문자열의 수명과 재할당에 종속된다. API와 위치 인자를 실제 구현에 맞춰 확인한다. |
| `operator+`, `operator+=`, `replace`, `rfind`, 반복자 기반 알고리즘 | `append` 등으로 호출별 재작성 | 기계적으로 이름만 바꾸지 않는다. 코드 단위 인덱스와 문자열 경계, 중첩 데이터 및 임시 객체 수명을 확인한다. |
| 정렬·해시·`std::format`·스트림·외부 라이브러리 인자 | 호출별 비교자/해시/형식 어댑터 또는 경계 변환 | 기존 키 비교와 해시, 출력 형식 및 ABI를 보존한다. 지원 여부를 확인하기 전에는 표준 타입과 호환된다고 가정하지 않는다. |
| UTF-8 ↔ UTF-16 변환 | `ce::to_utf16`, `ce::to_utf8` | `unicode_result`를 확인한다. 잘못된 입력을 조용히 버리지 않고 호출 경로에 맞는 실패 처리를 정한다. |

문자열 정의와 변환 구현은 `Engine/Containers/string/string.h`를 기준으로 한다. UTF-8의 `size()`는 바이트 수, UTF-16의 `size()`는 코드 단위 수이며 어느 쪽도 사용자에게 보이는 글자 수가 아니다.

## ImGui 경계

| 경로 | 적용 방법 | 수명과 동작 |
| --- | --- | --- |
| UTF-8 표시/ID | `ce::utf8_string`의 `c_str()` 또는 길이 있는 범위를 직접 전달 | 변환하지 않는다. 본문은 포맷 해석을 피하도록 `TextUnformatted`의 범위 인자를 사용한다. ID는 안정적인 UTF-8을 유지한다. |
| UTF-16 표시 | `editor::text::TextUnformatted(value.view(), cache)` 또는 동일한 `ce::utf8_conversion_cache` | 원본별로 재사용되는 캐시를 둔다. 매 호출 원본을 비교하고 변경 시에만 변환하므로 비교 비용과 두 표현의 메모리는 남는다. 변환 실패 시 이전 캐시를 새 값으로 표시하지 않는다. |
| UTF-16 입력 | `editor::text::InputText`, `InputTextWithHint`, `InputTextMultiline`과 `ce::utf16_input_state` | 위젯마다 편집이 끝날 때까지 살아 있는 상태를 둔다. 편집 중 외부 변경은 `conflict`로 알리고, 상태와 변환 실패를 호출자가 처리한다. |
| UTF-8 입력 | UTF-8 버퍼를 직접 편집하는 수명 있는 어댑터 | UTF-16으로 왕복하지 않는다. 버퍼 크기·재할당 콜백·편집 중 외부 변경 정책을 유지한다. 기존 `std::string` 전용 ImGui 어댑터를 `ce::utf8_string`에 그대로 전달하지 않는다. |
| Enter 확정 또는 ImGui 입력 콜백 사용 | 기존 동작을 유지하는 별도 어댑터를 마련한 뒤 전환 | 현재 `Utf16Text.h`의 입력 어댑터는 `EnterReturnsTrue`와 콜백 플래그를 거부한다. 단순 치환하면 기존 동작이 사라진다. |

입력은 embedded NUL을 거부한다. 표시 캐시와 입력 상태는 각각 `Engine/Containers/string/string.h` 및 `Editor/ImGuiHelper/Utf16Text.h`에 구현되어 있다. 고유 객체·행별 상태가 필요한 목록에서 단일 전역 캐시를 교대로 재사용하면 매번 재변환되므로 소유자와 수명을 먼저 정한다.

## 적용 순서와 확인할 경로

1. 각 사용처를 텍스트 인코딩, 소유자, 변이 시점, 소비 API, 직렬화·ABI 경계로 분류한다. 알려지지 않은 바이트는 확인 전까지 보류한다.
2. UTF-8 생산·소비가 이어지는 경로부터 `ce::utf8_string`으로 전환한다. `Editor/EngineGUIWindow/OutputLogView.h`의 로그와 `Engine/Utility_Framework/Authoring/AuthoringParsedDocument.h`의 문서·경로는 외부 타입 경계를 함께 조사할 후보이다.
3. UTF-16 소유가 적절한 사용자 편집 텍스트를 한 기능 단위로 전환하고 ImGui 표시 캐시와 위젯별 입력 상태를 연결한다. `Editor/EngineGUIWindow/EditorWorkspaceStore.*`는 이름, 파일 데이터와 ImGui ID가 섞여 있어 필드별로 구분한다.
4. Win32, 파일시스템, 직렬화, 스크립트 및 외부 라이브러리는 별도 경계 어댑터와 기존 데이터 호환성을 확인한다. `Editor/EngineEntry/Assets/EditorAssetDatabase.cpp`의 wide 경로는 일괄 치환하지 않는다.
5. 각 기능 단위로 빈 값, ASCII, 한글, 보조 평면 문자, 잘못된 UTF-8/서로게이트, embedded NUL 정책, 긴 문자열, 편집 중 외부 변경 및 저장·로드 왕복을 검증한다. 입력 플래그와 기존 확정/취소 동작도 확인한다.
6. 관련 빌드와 실제 UI를 확인하고, 변경 전후 같은 조건에서 프레임 CPU와 할당을 측정한다. 캐시 적중 시 원본 비교 비용을 포함하며, 측정 전 성능 향상을 단정하지 않는다.

## 조사 범위

추적된 `Engine`, `Editor`, `RHI`, `Player`, `HeadlessHost`, `Tools`, `BuildTool`에서 `Tools/build/ModuleAdapters`를 제외한 기계적 검색은 표준 문자열 계열이 포함된 **1,188개 파일, 11,069개 줄**이다. 같은 줄의 여러 사용은 한 줄로 세며 주석을 포함한다. ThirdParty와 테스트는 이 수치에 포함하지 않는다. 전환 대상 수나 남은 작업의 완료율을 뜻하지 않는다.
