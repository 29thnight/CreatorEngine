# x64-windows-static-md-idl0 — 정적 라이브러리 + 동적 CRT, 디버그 IDL=0.
#
# 왜 별도로 필요한가 (ContainerLibraryDesign §5 C0-2):
#
#   ManagedHeap.vcxproj는 매니페스트를 우회해 전역 vcpkg 트리의
#   x64-windows-static-md에서 mimalloc을 직접 가져온다(ManagedHeap.vcxproj:102).
#   의도된 배치다 — 동적 mimalloc은 mimalloc-redirect.dll로 프로세스 전역 CRT를
#   후킹해 DLL에서 DllMain을 깨뜨리므로 정적으로 링크해야 한다.
#
#   그래서 매니페스트 쪽 트리플릿(x64-windows-idl0)만 만들면 mimalloc이 여전히
#   IDL=2로 남아 LNK2038이 난다(실측). 이 트리플릿이 그 구멍을 메운다.
#
# 설치: vcpkg install mimalloc:x64-windows-static-md-idl0 --overlay-triplets=triplets

set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE dynamic)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_C_FLAGS_DEBUG   "/D_ITERATOR_DEBUG_LEVEL=0")
set(VCPKG_CXX_FLAGS_DEBUG "/D_ITERATOR_DEBUG_LEVEL=0")
