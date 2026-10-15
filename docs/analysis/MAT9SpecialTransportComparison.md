# MAT-9 Special transport 비교와 bounded Volume 수용 범위

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 비교 모델을 먼저 맞춘다

Blender의 closed-solid refraction·Random Walk SSS와 엔진의 single-interface screen/environment refraction·visible-surface diffusion은 같은 transport model이 아니다. Volume 비교도 다중 산란 reference를 single-scattering 구현과 직접 비교하면 recipe 차이와 구현 오차를 혼동한다.

현재 reference 도구는 Special suite와 geometry/transport/exclusion을 manifest에 기록한다. v2 Volume recipe는 직접 단일 산란 범위를 맞추기 위해 RNA `volume_bounces=0`을 명시한다. 이를 원래 Blender 전체 transport와 동등하다고 설명하지 않는다.

## 보존된 결과

[Volume 판정](MAT9SpecialVolumeTargets.json)은 당시 고정 homogeneous 단일 산란 8/8을 수용한다. [비교](MAT9SpecialVolumeComparison.json)와 [독립 적분](MAT9SpecialVolumeIntegral.json)은 같은 bounded 범위의 증거다. closed smooth 80-triangle icosphere, radius 0.5, self-extinction, 외부 occluder 없는 조건이다.

[Surface 판정](MAT9SpecialSurfaceTargets.json)은 별도 7/18 미수용이다. Volume 8/8과 다른 generation의 Surface 결과를 합산하여 Special 전체 통과로 세지 않는다. 자세한 residual은 [profile correction](MAT9SpecialProfileCorrection.md)에 있다.

기존 JSON의 “Scene blended queue unsupported”는 캡처 당시 exclusion이다. 현재 Blend 구현이 추가되었지만 그 역사적 opaque-only 캡처를 alpha+transmission의 검증으로 확대할 수는 없다.

## 현재 제품의 bounded 계약

Volume은 homogeneous coefficient와 closed consistently oriented static mesh를 입력으로 받는다. 16 objects/128 triangles 등 명시 예산을 검사하며 open/degenerate mesh, skin, singular transform과 유효하지 않은 camera 범위를 거부한다. 경계 길이에 Beer–Lambert attenuation과 HG single scattering을 적용한다. [Scene Volume](../design/MaterialGraphSceneVolume.md)이 자원·오류 계약의 정본이다.

surface/refraction/Volume은 각 frame의 정확한 owner와 graph resource를 사용한다. 지원하지 않는 물체를 빈 volume이나 기본 surface로 조용히 바꾸지 않는다. scratch/output 수명은 submission/completion과 분리해서 보존한다.

## 수용 도구와 남은 요구

`assess-special-material-images.py`는 v2 reference recipe, 독립 seed, fixed full suite, product shader root, 실제 pixel hash와 공통 mask를 요구한다. diagnostic geometry/subset/volume-shadow 변형은 제품 판정에서 제외한다. 정상 exit와 미수용 exit 2를 GPU 실패와 구분한다.

multiple/heterogeneous scattering, 외부 visibility와 복잡한 경계, 실제 alpha/transmission·강한 Volume, moving camera와 전체 비용은 별도 gate다. 이번 갱신은 코드/보존 데이터를 읽었으며 새 native 캡처를 만들지 않았다.

## 구현 근거

- [`Engine/RenderEngine/MaterialGraphSceneVolume.h`](../../Engine/RenderEngine/MaterialGraphSceneVolume.h)
- [`Engine/RenderEngine/MaterialGraphSceneVolume.cpp`](../../Engine/RenderEngine/MaterialGraphSceneVolume.cpp)
- [`Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledVolume.slang`](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledVolume.slang)
- [`Tools/blender/material_matched_reference.py`](../../Tools/blender/material_matched_reference.py)
- [`Tools/regression/assess-special-material-images.py`](../../Tools/regression/assess-special-material-images.py)
- [`docs/analysis/MAT9SpecialVolumeTargets.json`](MAT9SpecialVolumeTargets.json)
- [`docs/analysis/MAT9SpecialSurfaceTargets.json`](MAT9SpecialSurfaceTargets.json)
