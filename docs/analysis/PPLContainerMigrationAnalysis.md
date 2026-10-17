# PPL 동시성 컨테이너 → STL 교체 분석

작성: 2026-08-08 · 목적: 외부 라이브러리 최소화 · 근거: 사용처 32건/16파일 전수 조사

## 요약

**전면 교체는 권고하지 않는다. 단, 현재 사용 중인 9개 지점 중 실제로 동시성이 필요한 곳은 2곳뿐이다.**

핵심 발견은 교체 가능성이 아니라 **현행 사용의 부정확성**이다.

- `parallel_for`·`parallel_invoke`는 코드베이스에 **한 건도 없다**. 병렬성은 전부 자체 `ThreadPool`이 담당한다.
- 렌더 큐 8종(`concurrent_vector`)은 **생산자가 없다** — `PushRenderQueue` 계열 3함수의 호출처가 코드베이스에 존재하지 않는다.
- `RenderScene.h`의 `concurrent_unordered_map.h`는 **죽은 include**다 — 실제 맵은 전부 `std::unordered_map`.
- `HLSLCompiler`는 동시성 컨테이너를 쓰면서 오히려 **레이스가 남아 있다**.

따라서 작업 순서는 "PPL → STL 치환"이 아니라 **죽은 것 제거 → 잘못 쓰는 것 교정 → 남은 것 판단**이다.

## 1. 사용처 전수 분류 — 생산·소비 스레드 실측 기준

각 지점의 push/pop 호출부를 역추적해 실제 스레드를 특정한 결과다. 초판의 "SPSC 추정"은
패턴만 보고 내린 것이라 이번에 근거를 붙여 재판정했다.

### 등급 A — 동시성 자체가 불필요 (제거 대상)

| 지점 | 근거 | 조치 |
|---|---|---|
| `RenderScene.h:27` `concurrent_unordered_map.h` | 죽은 include — 실제 맵은 전부 `std::unordered_map`(48·50·51·59행) | 즉시 제거 |
| `RenderPassData.h:17-18` `concurrent_vector<*Proxy*>` ×8 | **생산자 없음** — `PushRenderQueue` 계열 3함수의 호출처가 코드베이스에 0 (§2) | 판정 후 삭제 |
| `IRenderPass.h:30` `concurrent_queue<ID3D11CommandList*>` | DX11 디퍼드 컨텍스트 전용. 패스당 3×10=30 인스턴스 | DX11 은퇴 시 소멸 |

### 등급 B — 크로스 스레드지만 동시성 컨테이너가 과함

전부 **소수 push + 프레임당 1회 drain** 형태다. 락 경합이 성립하지 않는다.

| 지점 | 생산자 | 소비자 | 빈도 |
|---|---|---|---|
| `ProxyCommandQueue.h:13` | 게임 — `RenderSceneBridge.cpp` 8곳 | 렌더 — `EnhancedSceneRendererLive.cpp:2154` | 3프레임 링으로 시간 분리 |
| `EffectProxyController.h:13` | 게임 | 렌더 — `EffectManager.cpp:58` | 위와 동일 구조 |
| `EffectRenderProxy.h:8` | 게임 | 렌더 (`TryPop`) | 프록시별, 소량 |
| `DataSystem.h:118` | 메인 — 드롭 핸들러 `App.cpp:324` | 렌더 — `ImGuiRenderer.cpp:366` | **사용자가 파일을 끌어놓을 때만** |
| `RenderScene.h:160-161` | 프록시 소멸 지점 ×2 | 렌더 — `OnProxyDestroy()` | 파괴 시점만 |

### 등급 C — 동시성이 실제로 필요

| 지점 | 근거 | 비고 |
|---|---|---|
| `Core.ThreadPool.h:18` | 진짜 MPMC — 워커 N개 동시 pop | 부하는 낮다 (§5-정정) |
| `RenderPassData.h:19-20` `concurrent_vector<HashedGuid>` ×3 | 스레드풀 잡 6개가 같은 벡터에 동시 push (`Scene.cpp:552~620`) | 대안 있음 (§4) |
| `HLSLCompiler.h:81` | `BS::thread_pool`이 셰이더 로드를 병렬 실행 (§3) | **부팅 시 1회뿐** |

