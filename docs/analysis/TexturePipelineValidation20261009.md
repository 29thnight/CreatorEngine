# PHASE 12 구현 적용 및 수용 검증 — 2026-10-09

작업 시작 2026-10-09, 최종 결과 정리 2026-10-10 KST. 파일명은 시작일 기준이다.

## 범위와 출처

- [PR #166](https://github.com/29thnight/CreatorEngine/pull/166), 병합 커밋 `117b729a60acf9663dd6f41fc9b71f4ce6f60e00`.
- 로컬 HEAD `d5a6a26a78da93686b8168429183ee7e0d50a8de` 위에 병합된 소스를 작업 트리로 적용했다. HEAD/index 변경이나 commit/push는 하지 않았다.
- 현재 HEAD 이후 master의 선행 PR #119/#130 의존성을 포함한다. 기존 RG8/GCCE 수정은 유지하고 충돌 여부를 사전 확인했다. 원본 백업은 `C:/Users/lance/AppData/Local/Temp/creator-phase12-integration-6rxput91`에 있다.
- MSVC v145 / Visual Studio 18 / x64, Debug 및 Release. GPU 검증 장비는 NVIDIA RTX 4070 Ti.
- 병합 여부, CPU 계약, 빌드, GPU 결과, 패키지 소비를 별도 증거로 취급한다. 전체 T0/T1a/T2 수용이나 T1b 성능 달성을 선언하지 않는다.

## 빌드 및 실행 중 발견한 수정

| 위치 | 문제와 수정 |
|---|---|
| `Engine/SceneRuntime/MeshRenderer.cpp` | `ParseText` 정의 헤더 누락 보완 |
| `Engine/RenderEngine/Texture.cpp` | `AssetSetEntry`의 실제 GUID 필드 경로로 진단 수정 |
| `Engine/RenderEngine/DataSystem.cpp` 및 Editor 재질 소비자 | FileGuid/문자열 overload 모호성을 명시적 `std::string_view`로 해소 |
| `Engine/RenderEngine/Interfaces/AssetAuthoringPort.h` | 독립 translation unit에서 찾을 수 있는 Texture 상대 include |
| `Editor/EngineGUIWindow/DrawYamlNodeEditor.cpp` | 파일 대화상자의 wide 문자열을 filesystem path로 변환 |
| `Editor/RenderTests/ExperimentParity/ExperimentSceneCookSelfTest.cpp` | 이미 ReadNode인 envelope root에서 중복 `Read()` 제거 |
| `Tools/regression/verify-material-codegen.ps1` | 실제 nested vcpkg include/lib/bin 경로와 일치하는 DLL 사용 |
| `Tools/regression/material_codegen_probe.cpp` | 기본값 재지정은 변경 없음(false)을 반환하므로 BC5 fixture가 실제 값 변경을 수행하도록 수정 |
| `Tools/regression/verify-experiment-contract.ps1` | 구성에 맞는 vendor DLL 경로를 실행 환경 앞에 추가 |
| `Tools/regression/experiment_contract_probe.cpp` | standalone 호스트가 전체 검사 동안 COM을 유지하고 종료 직전에 WIC factory를 정리 |
| `Editor/RenderTests/ExperimentParity/ExperimentMaterialInstanceSelfTest.cpp` | CB 비교 fixture의 base가 `FOG=on`을 선택하므로 ShaderMeta에도 FOG 축 선언; unknown keyword 거부 규칙 유지 |

COM 수정의 근거: per-call `CoInitializeEx`/`CoUninitialize`만 가진 독립 재현에서 첫 PNG metadata 호출은 성공하고 두 번째 호출이 DirectXTex WIC에서 접근 위반으로 종료했다. Editor와 AssetCooker는 이미 main에서 COM을 유지한다. standalone probe도 같은 호스트 수명 계약을 갖도록 수정했다. DirectXTex의 [WIC factory 문서](https://github.com/microsoft/DirectXTex/wiki/WICFactory)는 cached factory와 명시적 교체/정리를 설명한다. 진단용 forced include/link 옵션은 정식 검증 빌드에 사용하지 않았다.

## 현재 확인된 결과

| 검증 | 결과 | 경계 |
|---|---|---|
| texture import Editor source | Python 5 tests 통과 | 정적 계약 |
| BuildTool | `BUILD_TOOL_TESTS_OK checks=142` | managed 단위/통합 계약 |
| Debug·Release Editor / Player / AssetCooker / AssetPacker / BuildTool | 실제 project build 통과 | 런타임 수용과 별도 |
| Player dependency boundary | target 통과 | source/build metadata 경계 |
| Release RenderEngine archive | WIC/HDR/TGA/DDS decode/save 및 mip/compress 함수 정의 0 | Shipping 실제 링크 전체의 대체 증거 아님 |
| Debug·Release texcook | 각 기존 138/138, PHASE 12 53/53 통과 | GPU 없는 실제 PNG/HDR/DDS cook·손상 거부·밉 정책·BC5/BC7·Terrain byte contracts |
| Debug·Release 전체 experiment contract | 각 13/13 통과 | texcook 외 font/material codec·instance·seal·sampler·normal·tangent·shaderMeta·vertex layout 등 회귀 |
| Debug·Release cooked Scene GPU 소비 | 각 24 frames, checks=57259, gpuComponents=10746, sceneCompiles=0, validation=0, encrypted PAK; 반복 cook 동일·실패 6종 accepted 출력 보존, sources=719/drift=0 | native DX12 회귀 probe; 실제 Player 패키징은 별도 |
| material codegen | cases=22, checks=15244, compiled=101, rejected=31, gpuChecks=3960 | BC5 normal reconstruction 포함; 전체 텍스처 품질 코퍼스 아님 |
| material metadata | JSON cases=22 통과 | generated metadata 구조 |
| Debug·Release PNG 정본 왕복 | 각 실제 프로젝트 102/102 및 RGBA byte fixtures 통과 | 같은 DirectXTex decoder의 왕복; 외부 decoder 정확도 대조 아님 |
| Debug·Release HDR 정본 왕복 | 각 실제 프로젝트 19/19 float DDS 왕복 일치 | float bits 보존; 시각 품질 판정 아님 |
| DX12/Vulkan `vk.texturecodec` | 각 4 textures, 95604 logical bytes, 226 rows, digest `948bc2173e154fae` 일치 | BC1_SRGB·BC3·BGRA8·비압축; BC5/BC7 GPU 전체 수용 아님 |
| PNG/JPEG embedded model export | 두 glTF fixture, CECT schema=2/representation=2, identity 및 generation fingerprint 보존 | legacy schema/ambiguous references 거부; 실제 Player 모델 scene 소비는 별도 |
| builtin blue noise | cooker가 CECT2 BC3 128×128/8 mips/22256 bytes 생성 | 내장 resource cook |
| Debug·Release 실제 Player 패키지 | programs=3, materials=1; Debug gameThreadFrames=2649/displayFrame=2659, Release gameThreadFrames=2831/displayFrame=2833; 각 promotions=120, sceneCompiles=0, textParserCalls=0, payloadPreserved=true, source drift=0 | 모델 임베디드/독립 텍스처 산출물을 포함한 제한된 source-free DX12 fixture; 모델 모든 텍스처의 실제 sampling 및 전체 코퍼스 수용 아님 |
| Release / Release-Shipping Player 실제 DLL import | 각 DirectXTex 및 decode/mip/compress import 0 | 실제 네이티브 DLL |
| Release 실제 Player 패키지 runtimeEntries | DirectXTex/stb 이미지 코덱 항목 0 | 현재 제한된 패키지 manifest의 실제 closure |
| Release-Shipping Player | 실제 전체 dependency build 통과; RenderEngine codec 정의 0; 공유 런타임 22 files/codec files=0, ABI shipping=1 | loader probe 통과; Shipping GPU 패키지 smoke는 별도 미실행 |

Release 링크에는 기존 라이브러리 PDB type record 관련 LNK4020 경고가 있었다. 실행 파일 빌드 성공을 디버그 심볼 완전성으로 확대하지 않는다.

초기 Debug 패키징은 10/07 BuildTool 바이너리가 남아 새 `.cetex` 경로를 거부했다. 현재 source의 Debug BuildTool을 별도 빌드한 뒤 fresh candidate 재검증을 통과했다. Release의 통과 패키지도 현재 BuildTool을 사용했다.

대시보드 전체 JS 파싱/렌더 및 유한 진행률 계산은 통과했다. 구조 검사기는 HEAD에서도 존재하던 PHASE 4.85 quoted-key RTP 14개를 잘못 읽어 string violations 140/shape errors 14를 보고한다. 현재 변경 후 같은 수량이며 이 별도 기존 문제를 PHASE 12 완료 근거로 쓰거나 범위 밖에서 수정하지 않았다. 변경 문서의 whitespace 검사는 통과했다.

## 재현 경로

검증 스크립트:

- `Tools/regression/verify-texture-import-editor-source.py`
- `Tools/regression/verify-experiment-contract.ps1 -Configuration Release -Only texcook`
- `Tools/regression/verify-texture-canonical-roundtrip.ps1 -Configuration Release`
- `Tools/regression/verify-material-codegen.ps1`
- `Tools/regression/verify-model-texture-export.py` (PNG/JPEG 모델 generation을 준비한 독립 project)
- `Tools/regression/verify-material-scene-cook.ps1 -SkipDependencyRestore -SkipProjectReferences`

현재 실행 로그는 ignored `Build/phase12-*.log`에 보존한다. 실행 파일 SHA-256은 `Build/phase12-binary-hashes.json`에 있다. PNG/JPEG 모델 project는 `Build/Phase12ModelFixture-ef3add4fec5745e88932308f484aaeae`, 정본 왕복 유지 스크립트 결과는 Release `Build/TextureRoundtrip-e1fe3827e02d4e3da26e9b23f2797e72/results.jsonl`, Debug `Build/TextureRoundtrip-5d364ba7681e4b5da12c557ac0f36258/results.jsonl`에 있다. Vulkan 업로드 대조는 `Build/Phase12Roundtrip-4962468abbcf4b8c87fb730fa0490e5f/results.jsonl`에 있다. 실제 Player 패키지는 Release `Build/Obj/MaterialProductProbe/ScenePkg-Release-7d009f765d664dffb93965be25a89e62`, Debug `Build/Obj/MaterialProductProbe/ScenePkg-Debug-aad0e2f7fd764d4687557fe910868ccf`에 보존한다. 검증용 source asset/sidecar는 격리된 fixture/project에만 작성했다. PR 자체의 source asset 변경은 앞의 구현 적용 범위에 포함한다.

## 남은 수용 조건

1. Shipping GPU 패키지 smoke를 추가한다. 실제 Shipping build/link/loader 및 staged codec closure는 이번 실행에서 확인했다.
2. 새 BC5/BC7, cube/HDR 등의 실제 runtime GPU 소비 범위를 넓힌다. 기존 Vulkan digest 통과만으로 이 범위를 닫지 않는다.
3. Inspector 설정 변경·확인창 직접 조작 및 SpriteRenderer/모델 등의 소비자 수용을 확장한다. 2026-10-10 후속 자산 서비스 검증은 [임포트 수명 기록](TextureImportLifecycleValidation20261010.md), 씬 교체 성공/실패는 [씬 재로드 기록](TextureSceneReloadValidation20261010.md), 실제 한 프로세스의 ImageComponent 재임포트→재로드 수용과 빈 참조 저장 수정은 [종단 기록](TextureReimportSceneValidation20261010.md)에서 구분한다.
4. 모델 모든 텍스처의 source-free Player 실제 sampling과 version/hash/settings 불일치 거부를 확장한다. 현재 DX12 fixture를 전체 자산·Vulkan 패키지 수용으로 확대하지 않는다.
5. 현재 코퍼스의 품질·cook 시간·메모리·디스크·recook/reload 지연을 측정한다. 과거 수치를 재사용하지 않는다.

T0/T1a/T2는 진행 중, `earnedDays: 0`. T1b는 품질/성능 수용 대기. T3는 실제 모바일/Web 제품 타깃이 정해질 때까지 중단이다.
