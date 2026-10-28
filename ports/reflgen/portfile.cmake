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

# REFLGEN_SOURCE_BEGIN (make-overlay-port.ps1 -Ref 985b8121041c65f4099ad39ebbc697cd767b35cb)
vcpkg_from_github(
    OUT_SOURCE_PATH SOURCE_PATH
    REPO 29thnight/reflgen_cpp
    REF 985b8121041c65f4099ad39ebbc697cd767b35cb
    SHA512 944dc0a1f34c8afe29e95cd7fa144e3ef37204ff17b645d35d3d7baa8347c1b39a52b474686385e1ec2687435867947abbb2639313b0d1c534d2f56a28588291
    HEAD_REF main
    PATCHES fsharp-optional-slots.patch
)
# REFLGEN_SOURCE_END

# vcpkg configures with FetchContent disconnected; acquire pinned sources through its download cache.
vcpkg_from_github(
    OUT_SOURCE_PATH IFC_SOURCE_PATH
    REPO microsoft/ifc
    REF 096b22d357d8af1c5f10d5f1895b8e2e80eaae5a
    SHA512 ce7fd0fd9ff385e5940035bbd060110db1e80e3ab37f8f4299c9333c27e1284404b0fa4de1aa8f16cde172b1eae0887b1021a0ebae0465d45f361bc450dc579f
)
vcpkg_from_github(
    OUT_SOURCE_PATH GSL_SOURCE_PATH
    REPO microsoft/GSL
    REF 2828399820ef4928cc89b65605dca5dc68efca6e
    SHA512 a6d731c450da91cba34c13293cc0881a842bd90268dd5ef721ae1f48752c41dc355ebea7716a6de5b49d41568e55751c46a9dd78da4cd68656bffd33e5ee448e
)

vcpkg_cmake_configure(
    SOURCE_PATH "${SOURCE_PATH}"
    OPTIONS
        -DREFLGEN_BUILD_TESTS=OFF
        -DREFLGEN_BUILD_EXAMPLES=OFF
        -DREFLGEN_BUILD_GENERATOR=ON
        -DREFLGEN_BUILD_IFC=ON
        "-DFETCHCONTENT_SOURCE_DIR_REFLGEN_IFC_SDK=${IFC_SOURCE_PATH}"
        "-DFETCHCONTENT_SOURCE_DIR_REFLGEN_IFC_GSL=${GSL_SOURCE_PATH}"
)
vcpkg_cmake_install()
vcpkg_cmake_config_fixup(CONFIG_PATH lib/cmake/reflgen)

# bin/ 의 reflgen.exe, libclang.dll, clang/include(clang 내장 header)를 tools/reflgen/ 으로 옮긴다.
vcpkg_copy_tools(TOOL_NAMES reflgen reflgen-ifc AUTO_CLEAN)
file(COPY "${CURRENT_PACKAGES_DIR}/bin/libclang.dll" "${CURRENT_PACKAGES_DIR}/bin/clang"
     DESTINATION "${CURRENT_PACKAGES_DIR}/tools/reflgen")
file(REMOVE_RECURSE "${CURRENT_PACKAGES_DIR}/bin" "${CURRENT_PACKAGES_DIR}/lib")

vcpkg_install_copyright(FILE_LIST
    "${SOURCE_PATH}/LICENSE"
    "${SOURCE_PATH}/third_party/clang-c/LICENSE.TXT")
