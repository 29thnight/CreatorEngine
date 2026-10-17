# MAT-9 HDRI MIS: proposal와 radiance의 분리

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## MIS가 해결하는 문제

작고 밝은 HDR emitter는 BRDF 방향 샘플만으로 놓치기 쉽다. environment CDF는 밝은 방향을 더 자주 제안하고, BRDF proposal과 함께 balance heuristic으로 적분한다. proposal의 값이나 PDF를 radiance 자체로 사용하면 조명의 에너지를 바꾸게 된다.

현재 `BakeEnvironmentPdf`는 선택 방향의 CDF cell/PDF를 조회하고 `BakeRadiance`는 실제 decoded source 또는 cube를 읽는다. 양 경로는 동일한 방향 정의와 유효한 PDF를 공유해야 한다. cell 경계 반올림으로 다른 cell의 PDF를 읽는 오류도 회귀 대상이다.

## 현재 reference reflection

`gImportanceSamples`의 폭이 5120이면 앞 1024개 이후의 4096 reflection bank를 쓴다. 이전 1024 폭에서는 기존 bank를 쓴다. BRDF 1024와 environment 4096의 비율을 MIS denominator에 반영한다. 단순히 샘플 수만 늘리고 1:1 weight를 유지하면 bias가 생긴다.

반사 결과의 RGB 정규화 denominator는 prepared directional albedo와 동일 BRDF sequence를 유지한다. environment-dependent denominator로 바꾸지 않는다. tinted dielectric/F82/film의 채널별 반응을 단일 scalar로 대체하지 않는다. sheen은 자신의 LTC proposal과 원래 1024 environment bank를 사용한다.

## cook와 소비 경계

importance rows/marginal/direction-PDF samples는 CEIBL에 저장하고 upload/readback을 검증한다. cooker 성공만으로 실제 Scene이 같은 bank를 읽는지 확인된 것은 아니다. current recipe, source, map dimensions/format과 shader contract를 함께 확인해야 한다.

이 적분은 reference/준비 경로의 계약이다. live split-sum 최종 lookup의 매 픽셀 비용이나 수렴 과정으로 설명하지 않는다.

## 역사적 판정과 제한

[MIS 단계 판정](MAT9HdriMisAcceptance.json)은 20개 material 중 3개, 6개 control 중 3개만 당시 목표에 들었고 전체 미수용이다. MIS만으로 source clipping·cube resampling·reference noise를 해결하지 못했다. [range](MAT9HdriRange.md)와 [최종 수렴](MAT9HdriConvergence.md)의 별도 결과를 합쳐 읽는다.

새 검증은 proposal PDF 일치, bright-emitter·mirror·diffuse controls, source/cook hash와 independent seed를 포함해야 한다. diagnostic MIS-off 캡처는 원인 분리용이며 제품 수용 도구가 거부한다. 이번 갱신에서는 실행하지 않았다.

## 구현 근거

- [`Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledEnvironmentBake.slang`](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledEnvironmentBake.slang)
- [`Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/IblImportance.slang`](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/IblImportance.slang)
- [`Engine/RenderEngine/Assets/CookedEnvironment.h`](../../Engine/RenderEngine/Assets/CookedEnvironment.h)
- [`Tools/regression/verify-environment-importance.py`](../../Tools/regression/verify-environment-importance.py)
- [`Tools/regression/assess-hdri-material-images.py`](../../Tools/regression/assess-hdri-material-images.py)
- [`docs/analysis/MAT9HdriMisAcceptance.json`](MAT9HdriMisAcceptance.json)
