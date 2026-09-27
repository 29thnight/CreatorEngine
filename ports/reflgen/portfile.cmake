# reflgen — 헤더 전용 라이브러리 + 생성기(reflgen.exe, libclang.dll) + MSBuild targets.
#
# 설치 배치(vcpkg):
#   include/reflgen/...                    라이브러리
#   tools/reflgen/reflgen.exe, libclang.dll 생성기
#   tools/reflgen/clang/include             clang 내장 header(libclang 과 같은 판) — 생성기가 파싱에 쓴다
#   share/reflgen/msbuild/reflgen.targets  .vcxproj 연동 — tools/reflgen 의 생성기를 스스로 찾는다
#   share/reflgen/reflgen-config.cmake     find_package(reflgen) — reflgen_generate() 가 같은 생성기를 쓴다
#
# 생성기는 libclang 을 쓴다. Visual Studio 의 "C++ Clang tools for Windows" 구성 요소가 설치돼 있어야 한다
# (VS 에 딸린 LLVM 의 libclang 을 찾는다).
#
# 개발 중에는 scripts/make-overlay-port.ps1 이 이 파일의 원본 가져오기를 로컬 저장소의 커밋으로 바꾼 overlay
# port 를 만든다(REFLGEN_SOURCE 표지 사이).
set(VCPKG_BUILD_TYPE release) # 생성기는 도구이고 라이브러리는 헤더뿐이다 — debug 빌드가 필요 없다.

# REFLGEN_SOURCE_BEGIN (make-overlay-port.ps1 -Ref cdf7787796a1b1ffaf0424ff15e96fe9ea4e3acc)
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO 29thnight/reflgen_cpp
    REF cdf7787796a1b1ffaf0424ff15e96fe9ea4e3acc
    SHA512 be5b3e9d140bc3ca8d2925e9ad30a16ea2066266d9c3b105963e4a5baad0e1e580a64ff958347901c0a9cb001665ba5476691af0a56e83a01a7563bc6fc61bed
    HEAD_REF main
)
# REFLGEN_SOURCE_END

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DREFLGEN_BUILD_TESTS=OFF
        -DREFLGEN_BUILD_EXAMPLES=OFF
        -DREFLGEN_BUILD_GENERATOR=ON
)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/reflgen)

# bin/ 의 reflgen.exe, libclang.dll, clang/include(clang 내장 header)를 tools/reflgen/ 으로 옮긴다.
vcpkg_copy_tools(TOOL_NAMES reflgen AUTO_CLEAN)
file(COPY "${CURRENT_PACKAGES_DIR}/bin/libclang.dll" "${CURRENT_PACKAGES_DIR}/bin/clang"
     DESTINATION "${CURRENT_PACKAGES_DIR}/tools/reflgen")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/bin" "${CURRENT_PACKAGES_DIR}/lib")

vcpkg_install_copyright(FILE_LIST
    "${SOURCE_PATH}/LICENSE"
    "${SOURCE_PATH}/third_party/clang-c/LICENSE.TXT")
