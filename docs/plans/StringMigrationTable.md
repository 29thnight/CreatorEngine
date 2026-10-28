# 문자열 전환표

2026-10-28 기준 문자열 계약과 적용 현황. `08ea3773`의 선택적 전환을 이어서 내부 소유권, ImGui 입력, 리플렉션과 저장 경계를 연결했다. 외부 계약과 데이터의 실제 인코딩을 먼저 확인하고, 생산자부터 소비자까지 한 단위로 변경한다. 아래 적용 범위와 검증 범위를 구분한다.

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
| UTF-8 입력 | `InputTextUtf8`, `InputTextWithHintUtf8`, `InputTextMultilineUtf8` | UTF-16으로 왕복하지 않는다. 어댑터가 재할당과 소유 문자열 길이를 동기화한다. `EnterReturnsTrue`의 반환이 false인 편집 프레임에도 길이가 갱신된다. 외부 변경 정책은 소유자가 정한다. |
| Enter 확정 또는 ImGui 입력 콜백 사용 | UTF-8/UTF-16 어댑터의 flags, callback, userData 인자 | `EnterReturnsTrue`와 사용자 콜백을 지원한다. UTF-16 결과의 `changed`와 `submitted`는 별개다. resize 콜백은 어댑터가 소유하며 사용자 `CallbackResize`를 거부한다. 콜백이 필요한 플래그와 callback 누락, multiline에서 지원하지 않는 플래그는 오류로 반환한다. |
| 리플렉션 문자열 편집 | `InputReflectedString` | 객체·멤버·ImGui context별 draft를 유지하고 Enter에서 제출한다. 대상 변경, 외부 값 변경, 위젯이 사라졌다 돌아오는 경우를 구분하여 이전 객체의 편집 값을 재사용하지 않는다. |

입력은 embedded NUL과 잘못된 Unicode를 거부하고 실패 상태를 반환한다. 길이가 있는 저장·직렬화 경로의 embedded NUL 허용 여부와 구분한다. 표시 캐시와 입력 상태는 `Engine/Containers/string/string.h`, `Editor/ImGuiHelper/Utf8Text.h`, `Utf16Text.h`, `ReflectedStringInput.h`에 구현되어 있다. 고유 객체·행별 상태가 필요한 목록에서 단일 전역 캐시를 교대로 재사용하면 매번 재변환되므로 소유자와 수명을 먼저 정한다.

## 연결한 내부 소유권 경로

