# 대체 재질 최소 렌더 경로·배포 캐시 검증 — 2026-10-21

## 결과와 범위

Debug x64를 재빌드하고 CreatorRobot과 Aria를 실제 콘텐츠 브라우저에서 씬으로 드래그했다. 각 모델의 빈 엔진 캐시와 해당 캐시 재실행을 별도 프로세스로 확인했다. 체커 무늬 대체 재질, 대체 상태 선택·이동, 원본 재질 전환이 동작한다. 배포 제품을 읽은 네 조건 모두 대체 셰이더 런타임 컴파일은 0회였다.

**첫 표시를 앞당기는 변경이다. 전체 로딩이 항상 빨라졌다고 결론 내리지는 않는다.** Aria cold의 최종 원본 제출은 이번에 57.126초로, 이전 회색 대체 경로의 47.325초보다 늦었다. 공통 렌더 준비와 원본 전환 시 GPU 준비 대기는 남아 있다.

## 구현

- 대체 프로그램에만 별도 식별자를 부여했다. 원본 재질의 셰이더 기능과 패스 계약은 유지한다.
- 대체 표면은 로컬 좌표 기반 3차원 체커와 저렴한 법선 명암으로 표시한다. 별도 텍스처를 로드하지 않고, 화면 미분으로 먼 거리 패턴을 완화한다.
- 대체 색상 경로에서는 원본 그래프/PBR·IBL·데칼 평가를 생략한다. 대체용 그림자 및 lookup 셰이더 5개를 컴파일 대상에서 제외해 DXIL entry가 16개에서 11개가 됐다. 대응 그림자·lookup·블렌드 PSO 준비와 대체 draw의 shadow/lookup capture도 생략한다.
- 깊이/visibility, GBuffer, temporal/motion, 선택과 이동에 필요한 기본 경로는 유지한다. 공통 lookup clear 및 공통 렌더 파이프라인 초기화까지 모두 제거한 구현은 아니다. 원본과 대체가 섞인 씬은 원본의 일반 렌더 정책을 유지한다.
- `Build-EditorPreviewPackage.ps1`로 검증된 대체 제품을 미리 생성해 실행 파일 옆 `EditorResources/Preview`에 설치한다. 실제 실행은 소스·컴파일 환경·의존 내용·cooked 계약·백엔드를 검증한 제품을 우선 읽고, 누락/손상/불일치 시 기존 컴파일 경로로 복구한다.
- 배포 도구는 해당 제품과 manifest를 함께 복사하며, 런타임 hash 또는 제품 길이/hash가 다른 패키지는 거부한다. 전체 엔진 배포 작업은 실행하지 않았고 배포 선택·검증 코드의 회귀 검사를 수행했다.

캐시 miss의 실제 원인은 소스 직렬화가 아니었다. `MaterialGraphPrimitiveCache.slang`라는 본체와 같은 이름의 include가 있어 기존 include-shadow 검사가 제품 재사용을 거부했다. 진단 로그 `reason=dependency-resolution`으로 확인한 뒤 include를 `MaterialGraphPrimitiveCacheTypes.slang`로 변경했다. 기존 include의 meta GUID는 유지했다. 안전성 검사를 완화하지 않았다. 이름 변경 후 실제 실행에서 `[preview.package] hit stages=11`을 확인했다.

주요 파일:

- [대체 패키지 로딩/검증](C:/Users/idene/source/repos/CreatorEngine/Engine/RenderEngine/DataSystem.cpp)
- [최소 셰이더 entry 계약](C:/Users/idene/source/repos/CreatorEngine/Engine/RenderEngine/MaterialGraphSceneCompiler.cpp)
- [체커 표면/색상 경로](C:/Users/idene/source/repos/CreatorEngine/Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/MaterialGraphSceneHost.slang)
- [대체 PSO 및 draw 준비](C:/Users/idene/source/repos/CreatorEngine/Engine/RenderEngine/MaterialGraphSceneHost.cpp)
- [배포 제품 생성](C:/Users/idene/source/repos/CreatorEngine/Tools/build/Build-EditorPreviewPackage.ps1)
- [배포 제품 검증](C:/Users/idene/source/repos/CreatorEngine/BuildTool/EditorPreviewPackage.cs)

## 측정 조건

`Bin/x64-Debug/Editor/CreatorEditor.exe` 직접 실행, 디버거 미부착. DX12 DebugLayer on, GPUValidation off. 성능 측정에 CLI 모델 배치를 사용하지 않았다. 각 cold 디렉터리는 시작 시 0개 항목이고 warm은 해당 디렉터리를 재사용했다. 기존 사용자 캐시는 이동/삭제하지 않았다.

