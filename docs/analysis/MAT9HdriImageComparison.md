# MAT-9 HDRI 비교의 원인 분리와 결과 계보

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 먼저 분리할 입력

HDRI 오차는 BSDF 수식만의 문제가 아니다. 원본 decode/색 공간, HDR range, equirectangular→cube resampling, radiance sampler, importance PDF, sample 분산과 reference noise를 각각 분리해야 한다. 원본·밝기·geometry·camera·material을 고정하지 않은 전후 이미지는 이 원인을 설명하지 못한다.

초기 Scene lookup은 point material-field sampler와 linear 환경 radiance sampler를 구분해야 했다. 현재 `BakeRadiance`는 환경용 sampler를 사용하고 source 경로는 float bilinear wrap-U/clamp-V로 조회한다. [mirror 기록](MAT9MirrorSampling.md)은 sharp reflection에서 cube 재표본화와 hardware subtexel 정밀도를 분리한다.

## 보존된 단계

[초기 HDRI 판정](MAT9HdriAcceptance.json)은 material 1/20, control 3/6으로 미수용이다. [MIS 판정](MAT9HdriMisAcceptance.json)은 material 3/20, control 3/6이며 역시 미수용이다. 이 숫자를 현재 품질 상태로 유지하는 대신, 각 수정의 한계를 보여 주는 역사적 결과로 보존한다.

후속 [HDR range](MAT9HdriRange.md), [mirror](MAT9MirrorSampling.md), [수렴](MAT9HdriConvergence.md)은 서로 다른 원인을 처리했다. 최종 고정 26조건 통과가 초기 실패 파일을 무효화하거나 모든 장면 수용으로 바꾸지는 않는다.

## 현재 구현 경계

environment cooker는 source identity와 recipe를 포함한 cache를 만들고 decoded source·lighting map·importance 자료를 보존한다. Scene reference convolution은 source radiance와 PDF proposal을 분리한다. sampler 하나를 바꾸는 작업과 environment cook bytes/recipe를 바꾸는 작업은 cache invalidation 범위도 다르다.

live Scene은 현재 split-sum을 최종 결과로 사용한다. 고비용 reference의 개선 수치를 live 화면의 동일 오차 또는 동일 비용으로 보고하지 않는다. 실제 제품 연결은 [Scene lookup](../design/MaterialGraphSceneLookup.md)을 따른다.

## 비교 gate

고정 suite는 두 HDRI의 constant Core/Layered와 controls이며 직접광/원래 area-light/textured grid는 제외된다. 독립 reference seed, source/cook manifest, 공통 mask, normal/view diagnostics를 확인한 뒤 RMS/p95/max를 판정한다. 동일 조건의 pixel 재평가와 새 native capture도 구분한다.

이번 갱신은 현재 코드와 보존된 JSON을 대조했다. 원본 HDRI·reference/native pixels·PNG를 변경하지 않았고 새 수치나 backend 실행 결과를 만들지 않았다.

## 구현 근거

- [`Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledEnvironmentBake.slang`](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledEnvironmentBake.slang)
- [`Engine/RenderEngine/Assets/CookedEnvironment.cpp`](../../Engine/RenderEngine/Assets/CookedEnvironment.cpp)
- [`Tools/regression/assess-hdri-material-images.py`](../../Tools/regression/assess-hdri-material-images.py)
- [`docs/analysis/MAT9HdriAcceptance.json`](MAT9HdriAcceptance.json)
- [`docs/analysis/MAT9HdriMisAcceptance.json`](MAT9HdriMisAcceptance.json)
- [`docs/analysis/MAT9HdriConvergenceTargets.json`](MAT9HdriConvergenceTargets.json)
