# Slang

CreatorEngine PHASE 3.5 M1B가 직접 소유하는 셰이더 컴파일러 런타임이다.
설치된 Vulkan SDK나 PATH의 Slang을 빌드·실행 fallback으로 사용하지 않는다.

## 판본과 출처

```
Slang 2026.14
출처: https://github.com/shader-slang/slang/releases/tag/v2026.14
원본 archive: slang-2026.14-windows-x86_64.zip
archive SHA-256: 36029c50ef0c82f2616ffb02e0ed27d642cb44a2a297d531cc2ad333b85b85b6
라이선스: Apache-2.0 WITH LLVM-exception

DXC v1.9.2607 (July 2026 stable)
출처: https://github.com/microsoft/DirectXShaderCompiler/releases/tag/v1.9.2607
원본 archive: dxc_2026_07_29.zip
archive SHA-256: a1dfb116ba3eeae6a1582291b53a8e7bf65ad760676bd3194685c8f7367cd241
```

각 공식 archive의 SHA-256을 GitHub release digest와 대조한 뒤 아래 최소 파일만
옮겼다. Slang은 SPIR-V를 직접 생성하지만 DXIL에는 downstream DXC를 동적 적재한다.
따라서 `slang-compiler.dll`과 같은 `bin`에 고정 DXC의 `dxcompiler.dll`·`dxil.dll`을
함께 둔다. 패키지 PATH를 stage+Windows System32로 제한한 Player smoke가 이 구성을
검증한다.

| 파일 | SHA-256 |
|---|---|
| `bin/slang-compiler.dll` | `bf277592acab648f8bd1777054a6703b6df797b7f2fca55ed6116777363986d7` |
| `bin/dxcompiler.dll` | `9a5100511e127c6a2fc78edf984f95074a76d35b90c90c4d342430a5ae160e9b` |
| `bin/dxil.dll` | `feb57253eff0a622561e29b44cedbe86b89fc9a5bc8dc00fa2f98fafd712c2d8` |
| `include/slang.h` | `aca0f581325c260ab420f54d0759ce17ca14a9d788583a7554f0b9e2a752b6a9` |
| `include/slang-com-helper.h` | `ceb79764b1266d1f2f4fcc3318514d507c2136e4d2d57c638201821ce628bb47` |
| `include/slang-com-ptr.h` | `70f687139bb19195a1cf0b047376bc1ed749c511b89076800c3d26c8012e38d4` |
| `include/slang-deprecated.h` | `ab7797a04717d560fc6ae297d6dbd8768b2f0d6a600c298e148736ac01579ce5` |
| `include/slang-image-format-defs.h` | `184948d16aaf606399748273e329b0b4715145b5e1475cb7f9b0b3eaee37ad3d` |
| `LICENSE` | `61f6d4d529404f6f8270c30bbe15d755157424cc83a1af563b4840adfd8b5466` |
| `licenses/DXC-LICENCE-MIT.txt` | `903df5512f7d02609fed0c780a9b704f5a3eeb6e4d84ebe42a29845c81899a3c` |
| `licenses/DXC-LICENSE-LLVM.txt` | `729615317e28dd03907e46f0fc3b5e88f7853cee61d1a1471d2749335516b46f` |
| `licenses/DXC-LICENSE-MS.txt` | `734f72f239fe7b07b4c7203f294c1a7ce27095687278bab7e56d630d7c672963` |

## 갱신 규약

1. Slang과 DXC 공식 Windows x86_64 release archive 및 게시 digest를 먼저 대조한다.
2. 위 파일을 교체하고 이 문서의 판본·archive/file hash를 갱신한다.
3. `RHIShaderCompiler` cache identity가 Slang build tag 및 Slang/DXC/DXIL DLL
   콘텐츠 hash를 담는지 확인한다.
4. DXIL/SPIR-V 전 엔트리, include 국소 무효화, strict-math IBL, DX12/Vulkan
   selftest·livecheck, SDK 없는 packaged runtime을 모두 다시 통과한다.

Slang CLI·LLVM·GFX·standard-module·문서 및 DXC 실행 파일·헤더·라이브러리는
런타임 API 경로가 사용하지 않으므로 저장소에 넣지 않는다.

## 2026.14 API 호환 메모

`parseCommandLineArguments()`는 `VulkanBindShift`를 session과 target option에
동시에 싣는다. 두 벌을 그대로 `createSession()`에 넘기면 SPIR-V binding에
리소스 종류 코드가 들어간다. 엔진 어댑터는 전처리 매크로 등 session option은
보존하고 session 쪽 shift 중복본만 거른다. 버전을 올릴 때 이 보정을 임의로
지우지 말고, 동일 버전 `slangc`와 API 산출의 `b0/t100/u200/s300` decoration이
일치하는지 먼저 검증한다.
