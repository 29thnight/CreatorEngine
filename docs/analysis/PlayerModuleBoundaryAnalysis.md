# Player 모듈 경계(DLL화) 판단 자료 (E7 잔여 결정)

- 작성일: 2026-08-26
- 목적: EngineLayerSeparationPlan E7의 미착수 항목 — "Player thin exe +
  game module DLL 구조 또는 Player DLL export 구조 검토" — 의 결정 자료.
- 방법: 링크 구조·관리 계층·DLL 제거 이력·계획 문서·전역 상태를 병렬 실측한
  뒤, 도출한 결론 3건을 적대적으로 반박 검증했다. **반박 3건이 전부 성립해
  최초 결론을 폐기하고 다시 썼다** — 그 과정은 §6에 남긴다. 수치는 전부 이
  날짜의 실측이다.

## 0. 한 줄 결론

**"안 한다"가 아니라 "선행 조건이 없어 지금은 못 한다"**다. 이득은 실재하고
(오늘도 매 게임 빌드마다 네이티브 재컴파일 비용을 내고 있다), 저장소 자신이
해법의 이름을 이미 `Core DLL/version provenance`라고 적어 뒀다. 다만 그
provenance가 없는 상태에서 DLL만 떼면 비용만 내고 이득은 못 받는다.

## 1. 질문의 분해 — 분리축이 둘이다

"유니티처럼 DLL화"는 서로 다른 두 분리를 한 단어로 부른다. 이 구분을 놓치면
"이미 다 했다"와 "하나도 안 했다"가 동시에 참인 것처럼 보인다.

| 축 | 유니티 | CreatorEngine 현재 | 상태 |
|---|---|---|---|
| **A. 게임코드 ↔ 엔진** | `Assembly-CSharp.dll` | `GameScripts.dll` (ALC 언로드 가능) | **달성** |
| **B. 엔진코어 ↔ 호스트셸** | `UnityPlayer.dll` + 얇은 exe | 없음 — exe가 static lib 6종을 통째로 링크 | **미달성** |

축 A는 이미 유니티와 같은 원리로 구현돼 있다. `ScriptCore.dll`은 기본
컨텍스트 상주(엔진 API, 안 바뀜), `GameScripts.dll`은 언로드 가능 컨텍스트
(게임 코드, 자주 바뀜) — 유니티의 `UnityEngine.*.dll` vs `Assembly-CSharp.dll`
구분과 같다. 사용자 게임 스크립트 27개 전부 `ScriptCore.Behaviour` 상속이고,
**C++로 게임플레이를 짜는 경로는 남아 있지 않다**(`ScriptComponent`는 로직 0,
수명 6단계를 전부 ClrHost에 위임).

따라서 **질문이 실제로 가리키는 것은 축 B뿐**이다. 이 문서는 축 B만 다룬다.

## 2. 현재 구조 실측

### 2.1 Player는 이미 얇다 — 다만 "exe 안이 얇다"는 뜻이 아니다