## 2. 렌더 큐 8종 — 생산자가 없다

`m_deferredQueue`·`m_forwardQueue`·`m_terrainQueue`·`m_foliageQueue`·`m_shadowRenderQueue`·
`m_UIRenderQueue`·`m_decalQueue`·`m_spriteRenderQueue`를 채우는 경로는
`PushRenderQueue` / `PushShadowRenderQueue` / `PushUIRenderQueue` 세 함수뿐인데,
이 함수들의 호출처는 `RenderPassData.h`(선언)와 `RenderPassData.cpp`(정의) 밖에 없다.

소비 측은 살아 있다 — DX12가 읽는다:

- `EnhancedSceneRenderer.cpp:2341-2342` — `copyQueue(renderData->m_deferredQueue, draws)`
- `EnhancedSceneRenderer.cpp:2422` — `m_UIRenderQueue`를 flat에 insert
- `EnhancedGizmoSceneTest.cpp:124-125` — 동일

**즉 DX12 라이브 경로는 항상 빈 큐를 읽고 있을 가능성이 높다.** DX11 은퇴 과정에서 채우는 쪽이
먼저 사라지고 읽는 쪽만 남은 형태다. 이건 컨테이너 교체 문제가 아니라 **파이프라인 결손**이므로
DX12 라이브 작업에서 먼저 판정해야 한다.

> 참고: 소비 측은 어차피 `std::vector`로 즉시 복사한다(`copyQueue`). 소비 경로에는
> 동시성 컨테이너가 필요 없다. `EnhancedUIPass.h:118-119` 주석도 같은 인식을 적어 두었다 —
> 포인터+개수로 받아 컨테이너 타입을 헤더가 모르게 했다.

### 부수 발견 — 실제 버그

`RenderPassData.cpp:286` — `SortShadowRenderQueue()`가 `m_deferredQueue.empty()`를 검사하고
`m_shadowRenderQueue`를 정렬한다. 검사 대상과 정렬 대상이 다르다.

## 3. HLSLCompiler — 동시성 컨테이너인데 안전하지 않다

```cpp
// HLSLCompiler.h:81-82
static concurrency::concurrent_unordered_map<std::string, ComPtr<ID3DBlob>> m_shaderCache;
//static std::mutex m_compileMutex;   // ← 주석 처리됨
```

세 가지가 겹쳐 있다.

1. **`operator[]` 대입이 원자적이지 않다.** `HLSLCompiler.cpp:76`의
   `m_shaderCache[key] = shaderBlob`은 슬롯 삽입만 스레드 안전하고, `ComPtr` 대입(참조 카운트
   조작)은 보호되지 않는다. 같은 셰이더를 두 스레드가 동시에 컴파일하면 그대로 레이스다.
2. **`find` → `operator[]` 사이에 창이 있다** (`HLSLCompiler.cpp:16-18`).
3. **`clear()`는 동시 안전하지 않다** (`CleanUpCache`, `HLSLCompiler.h:74`). PPL 문서상
   `clear`/`erase`는 단독 접근을 전제한다.

→ `std::unordered_map` + `std::mutex`로 되돌리는 것이 **기능적으로 더 안전하다**.
주석 처리된 뮤텍스를 되살리는 방향이다.

### 동시성의 출처 — 그리고 같은 파일 안의 불일치

이 캐시를 동시에 만지는 주체가 확인됐다. `ShaderSystem.cpp:44`의 `BS::thread_pool pool`이
`AddShaderFromPath`를 병렬 제출하고(69·78·86·96행), 그 안에서 `LoadFormFile`이 캐시를 읽고 쓴다.
**단, 이 병렬성은 `LoadShaders()` — 엔진 부팅 시 1회뿐이다.**

같은 흐름의 도착지인 `AddShader`는 이미 `std::mutex` 6개로 셰이더 타입별 맵을 보호한다
(`ShaderSystem.cpp:524~574`). 즉 **같은 병렬 구간에서 맵 6개는 뮤텍스로 지키고 캐시 맵 하나만
PPL로 지키는** 상태다. 뮤텍스로 통일하면 패턴이 일관되고 PPL 의존이 하나 준다.

