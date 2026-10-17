# MAT-9 Blender 이미지 비교: 재현·판정·현재 범위

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 비교 대상

MAT-9의 목적은 Blender와 비슷한 노드 이름을 제공하는 것에서 더 나아가, 같은 geometry·normal/tangent·camera·light·material 입력의 pre-tone scene-linear 출력을 비교하는 것이다. tone mapping, display 변환, screenshot의 외관만으로 BSDF 오차를 판단하지 않는다. 고정 Cycles fixture, EEVEE/Material Preview, 실제 live split-sum renderer는 서로 다른 대상이다.

현재 live Scene은 최종 split-sum lookup을 사용한다. 과거의 고비용 directional reference bake 이미지가 통과했다고 현재 화면이 같은 적분을 수행한다고 주장할 수 없다. [live/reference 차이](MAT9SplitSumLiveLookupDifference.md)를 함께 읽는다.

## 측정 파이프라인

`material_matched_reference.py`는 고정 Blender 입력과 manifest/geometry/reference pixels를 만든다. `measure-material-blender-images.ps1`은 manifest와 sphere 입력을 확인하고 고유 output directory를 요구하며 native probe를 빌드·실행한 뒤 Python 비교기를 부른다. 기존 label을 덮어쓰지 않는다. 이 스크립트는 읽기 전용 문서 검사 도구가 아니다.

`compare-material-blender-images.py`는 reference/native metadata와 hashes, 공통 emission-control coverage, normal/view 진단을 대조한다. 재질마다 유리한 mask를 고르지 않고 공통 내부를 2픽셀 침식한 영역을 사용한다. `reference-repeat`는 독립 seed의 noise를 구분하는 입력이다. 측정 완료 marker와 `materialAcceptance=pending`은 서로 모순이 아니다. 정상 측정과 목표 수용은 다른 단계다.

## 고정 공학 목표

relative RGB RMS ≤1%, normalized p95 ≤1%, normalized max ≤5%, 독립 reference seed RMS ≤0.25%를 유지한다. normalized error는 `abs(native-reference)/max(1,abs(reference))`다. 이 수치는 이 프로젝트의 고정 공학 gate이며 보편적인 지각/JND 기준이나 FPS 동등성 기준이 아니다.

`assess-hdri-material-images.py`와 `assess-special-material-images.py`는 실제 pixel hash와 수치를 다시 확인하고, 다른 seed이되 같은 setup을 요구한다. diagnostic shader root, MIS 비활성화 또는 편의상 바꾼 subset을 제품 수용으로 올리지 않는다. exit 2는 정상 측정 뒤 목표 미달일 수 있으며 compile/GPU 실패와 구분한다.

## 보존된 결과와 해석

- [GGX/박막 고정 on/off](MAT9GgxClosureComparison.md): 당시 제한된 조명·시야 범위의 수용
- [HDRI 최종 고정 26조건](MAT9HdriConvergence.md): constant Core/Layered, 두 HDRI의 당시 수용
- [Special Surface](MAT9SpecialProfileCorrection.md): 7/18, 전체 미수용
- [bounded Volume](MAT9SpecialTransportComparison.md): 고정 단일 산란 사례의 당시 수용

JSON·PNG·numeric golden은 역사적 증거로 보존한다. 현재 revision/host/backend의 새 결과로 덮어쓰지 않았다. 원본 area-light grid, texture/factor/normal-map, Deferred/Forward route parity, extreme grazing, live split-sum과 실제 모델 품질·성능은 별도 gate로 남는다.

## 구현 근거

- [`Tools/blender/material_matched_reference.py`](../../Tools/blender/material_matched_reference.py)
- [`Tools/regression/measure-material-blender-images.ps1`](../../Tools/regression/measure-material-blender-images.ps1)
- [`Tools/regression/compare-material-blender-images.py`](../../Tools/regression/compare-material-blender-images.py)
- [`Tools/regression/assess-hdri-material-images.py`](../../Tools/regression/assess-hdri-material-images.py)
- [`Tools/regression/assess-special-material-images.py`](../../Tools/regression/assess-special-material-images.py)
- [`Tools/regression/material_matched_image_probe.cpp`](../../Tools/regression/material_matched_image_probe.cpp)