| 항목 | 실측 |
|---|---|
| exe 자체 코드 | `PlayerApp.cpp` 318줄 + `PlayerMain.cpp` 538줄 = **856줄**, 게임 로직 0 |
| 정적 링크되는 엔진 | static lib 6종, 소스 합계 **약 128,258줄** |
| `Bin\x64-Release\Player\` | 25파일 349.7MB (exe 4.1MB) |
| `DynamicLibrary` 프로젝트 | **0개** (11개 vcxproj 전수) |

exe 안의 조립 코드는 이미 최소다. 얇지 않은 것은 **exe라는 배포 단위**다 —
엔진 12만 줄이 그 안에 정적으로 박혀 있다.

### 2.2 유일한 진짜 DLL 경계는 관리 계층뿐

`build.ps1`이 `ScriptCore.csproj`·`GameScripts.csproj`를 `dotnet build`로
독립 빌드하고, Stage 단계가 `Managed\`로 골라 복사한다. 런타임 DLL 목록에
게임 코드 네이티브 DLL은 없다. 즉 이 엔진에서 "게임마다 바뀌는 것"은 이미
네이티브 재빌드 없이 교체된다 — **원리적으로는**.

## 3. 이득 — 실재하며, 지금도 비용을 내고 있다

### 3.1 결정적 증거: 게임 빌드가 오늘도 네이티브를 재컴파일한다

`BuildPipelinePlan`이 "유니티식(선빌드 + 복사)"이라 적은 것은 **목표 상태**이지
현재가 아니다. 문서가 스스로 구분해 뒀다:

> **목표 상태의** 게임 빌드는 복사·쿡·pak이다. 다만 Core ABI/version
> provenance가 아직 없으므로 현재 제품 경로는 stale Player 배포를 막기 위해
> 오케스트레이터에 `-BuildNative`를 맡긴다. 선빌드 Player의 정체성을 검증할 수
> 있게 된 뒤 이 옵션을 제거해야 비로소 분 단위 native build가 사라진다.
> — `BuildPipelinePlan.md:227-230` (§2.3:316-317도 재확인)

문서상의 수사가 아니라 코드 동작이다 — `GameBuilderSystem.cpp:121`이
`-BuildNative`를 **조건문 없이 20개 고정 인자 중 하나로 항상** 넘긴다. 에디터에서
게임 패키지를 빌드할 때마다 Player·AssetPacker가 실제로 재컴파일된다.

그 바로 위 주석(`:171-172`)이 전제 조건의 이름을 명시한다:

> Core DLL/version provenance가 생기기 전에는 stale Player 배포를 막기 위해
> 제품 경로도 `-BuildNative`를 명시한다.

즉 **재컴파일을 없애는 해법 자체가 이미 이 저장소의 언어로 "Core DLL"이라
지목돼 있다.** 축 B는 "해도 되고 말아도 되는 선택지"가 아니라 이미 지목된
미완의 해법이다.

### 3.2 부수 이득: 정적 링크의 조용한 진부화

`CreatorEditor.exe`와 `Player.exe`가 같은 Runtime Core static lib을 **각자
독립적으로** 링크한다. 공유 lib이 바뀌면 소비하는 모든 exe를 빠짐없이
재링크해야 하고, 하나라도 빠뜨리면 옛 로직이 조용히 실행된다. 이 결함
계열은 이미 겪었다 — 메모리 `gate-measures-stale-binary`(8-23)가 같은 뿌리다.
DLL 경계는 이 실패를 "버전 불일치"라는 더 시끄러운 실패로 바꾼다.

⚠ 단, 이 이득은 축 B 없이 **version provenance만으로도** 상당 부분 얻는다 —
DLL화의 고유 이득으로 과대 계상하지 말 것.

### 3.3 미래 이득: 네이티브 플러그인

`BuildPipelinePlan.md:257-259`가 재검토 트리거를 명시한다 — "게임별 네이티브
최적화의 자유를 잃는다 — 게임 코드가 C#뿐이라 지금 실익이 없다. 미래에 게임별
네이티브 플러그인 개념이 생기면 이 결정을 재검토한다." `EngineDistribution
AndLauncherPlan.md:412,417`도 "C#/native plugin/build hook"을 **병렬 항목**으로
나열한다. 즉 저장소 저자들 스스로가 축 A(C#)와 네이티브 모듈 경계를 별개
축으로 취급하고 있다. **현재 실익 0, 미래 옵션으로만 계상.**

## 4. 비용 — 국소적이지만 실재한다

### 4.1 DLL마다 사본이 생기는 전역: 115개 이상

| 범주 | 수 | 내용 |
|---|---:|---|
| `Singleton<T>` 파생 | **36** | `s_instance`(atomic)·`s_mutex`가 T마다 템플릿 정적 |
| 리플렉션 등록 타입 | **77** | `adapt<T>()`의 함수-로컬 `static Meta::Type` |
| mutable inline 전역 | 2 | `EngineMode::s_mode`, `EngineBootstrap::g_exitCode` |

리플렉션이 특히 나쁘다 — `Registry`가 위 36개 중 하나라 **DLL마다 별도
사본**이고, 그러면 `nameIndex`/`idIndex`가 DLL 단위로 갈라진다. 직렬화·프리팹
해석이 모듈 경계마다 다른 타입 표를 보게 된다.

### 4.2 즉시 터지는 고위험 지점 2곳

`ScreenSizedResource.h`의 헤더 인라인 Meyers 싱글턴:

```cpp
static ScreenSizedRegistry& Get() { static ScreenSizedRegistry instance; return instance; }  // :83
static ScreenResizeBus&    Get() { static ScreenResizeBus    instance; return instance; }    // :137
```

이 둘은 이미 **7개 폴더 트리**(Editor/EngineEntry·EngineGUIWindow,
Engine/RenderEngine 3계층, Engine/SceneRuntime, Player)에서 호출된다. static
지역 변수의 COMDAT folding은 **하나의 PE 안에서만** 일어나므로, RenderEngine을
DLL로 떼는 순간 한쪽에서 등록한 리사이즈 구독자가 다른 쪽 브로드캐스트를 받지
못하는 조용한 결함이 된다.

⚠ **계획서가 이 지뢰를 아직 모른다** — EngineLayerSeparationPlan §2.2·2.3의
결합 인벤토리에 `ScreenSizedResource.h`가 등재돼 있지 않다.

### 4.3 반면 준비된 것도 있다

- Get() 싱글턴 10곳 중 **8곳은 `.cpp` out-of-line 정의** — export 매크로만
  얹으면 안전(`RuntimeSettings`·`ClrHost`·`ScriptObjectRegistry` 등)
- CRT 링크가 이미 vcpkg 전역 동적(`x64-windows-idl0.cmake`) — "할당한 모듈에서
  해제" 함정은 이미 회피
- 네이티브/CoreCLR 경계에서 POD + `static_assert(sizeof)` +
  `UnmanagedCallersOnly` 규율을 **이미 실천 중** — 같은 규율을 네이티브 DLL
  경계에 적용할 수 있다

### 4.4 잃는 것

- **LTCG 범위 절단**: 11개 vcxproj 전부 `WholeProgramOptimization` 적용.
  DLL 경계는 모듈 간 인라인·역가상화를 export 표면에서 끊는다.
- **즉시 실패 모드 상실**: 정적 링크는 심볼 누락이 링크 타임에 즉시 터진다
  (E5-1이 이 성질에 기대 Player 링크 안전을 사전 검증했다 —
  `EngineLayerSeparationPlan.md:1814-1816`). DLL은 런타임에만 드러난다.
- **export 표면 신설**: 엔진 자체 `__declspec(dllexport)` 관례가 **0건**.
  `FOO_API` 매크로 체계가 세워진 적이 없다.

## 5. 과거 이력 — 인과를 거꾸로 읽으면 안 된다

이 저장소는 네이티브 DLL을 두 번 걷어냈다. **둘 다 축 B를 반대하는 근거가
아니다.**

| 커밋 | 무엇을 | 실제 이유 |
|---|---|---|
| `fa7a055c` (8-18) | ManagedHeap.dll·SingletonManager.dll 삭제 (92파일) | 그 DLL이 **애초에 DLL 간 싱글턴 중복을 막던 장치**였는데, 전 모듈이 StaticLibrary가 되며 풀 문제가 사라져 순수 오버헤드만 남음 |
| `07a5adca` (8-7) | HotLoadSystem·ModuleBehavior 삭제 (C++ 핫리로드) | **CoreCLR이 대체를 마쳤으므로** — 축 A를 관리 계층으로 옮긴 결정 |

`ClassProperty.h`의 신설 주석이 첫 번째를 자백한다:

> SingletonManager.dll(DLLCore::Singleton)을 대체한다. 그쪽은 DLL마다 따로
> 생기는 정적 변수를 한곳으로 모으려고 타입 해시→void\* 표를 DLL이 들고 있었다.
> 그런데 엔진 모듈은 전부 StaticLibrary라 (…) 그 표가 풀던 문제가 존재하지 않는다.

**인과가 반대다.** "DLL 경계 때문에 싱글턴이 깨져서 걷어냈다"가 아니라 "DLL
경계가 이미 사라져서, 그걸 막던 장치가 쓸모없어져 걷어냈다"다. 삭제된
`ManagedHeapObject.h`가 스스로 "Base class for all objects that need to be
passed across EXE/DLL boundaries"라 적고 있었다.

**다만 남는 교훈은 있다**: 그 장치를 걷어낸 결과, 지금 코드베이스는 DLL을
재도입하면 즉시 부서지는 전역 115개를 무방비로 깔고 있다. 제거 작업이 문제의
소지까지 없앤 게 아니라 **"현재 링크 형태에서는 발생하지 않는다"에 기대고
있을 뿐**이다(§4.1).

## 6. 계획 공백 — 위임 사슬이 끊겨 있다

이 항목에 아무도 주인이 없다.

| 문서 | 이 질문을 어떻게 다루나 |
|---|---|
| `EngineLayerSeparationPlan` §1.2:37 | "Player.exe를 Player.dll로 바꾸는 일은 완료 조건이 아니다" — 범위 밖 |
| 〃 §3:129 | 목표 구조를 문자 그대로 "**Player.exe 또는 Player.dll**"로 미결 표기 |
| 〃 E7:1939 | "배포·Launcher 계획과 함께 결정" — **PHASE 23에 위임** |
| `EngineDistributionAndLauncherPlan` (PHASE 23) | `Player.exe`·`Player.dll`·`Core DLL`·`Core ABI`·`BuildNative` 문자열 **0건** — 위임받은 질문을 다루지 않음 |
| `BuildPipelinePlan` §2.1~2.3 | 위임 대상이 아닌데 **Player를 시종 "exe"로 확정 서술**하고 유니티식 결정 전체를 그 위에 세움 |
| `EnginePackagingPlan` | `Player` 문자열 0건 |

즉 **E7이 가리키는 포인터가 아무 데도 안 간다.** "배포 계획 연계"는 실체가
없는 위임이었다.

## 7. 판단

| 안 | 내용 | 비용 | 이득 | 판정 |
|---|---|---|---|---|
| 1 | **Core ABI/version provenance 먼저** | 중 — 버전 스탬프·매니페스트·검증 게이트 | `-BuildNative` 제거의 **유일한 열쇠**. DLL 없이도 §3.2 상당분 획득 | **선행 필수** |
| 2 | 엔진 코어 DLL화 (축 B) | 대 — export 표면 신설(관례 0), 싱글턴 2곳 정리, 리플렉션 Registry 단일화, LTCG 손실 | 배포 단위 분리·재컴파일 소멸 완성 | **안 1 이후** |
| 3 | Player.exe → Player.dll (셸만 뒤집기) | 소 | **없음** — 엔진 12만 줄은 여전히 정적 링크. 형태만 바꾸는 것 | **기각** |
| 4 | 전 Core를 하나의 거대 DLL | 대 | — | **기각** — 계획서 §1.2:39가 이미 명시 제외 |

**안 3을 명시적으로 기각하는 이유**: 질문을 문자 그대로 읽으면 "exe를 dll로"인데,
그것만으로는 아무 이득이 없다. 유니티의 이득은 `UnityPlayer.dll`이 **미리
빌드돼 게임마다 재사용된다**는 데서 나오지, 확장자가 `.dll`이라는 데서 나오지
않는다. 축 B의 본질은 파일 형식이 아니라 **재사용 가능한 배포 단위**다.

### 착수 조건

1. **Core ABI/version provenance 설계** — 어느 계획도 소유하지 않은 공백.
   PHASE 23에 편입하거나 신규 문서를 세운다.
2. **§4.2 지뢰 2곳 정리** — `ScreenSizedRegistry`·`ScreenResizeBus`를 out-of-line
   정의로. **DLL화와 무관하게 지금 해도 되는 위생 작업**이고, 계획서 결합
   인벤토리에 등재부터 해야 한다.
3. 수학 이주 WIP 착지 (RenderEngine 폴더 점유 해제).

### 즉시 실행 가능한 결론

- E7의 죽은 위임(→PHASE 23)을 **이 문서로 교체**한다.
- 착수 조건 2는 DLL 결정과 독립적이므로 언제든 별도 슬라이스로 가능하다.
- 축 B 자체는 **안 1이 서기 전까지 착수 금지** — 비용만 내고 이득은 못 받는다.

## 8. 조사 과정의 정정 기록

이 문서의 최초 결론 3건은 전부 적대적 검증에서 무너졌다. 남겨 둔다 — 같은
오판이 반복되기 쉬운 자리들이다.

| 최초 결론 | 왜 틀렸나 |
|---|---|
| "유니티가 DLL로 얻는 이득은 C#이 이미 충족" | 분리축을 하나로 뭉갰다. C#이 충족한 것은 축 A뿐(§1) |
| "저장소가 DLL 경계를 감당할 준비가 안 됐고, 그 이유로 걷어낸 전례가 있다" | 인과가 반대였다(§5). 위험도 "전면 미준비"가 아니라 국소 2곳(§4.2~4.3) |
| "게임 빌드는 이미 유니티식이라 재컴파일이 없다" | **목표 상태를 현재형으로 읽었다.** 오늘도 매 빌드 재컴파일한다(§3.1) |

세 번째가 가장 위험한 종류다 — 계획 문서의 "우리는 X다"라는 서술을 현재
상태로 읽었는데, 문서 자신은 바로 다음 문장에서 "다만 아직 아니다"라고
적고 있었다. **계획서의 선언형 문장은 목표일 수 있다. 코드로 확인해야 한다.**
