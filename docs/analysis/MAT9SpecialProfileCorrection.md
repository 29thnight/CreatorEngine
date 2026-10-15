# MAT-9 Special profile 수정과 남은 Surface 오차

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 보존된 판정

[최종 Surface 판정](MAT9SpecialSurfaceTargets.json)은 18조건 중 7개만 목표를 만족하며 `accepted=false`다. [비교](MAT9SpecialSurfaceComparison.json), [대조 그림](MAT9SpecialSurfaceComparison.png), [검증 identity](MAT9SpecialSurfaceVerification.json)는 당시 host 10의 결과다. 현재 host 34의 새 실행 결과로 읽지 않는다.

당시 sun SSS RMS 약 4.1009%, mixed 약 2.0373%, tinted transmission 약 33.9664%, pure glass 약 34.7890%가 남았다. 수렴한 reference의 seed noise보다 큰 차이를 noise만으로 설명하지 않는다. 허용치를 늘리거나 유리한 flat geometry로 기준을 바꾸어 수용하지 않는다.

## 현재 유지되는 수정의 이유

Scene SSS는 artist radius×scale을 `1/(4π)`로 환산한 뒤 물리 mean-free-path profile에 전달한다. 공용 `BuildSubsurfaceProfile`의 물리 입력 계약과 Scene 저작값 계약은 다르다. 이 길이 환산이 Random Walk의 albedo remapping·내부 path tracing까지 구현한다는 뜻은 아니다.

glass 준비 적분은 공통 Special consumer와 같은 별도 reflection/transmission budget을 사용한다. 일반 GGX 보상을 임의로 더하면 준비와 소비가 달라진다. 이 일치는 single-interface 모델 내부의 정합성이지 폐곡면 입·출구 추적의 대체물이 아니다.

smooth raster normal은 grazing/뒤쪽을 향할 수 있다. Scene 준비는 finite·값 범위·tier·유효 view를 검사하되 standalone front-hemisphere 제약을 그대로 적용해 자홍색 실패 픽셀로 바꾸지 않는다. standalone directional bake의 입력 제약은 별도로 유지한다.

## 전체 이미지 제어

초기 공통 내부 mask만으로는 가장자리 오류를 놓쳤다. Scale=0과 SSS off를 전체 64×64 RGB·alpha로 비교하는 제어가 추가되었다. 보존된 최종 JSON의 sun/furnace는 max abs 0이며, 기존 [pre-grazing 판정](MAT9SpecialSurfacePreGrazingTargets.json)과 구분한다.

이 full-image control 통과와 material 오차 판정 mask는 목적이 다르다. 제어가 통과했다고 SSS 공간 응답 전체를 수용할 수 없다.

## 남은 구현·검증

현재 SSS는 같은 draw owner의 visible surface dipole gather다. 굴절은 더 먼 composed color/opaque depth 또는 environment의 bounded 조회다. closed-interface exit, 내부 재방문, 공간 diffusion, smooth grazing, alpha+transmission와 강한 Volume은 계속 검증·개선할 항목이다.

옛 보고서의 Blend 미지원 문장은 당시 캡처 범위로만 보존한다. 현재 ordered Blend가 있어도 opaque Surface 18장으로 그 합성을 검증한 것은 아니다. texture/normal-map·route parity·area-light·실제 모델 비용도 이 결과에 포함되지 않는다.

## 구현 근거

- [`Engine/RenderEngine/MaterialGraphSceneSubsurface.cpp`](../../Engine/RenderEngine/MaterialGraphSceneSubsurface.cpp)
- [`Engine/RenderEngine/MaterialGraphSceneRefraction.cpp`](../../Engine/RenderEngine/MaterialGraphSceneRefraction.cpp)
- [`Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledSubsurface.slang`](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledSubsurface.slang)
- [`Tools/regression/assess-special-material-images.py`](../../Tools/regression/assess-special-material-images.py)
- [`docs/analysis/MAT9SpecialSurfaceTargets.json`](MAT9SpecialSurfaceTargets.json)
- [`docs/analysis/MAT9SpecialSurfaceVerification.json`](MAT9SpecialSurfaceVerification.json)
