# Scene 굴절의 배경 snapshot과 Forward 합성

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## Scene 합성 순서

transmission surface는 ordered Blended stream에 포함된다. `DeclareBlended`는 특수 surface input을 준비하고 현재까지 합성된 farther color와 opaque depth를 `DeclareBackground`로 snapshot한다. 현재 쓰는 color target, 자기 transmitting draw, 이전 frame을 배경으로 직접 읽지 않는다.

reference 경로는 `SceneRefractionFrame`의 2 capture input과 80-byte `SceneRefractionSample`을 사용한다. live는 runtime special effect 자원을 사용한다. 둘 다 동일한 surface/instance·recording 계약 안에서 준비되며 Forward+ light tile과 shadow를 공유한다.

## 화면 transport

`LXRefractionBackground`는 world ray를 viewProjection으로 투영하고 bounded step과 8회 이분 refinement로 depth crossing을 찾는다. foreground silhouette/depth가 시작 표면보다 앞이면 배경으로 채택하지 않는다. 화면 밖/유효 hit 없음은 environment radiance fallback이다.

IOR 1 또는 roughness 0은 단일 방향을 추적하고 rough glass는 bounded microfacet ray 응답을 적분한다. reflection과 transmission Fresnel/에너지 조합을 분리한다. alpha는 coverage이고 transmission weight는 physical lobe이므로 같은 값으로 취급하지 않는다.

## Volume과 중복 감쇠 방지

Volume이 있을 때 굴절 ray는 자기 medium segment를 적분한다. farther Forward+ 배경에 이미 camera-ray medium이 들어 있다면 그 segment를 제거한 뒤 refracted ray medium을 적용한다. 역변환은 작은 transmittance에서 수치적으로 불안정하므로 코드의 `max(T,1e-4)` 한계를 품질 gate에 포함한다.

## 자원·오류·수명

`Prepare`는 viewProjection, environment, optional exact Volume frame과 memory budget을 받는다. 잘못된 device/recording, 부족한 budget, input/PSO 준비 실패는 후보를 거부한다. graph Declare는 background color/depth 읽기와 output 쓰기를 명시하며 제출 완료까지 frame owner를 유지한다.

## 근사와 미지원

single color + opaque depth snapshot은 여러 투명 surface의 실제 깊이를 모두 저장하지 않는다. 정렬 단위는 object/draw이며 self-overlap·교차 triangle의 OIT가 아니다. off-screen scene hit, closed-surface 출구 경계의 정확한 다중 굴절, caustic와 path tracing은 지원 보장이 없다.

alpha+transmission·강한 medium·grazing·폐곡면 비교는 별도 품질 수용 항목이다. 코드를 연결했다는 사실로 기존 Special target을 완료 처리하지 않는다. 이번 갱신은 runtime image를 다시 만들지 않았다.

## 구현 근거

- [`Engine/RenderEngine/MaterialGraphSceneRefraction.h`](../../Engine/RenderEngine/MaterialGraphSceneRefraction.h)
- [`Engine/RenderEngine/MaterialGraphSceneRefraction.cpp`](../../Engine/RenderEngine/MaterialGraphSceneRefraction.cpp)
- [`Engine/RenderEngine/MaterialGraphSceneEffectStage.inl`](../../Engine/RenderEngine/MaterialGraphSceneEffectStage.inl)
- [`Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/MaterialGraphRefractionTransport.slang`](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/MaterialGraphRefractionTransport.slang)