| 경로 | 소유 타입과 경계 | 회귀 검사 |
| --- | --- | --- |
| 로그 수집 → 저장/그룹 → 검색/OutputLog → HTML | 메시지, logger, source, 그룹 키, 검색어를 `ce::utf8_string`으로 소유한다. spdlog/fmt와 파일 API에서만 길이 있는 명시적 경계를 쓴다. | `log_store_probe.cpp`: 긴 입력, Unicode, embedded NUL, 원본 수명, 동시 Push/Read/Clear, HTML/경로 |
| Authoring 파일 → parsed/cooked 문서 → scalar/write | 원문과 오류·컨텍스트는 UTF-8 소유다. 파일 입력은 바이트로 판별한 뒤 텍스트/바이너리 파서를 선택한다. `ParseFilePath`/`LoadMapPath`로 native path를 보존한다. 기존 std 호출도 어댑터로 지원한다. | `authoring_string_migration_probe.cpp`: parse/write 왕복, alias 입력/오류, CEDO/CEMA, 한글 경로, 실패 시 목적지 보존 |
| 진단 문서 → JSON → 스트림 | key/value/JSON 출력은 `ce::utf8_string`이다. 스트림은 `write(data, size)`를 사용한다. | `diagnostic_string_migration_probe.cpp`: JSON escape, embedded NUL, 수치, 긴 Unicode |
| 워크스페이스 이름/큐 → INI/파일 → 메뉴/명령 | 직렬화·상태·큐·목록은 UTF-8 소유다. 편집용 이름과 삭제 대상의 UTF-16 상태는 유지한다. 파일시스템/Win32는 native path로 연결한다. | `editor_workspace_string_probe.cpp`: codec, 길이·경로·오류, 기존 6개 INI fixture |
| 태그 소유권 → undo → Inspector | 태그 배열·map key는 `ce::utf8_string`이다. Entity/명령의 기존 공개 std 계약은 길이 기반으로 연결한다. | `string_storage_contract_probe.cpp`, `TagManagerStringViewTests.cpp` |
| Inspector 검색/카테고리/클립/새 스크립트 입력 | 영속 상태는 UTF-8 소유, 같은 draw에서만 쓰는 catalog row는 view다. ScriptAuthoring/SoundSystem의 기존 계약은 경계에서 변환한다. | `editor_component_catalog_probe.cpp`: 긴 한글·bounded query 포함 |
| 노드 이름/Animator → 노드 설정 JSON | 이름·설정 snapshot은 UTF-8 소유, 설정 위치는 filesystem path다. 라이브러리의 저장·로드 콜백에 수명이 보장되는 데이터를 전달한다. | `node_editor_string_probe.cpp`: 실제 node editor JSON 왕복, rename, 한글 경로, 두 단계 load |
| ce 문자열 → 리플렉션/JSON/YAML/메서드/Inspector | UTF-8/UTF-16을 문자열로 분류하고 scalar serializer를 제공한다. UTF-16은 strict 변환한다. YAML의 null 표기·제어 문자는 문자열로 quote한다. method any 인자는 원래 ce 타입을 유지한다. | `reflection_string_probe.cpp`, `reflected_string_input_probe.cpp` |
| ce 문자열 키 → engine map/set | UTF-8/UTF-16 소유 타입과 view의 기본 hash/equality/order 정책을 연결한다. view lookup은 키를 복사하지 않는다. | `string_storage_contract_probe.cpp`: 긴 키 lookup/중복 삽입의 ce allocator 호출 수 포함 |

## 재현 가능한 검증

`Tests/Containers/CMakeLists.txt`에 Unicode와 문자열 회귀 검사를 등록한다. `prepare-string-contract-deps.py`는 저장소의 의존성 버전과 같은 ImGui, imgui-node-editor, fmt, spdlog, rapidyaml, reflgen 소스를 고정 commit으로 준비하고 node editor의 vcpkg 패치 체크섬을 검증한다.

```sh
python Tools/regression/prepare-string-contract-deps.py --directory Build/StringDependencies
cmake -S Tests/Containers -B Build/StringContracts -DCMAKE_BUILD_TYPE=Debug \
  -DCE_STRING_IMGUI_SOURCE_DIR="$PWD/Build/StringDependencies/imgui" \
  -DCE_STRING_NODE_EDITOR_SOURCE_DIR="$PWD/Build/StringDependencies/imgui-node-editor" \
  -DCE_STRING_SPDLOG_INCLUDE_DIR="$PWD/Build/StringDependencies/spdlog/include" \
  -DCE_STRING_FMT_INCLUDE_DIR="$PWD/Build/StringDependencies/fmt/include" \
  -DCE_STRING_RYML_SOURCE_DIR="$PWD/Build/StringDependencies/rapidyaml" \
  -DCE_STRING_REFLGEN_INCLUDE_DIR="$PWD/Build/StringDependencies/reflgen/include"
cmake --build Build/StringContracts --config Debug --parallel 4
ctest --test-dir Build/StringContracts -C Debug --output-on-failure --parallel 4
```

`.github/workflows/string-contracts.yml`은 Windows/Linux × Debug/Release로 같은 계약을 검사한다. 이 타깃은 실제 ImGui·node editor·ryml·reflgen·spdlog/fmt를 연결하지만 렌더러 없이 실행한다. 전체 Windows Engine/Editor 모듈 빌드, 실제 IME 입력·UI 조작, 렌더링 성능 측정을 대신하지 않는다. Unix의 `CE_ASAN=ON`은 AddressSanitizer와 UndefinedBehaviorSanitizer를 활성화한다.

