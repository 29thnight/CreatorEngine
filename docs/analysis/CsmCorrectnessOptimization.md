# CSM 정확도와 최적화의 현재 기준

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 수신 영역과 그림자 후보

ShadowMath::ReceiverCascades는 세 cascade의 수신 영역을 만든다. perspective split은 로그/균등 혼합, orthographic split은 균등이며 camera far와 설정 distance로 범위를 제한한다. sphere fit을 양자화하고 light-space XY texel snap을 적용한다. cascade blend 구간까지 포함한 수신 영역을 pre-budget 선택과 shadow projection이 공유한다.

화면 밖 caster도 수신 영역에 그림자를 만들 수 있다. CastsInto는 광원 방향의 보수적 cylinder 관련성을 사용하며, 알 수 없는 반지름은 조용히 버리지 않는다. affine scale·shear의 bound는 ScaleBound와 ShadowCasterBounds를 통해 다루며 skin bounds는 실제 sealed geometry/palette 계약을 확인해야 한다.

## 깊이 범위와 bias

EnhancedShadowPass::PrepareGraphFrame은 identified caster stream과 재질 그래프 입력을 요구한다. 각 cascade에서 관련 caster가 광원 쪽으로 멀리 있으면 backOff를 늘려 orthographic depth span에 포함한다. camera frustum 밖이라는 이유로 caster를 제거하거나 backOff 확장을 빼면 멀리 있는 가림체의 그림자를 잃을 수 있다.

bias는 cascade의 world texel과 depth span에 연결되고, splitDepths.w는 선택한 광원 index+1을 표현한다. Fog와 forward/material 조명의 shader가 같은 cascade·bias 의미를 읽어야 한다. 조명 또는 shadow matrix를 캡처할 때 index와 단위를 함께 기록한다.

## 현재 GPU 소비

ShadowStage가 Geometry table을 받고 cascade별 meshlet culling·command producer와 METEOR.Scene.Shadow.C0/C1/C2를 소유한다. 각 출력은 3-slice D32Float shadow map의 Modify 버전이다. survivor list는 현재 posed geometry와 각 light projection을 기준으로 생성하며 camera HZB와는 독립적이다.

논리 caster count, meshlet 후보, 실제 survivor, bucket submission과 최종 깊이 coverage를 구분한다. prepared count만으로 그림자 누락이 없다는 결론을 내리지 않는다. 과거 indexed/meshlet 선택 옵션은 현행 SceneHost 필수 meshlet 경로의 사용자 설정으로 제공하지 않는다.

## 수용해야 하는 사례

- 광원 쪽으로 멀리 떨어진 caster, 카메라 바깥 caster와 빈 cascade
- split 경계·blend band, camera 이동·회전과 texel snapping
- 정적·skin pose, 음수/비균일 scale·shear와 양면 재질
- alpha cutoff의 통과·동일·거부 경계와 여러 meshlet에 걸친 topology
- 실제 보이는 meshlet 하나를 누락시킨 변이가 depth/최종 shadow oracle에서 실패하는지

동치 검사는 같은 frame·view·pose·light matrix와 세 cascade attachment를 대조한다. 성능은 GPU validation/capture를 켠 정확도 실행과 일반 프레임을 나눠 반복 측정한다. 이번 감사에서는 위 사례를 실행하지 않았다.

## 남겨 둔 설계 의도

지속 shadow-depth cache는 현재 라이브 조립에 독립 cache invalidation 계약으로 연결되어 있지 않다. 도입한다면 caster geometry·pose·world·material alpha, 광원, cascade fitting과 scene generation을 key에 포함하고 실패·퇴출·GPU 완료 수명을 정의해야 한다. 정적인 장면의 과거 결과만으로 동적인 장면의 cache 적합성을 판정하지 않는다.

## 소스 근거

- [Engine/RenderEngine/Render/Graph/ShadowMath.h](../../Engine/RenderEngine/Render/Graph/ShadowMath.h)
- [Engine/RenderEngine/Render/Graph/ShadowCasterBounds.h](../../Engine/RenderEngine/Render/Graph/ShadowCasterBounds.h)
- [Engine/RenderEngine/Render/Passes/Geometry/EnhancedShadowPass.cpp](../../Engine/RenderEngine/Render/Passes/Geometry/EnhancedShadowPass.cpp)
- [Engine/RenderEngine/MaterialGraphSceneShadowStage.inl](../../Engine/RenderEngine/MaterialGraphSceneShadowStage.inl)
- [Engine/RenderEngine/MaterialGraphSceneHost.cpp](../../Engine/RenderEngine/MaterialGraphSceneHost.cpp)
- [Engine/RenderEngine/GpuGeometryVisibility.cpp](../../Engine/RenderEngine/GpuGeometryVisibility.cpp)
