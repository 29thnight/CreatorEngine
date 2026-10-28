# Creator.Containers 모듈

22개 구현의 정본은 이 디렉토리의 `.ixx` 인터페이스 파티션이다. `Creator.Containers.ixx`가 전체 API를 재수출한다. 구현 헤더를 포함하는 래퍼가 아니라 각 파티션의 export 블록 안에 실제 구현을 둔다. 내부 런타임 헤더와 컴파일러 매크로는 전역 모듈 조각에 둔다. STL 컨테이너와 `import std`에 의존하지 않는다.

```cpp
import Creator.Containers;
int main() {
    ce::dynamic_array<int> values;
    values.push_back(42);
    ce::string text("CreatorEngine");
    return values[0] == 42 && text.size() == 13 ? 0 : 1;
}
```

CMake 3.28 이상에서 `-DCE_MODULES=ON`으로 구성하고 소비 타깃을 `CreatorContainers::Modules`에 연결한다. MSVC에서는 기본 ON이다. C++23을 사용하며 IFC/PCM은 컴파일러·옵션·구성별로 다시 빌드한다. MSVC 검증은 Visual Studio 18 2026, CMake 4.3.1로 수행했다.

```cmake
add_subdirectory(path/to/CreatorContainers containers)
target_link_libraries(my_application PRIVATE CreatorContainers::Modules)
```

기존 `include/ce/*.h`는 호환용 생성물이다. 정본 수정 후 `./modules/sync-headers.ps1`을 실행한다. `-Check`는 변경 없이 확인한다. CMake 빌드도 정본과 생성 헤더의 불일치를 거부한다. 헤더 사용 타깃은 `creator_containers_headers`다. 같은 프로그램에서 헤더 API와 named module API를 혼합하지 않는다. 두 타깃을 함께 연결하면 CMake가 거부한다. 직접 헤더를 포함하는 우회 사용까지 감지하지는 않는다.

Windows 재현: `./modules/validate-msvc.ps1 -Tag <새태그>` (필요하면 `-AddressSanitizer`). 설치된 WSL Ubuntu Clang 재현: `./modules/validate-linux.ps1 -Tag <새태그>`. Linux 스크립트는 clang-scan-deps 없이 명시적인 PCM 의존성으로 빌드하며 패키지를 설치하지 않는다.

2026-10-26: MSVC Release 9/9, ASan 9/9, Linux Clang 18 실행 7/7 통과. 교차 번역 단위의 자원/TLS 정체성, 문자열 전달, 예외 전달 및 다른 named module에서의 재수출을 MSVC에서 확인했다. MSVC IFC의 소비자 람다 소멸자 문제는 컴파일러의 `__is_nothrow_destructible` 내장 판정으로 해결했다. 호환 헤더도 같은 판정을 생성한다.

전체 성능 수용은 이전 헤더 바이너리의 결과다. 모듈 바이너리의 별도 확인은 `array_growth,64` 한 항목이며 STL 대비 0.6477, custom/STL spread 1.0051/1.0055였다. 전체 성능 및 엔진 통합 완료를 뜻하지 않는다.
