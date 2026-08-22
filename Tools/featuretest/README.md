# 기능별 테스트 씬

렌더 기능을 하나씩 눈으로 확인하기 위한 씬과, 그 씬을 만들고 찍는 도구다.

## 왜 필요한가

저장소에 있던 모델은 실게임 에셋(캐릭터 메시)뿐이었다. 캐릭터로는 무엇이 틀렸는지
구분하기 어렵다 — 노멀이 뒤집혔는지, 접선이 어긋났는지, 그림자가 새는지가 복잡한
실루엣에 묻힌다. 기본 도형은 정답을 알고 보는 것이라 어긋난 순간 바로 보인다.

씬을 에디터로 손수 만들지 않는 이유도 같다. 손으로 만든 씬은 무엇이 왜 그 자리에
있는지가 파일 안에 남지 않는다. 여기서는 배치 의도가 스크립트에 코드로 남는다.

## 흐름

```
블렌더 ──> .glb ──> 엔진 CLI로 씬 저작 ──> .creator ──> 씬 로드 ──> PNG
export-primitives   model.load          scene.save    scene.switch  capture-window
```

세 단계가 각각 별도의 스크립트다. 실패했을 때 어느 단계가 깨졌는지 바로 알 수 있어야
하기 때문이다.

### 1. 도형 뽑기

```bash
pwsh Tools/blender/export-primitives.ps1
```

`Dynamic_CPP/Assets/Models/`에 `Prim_*.glb` 9개를 만든다. 블렌더는 자동으로 찾고,
없으면 `-Blender <경로>`로 준다.

하위 폴더에 두지 않는다 — `DataSystem::LoadModel`이 모델을 `Assets/Models` 바로
아래로 복사해 거기서 읽기 때문에, 하위 폴더에 두면 복사가 한 번 더 일어나고 그
복사가 에셋 감시자의 `.meta` 생성과 겹쳐 로드가 실패한다.

| 도형 | 무엇을 보는가 |
|------|---------------|
| `Prim_Cube` | 면이 평평하고 모서리가 뚜렷하다 — 노멀·접선이 면마다 상수 |
| `Prim_Sphere` | UV 구. 경도/위도 UV라 접선 공간 확인에 좋다 |
| `Prim_IcoSphere` | 삼각형이 고른 구. UV 이음매가 없어 셰이딩만 볼 때 |
| `Prim_Cylinder` | 평평한 면과 굽은 면이 한 메시에 — 스무딩 경계 |
| `Prim_Cone` | 한 점으로 모이는 노멀 — 정점 노멀 보간 |
| `Prim_Torus` | 자기 자신을 가린다 — 자기 그림자·여드름 편향 |
| `Prim_Plane` | 바닥. 그림자가 드리우는 대상 |
| `Prim_Suzanne` | 오목·볼록이 섞인 실물형 메시 — 종합 |
| `Prim_MatGrid` | 거칠기 5 × 금속성 2 격자 — 재질 상수가 닿는지 |

베이스 컬러 텍스처를 반드시 붙인다. 값(factor)만 넣으면 DX11 경로가 베이스 컬러
SRV를 못 찾아 검게 그린다. `Prim_MatGrid`만 단색이고 나머지는 UV 색상 격자다 —
격자는 UV가 뒤집히거나 이음매가 어긋나면 바로 보인다.

### 2. 씬 저작

```bash
pwsh Tools/featuretest/build-scenes.ps1
pwsh Tools/featuretest/build-scenes.ps1 -Only FT_Shadow    # 하나만
```

`Dynamic_CPP/Assets/Scenes/FT_*.creator` 4개를 만든다.

| 씬 | 무엇을 보는가 |
|----|---------------|
| `FT_Primitives` | 도형 임포트 자체 — 메시·UV·노멀·재질이 엔진까지 오는가 |
| `FT_Shadow` | 깊이 방향으로 늘어놓은 캐스터 — 캐스케이드가 거리에 따라 갈리는가 |
| `FT_Material` | 거칠기·금속성 격자 — 재질 상수가 셰이더에 닿는가 |
| `FT_Lights` | 방향광·점광·스포트가 각각 다르게 보이는가 |