### 부수 발견 — 외부 스레드풀 병존

`BS::thread_pool`(bshoshany/thread-pool)이 vcpkg로 이미 들어와 있다(`vcpkg-installed-ports.json:119`).
자체 `ThreadPool`이 있는데도 `ShaderSystem.cpp` 한 곳에서 지역 변수로 병행 사용 중이다.
외부 라이브러리 최소화 관점에서는 **PPL보다 이쪽이 먼저 걸린다** — 사용처가 한 곳뿐이라
자체 풀로 치환하는 비용이 작다.

같은 파일의 `m_shaderReloadThreadPool`(`ShaderSystem.h:71`)은 `Initialize`에서 생성만 되고
`Enqueue` 호출이 **0건**이다. `Finalize`가 delete하지 않아 누수이기도 하다.

## 3-0. 선행 질문 — 셰이더 로드 병렬화에 효과가 있는가

`LoadFormFile`은 확장자로 두 갈래로 갈린다. 성격이 완전히 다르다.

| 경로 | 호출 | 성격 | 실측 규모 | 병렬화 이득 |
|---|---|---|---|---|
| `.cso` | `D3DReadFileToBlob`(`HLSLCompiler.cpp:111`) | 디스크 읽기 + 메모리 복사 | 294개 / 9.1 MB | **작다** |
| `.hlsl` | `D3DCompileFromFile`(`:57`) | CPU 바운드 컴파일 | 147개 / 481 KB | **크다** |

분기는 `ShaderSystem.cpp:59~99`가 정한다 — cso가 존재하고 hlsl보다 최신이면 cso를 읽고,
아니면 컴파일한다. 따라서 **평시 실행은 거의 전부 `.cso` 경로**이고, 셰이더를 수정한 직후
첫 실행에서만 컴파일이 돈다.

→ 의문은 절반 타당하다. 평시에는 병렬화가 거의 값을 하지 않고(NVMe에서 9 MB 순차 읽기),
셰이더 수정 후 첫 실행에서는 147개 컴파일이 병렬로 갈라져 이득이 크다.
**벤치마크를 평시 상태에서 돌렸다면 측정한 것은 컴파일 성능이 아니라 I/O 편차일 수 있다.**

### 그보다 큰 문제 — 이 캐시는 아무도 읽지 않는다

`m_shaderCache`는 넣기만 하고 히트가 발생하지 않는다.

- `LoadShaders()` — 디렉터리를 순회하며 각 파일을 **1회씩만** 로드한다. 중복 경로가 없다.
- `ReloadShaders()` — `HLSLCompiler::CleanUpCache()`를 먼저 부르고(`ShaderSystem.cpp:129`)
  시작하므로 캐시는 항상 비어 있다.
- 개별 핫 리로드 경로는 없다 — `ReloadShaderFromPath`는 `ReloadShaders` 내부(162·166·171행)
  에서만 호출된다.

즉 캐시는 **블롭을 영구 보관하는 역할만** 한다. `AddShader`가 셰이더 객체를 만든 뒤 원본 블롭은
쓰이지 않으므로, 상주 메모리만 늘린다.

→ **PPL 컨테이너 교체를 논하기 전에 이 캐시가 필요한지부터 물어야 한다.** 캐시를 제거하면
§3의 레이스도 함께 사라진다. 남기려면 히트가 생기는 경로(개별 파일 감시 기반 핫 리로드)를
먼저 만들어야 하는데, 그때는 **캐시 무효화가 없으면 옛 블롭을 반환**하므로 키에 타임스탬프를
넣는 설계가 필요하다.

### 결정적 전제 — DX12는 이 경로를 전혀 쓰지 않는다

DX12 렌더러가 셰이더 바이트코드를 어디서 얻는지 확인한 결과, **DX11 셰이더 자산과 완전히
분리되어 있다.**

- DX12 패스 **24개 전부**가 `.cpp` 안의 raw string literal을 `D3DCompile`로 런타임 컴파일한다
  (`EnhancedGBufferPass.cpp:202`, `EnhancedDeferredPass.cpp:295`, `EnhancedForwardPass.cpp:690`,
  `EnhancedSkyBoxPass.cpp:94`, `EnhancedUIPass.cpp:119` 등).
