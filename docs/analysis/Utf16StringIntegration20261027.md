# UTF-16 기본 문자열과 ImGui 입력 통합

> 2026-10-27 후속 최적화에서 기존 수용 기준을 유지해 **57/57**을 통과했다. UTF-8 2KB 추가 두 건도 해소했다. [원인 분리·수정·최종 재수용](Utf8AppendOptimization20261027.html). 아래의 55/57은 초기 통합 당시 기록이다.

구현·기능·실제 IME·Editor Debug 실행 검증은 통과했다. 초기 통합 성능은 **55/57**이며 UTF-16 기본 문자열 조건은 **18/18**이다. UTF-8 2KB 재사용 추가 두 건이 미달이므로 전체 성능 수용 완료로 판정하지 않는다.

`Engine/Containers/modules/string.ixx`가 구현 정본이다. `ce::string`과 `ce::string_view`의 코드 단위는 `char16_t`이며, `basic_string<Allocator>`도 UTF-16이다. 기존 바이트 문자열은 `utf8_string`, `utf8_string_view`, `basic_utf8_string<Allocator>`로 명시했다. 생성 헤더는 동기화 도구의 산출물이며 외부 테스트 솔루션은 이 엔진 내부 구현을 직접 빌드한다.

## 인코딩과 소유권 계약

- 문자열 길이·인덱스·부분 문자열은 코드 단위 기준이다. UTF-16 코드 단위 2개가 하나의 보충 평면 문자를 나타낼 수 있다. 일반 배열식 변경은 코드 단위 연산이며 글자소 편집 API가 아니다.
- UTF-8 표현은 ImGui의 `char*` API와 연결하기 위해 `char` 바이트를 사용한다. 로캘·ACP 변환을 사용하지 않는다. `wchar_t`의 플랫폼별 크기를 UTF-16 타입의 전제로 삼지 않는다.
- `to_utf8`과 `to_utf16`은 전체 입력을 검증한 뒤 결과를 변경한다. 잘못된 서로게이트, 잘린 시퀀스, 잘못된 연속 바이트, 과잉 인코딩, 서로게이트 스칼라, U+10FFFF 초과를 거부하고 첫 오류의 코드 단위/바이트 위치를 반환한다. 잘못된 입력이나 할당 실패는 목적 문자열의 기존 내용을 보존한다.
- 입력·목적 저장소가 겹치면 임시 소유 문자열에서 변환 후 교환한다. 일반 변환은 내장 NUL을 보존한다. ImGui 편집 API는 C 문자열 경계 때문에 내장 NUL을 명시적으로 거부한다.
- 캐시는 UTF-16 원본 스냅샷과 UTF-8 결과를 모두 소유한다. 호출자 버퍼의 주소나 수명을 캐시 키로 신뢰하지 않는다. 같은 내용의 적중과 확보한 용량 안의 변경은 할당하지 않는다. 내용 비교 비용은 문자열 길이에 비례한다.
- 캐시·입력 상태는 위젯별로 유지하며 ImGui 스레드에서 사용한다. 동시 접근용 컨테이너가 아니다. 확보 용량은 재사용하고 객체 소멸 때 반환한다.

## ImGui API와 실제 사용처

`Editor/ImGuiHelper/Utf16Text.h`에 `editor::text::TextUnformatted`, `InputText`, `InputTextWithHint`, `InputTextMultiline`을 추가했다. 모듈 또는 `ContainerAPI.h`로 컨테이너 API를 먼저 공급한다.

```cpp
ce::string value(u"한글 \U0001F680");
ce::utf8_conversion_cache display;
ce::utf16_input_state editing;
// display와 editing은 매 프레임 생성하지 않고 위젯 수명 동안 보관한다.
editor::text::TextUnformatted(value.view(), display);
auto result = editor::text::InputText("##name", value, editing);
```

레이블과 힌트는 ImGui의 UTF-8 식별자/표시 규약을 유지한다. UTF-16 값은 소유된 UTF-8 편집 버퍼로 변환한다. resize 콜백은 버퍼 성장과 주소 갱신을 담당하고, 편집 결과는 검증 후 UTF-16 원본으로 확정한다. 편집 중 외부 값이 바뀌면 `conflict`를 보고하고 외부 값을 덮어쓰지 않는다. 포커스를 잃은 뒤 외부 값으로 재동기화하며, 외부 값이 기존 확정값으로 돌아오면 충돌을 해제한다.

