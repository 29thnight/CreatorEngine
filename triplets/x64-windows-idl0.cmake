# x64-windows-idl0 — x64-windows와 같되 디버그 빌드에서 이터레이터 디버깅을 끈다.
#
# 왜 (ContainerLibraryDesign §5 C0-2):
#
#   MSVC는 _DEBUG가 정의되면 _ITERATOR_DEBUG_LEVEL을 2로 두고, 그 상태에서
#   모든 컨테이너 인스턴스마다 _Container_proxy를 힙에 따로 할당한다. 이것이
#   두 가지를 동시에 망친다.
#
#     1. 성능 — 디버그 빌드가 느려지는 주된 원인이다.
#     2. 측정 — 할당 귀속을 떠 보면 상위 지점이 전부 _Container_proxy로 채워져
#        실제 코드의 할당량이 가려진다(실측: 상위 8개 중 7개).
#
#   이 매크로는 링크 단위 ABI라 프로세스 안의 모든 목적 파일이 값을 맞춰야 한다.
#   엔진만 0으로 돌리면 vcpkg 디버그 라이브러리(값 2)와 충돌해 LNK2038이 난다
#   — 실제로 그 오류로 이 트리플릿이 필요하다는 것을 확인했다.
#
# 되돌리기: Directory.Build.props의 VcpkgTriplet 줄을 지우면 기본 x64-windows로
# 돌아간다. 설치 트리가 트리플릿별로 갈리므로 기존 vcpkg_installed는 그대로 남는다.

set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE dynamic)

# 디버그 구성에만 넣는다. 릴리스는 _DEBUG가 없어 이미 IDL이 0이다.
set(VCPKG_C_FLAGS_DEBUG   "/D_ITERATOR_DEBUG_LEVEL=0")
set(VCPKG_CXX_FLAGS_DEBUG "/D_ITERATOR_DEBUG_LEVEL=0")