빈 캐시는 엔진의 RHI 셰이더와 재질 프로그램 캐시를 뜻한다. 배포용 대체 제품은 설치된 상태다. GLB/임포트 산출물, OS 파일 캐시, GPU 드라이버 내부 캐시를 초기화한 것은 아니다. 조건별 1회이므로 통계적 개선율이나 최악 시간으로 해석하지 않는다.

최종 runtime SHA256: `DC13B95EB1671786D3FE66ABC1502752DA7D8E15A9314417971459B013BFE0A9`.
빌드: `Build/editor-minimal-preview-20261021/build5.log`, 패키지: `bake-renamed2/summary.json`.

`model.display`는 모델 draw를 포함한 프레임의 GPU 제출 수락 시각이다. Present 완료나 첫 픽셀 시각은 아니다. 아래 화면 관찰 표와 구분한다. `model.materials`는 CPU 측 원본 재질 적용 시각으로, 최종 화면 시각으로 사용하지 않는다.

## 이전 회색 경로와 렌더 제출 비교

단위: 드롭 요청 수락 후 초. 이전 값은 [이전 실제 드래그 보고서](C:/Users/idene/source/repos/CreatorEngine/docs/EditorFallbackMaterialDragComparison-20261021.md)의 보존된 측정이다. 이번에 구 바이너리를 다시 실행한 동시 A/B는 아니다.

| 모델 / 조건 | 이전 회색 대체 | 이번 체커 대체 | 이전 원본 | 이번 원본 |
|---|---:|---:|---:|---:|
| CreatorRobot / cold | 33.359 | 11.474 | 58.542 | 36.891 |
| CreatorRobot / warm | 2.890 | 2.092 | 6.258 | 4.259 |
| Aria / cold | 24.041 | 18.675 | 47.325 | **57.126** |
| Aria / warm | 2.375 | 1.292 | 5.576 | 3.576 |

이전 Robot cold 추가 표본은 대체 24.504초 / 원본 49.571초였다. 이전 표본 자체에도 약 9초 편차가 있으므로 첫 표본만으로 개선 배율을 주장하지 않는다.

## 실제 화면 및 조작 증거

단위: 드래그 입력 직전부터 초. `(a,b]`는 마지막 미표시와 첫 표시 사이의 관찰 범위다. 선택·이동은 실제 입력 성공을 확인한 상한이며, 그때까지 조작 불가능했다는 뜻이 아니다. 도구 호출 사이의 작업자 공백을 로딩 시간으로 간주하지 않는다.

| 모델 / 조건 | 첫 모델 화면 | 선택 응답 확인 | 이동 응답 확인 | 원본 화면 |
|---|---:|---:|---:|---:|
| Robot / cold | (7.154, 12.205], 체커 | 20.140, 체커 | 26.833, 체커 | (35.322, 43.712] |
| Robot / warm | (2.068, 3.000], 체커 | 17.852, 원본 | 32.002, 원본 | (3.000, 17.852] |
| Aria / cold | (12.836, 28.479], 체커 | 38.463, 체커 | 48.068, 체커 | (48.068, 62.467] |
| Aria / warm | ≤3.478, 이미 원본 | 17.151, 원본 | 28.448, 원본 | ≤3.478 |

Robot cold는 Z=0.219→0.839, Aria cold는 Z=0.219→0.870 이동을 원본 준비 중 수행했고, 원본 전환 후 위치와 선택을 유지했다. Aria warm은 첫 관찰 전에 원본으로 바뀌어 대체 화면의 별도 시각을 시각적으로 구분할 수 없었다. 내부 제출 로그의 대체/원본 시각은 위 표에 별도 기재했다. 두 타이머는 시작 기준이 달라 화면 관찰값을 내부 제출값과 직접 차감하지 않는다.

[Robot 체커 이동](C:/Users/idene/source/repos/CreatorEngine/Build/editor-minimal-preview-20261021/packaged-robot-cold/ui-9.jpg), [Aria 체커 이동](C:/Users/idene/source/repos/CreatorEngine/Build/editor-minimal-preview-20261021/packaged-aria-cold/ui-9.jpg), [원본 Robot + 체커 Aria 혼합 장면](C:/Users/idene/source/repos/CreatorEngine/Build/editor-minimal-preview-20261021/packaged-robot-warm/mixed-drop.jpg).

Robot warm 측정 완료 후 혼합 장면 확인을 위해 Aria를 추가 드롭했다. 이 추가 작업은 Robot 성능 통계에서 제외했으며, 독립 Aria cold/warm은 다른 캐시에서 실행했다.

## 컴파일·캐시와 남은 지연

| 조건 | 대체 런타임 컴파일 | 원본 런타임 컴파일 | 공통 shader 컴파일 / disk hit |
|---|---:|---:|---:|
| Robot cold | 0 | 64 | 46 / 0 |
| Robot warm | 0 | 0 | 0 / 46 |
| Aria cold | 0 | 48 | 46 / 0 |
| Aria warm | 0 | 0 | 0 / 46 |