콜백 플래그와 `EnterReturnsTrue`는 반환값 의미가 다른 별도 확장 API가 필요하므로 현재 명시적으로 거부한다. Release에서도 오류를 반환한다. 작업공간 이름 대화 상자를 이 UTF-16 입력 경로로 전환했다. 파일명 검증·저장은 UTF-8 경계에서 기존 로직을 계속 사용한다. 기존 파일 형식을 인코딩 변경으로 재해석하지 않는다. 식별자 atom과 기존 바이트 키 맵도 UTF-8 저장을 유지한다.

## 검증 범위

전체 엔진 STL 사용처의 전환 완료 또는 모든 PC OS의 동등 성능을 뜻하지 않는다. 잠금 없는 큐 구현은 이번 문자열 변경으로 수정하지 않았다.

| 검사 | 최종 구현 결과 |
| --- | --- |
| MSVC named module 일반 검사 | 15/15 통과 |
| MSVC AddressSanitizer | 15/15 통과 |
| Linux Clang 18, ThreadSanitizer 포함 | 25 partition + primary, 12개 실행 통과 |
| 공개 헤더 독립 컴파일 | 전체 통과 |
| Windows SDK 헤더 뒤 컨테이너 포함 | UTF-16·UTF-8 변환 포함 통과 |
| 유니코드 스칼라 왕복 | 1,112,064개 전체 통과 |
| UTF-16 변경 연산과 STL 차등 검사 | 20,000회 통과 |
| 무효 인코딩·할당 실패·겹치는 변환/추가 | 기존 결과 보존과 경계 검사 통과 |
| 캐시 적중 10,000회·같은 용량의 변경 | 계수 allocator의 추가 할당 0회 |
| ImGui 입력 이벤트 | 한글·이모지 쌍, 스칼라 단위 Backspace, 4KB 성장, 힌트·여러 줄 통과 |
| 실제 Windows 한글 IME | 조합·확정 메시지와 최종 UTF-16 값 확인 |
| Editor Debug 엔진 빌드·실행 | 17명령 통과, GPU 완료·Play/Stop 복원·정상 종료 |

UTF-16 키의 노드 맵을 `string_view`로 조회하는 검사도 추가했다. `utf16_string_hash`와 `equal_to<basic_utf16_string<A>>`가 뷰를 직접 받아 소유 키를 생성하지 않는다. `hash<ce::string>`과 `hash<ce::string_view>`도 같은 해시 규약을 사용한다.

## 실제 한글 IME

단순한 `AddInputCharacterUTF16` 호출을 실제 IME 통과로 간주하지 않았다. 엔진이 사용하는 ImGui 1.92.8 라이브러리, Win32 backend와 같은 입력 어댑터를 연결한 실제 D3D11 창에서 키를 입력했다. 해당 창의 한국어 IME 컨텍스트를 열고 `g k s r m f Return`으로 `한글`을 조합·확정했다. Windows 전역 설정이나 설치 언어 팩을 변경하지 않았다.

관측한 조합은 `314E → D558 → D55C`, `3131 → ADF8 → AE00`이고 각 음절의 `GCS_RESULTSTR`을 확인했다. 최종 소유 문자열은 `D55C AE00`, 길이 2 코드 단위였다. 조합 중 미확정 문자열은 OS/ImGui가 유지하고, UTF-16 원본에는 확정된 입력을 반영한다. 최종 바이너리로 다시 수행한 `20261027-unicode-ime-locked.log`와 바이너리·소스 해시를 보존했다.

이 결과는 어댑터의 실제 Windows IME 경로 검증이다. Editor 작업공간 대화 상자를 UI로 열어 같은 입력을 반복한 결과로 확대하지 않는다. Editor 자체에서는 모듈 문자열·변환·입력 상태를 명령 프로브로 실행하고 작업공간 입력창 배선을 빌드했다. 기존 엔진의 한국어/보충 평면 폰트 fallback 설정도 유지한다.

## 성능 비교 방법과 원인 분리

C++23 Release 소비자가 `import Creator.Containers;`로 정본 모듈을 사용한다. MSVC 19.50.35730.0의 `std::u16string` 및 `std::string`과 각각 같은 코드 단위·내용을 비교한다. 엔진 기능 빌드는 기존 도구 체인 요구에 따라 MSVC 19.51을 사용한다.

조건은 UTF-16·UTF-8 각각 5/64/2,048 코드 단위의 생성·재사용 추가 12개, ASCII+이모지 및 한글+이모지의 양방향 변환 4개, 캐시 적중/변경 2개, 입력 확정 1개로 총 19개다. 변환은 같은 엄격한 변환기로 출력 저장소만 ce/STL로 바꿔 비교한다. STL 자체 변환 알고리즘 대비 성능이라는 뜻이 아니다. 캐시도 STL 측에 같은 소유 스냅샷·내용 비교·변환 재사용을 적용한다. 입력 수치는 확정 처리이며 ImGui 레이아웃·GPU 렌더링 시간 비교가 아니다.

