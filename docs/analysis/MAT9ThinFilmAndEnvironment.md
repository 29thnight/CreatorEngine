# MAT-9 박막·환경 적분 구현의 계보와 현재 계약

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 박막을 바꾼 이유

초기 650/550/450nm 세 파장 평가는 RGB 채널을 가시광 적분처럼 취급하여 간섭색을 충분히 설명하지 못했다. 현재 `PrincipledThinFilm`은 s/p 편광과 경계 위상을 계산하고 Fourier sensitivity LUT와 3차 Airy 간섭 항을 사용한다. linear Rec.709 데이터는 고정 원본과 생성 도구/NOTICE를 갖는다. 직접광과 directional IBL bake는 같은 optical 함수를 사용한다.

film-relative 금속 F82 경계와 dielectric diffuse 감쇠, substrate Fss 에너지 보상은 서로 다른 수정이다. 박막 색 평균을 기판의 multiple-scatter 예산으로 대신하거나 metallic weight를 두 번 곱하지 않는다. 상세 수학 계약은 [Layered semantics](../design/PrincipledLayeredSemantics.md)에 모았다.

## optical oracle의 역할

`measure-thin-film-spectrum.py`는 직접 파장 적분, 3차 절단, LUT 3차 응답을 나누어 비교한다. 이 구분은 LUT 보간 오차와 높은 반사/grazing에서의 차수 절단 오차를 분리하기 위한 것이다. RGB material 입력에서 복원한 metal optical 값과 RGB environment에는 원래의 완전한 spectrum 정보가 없다.

기존 RGB-3 golden과 spectral/GGX golden은 역사적 의미가 다르다. 새 모델의 숫자로 예전 golden을 덮어써 회귀를 숨기지 않는다. thickness=0, film IOR=1, 동일 경계 IOR 등의 불변식은 계속 보존해야 한다.

## 환경 측의 현재 흐름

환경 cooker는 decoded source, cube/prefilter, irradiance/BRDF, importance CDF와 sample bank를 검증된 `.ceibl`로 저장한다. 현재 CEIBL005는 sharp reflection용 source와 1024+4096 proposal bank를 유지한다. source radiance와 proposal PDF는 역할이 다르며 CDF 값을 밝기로 사용하지 않는다.

reference directional bake는 1024 BRDF 샘플과 4096 environment 샘플의 수 비율을 MIS에 반영한다. live Scene은 별도의 최종 split-sum 경로이므로 이 숫자를 매 화면 픽셀의 현재 비용으로 설명하지 않는다.

## 후속 결과의 위치

이 페이지의 2026-09-30 초기 수치는 이후 GGX·HDR range·source sampling·수렴 수정 전의 단계다. 현재 기준으로는 [박막 수용 이력](MAT9ThinFilmAcceptance.md), [GGX 비교](MAT9GgxClosureComparison.md), [HDRI 수렴](MAT9HdriConvergence.md), [live lookup 차이](MAT9SplitSumLiveLookupDifference.md)를 순서대로 사용한다.

고정 optical/numeric 통과를 full rendered parity로 확대하지 않는다. grazing 이미지, 강한 anisotropy, texture와 실제 viewport 비용은 남는 검증이다. 이 갱신에서는 LUT를 재생성하거나 shader를 실행하지 않았고 기존 JSON/이미지를 유지했다.

## 구현 근거

- [`Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledThinFilm.slang`](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledThinFilm.slang)
- [`Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledThinFilmSensitivity.slang`](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledThinFilmSensitivity.slang)
- [`Tools/blender/generate-thin-film-sensitivity.py`](../../Tools/blender/generate-thin-film-sensitivity.py)
- [`Tools/blender/fixtures/thin-film-sensitivity-5.1.1/NOTICE.md`](../../Tools/blender/fixtures/thin-film-sensitivity-5.1.1/NOTICE.md)
- [`Tools/regression/measure-thin-film-spectrum.py`](../../Tools/regression/measure-thin-film-spectrum.py)
- [`Engine/RenderEngine/Assets/CookedEnvironment.h`](../../Engine/RenderEngine/Assets/CookedEnvironment.h)
- [`Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledEnvironmentBake.slang`](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledEnvironmentBake.slang)