- `RHI/DX12` 전체에서 `.cso` 읽기(`D3DReadFileToBlob`) **0건**, `HLSLCompiler::` 호출 **0건**,
  `ShaderSystem->` 호출 **0건**.
- `Material::GetShaderPSO()` 참조도 **0건** — `DX12PSOManager.h:16`의 설명 주석 한 줄이 전부다.
  ("ShaderPSO/VisualShaderPSO는 이름만 PSO인 '셰이더 묶음'이다. 진짜 PSO는 DX12에서…")

받을 자리는 이미 있다. `DX12GraphicsPipelineDesc`(`DX12PSOManager.h:28`)는 `vsBytecode`/
`psBytecode`를 `const void*` + 길이로 받아 **출처를 묻지 않는다**. 다만 지금은 그 자리에
인라인 컴파일 결과만 꽂힌다.

그리고 `ShaderSystem`은 `Shader.h`를 통해 `ID3D11Device::CreateVertexShader` 계열로 DX11 셰이더
객체를 만든다 — **디바이스 종속이라 DX12가 재사용할 수 없는 구조**다.

**따라서 이 절과 §3의 리팩터링(캐시 제거·PPL 교체)은 지금 하면 낭비다.** 두 갈래 중 어느
쪽이든 이 코드는 유지 대상이 아니다.

- DX11이 은퇴하면 → `HLSLCompiler`·`ShaderSystem`·`m_shaderCache`가 통째로 사라진다.
- DX12가 머티리얼 셰이더를 받게 되면 → 필요한 것은 **바이트코드뿐**이므로(디바이스 객체 생성
  불필요) 현재 경로를 재사용할 수 없고 새로 만들어야 한다.

남는 것은 **PDB 덮어쓰기 버그**(아래)뿐이다. 그건 DX11이 살아 있는 동안 계속 물릴 수 있으므로
독립적으로 고칠 값이 있다.

### 파생 문제 — 셰이더 자산과 머티리얼이 DX12에서 끊겨 있다

이건 PPL 논의 밖이지만 같은 조사에서 드러났으므로 적어 둔다.

1. **147개 hlsl / 294개 cso 자산이 DX12에서 미사용**이다. DX11 은퇴 시점에 이대로면 자산이
   통째로 버려진다.
2. **머티리얼이 지정한 커스텀 셰이더(ShaderDSL·VisualShader)가 DX12에서 무시된다.** 모든 드로우가
   패스에 하드코딩된 셰이더로 그려진다.
3. 셰이더 핫 리로드도 DX12에서는 의미가 없다.
4. 매 실행마다 24개 패스 셰이더를 런타임 컴파일하므로 그 비용이 부팅 시간에 실린다 —
   사전 컴파일(cso) 도입 후보지만, 그때 필요한 것은 지금의 `HLSLCompiler`가 아니라
   **바이트코드 로더**다.

§2의 "렌더 큐에 생산자가 없다"와 같은 결의 결손이다 — DX12가 아직 자기 소스로만 그리는 단계다.

### 부수 발견 — PDB가 CSO를 덮어쓴다

`HLSLCompiler.cpp`의 두 경로가 **완전히 같은 파일명**을 만든다.

```cpp
:78  std::string csoPath = ...RelativeToPrecompiledShader() + filePath.stem() + ".cso";
:97  std::string pdbPath = ...RelativeToPrecompiledShader() + filePath.stem() + ".cso";  // ← .pdb가 아니다
```

DEBUG 빌드에서 hlsl을 컴파일하면 방금 쓴 cso를 디버그 블롭으로 덮어쓴다. 다음 실행은 그 파일을
셰이더로 읽는다. `_DEBUG` 한정이므로 릴리스에서는 드러나지 않는다.

## 3-1. BS::thread_pool 교체가 중단된 이유 — 가설과 검증법

`m_shaderReloadThreadPool`은 죽은 멤버가 아니라 **중단된 마이그레이션의 잔해**다.
경위: 벤치마크에서 BS 쪽이 우세해 교체를 진행하다가 크래시가 나서 되돌렸고, 원인은 미규명
상태로 남았다(마감 압박). `LoadShaders()`만 BS로 넘어간 채 멈춘 것이 현재 모습이다.

