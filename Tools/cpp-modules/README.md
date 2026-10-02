# C++ 모듈 전환 — 1단계 검사와 빌드 시간 측정

브랜치 `cpp_module_ixx` 의 실험 도구다. 솔루션 빌드에는 아무것도 더하지 않는다 —
`.ixx` 두 개는 아직 어느 vcxproj 에도 들어 있지 않고, 여기 스크립트만 그것을
컴파일한다. **판정은 Windows 로컬(VS 18 · v145 · PowerShell 7)에서만 한다.**

## 무엇을 확인하는가

1단계의 방식은 "헤더 감싸기"다. 정의를 모듈로 옮기지 않고, 헤더를 global module
fragment 에서 연 뒤 이름만 `export using` 으로 내보낸다. 그래서 헤더로 본 번역
단위와 `import` 로 본 번역 단위가 **같은 엔터티**를 가리킨다. 헤더 소비자(ProfileScope.h
만 44 곳)와 모듈 소비자가 한동안 한 프로그램에 함께 살아야 하기 때문이다.

| 모듈 | 파일 | 감싸는 헤더 |
|---|---|---|
| `ce.diagnostics` | `Engine/EngineDiagnostics/ce.diagnostics.ixx` | Profile*.h 아홉 개 전부(표면을 바꾸지 않는다) |
| `ce.core` | `Engine/Utility_Framework/ce.core.ixx` | `Uuid.h` · `ClassProperty.h` — STL 만 쓰는 공용층 |

`TypeTrait.h` 는 `ce.core` 에 넣지 않았다. 매크로(`type_guid`)·Win32(`combaseapi.h`)·
헤더 안의 내부 연결 전역(`static std::set<HashedGuid> g_guids`)이 모듈 경계를 넘지
못한다. 그 경계는 관찰 항목으로 따로 잰다.

## 1. 모듈 검사 — `Invoke-CppModuleProbe.ps1`

```powershell
pwsh -NoProfile -File .\Tools\cpp-modules\Invoke-CppModuleProbe.ps1
pwsh -NoProfile -File .\Tools\cpp-modules\Invoke-CppModuleProbe.ps1 -Configuration Debug -Variant Development -BenchRepeat 0
```

엔진을 띄우지 않고 `cl` 로 직접 컴파일한다(`verify-profile-core.ps1` 과 같은 수법).
vcvars 는 vswhere 로 찾고(`-VcVars` 로 지정 가능) 한 번만 부른다. 도구 집합이
`-MinimumToolsVersion`(기본 14.50 = v145)보다 낮으면 바로 붉다 — 다른 도구 집합의
결과를 v145 의 판정으로 읽지 않게 한다. `/std` 는 엔진과 같은 `c++23preview` 다.

판정은 세 갈래다. 종료 코드는 Gate 와 ExpectFail 만 본다.

| 갈래 | 뜻 |
|---|---|
| **Gate** | 1단계가 성립하려면 초록이어야 한다. 건너뛴 Gate 도 붉음이다(앞 단계가 붉어 못 잰 것을 통과로 읽지 않는다). |
| **ExpectFail** | **실패해야** 초록이다. 그것도 기대한 문안으로 — 모듈을 못 찾는 등 엉뚱한 이유로 실패한 것은 붉다. |
| **Observe** | 종료 코드에 넣지 않는다. 컴파일러가 실제로 어떻게 하는지를 기록해 다음 단계의 설계가 그 값을 보고 정한다. |

### 항목

