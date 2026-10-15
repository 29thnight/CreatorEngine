# Blender형 Material 지원 대응과 미지원 경계

> 기준: `493536e300e274cbf59989cdd62b20310c8ae64f`의 코드·호출 연결 정적 검토. 이 문서 갱신에서는 빌드·GPU 실행·성능 측정을 수행하지 않았다.

## 기준선의 역할

기준은 저장소에 고정한 Blender 5.1.1 socket/재질 fixture다. `LXMaterialNodes.cpp`는 pinned schema에서 생성한 정의와 엔진 primitives를 등록한다. 이 목록은 Blender 전체 Shader Editor 지원 선언이 아니다. 정의, 문서 왕복, Slang lowering, 제품 자원, 이미지 수용을 각각 판단한다.

## 현재 확인 가능한 지원

Color/Value, Image Texture, Normal Map, Principled BSDF, Material Output 정의가 있고 typed parameter/reroute와 multiply, Separate Color, coordinates/sample, Surface Settings, Principled Volume을 추가한다. 그룹/중첩/interface는 Core/Material 모델에 있으며 compiler는 invocation을 내려 source 위치를 보존한다.

Material 문서는 stable node/pin/parameter ID, typed default, scope, socket enabled/hidden/hideValue, color-space 의도를 저장한다. unknown node는 원문을 보존할 수 있지만 IR/컴파일에서는 거부된다. 일반 Core의 group API와 독립 예제 조작은 제품 전용 panel의 완성을 의미하지 않는다.

## 지원하지 않는 설정

현재 lowering 기준으로 Image Texture는 FLAT·Linear/Closest·REPEAT/EXTEND만 가능하다. Normal Map은 TANGENT/WORLD, named UV map 없음이다. Material Output target은 ALL이며 nonzero Displacement/Thickness는 거부한다. Principled의 nonzero Diffuse Roughness/Weight도 거부한다.

Principled coat/sheen/anisotropy/thin film/transmission/SSS는 지원 의미가 있으나 렌더 근사가 Blender와 같지는 않다. Volume은 엔진 LXPrincipledVolume을 통해 균질 closed static medium만 제품에 연결된다. 임의 Blender node tree import, 모든 enum 전환/mute/hide 동작, 전체 procedural texture/closure는 구현됐다고 볼 수 없다.

## 이미지 parity와 조작 parity

화면 비교는 pre-tone linear HDR, 같은 geometry/normal/카메라/light/environment와 충분히 수렴한 reference를 요구한다. local numeric probe와 같은 bytecode의 Debug/Release 일치는 Blender image parity와 다른 gate다.

live IBL은 split-sum이며 anisotropy/film/sheen 각도 세부를 축약한다. SSS는 dipole 화면 근사, refraction은 screen snapshot/환경, Volume은 bounded single scattering이다. 현재 모드의 차이는 % 지표로 공개하고 reference 적분의 기존 target 통과로 감추지 않는다.

## 유지해야 할 후속 요구

제품 그룹/interface·Blackboard 전용 편집, annotation, reroute wire 삽입과 단일 Undo, socket enum 전환 시 이전 link/default 보존, mute 평가 의미, DPI/keyboard/scene transition/재개방을 별도 수용한다. 노드 확장 시 schema revision, fixture default, lowering/diagnostic, 자원 수명, source-free cook을 함께 갱신한다.

실행 기준과 남은 품질 항목은 [Material 계획](../plans/BlenderMaterialGraphPlan.md), UI 마감은 [LX 계획](../plans/LatticeAdoptionPlan.md), 정확한 미지원 코드는 [Slang 생성](MaterialSlangCodegen.md)에 모았다. 이번 문서는 parity 실행 결과가 아닌 현재 구현 대응표다.

## 구현 근거

- [`Lattice/Material/LXMaterialNodes.cpp`](../../Lattice/Material/LXMaterialNodes.cpp)
- [`Lattice/Material/LXMaterialOperators.cpp`](../../Lattice/Material/LXMaterialOperators.cpp)
- [`Lattice/Material/LXMaterialCompiler.cpp`](../../Lattice/Material/LXMaterialCompiler.cpp)
- [`Lattice/Material/LXMaterialGraph.h`](../../Lattice/Material/LXMaterialGraph.h)
- [`Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledRuntimeIbl.slang`](../../Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/PrincipledRuntimeIbl.slang)
