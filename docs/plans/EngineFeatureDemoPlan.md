# 엔진 피처 데모 계획 · CQB FPS (PHASE 25)

2026-09-06 최초 작성. 리팩토링 대시보드의 열린 트랙이 전부 닫힌 시점을 전제로,
포트폴리오 산출물이 될 단일 데모를 정의하고 그 데모가 소비할 피처를 역으로 명세한다.

상태: **설계 기준선 확정 · 착수 조건 미충족(선행 트랙 184항목 잔여).**

---

## 0. 결정 요약

1. **데모는 하나다. 레벨도 하나다.**
   같은 무대를 플레이 슬라이스와 기술 쇼케이스 두 겹으로 쓴다. 새 레벨을 만드는
   비용이 데모 전체에서 가장 크므로, 무대를 늘리는 대신 그 위에 얹는 관측 층을 늘린다.

2. **장르는 실사풍 CQB(Close Quarters Battle) FPS.**
   1인칭이라 전신 캐릭터 애니메이션 저작이 불필요하고(손·무기만), 좁은 실내라
   경로탐색·LOD·스트리밍 요구가 낮으며, 실사 재질과 국소 조명이 화면을 채운다.

3. **이 데모는 남은 트랙의 소비자다.**
   CQB는 하필 지금 없는 것들(국소 그림자·파티클·실내 반사·자동 노출)을 정확히 요구한다.
   따라서 §2의 매핑 표는 데모 요구사항 목록인 동시에 **남은 트랙의 우선순위 근거**다.
   데모가 안 쓰는 트랙은 데모 일정에서 선행이 아니다.

4. **리팩토링이 다 끝나도 데모는 못 만든다.**
   §3의 갭 5종은 어느 계획서도 소유하지 않은 항목이다. 이것을 신설하지 않으면
   대시보드가 100%가 되어도 착수할 수 없다. 이 계획서의 핵심 산출물은 그 목록이다.

5. **데모 제작 자체의 예산은 2~3주(순수 작업일 15~20일)다.**
   선행 트랙 공수는 여기에 포함하지 않는다. 포함하면 이 문서는 일정이 아니라 소설이 된다.

한 줄로 줄이면:

> **한 레벨 CQB FPS + 관측 3종. 데모가 트랙 우선순위를 정하고, 갭 5종이 착수 조건이다.**

---

## 1. 데모 정의

### 1.1 무대

좁은 반실외 구조물 한 채. 창과 천창으로 방향광이 들어오고, 내부는 국소 광원으로
채운다. 실내 전용으로 닫지 않는 이유는 §2의 조명 항목에 있다 — 방향광 캐스케이드가
이미 완성돼 있어 창광(窓光)이 공짜인 반면, 국소 그림자는 선행 트랙에 의존한다.
개활지를 두지 않는 이유는 LOD·스트리밍·경로탐색을 전부 부르기 때문이다.

권장 규모: 방 4~6개, 시야가 20 m를 넘지 않는 동선, 재질 12종 이하.

### 1.2 플레이 (3분)

1. 진입 — 조작 학습(이동·조준·사격)이 자연히 일어나는 복도
2. 첫 교전 — 근거리 1체, 사격 피드백(탄흔·머즐 플래시·반동·히트 리액션) 확인
3. 개방 공간 — 2~3체 동시 교전, 엄폐물 사용, 광원 파괴로 조명 변화
4. 퇴출 — 창광이 강한 공간으로 나오며 자동 노출이 눈에 띄게 작동

### 1.3 기술 모드 (같은 레벨, 토글)

플레이 중 언제든 켜고 끌 수 있어야 한다. 별도 씬을 만들지 않는다.

- **T-A 렌더 패스 분해 뷰어** — GBuffer 채널(baseColor/normal/ORM/emissive/AO),
  SSAO, SSGI, 그림자 캐스케이드, 볼류메트릭 포그를 단계별로 표시
- **T-B 프로파일러 오버레이** — FPS, CPU 프레임 구간, 드로우 수, 광원 수, 배치 수
- **T-C 라이브 C# 리로드 시연** — 실행 중 게임 스크립트를 고쳐 즉시 반영

