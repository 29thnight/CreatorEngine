# Mathematics 이주 계획

작성: 2026-08-25  
상태: 진행 중 · 업스트림 pin 갱신 완료 · S6-B Rect/UI hitbox 전환 완료 · DirectX 수학 의존 완전 제거 작업 계속
대상: [`29thnight/Mathematics`](https://github.com/29thnight/Mathematics) `d81ca3338ef6f645cc5743625067eece5f1099f0`

2026-08-26 재확인에서 remote `master`/`HEAD`가 위 SHA임을 확인했다. 이전 pin
`04c8bbe30272b3332716cec66cd35dc4d8cb8dbf` 이후 두 커밋은 문서만 변경했으며,
vendored 헤더 32개와 `LICENSE`는 바이트 단위로 동일하다. 그래도 clean detached
checkout에서 해당 파일을 다시 복사하고 provenance와 contract SHA를 새 HEAD로
갱신했다. 업스트림 MSVC C++23 Release 317/317, CreatorEngine contract Debug/Release,
CreatorEditor Debug non-unity와 Release unity 빌드를 통과했다.

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
- `Mathf::Color4`와 `Mathf::Rect`는 각각 distinct type인 `math::color`와
  `math::rect`로 옮긴다. `Color4 = math::vector4`로 합치지 않는다.
- PhysX, GPU, C# ABI, YAML은 라이브러리 객체를 그대로 믿고 넘기는 경계가 아니다.
  레이아웃을 검증하고, 필요한 경계는 필드 단위로 명시 변환한다.
- `DirectX::BoundingBox/Sphere/BoundingFrustum`은 `math::aabb/sphere/`
  `bounding_frustum`으로 옮긴다. AABB affine transform과 frustum projection 생성,
  transform, 교차·포함·raycast는 최신 upstream에 존재하며 선행 기능 추가가 필요 없다.
- `DirectX::BoundingOrientedBox`는 Mathematics에 추가하지 않는다. 현재 유일한
  소비자인 `UIButton`은 orientation을 항등으로 고정하고 2D XY 판정을 직접 하므로
  `math::rect` 기반 UI hitbox로 제거한다.

### 0.1 남은 작업 목록 — 2026-08-26 재작성

이 목록이 이후 실행 순서의 정본이다. 아래 S0~S4-L2b1 구현 기록에서 “변경하지 않았다”는
문구는 당시 슬라이스의 stop point일 뿐 최종 제외를 뜻하지 않는다. 최종 목표는 제품,
Editor, RenderTests와 회귀 도구를 포함한 저장소 소스에서 DirectXMath,
DirectXCollision, DirectXColors와 SimpleMath 의존을 0으로 만드는 것이다. D3D12,
DXGI, DirectXTex 같은 렌더/API 의존은 이 수학 이주의 제거 대상이 아니다.

현재 tracked native source 기준선은 다음과 같다. `ThirdParty`, 설치 패키지와 빌드
산출물은 제외했고 C++ 식별자는 대소문자를 구분해 셌다.

| 잔존 표면 | 현재 수치 |
|---|---:|
| `Mathf::*` qualified 사용 | 300건 / 68파일 |
| `Mathf` 저장 타입 별칭 사용 | 276건 / 62파일 |
| `DirectX::SimpleMath::*` | 17건 / 2파일 |
| raw `XM*` 저장 타입 | 110건 / 10파일 |
| `XMVector*`/`XMMatrix*` 등 함수 | 199건 / 24파일 |
| `DirectX::Bounding*` | 45건 / 21파일 |
| `DirectX::Colors::*` | 0건 / 0파일 |
| DirectX 수학 헤더 직접 include | 12건 / 8파일 |

재현 규칙은 저장 별칭을 `xMatrix/xVector/Color3/Color4/Vector2/3/4/Matrix/Quaternion/Rect`
정확 일치로, raw XM 저장을 `XMVECTOR*`, `XMMATRIX`, `XMFLOAT*`, `XMINT*`, `XMUINT*`와
packed XM storage 식별자로 센다. XM 함수 표면은 `XMVector`, `XMMatrix`,
`XMQuaternion`, `XMPlane`, `XMColor`, `XMScalar`, `XMConvert`, `XMLoad`, `XMStore`
접두 식별자를 센다. 주석을 포함한 tracked
native source의 텍스트 기준이므로, 최종 0 판정에서는 include/dependency gate를 별도로
함께 실행한다.

S5-A~D로 Physics 독립 섬을 닫고 S6-A/B로 native Color와 Rect 저장·직렬화·UI
hitbox를 `math::color/rect`로 연결했다. Physics+SceneRuntime의 SimpleMath/직접 수학
헤더와 전체 저장소의 `Mathf::Color3/4`, `Mathf::Rect`, `DirectX::Colors`는 0이다.
남은 구현은 다음 순서로 닫는다.

1. **S6-C — UI/Editor vector와 helper teardown**
   RectTransform/Input/ActionMap과 Editor property UI의 `Vector2/3/4` 저장·호출 경계를
   `math::vector*`로 옮긴다. `Mathf::Easing/Tween` 소비자는 전용 Mathematics/engine
   helper 헤더를 직접 include하게 해 `Core.Mathf.h`의 비타입 책임도 줄인다.
2. **S7-A — bounds/frustum 전환**
   이미 Mathematics인 Mesh asset/component bounds는 되돌리지 않는다. 남은
   `BoundingFrustum` 25건/17파일, UI/editor bounds, light packing, AI/Foliage,
   SceneView와 gizmo corners/intersection을 `math::bounding_frustum/aabb/sphere`로
   연결하고 `MathematicsInterop`의 collision bridge를 제거한다.
3. **S7-B — root teardown과 최종 0 게이트**
   `Core.Mathf.h` 타입 별칭과 legacy helper, `Core.Definition.h`의 전이 include,
   `MathematicsInterop`의 DX/SimpleMath bridge와 DirectX 기반 contract oracle을 없앤다.
   코드 사용 0을 확인한 뒤 `vcpkg.json`의 `directxmath`/`directxtk12`와
   `Directory.Build.props`의 DirectXTK 설정을 제거한다.
4. **통합 검증**
   Debug non-unity, Release unity의 엔진·CreatorEditor·Player, reflection/asset/C# ABI,
   DX12/Vulkan pixel tests, Physics/UI/frustum runtime smoke를 통과시킨 뒤 정적 0 게이트를
   마지막으로 다시 실행한다.

## 1. 현재 배선

### 1.1 진입점

현재 수학 표면의 루트는 `Engine/Utility_Framework/Core.Mathf.h`다.

```text
Core.Minimal.h
  -> Core.Definition.h
       -> DirectXMath.h
       -> directxtk12/SimpleMath.h
  -> Core.Mathf.h
       -> Mathf::Vector2/3/4     = DirectX::SimpleMath::*
       -> Mathf::Matrix         = DirectX::SimpleMath::Matrix
       -> Mathf::Quaternion     = DirectX::SimpleMath::Quaternion
       -> Mathf::xVector        = DirectX::XMVECTOR
       -> Mathf::xMatrix        = DirectX::XMMATRIX
       -> scalar/legacy helper
```

`Core.Mathf.h`를 직접 include하는 파일은 RenderEngine 인터페이스, SceneRuntime의
Transform/UI/Input, Utility reflection, Editor Scene View에 퍼져 있다.
그보다 큰 실제 도달 표면은 `Core.Minimal.h`의 전이 include다. 따라서 마지막
별칭만 바꾸면 직접 include 목록보다 훨씬 많은 번역 단위가 동시에 깨진다.

### 1.2 2026-08-26 현재 수치

`Build`, `Bin`, `x64`, `Artifacts`를 제외한 native `.h/.hpp/.cpp/.ixx/.inl`을
PowerShell `Select-String`으로 다시 셌다.

| 표면 | 현재 수치 |
|---|---:|
| `Mathf::*` qualified 사용 | 300건 / 68파일 |
| `Mathf` 저장 타입 별칭 사용 | 276건 / 62파일 |
| raw `XM*` 저장 타입 | 110건 / 10파일 |
| `XMVector*`/`XMMatrix*` 등 함수 | 199건 / 24파일 |
| `DirectX::Bounding*` | 45건 / 21파일 |
| `DirectX::Colors::*` | 0건 / 0파일 |
| `DirectX::SimpleMath::*` 직접 사용 | 17건 / 2파일 |
| DirectX 수학 헤더 직접 include | 12건 / 8파일 |

`Mathf::*` 저장 별칭의 큰 축은 `Vector2` 142, `Vector4` 41, `Vector3` 41,
`Matrix` 24, `xVector` 18, `Quaternion` 4, `xMatrix` 6건이다.
`Color3/4`와 `Rect`는 0이다. 별칭 사용이 줄었어도 `Core.Definition.h`가 DirectXMath와
SimpleMath를 전이 include하므로 실제 dependency root는 아직 살아 있다.

Physics의 별도 SimpleMath 섬은 S5-A~D에서 닫혔다. `PhysicsCommon.h`, `Physx.cpp`,
ragdoll, rigid body, collider와 SceneRuntime 물리 브리지의 공개 필드·인자는
`math::*`이며 PhysX 변환만 `PhysicsMathAdapter.h`에 남는다.

### 1.3 값이 흐르는 주요 경계

| 경계 | 현재 타입/행동 | 이주 시 지켜야 할 계약 |
|---|---|---|
| Transform 정본 | `Vector4` position/rotation/scale, `TransformStore`의 `Matrix`/`Vector4` | 첫 이주에서는 필드 수와 YAML `x/y/z/w` 형상을 바꾸지 않는다 |
| Camera -> Render | `Camera`와 `FrameCameraSnapshot`이 `math::matrix4x4/vector3/quaternion`을 값으로 전달 | Camera 값을 DirectX로 되돌리지 않고 frame snapshot에 그대로 밀봉한다 |
| Render -> GPU | matrix transpose와 packed vector를 상수 버퍼에 복사 | 기존 transpose 위치와 8/12/16/64B 레이아웃을 그대로 고정한다 |
| Reflection/YAML | Vector2/3/4, Color4, Quaternion, Rect, xMatrix를 타입별 emit/read | 키 이름, 순서, 16-float matrix 형상을 바꾸지 않는다 |
| Editor/ImGui | `&v.x`를 `DragFloatN`/`ColorEdit4`에 전달 | 연속 public float 레이아웃을 static_assert로 고정한다 |
| Physics/PhysX | SimpleMath 값과 `PxVec3/PxQuat/PxMat44` 사이 필드 변환, 한 곳은 `memcpy` | `memcpy`를 없애고 필드 단위 변환을 정본으로 만든다 |
| Native -> C# | 별도 `Float2/3/4` Sequential ABI | 함수표 버전과 wire layout은 이번 이주에서 바꾸지 않는다 |
| Collision/culling | `BoundingBox/Sphere/Frustum` 값이 Camera, AI, Foliage, light packing을 통과 | `aabb/sphere/bounding_frustum` producer와 소비자를 한 수직 슬라이스에서 함께 닫는다 |

카메라 값은 이미 `Scene -> FrameCameraSnapshot -> Render` 값 경계로 닫혀 있다.
수학 타입 이주가 Camera 소유권이나 render thread 동기화 계약을 다시 열 이유는 없다.

## 2. Mathematics 적합성

확인한 기준은 위 고정 커밋의 공개 헤더, 테스트와 README/GUIDE다. 라이브러리는
헤더 온리, C++20 이상이고 CreatorEngine은 전체 C++ 프로젝트가 C++23이므로 언어
기준은 맞는다.

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
| `Mathf::Color4` | `math::color` (`r/g/b/a` distinct type) |
| `Mathf::Rect` | `math::rect` (`x/y/width/height`) |
| `BoundingBox` | `math::aabb` (center + extents) |
| `BoundingSphere` | `math::sphere` |
| `BoundingFrustum` | `math::bounding_frustum` |
| `BoundingBox::CreateFromPoints(min,max)` | `math::aabb::from_min_max` |
| `BoundingSphere::CreateFromBoundingBox` | `math::bounding_sphere(aabb)` |
| `BoundingBox::Transform` | `math::transform(aabb, matrix)` 또는 TRS overload |
| `BoundingFrustum::CreateFromMatrix` | `math::bounding_frustum_from_projection_lh/rh` |
| `BoundingFrustum::Transform` | `math::transform(bounding_frustum, ...)` |
| `BoundingFrustum::GetCorners/GetPlanes` | `corners()` / `math::frustum_planes` |
| `Bounding*.Contains/Intersects` | `math::contains/intersects` |
| `Bounding*.Intersects(ray, distance)` | `math::raycast` |
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

4. **Color는 vector4와 다른 타입이다.** 최신판의 `math::color`는 `r/g/b/a`를 가진
   16B standard-layout/trivially-copyable 타입이다. 따라서 Reflection의
   Vector4/Color4 분기와 YAML의 `x/y/z/w` 대 `r/g/b/a` 구분을 유지할 수 있다.
   `Mathf::Color3`는 현재도 `Vector3` alias이고 Material API 두 곳뿐이므로 별도
   `color3`를 Mathematics에 요구하지 않는다.

5. **Rect는 직접 매핑되지만 의미 게이트가 필요하다.** `math::rect`는 기존과 같은
   `x/y/width/height` 16B 형상이며 half-open point containment와 양의 면적 overlap을
   사용한다. 현재 `Mathf::Rect`는 POD라 기존 query 동작과 충돌하지 않지만 UI hit-test
   전환 때 공유 모서리·0/음수 크기를 회귀로 고정한다. `XMUINT4`는 여전히 수학 타입이
   아니며 실제 소비자가 없는 `CreateBoneIndex` helper와 함께 제거하거나, 필요해지면
   GPU DTO/`std::array<uint32_t,4>`로 둔다.

6. **frustum 선행 기능 gap은 닫혔다.** `math::bounding_frustum`은 LH/RH perspective
   projection 생성과 실패 가능한 `try_*`, matrix/TRS transform, corners/planes,
   point/sphere/aabb/frustum contains/intersects, plane classification과 raycast를 제공한다.
   DirectXCollision parity test도 upstream에 있다. projection 생성은 perspective 전용이며
   singular/잘못된 projection을 구분해야 하는 엔진 경로는 반드시 `try_*`를 사용한다.

7. **퇴화 입력 정책을 동작 변경으로 취급해야 한다.** Mathematics는 0-vector
   normalize -> zero, singular inverse -> identity처럼 NaN 전파를 줄이는 정책이다.
   ray가 볼륨 안에서 시작할 때 distance 0을 주는 것도 DirectXCollision 일부와 다르다.
   컴파일 성공을 의미 보존의 증거로 삼지 않는다.

   특히 기본 bounds는 직접 치환하면 안 된다. DirectXCollision의 기본
   `BoundingBox/Sphere`는 원점의 unit volume이지만 `math::aabb{}`는 merge identity인
   empty box이고 `math::sphere{}`는 반지름 0이다. 기존 `{}`가 "미계산", "빈 값",
   "unit editor bounds" 중 무엇이었는지 소비자별로 판정하고 unit이 필요하면 명시 초기화한다.

8. **현재 성능 표의 AVX2 수치를 곧 엔진 수치로 읽으면 안 된다.** CreatorEngine
   프로젝트에는 현재 `/arch:AVX2` 설정이 없다. 이 상태에서는 x64 SSE2 경로가
   기준이다. 이주와 CPU baseline 상향을 묶지 않고, `/arch` 결정은 별도 벤치마크와
   배포 하드웨어 정책으로 판단한다.

9. **0.1 릴리스 게이트와 태그가 아직 없다.** upstream README 기준 기능 구현은
   끝났지만 clang-cl 행렬 곱 처리량과 성능 표 자동화 범위가 release blocker이고,
   remote tag도 없다. 그래서 `master`를 빌드 때 fetch하지 않고 검증한 commit을
   저장소에 고정한다.

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
추가하고 프로젝트 설정 뒤에 평가되는 `Directory.Build.targets`의 모든 C++ 프로젝트
공통 include 경로에
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
static_assert(sizeof(math::color) == 16);
static_assert(sizeof(math::rect) == 16);
static_assert(sizeof(math::sphere) == 16);
static_assert(sizeof(math::aabb) == 24);
static_assert(sizeof(math::bounding_frustum) == 52);
static_assert(std::is_standard_layout_v<math::matrix4x4>);
static_assert(std::is_trivially_copyable_v<math::matrix4x4>);
```

GPU/C# 경계는 이 assert만으로 끝내지 않고 실제 offset/직렬화/업로드 검사를 함께 둔다.

### 3.3 전환용 interop 규칙

공존 기간에는 변환을 한 파일로 숨기되 암시 변환은 만들지 않는다.

- S2에서 카메라 행렬만 먼저 옮기고 기존 DirectXCollision 반환 타입을 잠시 유지한다면
  `ToDirectX(math::matrix4x4)` 같은 함수는 그 소비자만 직접 include하는 좁은
  `MathInterop.DirectX.h`에 둔다. S7을 같은 패치에 수행할 수 있으면 bridge를 만들지 않는다.
- `Core.Minimal.h`나 `Core.Mathf.h`에서 interop 헤더를 전이시키지 않는다.
- PhysX 변환은 `PxVec3{v.x,v.y,v.z}` / 역방향 필드 복사로 둔다.
- `reinterpret_cast`, 상속, 사용자 정의 `operator XMVECTOR()`로 SimpleMath 호환을
  재현하지 않는다.
- 전환 bridge마다 삭제 대상과 마지막 소비자를 주석 또는 게이트로 남긴다.

## 4. 구현 슬라이스

모든 슬라이스는 독립적으로 빌드 가능해야 한다. 한 슬라이스 안에서는 producer,
value boundary, consumer, test를 함께 닫고 다음 슬라이스로 넘어간다.

### S0. dependency + contract probe, 소비자 미배선 — 완료 (2026-08-25)

변경:

- `ThirdParty/Mathematics` 최초 도입 판본을 `04c8bbe30272b3332716cec66cd35dc4d8cb8dbf`에 고정하고
  provenance/license를 기록한다.
- 공통 include 경로만 추가한다.
- `Tools/regression/verify-mathematics-contract.ps1` 또는 동등한 standalone probe로
  크기·layout·행렬 규약·TRS·quaternion order를 컴파일/실행한다.
- 기존 `Core.Mathf.h`와 소비자는 그대로 둔다.

게이트:

- Debug/Release, MSVC C++23 probe 통과.
- `math::transform_point({1,0,0}, compose(...))`, LH view/projection,
  quaternion multiply가 임시 DirectXMath oracle과 허용 오차 안에서 일치.
- `color/rect/aabb/sphere/bounding_frustum` 크기·layout과 AABB/frustum DirectX parity 확인.
- 프로젝트/패키징/런타임 동작 변화 0.

검증 기록:

- clean upstream SHA에서 `include/mathematics` 26개 헤더와 MIT `LICENSE`를 복사하고
  원본과 SHA-256이 모두 같은지 확인했다.
- `verify-mathematics-contract.ps1 -Configuration All`이 MSVC 19.51 x64 Debug/Release
  두 구성에서 compile + run을 통과했다.
- probe가 layout/offset, row-vector `S*R*T`, point transform, quaternion 곱 순서,
  AABB affine transform, LH projection과 frustum field/corner/transform을
  DirectXMath/DirectXCollision oracle과 대조했다.
- MSBuild Debug 전처리 결과에서 공통 Mathematics include 경로가 한 번 반영됨을 확인했다.
- `Core.Mathf.h`, 기존 consumer, `vcpkg.json`, package/deployment 배선은 바꾸지 않았다.
  CreatorEngine library/Editor/Player build와 runtime smoke는 S0에서 실행하지 않았다.

### S1. `Core.Mathf.h` 해체 — 구현/빌드 게이트 완료 (2026-08-25)

기존 `UtilityFrameworkModernizationPlan`의 M0~M4를 이 단계의 선행 정리로 수행한다.

- 소비자 0 Assimp/JSON helper와 죽은 `XM*` 상수를 제거한다.
- `Easing/Tween`을 수학 헤더 밖으로 분리한다. `Rect`와 `Color`는 별도 엔진 타입을
  만들지 않고 S6에서 `math::rect/color`로 직접 옮긴다.
- 헤더 전역 `using namespace DirectX`를 제거한다.
- 수학 함수 중복을 Mathematics 이름/의미와 맞춰 정리한다.
- 직접 필요한 include를 각 소비자가 갖게 한다.

현재 적용:

- 소비자 0 Assimp/JSON helper, 죽은 보간·클램프 wrapper와 `XM*` 상수를 제거했다.
- `Core.Mathf.h`는 682줄에서 106줄로 줄었고, Easing/Tween 399줄은
  `Core.Easing.h`로 이동했다. 실제 Easing 소비 헤더 두 곳은 이 헤더를 직접 include한다.
- Assimp 선언을 노출하는 `AnimationLoader.h`, `Mesh.h`, `SkeletonLoader.h`,
  `ModelLoader.h`와 importer 구현에 직접 Assimp include를 추가했다.
- `xMatrixIdentity`/`xVectorZero`/`xVectorOne` 소비자는 값 표현으로 바꿨고
  `halfPi`/`pi`/`pi2` 리터럴은 `float` 정본으로 고정했다.
- `Core.Mathf.h`의 전역 `using namespace DirectX`를 제거했다. 남는 DirectXMath/
  DirectXCollision 타입·함수·상수는 소비자에서 `DirectX::`로 명시 수식했고,
  raw `XMVECTOR` 산술은 `XMVectorAdd/Scale/Subtract` 호출로 바꿨다. 저장 타입과
  `Mathf::Vector*` 별칭은 이 단계에서 바꾸지 않았다.
- `verify-directx-namespace-hygiene.ps1`가 Engine/Editor 723개 파일에서 전이
  비수식 DirectX 식별자 0건을 확인한다. 명시적 로컬 `using`은 구현/테스트 6개
  파일에만 남아 있으며 `Core.Mathf.h`를 통한 전파는 없다.
- Debug non-unity와 Release unity에서 `Utility_Framework`, `RenderEngine`, `Physics`,
  `SceneRuntime` 네 라이브러리가 모두 빌드됐다. 기존 `Terrain.cpp` C4244 경고는 남는다.
- 현재 엔진 라이브러리를 relink한 Debug `CreatorEditor` 통합 빌드가 통과했고,
  이 과정에서 수정된 `RenderTests`도 빌드됐다. reflection golden도
  77타입·실패 0·diff 0을 통과했다.

남은 항목:

- 전체 솔루션 Release, Player와 UI runtime regression은 아직 실행하지 않았다.
  따라서 S1은 구현/정적/빌드 게이트까지만 완료이며 runtime 확대 검증 완료로
  해석하지 않는다.

게이트:

- Debug non-unity + Release unity 엔진 라이브러리 빌드.
- Debug `CreatorEditor` 및 `RenderTests` 빌드.
- 기존 reflection golden diff 0.
- `Core.Mathf.h`에서 Assimp/JSON/Easing/Tween include와 전역 DirectX using 0.
- namespace hygiene 정적 검사에서 전이 비수식 DirectX 식별자 0.

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
- S7을 아직 수행하지 않으면 `Camera::GetFrustum`, `EnhancedLightPacking` 등 기존
  `BoundingFrustum` 경계만 명시적 matrix 변환으로 임시 격리한다. bridge 생성과
  마지막 소비자를 함께 기록하고 S7에서 삭제한다.
- RenderTests의 camera fixture도 같은 슬라이스에서 바꾼다.

게이트:

- camera snapshot/parity smoke.
- DX12 camera를 쓰는 geometry/lighting/editor self-test.
- Vulkan geometry/grid/gizmo/skybox/wireframe camera test.
- CPU projection 결과와 화면 pixel 기준선 유지.

#### S2 구현 결과 (2026-08-25)

- `FrameCameraSnapshot`의 행렬 4개와 카메라 vector 4개를 각각
  `math::matrix4x4`, `math::vector3`로 교체했다. 스냅샷은 standard-layout 및
  trivially-copyable 계약을 정적으로 고정했다.
- `Camera`의 view/projection/inverse 계산은 Mathematics가 소유한다. 저장 FOV는
  degree이고 projection·삼각함수 경계에서만 radians로 바꾼다.
- 아직 남겨 둔 DirectX `BoundingFrustum`과 렌더 상수 구조는
  `MathematicsInterop`의 명시 bridge에서만 스냅샷 값을 받는다. 암시 변환은 없다.
- RenderEngine, SceneRuntime, Editor와 DX12/Vulkan RenderTests의 카메라 fixture를
  같은 슬라이스에서 바꿨다. fixture의 identity/perspective/orthographic/look-at/inverse와
  FOV 값은 Mathematics/degree 계약을 사용한다.
- Debug non-unity에서 RenderEngine·SceneRuntime·Editor·RenderTests 및 전체
  `CreatorEditor` 링크가 통과했고, Release unity 전체 `CreatorEditor`도 통과했다.
- standalone camera parity 계약은 Debug/Release 모두 통과했다. DX12 카메라 관련
  GPU 검사 17개와 Vulkan 픽셀 대조 14개가 통과했다.
- `dx12.scene`은 시작 씬에 드로우 후보가 0개라 fixture 전제에서 실패했다. 같은
  실제 `CameraComponent -> FrameCameraSnapshot` 연결은 `dx12.gizmoscene`으로 통과했지만,
  메시가 있는 씬에서의 `dx12.scene` 재실행 전에는 이 한 게이트를 통과로 세지 않는다.

따라서 S2의 구조 변경과 카메라/패스 픽셀 회귀는 완료했고, 콘텐츠 의존
`dx12.scene` 한 항목만 별도 미충족 런타임 게이트로 남는다. S3 착수 시 이를 S2 실패로
숨기거나 전체 런타임 검증 완료로 합산하지 않는다.

### S3. Transform + TransformStore + YAML/reflection — 구현 완료, physics runtime gate 보류 (2026-08-25)

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

검증 기록:

- `TransformStore`의 local/world matrix를 `math::matrix4x4`, world
  position/rotation/scale 저장값을 `math::vector4`로 전환했다. slot grow/reset 계약은
  Debug/Release Mathematics contract probe로 통과했다.
- `Transform`의 authored position/rotation/scale은 기존 x/y/z/w field와 YAML key를
  유지하면서 `math::vector4`로 전환했다. 의미 API와 TRS 계산은
  `math::vector3`/`math::quaternion`/`math::matrix4x4` 및 Mathematics 함수만 사용한다.
- reflection golden 77/77, golden diff 0, scene transform 41-object round trip,
  prefab round trip을 통과했다.
- hierarchy deep/wide round trip, DDOL canvas, play round trip을 통과했으며 hierarchy,
  orphan, store mismatch는 모두 0이었다.
- Debug non-unity `SceneRuntime`, `Editor`, `CreatorEditor`와 Release unity
  `CreatorEditor` 빌드를 통과했다.
- Physics 소비 경계는 `SceneRuntime`/`CreatorEditor` 빌드로 컴파일 검증했다. 현재
  전용 physics runtime fixture가 없으므로 physics 동작 smoke 통과로 기록하지 않는다.

### S4. Render payload + mesh/import/animation

변경:

- Material, light, gizmo, UI proxy, pass constant, mesh vertex/bounds를 값 타입부터 옮긴다.
- `XMStore*`/`XMLoad*` 왕복을 packed `math` 값의 명시 필드/행 접근으로 바꾼다.
- GPU constant buffer의 padding/offset을 각 구조체에서 static_assert한다.
- Assimp/fastgltf 입력은 importer 경계에서 한 번만 `math` 값으로 변환한다.
- animation/skeleton의 실제 측정된 register hot loop만 `math::vec_reg` 후보로 두고,
  일반 저장 컨테이너에는 사용하지 않는다.
- `DirectX::Colors::*` 초기화는 `math::color` 상수 또는 명시 RGBA 값으로 바꾼다.

게이트:

- Experiment parity(gltf import, tangent, animation playback) 유지.
- DX12/Vulkan geometry, skinning, shadow, lighting, UI/gizmo 결과 유지.
- shader reflection/constant offset gate 유지.

#### S4 experiment 구간 구현 결과 (2026-08-25)

`Engine/RenderEngine/Experiment/**` 와 `Editor/RenderTests/ExperimentParity/**`
의 값 타입을 **전량 교체**했다. 치환 238건 / 20파일.

| 이전 (experiment 자체 정의) | 이후 | 크기 |
|---|---|---|
| `Float2` | `math::vector2` | 8B → 8B |
| `Float3` | `math::vector3` | 12B → 12B |
| `Float4` | `math::vector4` | 16B → 16B |
| `Float4`(회전 전용) | **`math::quaternion`** | 16B → 16B |
| `Matrix4` | `math::matrix4x4` | 64B → 64B |
| `Bounds` | **`math::aabb`** | 24B → 24B |

`sizeof(experiment::Vertex)` 는 96B 그대로다. 따라서 정점 기술표에서 유도되는
쿠킹 레이아웃 해시도 변하지 않는다 — 레이아웃이 실제로 안 변했으므로 맞는
결과다.

##### 별칭을 두지 않았다

§0 의 "동일 API 를 재포장하지 않는다"를 그대로 따랐다. `using Float3 =
math::vector3` 로 놓아두면 호출부가 어느 규약을 따르는지 흐려진다. 호출부를
전부 정본 이름으로 바꿨다.

##### ★ 기본값 규약이 둘 달랐다 — 조용히 넘어가면 안 되는 종류다

**① `math::matrix4x4` 의 기본은 영행렬이다**(예전 `Matrix4` 는 항등).
그대로 두었으면 변환이 없는 노드가 모든 것을 원점으로 뭉갰을 것이다.
저장 행렬 네 곳(`ModelNode::localTransform`, `Bone::inverseBindMatrix`,
`Skeleton::rootTransform`, `globalInverseTransform`)을 `identity()` 로 명시
초기화했다.

**② `math::aabb` 는 min/max 가 아니라 center/extents 다.** 크기는 24B 로
같고 필드도 `vector3` 둘이라 **바이트만 보면 구분이 안 된다**. 그래서:

- 생성은 반드시 `math::aabb::from_min_max` 를 거친다.
- legacy 브리지는 오히려 단순해졌다 — `DirectX::BoundingBox` 도 center/extents
  이라 min/max 로 폈다가 다시 접던 과정이 사라졌다.
- 쿠킹 포맷은 `kFormatVersion` 을 **1 → 2** 로 올렸다. 구버전 캐시를 그대로
  읽으면 min 을 center 로 조용히 오독하기 때문이다. 버전이 있는 이유가 정확히
  이것이다.
- 검증을 고쳤다. 예전 `IsValid` 는 `min <= max` 를 봤는데, `aabb` 에서 그것은
  "extents 가 음수가 아니다" = `!is_empty()` 와 같다. 다만 **빈 상자를 합법으로
  둔다** — 정점이 없는 메시가 실제로 있고, 그때 bounds 는 "없음"이 맞다. 예전
  `Bounds{}` 는 원점 크기 0 이라 없음과 원점을 구분하지 못했고 merge 에서
  원점을 끌어들였다. 새 규약이 맞다.

##### 회전은 `quaternion` 으로 갈라 넣었다

`RotationKey::quaternion` 과 `ImportedTransform::rotation` 은 `vector4` 가 아니라
`math::quaternion` 이다. 크기는 같지만 기본값이 항등(0,0,0,1)이고, 영 쿼터니언은
회전이 아니라서 합성에 들어가면 결과가 무너진다. 예전엔 주석으로 "쿼터니언
(x, y, z, w)"이라 적어 두었는데 이제 타입이 대신 말한다. 타입이 갈라졌으므로
`IsFinite` 오버로드도 따로 필요해졌다 — 없으면 조용히 안 되는 것이 아니라
컴파일이 막힌다. 그게 맞다.

##### 배선

`ThirdParty\Mathematics\include\` 를 `RenderEngine.vcxproj`(9곳)와
`RenderTests.vcxproj`(2곳)의 `AdditionalIncludeDirectories` 에 넣었다.

##### 검증 완료 (2026-08-25)

- Debug `CreatorEditor` unity 빌드를 통과했다.
- `experiment.cooked`는 합성 223/223과 Gunner 실자산 4,902/4,902를 통과했다.
- `experiment.gltf`(Gunner), `experiment.fbx`(Ani), `experiment.anim`(Gunner),
  `experiment.sampler` 35/35, `experiment.tangent` 17/17,
  `experiment.normal` 12/12를 각각 독립 프로세스로 통과했다.
- §1.5에 기록된 기존 실패 자산 `SU_Mythic.glb`는 녹색 회귀 기준선에 포함하지
  않았다. 이 자산의 AABB 차이는 별도 import 결함으로 유지한다.

#### S4-A. ProxyCommand world transform payload (2026-08-25)

게임 스레드의 `Transform`이 이미 Mathematics 값을 내는데 `ProxyCommand`를 만들 때
SimpleMath로 바꾸고, render proxy에 그대로 복사하던 왕복을 제거했다.

- `MeshUpdate`, `TerrainUpdate`, `FoliageUpdate`, `DecalUpdate`, `SpriteUpdate`의
  `worldMatrix`를 `math::matrix4x4`로 전환했다.
- 해당 payload의 `worldPosition`과 sprite `billboardAxis`를 `math::vector3`로
  전환하고 nested payload member type을 `static_assert`했다.
- 생산자는 Mathematics 값을 그대로 큐에 보존한다. 아직 SimpleMath를 저장하는
  render proxy에 적용할 때만 `MathematicsInterop::ToSimpleMath`를 호출한다.
- `worldBounds`, bone palette와 render proxy 저장소는 후속 S4 슬라이스로 남겼다.
  Color/Rect/BoundingFrustum은 당시 후속 슬라이스로 남겼으며 최종 제외 항목이 아니다.

검증:

- Debug non-unity `SceneRuntime`과 Debug/Release unity `CreatorEditor` 빌드를
  통과했다.
- `dx12.decal`, `dx12.gizmoscene`가 큐 적용 후 통과했다.
- `dx12.scene`는 시작 씬의 드로우 후보가 0이라 기존 fixture 전제에서 실패했다.
  프로세스/어서션 오류는 없었고, 메시가 있는 씬에서 다시 실행해야 한다.
- 위 experiment 7종을 갱신된 실행 파일로 다시 통과했다.

#### S4-B. RenderProxy world transform storage (2026-08-25)

`ProxyCommand`에서 보존한 Mathematics 변환을 render proxy에 적용할 때 다시
SimpleMath로 바꾸던 경계를 한 단계 더 렌더 패스 쪽으로 밀었다.

- `RenderProxy::m_worldMatrix`, `m_worldPosition`을 각각 `math::matrix4x4`,
  `math::vector3`로 전환하고 member type을 `static_assert`했다.
- `SpriteRenderProxy::m_billboardAxis`도 `math::vector3`로 전환했다.
- primitive bridge와 command apply 경로는 Mathematics 값을 그대로 proxy에
  보존한다. 기존 `EnhancedDrawItem`, decal, sprite pass payload를 조립하는 지점에서만
  `MathematicsInterop::ToSimpleMath`를 호출한다.
- `LightRenderProxy::Values::worldPosition`은 현재 authoring/gizmo DTO 경계를 유지하고,
  proxy 저장소에 적용할 때 명시적 필드 복사한다.
- `worldBounds`, bone palette, 기존 pass/draw payload, `FoliageInstance` 저장소는 후속
  S4 슬라이스로 남겼다. Color/Rect/BoundingFrustum도 당시 후속 항목으로 남겼다.

검증:

- Debug non-unity `RenderEngine`, `SceneRuntime` 빌드를 통과했다.
- Debug/Release unity `CreatorEditor` 통합 빌드를 통과했다.
- 갱신된 Debug 실행 파일로 `dx12.decal`, `dx12.gizmoscene`,
  `dx12.shadowquality`를 통과했다.
- 빌드 경고는 기존 Terrain C4244, PhysX PDB LNK4099, Vulkan delay-load LNK4229와
  ScriptCore analyzer/trimming 경고만 확인됐다.

#### S4-C. RenderProxy world AABB storage (2026-08-25)

메시의 컬링용 world bounds를 command payload부터 view draw pool까지
`math::aabb`로 보존하고, 당시 아직 남아 있던 DirectX `BoundingFrustum` 경계에서만
`BoundingBox`로 되돌리도록 변환 위치를 옮겼다.

- `ProxyCommand::MeshUpdate::worldBounds`, `MeshRenderProxy::m_worldBounds`,
  `EnhancedSceneRendererLive`의 `PooledDraw::worldBounds`를 `math::aabb`로 전환했다.
- 이 슬라이스 시점에는 component가 내는 `DirectX::BoundingBox`를 command/proxy
  생성 시 한 번 변환했다. 후속 S4-D에서 component/asset API까지 `math::aabb`로
  올려 이 입구 bridge도 제거했다.
- DirectX frustum culling 호출 직전에만 `MathematicsInterop::ToDirectX`를 사용한다.
  빈 `math::aabb`는 보수적으로 컬링하지 않아 잘못된 discard를 만들지 않는다.
- bridge는 center/extents를 명시적으로 필드 복사하며 Debug/Release contract probe에
  양방향 보존 검사를 추가했다.
- Mesh asset/component bounds API, bone palette, 기존 pass/draw transform payload와
  `FoliageInstance` 저장소와 Color/Rect/BoundingFrustum은 후속 슬라이스로 남겼다.

검증:

- Mathematics contract probe Debug/Release를 통과했다.
- Debug non-unity `RenderEngine`, `SceneRuntime` 빌드를 통과했다.
- Debug/Release unity `CreatorEditor` 통합 빌드를 통과했다.
- 갱신된 Debug 실행 파일로 `dx12.decal`, `dx12.gizmoscene`,
  `dx12.shadowquality`를 통과했다.
- `dx12.scene`는 이전과 동일하게 시작 씬의 드로우 후보가 0이라 fixture 전제에서
  실패했다. 프로세스/어서션/DX12 오류는 없었다.
- 빌드 경고는 기존 Terrain C4244, PhysX PDB LNK4099와 Vulkan delay-load LNK4229만
  확인됐다.

#### S4-D. Mesh asset/component bounds + cache rebuild (2026-08-25)

Mesh 로컬 bounds부터 component world bounds, Editor 선택/AI/Foliage/Experiment
소비자까지 `math::aabb/sphere`로 연결했다. 이 슬라이스에서는 DirectX
`BoundingFrustum`을 후속 항목으로 두고 실제 교차 호출 직전만 `BoundingBox`로 변환했다.

- `Mesh::GetBoundingBox/GetBoundingSphere`와 저장 멤버를
  `math::aabb/math::sphere`로 바꾸고, 절차 생성 메시의 `RecalculateBounds`도
  `math::aabb::from_min_max`와 `math::bounding_sphere`를 사용한다.
- `MeshRenderer::GetBoundingBox`는 world `math::aabb`를 반환한다. proxy command와
  `MeshRenderProxy`는 이를 변환 없이 보존한다.
- AI/Foliage의 affine AABB 변환, SceneView의 world bounds 조회,
  Experiment legacy bridge를 Mathematics 값으로 옮겼다. AI/Foliage/SceneView는
  DirectX frustum/ray 최종 경계에서만 명시 bridge를 사용한다.
- Assimp 임포트의 AABB는 메시마다 `aiMesh::mAABB`에서 독립 생성한다. 이전 구현처럼
  0과 앞선 메시의 min/max를 누적해 뒤쪽 메시 bounds를 부풀리지 않는다.
- 모델 캐시에 8-byte `CEMA` envelope와 포맷 버전 2를 추가했다. 무버전 캐시는
  사용하지 않고 원본을 다시 임포트하며, `.asset`만 있어 재생성할 수 없는 경우는
  명시적으로 실패한다. 잘린 payload도 성공으로 기록하지 않는다.
- Editor CLI `model.cache.build <원본>`은 원본 복사 없이 실제 Assimp → Editor
  writer를 태우고, 생성 캐시를 즉시 다시 열어 mesh 이름/개수/AABB/sphere 왕복을
  비교한다.
- `Tools/regression/rebuild-model-assets.ps1`은 source stem 충돌과 목적 경로를 먼저
  검증하고, 기존 payload를 실행별 폴더에 백업한 뒤 전체 재생성한다. `.asset.meta`는
  GUID 정본이므로 이동하거나 다시 만들지 않으며, 실패하면 확정된 cache 경로만
  제거하고 백업을 복원한다.

일회성 재생성 결과:

- `Dynamic_CPP/Assets`의 원본 14개와 `Assets/Models` 캐시 14개를 전부 CEMA v2로
  재생성했다. 모든 명령이 캐시 재로드 bounds 비교를 통과했다.
- manifest와 SHA-256은
  `Build/model-asset-rebuild/run-20260825-214226/manifest.csv`, 교체 전 payload는
  같은 실행 폴더의 `backup/`에 보존했다.

검증:

- Mathematics contract probe Debug/Release 통과.
- Debug non-unity `RenderEngine`, `SceneRuntime`, `CreatorEditor` 통합 빌드 통과.
- Release unity `CreatorEditor` 통합 빌드 통과.
- asset authoring ownership probe의 CEMA v2 게시, material payload, runtime reload와
  나머지 Editor writer 트랜잭션을 모두 통과했다.
- 갱신된 Debug 실행 파일로 `dx12.gizmoscene`, `dx12.shadowquality`,
  `experiment.gltf`를 통과했다.
- 빌드 경고는 기존 Terrain C4244, PhysX PDB LNK4099와 Vulkan delay-load LNK4229만
  확인됐다.

#### S4-E. Animator bone palette storage + GPU staging (2026-08-25)

실제 렌더 스키닝 팔레트의 생산 지점부터 command/proxy/draw 전달, 세 패스의 GPU
staging까지 `math::matrix4x4`로 연결했다. 애니메이션 pose 계산 hot loop는 아직
DirectXMath를 사용하고, 최종 렌더 저장소에 쓰는 세 지점에서만 명시적으로 변환한다.

- `Animator::m_FinalTransforms[512]`를 `math::matrix4x4`로 전환했다.
  `AnimationJob`의 local/global pose 계산은 `XMMATRIX`를 유지하고 최종 palette write에
  `MathematicsInterop::FromDirectX`를 둔다.
- `ProxyCommand::MeshUpdate::bonePalette`와 `MeshRenderProxy::m_finalTransforms`의 공유
  소유권을 `std::shared_ptr<math::matrix4x4[]>`로 전환했다. command snapshot은
  `std::copy_n`으로 512개를 복사하므로 기존 immutable frame snapshot 수명 규약은
  바뀌지 않는다.
- `EnhancedDrawItem::bonePalette`는 `const math::matrix4x4*`를 받고,
  GBuffer/Shadow/WireFrame의 frame staging은 `std::vector<math::matrix4x4>`를 사용한다.
  shader 규약에 맞춘 전치는 `math::transpose`로 수행한다.
- GPU upload stride는 `sizeof(math::matrix4x4)`로 계산하며, 16-float/64-byte 크기와
  trivially-copyable 계약을 draw 경계에서 `static_assert`했다.
- DX12 skinning/wireframe과 Vulkan wireframe fixture의 palette도 Mathematics 값으로
  바꿔 테스트가 legacy 타입을 우회하지 않게 했다.
- `AnimationController::m_FinalTransforms`는 실제 render palette와 별개의 write-only
  저장소이며 현재 consumer가 없다. 이번 슬라이스에서 억지로 연결하지 않고,
  animation/skeleton 내부 DirectXMath 정리 때 제거 또는 전환할 대상으로 남겼다.

검증:

- Mathematics contract probe Debug/Release 통과.
- Debug non-unity `RenderEngine`, `SceneRuntime`, `CreatorEditor` 통합 빌드 통과.
- Release unity `CreatorEditor` 통합 빌드 통과.
- 갱신된 Debug 실행 파일로 `dx12.skinning`, `dx12.shadowquality`,
  `dx12.wireframe`, `vk.wireframe`를 통과했다.
- `Gunner_F_Mythic.glb`의 `experiment.anim`을 통과해 실제 Animator 출력 경계의
  pose 변화와 legacy reference parity를 확인했다. 프로세스 종료 코드는 0이고
  stderr는 비어 있었다.
- 빌드 경고는 기존 Terrain C4244, PhysX PDB LNK4099와 Vulkan delay-load LNK4229만
  확인됐다.

후속 S4-F에서 `EnhancedDrawItem::worldMatrix`와 이를 소비하는 instance staging을
전환한다. `FoliageInstance`의 derived world cache와 pass별 frame/constant
payload는 각각 별도 asset-schema/layout 게이트를 두고 뒤따른다. Color/Rect/
BoundingFrustum도 후속 슬라이스이며 최종 제외 항목이 아니다.

#### S4-F. EnhancedDrawItem world matrix + instance staging (2026-08-25)

render proxy에 이미 보존된 Mathematics world matrix를 공용 draw DTO부터 실제 GPU
instance upload까지 변환 없이 운반한다. 정찰 중 같은 DTO를 읽는 Forward 패스와
투명 드로우 깊이 정렬도 확인돼 세 패스만 바꾸지 않고 해당 소비자까지 함께 닫았다.

- `EnhancedDrawItem::worldMatrix`를 `math::matrix4x4`로 전환하고 member type을
  `static_assert`했다. `EnhancedSceneRendererLive::BuildDrawPool`은
  `MeshRenderProxy::m_worldMatrix`를 그대로 sealed draw에 복사한다.
- 투명 queue의 back-to-front depth key는 DirectX register 접근 대신
  `worldMatrix.translation()`과 `math::dot`으로 계산한다. 동일 depth의 mesh pointer
  tie-break 규약은 유지한다.
- GBuffer `InstanceData`, Shadow `ShadowInstance`, WireFrame `InstanceData`, Forward
  `ShadeInstance`의 world field를 `math::matrix4x4`로 전환하고 `math::transpose`로
  shader storage를 만든다.
- raw upload 구조체의 기존 HLSL stride는 각각 GBuffer/Forward 96B,
  Shadow/WireFrame 80B로 유지했으며 trivially-copyable 계약을 assert했다.
- Shadow caster culling은 object origin을 `translation()`, 비균등 scale을
  `length(right/up/forward)`의 최댓값으로 구한다. `CastsInto`의 나머지 pass vector
  DTO는 후속 값 타입 슬라이스이므로 그 호출 한 곳에서만 SimpleMath로 변환한다.
- DX12/Vulkan geometry, forward, shadow, wireframe과 skinning fixture의 draw world
  값을 Mathematics identity/translation으로 바꿔 legacy assignment 우회를 제거했다.

검증:

- Mathematics contract probe Debug/Release에서 translation accessor, basis scale,
  GPU transpose의 DirectXMath parity를 통과했다.
- Debug non-unity `RenderEngine`, `CreatorEditor` 통합 빌드와 Release unity
  `CreatorEditor` 통합 빌드를 통과했다.
- `dx12.gbuffer`, `dx12.forwardshade`, `dx12.shadowquality`, `dx12.wireframe`,
  `dx12.skinning`, `dx12.gizmoscene`를 통과했고 `dx12.forwardscale`을 완료했다.
- `vk.gbuffer`, `vk.forward`, `vk.shadow`, `vk.wireframe`를 통과했다.
- 런타임 프로세스 종료 코드는 0이고 stderr는 비어 있었다. 구형 draw world 전치와
  register row 접근은 0건이며 `git diff --check`도 통과했다.
- 빌드 경고는 기존 ScriptCore analyzer/trimming, PhysX PDB LNK4099와 Vulkan
  delay-load LNK4229만 확인됐다.

다음 S4 수직 슬라이스는 scene sealing에 남은 `EnhancedDecalPass::Item`과
`EnhancedSpritePass::Item`/`PooledSprite` world matrix다. 그 뒤 `FoliageInstance`의
derived world cache를 persisted input schema round trip과 함께 분리해 진행한다.

#### S4-G. Decal/Sprite world payload + billboard assembly (2026-08-25)

scene proxy에 이미 있는 Mathematics transform을 decal/sprite frame payload와 GPU
structured buffer까지 그대로 운반한다. SpriteRenderer billboard와 3D Canvas의 quad
조립도 packed `math::vector3`/`math::matrix4x4` 계산으로 닫았다.

- `EnhancedDecalPass::Item::worldMatrix`와 `EnhancedSpritePass::Item::world`를
  `math::matrix4x4`로 전환했다. `BuildDrawPool`은 Decal/Sprite proxy의 world matrix와
  billboard axis를 중간 SimpleMath 복사 없이 frame pool에 밀봉한다.
- `PooledSprite`, `CanvasPlane`, `MakeSpriteMatrix`와 None/Spherical/Cylindrical billboard
  축 계산을 Mathematics로 전환했다. ScreenSpaceCamera는 camera snapshot 값을 직접
  사용하고, WorldSpace Canvas는 아직 UI schema가 소유한 `canvasWorld`에서
  `FromSimpleMath`를 한 번 호출하는 명시 경계만 남긴다.
- Decal inverse-world와 두 패스의 GPU world transpose를 각각
  `math::inverse`/`math::transpose`로 만든다. HLSL stride는 Decal 144B, Sprite 96B로
  유지하고 raw upload 구조체의 크기와 trivially-copyable 계약을 assert했다.
- DX12/Vulkan decal fixture의 world matrix를 Mathematics scaling/translation/identity로
  바꿔 public item에 legacy assignment가 다시 들어오지 않게 했다.
- 이번 범위는 transform payload만이다. Sprite의 `uv`/`color`, UI의 Rect/Color,
  Decal/Sprite frame constant matrix와 `BoundingFrustum`은 후속 슬라이스로 남겼다.

검증:

- Mathematics contract probe Debug/Release에서 transposed inverse-world의
  DirectXMath parity를 포함해 통과했다.
- Debug non-unity `RenderEngine`, `CreatorEditor` 통합 빌드와 Release unity
  `CreatorEditor` 통합 빌드를 통과했다. Release 첫 증분 빌드는 누락된
  `mikktspace_mikktspace.obj` LNK1181이 있었고, `/m:1` RenderEngine 재시도로 객체를
  복구한 뒤 통합 링크가 통과했다.
- 갱신된 Debug 실행 파일로 `dx12.decal`, `vk.decal`, `dx12.ui`, `vk.ui`,
  `dx12.gizmoscene`를 통과했으며 프로세스 종료 코드는 0, stderr는 비어 있었다.
  단, 현재 저장소에는 `EnhancedSpritePass` 전용 픽셀 fixture가 없으므로 UI/gizmo
  canary를 WorldSprite 픽셀 parity의 직접 증거로 간주하지 않는다.
- Decal/Sprite item 및 GPU instance의 legacy world matrix, legacy GPU transpose,
  scene sprite register-row 접근은 0건이고 `git diff --check`를 통과했다.

다음 S4 수직 슬라이스는 `FoliageInstance::m_worldMatrix`다. 정찰 결과 이 행렬 자체는
reflection/YAML 필드가 아니라 position/rotation/scale에서 만드는 파생 캐시다. 따라서
기존 4필드 asset schema 유지, load 후 world 재생성과 foliage placement/culling을 같은
게이트로 묶는다. 그 뒤 pass별 frame/constant matrix를 기능 단위로 정리한다.

#### S4-H. Foliage derived world matrix + asset schema gate (2026-08-25)

`FoliageInstance::m_worldMatrix`를 `math::matrix4x4`로 전환했다. 이 값은 디스크 정본이
아니며, reflection에 포함된 position/rotation/scale/type 네 필드로부터 런타임에 다시
만드는 파생 캐시라는 실제 계약을 코드와 회귀 검사에 고정했다.

- `FoliageInstance::RebuildWorldMatrix()`가 기존 SimpleMath 순서
  `S*Rx*Ry*Rz*T`를 Mathematics scaling/axis rotation/translation으로 재현한다.
  member type은 `static_assert`로 고정했다.
- `AddFoliageInstance`가 복사본의 world를 즉시 밀봉하고, culling update도 같은 함수로
  재계산한다. deserialization 뒤 별도 SimpleMath 계산과 const-cast write는 제거했다.
- world AABB 계산은 `math::transform(mesh bounds, foliage world)`를 직접 사용한다.
  이 슬라이스에서는 `DirectX::BoundingFrustum`을 후속 항목으로 두고 최종
  intersection 한 곳의 명시 bridge를 유지했다.
- `ProxyCommand::FoliageUpdate`와 `FoliageRenderProxy`는 `FoliageInstance` 벡터를 값으로
  복사하므로 새 world 타입을 추가 변환 없이 운반한다. 다만 현재 Enhanced renderer에는
  `FoliageRenderProxy`의 실제 draw consumer가 없으며 proxy create/update와 type별 map
  재구축에서 경로가 끝난다. 따라서 이 슬라이스는 foliage render pixel parity를
  달성했다고 기록하지 않는다.
- `foliage.authoring.probe`가 더 이상 빈 자산만 쓰지 않고 실제 인스턴스 하나를
  `Meta::Serialize`로 게시한다. 게시된 YAML이 정확히 4개 persisted field만 갖는지,
  `m_isCulled`/`m_worldMatrix`가 없는지, reload 값과 Mathematics world 재생성이 같은지
  검증한다. ownership probe도 이 결과와 실제 payload key를 함께 검사한다.

검증:

- Mathematics contract probe Debug/Release에서 foliage `S*Rx*Ry*Rz*T`의
  DirectXMath parity를 통과했다.
- Debug non-unity `RenderEngine`, `SceneRuntime`, `CreatorEditor` 통합 빌드와 Release
  unity `CreatorEditor` 통합 빌드를 통과했다.
- Debug Editor로 asset-authoring ownership 전체 probe를 통과했다. Foliage는
  `fields=4-runtime-absent`, persisted value round trip, derived world rebuild를 모두
  통과했고 임시 asset/meta는 정리됐다.
- 저장소에는 고정된 실제 `.foliage` 샘플 자산이 없었다. 따라서 동일한 기존 4필드
  형상으로 만든 런타임 probe asset의 save/load는 증명했지만, shipped asset fixture
  load라고 주장하지 않는다.
- Foliage world의 legacy storage/construction/bridge와 runtime field reflection은 0건이며
  `git diff --check`를 통과했다. 빌드 경고는 기존 Terrain C4244, ScriptCore analyzer/
  trimming, PhysX PDB LNK4099와 Vulkan delay-load LNK4229만 확인됐다.

다음 S4 수직 슬라이스는 Decal/Sprite의 pass frame constant matrix다. 두 패스의 item과
instance payload는 이미 Mathematics이므로 camera snapshot부터 constant upload 직전까지
남은 inverse-view/projection/view-projection 저장과 transpose를 기능 단위로 닫는다.

#### S4-I. Decal/Sprite frame constant matrices (2026-08-26)

`FrameCameraSnapshot`에서 Decal/Sprite GPU 카메라 상수까지 남아 있던 SimpleMath 저장과
DirectXMath 전치를 `math::matrix4x4` 경로로 전환했다. 두 패스 모두 프레임 준비 시점에
카메라 값을 밀봉하고, Record에서는 그 저장소를 GPU 규약에 맞게 전치만 한다.

- Decal은 `inverseView`, `inverseProjection`, `viewProjection` 세 값을 모두
  `PrepareFrame`에서 저장한다. 이전처럼 Record가 `context.camera`를 다시 읽지 않으며,
  카메라가 없는 프레임에는 세 값을 identity로 다시 설정해 직전 프레임 값이 남지 않는다.
- `DecalFrameConstants`의 세 행렬을 Mathematics로 바꾸고 HLSL `DecalFrame`과 같은
  208바이트 및 trivially-copyable 계약을 `static_assert`로 고정했다. 행렬은 업로드
  직전에 `math::transpose`한다.
- Sprite도 `m_viewProjection`을 Mathematics로 밀봉하고 64바이트 `SpriteCamera`
  업로드 직전에만 전치한다. S4-G에서 전환한 item/instance world 경로와 이어져
  WorldSprite 패스의 모든 matrix payload에서 SimpleMath/DirectXMath가 빠졌다.
- `Rect`, `Color`, `BoundingFrustum`은 후속 슬라이스로 남겼다. Sprite의 UV와 color
  payload도 이번 matrix 슬라이스의 범위 밖이라 그대로다.

검증:

- Mathematics contract probe Debug/Release에서 inverse-view/projection 및
  view-projection GPU 전치 값의 DirectXMath parity를 통과했다.
- Debug/Release non-unity selected compile로 `EnhancedDecalPass.cpp`,
  `EnhancedSpritePass.cpp`, DX12 `EnhancedDecalTest.cpp`를 각각 통과했다.
- 두 패스 파일의 `Mathf::Matrix`, `XMMatrixTranspose`, `XMMatrixIdentity`,
  `MathematicsInterop::ToDirectX` 잔존은 0건이며 대상 파일 `git diff --check`를 통과했다.
- 전체 Debug non-unity `RenderEngine`/`CreatorEditor` 통합 빌드와 Release unity
  `CreatorEditor` 통합 빌드를 통과했다. Release 최초 링크의 일시적 LNK1104는 재시도에서
  사라졌고, 기존 Vulkan delay-load LNK4229와 PhysX PDB LNK4099만 확인됐다.
- 새 Debug Editor에서 `dx12.decal`, `vk.decal`을 연속 실행해 exit 0을 확인했다. DX12는
  기대 픽셀 2048/변경 픽셀 2048, 상자 밖 유출 0, 하늘 오염 0과 배칭 검증까지 통과했다.
- 저장소에는 WorldSprite 전용 runtime fixture가 없다. Sprite는 layout/contract/compile
  증거까지만 확보했으며, 이후 fixture가 생길 때 직접 draw 회귀를 추가한다.

#### S4-J. GBuffer frame matrix + skinning fallback (2026-08-26)

GBuffer의 frame view-projection 저장과 빈 bone palette fallback까지
`math::matrix4x4`로 연결했다. S4-E/F에서 이미 Mathematics로 바꾼 draw world와 bone
palette를 포함해 GBuffer의 모든 matrix payload가 같은 저장·transpose 규약을 사용한다.

- `m_frameViewProjection`을 `math::matrix4x4`로 바꾸고 `PrepareFrame`에서 camera snapshot의
  `view * projection`을 그대로 밀봉한다. camera가 없으면 매 프레임 Mathematics
  identity를 기록해 직전 프레임 값이 남지 않는다.
- frame constant는 업로드 직전에만 `math::transpose`하고 정확히 64바이트를 올린다.
- bone palette upload의 stride와 byte count는 `sizeof(math::matrix4x4)`를 사용한다.
  palette가 비어도 t5 root SRV가 유효하도록 Mathematics identity 한 개를 올린다.
- `EnhancedGBufferPass`의 matrix 저장·연산에서 `Mathf::Matrix/xMatrix`, `DirectX::*`,
  `XM*`, `MathematicsInterop`은 0건이다. 남은 `Mathf::Color4` 한 건은 S6-A 대상이다.

검증:

- Mathematics contract probe Debug/Release에서 view-projection GPU transpose parity와
  빈 palette identity 계약을 통과했다.
- `EnhancedGBufferPass.cpp` Debug/Release non-unity selected compile을 통과했다.
- Debug non-unity와 Release unity `CreatorEditor` 통합 빌드를 통과했다. 기존 Terrain
  C4244, ScriptCore analyzer/trimming, Vulkan delay-load LNK4229와 PDB 관련
  LNK4099/LNK4020 경고는 남아 있다.
- 새 Debug Editor에서 `dx12.gbuffer`, `dx12.skinning`, `vk.gbuffer`를 연속 실행했다.
  DX12 GBuffer MRT 5개, skinning bone 이동/비스킨드 불변/shadow caster와 Vulkan
  MRT·depth·coverage parity가 모두 통과했고 Vulkan validation은 0건이었다.
- 대상 파일 `git diff --check`를 통과했다.

#### S4-K1. Camera storage/API + editor/runtime consumers (2026-08-26)

`Camera`의 저장 값과 공개 view/projection/screen-ray API를 Mathematics 정본으로
올리고, Editor rig·CameraComponent·SceneView의 직접 소비자를 같은 슬라이스에서
연결했다.

- `Camera::rotate`는 `math::quaternion`, eye/forward/right/up은 `math::vector3`로
  바꿨다. 같은 회전을 중복 보존하던 `m_rotation`과 forward에서 매번 재구성할 수 있던
  `m_lookAt`은 제거했다.
- view/projection/inverse API는 `math::matrix4x4`를 직접 반환한다. screen-to-world와
  raycast도 `math::vector2/vector3` 및 행벡터 `clip * inverse(view * projection)` 규약을
  사용한다.
- `EditorCameraRig`의 yaw/pitch quaternion 합성, basis 회전, 이동과 snapshot 적용을
  Mathematics로 바꿨다. `CameraComponent::ResolveCamera`도 Transform의 Mathematics
  position/quaternion을 변환 없이 사용한다.
- SceneView는 camera matrix의 연속 16-float 저장소를 ImGuizmo에 직접 넘기고,
  view cube 결과를 `math::inverse/decompose`로 camera pose에 되돌린다. 화면 ray와
  plane/terrain 교차 전처리도 `math::vector3`로 바꿨다.
- `Camera::GetFrustum`, CameraComponent/LightComponent의 editor bounds와 SceneView의
  최종 `DirectX::BoundingBox::Intersects`는 S7-A collision 수직 슬라이스가 닫을
  경계다. 이번 Camera 전환의 제외 항목이 아니며, `MathematicsInterop`은 Camera에서
  이 frustum 생성 경계에만 남아 있다.

검증:

- 대상 Camera/EditorCameraRig/CameraComponent 파일의 `Mathf::*`, raw
  `XMVECTOR/XMMATRIX/XMFLOAT*`, `XMVector*`/`XMMatrix*`/`XMQuaternion*`, 제거한
  `m_lookAt/m_rotation` 잔존은 모두 0건이다.
- Mathematics contract probe Debug/Release에서 clip-to-world 역투영과 Editor rig의
  yaw→pitch basis가 DirectXMath oracle과 일치했다.
- Debug non-unity와 Release unity `CreatorEditor` 통합 빌드를 통과했다. 기존 Terrain
  C4244, Vulkan delay-load LNK4229, PhysX PDB LNK4099와 Release PDB LNK4020 경고는
  남아 있다.
- reflection golden은 77/77 직렬화, 실패 0, diff 0으로 통과해 `Camera::rotate`의
  YAML 형상이 유지됨을 확인했다.
- 새 Debug Editor에서 `dx12.gizmoscene`, `dx12.gbuffer`, `vk.gbuffer`를 연속 실행했다.
  카메라/라이트 아이콘 2개, DX12 GBuffer 5 MRT와 Vulkan coverage/depth parity가
  통과했고 Vulkan validation과 세션 오류는 0건이었다.
- 현재 tracked native source 기준선은 `Mathf::*` 803건/124파일, raw XM 저장 타입
  348건/35파일, XM 함수 362건/54파일이다.

S4-K2, S4-L1, S4-L2a, S4-L2b1과 S4-L2b2는 아래 기록대로 완료했다.
다음 정본 순서는 S5 Physics 수직 이주다.

#### S4-K2. Render light/shadow/fog/gizmo values + dead scene light DTO removal (2026-08-26)

실제 renderer가 읽지 않던 DX11 시대 `Light`/`ShadowMapConstant` DTO를 값 타입만
바꾸어 보존하지 않고 제거한 뒤, 살아 있는 light proxy부터 GPU constant upload까지를
Mathematics로 연결했다.

- `LightProperty.h`에는 실제로 쓰는 `LightType`/`LightStatus`만 남겼다. 소비자 0건인
  `Light`, `ShadowMapConstant`, `ShadowMapRenderDesc`와 view/projection helper를 삭제했고,
  `RenderScene::g_shadowMapDesc`의 쓰기 전용 초기화도 제거했다.
- Scene의 옛 `vector<Light>`는 renderer 데이터가 아니라 직렬화된 light index를 위한
  슬롯 장부로만 쓰이고 있었다. 이를 `vector<uint8_t> m_lightSlots`와
  `AddLight/EnsureLightSlot/RemoveLight/DestroyLight`로 명시했다. `LightComponent`의
  쓰기 전용 `ApplyLightData`와 중복 position 저장도 제거했다.
- `LightRenderProxy`, `EnhancedLight`, light packing, Forward/Deferred/Shadow/Fog의
  frame·draw·cascade·cloud constant를 `math::vector3/vector4/matrix4x4`로 바꿨다.
  shadow cascade는 Mathematics frustum slope/corners와 `look_at_lh`,
  `orthographic_off_center_lh`, `dot/distance/length`만 사용한다.
- `EnhancedLight` 64바이트, deferred 4432바이트, shadow/fog/forward constant와 각
  field offset을 `static_assert`로 고정했다. 업로드 전 transpose 규칙은 유지했다.
- gizmo scene value, line vertex/collector, icon/line pass constant와 DX12/Vulkan
  projection helper를 Mathematics로 바꿨다. line vertex는 28바이트, icon instance는
  16바이트, icon/line frame constant는 80바이트 계약을 고정했다.
- `Mathf::Color4`는 S6-A의 reflection/Inspector/GPU RGBA 수직 슬라이스로 유지했다.
  light packing의 `BoundingFrustum/BoundingSphere`, camera frustum gizmo와 corner용
  `XMFLOAT3`는 S7-A 경계다. 이번 슬라이스의 제외가 아니라 다음 정본 순서에 남아 있다.
- `EnhancedRenderPass.h`의 `MathematicsInterop.h` 전이 include는 아직 SSAO/SSGI/SSR/
  SkyBox 소비자가 사용한다. 이 공통 include 제거는 모든 잔존 bridge를 닫는 S7-B에서 한다.

검증:

- Mathematics contract probe Debug/Release가 layout, convention, inverse/transpose와
  DirectX parity를 통과했다.
- `RenderEngine`, `SceneRuntime`, `RenderTests` Debug 빌드와 전체 `CreatorEditor` Debug
  non-unity 및 Release unity 빌드/링크를 통과했다. 기존 Terrain C4244, Vulkan
  delay-load LNK4229, PhysX PDB LNK4099와 Release PDB LNK4020 경고는 남아 있다.
- reflection golden은 77/77 직렬화, 실패 0, diff 0을 통과했다. light-slot restore는
  라이트 3개 씬 저장·재로드·재생, 계층 불일치 0, 종료 코드 0을 통과했다.
- DX12는 `forward`, `forwardshade`, `forwardscale`, `shadowquality`, `gizmoline`,
  `gizmoicon`, `gizmoscene`, `fog` 8개가 통과했다. Vulkan은 `gizmoicon`, `gizmoline`,
  `shadow`, `forward`, `deferred`, `fog` 6개가 픽셀/depth/3D scatter parity와 validation
  0건으로 통과했다. 두 실행 모두 clean shutdown과 pending RHI work 0을 확인했다.
- 대상 파일의 `Mathf::Vector/Matrix`, `DirectX::SimpleMath`, `XMVector*`/`XMMatrix*`
  계산 잔존은 0건이다. tracked native source 기준선은 `Mathf::*` 593건/106파일,
  저장 별칭 562건/101파일, raw XM 저장 336건/35파일, XM 함수 281건/41파일이다.
- 전체 working tree `git diff --check`를 통과했다.

#### S4-L1. Animation/skeleton/socket storage + evaluation (2026-08-26)

애니메이션 키에서 스켈레톤·Animator palette·socket으로 이어지는 저장과 평가 경로를
Mathematics 정본으로 연결했다. import/model-node/vertex의 남은 DirectX 표면은 S4-L2로
분리해 이 슬라이스의 cache/pose 회귀 범위를 고정했다.

- `PositionKey`, `RotationKey`, `ScaleKey`를 각각 `math::vector4`, `math::quaternion`,
  `math::vector3`로 바꾸고 Assimp loader가 이 값을 직접 생성하도록 했다. position의
  `w=1` 규약과 각 16/16/12바이트 payload 크기는 유지한다.
- `Skeleton`의 root/global-inverse와 bone offset, `Animator`/`AnimationController`의
  palette, `Socket`의 offset/bone matrix를 `math::matrix4x4`로 바꿨다. interpolation,
  slerp, decompose/compose, hierarchy 누적, final palette와 socket world 합성도 변환
  왕복 없이 Mathematics 연산을 직접 사용한다.
- `math::matrix4x4{}`가 영행렬인 점을 반영해 skeleton root/global-inverse와 bone
  offset의 기존 zero-default 직렬화 형상을 보존했다. `Socket::m_offset`만 기존 계약대로
  명시 identity다. decompose 실패 시에는 초기화되지 않은 출력 대신 현재 pose를 유지한다.
- 읽기 소비자가 없던 `Bone` local/global transform, `Skeleton::InitialMatrix[512]`,
  `Animator::blendtransform`, controller final palette를 제거했다. 저장 소유권은 local
  pose와 Animator final palette 두 단계로 정리됐다.
- CEMA v2 skeleton/animation payload는 `matrix4x4` 64바이트와 key payload 크기가 기존과
  같아 raw read/write의 byte layout이 유지된다. 따라서 format version을 올리지 않았고,
  2026-08-25 재생성한 `Build/model-asset-rebuild/run-20260825-214226` manifest와
  `Gunner_F_Mythic.asset` v2 cache를 그대로 회귀 입력으로 사용했다.
- animation/skeleton/socket의 저장·평가 경로에는 `Mathf::*`, `DirectX::*`, raw XM 타입/
  함수와 `MathematicsInterop`이 0건이다. import oracle과 mesh/model DTO의 잔존은
  S4-L2 범위다.

검증:

- `RenderEngine`, `SceneRuntime`, `RenderTests` Debug 빌드와 전체 `CreatorEditor` Debug
  non-unity full rebuild, Release unity 빌드/링크를 통과했다. 기존 Terrain C4244,
  Vulkan delay-load LNK4229, PhysX PDB LNK4099와 Release PDB LNK4020 경고는 남아 있다.
- Mathematics contract Debug/Release가 layout, convention과 DirectX parity를 통과했다.
  reflection golden은 첫 실행에서 identity로 잘못 바꾼 skeleton 기본값을 검출했고,
  zero-default 복원 뒤 77/77 직렬화, 실패 0, diff 0으로 통과했다.
- 새 Debug Editor와 실제 `Gunner_F_Mythic.glb`/v2 cache에서 `experiment.model`,
  `experiment.anim`, `experiment.import`, `experiment.sampler`를 통과했다. animation은
  10 clips × 61 channels 전부 움직임을 확인했고 legacy reference와 최대 오차는
  모든 clip에서 `0.000000`, 종료 코드 0과 stderr 0이었다.
- tracked native source 기준선은 `Mathf::*` 579건/102파일, 저장 별칭 548건/97파일,
  `DirectX::SimpleMath::*` 414건/43파일, raw XM 저장 245건/25파일, XM 함수
  222건/31파일이다. bounds 47건/22파일, Colors 1건/1파일, DirectX 수학 헤더
  include 22건/17파일은 이번 슬라이스에서 변하지 않았다.

#### S4-L2a. Material typed value boundary + flow DTO (2026-08-26)

Material의 디스크 정본인 `MaterialPropertyValue::m_numericValue`와 shader reflection byte
layout은 그대로 두고, 그 값을 읽고 쓰는 typed C++ API와 flow reflection DTO를
Mathematics로 올렸다. `MaterialInfomation::m_baseColor`는 S6-A의 `math::color` 범위로
유지해 Color 전환을 이 슬라이스에 섞지 않았다.

- `SetBaseColor`의 3채널 입력, wind/UV setter, Float2/3/4와 Float4x4 typed setter/getter를
  각각 `math::vector2/3/4`와 `math::matrix4x4`로 바꿨다. 저장 정본은 여전히 float 배열이고
  GPU constant buffer에는 기존 8/12/16/64바이트 payload를 그대로 복사한다.
- `MaterialFlowInformation`의 wind, UV와 padding을 Mathematics 값으로 바꿨다. 구조체는
  32바이트, field offset은 0/16/24로 고정해 기존 reflection/YAML과 cbuffer 형상을
  보존했다.
- `MaterialInfomation::m_baseColor`를 typed Float4 API로 보낼 때만 RGBA 네 필드를
  `math::vector4`로 명시 복사한다. SimpleMath의 암시 변환을 새 API에 남기지 않았다.
- Inspector typed draw가 `math::vector2/3`를 실제 `MemberT`로 편집하도록 확장했다.
  첫 비유니티 빌드가 이 임시값이 `Mathf::Vector2/3`로 고정된 숨은 결합을 검출했고,
  타입을 일반화한 뒤 독립 번역 단위에서 통과했다.
- Material API/implementation, flow DTO와 shader reflection 회귀 소비자의 `Mathf`/raw XM/
  `MathematicsInterop` 잔존은 0건이다. `MaterialInfomation::m_baseColor`의 `Mathf::Color4`는
  의도대로 S6-A에 남아 있다.

검증:

- `RenderEngine` Debug non-unity와 전체 `CreatorEditor` Debug non-unity 및 Release unity
  빌드/링크를 통과했다. 기존 Terrain C4244, Vulkan delay-load LNK4229, PhysX PDB
  LNK4099와 Release PDB LNK4020 경고는 남아 있다.
- Mathematics contract Debug/Release가 vector/matrix layout, convention과 DirectX parity를
  통과했다. reflection golden은 77/77 직렬화, 실패 0, diff 0을 통과했다.
- `dx12.selftest`가 Material schema 기본값, typed Float4 setter/getter, DataSystem YAML 왕복,
  DXIL/SPIR-V reflection을 실행해 통과했고 stderr는 0바이트였다.
- tracked native source 기준선은 `Mathf::*` 542건/98파일, 저장 별칭 511건/93파일이다.
  `DirectX::SimpleMath::*` 414건/43파일, raw XM 저장 245건/25파일, XM 함수 222건/31파일,
  bounds 47건/22파일, Colors 1건/1파일, DirectX 수학 헤더 include 22건/17파일은 변하지
  않았다.

#### S4-L2b1. Mesh/import vertex + model-node ABI (2026-08-26)

`Mesh`의 CPU/GPU/CEMA vertex와 model-node transform을 같은 수직 경계에서 Mathematics로
옮겼다. raw dump 포맷을 타입 이름에 기대지 않고 실제 크기·offset과 재로드 결과로
검증했으며, Scene hierarchy와 실제 모델/애니메이션 경로까지 함께 닫았다.

- `Vertex`의 position/normal/UV/tangent/bitangent/bone lane을
  `math::vector2/3/4`로 바꾸고 96바이트, align 4와 offset
  0/12/24/32/40/52/64/80을 `static_assert`로 고정했다. `UIvertex`도
  `math::vector3/vector2` 20바이트와 offset 0/12를 고정했다.
- `ModelNode::m_transform`은 `math::matrix4x4` 64바이트 identity로 바꿨다. Assimp 행렬은
  기존 DirectX transpose와 같은 숫자 배치를 명시 변환하고, Scene hierarchy는 interop
  왕복 없이 이 행렬을 직접 소비한다. 사용되지 않던 `ModelLoader::m_transform`은 제거했다.
- primitive/terrain/test vertex producer, mesh bounds 재계산, optimizer와 bone lane writer를
  Mathematics 값/연산으로 연결했다. optimizer의 기존 Gram-Schmidt 식은 동작 변경을
  섞지 않기 위해 기존 literal 연산 순서를 그대로 보존했다.
- upstream `vector2/3/4/quaternion::operator[]`의 named-member 포인터 산술은 표준상 안전한
  배열 접근이 아니므로 이 슬라이스의 bone lane writer는 x/y/z/w switch를 사용한다.
  vendored HEAD를 임의 수정하지 않았으며, 동적 subscript를 새로 쓰기 전 upstream 수정이 필요하다.
- CEMA v2의 node matrix와 vertex block은 크기와 바이트 배치가 같으므로 format version을
  올리지 않았다. `model.cache.build`는 node metadata/transform, mesh metadata,
  vertex raw block, index와 bounds를 import 직후와 cache 재로드 뒤 대조한다. source가
  기존 최신 cache로 해석된 경우도 명시적으로 실패시켜 cache 대 cache의 거짓 PASS를 막는다.
- `Tools/regression/rebuild-model-assets.ps1`로 14개 source, 28,374,634바이트를
  `Build/model-asset-rebuild-s4-l2b1/run-20260826-132811`에 처음 재생성·검증했다. 11개 cache는
  백업과 SHA가 같고, `Ani_Mon_3_die`, `Gunner_F_Mythic`, `SU_Mythic` 3개는 skeleton
  global-inverse의 수치상 동일한 `+0.0/-0.0` 부호 비트 6곳만 달랐다. node/vertex/index와
  파일 크기 차이는 없다. source/cache 타입 guard 보강 뒤
  `Build/model-asset-rebuild-s4-l2b1-final/run-20260826-134651`에서 다시 14개를 통과했고,
  직전 Mathematics cache와는 14/14 SHA가 같아 재생성도 결정적이다.

검증:

- `RenderEngine`, `SceneRuntime` Debug non-unity와 전체 `CreatorEditor` Debug non-unity 및
  Release unity 빌드/링크를 통과했다. 기존 Terrain C4244, Vulkan delay-load LNK4229,
  PhysX PDB LNK4099와 Release PDB LNK4020 경고는 남아 있다.
- Mathematics contract Debug/Release와 reflection golden 77/77, 실패 0, diff 0을 통과했다.
  `dx12.selftest`도 종료 코드 0, stderr 0바이트로 통과했다.
- 새 Debug Editor에서 실제 `Gunner_F_Mythic.glb`로 `experiment.model`,
  `experiment.import`, `experiment.anim`, `experiment.cacheopt`, `experiment.sampler`,
  `vk.gbuffer`를 연속 실행했다. animation
  clip 10개와 sampler 단정 35건이 모두 통과했고 종료 코드 0, stderr 0바이트였다.
  import bridge는 Mathematics `decompose` 성공 뒤 compose 왕복 오차를 별도로 검사해 shear
  손실과 비분해 near-zero scale을 구분하며, 두 경우의 synthetic contract도 함께 실행한다.
  cacheopt는 정상 quad LOD와 degenerate UV 삼각형을 legacy `MeshOptimizer`에 넣어 96바이트
  stride, index 보존과 normal/tangent/bitangent finite를 확인한다. `vk.gbuffer`는 공용
  `Vertex` upload와 indexed draw에서 DX12/Vulkan 5 MRT·depth 픽셀 parity, Vulkan
  validation 0건을 확인했다.
- 최신 cache가 있는 상태에서 `model.cache.build`를 직접 호출하면
  `source-resolved-to-cache`로 거부되고 stderr 0바이트였다. 따라서 재생성 스크립트가
  cache를 백업하지 못한 경우 cache 대 cache 비교로 성공할 수 없다.
- tracked native source 기준선은 `Mathf::*` 463건/90파일, 저장 별칭 435건/85파일,
  `DirectX::SimpleMath::*` 414건/43파일, raw XM 저장 143건/18파일, XM 함수
  207건/26파일이다. bounds 47건/22파일과 Colors 1건/1파일은 변하지 않았고 DirectX
  수학 헤더 include는 21건/16파일이다.

#### S4-L2b2. Navigation/SpriteSheet/terrain DTO + dead bridge cleanup (2026-08-26)

UI 텍스처 크기와 SpriteSheet parser, terrain CPU 상수 정본에 남은 raw storage를
Mathematics로 옮겼다. 생산자나 소비자가 없는 DX11-era DTO는 새 타입으로 보존하지 않고
배선과 함께 제거했다.

- `ImageInfo`는 GPU upload가 전혀 없는 component 내부 값이었다. 실제 reader가 있는
  `size`만 `math::vector2` 8바이트로 남기고, reader/writer가 모두 0인 `world`와
  `screenSize` 및 잘못 붙어 있던 `cbuffer` 분류를 제거했다. `Texture::m_size`와
  `GetImageSize/GetSize`, UI proxy의 image/text/SpriteSheet 2D 값도 `math::vector2`로
  연결했다. 아직 S6 대상인 RectTransform SimpleMath API와 만나는 곳만 필드 복사한다.
- `SpriteFrame::size/origin`과 사각형 helper 입력을 `math::vector2`로 바꿨다. `.txt`는
  raw dump가 아니라 10개 토큰을 필드별 파싱하므로 파일 형식과 순서는 바뀌지 않는다.
  외부 reader가 0인 `mMaxSrcW/mMaxSrcH` 계산과 저장은 제거했다.
- `TerrainLayerBuffer::layerTilling[16]`은 `math::vector4[16]`로 바꾸고 align 16,
  272바이트, header offset 0/4/8/12와 배열 offset 16을 정적으로 고정했다.
  `TerrainBrush::m_center`도 `math::vector2`다. 현재 terrain DX12/Vulkan pass는 없으므로
  이 구조는 GPU upload 완료가 아니라 future pass가 읽을 CPU 정본이며, 이번 증거는
  layout contract와 producer/consumer compile이다.
- 소비자 0인 `TerrainAddLayerBuffer`, `TerrainGizmoBuffer`와 Terrain proxy의 중복
  gizmo/layer member를 제거했다. 주석만 남아 있던 별도 `SceneRuntime/TerrainBrush.h`와
  vcxproj/filter 항목도 제거했다. 실제 brush/layer 정의는 계속 `TerrainBuffers.h` 하나다.
- 현재 reflected field가 0인 legacy `Mathf::xMatrix` YAML emit/read/vector overload를
  제거했다. Mathematics matrix 경로는 기존처럼 16-float row-major sequence를 직접
  읽고 쓴다. cooked model 설명에 남은 옛 raw 타입 이름도 실제 packed/runtime 의미로
  고쳤다.

검증:

- 전체 `CreatorEditor` Debug non-unity와 Release unity 빌드/링크를 통과했다. 기존
  Terrain C4244, ScriptCore analyzer/trimming, Vulkan delay-load LNK4229, PhysX PDB
  LNK4099와 Release PDB LNK4020 경고만 재현됐다.
- Mathematics contract probe Debug/Release가 layout/convention/DirectX parity를
  통과했다. reflection golden은 Debug에서 77/77 직렬화, 실패 0, diff 0을 통과했다.
- UI layout golden은 client 1920x1080, rect 14개와 hitbox 1개 diff 0을 통과했다.
  Navigation probe의 schema/instance isolation/fresh ID/spatial/legacy 6항목도 모두
  통과했다.
- 같은 최신 Debug Editor에서 `dx12.selftest`, `dx12.ui`, `vk.ui`를 연속 실행해 모두
  exit 0, stderr 0바이트로 통과했다.
- 저장소에 tracked SpriteSheet `.txt` fixture와 실제 SpriteSheet render consumer가
  없어 parser runtime parity는 직접 실행하지 못했다. parser의 token/field 식과 공개
  lookup API는 그대로이고 Debug/Release 양 빌드에서 header/bridge compile을 통과했다.
- 현재 tracked native source 기준선은 `Mathf::*` 458건/89파일, 저장 별칭
  430건/84파일, `DirectX::SimpleMath::*` 414건/43파일, raw XM 저장 117건/11파일,
  XM 함수 205건/26파일이다. bounds 47건/22파일과 Colors 1건/1파일은 변하지 않았고
  DirectX 수학 헤더 include는 20건/15파일이다.

### S5. Physics 독립 섬 — 구조 전환 완료, live simulation gate 없음

S5-D 완료 뒤 현재 기준:

- Physics와 SceneRuntime 물리 bridge의 `DirectX::SimpleMath::*`와 DirectX 수학 헤더
  직접 include는 모두 0이다. PhysX 변환은 `PhysicsMathAdapter.h`에 모였고 legacy
  `PhysicsHelper` 파일과 프로젝트 등록도 제거했다.
- 전체 저장소의 `DirectX::SimpleMath::*`는 19건/2파일만 남는다. 두 파일은 S7에서
  삭제할 `MathematicsInterop.h`와 최종 별칭 teardown 대상 `Core.Mathf.h`다.
- Physics public DTO, actor/collider/CCT/ragdoll storage와 SceneRuntime 물리 bridge는
  이제 모두 `math::*`를 정본으로 사용한다.

게이트 결과:

- Physics library Debug non-unity/Release unity와 전체 CreatorEditor 두 구성이 통과했다.
- DTO layout, PhysX field 왕복, collider offset, CCT rotation, ragdoll root/local 행렬
  계약은 standalone Debug/Release probe로 통과했다.
- Physics 수학 저장 타입과 직접 수학 헤더, legacy vector `memcpy`는 0이다.
- rigid body/CCT/query/ragdoll을 실제 scene에서 재생하는 tracked fixture가 없어 live
  simulation smoke는 미실행이다. 구조·계약·빌드 완료와 runtime 완료를 합산하지 않는다.

#### S5-A. Physics query DTO + PhysX adapter (2026-08-26)

raycast/sweep/overlap의 managed 입력부터 PhysX query와 결과 반환까지 한 수직 경로를
Mathematics 정본으로 옮겼다.

- `RayCastInput/Output`, `SweepInput/HitResult`, `OverlapInput`의 vector/quaternion을
  `math::vector3/quaternion`으로 바꿨다. `RayCastInput` 32바이트, `SweepInput`
  48바이트, `OverlapInput` 32바이트와 각 field offset을 `static_assert`로 고정했다.
- 새 `PhysicsMathAdapter.h`가 `math::vector3/quaternion`과 `PxVec3/PxQuat/PxTransform`
  사이의 명시적 field 변환을 소유한다. 성분은 그대로 보존하고 quaternion 순서는
  `(x,y,z,w)`이며 raw `memcpy`나 암시 변환을 사용하지 않는다.
- `PhysicX`의 raycast 3종, sweep 3종, overlap 3종과 PVD debug line이 이 adapter만
  사용한다. `PhysicsManager` query DTO와 managed `ScriptHitResult` bridge도 `math::*`
  값으로 연결했다. C# `Float3` wire ABI는 필드 복사를 유지하므로 바뀌지 않았다.
- Mathematics contract probe에 PhysX vector 및 transform 양방향 왕복을 추가했고
  Debug/Release 모두 통과했다.
- Physics Debug non-unity rebuild, SceneRuntime Debug non-unity, 전체 CreatorEditor
  Debug non-unity와 Release unity 빌드/링크를 통과했다. 새 Debug Editor의
  `dx12.selftest`/종료도 exit 0으로 통과했다. 기존 Terrain C4244, Vulkan delay-load,
  PhysX PDB와 Release PDB 경고만 재현됐다.
- 전용 live-scene physics query fixture는 이번 슬라이스에 추가하지 않았다. 따라서
  실제 hit ordering/filter 의미는 이후 S5 통합 smoke에서 rigid body/collider fixture와
  함께 검증한다. 이번 결과는 DTO layout, adapter 왕복, 소비자 컴파일/링크와 엔진 기동
  검증이다.
- 현재 tracked native source 기준선은 `Mathf::*` 456건/89파일, 저장 별칭
  428건/84파일, `DirectX::SimpleMath::*` 370건/42파일, raw XM 저장 117건/11파일,
  XM 함수 203건/25파일이다. bounds 47건/22파일, Colors 1건/1파일, DirectX 수학
  헤더 include 19건/14파일이다.

#### S5-B. Physics actor/collider + mesh cooking 경계 (2026-08-26)

rigid actor의 pose/velocity/force/scale, collider offset와 mesh cooking, collision contact
반환까지 한 수직 경로를 Mathematics 정본으로 옮겼다.

- `PhysicsTransform`, `RigidBodyGetSetData`, box/convex/triangle collider DTO와
  `CollisionData::contactPoints`를 `math::*`로 바꿨다. `RigidBody`, static/dynamic body와
  SceneRuntime `RigidBodyComponent`/`ICollider` 구현도 같은 타입을 저장하고 전달한다.
- `PhysicsManager`는 Transform의 Mathematics matrix를 직접 decompose/compose한다.
  box/sphere/mesh offset은 translation을 제외한 world matrix 방향 변환으로 scale과
  rotation을 보존하고, capsule/rigid actor는 기존 `offset 후 world` quaternion 순서와
  역 offset 복구 순서를 유지한다. CCT 내부 DTO에 닿는 지점은 S5-B stop point에서
  `MathematicsInterop` 명시 변환으로 남겼고 S5-C에서 제거했다.
- static/dynamic actor pose, velocity, force와 collision contact는
  `PhysicsMathAdapter.h`의 field 단위 변환만 사용한다. convex/triangle mesh cooking은
  `math::vector3` 배열의 레이아웃을 PhysX에 재해석하지 않고 임시 `PxVec3` 배열로
  명시 복사한다. C# `Float3` wire ABI도 기존 필드 복사를 유지한다.
- Mathematics contract probe에 collider offset을 적용한 actor pose의
  Mathematics→PhysX→Mathematics 왕복과 역 offset 복구를 추가했고 Debug/Release 모두
  통과했다. reflection golden은 77/77 타입, 실패 0, diff 0이다.
- Physics Debug non-unity/Release unity, SceneRuntime Debug non-unity, 전체
  CreatorEditor Debug non-unity와 Release unity 빌드/링크를 통과했다. 기존 Terrain
  C4244, ScriptCore analyzer/trimming, Vulkan delay-load, PhysX PDB와 Release PDB
  경고만 재현됐다.
- 저장소에 rigidbody/collider를 생성·재생하는 전용 CLI 회귀나 추적된 물리 씬
  fixture가 없어 실제 actor simulation, collision/trigger event ordering은 아직 runtime
  통과로 기록하지 않는다. S5 통합 smoke에서 CCT/ragdoll과 함께 검증한다.
- 현재 tracked native source 기준선은 `Mathf::*` 418건/84파일, 저장 별칭
  390건/78파일, `DirectX::SimpleMath::*` 230건/23파일, raw XM 저장 117건/11파일,
  XM 함수 203건/25파일이다. bounds 47건/22파일, Colors 1건/1파일, DirectX 수학
  헤더 include 16건/11파일이다. Physics+SceneRuntime의 SimpleMath 잔여는
  211건/21파일이고 Physics 단독은 163건/15파일이다.

#### S5-C. Character Controller + movement/forced move 경계 (2026-08-26)

CCT 생성 데이터, movement 상태와 SceneRuntime component 왕복을 Mathematics 정본으로
옮겼다. PhysX `PxExtendedVec3`는 adapter 경계 밖으로 새지 않게 했다.

- `CharacterControllerGetSetData`, `CharacterMovementGetSetData`,
  `CharacterControllerInfo`, `CharactorControllerInputInfo`의 position/rotation/scale,
  velocity/input을 `math::*`로 바꿨다. `CharacterMovement`도 PhysX 타입을 더 이상
  저장하거나 반환하지 않으며 `CharacterController`가 이동 직전에 adapter로 변환한다.
- `PhysicsMathAdapter.h`에 `math::vector3 <-> PxExtendedVec3` field 변환을 추가했다.
  CCT 생성, get/set position과 shape/controller contact point가 이 변환을 사용하며,
  float/double 축소·확대는 이 경계에서 명시적으로 일어난다.
- forced move의 velocity와 lerp, 일반 movement 출력, `PhysicsManager`의 pending CCT
  position을 `math::vector3`로 연결했다. `CharacterControllerComponent`의 move input,
  look direction과 자동 yaw 회전은 `math::normalize`, `length_sq`, `to_euler`,
  `quaternion_from_pitch_yaw_roll`, `slerp`를 사용한다. CCT 경로의
  `MathematicsInterop`, `PhysicsHelper`와 직접 SimpleMath 사용은 0이다.
- C# `Float2/Float3` 호출은 기존 aggregate field ABI를 유지한다. 공개 managed 함수표와
  wire layout은 바꾸지 않았다.
- Mathematics contract probe에 `PxExtendedVec3` position 왕복과 기존 DirectX yaw-only
  quaternion slerp oracle 대조를 추가했고 Debug/Release 모두 통과했다. reflection
  golden은 77/77 타입, 실패 0, diff 0이다.
- Physics Debug non-unity/Release unity, SceneRuntime Debug non-unity, 전체
  CreatorEditor Debug non-unity와 Release unity 빌드/링크를 통과했다. 기존 Terrain
  C4244, Vulkan delay-load, PhysX PDB와 Release PDB 경고만 재현됐다.
- 저장소에 CCT를 생성해 movement/forced move/contact를 재생하는 전용 CLI 회귀나
  tracked live-scene fixture가 없어 실제 이동, slope/step, 충돌 순서와 forced-move
  시간 곡선은 runtime 통과로 기록하지 않는다. S5-D 뒤 통합 physics smoke에서
  rigid body/collider/ragdoll과 함께 검증한다.
- 현재 tracked native source 기준선은 `Mathf::*` 413건/83파일, 저장 별칭
  389건/77파일, `DirectX::SimpleMath::*` 163건/15파일, raw XM 저장 117건/11파일,
  XM 함수 호출 207건/25파일이다. bounds 47건/22파일, Colors 1건/1파일, DirectX 수학
  헤더 include 16건/11파일이다. Physics+SceneRuntime의 SimpleMath 잔여는
  144건/13파일이고 Physics 단독은 123건/11파일이다.

#### S5-D. Ragdoll/articulation + Physics root cleanup (2026-08-26)

ragdoll/articulation DTO와 link/joint 계산을 Mathematics로 옮기고 Physics의 마지막
SimpleMath root를 제거했다.

- `Articulation*Data`, `JointInfo`, `LinkInfo`, `ArticulationInfo`, SceneRuntime
  `LinkData`/`ArticulationData`와 `RagdollLink/Joint/Physics` 저장 행렬을
  `math::matrix4x4`로 바꿨다. 기존 SimpleMath 기본 생성자가 identity였던 필드는
  `math::matrix4x4::identity()`로 명시해 zero-default로 바뀌지 않게 했다.
- link/root pose의 scale 제거, root Z 회전, joint child/parent pose, simulated local
  `childGlobal * inverse(parentGlobal)` 순서를 `decompose`, `compose`, `rotation_z`,
  `inverse`로 옮겼다. PhysX position/quaternion은 `PhysicsMathAdapter.h`의 field 변환만
  사용하며 `(x,y,z,w)` 순서를 유지한다.
- `RagdollLink` box extent의 `memcpy(PxVec3 <- Vector3)`를 adapter field 변환으로
  제거했다. rigid actor dirty pose 판정도 adapter로 옮긴 뒤, 소비가 끝난
  `PhysicsHelper.h/.cpp`와 vcxproj/filter 등록을 삭제했다.
- 호출자가 전혀 없던 `mDebugPolygon/mDebugVertices/mDebugHeightField`와
  `extractDebugConvexMesh` dead path는 Mathematics 타입으로 존치시키지 않고 제거했다.
- contract probe에 ragdoll root scale을 보존하는 PhysX pose 왕복, quaternion 부호를
  동치로 처리하는 transform dirty 판정, simulated-local row-vector 순서를 추가했고
  Debug/Release 모두 통과했다. reflection golden은 77/77 타입, 실패 0, diff 0이다.
- Physics Debug non-unity/Release unity, SceneRuntime Debug non-unity, 전체
  CreatorEditor Debug non-unity와 Release unity 빌드/링크를 통과했다. 기존 Terrain
  C4244, Vulkan delay-load, PhysX PDB와 Release PDB 경고만 재현됐다.
- repository call graph에서 `CreateCharacterInfo`, `Add/Get/SetArticulationData`는 Physics
  내부 선언·정의 외 호출자가 없고 tracked ragdoll/articulation scene fixture도 없다.
  따라서 실제 PxArticulation 생성, joint limit/drive와 simulation 결과는 runtime
  통과로 기록하지 않는다. 이 경로의 존치/삭제는 별도 Physics 재설계 범위다.
- 현재 tracked native source 기준선은 `Mathf::*` 413건/83파일, 저장 별칭
  389건/77파일, `DirectX::SimpleMath::*` 19건/2파일, raw XM 저장 117건/11파일,
  XM 함수 호출 207건/25파일이다. bounds 47건/22파일, Colors 1건/1파일, DirectX 수학
  헤더 include 13건/8파일이다. Physics+SceneRuntime의 SimpleMath와 직접 수학 헤더
  include는 모두 0이다.

### S6. UI/Editor/나머지 값 타입

현재 기준과 순서:

- **S6-A Color — 완료:** `Mathf::Color4` 72건/24파일과 `DirectX::Colors` 마지막
  사용을 `math::color`로 옮겼다. Reflection/YAML의 `r/g/b/a`, Inspector 편집,
  material/UI DTO와 GPU RGBA 16-byte layout을 함께 닫았다.
- **S6-B Rect/UI — 완료:** `Mathf::Rect` 정의와 qualified 사용 전량을 `math::rect`로
  옮기고 Reflection/YAML의 `x/y/width/height` 키와 순서를 유지했다. `UIButton`의
  항등 orientation `BoundingOrientedBox`도 같은 world rect hitbox로 제거했다.
- **S6-C UI/Editor vector/helper — 다음:** RectTransform/Input/ActionMap과 Editor
  property UI의 Vector2/3/4 잔여를 옮기고 `Mathf::Easing/Tween` 소비자가 전용 헤더를
  직접 include하게 한다.
- C# `Float2/3/4`와 ScriptCore Quaternion은 wire ABI로 유지하고 native 경계에서
  필드 복사한다. native 타입 이름 변경 때문에 API table version을 올리지 않는다.

게이트:

- UI layout/canvas/DDOL smoke.
- Inspector vector/color/rect 편집 + undo/redo.
- rect 공유 모서리의 half-open 판정, 0/음수 크기, UIButton hitbox/표시 위치 일치.
- managed transform/input/physics/image/material API smoke.

#### S6-A. Color 저장·직렬화·GPU 전달 (2026-08-26)

- native `Mathf::Color4` 72건/24파일을 distinct `math::color`로 옮겼다. 사용자가
  없던 `Color3`와 전환이 끝난 `Color4` 별칭을 `Core.Mathf.h`에서 삭제했고,
  `Core.Definition.h`의 `DirectXColors.h` include와 마지막 `DirectX::Colors::Black`
  사용도 제거했다. C# `Color4`와 native API table의 `Float4`는 wire ABI이므로
  유지하고 `ClrHost` 경계에서 `r/g/b/a`와 `x/y/z/w`를 필드 복사한다.
- typed YAML emitter/reader와 scalar concept를 `math::color`로 바꿨다. 키와 순서는
  계속 `{r, g, b, a}`이고 reflection golden은 77/77 타입, 실패 0, diff 0이다.
  Inspector/재질 편집은 첫 필드 `&color.r`을 넘기며 CLI property setter도 새 타입
  ID를 사용한다.
- material, light, UI/text/image/sprite, gizmo와 DX12/Vulkan 공용 frame payload를
  `math::color`로 통일했다. vector4가 실제 경계 타입인 곳만 `rgba()`로 명시 변환한다.
  `math::color` 16바이트/alpha offset 12 계약에 더해 gizmo vertex 28바이트,
  `EnhancedLight` 64바이트, GBuffer/Sprite instance 96바이트, UI instance 64바이트,
  Grid constants 160바이트의 크기·offset assert를 유지하거나 추가했다.
- CreatorEditor Debug non-unity와 Release unity가 엔진, RenderTests, managed assembly,
  최종 exe 링크까지 통과했다. Mathematics contract Debug/Release, reflection golden,
  `dx12.selftest`, `dx12.ui`, `vk.ui`도 모두 통과했고 세 GPU canary는 exit 0,
  stderr 0바이트다. 기존 Terrain C4244, Vulkan delay-load, PhysX PDB와 Release PDB
  경고만 재현됐다.
- 현재 tracked native source는 `Mathf::*` 341건/72파일, 저장 별칭 317건/66파일,
  `DirectX::SimpleMath::*` 17건/2파일이다. raw XM 저장 117건/11파일, XM 함수
  207건/25파일, bounds 47건/22파일은 변하지 않았다. `DirectX::Colors`는 0,
  DirectX 수학 헤더 직접 include는 12건/8파일이다.

#### S6-B. Rect 저장·UI hitbox (2026-08-26)

- `Core.Mathf.h`의 자체 `Rect` 정의와 native `Mathf::Rect` qualified 사용 전량을
  distinct `math::rect`로 옮겼다. RectTransform의 화면/부모/world rect, Canvas scale,
  Scene UI traversal, Text manual rect와 Editor anchor 편집이 같은 타입을 사용한다.
- typed YAML emitter/reader와 scalar concept, Inspector `DragFloat4`를 `math::rect`로
  바꿨다. 직렬화 키와 순서는 계속 `{x, y, width, height}`이며 16바이트/standard-layout/
  trivially-copyable/height offset 12 계약을 유지한다.
- `UIButton`의 항등 orientation `DirectX::BoundingOrientedBox`와 7개 raw XM 저장 값,
  8개 XM 함수 호출을 제거했다. hitbox는 표시용 world rect의 값 복사이며
  `math::contains`의 half-open 규약으로 판정한다. 최대 모서리와 공유 모서리는 제외하고
  0/음수 크기는 비어 있는 hitbox로 처리한다. `ui.hitbox`도 같은 rect를 직접 출력한다.
- Mathematics contract Debug/Release에서 최소/최대 모서리, 공유 모서리, 0/음수 크기와
  명시적 normalization을 통과했다. reflection golden은 77/77 타입, 실패 0, diff 0이다.
  UI layout golden은 rect 14개와 hitbox 1개가 diff 0이고, resolution sweep은 알려진
  swapchain resize 결함 전까지 2개 해상도·12개 단정·hitbox 2회를 통과했다. DDOL
  캔버스 이송도 전후 1개와 hierarchy/store 불일치 0으로 통과했다.
- SceneRuntime Debug non-unity, CreatorEditor Debug non-unity와 Release unity가 엔진,
  RenderTests, managed assembly와 최종 exe 링크까지 통과했다. 기존 Terrain C4244,
  Vulkan delay-load, PhysX PDB와 Release PDB 경고만 재현됐다.
- 현재 tracked native source는 `Mathf::*` 300건/68파일, 저장 별칭 276건/62파일,
  `DirectX::SimpleMath::*` 17건/2파일이다. raw XM 저장 110건/10파일, XM 함수
  199건/24파일, bounds 45건/21파일이며 `Mathf::Rect`와 UIButton의 DirectX/XM 수학
  표면은 0이다. DirectX 수학 헤더 직접 include는 12건/8파일이다.

### S7. bounds/frustum + DirectX 수학 의존 제거

확인 완료된 upstream 계약:

- `math::transform(aabb, matrix/TRS)`와 DirectX `BoundingBox::Transform` parity.
- `math::bounding_frustum`의 LH/RH projection 생성, transform, corners/planes,
  point/sphere/aabb/frustum query, raycast와 DirectXCollision parity.
- 이 항목들은 고정 SHA에 이미 있으므로 upstream 기능 추가를 S7 선행 조건으로 두지 않는다.

현재 상태:

- main `Mesh` asset/component bounds, proxy와 draw-pool AABB는 이미
  `math::aabb/sphere`다. 이를 다시 만드는 작업은 남은 범위가 아니다.
- DirectX collision 타입은 45건/21파일 남아 있다. 이 중 `BoundingFrustum`은
  25건/17파일이고 `UIButton`의 `BoundingOrientedBox`는 S6-B에서 제거됐다.
- 남은 box/sphere는 `UIMesh`, Camera/Light editor bounds, SceneView와 light packing,
  transition contract/interop에 한정된다.

변경 순서:

- Camera가 `math::bounding_frustum`을 생산하고 AI/Foliage/Scene/light packing이 같은
  타입을 값으로 전달하게 한다. gizmo는 `corners()`를 직접 소비한다.
- SceneView와 editor component bounds, `UIMesh`의 잔여 `BoundingBox/Sphere`를
  `math::aabb/sphere`로 바꾸고 기본값이 empty인지 unit인지 명시한다.
- `MathematicsInterop`의 `BoundingBox` 양방향 bridge와 DirectXCollision include를
  마지막 실제 소비자와 함께 삭제한다.
- projection 생성 실패를 구분해야 하는 경로는
  `try_bounding_frustum_from_projection_lh/rh`를 사용하고 fallback 정책을 호출부가 정한다.
- CEMA v2 mesh cache의 `math::aabb/sphere` round trip을 다시 검증한다. `UIMesh` 또는
  다른 raw dump가 발견되면 크기와 field offset을 assert하고 포맷 호환을 별도 증명한다.
- `MathematicsInterop.h`의 DirectXMath/SimpleMath bridge를 모두 삭제한다.
- `Core.Mathf.h`의 타입 별칭과 DirectX 기반 helper를 삭제한다. 필요한 scalar 상수는
  Mathematics 정본 또는 좁은 엔진 상수 헤더를 직접 사용한다.
- `Core.Definition.h`에서 DirectXMath/DirectXColors/SimpleMath include를 제거한다.
- transition parity 용도로 DirectX를 포함하는 `mathematics_contract_probe.cpp`도 최종
  numeric/property 계약으로 바꾸어 저장소의 수학 의존 0 게이트에 포함한다.
- 코드 사용이 0이면 `vcpkg.json`의 `directxmath`/`directxtk12` 직접 의존과
  `Directory.Build.props`의 DirectXTK 정적 링크 설정을 제거한다. DirectXTex, DXGI,
  D3D12 같은 렌더/API 의존은 이 계획의 제거 대상이 아니다.

최종 게이트:

- 제품, Editor, RenderTests와 `Tools/regression` source에서 `Mathf` 타입 별칭,
  `DirectX::SimpleMath`, raw `XMVECTOR/XMMATRIX/XMFLOAT*`, `XMVector*`/`XMMatrix*`,
  `BoundingBox/Sphere/Frustum/OrientedBox`, `DirectX::Colors` 0건(역사 문서/ThirdParty 제외).
- 같은 범위에서 `<DirectXMath.h>`, `<DirectXCollision.h>`, `<DirectXColors.h>`와
  SimpleMath 직접·전이 include 0.
- `vcpkg.json`의 `directxmath`/`directxtk12`와 DirectXTK 전용 build 설정 0.
- Debug non-unity + Release unity 엔진 라이브러리, CreatorEditor, Player build.
- reflection golden diff 0, scene/prefab round trip, C# ABI, DX12/Vulkan, Physics smoke 통과.
- CEMA v2 bounds round trip, AABB transform, perspective/orthographic frustum culling,
  raycast/intersection과 gizmo corner 순서 parity 통과.
- 제거 후 `git diff`에 asset/schema 변화가 없고, 실행 산출물에 새 DLL 요구가 없음.

## 5. S0 구현 패치의 실제 범위

첫 패치는 계획대로 S0만 수행했으며 다음을 넘지 않았다.

1. `ThirdParty/Mathematics/include`, license, provenance의 최초 도입 판본을
   `04c8bbe30272b3332716cec66cd35dc4d8cb8dbf`에 고정.
2. `ThirdParty/README.md` 판본/갱신 규약 추가.
3. `Directory.Build.targets` 공통 include 경로 추가. 프로젝트보다 먼저 평가되는
   `Directory.Build.props`에서는 vcxproj의 구성별 값이 이를 덮어쓸 수 있어 S2 실제
   빌드에서 뒤쪽 targets 배선으로 교정했다.
4. 독립 contract probe와 실행 스크립트 추가.
5. Debug/Release probe 실행 결과 기록.

이 패치에서는 `Core.Mathf.h`, `Core.Definition.h`, `vcpkg.json`, 기존 소비자와
개별 `.vcxproj`/package/deployment 설정을 바꾸지 않았다. 공통 include 경로에 헤더
온리 의존을 "놓기"와 실제 consumer를 "배선하기"를 분리해, 외부 라이브러리 고정과
엔진 회귀를 따로 판정한다.

다음 패치가 S1 cleanup, 그다음이 S2 camera 수직 슬라이스다. S2가 DX12와
Vulkan 기준선을 모두 통과하기 전에는 Transform/Physics로 확장하지 않는다.

## 6. 판정표

| 질문 | 판정 |
|---|---|
| 행렬/쿼터니언 관례가 현재 엔진과 맞는가 | 맞음. DirectXMath parity 규약 |
| 저장 레이아웃이 GPU/YAML에 쓸 수 있는가 | vector/matrix/color/rect/bounds는 맞음. 경계별 assert와 round trip 필요 |
| `Mathf` alias만 교체할 수 있는가 | 불가. member API와 implicit DX bridge가 다름 |
| `xVector -> vec_reg`로 바꾸면 되는가 | 불가. `vec_reg`는 저장 타입이 아님 |
| Color/Rect도 Mathematics로 가는가 | 가능. `math::color/rect`가 distinct type과 기존 저장 형상을 제공 |
| BoundingBox/Sphere는 갈 수 있는가 | 가능. aabb(center/extents)/sphere와 AABB affine transform 제공 |
| BoundingFrustum도 지금 갈 수 있는가 | 가능. `math::bounding_frustum`과 projection/transform/query/parity 제공 |
| BoundingOrientedBox도 추가해야 하는가 | 현재는 아니오. UIButton의 항등 2D hitbox를 `math::rect`로 제거 |
| C# ABI를 같이 바꿔야 하는가 | 아니오. Float2/3/4 wire shape 유지 |
| AVX2를 같이 켜야 하는가 | 아니오. 별도 CPU baseline 결정 |
| master를 빌드 때 받아도 되는가 | 아니오. 검증 commit 벤더링 |