2026-10-28 로컬 Linux/GCC 13.3 검증에서는 Debug 25개와 Release 25개가 통과했다. 의존성까지 계측한 ASan/UBSan에서는 Unicode·문자열 11개가 통과했다. 이 환경의 `/proc` 제약으로 LeakSanitizer는 비활성화(`ASAN_OPTIONS=detect_leaks=0`)했으므로 누수 검사 통과를 의미하지 않는다. Unicode 검사는 유효 scalar 1,112,064개와 변이 입력 20,000개를 포함한다.

기존 `scripts/check_include_boundary.py`는 같은 checkout 범위의 기준 커밋에서 상향 29건·물리 경계 112건으로 실패한다. 최종 변경에서는 기존 항목이 그대로이고 `Utf8Text.h`의 외부 `imgui_internal.h` include 진단 1건이 더해져 30건·112건이다. 스캐너가 basename만으로 `Player/ForbiddenIncludes/imgui_internal.h`의 차단용 동명 헤더를 소유자로 오인하는 경우이며, 실제 컴파일 의존성에는 고정한 외부 ImGui 헤더가 기록된다. 이 include는 렌더링을 건너뛴 입력이 이전 위젯의 edited 상태를 읽지 않도록 `SkipItems`를 확인하는 데 필요하다. allowlist나 검사 스크립트는 변경하지 않았다.

## 적용 순서와 확인할 경로

1. 각 사용처를 텍스트 인코딩, 소유자, 변이 시점, 소비 API, 직렬화·ABI 경계로 분류한다. 알려지지 않은 바이트는 확인 전까지 보류한다.
2. UTF-8 생산·소비가 이어지는 경로부터 `ce::utf8_string`으로 전환한다. 위 표의 로그와 Authoring 경로처럼 외부 타입 경계와 소비자를 함께 변경한다.
3. UTF-16 소유가 적절한 사용자 편집 텍스트를 한 기능 단위로 전환하고 ImGui 표시 캐시와 위젯별 입력 상태를 연결한다. Workspace처럼 이름, 파일 데이터와 ImGui ID가 섞인 기능은 필드별로 구분한다.
4. Win32, 파일시스템, 직렬화, 스크립트 및 외부 라이브러리는 별도 경계 어댑터와 기존 데이터 호환성을 확인한다. `Editor/EngineEntry/Assets/EditorAssetDatabase.cpp`의 wide 경로는 일괄 치환하지 않는다.
5. 각 기능 단위로 빈 값, ASCII, 한글, 보조 평면 문자, 잘못된 UTF-8/서로게이트, embedded NUL 정책, 긴 문자열, 편집 중 외부 변경 및 저장·로드 왕복을 검증한다. 입력 플래그와 기존 확정/취소 동작도 확인한다.
6. 관련 빌드와 실제 UI를 확인하고, 변경 전후 같은 조건에서 프레임 CPU와 할당을 측정한다. 캐시 적중 시 원본 비교 비용을 포함하며, 측정 전 성능 향상을 단정하지 않는다.

## 조사 범위

기준 커밋 `08ea3773`의 추적된 `Engine`, `Editor`, `RHI`, `Player`, `HeadlessHost`, `Tools`, `BuildTool`에서 `Tools/build/ModuleAdapters`를 제외한 기계적 검색은 표준 문자열 계열이 포함된 **1,188개 파일, 11,069개 줄**이었다. 같은 줄의 여러 사용은 한 줄로 세며 주석을 포함한다. ThirdParty와 테스트는 이 수치에 포함하지 않는다. 전환 대상 수나 남은 작업의 완료율을 뜻하지 않는다.

표준 문자열은 OS·파일시스템, 외부 라이브러리, 공개 직렬화/스크립트 계약과 아직 분류하지 않은 기능에도 남아 있다. 위 표의 적용 완료를 저장소 전체의 표준 문자열 제거로 해석하지 않는다. 특히 `Object` 이름, `Entity` 태그 같은 기존 공개 타입을 바꿀 때는 현재 연결한 내부 저장소 어댑터와 별도로 소비자·직렬화·스크립트 호환성을 검증해야 한다.
