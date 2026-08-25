# Mathematics 이주 계획

작성: 2026-08-25  
상태: 정찰 완료 · 구현 전  
대상: [`29thnight/Mathematics`](https://github.com/29thnight/Mathematics) `96c41f7d66cd1540685aeb3eed7f6a53bac2d397`

## 0. 결론

`Mathf`가 노출하는 DirectXTK `SimpleMath`/DirectXMath 타입과 연산은 Mathematics로
옮긴다. 다만 `Core.Mathf.h`의 별칭만 한 번에 바꾸는 방식은 사용하지 않는다.
현재 `SimpleMath` 저장 타입이 raw `XMVECTOR`/`XMMATRIX`로 암시 변환되는 성질에
호출부가 기대고 있고, Mathematics는 의도적으로 그 암시 변환을 제공하지 않는다.

최종 상태는 다음과 같다.

- 벡터·행렬·쿼터니언·스칼라·TRS·교차 판정의 정본은 `math::*`다.
- 새 코드가 호출하는 함수의 정본도 `math::normalize`, `math::compose`처럼
  Mathematics 네임스페이스다. `Mathf`에 동일 API를 재포장하지 않는다.
- `Mathf::xMatrix`와 `Mathf::xVector`는 다른 이름의 별칭으로 존치하지 않고 없앤다.
  저장 값은 `math::matrix4x4`, `vector3/4`, `quaternion`이고, `math::vec_reg`는
  한 함수 안의 실제 핫 루프에서만 값으로 사용한다.
- Mathematics에 없는 `Color`와 UI `Rect`는 엔진 소유의 서로 다른 의미 타입으로
  분리한다. `Color4 = math::vector4`로 합치지 않는다.
- PhysX, GPU, C# ABI, YAML은 라이브러리 객체를 그대로 믿고 넘기는 경계가 아니다.
  레이아웃을 검증하고, 필요한 경계는 필드 단위로 명시 변환한다.
- `DirectX::BoundingFrustum`은 Mathematics에 대응 타입이 없으므로 마지막까지
  별도 격리한다. 완전한 DirectXMath 제거는 Mathematics에 frustum을 먼저 추가한
  뒤에 닫는다.

이 문서는 교체 구현이 아니다. 외부 헤더, 프로젝트 include 경로, 현재 소비자는
아직 변경하지 않는다.

## 1. 현재 배선

### 1.1 진입점

현재 수학 표면의 루트는 `Engine/Utility_Framework/Core.Mathf.h`다.

```text
Core.Minimal.h
  -> Core.Definition.h
       -> DirectXMath.h
       -> DirectXColors.h
       -> directxtk12/SimpleMath.h
  -> Core.Mathf.h
       -> Mathf::Vector2/3/4     = DirectX::SimpleMath::*
       -> Mathf::Matrix         = DirectX::SimpleMath::Matrix
       -> Mathf::Quaternion     = DirectX::SimpleMath::Quaternion
       -> Mathf::xVector        = DirectX::XMVECTOR
       -> Mathf::xMatrix        = DirectX::XMMATRIX
       -> Color/Rect + JSON/Assimp helper + Easing/Tween
```

`Core.Mathf.h`를 직접 include하는 파일은 적어도 Physics, RenderEngine 인터페이스,
SceneRuntime의 Transform/UI/Input, Utility reflection, Editor Scene View에 퍼져 있다.
그보다 큰 실제 도달 표면은 `Core.Minimal.h`의 전이 include다. 따라서 마지막
별칭만 바꾸면 직접 include 목록보다 훨씬 많은 번역 단위가 동시에 깨진다.

### 1.2 2026-08-25 현재 수치

`Build`, `Bin`, `x64`, `Artifacts`를 제외한 native `.h/.hpp/.cpp/.ixx/.inl`을
PowerShell `Select-String`으로 다시 셌다.

| 표면 | 현재 수치 |
|---|---:|
| `Mathf::*` qualified 사용 | 1,153건 / 141파일 |
| raw DirectX math 타입·`XM*` 함수·collision 타입 | 1,196건 / 104파일 |
| `DirectX::SimpleMath::*` 직접 사용 | 403건 / 43파일 |
| `<DirectXMath.h>`/`<DirectXCollision.h>` 직접 include | 10파일 |
| `<directxtk*/SimpleMath.h>` 직접 include | 7파일 |

`Mathf::*`의 큰 축은 `Vector3` 283, `Vector2` 174, `Vector4` 166,
`Matrix` 140, `xMatrix` 112, `xVector` 88, `Color4` 76, `Rect` 40,
`Quaternion` 25건이다. raw 함수는 `XMVectorSet`, `XMMatrixIdentity`,
`XMMatrixInverse`, `XMMatrixTranspose`, `XMVector3Normalize`,
`XMMatrixMultiply`, `XMMatrixPerspectiveFovLH`, `XMMatrixLookAtLH` 순으로 많다.

Physics는 `Mathf` 별칭을 우회한다. `PhysicsCommon.h`, `Physx.cpp`, ragdoll,
rigid body, collider와 SceneRuntime 물리 브리지의 공개 필드·인자에
`DirectX::SimpleMath::Vector3/Quaternion/Matrix`가 직접 박혀 있다. 이 섬은
`Core.Mathf.h` 교체와 별개의 이주 단위다.

### 1.3 값이 흐르는 주요 경계

| 경계 | 현재 타입/행동 | 이주 시 지켜야 할 계약 |
|---|---|---|
| Transform 정본 | `Vector4` position/rotation/scale, `TransformStore`의 `Matrix`/`Vector4` | 첫 이주에서는 필드 수와 YAML `x/y/z/w` 형상을 바꾸지 않는다 |
| Camera -> Render | `FrameCameraSnapshot`이 `xMatrix` 4개와 `xVector` 4개를 값으로 전달 | 레지스터 타입 저장을 없애고 matrix + 의미별 vector 값으로 바꾼다 |
| Render -> GPU | matrix transpose와 packed vector를 상수 버퍼에 복사 | 기존 transpose 위치와 8/12/16/64B 레이아웃을 그대로 고정한다 |
| Reflection/YAML | Vector2/3/4, Color4, Quaternion, Rect, xMatrix를 타입별 emit/read | 키 이름, 순서, 16-float matrix 형상을 바꾸지 않는다 |
| Editor/ImGui | `&v.x`를 `DragFloatN`/`ColorEdit4`에 전달 | 연속 public float 레이아웃을 static_assert로 고정한다 |
| Physics/PhysX | SimpleMath 값과 `PxVec3/PxQuat/PxMat44` 사이 필드 변환, 한 곳은 `memcpy` | `memcpy`를 없애고 필드 단위 변환을 정본으로 만든다 |
| Native -> C# | 별도 `Float2/3/4` Sequential ABI | 함수표 버전과 wire layout은 이번 이주에서 바꾸지 않는다 |
| Collision/culling | `BoundingBox/Sphere/Frustum` 값이 Camera, AI, Foliage, light packing을 통과 | aabb/sphere와 frustum을 다른 슬라이스로 나눈다 |

카메라 값은 이미 `Scene -> FrameCameraSnapshot -> Render` 값 경계로 닫혀 있다.
수학 타입 이주가 Camera 소유권이나 render thread 동기화 계약을 다시 열 이유는 없다.

## 2. Mathematics 적합성

확인한 기준은 위 고정 커밋의 공개 헤더와 README/GUIDE다. 라이브러리는 헤더 온리,
C++20 이상이고 CreatorEngine은 전체 C++ 프로젝트가 C++23이므로 언어 기준은 맞는다.

### 2.1 그대로 맞는 규약

| 항목 | CreatorEngine/DirectX 계열 | Mathematics |
|---|---|---|
| 행렬 저장 | row-major | row-major `m[row][column]` |
| 벡터 적용 | 행벡터 | `v * matrix` |
| TRS 합성 | `scale * rotation * translation` | 같은 순서 |
| 이동 위치 | 3행 | `m[3][0..2]` |
| 쿼터니언 | `(x,y,z,w)` | 같은 저장 순서 |
| 쿼터니언 곱 | DirectXMath 순서 | `a` 적용 후 `b`, DirectXMath와 동일 |
| 투영 깊이 | D3D `[0,1]` | 동일 |
| handedness | LH/RH 함수를 명시 | `_lh`/`_rh`를 반드시 명시 |

`math::vector2/3/4`는 8/12/16B, `math::quaternion`은 16B,
`math::matrix4x4`는 64B이며 공개 헤더가 standard-layout과 trivially-copyable를
assert한다. 행렬은 상수 버퍼에 직접 올릴 수 있는 저장 형상이다.

### 2.2 직접 매핑

| 현재 | 목표 |
|---|---|
| `Mathf::Vector2/3/4` | `math::vector2/3/4` |
| `Mathf::Matrix`, 저장용 `XMFLOAT4X4` | `math::matrix4x4` |
| `Mathf::Quaternion` | `math::quaternion` |
| `Mathf::xMatrix` | 삭제. 저장·반환 모두 `math::matrix4x4` |
| 저장용 `Mathf::xVector` | 삭제. 의미에 따라 `vector3`, `vector4`, `quaternion` |
| 함수 내부 `XMVECTOR` 핫 값 | 필요가 측정된 곳만 `math::vec_reg` |
| `BoundingBox` | `math::aabb` (center + extents) |
| `BoundingSphere` | `math::sphere` |
| `XMMatrixMultiply(a,b)` | `a * b` |
| `XMMatrixInverse` | `math::inverse` 또는 실패를 구분할 때 `try_inverse` |
| `XMMatrixTranspose` | `math::transpose` |
| `XMMatrixDecompose` | `math::decompose` |
| `XMMatrixLookAtLH/RH` | `math::look_at_lh/rh` |
| `XMMatrixPerspectiveFovLH/RH` | `math::perspective_fov_lh/rh` |
| `XMVector3TransformCoord` | `math::transform_point` |
| `XMVector3TransformNormal` | `math::transform_direction` |
| `XMVector3Normalize/Cross/Dot` | `math::normalize/cross/dot` |
| `XMQuaternionMultiply` | `operator*` |
| `XMQuaternionRotationRollPitchYaw` | `math::quaternion_from_pitch_yaw_roll` |
| `XMQuaternionSlerp` | `math::slerp` |

### 2.3 단순 치환이 안 되는 지점

1. **`vec_reg`는 저장 타입이 아니다.** Mathematics 문서와 헤더가 명시적으로
   함수 경계의 계산 타입으로 제한한다. 현재 `FrameCameraSnapshot`, `Camera`,
   `Transform` 반환값에 저장된 `xVector`를 `vec_reg`로 이름만 바꾸면 새 라이브러리의
   계약을 다시 어기게 된다.

2. **암시적 DirectX 브리지가 없다.** SimpleMath는 `operator XMVECTOR()`와
   `Matrix(CXMMATRIX)`를 제공하지만 Mathematics는 제공하지 않는다. 이 부재는
   결함이 아니라 portable API를 유지하는 의도다. `XM*` 호출을 모두 명시적인
   Mathematics 함수로 바꾸거나, 아직 남은 DirectXCollision 경계에서만 좁은 변환을
   써야 한다.

3. **멤버 API가 free-function API로 바뀐다.** `Vector3::Zero`, `.Length()`,
   `.Normalize()`, `Matrix::CreateTranslation`, `.Decompose()`는 각각 `zero()`,
   `math::length`, `math::normalize`, `math::translation_matrix`, `math::decompose`로
   바뀐다. 타입 alias만 바꾸어서는 컴파일되지 않는다.

4. **Color가 없다.** `Mathf::Color4`를 `math::vector4` alias로 만들면 Reflection의
   Vector4/Color4 분기와 YAML의 `x/y/z/w` 대 `r/g/b/a` 구분이 같은 타입으로
   합쳐진다. `Core::Color3/Color4` 같은 distinct standard-layout POD를 엔진이
   소유해야 한다.

5. **Rect와 uint4가 없다.** `Mathf::Rect`는 UI 도메인 타입으로 분리하고,
   bone index의 `XMUINT4`는 `std::array<uint32_t,4>` 또는 별도 GPU `uint4` POD로
   둔다. 수학 라이브러리에 억지로 넣지 않는다.

6. **frustum이 없다.** 현재 공개 타입은 plane/ray/aabb/sphere까지다.
   `BoundingFrustum::CreateFromMatrix`, `Transform`, `Contains/Intersects`를 대체하려면
   Mathematics에 frustum과 DirectXCollision parity test를 먼저 추가해야 한다.

7. **퇴화 입력 정책을 동작 변경으로 취급해야 한다.** Mathematics는 0-vector
   normalize -> zero, singular inverse -> identity처럼 NaN 전파를 줄이는 정책이다.
   ray가 볼륨 안에서 시작할 때 distance 0을 주는 것도 DirectXCollision 일부와 다르다.
   컴파일 성공을 의미 보존의 증거로 삼지 않는다.

8. **현재 성능 표의 AVX2 수치를 곧 엔진 수치로 읽으면 안 된다.** CreatorEngine
   프로젝트에는 현재 `/arch:AVX2` 설정이 없다. 이 상태에서는 x64 SSE2 경로가
   기준이다. 이주와 CPU baseline 상향을 묶지 않고, `/arch` 결정은 별도 벤치마크와
   배포 하드웨어 정책으로 판단한다.

9. **0.1 릴리스 게이트가 아직 닫히지 않았다.** upstream README 기준 기능 구현은
   끝났지만 clang-cl 행렬 곱 처리량과 성능 표 자동화 범위가 release blocker다.
   그래서 `master`를 빌드 때 fetch하지 않고 검증한 commit을 저장소에 고정한다.

## 3. 의존성 배선

### 3.1 권장 방식: 고정 벤더링

`ThirdParty/Mathematics/`에 다음 최소 세트를 고정한다.

```text
ThirdParty/Mathematics/
  include/mathematics/**
  LICENSE
  README.md 또는 PROVENANCE.md  # upstream URL, commit SHA, 갱신 절차
```

이 저장소는 git submodule을 쓰지 않고 CI의 `actions/checkout`도 submodule을 받지
않는다. Mathematics는 현재 vcpkg 정식 포트도 아니므로, 첫 이주에서 submodule이나
빌드 중 네트워크 fetch를 새로 들이지 않는다. `ThirdParty/README.md`에 판본과 이유를
추가하고 `Directory.Build.props`의 모든 C++ 프로젝트 공통 include 경로에
`$(SolutionDir)ThirdParty\Mathematics\include\`를 한 번만 넣는다.

헤더 온리이므로 `.lib`, DLL 배치, `ProjectReference`는 없다. upstream의 test/bench
옵션도 엔진 빌드에 전파하지 않는다. upstream 전체 검증은 Mathematics 저장소에서,
CreatorEngine은 자신이 기대는 ABI·규약·렌더 결과만 검증한다.

### 3.2 컴파일 설정 계약

- C++23은 현재 `Directory.Build.props` 정본을 그대로 사용한다.
- `MATHEMATICS_FORCE_SCALAR`, SSE baseline, AVX 관련 매크로를 프로젝트마다 다르게
  주지 않는다. inline header의 backend 선택은 모든 번역 단위에서 같아야 한다.
- Debug/Release의 기존 floating-point 옵션을 먼저 유지한다. 이주 패치가 `/fp:fast`
  또는 `/arch:AVX2`를 몰래 추가하지 않는다.
- 첫 배선에는 `static_assert`와 standalone contract probe를 둔다.

필수 compile-time 계약은 최소 다음이다.

```cpp
static_assert(sizeof(math::vector2) == 8);
static_assert(sizeof(math::vector3) == 12);
static_assert(sizeof(math::vector4) == 16);
static_assert(sizeof(math::quaternion) == 16);
static_assert(sizeof(math::matrix4x4) == 64);
static_assert(std::is_standard_layout_v<math::matrix4x4>);
static_assert(std::is_trivially_copyable_v<math::matrix4x4>);
```

GPU/C# 경계는 이 assert만으로 끝내지 않고 실제 offset/직렬화/업로드 검사를 함께 둔다.

### 3.3 전환용 interop 규칙

공존 기간에는 변환을 한 파일로 숨기되 암시 변환은 만들지 않는다.

- DirectXCollision용 `ToDirectX(math::matrix4x4)` 같은 함수는 frustum 소비자가
  직접 include하는 좁은 `MathInterop.DirectX.h`에만 둔다.
- `Core.Minimal.h`나 `Core.Mathf.h`에서 interop 헤더를 전이시키지 않는다.
- PhysX 변환은 `PxVec3{v.x,v.y,v.z}` / 역방향 필드 복사로 둔다.
- `reinterpret_cast`, 상속, 사용자 정의 `operator XMVECTOR()`로 SimpleMath 호환을
  재현하지 않는다.
- 전환 bridge마다 삭제 대상과 마지막 소비자를 주석 또는 게이트로 남긴다.

## 4. 구현 슬라이스

모든 슬라이스는 독립적으로 빌드 가능해야 한다. 한 슬라이스 안에서는 producer,
value boundary, consumer, test를 함께 닫고 다음 슬라이스로 넘어간다.

### S0. dependency + contract probe, 소비자 미배선

변경:

- `ThirdParty/Mathematics`를 위 commit에 고정하고 provenance/license를 기록한다.
- 공통 include 경로만 추가한다.
- `Tools/regression/verify-mathematics-contract.ps1` 또는 동등한 standalone probe로
  크기·layout·행렬 규약·TRS·quaternion order를 컴파일/실행한다.
- 기존 `Core.Mathf.h`와 소비자는 그대로 둔다.

게이트:

- Debug/Release, MSVC C++23 probe 통과.
- `math::transform_point({1,0,0}, compose(...))`, LH view/projection,
  quaternion multiply가 임시 DirectXMath oracle과 허용 오차 안에서 일치.
- 프로젝트/패키징/런타임 동작 변화 0.

### S1. `Core.Mathf.h` 해체

기존 `UtilityFrameworkModernizationPlan`의 M0~M4를 이 단계의 선행 정리로 수행한다.

- 소비자 0 Assimp/JSON helper와 죽은 `XM*` 상수를 제거한다.
- `Easing/Tween`, `Rect`, `Color`를 수학 헤더 밖으로 분리한다.
- 헤더 전역 `using namespace DirectX`를 제거한다.
- 수학 함수 중복을 Mathematics 이름/의미와 맞춰 정리한다.
- 직접 필요한 include를 각 소비자가 갖게 한다.

게이트:

- Debug non-unity + Release unity 엔진 라이브러리 빌드.
- 기존 reflection golden diff 0.
- `Core.Mathf.h`에서 Assimp/JSON/Easing/Tween include와 전역 DirectX using 0.

이 단계에서는 아직 `Mathf::Vector*` 별칭을 갈아끼우지 않는다. 먼저 전이 의존과
비수학 책임을 줄여 최종 타입 교체의 폭을 실제 소비자로 한정한다.

### S2. Camera -> FrameCameraSnapshot -> Render 첫 수직 슬라이스

첫 실제 소비자는 카메라 값 경계를 권장한다. 행렬, vector, inverse, view/projection,
DX12/Vulkan 결과를 한 번에 검증하면서 Scene 소유권에는 손대지 않을 수 있다.

변경:

- `FrameCameraSnapshot`의 네 행렬을 `math::matrix4x4`로 바꾼다.
- eye position/forward/right/up은 `math::vector3`로 저장한다. `vec_reg`를 저장하지 않는다.
- `Camera`의 계산을 `look_at_lh`, `perspective_fov_lh`, `orthographic_lh`, `inverse`,
  `normalize`, `cross`로 바꾼다.
- 직렬화되는 `m_fov`의 단위는 degree로 유지하고 호출 직전에 `math::radians(m_fov)`를
  적용한다.
- `EnhancedLightPacking`의 기존 `BoundingFrustum` 경계만 명시적 matrix 변환으로
  임시 격리한다.
- RenderTests의 camera fixture도 같은 슬라이스에서 바꾼다.

게이트:

- camera snapshot/parity smoke.
- DX12 camera를 쓰는 geometry/lighting/editor self-test.
- Vulkan geometry/grid/gizmo/skybox/wireframe camera test.
- CPU projection 결과와 화면 pixel 기준선 유지.

### S3. Transform + TransformStore + YAML/reflection

변경:

- `TransformStore` 저장 행렬을 `math::matrix4x4`, 저장 vector를
  `math::vector4`로 바꾼다.
- `Transform`의 `xMatrix`/`xVector` 반환을 의미 타입으로 바꾼다.
- TRS 계산을 `compose/decompose`, `rotation_matrix`, `transform_point/direction`으로
  바꾼다.
- 첫 패스에서는 reflect 대상 position/rotation/scale의 4-float 형상과 field name을
  유지한다. vector3/quaternion으로 줄이는 것은 별도 asset schema migration이다.
- `ReflectionTypedYml.h`의 matrix emit/read는 `m[row][column]`을 직접 순회하고,
  Vector/Quaternion/Color/Rect의 기존 키를 보존한다.
- `ReflectionTypedDraw.h`는 public contiguous float 계약을 assert한 뒤 기존 ImGui
  편집 경로를 유지한다.

게이트:

- reflection golden 전체 통과 + diff 0.
- scene/prefab load -> save -> reload round trip.
- hierarchy/DDOL/physics가 앞당겨 쓰는 world matrix 경로 smoke.
- `sizeof(TransformStore)` 요소 타입과 slot grow/reset 회귀.

### S4. Render payload + mesh/import/animation

변경:

- Material, light, gizmo, UI proxy, pass constant, mesh vertex/bounds를 값 타입부터 옮긴다.
- `XMStore*`/`XMLoad*` 왕복을 packed `math` 값의 명시 필드/행 접근으로 바꾼다.
- GPU constant buffer의 padding/offset을 각 구조체에서 static_assert한다.
- Assimp/fastgltf 입력은 importer 경계에서 한 번만 `math` 값으로 변환한다.
- animation/skeleton의 실제 측정된 register hot loop만 `math::vec_reg` 후보로 두고,
  일반 저장 컨테이너에는 사용하지 않는다.
- `DirectX::Colors::*` 초기화는 엔진 Color 상수로 바꾼다.

게이트:

- Experiment parity(gltf import, tangent, animation playback) 유지.
- DX12/Vulkan geometry, skinning, shadow, lighting, UI/gizmo 결과 유지.
- shader reflection/constant offset gate 유지.

### S5. Physics 독립 섬

변경:

- Physics와 SceneRuntime 물리 bridge의 `DirectX::SimpleMath::*` 403건/43파일을
  한 API 경계씩 `math::*`로 바꾼다.
- `PhysicsCommon.h`의 공개 DTO가 먼저 정본이 되고, PhysX actor/collider/ragdoll
  구현이 그 다음 소비자가 된다.
- `RagdollLink.cpp`의 `memcpy(PxVec3 <- Vector3)`를 필드 변환으로 제거한다.
- `_41/_42/_43` 접근은 `m[3][0..2]` 또는 `translation()`/명시 setter로 바꾼다.
- PxQuat 순서 `(x,y,z,w)`, scale 제거, ragdoll local/world compose 순서를 parity로 고정한다.

게이트:

- Physics library Debug/Release build.
- rigid body sync, collider offset, CCT forced move, raycast/overlap, ragdoll local/world smoke.
- PhysX 변환 헤더 외 `Px* <-> math::*` 임의 변환과 vector `memcpy` 0.

### S6. UI/Editor/나머지 값 타입

변경:

- RectTransform/Input/ActionMap과 editor property UI의 Vector2/3/4 잔여를 옮긴다.
- `Mathf::Color3/4`는 distinct engine color 타입으로, `Mathf::Rect`는 UI Rect로 옮긴다.
- `Mathf::Easing/Tween` 소비자가 새 헤더를 직접 include하게 한다.
- C# `Float2/3/4`와 ScriptCore Quaternion은 wire ABI로 유지하고 native 경계에서
  필드 복사한다. native 타입 이름 변경 때문에 API table version을 올리지 않는다.

게이트:

- UI layout/canvas/DDOL smoke.
- Inspector vector/color/rect 편집 + undo/redo.
- managed transform/input/physics/image/material API smoke.

### S7. bounds/frustum + DirectX 수학 의존 제거

선행:

- Mathematics에 `frustum`과 projection 생성, transform, point/sphere/aabb
  contains/intersects를 추가한다.
- DirectXCollision parity와 degenerate projection 정책을 upstream test로 고정한다.

변경:

- Mesh/ModelLoader의 `BoundingBox/Sphere`를 `math::aabb/sphere`로 옮긴다.
- Camera/AI/Foliage/light packing의 `BoundingFrustum`을 `math::frustum`으로 옮긴다.
- 임시 `MathInterop.DirectX.h`를 삭제한다.
- `Core.Definition.h`에서 DirectXMath/DirectXColors/SimpleMath include를 제거한다.
- 코드 사용이 0이면 `vcpkg.json`의 directxmath/directxtk12 직접 의존도 제거한다.
  DirectXTex, DXGI, D3D12 같은 렌더 API 의존은 이 계획의 제거 대상이 아니다.

최종 게이트:

- source에서 `Mathf::xMatrix`, `Mathf::xVector`, `DirectX::SimpleMath`, `XMVector*`,
  `XMMatrix*`, `BoundingBox/Sphere/Frustum` 0건(역사 문서/ThirdParty 제외).
- `<DirectXMath.h>`, `<DirectXCollision.h>`, `<DirectXColors.h>`, SimpleMath 직접 include 0.
- Debug non-unity + Release unity 엔진 라이브러리, CreatorEditor, Player build.
- reflection golden diff 0, scene/prefab round trip, C# ABI, DX12/Vulkan, Physics smoke 통과.
- 제거 후 `git diff`에 asset/schema 변화가 없고, 실행 산출물에 새 DLL 요구가 없음.

## 5. 첫 구현 패치의 정확한 범위

첫 패치는 S0만 수행한다. 다음을 넘지 않는다.

1. `ThirdParty/Mathematics/include`, license, provenance 고정.
2. `ThirdParty/README.md` 판본/갱신 규약 추가.
3. `Directory.Build.props` 공통 include 경로 추가.
4. 독립 contract probe와 실행 스크립트 추가.
5. Debug/Release probe 실행 결과 기록.

이 패치에서는 `Core.Mathf.h`, `Core.Definition.h`, `vcpkg.json`, 기존 소비자,
project/package/deployment 설정을 바꾸지 않는다. 헤더 온리 의존을 "놓기"와 실제
consumer를 "배선하기"를 분리해야, 외부 라이브러리 고정과 엔진 회귀를 따로 판정할 수 있다.

두 번째 패치가 S1 cleanup, 세 번째 패치가 S2 camera 수직 슬라이스다. S2가 DX12와
Vulkan 기준선을 모두 통과하기 전에는 Transform/Physics로 확장하지 않는다.

## 6. 판정표

| 질문 | 판정 |
|---|---|
| 행렬/쿼터니언 관례가 현재 엔진과 맞는가 | 맞음. DirectXMath parity 규약 |
| 저장 레이아웃이 GPU/YAML에 쓸 수 있는가 | vector/matrix는 맞음. 경계별 assert 필요 |
| `Mathf` alias만 교체할 수 있는가 | 불가. member API와 implicit DX bridge가 다름 |
| `xVector -> vec_reg`로 바꾸면 되는가 | 불가. `vec_reg`는 저장 타입이 아님 |
| Color/Rect도 Mathematics로 가는가 | 아니오. distinct engine domain type |
| BoundingBox/Sphere는 갈 수 있는가 | 가능. aabb(center/extents)/sphere 제공 |
| BoundingFrustum도 지금 갈 수 있는가 | 불가. upstream 기능 선행 필요 |
| C# ABI를 같이 바꿔야 하는가 | 아니오. Float2/3/4 wire shape 유지 |
| AVX2를 같이 켜야 하는가 | 아니오. 별도 CPU baseline 결정 |
| master를 빌드 때 받아도 되는가 | 아니오. 검증 commit 벤더링 |

