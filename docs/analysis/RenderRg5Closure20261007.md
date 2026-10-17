# RG5 종결 검사 — 2026-10-07

**최신 판정:** reference 및 최종 Fog/PostChain/UI/Editor·실제 capture/fixture 수용까지 완료해 RG5를 done/기성 10인일로 닫았다. [최종 수용 기록](RenderRg5FinalAcceptance20261007.md). 아래 보류 판정과 실패/복구 증거는 이 검사의 중간 이력으로 보존하며 RG6 전환 전후 수용은 별도로 남는다.

## 판정 범위

대상 HEAD는 `3afe1daaee7b75f644ac10b12d96fb684a0e74c8`이며 기존 계획서/대시보드 미커밋 변경을 보존했다. 이 검사는 RG5의 접근 선언 이관·fixture 정합성 검사다. RG6의 전체 제품 전환 전후 이미지·성능 수용을 대신하지 않는다.

증거 디렉터리: `Build/Verification/RG5Closure20261007`. 시작 시 Release 바이너리의 SHA-256을 기록하고 `dx12.rendergraph`, `dx12.decal`, `dx12.validation`을 GPU validation 활성 상태로 실행했다. 기존 Release 바이너리 실행은 수정 전 재현용이며 이번 소스 전체 재빌드 증거가 아니다.

## 수정 전 재현

| 명령 | 결과 |
|---|---|
| dx12.rendergraph | `command.exception`: Sprite GPU visibility must be prepared before graph declaration. |
| dx12.decal | `command.exception`: Decal GPU visibility must be prepared before graph declaration. |
| dx12.validation | GPU validation 활성, problems=0, droppedMessages=0 |
| 프로세스 | 정상 종료, exitCode=5(명령 실패) |

근거: `Release-before/results.jsonl`, `identity.json`, `process.json`. validation 0은 앞의 두 검사 통과를 뜻하지 않는다.

현재 제품 SceneRenderer는 준비된 recording 경계에서 `PrepareGpuVisibility`를 호출한다. 오래된 Sprite/Screen/Decal fixture는 `PrepareFrame` 뒤 바로 `Declare`를 호출했다. 다음 검사 호출부에 누락된 준비를 추가했으며 indirect 경로를 끄거나 예외를 삼키는 우회는 추가하지 않았다.

- `Editor/RenderTests/RHI/DX12/Tests/EnhancedSpriteRg5Tests.h`
- `Editor/RenderTests/RHI/DX12/Tests/EnhancedScreenRg5Tests.h`
- `Editor/RenderTests/RHI/DX12/Tests/Geometry/EnhancedDecalTest.cpp`
- `Tools/regression/material_scene_decal_tests.inl`: 병렬 upload prefix 뒤, graph 선언 전 GBuffer/Decal visibility 준비.

## 선언 감사

현재 live 그래프 생성 두 곳은 `EnhancedSceneRenderer.cpp`에서 모두 `ExplicitVersioned + DependencyOrder`를 명시한다. versioned compile은 명시적 Read/Write/ReadWrite가 아닌 접근을 거부한다. 따라서 소스에 `LegacyState` 문자열이 있다는 이유만으로 live 제품이 암묵 추론을 사용한다고 판정하지 않는다.

Fog/PostChain/UI/Grid/WireFrame/GizmoIcon/GizmoLine에는 명시적 접근·Write/Modify 배선이 있다. 과거 문서의 “이 소비자들을 아직 구현하지 않았다”는 설명은 현재 상태가 아니다. 다만 이들 전체 옵션/입력 조합에 대한 RG5 최종 실행 수용을 이번 정적 확인으로 대체하지 않는다.

**검사 당시 남았던 접근 생략 경로**는 정밀 bake/reference/probe용 API다. 아래 표는 최초 검사 이력이다. 후속 [reference API 이관](RenderRg5ReferenceMigration20261007.md)에서 세 API의 명시 Read/Write·생산 버전 반환과 D/R 3정책·일반 체인 63 frames·공유 depth 72 frames·GPU validation 0·exit 0 수용을 완료했다.

| 파일·선언 | 확인한 잔여 | 실제 소비 |
|---|---|---|
| [MaterialGraphIblBake.cpp](../../Engine/RenderEngine/MaterialGraphIblBake.cpp), IblBakeResult::Declare | 입력/환경 Read 및 출력 UAV Write·Ready Read의 접근 모드 생략, 출력 버전 진전 없음 | material_raster_surface_probe.cpp의 raster→surface→baked graph |
| [MaterialGraphSurfaceBatch.cpp](../../Engine/RenderEngine/MaterialGraphSurfaceBatch.cpp), SurfaceBatch::Declare | 입력·texture Read, 결과 UAV Write·Ready Read 접근 생략 | 같은 probe의 SurfaceBatch graph consumer |
| [MaterialGraphRasterSurface.cpp](../../Engine/RenderEngine/MaterialGraphRasterSurface.cpp), RasterSurfaceBatch::Declare | capture/resolve/readiness 접근 생략·버전 배선 잔여 | 같은 probe의 capture/resolve graph |

최초 검사 당시 이 선언들은 `RGPassUsage::access`의 기본 `LegacyState`에 의존했다. 후속 이관에서는 명시 모드의 compiled snapshot에 암묵 접근이 없고 writer v1·version edge가 존재함을 확인했다. DeclarationOrder는 의도한 기준 비교 분기로 유지한다. 이 reference 수용은 live SceneHost 전체 수용과 구분한다.

