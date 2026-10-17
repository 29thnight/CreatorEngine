# Source-free AssetSet 패키징 계약

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 세 개의 생산물

source-free 패키지는 하나의 cook 명령이 아니다. 먼저 `build-asset-set`으로 typed CEMF3와 immutable blob 디렉터리를 만든다. `build-runtime-bootstrap`은 scene/prefab/설정과 필요한 runtime 문서 및 manifest bridge를 만든다. 마지막 `package-game --content-mode PrebuiltAssetSets`가 둘과 사전 컴파일된 GameScripts, 발행된 Player를 조합한다.

AssetSet output은 반드시 새 디렉터리이며 C# orchestration은 native completion report, CEMF version envelope, build-key hash와 허용된 blob 파일 inventory를 확인한다. CEMF의 세부 의미 검증은 native reader가 소유한다. Legacy v2 package manifest를 자동 변환하는 입력 경로는 아니다.

## bootstrap 생산

RuntimeBootstrap은 선택된 배포와 project pin, content ABI를 검증한다. project의 문서 입력을 별도 candidate에 복사하고 configured set group을 activation validator에 제공한다. texture identity sidecar는 입력 증거로 포착·재검증하지만 runtime 문서로 배포하지 않는다.

scene/prefab/audio/collision 등 지원되는 cooked 결과와 CEBR document-reference bridge를 구성하고 source geometry/audio 및 제외 대상 sidecar를 제거한다. runtime settings와 시작 scene/backend는 여기서 고정한다. `CEBOOT1` report는 engine buildId/payloadDigest, content ABI, input/content digest, exact file entries 및 preflight를 기록한다. 검증에만 잠시 합친 AssetSets는 bootstrap 자체에 중복 저장하지 않는다.

`ValidateBoundary`가 허용하는 shader source(.slang/.hlsl/.hlsli)와 문서 확장자는 남을 수 있다. 여기서 source-free란 원본 모델·텍스처 등의 재import 없이 prebuilt typed payload를 조합한다는 의미다. 모든 종류의 소스 문자열이 패키지에 0개라는 뜻은 아니다.

## 최종 조합의 제한

PrebuiltAssetSets는 `--bootstrap-root`, `--asset-set-list`, `--game-scripts-assembly`를 요구한다. Project mode만 허용하고 asset-list, startup-scene, render-backend override와 build-native를 거부한다. 이미 동결된 bootstrap을 다른 설정으로 조용히 재해석하지 않는다.

AssetSetPackaging은 native `--copy-asset-set`으로 각 set을 복사하고 manifest hash 목록으로 `CEAS1` activation policy를 만든다. 기존 source가 activation policy를 공급하면 중복 정본을 거부한다. 설치 ABI는 manifest의 선언뿐 아니라 hash inventory에 묶인 Player/AssetCooker build record와 실제 payload까지 검증한다. `--asset-set-abi`는 호출자의 assertion이지 실제 host ABI를 덮어쓰는 스위치가 아니다.

## 실패와 수용

bootstrap과 AssetSet은 새 candidate를 검증한 후 발행한다. package는 마지막 Player smoke와 digest 재확인 뒤 current pointer를 변경한다. prebuilt GameScripts receipt도 정확한 엔진 buildId/script API/hash와 맞아야 한다. 손상 set, 누락 hard dependency, stale bootstrap, 다른 engine, source 삭제 후 실행, 취소·종료를 각기 검증해야 한다.

현재 구현의 조합 경로를 설명했으며 source-free Windows 패키지 실행을 이번에 재검증하지 않았다. [BuildPipelinePlan](../plans/BuildPipelinePlan.md), [AssetSetImportReceipts](AssetSetImportReceipts.md)와 함께 사용한다.

## 구현 근거

- [BuildTool/Program.cs](../../BuildTool/Program.cs)
- [BuildTool/AssetSetBuilding.cs](../../BuildTool/AssetSetBuilding.cs)
- [BuildTool/RuntimeBootstrap.cs](../../BuildTool/RuntimeBootstrap.cs)
- [BuildTool/AssetSetPackaging.cs](../../BuildTool/AssetSetPackaging.cs)
- [BuildTool/ContentCompatibility.cs](../../BuildTool/ContentCompatibility.cs)
- [BuildTool/GamePackager.cs](../../BuildTool/GamePackager.cs)
- [BuildTool/GameCompiler.cs](../../BuildTool/GameCompiler.cs)
- [Tools/AssetCooker/AssetSetBuild.cpp](../../Tools/AssetCooker/AssetSetBuild.cpp)