백엔드 실시간 전환은 **설계상 불가능하므로 기술 모드에서 제외한다**
(`EnhancedSceneRenderer.h:543` — 백엔드는 `InitializeRuntime`에서 고정되고 실행 중
변경 API가 없다). DX12/Vulkan 대비가 필요하면 두 프로세스를 나란히 띄운 스크린샷
비교로 문서에 싣는다. 단 Vulkan 표시 경로는 매 프레임 CPU 리드백 왕복이므로
영상·플레이 캡처는 DX12로 한다.

---

## 2. 데모가 소비하는 트랙

각 행은 "데모의 이 연출이 저 트랙을 필요로 한다"는 의존이다. 현 상태는 2026-09-06
코드 실측이다.

### 2.1 선행 필수 — 없으면 데모가 성립하지 않는다

| 데모 요구 | 필요 피처 | 현 상태 | 소유 트랙 |
|---|---|---|---|
| 실내 국소 조명의 접지 그림자 | 포인트·스포트 섀도우 | **없음.** 방향광 3캐스케이드만, 그것도 가장 센 방향광 하나(`EnhancedShadowPass.cpp:172`) | RND-2 |
| 머즐 플래시·탄피·연기·먼지 | 파티클/VFX | **없음.** `EffectSystem` 54파일이 솔루션에서 제거됨, `Particle` 심볼 0건 | PHASE 10 |
| 실내 금속·유리의 반사 | 로컬 리플렉션 프로브 | **없음.** `ReflectionProbe` 심볼 0건 | RND-1 |
| 실내↔창밖 이동 시 노출 적응 | 자동 노출 | **없음.** 수동 스칼라 `exposure = 0.7` 고정, 휘도/히스토그램 패스 0건 | RND-3 |
| 탄약·체력 HUD, 상호작용 프롬프트 | 텍스트 렌더링 | **없음.** 폰트 라이브러리·폰트 자산 0, UI 패스가 텍스트 프록시를 버림(`EnhancedUIPass.cpp:83`) | UI 트랙 T |
| 실사 PBR 재질 | BC5/BC7, 밉, 자산별 임포트 설정 | **BC1/BC3만.** Normal·ORM·AO는 RGBA8 무압축, `.meta`에 색공간·압축·밉 어휘 없음 | PHASE 12 |
| 배포된 exe에서 룩 유지 | 패키지 렌더 설정 배선 | **죽은 경로.** `IsVolumeProfileApply()` 독자 0건 → Player는 하드코딩 HDRI·기본값으로만 그림 | §3 갭 ① |
| 총성·발소리·잔향 | 오디오 백엔드 | FMOD 의존, miniaudio 전환 예정 | AudioBackend |

### 2.2 이미 완성 — 데모가 그대로 소비한다

이 목록이 데모 컨셉을 CQB로 고정한 근거다.