`declaration-inventory-before.json`은 파일·행·원문 목록이다. 보조 검색은 `.AddPass`/`->AddPass` 및 Split/Repeated 호출 행 기준 제품 69·fixture 250행을 찾았다. 다중 행 호출·매크로·주석을 완전히 해석한 AST 수가 아니며, 이 숫자를 이관률로 사용하지 않는다. `LegacyState` 검색의 제품 호환 분기 87행·graph core 6행·fixture 18행도 같은 이유로 미이관 개수가 아니다.

## 종결 전 잔여

1. **완료:** 수정한 fixture의 Debug/Release 실행·GPU validation·픽셀/정책 대조 통과(아래 결과).
2. **완료:** 위 reference API의 명시 Read/Write·생산 버전 반환 및 probe의 versioned preserve/dependency·기존 기준 정책 대조. 단일 결과 생산에는 Write를 사용하며, legacy 비교 분기는 의도한 호환 계약으로 분류한다.
3. **후속 완료:** 최종 소비자(Fog/PostChain/UI/Editor)와 capture/fixture의 접근·반환 출력·중복 Read/Modify·잘못된 입력 거부를 [최종 수용](RenderRg5FinalAcceptance20261007.md)에서 확인했다.

RG6의 전체 live frame 전환 전후 픽셀·성능 증거는 RG6에 남긴다. RG5의 10인일은 위 완료 조건이 모두 닫힌 최종 수용에서 회수했다.

## 수정 후 실행

**검사 복구와 후속 reference 이관은 통과, RG5 전체 종결은 보류**다. 최초 `closure-result.json`은 작성 당시의 reference/최종 소비자 잔여를 보존하며, 현재 남은 완료 조건은 최종 소비자 조합 및 capture/fixture 수용이다.

| 검사 | Debug | Release |
|---|---|---|
| dx12.rendergraph | succeeded | succeeded |
| dx12.decal | succeeded | succeeded |
| GPU validation / 유실 | 0 / 0 | 0 / 0 |
| commandlet 종료 | 정상, exit 0 | 정상, exit 0 |
| Sprite 정책별 전체 이미지 | 3정책·18프레임·오차 0 | 3정책·18프레임·오차 0 |
| 화면 SSS/SSR 정책별 전체 이미지 | 3정책·48프레임·오차 0 | 3정책·48프레임·오차 0 |
| 재질 Decal 별도 probe | 3정책·342프레임·오차 0·validation 0·exit 0 | 3정책·342프레임·오차 0·validation 0·exit 0 |

RenderGraph 명령은 기존 RG1~RG4 양·음성/정렬 검사와 RG5 producer·consumer·indirect·Forward·Lookup·Special·Surface 검사도 통과했다. 일반 Decal 검사는 변경 픽셀 2,048/기대 2,048, 상자 밖 누출 0·하늘 0 및 색/채널·batch 검사를 통과했다. GPU visibility 후보 준비는 실제 culling 비율이나 전체 제품 성능의 증거로 세지 않는다.

증거는 `Debug-final`/`Release-final`의 `results.jsonl`, `identity.json`, `process.json`, `Debug-decal-final`/`Release-decal-v2`의 결과·로그·소스 해시다. 최종 소스 1,705파일의 SHA-256이 검사 뒤에도 동일했다. `finalize-results.ps1`은 명령 4행의 성공·marker·validation·정상 종료와 두 probe 결과·소스 불변을 재확인했다.

빌드는 VS18/v145를 사용했다. Debug Editor는 전체 프로젝트 의존성을 빌드한 뒤 마지막 Decal 단정 수정분을 재빌드/링크했다. Release는 기존 의존 라이브러리를 재사용하고 RenderTests·Editor host·probe를 증분 빌드했다. 전체 Release clean rebuild나 Player/Shipping 빌드 통과를 주장하지 않는다. 기존 C4244/C4456/미사용 변수·managed trimming 등 경고와 Release probe의 Utility_Framework PDB LNK4020 경고를 로그에 보존했다.

## 하네스·재현 주의

- `Release-after-v1`은 과거 RG4 fixture에 GeometryVisibility.slang이 없어 실패했다. 원본 fixture는 보존하고 별도 `fixture/Project`에 Assets/ProjectSetting을 복사한 뒤 현행 기본 셰이더를 배치했다. `fixture-shader-hashes.json`에 해시를 기록했다.
- `Release-after-v2`에서 rendergraph는 통과했지만 Decal의 고정 3패스 단정이 실패했다. 실제 픽셀 결과는 이미 맞았으며 GPU reset/cull을 포함한 5패스·후보 1개 단정으로 보완했다. 최종 결과는 `*-final`만 사용한다.
- `Release-decal-v1`은 실행 DLL 검색 경로가 없어 빈 로그로 하네스가 실패했다. 실제 설치 경로 `vcpkg_installed/x64-windows/x64-windows/{bin,debug/bin}`을 PATH에 포함해 재실행했다. 전역 환경 설정은 변경하지 않았다.
- 기존 `verify-rg5-decal.ps1`의 결과 필드 `productDefault=DeclarationOrder`는 과거의 고정 문자열이다. 이 검사는 세 정책을 대조하며, 현재 제품 기본 모드 판정에는 그 필드를 사용하지 않는다. 실제 live 생성자의 ExplicitVersioned/DependencyOrder와 구분한다.
- 재현 진입점: 증거 폴더의 `run-commandlet.ps1`에 구성·새 Tag·`-Fixture Build/Verification/RG5Closure20261007/fixture/Project`를 지정한다. 재질 검사는 실제 구성별 DLL 경로를 PATH에 추가한 세션에서 `Tools/regression/verify-rg5-decal.ps1`에 구성과 새 OutputDirectory를 지정한다.

문서 대시보드 442항목의 전체 파싱/렌더·상태 구조 검사 및 `git diff --check`를 통과했다. 기존 완료 공수는 늘리지 않았다.
