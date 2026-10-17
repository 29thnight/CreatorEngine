# Graph material의 자동 cook, package, Player 소비

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## source 수집과 검증

`AssetCooker.cpp`는 명시 `--shadergraph` 입력과 model material에서 파생된 graph 경로를 모아 canonical path로 중복 제거한다. `.shadergraph`가 Assets 안에 있고 16 MiB 이하이며 `.meta` identity를 읽을 수 있어야 한다. strict archive reader로 원본을 읽으며 손상 source를 backup으로 대체해 package하지 않는다.

기본 경로는 `GenerateMaterialSlang → CompileSceneProduct → BuildMaterialProgramCookProduct`다. shader root는 Assets의 DefaultPassShader 또는 명시 override이고 생성 cache는 Library/LXSceneCook에 놓인다. 기존 verified program root를 제공하면 bounded cooked bytes를 읽어 검증하며 임의 옛 package를 찾는 암묵 fallback은 없다.

## target과 identity

package는 Player가 DX12/Vulkan 중 선택할 수 있으므로 DXIL과 SPIR-V를 모두 요구한다. Editor가 현재 backend만 준비한 product는 이 검사에서 불충분하다. Scene target 집합은 current feature의 VS/PS/mesh/shadow/special entry를 정확히 포함해야 한다.

Graph compiler 의미 key와 compiler dependency, generated metadata/source, reflected layout, bytecode가 같은 generation에 묶인다. 현재 SceneHostIdentity 34, CookedProgramVersion 12와 맞지 않는 product는 recook 대상이다. 이전 문서의 LXMC v3/v4 또는 host 10을 현재 package ABI로 재사용하지 않는다.

## publication과 AssetSet 경계

cook은 후보 artifact를 만들고 persisted bytes를 다시 열어 검증한다. duplicate artifact path, source/identity mismatch, incomplete stage, 잘못된 typed dependency를 거부한다. 자산을 수집했다는 사실과 package 게시 완료를 구분한다.

AssetSet v3 source recipe의 MaterialProgram은 explicit verified program bytes를 입력으로 받는 별도 producer다. producer 자체가 hidden shader compiler를 실행하지 않는다. source/meta/verified input snapshot과 publication 전 변경 감지는 `AssetSetBuild`의 책임이다. Code recipe는 [Code Material AssetSet](CodeMaterialAssetSet.md)에 따로 정의한다.

## Player와 소유권

`LoadCookedGeneration`/`LoadSceneShaders`는 catalog/artifact bytes에서 source-free로 복구한다. packaged load에는 `.shadergraph`나 Slang compiler가 필요하지 않다. host identity, running backend, complete target/profile과 nonempty bytecode를 검사한다. 실패를 authoring compile로 우회하지 않는다.

`Generation`과 AssetDepot origin은 exact artifact/default texture owner를 유지하고 instance/frame이 이를 pin한다. mount/root 교체는 이후 요청의 해석을 바꾸지만 이미 받은 immutable owner의 bytes를 재해석하지 않는다. GPU residency/PSO 준비는 CPU decode 후 별도 단계다.

## 검증 경계

필요한 회귀는 cold/warm cook, source/sidecar/include 변경, backend 누락, corrupted artifact, loose/package source-free 재개방, Player 실제 render다. codec roundtrip만으로 Player 화면/종료 성공을 선언할 수 없다. 이번 갱신은 build/cook/package/Player를 실행하지 않았다.

## 구현 근거

- [`Tools/AssetCooker/AssetCooker.cpp`](../../Tools/AssetCooker/AssetCooker.cpp)
- [`Tools/AssetCooker/AssetSetBuild.cpp`](../../Tools/AssetCooker/AssetSetBuild.cpp)
- [`Engine/RenderEngine/Experiment/Cooked/MaterialCookProducer.cpp`](../../Engine/RenderEngine/Experiment/Cooked/MaterialCookProducer.cpp)
- [`Engine/RenderEngine/Experiment/Cooked/MaterialAssetSetProducer.cpp`](../../Engine/RenderEngine/Experiment/Cooked/MaterialAssetSetProducer.cpp)
- [`Engine/RenderEngine/MaterialGraphSceneCompiler.cpp`](../../Engine/RenderEngine/MaterialGraphSceneCompiler.cpp)
- [`Engine/RenderEngine/MaterialGraphRuntime.cpp`](../../Engine/RenderEngine/MaterialGraphRuntime.cpp)