씬마다 엔진을 새로 띄운다. 한 프로세스에서 연달아 만들면 앞 씬의 잔재가 남을 수 있고,
그러면 무엇을 보고 있는지 불분명해진다.

#### `FT_Primitives` 패키지 정본 계약

`FT_Primitives.creator`는 눈으로 보는 기능 테스트인 동시에 Player package smoke의
canonical fixture다. 현재 계약은 Entity 11, Transform 11, Mesh 8, Camera 1, Light 1,
`PackageSmokeProbe` 1이다. probe는 패키지에서 C# `OnInitialized`와
`OnBeginSimulation`이 각각 정확히 한 번 호출됐는지 확인한다.

```bash
pwsh Tools/featuretest/build-scenes.ps1 -Only FT_Primitives
pwsh Tools/build.ps1 -Config Release -InputMode Workspace -BuildNative
```

생성기는 정본 파일을 바로 덮어쓰지 않는다. 같은 디렉터리에 candidate를 만들고 현재/구
schema key, 위 component 수, probe script type을 검증한 뒤 원자적으로 교체한다.
생성·검증 실패 시 기존 `FT_Primitives.creator`는 그대로 남는다. 저장·재로드 뒤의
계층/transform 보존은 생성기 내부 검사가 아니라 별도 scene 회귀로 판정한다.

Player는 fixture를 source tree에서 저작하지 않는다. Stage의 pak을 PID별 TEMP
`RuntimeContent`에 풀어 읽고, 로그·dump·cache는 sibling `RuntimeData`에 기록한다.
asset-authoring capability가 꺼져 있어 현재 package boot/smoke가 지나는
DataSystem·ModelLoader·shader compiler·settings·physics write hotspot은 차단된다.
모든 public writer와 watcher의 물리 분리 완료를 뜻하지 않으며, 그 범위는 E2 gate로
계속 추적한다. fixture 재생성은 Editor CLI를 사용하는 위 저작 단계에서만 수행한다.

### 3. 스크린샷

```bash
pwsh Tools/featuretest/run-featuretests.ps1
```

`Tools/featuretest/screenshots/FT_*.png`를 만든다. 캡처는 밖에서 창을 찍는다
(`Tools/regression/capture-window.ps1`) — 엔진 내부 캡처는 게임 스레드에서 죽는
문제로 보류돼 있다.

## 쓰인 CLI 명령

이 도구를 위해 엔진에 추가한 것들이다. 전부 `--script` 파일에서 쓸 수 있다.

| 명령 | 하는 일 |
|------|---------|
| `scene.new [이름]` | 빈 씬을 만들어 활성화한다 |
| `scene.save <경로>` | 활성 씬을 `.creator`로 저장한다 |
| `object.create <이름> [타입]` | 빈 오브젝트(Empty/Light/Camera/Mesh) |
| `object.rename <이전> <새>` | 이름을 바꾼다(새 이름이 마지막 토큰) |
| `object.transform <이름> <px py pz> [rx ry rz] [sx sy sz]` | 변환(회전은 도) |
| `object.property <오브젝트> <컴포넌트> <필드> <값>` | 리플렉션으로 프로퍼티 설정 |
| `script.add <오브젝트> <타입>` | C# 스크립트 컴포넌트를 오브젝트에 부착 |

`object.property`는 컴포넌트마다 전용 명령을 만들지 않는다. 인스펙터가 훑는 것과
같은 프로퍼티 목록을 타므로, 컴포넌트가 늘어도 CLI는 그대로다. 값은 `1,2,3`이나
`1 2 3` 둘 다 되고, 열거형은 이름(`DirectionalLight`)으로도 숫자로도 받는다.

## 원본을 다시 뽑으면 캐시가 무효화된다

`Model::LoadModel`이 `.asset` 캐시의 **존재 여부만** 보고 있었다. 그래서 저작
도구에서 모델을 다시 내보내도 캐시가 있으면 그것을 읽었고, 바뀐 내용이 반영되지
않았다. 증상이 고약하다 — 텍스처를 넣어 다시 뽑았는데 엔진에서는 재질이 계속
비어 있고, 파일에는 텍스처가 멀쩡히 들어 있으니 임포터를 의심하게 된다.

