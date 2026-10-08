# RG5 최종 Fog/PostChain/UI/Editor 및 capture/fixture 수용

2026-10-07. **RG5 완료·기성 10인일 회수.** 최종 조합·개별 기능·실제 Editor 장면 캡처/fixture 수용이 Debug/Release 모두 통과했다. 이는 계획 공수 회수이며 실제 소요 시간 역산이 아니다. RG6·RG-V는 progress/기성 0으로 유지한다.

## 최종 조합

`EnhancedFinalRg5Tests.h`는 실제 Fog/PostChain/UI/Grid/WireFrame/GizmoIcon/GizmoLine의 Declare/Execute를 사용한다. 43 fixture를 DeclarationOrder, ExplicitVersioned/PreserveDeclarationOrder, ExplicitVersioned/DependencyOrder에서 각각 실행한다. 구성마다 129 GPU frames이며, 64×64 전체 픽셀의 9단계 결과를 첫 정책과 정확히 대조한다.

| 범위 | fixture |
|---|---|
| 최종 조합 | Fog·UI·Editor on/off, PostChain 직접/별도 입력, Bloom/FXAA on/off의 32 조합; ToneMap/vignette/grading 활성 |
| Editor 개별 기여 | Grid/WireFrame/Icon/Line 독립 4개 |
| 입력 누락 | Fog cloud 누락 우회, Post 입력 누락 거부와 LDR fallback |
| 자체 출력 | UI 자체 색상, Grid 자체 색상/depth 이후 Editor 전체 체인 |
| Fog 이력 | lit frame → 무광원/이력 blend 유지 → ResetHistory 후 차이 |

각 단계 선언 직후 readback을 붙여 이후 Modify보다 먼저 해당 버전을 소비한다. 마지막 출력은 native RHI texture를 Common으로 import한 뒤 새 버전에 Write하여 CopyTexture하고, 복사 결과까지 readback한다. 전체 픽셀 유한성, 정책 간 동일성, 최종 capture와 복사 결과의 동일성, imported final state 복원을 확인한다. 일곱 기능 각각의 실제 픽셀 기여가 최소 한 fixture에서 발생해야 통과한다. 명시 모드 snapshot의 암묵 접근·중복 자원 사용을 거부하고 Present가 최종 출력 버전을 읽는지 검사한다.

| 검사 | Debug | Release |
|---|---|---|
| 최종 3정책/129 frames/9단계 | maxError 0 | maxError 0 |
| rendergraph·fog·post·ui·grid·wireframe·gizmoicon·gizmoline | 모두 succeeded | 모두 succeeded |
| GPU validation / dropped messages | 0 / 0 | 0 / 0 |
| commandlet·quit | 정상 exit 0 | 정상 exit 0 |

증거: `Build/Verification/RG5Final20261007/Debug-v2`, `Release-final`의 result/source-hashes/results 및 stdout/stderr. VS18/v145로 두 Editor의 프로젝트 의존성을 빌드한 뒤 최종 RenderTests를 재빌드하고 Editor를 다시 링크했다. 기존 컴파일 경고는 로그에 보존하며 warning-free/Player/Shipping 검증을 주장하지 않는다.

## fixture 및 감사 도구 보완

- 기존 Fog fixture가 업로드한 입력 5개를 PixelShaderResource 상태로 전환한 뒤 ShaderResource로 import했다. 실제 상태를 보관하고 import tracker로 연결해 compute noise SRV의 GPU validation 오류를 해결했다.
- 별도 fixture 사본에 GizmoIcon codec 검사에 필요한 기존 BC3 blueNoise.dds를 추가하고 해시를 남겼다. 원본 및 실패 증거는 보존했다.
- 최초 Debug 실패의 Present 검사는 legacy access가 같은 source/destination을 혼동했다. CopySource 상태와 함께 검사하도록 바로잡았다. 실패 뒤 최종 Debug/Release 모두 정상 종료·validation 0을 확인했다.
- `base0_artifacts.py`는 schema 3의 명시 접근·resource kind/version·생산자/소비자·RAW/WAR/WAW·culling·lifetime·wave를 감사한다. schema 1의 역사적 검사는 유지한다. 새 schema 변이로 누락 edge·암묵 access·잘못된 kind·오래된 display version 거부를 확인한다.
- 기존 RG5 probe wrapper의 stale `productDefault=DeclarationOrder` 메타데이터를 실제 ExplicitVersioned/DependencyOrder로 정정하고 nested/ordinary vcpkg DLL 경로를 모두 지원한다. Debug 조합 실행 이후 바뀐 4개 wrapper는 런타임 코드 변경과 구분한다.
- `Capture-Debug-v1`의 실제 캡처는 `gbuffer-pending-lattice` 준비 중 경로였고 30초 종료 제한도 넘었다. 성공 캡처 명령이나 validation 0만으로 수용하지 않는다. CPU packet fence와 비동기 재질/PSO 준비 완료는 별개다. 별도 preparation capture에서 정상 재질 경로를 확인한 뒤 입력을 동결하고, 정상 종료를 최대 180초 기다리도록 하네스를 보완했다. 준비 중 결과는 accepted image set에서 제외하고 보존한다.