| 항목 | 갈래 | 붉으면 |
|---|---|---|
| `toolchain/std-flag` | Gate | `/std:c++23preview` 이 C++23 으로 먹지 않는다. 뒤의 모든 결과가 무의미하다 |
| `std/bmi` · `std/import` | Gate | `import std;` 를 엔진 설정으로 쓸 수 없다 |
| `std/std_include_then_import` · `std/std_import_then_include` | Observe | 붉은 순서가 있으면, 엔진 헤더가 STL 을 #include 로 여는 동안 그 순서의 혼용을 피해야 한다 |
| `core/bmi` · `core/identity` | Gate | Uuid 값(RFC 4122 v5 공표 벡터 · 거절 집합)이 import 경로에서 다르거나, `Singleton<T>::s_instance` 가 헤더 쪽과 모듈 쪽에서 둘이 된다 |
| `typetrait/bmi` · `typetrait/use` | Observe | TypeTrait.h 를 모듈 뒤로 숨길 수 없다(예상된 결과일 수 있다 — 위 세 가지 이유) |
| `typetrait/macro-boundary` | ExpectFail | `type_guid` 가 import 로 **보인다면** 붉다. 모듈이 매크로를 막는다는 전제가 깨진 것이다 |
| `typetrait/module-owned-name` | Observe | 모듈에 붙은 타입의 `TypeTrait::type_name<T>()` 가 장식된다 — 2단계에서 타입을 모듈로 옮기면 `MakeTypeID` 가 바뀐다. 실제 문자열이 `INFO` 로 남는다 |
| `diag/sources` · `diag/bmi` | Gate | 코어 일곱 개 또는 모듈 인터페이스가 `/W4 /WX` 계약 아래서 서지 않는다 |
| `diag/identity` | Gate | 아래 "동일성" 중 하나가 깨졌다. 링크가 실패하면 두 쪽이 다른 엔터티를 본다는 뜻이다 |
| `diag/shipping-mismatch(...)` | ExpectFail | 다른 구성으로 만든 BMI 를 소비자가 조용히 받는다 |
| `diag/diagnostics_mixed_*` | Observe | 한 번역 단위에 헤더와 import 를 섞을 수 없다 — 전환 단위가 ".cpp 하나" 가 아니라 "그 .cpp 가 전이로 여는 공개 헤더까지" 로 커진다 |
| `diag/std_with_diagnostics` | Observe | `import std;` 와 STL 을 #include 로 품은 사용자 모듈을 함께 쓸 수 없다 |

`diag/identity` 가 한 실행 파일 안에서 대조하는 것:

- `typeid(ce::profile_event)` · `typeid(ce::profiler_service)` 가 같다
- `&ce::profiler()` 가 같다
- `ce::marker<"...">()` 의 id 가 같다(Shipping 에서는 양쪽 다 `invalid_marker`)
- **`&ce::current_cpu_context` 가 같고, 모듈 쪽에서 연 `profile_context_scope` 를 헤더 쪽이 본다** —
  정의를 모듈로 옮긴 래퍼였다면 여기서 thread_local 이 둘로 갈린다
- 모듈 쪽 스코프 안에서 연 헤더 쪽 스코프가 깊이 1 로 찍힌다(스레드 스트림이 하나다)
- 캡처 파일 인코딩·디코딩 왕복(`std::expected` · `std::byte` 범위가 경계를 넘는다)
- Shipping: 스코프가 빈 껍데기이고 이벤트가 0 이다

### 구성(Development / Shipping)

`ce.diagnostics` 의 BMI 는 `CE_SHIPPING` 값을 굳혀 담으므로 구성마다 다른 디렉터리에
만든다. 사용자 매크로는 BMI 호환성 검사 대상이 아니어서, 어긋난 BMI 를 import 해도
컴파일러는 말하지 않는다. 그래서 BMI 가 `ce::diagnostics_build::shipping` 을 들고
다니고 소비자가 `probes/diagnostics_build_guard.h` 의 static_assert 로 대조한다.
`diag/shipping-mismatch` 가 그 방어에 이빨이 있는지 본다.

### 벤치

`-BenchRepeat`(기본 5) 회 include 판과 import 판을 **번갈아** 컴파일해 소비자 번역 단위
하나의 시간 중앙값을 낸다. 두 판은 같은 본문(`probes/bench/*_body.inl`)을 컴파일한다.

- `std` — 엔진 공용 헤더 사슬이 실어 나르는 STL 묶음(약 50 개) vs `import std;`
- `diagnostics` — Profile*.h 네 개 vs `import ce.diagnostics;`

BMI 를 만드는 1회 비용은 따로 출력한다. 이 수치는 **컴파일러 프런트엔드의 상한**이다.
엔진은 x64 에서 유니티 빌드(블롭당 10~15 파일)로 이미 헤더 파싱을 나눠 내고 있으므로,
솔루션 빌드의 실제 이득은 아래 2 의 측정으로만 판정한다.

