# MAT-9 고정 HDRI 수렴 결과와 현재 live 경계

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 역사적 수용 결과

[최종 판정](MAT9HdriConvergenceTargets.json)은 constant Core/Layered 10개 입력×forest/autumn의 material 20조건과 control 6조건, 총 26/26을 수용한다. 직접광과 원래 area-light/textured grid는 제외된다. [비교 JSON](MAT9HdriConvergenceComparison.json)·[대조 이미지](MAT9HdriConvergenceComparison.png)는 해당 실행의 증거로 그대로 보존한다.

목표는 relative RMS≤1%, p95 normalized≤1%, max normalized≤5%, 독립 seed RMS≤0.25%였다. 기준을 낮춰 통과시킨 결과가 아니다. 다만 당시 revision·cook·reference 경로의 결과이며 current host 34/live split-sum의 새 수용은 아니다.

## 수렴과 제품 오차를 분리한 방법

reference를 131072 samples의 독립 seed 0/11로 만든 이유는 reference noise가 native material 오차와 섞이는 것을 줄이기 위해서다. 이전 native pixels를 새 reference로 다시 비교한 [baseline](MAT9HdriConvergenceBaseline.json)은 별도의 A/B 진단이다. geometry/material/source/cook/strength가 같다는 검증 없이 서로 다른 캡처를 비교하지 않는다.

최종 수용은 새 reference manifest를 소비한 별도 native capture에 근거한다. 기존 pixels의 재계산과 새로운 GPU 실행을 같은 종류의 증거로 세지 않는다.

## 현재 남아 있는 구현

CEIBL005는 1024 proposal 뒤에 4096 reflection proposal을 저장한다. reflection MIS는 두 샘플 수의 비율을 반영하고 diffuse/sheen은 기존 bank를 사용한다. source radiance는 retained RGBA32F를 float bilinear로 읽고, proposal은 sampling density만 제공한다.

`CookedEnvironment`는 legacy 읽기와 current recipe identity를 구분한다. 옛 source-bearing 형식을 읽고 재저장하는 호환 동작이 현재 5120 bank 생성과 같다고 가정하지 않는다. [forest cook](MAT9HdriConvergenceForestCook.json)·[autumn cook](MAT9HdriConvergenceAutumnCook.json)은 당시 upload/readback·identity 기록이다.

## live 사용 정책과 남은 gate

현재 live Scene의 split-sum은 최종 결과다. idle 시 이 고비용 reference로 자동 수렴하는 모드가 아니다. exact/reference cache와 approximate cache도 분리된다. 이 정책은 지연·비용을 줄이는 선택이며 고정 reference의 오차 수용을 live에 이전하지 않는다.

texture/factor/normal-map, 같은 feature의 Deferred/Forward 교차 비교, 원래 area-light 소비, Special surface, 이동 카메라·cold/warm variant·메모리 비용은 남는다. 이번 문서 갱신은 code/JSON 검토만 수행했고 원본 측정과 이미지를 바꾸지 않았다.

## 구현 근거

- [`Engine/RenderEngine/Assets/CookedEnvironment.h`](../../Engine/RenderEngine/Assets/CookedEnvironment.h)
- [`Engine/RenderEngine/Assets/CookedEnvironment.cpp`](../../Engine/RenderEngine/Assets/CookedEnvironment.cpp)
- [`Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledEnvironmentBake.slang`](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledEnvironmentBake.slang)
- [`Engine/RenderEngine/MaterialGraphSceneLookup.cpp`](../../Engine/RenderEngine/MaterialGraphSceneLookup.cpp)
- [`Tools/regression/assess-hdri-material-images.py`](../../Tools/regression/assess-hdri-material-images.py)
- [`docs/analysis/MAT9HdriConvergenceTargets.json`](MAT9HdriConvergenceTargets.json)