## 접근 선언 목록과 수용 경계

tracked cpp/h/inl과 신규 최종 검사 헤더의 `.AddPass`/`->AddPass`/`AddSplitPass`/`AddRepeatedPass` 호출 위치는 제품 69·fixture 253이다. `declaration-inventory.json`은 위치 감사용 목록이며 AST 분석이나 이관율 계산은 아니다. 제품 live 그래프 두 생성자는 ExplicitVersioned/DependencyOrder다. 과거 정책 대조용 DeclarationOrder fixture와 그에 대응하는 호환 분기는 의도한 검사 계약이며 제품 명시 그래프의 암묵 선언 adapter로 취급하지 않는다.

Editor overlay는 Grid → WireFrame → Icon → Line의 반환 Display.LDR 및 depth를 후속 입력으로 연결한다. 단계 capture와 전체 PBR capture는 선언 당시 최신 핸들을 값으로 보관하고, live_present는 최종 Display.LDR을 읽는다. 실제 live 캡처 snapshot의 접근·의존성 감사와 이미지 비교도 아래 별도 증거로 통과했다. 제품 명시 경로의 임시 추론 adapter 잔여는 없고, legacy 비교 계약은 유지한다.

## 실제 Editor 캡처

`Capture-Debug-v2/dx12-0`에서 준비용 capture 2회 후 실제 `lattice` 재질 경로를 회수했다. 65 declared/executed passes, culling 0의 schema 3 ExplicitVersioned/DependencyOrder snapshot을 감사했고, 같은 프로세스 2캡처의 16개 이미지 maxError/rmse/changedPixels/exceededPixels 모두 0이었다. GPU 계측은 measured, validation 문제/유실 0, 강제 종료 없이 exit 0이다. 14개 변이(입력·해상도·generated frame·역순·누락 RAW/version edge·암묵 access·kind·stale display version·수명·GPU 측정·IBL 품질·배리어·픽셀 오류)를 모두 거부했다.

Debug/Release 각각 독립 2프로세스×2캡처를 정상 종료했다. 같은 프로세스·독립 프로세스·구성 간 비교에서 각각 16개 이미지 모두 maxError/rmse/changedPixels/exceededPixels 0이다. 각 캡처의 GPU 계측은 measured, validation 문제/유실 0이고 소스·실행 파일·runtime DLL SHA-256 불변을 확인했다. `complete-render-base0.ps1`의 현재 두 구성 계약과 `finalize.ps1`의 조합/reference/runtime 소스 감사도 통과했다.

| 최종 증거 | 위치 (`Build/Verification/RG5Final20261007/` 기준) |
|---|---|
| Debug live 기준선 | `Capture-Debug-v2/result.json` |
| Release live 기준선 | `Capture-Release-final/result.json` |
| 구성 간 16개 이미지 | `Debug-Release/comparison.json`, final PNG·차영상 |
| 현재 두 구성 완료 | `baseline-phase-complete.json` (`phaseComplete=true`) |
| RG5 통합 판정 | `final-result.json` (`rg5Complete=true`, `earnedDays=10`) |

개별 조합/reference 결과의 `rg5Complete=false`는 작성 당시 범위 한정 판정으로 보존한다. 이후 도구 변경으로 달라진 과거 source snapshot의 파일은 regression PS1뿐이며 C++/헤더/셰이더 변경 0이다. 현행 live 기준선은 갱신된 감사 도구까지 해시를 확인한다. 이 대표 장면은 PostChain/Editor와 실제 LX 경로·Present/capture를 검증한다. UI rect·Wire selection·Fog cloud 입력이 없는 장면이므로 해당 기능의 활성 기여는 앞의 43-fixture 조합으로 검증하며, 대표 장면에서 모두 활성화됐다고 주장하지 않는다.

RG6은 변경 전후 별도 프로세스의 제품 cutover 및 CPU/GPU 비용 수용을 계속 소유한다. 현행 구성의 반복성 검사만으로 RG6·RG-V·MAT-9·GPU 기능·Vulkan 비교 완료를 판정하지 않는다.

계획서·공수 원장·대시보드에 RG5 done/기성 10을 반영했다. PHASE 4 계열 총 355/기성 98/잔여 257(+미산정), PHASE 4.3 총 100/기성 46/잔여 54다. 대시보드 442항목 전체 JavaScript 파싱·렌더, 문자열/구조 오류 0·진행률 유한값·phase-meta 8개 산수 검사를 통과했다. TASKS의 실제 공수 합계와 RG6/RG-V progress·기성 0도 별도로 계산해 확인했다(`ledger-check.json`). 보고서 상대 링크·수정 PowerShell 구문·Python import 및 `git diff --check` 통과.
