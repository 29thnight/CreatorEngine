# MAT-7 alpha·SSS·굴절·Volume 합성의 현재 경계

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 합성 입력과 순서

현재 graph-only Scene은 surface radiance, subsurface 재분배, refraction, volume을 서로 다른 자원과 단계로 관리한다. refractive layer는 자기 color target을 읽지 않고 더 먼 layer까지 완성된 color와 opaque depth snapshot을 읽는다. 이전 frame color를 새 transport의 임의 fallback으로 사용하지 않는다.

alpha는 coverage 합성 계수이며 transmission weight는 BSDF transport 예산이다. 둘을 한 값으로 합치면 alpha 0/1, tint, Fresnel과 opaque occlusion이 틀어진다. Volume은 경로 transmittance와 in-scattering을 계산하고 surface/background의 합성 구간과 맞춘다.

## 현재 구현의 근사

SSS는 visible surface의 world 거리·projected area·동일 draw owner를 사용한 dipole gather다. Cycles Random Walk의 내부 산란과 같지 않다. artist radius×scale은 Scene 경계에서 물리 길이로 환산한다.

굴절은 bounded screen-space ray와 opaque depth를 사용하고 miss에는 environment를 사용한다. 닫힌 물체의 모든 입·출구 경계와 내부 재방문을 추적하지 않는다. Volume 역보정은 작은 T에서 불안정하므로 transmittance를 `1e-4` 이상으로 제한한다. 따라서 강한 흡수의 alpha+transmission 장면은 별도 오차 검증이 필요하다.

Volume 입력은 현재 homogeneous·static·closed consistently oriented mesh에 제한되며 skin/open mesh를 일반 지원으로 취급하지 않는다. [Volume 계약](../design/MaterialGraphSceneVolume.md)에 자원 예산과 거부 조건이 있다.

## owner와 실패

각 pass가 읽는 scene/lookup/effect handle은 동일 frame·view·epoch의 봉인된 입력이어야 한다. 정렬 순서가 달라지면 transport snapshot도 달라져야 한다. 모든 program과 자원 준비가 끝나기 전 일부 layer만 새 상태로 게시하지 않는다.

`material_forward_transport_tests.inl`은 isolated 입력으로 독립 합성식을 만들고 alpha·SSS·refraction·Volume의 상호작용을 검사한다. 예전 host 12 실행 기록은 현재 host 34의 새 수용이 아니다. generated target bytecode·layout이 달라졌으면 현재 cook 계약으로 다시 검증해야 한다.

## 유지할 미완료 게이트

폐곡면 출구 굴절, SSS 공간 응답, grazing normal, interpenetrating/self-overlap, 강한 Volume 역보정의 이미지 오차가 남는다. 순차/병렬 결과, camera cut/disocclusion, viewport 변경, material/environment 변경 뒤 cache invalidation도 실제 제품 입력으로 확인해야 한다.

과거 “Scene Blended 미지원”은 후속 구현으로 바뀌었으므로 현재 제한으로 복제하지 않는다. 반대로 Blend 경로의 존재를 Blender transport parity나 모든 장면의 정확한 합성으로 확대하지 않는다.

## 구현 근거

- [`Engine/RenderEngine/MaterialGraphSceneLayerStage.inl`](../../Engine/RenderEngine/MaterialGraphSceneLayerStage.inl)
- [`Engine/RenderEngine/MaterialGraphSceneRefraction.cpp`](../../Engine/RenderEngine/MaterialGraphSceneRefraction.cpp)
- [`Engine/RenderEngine/MaterialGraphSceneSubsurface.cpp`](../../Engine/RenderEngine/MaterialGraphSceneSubsurface.cpp)
- [`Engine/RenderEngine/MaterialGraphSceneVolume.cpp`](../../Engine/RenderEngine/MaterialGraphSceneVolume.cpp)
- [`Tools/regression/material_forward_transport_tests.inl`](../../Tools/regression/material_forward_transport_tests.inl)
