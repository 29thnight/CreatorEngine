# MAT-9 박막 초기 판정과 후속 GGX 수용의 구분

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 보존한 판정

[초기 판정 JSON](MAT9ThinFilmAcceptance.json)의 `accepted=false`는 그대로 유효한 역사적 결과다. 같은 이름의 문서가 현재 존재한다는 이유로 true로 바꾸지 않는다. 후속 [GGX 판정](MAT9GgxFilmAcceptance.json)은 별도 실행·수정 뒤 film 16/off 16의 제한된 조건을 수용한 결과다. 두 결과는 모순이 아니라 서로 다른 generation의 증거다.

## 고정 입력과 수용 조건

8개 film/off 쌍을 sun과 white furnace에서 비교한다. thickness=0 대조군은 박막 함수뿐 아니라 기판 GGX·diffuse layering의 차이를 찾는 데 필요하다. 공통 emission interior mask를 사용하고 geometry/normal/view 진단을 먼저 통과해야 한다.

목표는 RMS 1%, normalized p95 1%, max 5%, 독립 seed RMS 0.25%다. off 제어가 틀리면 film만 개선되었다는 이유로 전체 모델을 수용하지 않는다. reference noise보다 큰 차이를 단순 샘플 잡음으로 처리하지 않는다.

보존된 이미지의 cosine 범위는 약 0.574..0.998이다. extreme grazing은 이 이미지 안에 없으며 별도의 optical oracle로 다뤘다. 따라서 이 범위의 이미지 통과를 모든 입사각의 rendered 수용으로 확대하지 않는다.

## 현재 구현에서 이어지는 불변식

`PrincipledThinFilm`의 3차 Airy/Fourier 응답과 기판 energy compensation은 서로 다른 책임이다. film IOR=1, thickness=0, 인터페이스가 사라지는 동일 IOR 조건을 보존한다. 후속 GGX 수정은 nonlinear 보상을 metal/dielectric 각 lobe에 적용한 뒤 혼합하고 dielectric diffuse를 별도 감쇠한다.

현재 live split-sum은 directional film 환경 반사를 완전히 재현하지 않는다. [Layered 계약](../design/PrincipledLayeredSemantics.md)과 [live/reference 차이](MAT9SplitSumLiveLookupDifference.md)의 근사 한계를 함께 적용한다.

## 재검증 요구

`assess-thin-film-images.py`는 고정 입력, 독립 seed, 실제 pixel 수치와 product provenance를 기준으로 판정한다. 새 실행에서는 현재 host/source/binary identity를 별도 산출물에 기록하고 기존 초기 JSON·PNG를 보존해야 한다. 이번 문서 작업은 판정 파일과 코드를 읽었으며 새 수용을 만들지 않았다.

## 구현 근거

- [`Tools/regression/assess-thin-film-images.py`](../../Tools/regression/assess-thin-film-images.py)
- [`Tools/regression/measure-thin-film-spectrum.py`](../../Tools/regression/measure-thin-film-spectrum.py)
- [`Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledThinFilm.slang`](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledThinFilm.slang)
- [`Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledLayered.slang`](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledLayered.slang)
- [`docs/analysis/MAT9ThinFilmAcceptance.json`](MAT9ThinFilmAcceptance.json)
- [`docs/analysis/MAT9GgxFilmAcceptance.json`](MAT9GgxFilmAcceptance.json)