기존과 같이 각 측면 1초 워밍업, 100ms 독립 배치 15개, 측면별 자기 프로세스 heap 최적화, CPU4 고정과 한시적인 CPU5 helper를 사용한다. 세 독립 프로세스 중 두 번째는 측면 순서를 바꾼다. 모든 원시 샘플을 보존하고 **각 프로세스·각 조건**에 시간 중앙값 비율 ≤ 1.10, 양쪽 편차 x[13]/x[1] ≤ 1.30을 요구한다. 빌드·기능 검사를 종료하고 소스·바이너리 해시를 고정해 수집한다.

최초 구현은 34/57이었다. UTF-16 생성에서 임시 객체를 거쳐 할당·이동하는 경로를 제거한 진단 변형은 약 17~29% 개선됐다. 짧은 고정 폭 복사는 단순 직접 생성 대비 추가 이득을 입증하지 못해 채택하지 않았다. 추가 경로는 용량이 충분할 때 성장·별칭 검사를 건너뛰고, 성장 처리를 별도 함수로 분리했다. 겹치는 데이터의 복사는 계속 `memmove`로 보장한다.

분리 실험에서 성장 경로의 분리는 강제 인라인보다 유리했다. 적합 용량 경로만 바꾼 대조군에 비해 5/64 코드 단위 추가에서 약 19~25% 개선됐다. 이후 정본 재측정은 53/57이었으며, UTF-8 생성의 인라인 결정과 한 번의 4KB 추가 미달이 남았다. UTF-8 초기화 인라인을 보완했다. 순수 `memmove`와 네 주소 조합의 대조군에서는 차이가 약 4% 이내였으므로, 이전 18% 미달의 원인을 주소만으로 확정하지 않았다. 마지막 판정은 이 진단 수치가 아니라 아래 정본 바이너리 결과를 사용한다.

최초 수집 스크립트에는 helper 래퍼의 태그 변수 가림으로 CSV 이름에 `-guard`가 붙는 집계 오류가 있었다. 세 원시 CSV를 그대로 보존해 별도 회수 집계했으며, 수집 조건과 바이너리는 바뀌지 않았다. 후속 실행에서는 변수 이름을 고쳐 자동 판정을 수행했다.

## 최종 성능 판정

초기 통합 정본 `20261027-unicode-final1`은 **55/57**이다. 수집 전후 입력 66개 해시가 일치했다. 모든 조건의 편차 기준은 통과했고, 시간 비율에서 두 건이 미달이었다. 최대 custom 편차는 1.068938, STL 편차는 1.050104다.

| 구분 | 조건 × 프로세스 | 통과 |
| --- | ---: | ---: |
| UTF-16 생성·재사용 추가 | 6 × 3 | 18/18 |
| UTF-8 생성·재사용 추가 | 6 × 3 | 16/18 |
| 같은 변환기의 ce/STL 출력 저장소 | 4 × 3 | 12/12 |
| 같은 소유권 규약의 캐시 | 2 × 3 | 6/6 |
| 입력 확정 | 1 × 3 | 3/3 |

미달은 다음과 같다. 원시 샘플을 빼거나 허용치를 바꾸지 않았다.

| 프로세스 | 조건 | ce ns/회 | STL ns/회 | 비율 |
| --- | --- | ---: | ---: | ---: |
| 1 | utf8_reuse_append_2048 | 24.483 | 21.864 | 1.119759 |
| 2 | utf8_reuse_append_2048 | 36.821 | 22.917 | 1.606680 |

아래 중앙값으로 프로세스를 합쳐 통과시키지 않고, 각 프로세스를 별도로 판정했다.

