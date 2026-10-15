# MAT-9 GGX closure 수정과 고정 박막 결과

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 역사적 판정

[MAT9GgxFilmAcceptance.json](MAT9GgxFilmAcceptance.json)은 당시 film on 16/16, off 16/16을 기존 목표로 수용했다. [전체 비교](MAT9GgxClosureComparison.json)는 측정 데이터이며 자체 설명은 diagnostic이다. 최종 정책 판정 파일과 단순 비교 파일을 구분한다. 이번 갱신은 두 파일을 수정하거나 새 GPU 실행을 하지 않았다.

## 수정의 근거와 현재 의미

기존 혼합 총반사율로 diffuse를 감쇠하면 금속 weight가 중복되어 기판이 어두워질 수 있다. 현재 diffuse budget은 dielectric closure의 layering albedo를 사용하고 metallic weight는 혼합에서 적용한다.

GGX의 부족 에너지를 별도의 Lambertian lobe로 더하면 직접광 반사 모양이 달라진다. 현재 보상은 metal/dielectric/coat의 기판 Fss와 directional energy를 사용한 곱셈 계수로 각각 적용한 뒤 합친다. nonlinear 보상을 혼합 후 한 번 수행하는 것과 같지 않다. film이 켜져도 기판 Fss를 film 평균색으로 대체하지 않는다.

Principled graph의 Core와 Layered는 같은 보상 모델을 공유한다. feature/tier는 기능 분류이고 IBL의 계산 model 선택은 별도다. 독립 legacy model 0 helper가 남아 있어도 graph의 model 1 검증 기준을 바꾸지 않는다.

## 데이터와 ABI

고정 GGX E/Eavg·dielectric albedo 표는 생성 도구가 원본 hash를 확인하고 NOTICE를 동반한다. 표의 float 수나 source byte 수는 GPU register 사용량·compiled binary 크기·FPS가 아니다. 최종 compiler 배치 비용은 별도 측정해야 한다.

IBL point 176-byte/sample 144-byte의 helper 계약과 논리 BSDF 구조체는 서로 다르다. model 1의 보상된 반사 적분을 소비할 때 multiple-scatter 에너지를 또 더하지 않는다. [Core](../design/PrincipledCoreSemantics.md)와 [IBL bake](../design/PrincipledIblBake.md)에 현재 계약이 있다.

## 수용 범위와 미완료 항목

고정 sun/furnace·constant material·공통 내부 mask의 결과다. 강한 anisotropy의 isotropic energy LUT, extreme grazing rendered 이미지, HDRI 방향성, textured inputs, live split-sum과 전체 모델 성능은 자동 포함되지 않는다.

이후 HDRI 변경과 host identity 증가가 있으므로 당시 host 4 artifact를 현행 host 34로 재사용할 수 없다. 현재 품질 게이트는 같은 입력/오차 정책과 current source/binary identity를 새로 기록해야 한다. 과거의 완료 공수나 check 수를 현재 진행률로 복제하지 않는다.

## 구현 근거

- [`Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledBrdf.slang`](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledBrdf.slang)
- [`Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledLayered.slang`](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledLayered.slang)
- [`Tools/blender/export-ggx-energy-tables.py`](../../Tools/blender/export-ggx-energy-tables.py)
- [`Tools/regression/principled_ggx_energy_tables.h`](../../Tools/regression/principled_ggx_energy_tables.h)
- [`Engine/RenderEngine/MaterialGraphIblBake.h`](../../Engine/RenderEngine/MaterialGraphIblBake.h)
- [`docs/analysis/MAT9GgxFilmAcceptance.json`](MAT9GgxFilmAcceptance.json)
- [`docs/analysis/MAT9GgxClosureComparison.json`](MAT9GgxClosureComparison.json)