지금은 원본이 캐시보다 새것이면 다시 임포트한다. `Prim_*.glb`를 다시 뽑으면
자동으로 반영되므로 `.asset`을 손으로 지울 필요가 없다.

## 알려진 것

**빈 씬을 만들 때 즉시 교체하면 안 된다.** `SceneManager::CreateScene`은 옛 씬을
그 자리에서 해체하는데, 그 자리가 프레임 중간이라 커맨드를 만들던 렌더 워커의
발밑에서 자료구조가 사라진다(`ShadowMapPass::CreateCommandListCascadeShadow` →
`UpdateBuffer`에서 죽었다). `scene.new`는 `Scene::CreateNewScene` + `ActivateScene`
경로를 쓴다 — 교체를 프레임의 안전 지점까지 미룬다.

**종료가 멈출 때가 있다.** 씬을 로드한 뒤 `quit`이 돌아오지 않는 경우가 있어
`build-scenes.ps1`과 `run-featuretests.ps1`은 타임아웃 후 강제 종료하고 산출물
유무로 판정한다. 별건으로 추적 중이다.

**리사이즈하면 검게 변하던 것은 고쳤다.** 원인은 SSGI 로직이 아니라 리소스였다.

SSGI·블룸·비트마스크는 화면의 1/N 해상도 버퍼를 쓴다. 그것들은 비율 오버로드
(`Texture::Create(ratioX, ratioY, width, height, ...)`)로 만들어지는데, 해상도
슬라이스에서 화면 크기를 인자로 넘기는 형태만 바꾸느라 **이 비율 형태 6곳이
빠져 있었다.** 그래서 창을 리사이즈해도 이 버퍼들만 옛 크기로 남았고, 디스패치는
현재 뷰포트에서 스레드 수를 계산하니 둘이 어긋나 결과가 깨졌다.

★ 더 나쁜 것은 `render.rtinfo`가 "어긋난 것 0개"라고 보고했다는 점이다.
추종을 **선언한** 것만 명부에 오르는데 이 6곳은 선언 자체가 빠져 있었으니
명부에도 없었다. 검사가 자기가 못 보는 것을 정상으로 보고한 것이다 —
"확인하지 못했다"와 "확인했고 문제없다"를 구분하지 못한 사례가 하나 더 늘었다.
고친 뒤 명부가 33개 → 45개가 됐고, 그 12개가 정확히 빠져 있던 것들이다.

이걸 알기까지 두 번 더 틀렸다. 기록해 둔다.

| 세웠던 가설 | 결과 |
|-------------|------|
| 자동 노출이 밝은 하늘을 측광해 뭉갠다 | 아님. `render.exposure`로 재니 자동 노출은 꺼져 있고 노출 1.3 |
| 광원 강도 단위가 달라 강도 1은 검다 | 아님. 그때 밝아진 것의 상당 부분이 포그였다 |

두 번 다 원인은 **내 캡처 절차가 매번 창을 리사이즈하고 있던 것**이었다.
재현 절차 자체가 변수를 하나 더 넣고 있었는데 그것을 의심하지 않았다.

**포그를 끄고 찍는다.** 프로젝트 설정에 `volumetricFog.isOn: true` · `mStrength: 2` ·
`mBlendingWithSceneColorFactor: 0.851`로 저장돼 있어 최종 색의 85%가 안개 색이다.
게임의 룩으로는 의도된 값이지만 기능 확인용 그림에서는 보려는 것을 덮는다.
`run-featuretests.ps1`이 캡처 전에 `render.post fog off`를 건다 — 설정 파일은
건드리지 않는다. 포그가 켜진 그림이 필요하면 `-KeepFog`를 준다.

**렌더 타깃 해상도는 고쳤다.** 예전에는 `Texture::Resize2DViews`가 인자를 쓰지 않아
창을 리사이즈해도 타깃이 따라오지 않았고, 뷰포트만 따라가 그림이 패널 좌상단에
몰렸다. 지금은 `RHI/ScreenSizedResource.h`의 정책을 선언한 리소스만 따라간다.
`dx12.resize`로 확인할 수 있다.