| 조건 | 비율 중앙값 | 최소 | 최대 |
| --- | ---: | ---: | ---: |
| cache_change_256 | 0.9928 | 0.9916 | 0.9942 |
| cache_hit_256 | 1.0495 | 1.0483 | 1.0519 |
| decode_ascii_emoji | 1.0043 | 0.9999 | 1.0107 |
| decode_korean_emoji | 1.0404 | 1.0399 | 1.0404 |
| encode_ascii_emoji | 0.8802 | 0.8780 | 0.8803 |
| encode_korean_emoji | 1.0175 | 1.0172 | 1.0181 |
| input_commit_256 | 0.9596 | 0.9594 | 0.9619 |
| utf16_construct_2048 | 0.9810 | 0.9765 | 0.9812 |
| utf16_construct_5 | 0.8702 | 0.8669 | 0.8719 |
| utf16_construct_64 | 0.9645 | 0.9540 | 0.9743 |
| utf16_reuse_append_2048 | 1.0169 | 0.9951 | 1.0190 |
| utf16_reuse_append_5 | 0.9246 | 0.9160 | 0.9270 |
| utf16_reuse_append_64 | 1.0296 | 1.0240 | 1.0298 |
| utf8_construct_2048 | 1.0904 | 1.0846 | 1.0991 |
| utf8_construct_5 | 0.9034 | 0.8976 | 0.9036 |
| utf8_construct_64 | 1.0250 | 1.0021 | 1.0328 |
| utf8_reuse_append_2048 | 1.1198 | 1.0105 | 1.6067 |
| utf8_reuse_append_5 | 0.9375 | 0.9353 | 0.9382 |
| utf8_reuse_append_64 | 0.9324 | 0.9295 | 0.9337 |

SIMD 대조군은 채택하지 않았다. 4KB UTF-16 추가에서 STL의 약 1.69배였고, 2KB UTF-8 추가도 약 1.60배였다. 이 복사본은 외부 진단 디렉토리에만 남기며 엔진 구현에 포함하지 않는다.

동일한 `std::string`끼리 버퍼만 달리한 네 쌍에서도 빠른 쪽 대비 느린 쪽이 약 19~29% 차이를 보였다. 같은 버퍼 쌍에 컨테이너 없이 `memmove`만 수행해도 차이가 관측됐다. 실제 객체 코드에서 ce와 STL의 해당 경로는 모두 CRT `memmove`를 호출한다. 버퍼 배치 영향을 구현 자체의 비용으로 곧바로 해석할 수 없지만, 이 대조군만으로 앞선 61% 미달 전체의 원인을 확정하지 않는다. 단일 버퍼 조건을 여러 버퍼 평균으로 바꿔 수용을 선언하지 않았으며, 이 항목의 원인 분리·성능 수용은 남겨 둔다.

## 증거와 재현

외부 `CreatorContainers`는 엔진 내부 정본을 참조한다. 진단용 복사본은 `CE_UNICODE_ABLATION=ON`에서만 빌드하며 기본값은 OFF다. 일반 모듈 검사와 성능 소비자는 정본을 import한다.

외부 `CreatorContainers/results`의 주요 파일:

- `20261027-unicode-final1-{1,2,3}.csv`, 같은 이름의 `.stderr`: 모든 원시 샘플과 컴파일러·일부 버퍼 배치 정보.
- `20261027-unicode-final1-verdict.json`, `-inputs.json`, `-snapshot/`, `-guard-guard.json`: 최종 판정, 수집 당시 입력·바이너리, helper 수명 영수증.
- `20261027-unicode-diag1-guard-{1,2,3}.csv`, `-recovered-summary.json`: 최초 34/57과 집계 오류 회수 자료.
- `20261027-unicode-ablation1.csv`, `-inlining1.csv`, `-ctor1.csv`: 생성·적합 용량 경로·인라인 결정의 분리 측정.
- `20261027-unicode-opt1-verdict.json`: 중간 정본 53/57.
- `20261027-unicode-addresses1.csv`, `-bulk1.csv`, `-address-self1.csv`: 주소·복사 및 채택하지 않은 SIMD 대조군.
- `unicode-final1-disassembly.txt`: 실제 소비자 객체의 호출 경로.
- `unicode-locked-normal-driver.log`, `unicode-locked-asan-driver.log`, `unicode-locked-linux-driver.log`: 최종 기능 검사.
- `unicode-locked-build.log`: 성능/ImGui 소비자와 Windows·독립 헤더 컴파일.
- `20261027-unicode-ime-locked.log`, `-hashes.json`: 최종 바이너리의 실제 Windows IME 조합·확정.

엔진 빌드는 `Build/utf16-editor-locked-debug.log`, 마지막 입력창 변경의 증분 빌드는 `Build/utf16-editor-final-incremental.log`다. 실행 증거는 `Build/Verification/ContainerMigration/Utf16Debug1/acceptance.json`과 `results.jsonl`이며 두 프로브의 `utf16Strings=true`를 요구한다. 수집 이후에는 외부 진단 타깃 등록만 추가했고 정본 구현·성능 소비자·바이너리는 바꾸지 않았다.

Windows/MSVC 문자열의 위 범위만 측정했다. Linux는 기능·ThreadSanitizer 검사이며 성능 수용이 아니다. macOS 성능, 최신 전체 82조건 및 PPL 큐 재수용, 기존 엔진 STL 문자열 전체 전환 완료를 이 결과로 대신하지 않는다.