산출물은 `Build/Obj/CppModules/`(추적 밖)에 생긴다: `results.tsv` · `bench.tsv` · 단계별 `logs/`.

## 2. 빌드 시간 측정 — `Measure-BuildTime.ps1`

기준선 워크트리와 모듈 워크트리를 **같은 기계에서 차례로** 재고, 한 TSV 에 쌓아
레이블로 견준다. 기준선 워크트리에는 이 스크립트가 없으므로 모듈 워크트리의 스크립트로
`-RepoRoot` 를 가리킨다.

```powershell
git fetch origin
git worktree add --detach ..\CreatorEngine-baseline origin/master
git worktree add ..\CreatorEngine-modules cpp_module_ixx

cd ..\CreatorEngine-modules
pwsh -NoProfile -File .\Tools\cpp-modules\Measure-BuildTime.ps1 -Label baseline -RepoRoot ..\CreatorEngine-baseline
pwsh -NoProfile -File .\Tools\cpp-modules\Measure-BuildTime.ps1 -Label modules
```

| 시나리오 | 재는 것 |
|---|---|
| `clean` | `/t:Clean` 뒤의 `/t:Build`(Clean 시간은 뺀다). 기본 1 회 |
| `noop` | 아무것도 안 바꾼 재빌드 — MSBuild 최신 판정 비용. 모듈의 의존성 스캔이 여기에 붙는다 |
| `touch-cpp` | `.cpp` 하나의 수정 시각만 바꾼다(기본 `RenderEngine/Texture.cpp`). 유니티 블롭 하나가 다시 돈다 |
| `touch-header` | 헤더 하나의 수정 시각만 바꾼다(기본 `Uuid.h` · `ProfileScope.h`) |

- 결과: `Build/Timing/build-time.tsv`(이 스크립트가 든 워크트리 쪽). 요약은 같은 구성·대상·CPU 의 **성공했고 다른 빌드가 떠 있지 않던** 회차의 중앙값이다.
- 첫 빌드는 재지 않는다. 준비 빌드가 vcpkg 매니페스트 설치(워크트리마다 `vcpkg_installed\` 약 1.5GB — 바이너리 캐시가 차 있으면 약 26 초, 비어 있으면 약 38 분)와 reflgen 의 "Build again" 을 흡수한다.
- 두 워크트리를 **동시에** 빌드하지 않는다. 측정 직전에 `cl`/`link`/`MSBuild` 가 떠 있으면 경고하고 그 회차를 `quiet=False` 로 적어 요약에서 뺀다.
- `/nr:false` 로 노드 재사용을 끈다. 다른 워크트리의 MSBuild 노드를 물려받지 않게 한다.
- `-BinaryLog` 로 회차마다 `.binlog` 를 남길 수 있다.

★ **1단계에서는 두 레이블의 솔루션 빌드가 같아야 한다.** `.ixx` 가 아직 vcxproj 에 없으므로
같은 것을 두 번 재는 셈이다(A/A 측정). 이 차이가 이 기계의 측정 잡음이다 — 다음 단계에서
모듈을 프로젝트에 넣었을 때 그보다 작은 차이는 개선으로 읽지 않는다.

## 다루지 않는 것(다음 단계)

- MSBuild 통합: `.ixx` 를 vcxproj 에 넣고 프로젝트 참조로 BMI 를 넘기는 경로. SceneRuntime 은
  EngineDiagnostics 를 프로젝트 참조 없이 include 경로로만 쓰므로 그대로는 import 할 수 없다.
- 유니티 빌드와 모듈 단위의 관계(`.ixx` 의 유니티 제외 여부).
- reflgen 이 모든 번역 단위에 강제 include(`/FI`)하는 주입 헤더가 모듈 단위에 들어갈 때의 동작.
  EngineDiagnostics 는 reflgen 대상에서 빠져 있어 1단계에서 이 위험이 드러나지 않는다.
- STL header unit 과 "Translate Includes to Imports".
- Editor 에서 실제로 가져와 링크하고 프로파일링 · Shipping 격리를 확인하는 일.