네 조건 모두 배포 제품 11 entry 적중이다. warm 원본은 검증된 재질 프로그램 제품을 읽어 RHI 셰이더 요청 자체를 피했다. 공통 48개 요청에는 memory hit 2개가 별도로 있다.

대체 컴파일은 드롭 경로에서 빠졌지만 공통 셰이더 요청 시간 합은 Robot 9.586초, Aria 9.729초로 남았다. 원본은 병렬 컴파일이므로 요청 시간 합을 로딩 시간으로 더하지 않는다. 원본 컴파일 span은 Robot 22.748초, Aria 20.463초였다.

Aria는 CPU 원본 적용 39.376초 이후 원본 draw 제출 57.126초까지 **17.750초**가 더 걸렸다. Robot의 같은 간격은 약 2.267초였다. 이 구간은 원본 렌더 프로그램/PSO/lookup 준비와 제출 경로를 추가 분리해야 한다. 이번 로그만으로 GPU 드라이버 PSO가 단독 원인이라고 확정하지 않는다. 대체 재질 최소화만으로 이 지연이 해결됐다고 주장하지 않는다.

준비 중 기존 `requested material program is still preparing` 프레임 로그가 1회 발생하는 동작도 남아 있다. 셰이더 컴파일 실패는 0회였으며 이후 표시/편집/원본 전환은 성공했다.

## 검증과 보존

- 최종 Debug 재빌드 성공. 위 네 가지 실제 창 실행 모두 exit 0, 동일 runtime hash.
- `BUILD_TOOL_TESTS_OK checks=166`: 선택적 패키지 없음, 유효한 제품+manifest, 손상 제품, 다른 런타임, 경로 이탈 검사 포함. 전체 배포 실행을 대신하는 주장은 하지 않는다.
- `lifecycle-commandlet/summary.json` 통과: 대체 프레임 표시 후 원본 준비 중 저장, 원본 graph ID 4개 유지, 임시 graph 저장 0, Undo 및 Aria 준비 중 씬 전환 취소 2, 늦은 씬 누출 0. 기능용 offscreen 검사이며 성능 표와 구분한다.
- 별도 실제 에디터 패키지 회귀 검사 통과: 정상 제품 적중과 손상 제품의 검증 거부/11 entry 재컴파일 복구를 확인했다. 최종 결과와 제품 복원 증거는 `package-regression2/summary.json`에 기록한다. 최초 회귀 스크립트는 CPU 적용 후 30프레임만 기다려 원본 GPU 제출 전에 종료되는 검사 오류가 있었고, 300프레임 대기로 수정한 실행을 최종 게이트로 사용한다.
- 기존 사용자 셰이더 캐시 263,128개 유지, 파일명/크기/mtime 변경·누락·추가 0. 전체 내용 hash 비교는 아니다.
- 두 원본 GLB SHA256 유지: Robot `58D779AFDE1A7FBD13332999C8B11890FB7E10FF2036CA6AE3645D14BEDBA402`, Aria `8ED22A7E3CF7E4F7087A45E83EEBF327B283F0162622F0051CF8AE45833FB0E7`.
- 모든 에디터 종료 후 작업 시작 시 `active.workspace` 바이트로 복원했다. 백업/복원 SHA256은 `D748399BF2184E3A20357E8730A4E7D7F5C26F3BA491ABD1E52E5F4375F791DC`로 일치한다. 다른 기존 변경은 되돌리지 않았고 커밋/푸시는 수행하지 않았다.
- 변경한 소스 범위의 `git diff --check`는 통과했다. 저장소 전체 검사에는 작업 시작 백업에도 존재하던 `active.workspace`의 EOF 공백 경고가 있어, 바이트 보존을 위해 해당 기존 내용은 정리하지 않았다.

원시 측정: [comparison.json](C:/Users/idene/source/repos/CreatorEngine/Build/editor-minimal-preview-20261021/comparison.json). 각 run에 실행 정보, UI 이벤트, 스크린샷, stdout/stderr, 요약을 보존했다. 패키지 적중 전의 탐색 실행은 최종 성능 표에 포함하지 않았다.

## 사용

에디터를 빌드한 뒤 PowerShell 7에서 다음을 실행하면 해당 빌드에 맞는 대체 제품을 생성한다.

```powershell
pwsh -NoProfile -File Tools/build/Build-EditorPreviewPackage.ps1 -Configuration Debug
```

Release 배포는 Release 에디터로 제품을 별도 생성해야 한다. 이번 실행 검증 범위는 Debug/DXIL이다. SPIR-V/Player 실행 검증은 수행하지 않았다.
