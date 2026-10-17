# Scene material packet 회귀 helper와 제품 대체 경계

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 현재 위치와 목적

`SceneMaterialPacket`과 `SceneMaterialSlot` 구현은 `Tools/regression/support/MaterialGraphScenePacket.*`에 있다. 제품 Engine의 Scene host API가 아니다. 이 문서는 기존 링크를 유지하면서 packet 기반 독립 회귀가 검증하는 계약과 현재 제품 대응을 구분한다.

helper packet은 selection, coverage, exact instance pin, evaluated CPU point 또는 GPU SurfaceBatch, environment, RenderBindings, graphics pipeline, IBL 결과를 묶는다. CPU/GPU 평가 입력은 정확히 하나를 선택한다. packet 생성만으로 Scene capability를 켜지 않는다.

## Prepare, Bind, PublishSubmitted

`SceneMaterialSlot::Prepare`는 host가 제공한 fixed-state와 coverage의 일치, binding layout, backend/program, evaluation/view identity를 검증하고 후보 packet을 만든다. `CreateScenePassLayout`는 IBL root SRV와 material range를 결합하며 host 충돌을 거부한다.

`Bind`는 현재 recording에 맞는 immutable packet을 소비한다. `PublishSubmitted`는 owning host가 native submission 성공을 확인한 뒤 active packet을 바꾼다. GPU-evaluated input은 completion과 point/source readback 수용이 추가로 필요하다. 실패/pending은 active packet을 보존하고 제출된 실패 후보도 completion까지 유지한다.

## 현재 제품 대응

제품은 `SceneMaterialSource::Capture → SceneViewInput::Seal → SceneHost::SelectReadyInput/Prepare`를 사용한다. material owner table과 geometry/model pin이 view에 묶이고 stage별 exact RGHandle로 visibility, surface, shadow, color, blended, volume을 연결한다.

특히 helper의 Active packet 유지 설명을 제품의 자동 구세대 fallback으로 옮기면 안 된다. 현재 SceneHost는 requested generation 하나라도 pending이면 프레임을 보류하며 이전/native material을 대체하지 않는다. 제품 수명은 submitted frame과 graph owner가 책임진다.

## 유지할 회귀 의미

이 helper는 잘못된 layout, coverage/fixed-state 불일치, CPU/GPU 이중 입력, 실패 후보 보존, 제출/완료 분리를 작게 재현하는 데 유용하다. 제품 shader host ABI, geometry interpolation, RenderGraph usage, texture residency를 대신 검증하지 않는다. 향후 helper를 제거·합칠 때 이 회귀 의도는 보존해야 한다. 최신 실행 결과는 이번에 확인하지 않았다.

## 구현 근거

- [`Tools/regression/support/MaterialGraphScenePacket.h`](../../Tools/regression/support/MaterialGraphScenePacket.h)
- [`Tools/regression/support/MaterialGraphScenePacket.cpp`](../../Tools/regression/support/MaterialGraphScenePacket.cpp)
- [`Engine/RenderEngine/MaterialGraphSceneInput.h`](../../Engine/RenderEngine/MaterialGraphSceneInput.h)
- [`Engine/RenderEngine/MaterialGraphSceneHost.cpp`](../../Engine/RenderEngine/MaterialGraphSceneHost.cpp)