먼저 **배제된** 후보부터. `SetThreadInitCallback`/`SetThreadExitCallback`은 `Core.ThreadPool.h`에
정의만 있고 **호출처가 0건**이다. 따라서 "자체 풀은 워커에서 COM을 초기화했는데 BS는 안 했다"는
설명은 성립하지 않는다 — 양쪽 다 워커에 COM 초기화가 없다(`CoInitializeEx`는 `Dx11Main`·
`GameMain`의 CB/CE 전용 스레드에만 있다).

### 가설 1 (가장 유력) — `NotifyAllAndWait`이 사실상 전역 배리어였다

`SceneManagers->m_threadPool`은 **공유 풀**이다. 프레임 컬링(`Scene.cpp`), 지형 저장
(`Terrain.cpp`), 모델 로드(`ModelSceneBridge.cpp`)가 전부 같은 인스턴스를 쓴다.

```cpp
void NotifyAllAndWait() {
    if (m_taskCounts.load(std::memory_order_acquire) == 0) return;
    WaitForSingleObject(m_waitEvent, INFINITE);
}
```

이 대기는 **전역 카운터 기반**이라 누가 넣은 작업인지 구분하지 않는다. `Terrain`이 부른
`NotifyAllAndWait`은 `Scene`의 컬링 잡까지 기다리고, 그 반대도 성립한다. 즉 호출자가 의도한
것보다 **넓은 배리어**로 동작하며, 서브시스템 간 순서를 우연히 지켜주고 있었다.

BS로 옮기면서 `submit_task`가 돌려주는 `future`로 대기를 바꾸면 의미론이 **내 작업만 대기**로
좁아진다. 넓은 배리어에 기대고 있던 코드가 있었다면 이 전환에서 그대로 깨진다 —
기억에 남은 "설계 차이"와 부합한다.

전환 흔적이 코드에 그대로 남아 있어 이 가설을 뒷받침한다:

```cpp
// ShaderSystem.cpp:103~105
// 3) 모든 제출 작업 완료 대기 (NotifyAllAndWait 대체)
for (auto& f : futures) f.get();
// 또는 pool.wait(); // 풀에 들어간 모든 작업을 기다릴 때
```

"NotifyAllAndWait 대체"라는 주석과, 전역 대기(`pool.wait()`)를 대안으로 남겨둔 흔적이
곧 두 의미론 사이에서 고민한 자국이다.

**검증법**: `NotifyAllAndWait` 호출부 4곳(`Scene.cpp:677`, `Terrain.cpp:479`,
`ModelSceneBridge.cpp:34,55`)에서, 대기 직전에 자신이 넣지 않은 작업이 큐에 남아 있는지 로그로
센다. 남아 있다면 그 호출부는 전역 배리어에 의존하고 있다.

### 가설 2 — 워커 우선순위 차이로 잠재 레이스가 드러났다

자체 풀은 워커를 `THREAD_PRIORITY_HIGHEST`로 만든다(`Core.ThreadPool.h:20`). BS는 기본
우선순위다. 또 스레드 수 산정도 다르다 — 자체는
`GetActiveProcessorCount(ALL_PROCESSOR_GROUPS)`, BS는 `hardware_concurrency()`(MSVC에서는
현재 프로세서 그룹만 센다). 둘 다 실행 순서와 병렬도를 바꾸므로, **원래 있던 레이스가
BS에서만 드러났을** 수 있다. 이 경우 진짜 원인은 BS가 아니라 그 레이스다.

**검증법**: 자체 풀의 우선순위를 `THREAD_PRIORITY_NORMAL`로, 스레드 수를 BS와 같게 맞춘 뒤
재현되는지 본다. 재현되면 원인은 BS가 아니다.

### 가설 3 — 자체 풀의 `ResetEvent` 레이스 (BS와 무관하게 존재)

```cpp
int prev = m_taskCounts.fetch_add(1, std::memory_order_relaxed);
if (prev == 0) ResetEvent(m_waitEvent);   // ← fetch_add와 원자적이지 않다
m_tasks->push(std::move(task));
```