| 피처 | 상태 | 근거 |
|---|---|---|
| IBL (equirect → 큐브맵 → irradiance → prefilter 6밉 → BRDF LUT) | 켜짐, HDRI 19장 보유 | `EnhancedSceneRenderer.cpp:3453`, split-sum 소비 `Deferred.slang:213` |
| PBR 5텍스처 (baseColor/normal/ORM/emissive/**독립 AO**) | 켜짐 | `GBuffer.slang:74-79` |
| 방향광 3캐스케이드 그림자 (PSSM, 블렌드 밴드) | 켜짐 | `EnhancedShadowPass.h:46` |
| AgX 톤매핑 · 블룸 피라미드 · FXAA · 비네트 | 켜짐(기본 AgX) | `EnhancedPostChainPass.h:115-165` |
| SSAO · SSGI(뷰별 히스토리·재투영) | 항상 켜짐 | 노드 `EnhancedSceneRenderer.cpp:1963, 2038` |
| 볼류메트릭 포그 · SSR · SSS | 코드 완비, 기본 OFF | 뷰당 포그 42 MB |
| Forward+ 타일 컬링 (16px/타일, 32광원/타일, 씬 64광원) | 켜짐 | `EnhancedForwardPass.h:95`, `EnhancedDeferredPass.h:27` |
| 데칼 패스 (탄흔) | 켜짐 | 노드 `:1936` |
| 물리 쿼리 (Raycast/RaycastAll/OverlapSphere) + 충돌·트리거 콜백 6종 | C# 완비, 무할당 `Span<T>` | `ScriptCore/Physics.cs:51-70`, `ScriptRegistry.cs:566` |
| CCT 이동·회전·강제이동(대시·넉백) | C# 완비 | `ScriptCore/CharacterControllerComponent.cs:17-72` |
| 애니메이션 상태머신·블렌딩·레이어·아바타 마스크 + 노티파이 | 엔진 저작 + C# 파라미터 제어 | `AnimationController.h:49-92`, `AnimationEventBridge.cpp:145` |
| 카메라 FOV 제어 (ADS 전환) | C# 완비 | `ScriptCore/CameraComponent.cs:24` |
| 32비트 인덱스 (고밀도 메시) | 전 경로 R32Uint | `DX12MeshCache.cpp:403` |
| mikktspace 탄젠트 (UV1·비균등 스케일 대응) | 완비 | `Includes/MaterialEvaluation.slang:14-47` |
| exe + pak 독립 배포 | **전례 실물 존재** | `Build/Staging/`, 스모크 1,323프레임 exit 0, 3중 digest |
| 라이브 C# 리로드 (기술 모드 T-C) | LC7 완료 | — |
| Player 내 ImGui (기술 모드 T-A·T-B의 표시 층) | 이미 매 프레임 구동 | `Player/PlayerMain.cpp:575` |

### 2.3 데모가 쓰지 않는 트랙

아래는 완료 여부와 무관하게 이 데모의 선행이 아니다. 데모 일정에서 제외한다.

- 네트워크 N7~N10 — 싱글플레이 데모
- 지형 DX12 재설계(PHASE 11) — 실내 무대, 지형 렌더 패스 자체가 없음
- 라이트맵 베이커 — 실시간 SSGI + IBL로 대체
- Blender Material Graph(MAT-6) — 재질 12종은 손 저작으로 충분
- MAT-4/5 (coat·sheen·이방성·투과·SSS 레이어) — CQB 재질(콘크리트·금속·목재·천)에 불필요
- 에디터 워크스페이스 재설계(PHASE 21) — 저작 편의이지 산출물 요건이 아님
- 시뮬레이션 효과 계약(PHASE 24) — 있으면 AI 시퀀스가 편해지나 대체 가능

---

## 3. 리팩토링 완료 후에도 없는 것 — 갭 5종

**이 절이 이 문서의 핵심이다.** 아래는 대시보드의 어느 트랙도 소유하지 않은 항목이라,
184개 항목이 전부 닫혀도 여전히 없다. 데모 착수 조건으로 신설해야 한다.

### 갭 ① 패키지 렌더 설정이 렌더러에 도달하지 않는다 — **최우선**

`EngineSettings.runtime.yml`이 skybox HDRI·SSGI·포그·톤맵·블룸·AA·색보정을 담아
pak에 실리지만, 소비 사슬이 **쓰기 전용 플래그에서 끝난다**.

```
VolumeComponent.cpp:11  →  RuntimeSettings::SetRenderPassSettings
                        →  SceneManager::VolumeProfileApply()   [SceneManager.cpp:1358]
                        →  m_volumeProfileApply = true
                        →  IsVolumeProfileApply() 독자 0건 ✗
```

`EnhancedSceneRenderer`는 `GetRenderPassSettings()`를 한 번도 호출하지 않는다.
`SetSkyBoxPath`의 유일한 호출자는 에디터 UI 버튼(`SceneViewWindow.cpp:713`)이다.

**결과**: 배포된 Player는 항상 하드코딩된 `kloofendal_43d_clear_puresky_4k.hdr`
(`EnhancedSceneRenderer.cpp:4237`)와 하드코딩된 `EnhancedLiveTuning` 기본값
(AgX, exposure 0.7, 포그 OFF, SSR OFF)으로만 그린다. **에디터에서 맞춘 룩이
exe로 뽑는 순간 통째로 날아가고, SSR·볼류메트릭 포그는 Player에서 켤 수단이 없다.**

- 작업: `RenderPassSettings → EnhancedLiveTuning` 값 매핑, `VolumeProfileApply` 소비자
  신설, `SetSkyBoxPath` 런타임 경로. 게임→렌더 스레드 전달은 `SetLiveTuning`이 이미 있다.
- 난이도: 중간. **기술 모드 T-A(렌더 패스 분해)와 같은 배관을 공유하므로 함께 연다.**
- 판정: 패키지된 Player에서 씬별 HDRI가 바뀌고 포그·SSR이 켜진다.

### 갭 ② 커서 락 — FPS의 전제

`MouseDelta`는 읽히지만 커서를 창에 가둘 방법이 C#·네이티브 양쪽에 없다
(`Native.cs:205`에 `Input_SetCursorVisible`만 존재). `MousePosition` setter도 없어
중앙 복귀 우회조차 불가능하다. **마우스룩 중 커서가 창을 벗어나면 델타가 끊긴다.**

- 작업: `Input_SetCursorLocked` 네이티브 함수 + C# 표면. 창 포커스 상실 시 자동 해제.
- 난이도: 작음(반나절). 게임패드 우스틱(`GetThumbRight`)이 임시 회피책이나
  FPS 포트폴리오에서 패드 전용 조작은 부적절하다.

### 갭 ③ CCT 점프가 끊겨 있다

`CharacterMovement.cpp:71`이 `input.y != 0 && !m_isFall`일 때 `Jump()`를 부르는데,
`input.y`를 채우는 유일한 자리가 **주석 처리돼 있다**:

```cpp
// CharacterControllerComponent.cpp:42-45
/*if(m_isKnockBack)
{
    input.y = JumpPower;
}*/
```

현재 빌드에서 CCT 점프는 어떤 경로로도 발화하지 않는다. 중력 계수
(`CharacterMovement.h:40 SetGravityWeight`)도 C#에 미노출이다.

- 작업: 네이티브 경로 복원 + `Cct_Jump` 함수 + 점프력·중력 노출.
- 난이도: 작음~중간(반나절~1일). CQB에서 점프 비중은 낮으므로, 예산이 부족하면
  **점프 없는 데모로 범위를 줄이는 것이 정당한 절단**이다.

### 갭 ④ 물리 레이어 쓰기 API

`RaycastHit.Layer`를 읽고 `layerMask`로 거를 수는 있는데, **C#에서 오브젝트의 레이어를
지정할 수 없다**(`ScriptCore` 전체에 Layer setter 0건). FPS 사격 판정에서 적/엄폐물/
플레이어/트리거를 구분하는 표준 수단이 막힌다.

- 작업: `Entity.Layer` 또는 `Collider.Layer` setter.
- 난이도: 작음. 우회는 `GetComponent<T>() is not null` 판별이나 레이캐스트 필터링이
  안 되어 성능·정확도 모두 손해다.

### 갭 ⑤ 노멀맵 그린 채널 규약 — 무료 자산 도입의 관문

엔진은 **OpenGL(Y+) 규약 고정**이고 반전 코드가 저장소에 0건이다
(`MaterialEvaluation.slang:71`, `GBuffer.slang:148`). Fab·Megascans의 기본 다운로드는
**DirectX(Y-)**라 그대로 넣으면 **조명이 뒤집힌다.**

- 작업(택1): ⓐ 자산을 Y+ 변형으로만 받는 운영 규약 — 비용 0, 자산 선택 제약
  ⓑ `.meta`에 노멀맵 플래그 + cook 시 G 반전 — PHASE 12 T0과 함께
- 난이도: ⓐ 0 / ⓑ 중간. **데모에서는 ⓐ로 간다.** ⓑ는 PHASE 12의 정식 범위.

### 갭 외 — 명시적 비범위

- **경로탐색(NavMesh/Recast/Detour)**: 저장소 전체 0건. CQB는 좁고 시야가 짧아
  직선 추적 + `Raycast` 엄폐 판정으로 성립한다. **데모에서 신설하지 않는다.**
- **루트모션**: 저장소 전체 0건. 1인칭이라 플레이어 이동에 무관하고, 적의 전진 공격은
  `TriggerForcedMove`로 대체한다.
- **TAA**: 모션 벡터 G-buffer가 없어 큰 작업이다. FXAA로 간다.

---

## 4. 데모 제작 단계

선행(§2.1 트랙 + §3 갭)이 닫힌 뒤 착수한다. 예산 15~20일.

| 단계 | 내용 | 산출·판정 | 일 |
|---|---|---|---|
| **DM0** | 착수 조건 검증 — §2.1·§3 항목을 하나씩 실제로 확인. 특히 갭 ①을 패키지된 Player에서 검증 | 체크리스트 전항 통과. 하나라도 미충족이면 착수하지 않는다 | 1 |
| **DM1** | 자산 도입 — Fab 무료(Standard, 엔진 제한 없음, FBX/glTF 소스 동봉, **노멀맵 Y+**) 재질 12종·프롭 20종. 임포트·cook·GUID 안정성 확인 | `CREDITS.md`, 전 자산 재임포트 후 씬 참조 유지 | 2~3 |
| **DM2** | 레벨 블록아웃 — 프리미티브로 동선·시야·엄폐물 확정. 조명 배치(창광 방향광 1 + 국소 4~6) | 플레이 가능한 회색 상자 레벨 | 2 |
| **DM3** | 플레이어 — 1인칭 이동·마우스룩·ADS·사격(히트스캔)·반동·탄약. 손/무기 뷰모델 | 조작 완결. 커서 락·레이어 필터 실동작 | 2~3 |
| **DM4** | 전투 — 적 3체, 피격 리액션, 사망. 탄흔 데칼·머즐 플래시·연기. AI는 `Component` + `OnSimulate` 시퀀스(추적→사격→재장전→엄폐) | 교전이 성립 | 3 |
| **DM5** | 룩 마감 — 재질 배치, IBL·톤매핑·노출 튜닝, 포그·SSR·국소 그림자, 사운드 | 스크린샷 10장이 포트폴리오 품질 | 3 |
| **DM6** | 기술 모드 T-A/T-B/T-C — Player ImGui 위에 구현 | 플레이 중 토글 동작 | 2 |
| **DM7** | 패키징·녹화·문서 — exe 배포판, 3분 플레이 영상, 기술 모드 영상, 기술 문서 | 클린 머신에서 실행. 셰이더 컴파일 워밍업 후 캡처 | 2 |

**절단 우선순위** (예산 초과 시 뒤에서부터 버린다):
DM6 T-A → DM4 적 3체를 1체로 → DM3 점프 → DM5 사운드.
**DM7은 절대 자르지 않는다.** 실행 가능한 exe와 영상이 없으면 포트폴리오가 아니다.

---

## 5. 산출물

1. **실행 가능한 exe 배포판** — 클린 머신에서 동작. 현재 패키지는 slang/dxc/dxil
   57 MB를 싣고 **Player가 런타임에 셰이더를 컴파일**한다(B3 미착수). B3가 닫히면
   제거되고, 안 닫히면 첫 실행 지연을 문서에 명시하고 캡처 전 워밍업한다.
2. **3분 플레이 영상** (DX12)
3. **기술 모드 영상** — 패스 분해 · 프로파일러 · 라이브 리로드
4. **기술 문서 한 편** — 데모가 소비한 엔진 구조. 화면에 안 나오는 차별점을 여기 싣는다:
   결정적 자산 파이프라인(UUIDv8/SHA-256), 멀티백엔드 픽셀 패리티 게이트,
   변이로 이빨을 증명한 회귀 세트, HTTP 라이브 에디터 서비스
5. **DX12/Vulkan 패리티 스크린샷 비교** — 두 프로세스 나란히
6. **`CREDITS.md`** — 자산 출처·라이선스

---

## 6. 자산 운영 규약

리팩토링 완료 후에도 텍스처 예산은 유한하다. PHASE 12가 BC5/BC7과 cook 트랜스코딩을
닫는다는 전제에서도 아래를 유지한다.

- **해상도**: 1K 기본, 히어로 오브젝트만 2K. **4K 금지.**
  근거(현 엔진 실측): 4K PBR 세트 한 벌이 GPU 267 MiB + CPU 267 MiB.
  `DataSystem::Textures`는 `Finalize()`에서만 비워져 **CPU 픽셀이 종료까지 누적된다.**
  BC5/BC7 도입 후에도 4K는 세트당 ~75 MiB로 재질 12종이면 900 MiB다.
- **포맷**: PNG 8-bit 통일. EXR 디코더 없음. `.tga`는 로더는 있으나 자산 DB·cook
  허용 목록에 없어 GUID가 생기지 않는다. **16-bit PNG는 조용히 8비트로 내려앉는다**
  (`TextureDecodedFormatToRHI` 대응 없음) — Height/Displacement에 치명적.
- **노멀맵**: OpenGL(Y+)만. §3 갭 ⑤.
- **재질 수 상한 12종**: Vulkan 디스크립터 풀이 재질 ~102개에서 막히고
  (`VulkanFrameAllocators.cpp:11` maxSets 256 / SAMPLED_IMAGE 512, 재질당 SRV 5),
  DX12는 4096 페이지 초과 시 **조용한 draw 누락**이다.
- **Fab 라이선스 확인 항목**: ⓐ 엔진 제한 표기 없음 ⓑ FBX/glTF 소스 동봉
  (`.uasset` 전용은 임포트 불가) ⓒ 노멀맵 Y+ 변형 제공 ⓓ 출처를 `CREDITS.md`에 기록

---

## 7. 리스크

| 리스크 | 영향 | 대응 |
|---|---|---|
| 선행 트랙 5종(RND-1/2/3, PHASE 10, UI-T)이 데모 하나를 위해 전부 필요 | 착수 시점이 계속 밀린다 | 데모를 **트랙 완료 판정의 소비자**로 삼는다. 각 트랙의 완료 조건에 "데모 무대에서 동작"을 넣으면 트랙과 데모가 같이 닫힌다 |
| PBR W8(재질/디스크립터/PSO generation 원자 밀봉) 미착수 | **전체 메시가 검거나 색이 바뀌는 간헐 플리커** — 영상 캡처를 망친다 | W8을 데모 선행 필수로 승격. 캡처 전 장시간 구동 검증 |
| 파티클(PHASE 10)이 가장 큰 미지수 | 총기 연출 전체가 걸린다 | PHASE 10 완료 조건에 "머즐 플래시·연기·탄피 3종이 데모 무대에서 동작"을 넣는다 |
| Vulkan 표시 경로가 CPU 리드백 왕복 | Vulkan 캡처가 느리다 | 플레이·영상은 DX12 고정. Vulkan은 정지 스크린샷 패리티로만 |
| 실사풍은 UE와 직접 비교당한다 | 완성도 미달이 눈에 띈다 | 무대를 좁히고 밀도를 올린다. 넓은 개활지를 만들지 않는다 |
| 밉 생성 경로가 미커밋(작업 트리 `Texture.cpp`/`Texture.h`/`DataSystem.cpp`) | 4K/BC 왕복 검증 안 됨 | DM1 전에 커밋·검증. 비표준 슬롯과 일반 텍스처는 아직 밉을 안 탄다 |

---

## 8. 실측 근거

이 계획서의 현 상태 판정은 2026-09-06 코드 실측이다. 렌더 파이프라인 조립의 단일
출처는 `EnhancedSceneRenderer.cpp:1883` `BuildPipelineDesc`이며, 실제 노드 순서는:

```
Shadow → GBuffer → Decal → SSAO → Deferred → SkyBox → SSGI → Forward+
      → Sprite → SSS → SSR → VolumetricFog → PostChain → UI → live_present
```

DX12·Vulkan이 같은 템플릿 함수로 조립되고(`:1510`, `:1580`) 패스 14개 파일에
백엔드 분기가 없다 — 피처 커버리지는 사실상 동등하다. 차이는 표시 경로(DX12 공유
텍스처 vs Vulkan CPU 리드백)와 자가검증 커버리지(`vk.live`/`vk.scene` 부재)다.

각 항목의 파일:줄 근거는 §2·§3 본문에 인용했다.
