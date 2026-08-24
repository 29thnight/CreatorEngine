# fastgltf

glTF 2.0 임포터. `Engine/RenderEngine/Experiment/Import/GltfImporter`가 연다.

## 판본과 출처

```
fastgltf   v0.9.0    https://github.com/spnda/fastgltf        MIT
simdjson   3.12.3    singleheader (deps/simdjson/)            Apache-2.0 OR MIT
```

두 판본 모두 임의로 고른 것이 아니다. fastgltf는 vcpkg 포트파일이 SHA512로
검증하는 태그(`v0.9.0`)와 같고, simdjson은 **fastgltf 자신의
`cmake/dependencies.cmake`가 겨냥하는 판본**(`SIMDJSON_TARGET_VERSION "3.12.3"`)
이다. 그 스크립트가 받아오는 것과 같은 URL의 같은 두 파일이다:

```
https://raw.githubusercontent.com/simdjson/simdjson/v3.12.3/singleheader/simdjson.{h,cpp}
```

fastgltf는 `SIMDJSON_TARGET_VERSION`이 정의돼 있으면 `SIMDJSON_VERSION`과 같은지
static_assert로 검사한다. 여기서는 그 매크로를 정의하지 않으므로 검사가 돌지
않지만, 어차피 상류가 짝지어 시험한 조합을 그대로 쓴다.

## 왜 vcpkg가 아니라 여기인가

fastgltf는 vcpkg에 있다(`fastgltf` 0.9.0). 그러므로 이 폴더는 최상위
[../README.md](../README.md)가 세운 정책("vcpkg가 관리하는 것은 여기 오지
않는다")의 **의도적 예외**다. 근거는 하나다:

> 임포터 의존을 한곳에 모은다. 짝이 되는 FBX 임포터가 쓸 **ufbx는 vcpkg에
> 없어** 어차피 벤더링해야 한다. 둘을 서로 다른 경로로 관리하면 임포트 계층
> 하나를 두 가지 방식으로 갱신·고정하게 된다.

부수 효과로 판본이 저장소에 완전히 고정된다(Slang을 벤더링한 것과 같은 이유).

## 무엇을 걷어 냈나

상류 저장소에서 **빌드에 필요한 것만** 가져왔다. 걷어 낸 것:

| 걷어 낸 것 | 왜 |
|---|---|
| `tests/`, `examples/` | 각각 Catch2·glm·glfw·glad·imgui·stb를 FetchContent로 끌어온다. 우리는 라이브러리만 쓴다 |
| `docs/`, `.github/`, `cmake/` | 문서·CI·빌드 스크립트. 우리는 vcxproj로 직접 컴파일한다 |
| `src/fastgltf.ixx` | C++20 모듈 인터페이스. 이 저장소는 모듈을 쓰지 않는다 |

남은 것은 헤더 8개(약 245KB) + 소스 3개(약 300KB) + simdjson 2개(약 7.2MB)다.
**simdjson 통합 헤더가 용량의 대부분**인데, 자동 생성 amalgamation이라 Vulkan
헤더처럼 부분만 떼어낼 수 없다. 쪼개진 배포본을 쓰면 파일이 수백 개가 된다.

## simdjson은 구현 세부다

`include/fastgltf/*.hpp` 어디에도 `simdjson.h`가 없다. 여는 것은
`src/base64.cpp`와 `src/fastgltf.cpp`뿐이다. 그래서 `deps/simdjson/`은
**그 두 파일을 컴파일할 때만** include 경로에 올린다(vcxproj의 per-file
`AdditionalIncludeDirectories`). 저장소의 다른 코드가 `<simdjson.h>`를 여는
일이 없도록 전역 include 경로에 넣지 않는다.

## 빌드 편입

`Engine/RenderEngine/RenderEngine.vcxproj`가 소스 4개를 직접 컴파일한다.
per-file 설정 셋이 필요하다 — 없으면 깨진다:

- `PrecompiledHeader=NotUsing` — RenderEngine은 `pch.h`를 강제하는데 외부
  소스에는 그 include가 없다.
- `IncludeInUnityFile=false` — 유니티 빌드가 외부 소스를 우리 TU와 합치면
  매크로·익명 네임스페이스가 충돌한다. simdjson amalgamation은 특히 위험하다.
- `WarningLevel=TurnOffAllWarnings` — 남의 코드 경고로 우리 빌드 로그를
  더럽히지 않는다.

## 갱신하려면

1. 상류에서 새 태그를 받아 `include/`·`src/`(단 `.ixx` 제외)·`LICENSE.md`를 덮어쓴다.
2. 새 판본의 `cmake/dependencies.cmake`에서 `SIMDJSON_TARGET_VERSION`을 읽고,
   그 판본의 singleheader 두 파일로 `deps/simdjson/`을 덮어쓴다.
3. 위 판본 표를 고친다.
4. `experiment.import`·`experiment.model`로 실자산 회귀를 확인한다.