`fetch_add`와 `ResetEvent` 사이에 워커가 직전 작업을 끝내고 `SetEvent`를 부르면, 뒤늦은
`ResetEvent`가 그 신호를 덮어쓴다 → `NotifyAllAndWait` 무한 대기. 다중 생산자에서 성립한다.
§5-정정의 permit 유실과는 별개 경로다.

이건 자체 풀 쪽 결함이므로 BS 교체의 원인은 아니지만, **재현 실험 중 잡음이 되므로 먼저
막아두는 편이 낫다.**

### 재개 시 순서

1. 가설 2를 먼저 친다 — 자체 풀의 우선순위·스레드 수만 BS와 맞춰 재현 시도. 가장 싸고,
   재현되면 BS는 무죄이며 진짜 레이스를 잡는 문제로 바뀐다.
2. 재현되지 않으면 가설 1 — 대기 의미론. 위 로그 계측으로 전역 배리어 의존을 찾는다.
3. 그 전에 가설 3을 막아 잡음을 줄인다.

## 4. 컬 데이터 버퍼 — 동시성이 진짜 필요한 유일한 곳

`Scene.cpp:538-620`에서 카메라마다 스레드풀 잡을 6개 띄우고, 각 잡이 같은
`RenderPassData`의 `m_findProxyVec[index]` / `m_findShadowProxyVec[index]`에 push_back한다.
static·skinned·terrain·foliage·decal·sprite 잡이 **동시에 같은 벡터에 쓴다**.

여기서 `concurrent_vector`는 정당하다. 다만 두 가지 대안이 더 낫다.

- **대안 A — 잡별 스레드 로컬 수집 후 병합**: 각 잡이 자기 `std::vector`에 모으고 마지막에
  한 번 병합. 동시성 자체를 없앤다. 병합 비용은 프록시 수백 개 수준에서 무시할 만하다.
- **대안 B — 잡 종류별로 벡터 분리**: 6개 잡이 각자의 벡터를 가지면 경합이 사라진다.
  소비 측은 6개를 순회하면 된다.

둘 다 `std::vector`의 연속 메모리를 회복한다. `concurrent_vector`는 세그먼트 기반이라
순회·정렬에서 캐시 지역성이 나쁘다 — 실제로 `RenderPassData.cpp:251,260,288,310`에서
`std::ranges::sort`를 걸고 있다.

### `clear()` 문제는 교체해도 남는다

종료 크래시 스택(`Tools/regression/SHUTDOWN-CRASH-NOTES.md:53-56`)은
`concurrent_vector::clear`와 `concurrent_queue::push`가 파괴된 객체를 만진 흔적이다.
이건 **수명 관리 문제**이지 컨테이너 선택 문제가 아니다. STL로 바꿔도 동일하게 터진다.
교체를 크래시 해결책으로 기대해서는 안 된다.

## 5. 교체의 손익

**얻는 것**

- MSVC 전용 의존 제거 — PPL은 표준이 아니고 MSVC에만 있다. 이식성 확보.
- `concurrent_vector` → `std::vector`: 연속 메모리, 캐시 지역성, 정렬 성능.
- 락이 코드에 드러난다. 지금은 "동시성 컨테이너를 썼으니 안전하다"는 착시가 있고,
  §3이 그 착시의 실례다.
- `using namespace concurrency;`가 헤더에 퍼져 있는 문제(`IRenderPass.h:29`,
  `RenderPassData.h:8`, `RenderScene.h:29`, `ProxyCommandQueue.h:7`)도 함께 해소된다.

**잃는 것**

- `ThreadPool`의 MPMC 큐는 직접 구현해야 한다 — `std::deque` + `mutex` + `condition_variable`.
- `try_pop` 같은 비블로킹 API를 손으로 만들어야 한다.

### 정정 — 작업 큐는 고빈도가 아니다

초판에서 `ThreadPool` 큐를 "유일하게 락프리가 값을 하는 자리"로 적었으나, 호출 빈도를 세어 보니
근거가 약하다. `Scene.cpp:538~661`이 카메라당 `Enqueue` 10건, 카메라 3~10개면 프레임당 30~100잡 —
60fps 기준 **초당 2천~6천 회**다. 뮤텍스 하나로 세 자릿수 여유가 있는 영역이다. 나머지 풀(셰이더
리로드·에셋 로드·애니메이션)은 더 한산하다.

