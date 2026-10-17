# MAT-9 Scene 재질 성능: 현재 측정 경계와 과거 회귀

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 성능 수용의 단위

재질 성능은 Scene에 실제 모델을 배치하고 완료된 native render frame, CPU/GPU 구간, viewport와 품질 정책을 함께 측정한다. engine frame ID 증가, profiler의 CPU frame duration 또는 생성 프레임 출력률을 실제 render throughput으로 바꾸어 보고하지 않는다. inclusive CPU 구간을 더하면 중복 계산된다.

현재 측정 runner는 native-real 정책을 요청하고 그 receipt generation의 acknowledgement를 기다린다. FG/upscale/NIS/DeepDVC 설정을 확인하고, 저장된 `.ceprof`의 per-frame provenance를 최종 근거로 삼는다. 실행 전 live endpoint 조회만으로 과거 capture의 설정을 소급 인증하지 않는다.

## 현재 runner의 흐름

`measure-material-scene-performance.ps1`은 기존 Editor process와 기존 label을 거부한다. fixture model/Scene을 검사하고 프로젝트를 복사한 뒤 배율·shader를 격리본에 적용한다. default 측정 구간은 8초, 허용 3..120초다. 원본 프로젝트의 UI 설정을 변경하는 도구로 사용하지 않는다.

Editor command service의 async 완료를 기다리고 live snapshot·GPU timing·warm geometry upload 상태를 확인한다. recording pause/finalize/save 완료와 nonempty complete capture를 확인한 뒤 `summarize-material-profile.py --require-native-real`로 파일 자체를 검증한다. product frame capture와 실제 attachment dimensions를 확인하며 requested window 크기를 Scene 해상도라고 가정하지 않는다.

정확성 probe는 GPU validation on, timing은 별도 validation off 구간으로 분리한다. validation off에서 오류가 보고되지 않았다는 사실을 validation on 통과로 표시하지 않는다.

## 과거 회귀에서 남길 교훈

2026-09-30의 작은 viewport CreatorRobot 조사는 raster chunk를 compute IBL batch 한도와 분리하고, immutable geometry의 반복 LOD 생성과 pose palette 중복 포장을 줄일 필요를 보여 주었다. 같은 recording의 검증된 material packet 공유와 graph writer indexing도 CPU 준비 비용에 영향을 주었다.

현재 geometry/pose/packet 경계는 [mesh surface](../design/MaterialGraphMeshSurface.md), [pass recording](../design/MaterialGraphPassRecording.md)에 설명한다. 과거 small-window FPS와 Debug/Release 표는 당시 hardware·binary·UI scale·기존 lookup 경로의 결과다. 현행 GPU-driven/live split-sum 제품의 throughput으로 재사용하지 않는다. 로컬 Build/Obj 원시 로그가 없으면 옛 문서 숫자만으로 재현 가능한 현재 baseline을 만들 수 없다.

## 비교와 남은 수용

`compare-material-captures.py`는 camera/light/input identity와 attachment pixels를 대조한다. opaque publication order가 달라도 전체 input record를 identity로 정렬해 비교하며 record를 빼서 맞추지 않는다. Debug/Release fixture 이미지 일치는 Blender parity와 별개다.

현재 수용에는 Standard/Layered/Special 실제 모델, 근접/전체 화면, 움직이는 카메라·animation, cold/warm PSO와 material/environment 변경, median/p95/max, completed render FPS와 input lag, upload/VRAM 비용을 포함해야 한다. 이 문서 갱신에서는 측정 runner를 실행하지 않았다.

## 구현 근거

- [`Tools/regression/measure-material-scene-performance.ps1`](../../Tools/regression/measure-material-scene-performance.ps1)
- [`Tools/regression/NativeRealMeasurementGate.ps1`](../../Tools/regression/NativeRealMeasurementGate.ps1)
- [`Tools/regression/summarize-material-profile.py`](../../Tools/regression/summarize-material-profile.py)
- [`Tools/regression/compare-material-captures.py`](../../Tools/regression/compare-material-captures.py)
- [`Engine/RenderEngine/MaterialGraphMeshSurface.cpp`](../../Engine/RenderEngine/MaterialGraphMeshSurface.cpp)
- [`Engine/RenderEngine/MaterialGraphRenderBindings.cpp`](../../Engine/RenderEngine/MaterialGraphRenderBindings.cpp)