또한 현재 구조는 큐 push와 세마포어 release를 따로 센다(`Core.ThreadPool.h:75-77`). 워커가
`acquire()` 후 `try_pop`에 실패하면 permit이 유실되고 `m_taskCounts`가 줄지 않아
`NotifyAllAndWait()`이 무한 대기할 수 있다 — `Scene.cpp:677`·`Terrain.cpp:479`·`ModelSceneBridge`가
프레임 동기화 지점에서 이를 호출한다. 락프리 큐로 교체하면 이 창은 오히려 넓어진다
(생산자별 서브큐를 두는 구현은 큐가 비지 않아도 pop이 실패할 수 있다).
뮤텍스+`condition_variable`은 큐 상태와 대기를 한 락 아래 묶어 이 문제를 구조적으로 없앤다.

**중립**

- SPSC 성격의 프레임 링 큐들(#4~#8)은 애초에 경합이 거의 없다. 어느 쪽이든 성능 차이가
  드러나지 않는다. 교체 판단은 성능이 아니라 일관성 기준으로 하면 된다.

## 6. 권고 — 단계

교체 자체보다 앞서야 할 정리가 있다.

**등급 A부터 — 교체가 아니라 삭제다.**

1. **죽은 include 제거** — `RenderScene.h:27`. 위험 없음.
2. **렌더 큐 8종 판정** — 생산자 결손이 의도인지 결손인지 DX12 라이브 작업에서 확인.
   결손이면 파이프라인 수정이 먼저이고, 폐기라면 멤버 8개와 함수 3개를 삭제한다.
   `SortShadowRenderQueue` 버그(§2)도 이때 함께.
3. **`m_shaderReloadThreadPool` 삭제** — 죽은 멤버 + 누수(§3). PPL과 무관하지만 같은 파일이다.

**그다음 등급 C의 오작동 교정.**

4. ~~**HLSLCompiler 교체**~~ — **철회.** DX12가 이 경로를 전혀 쓰지 않고(§3-0), `ShaderSystem`이
   DX11 디바이스에 묶여 있어 어느 쪽으로 가든 유지 대상이 아니다. 여기서 손댈 것은
   **PDB 덮어쓰기 버그 하나뿐**이며, 그것도 PPL과 무관한 독립 수정이다.
5. **컬 데이터 버퍼** — §4 대안 A 또는 B. `std::vector`의 연속 메모리 회복.
   현재 등급 C 중 **유일하게 손댈 값이 있는 항목**이다.

**마지막이 등급 B — 순수 치환이라 이득이 가장 작다.**

6. **크로스 스레드 큐 5종** — 얇은 래퍼(`FrameQueue<T>`) 하나로 일괄 치환.
   `push`/`try_pop`/`empty`만 있으면 현재 호출부가 그대로 통과한다.
7. **`IRenderPass::CommandQueue`** — 손대지 않는다. DX11 은퇴와 함께 사라진다.
8. **`ThreadPool`** — 큐 교체 전에 permit 유실(§5-정정)부터 막고, 프레임 시간 기준선을 잡는다.
   외부 라이브러리 도입은 뮤텍스 버전이 실제로 밀릴 때 검토해도 늦지 않다.

1~5까지만 해도 PPL 의존 지점은 9곳에서 5곳으로 줄고, 그 과정에서 실제 결함 3건
(생산자 결손·캐시 레이스·죽은 멤버 누수)이 함께 정리된다. 6은 그 뒤에 해도 되고,
안 해도 기능적으로 잃는 것이 없다.

## 7. 연계

- 렌더 큐 판정은 [MultiCameraRenderPlan.md](../plans/archive/MultiCameraRenderPlan.md) / DX12 라이브 파이프라인 작업과 한 몸.
- `using namespace concurrency;` 헤더 오염은 [Phase5CouplingPlan.md](../plans/archive/Phase5CouplingPlan.md)의 인용 경로 규칙과 같은 결.
- 종료 크래시 수명 문제는 [Tools/regression/SHUTDOWN-CRASH-NOTES.md](../../Tools/regression/SHUTDOWN-CRASH-NOTES.md) — 컨테이너 교체로 해결되지 않음.
