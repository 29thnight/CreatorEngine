# RG5-9 실제 versioned SceneHost·Code/Graph 혼합 GPU 수용 — 완료

Forward+ Graph stream을 ExplicitVersioned에서 허용하고 ExplicitSingleWriter에서는 구체적 오류로 거부한다. 제품 기본 DeclarationOrder는 유지하며 전체 SceneRenderer의 기본 전환은 RG6 소유다. 실제 생산자·SceneHost·Forward+를 실행하는 DX12 GPU 검증으로 혼합 경로의 보호 조건을 회수했다. RG5 전체는 progress/기성 0이다.

## 구현과 검증 범위

기존 material_forward_transport_tests.inl의 실제 24개 장면을 기준 DeclarationOrder, ExplicitVersioned+PreserveDeclarationOrder, ExplicitVersioned+DependencyOrder의 세 정책으로 실행한다. --forward-transport의 기존 단일 정책·허용치는 유지하고 --rg5-mixed만 세 정책을 실행한다. 모든 if/for에 스코프를 작성했다.

Code/Graph 순서 혼합, alpha 0/1, 역방향 카메라 순서, opaque depth 차폐, identity refraction, SSS, Volume 및 조합, 입력 목록 순서 교체와 4-worker 병렬 기록·제출을 포함한다. 실제 GBuffer·SceneHost::DeclareGBuffer/DeclareColor·DeclareVolume·DeclareBlended·Forward+와 Lookup/Refraction/SSS/Volume 소유자 배선을 사용하며 수정된 출력 핸들을 실제 후속 입력·readback으로 전달한다.

versioned compile 진단은 LegacyState 접근 0과 반환 색상의 최신 writer를 확인한다. Volume은 별도 LX.Scene.VolumeColor 자원을 반환하므로 동일 자원 번호를 요구하지 않고 실제 반환 자원의 최신 writer를 확인한다. 화면 전체 HDR은 각 구성의 기준 정책과 비교하고 opaque depth/owner 보존, finite HDR, 기존 독립 alpha/refraction/Beer-Lambert 계산식도 검사한다. 정책 간 최대 오차는 두 구성 모두 0이다. 기본 품질 수치와 IBL 1024/4096은 변경하지 않았다.

## 증거

증거 루트: Build/Obj/RenderRG5Mixed. runner는 Tools/regression/verify-rg5-mixed.ps1이다.

- Debug/Release probe 최종 빌드와 Editor 빌드 통과: Build/rg5-mixed-{Debug,Release}-final-build.log 및 rg5-mixed-editor-{Debug,Release}-build.log.
- Debug-gpu-v3, Release-gpu-v1: 각각 정책 3 × 장면 24 = 72 frames, 독립 계산식 components 6804, maxError=0, validationProblems=0, exitCode=0. 소스·실행 파일 SHA-256 일치 확인.
- 두 구성의 geometry cache: cold uploads=2, hits=160, transforms=7, completed transformHits=155. 실제 cold 및 제출 완료 재사용을 확인했다. Lookup 재사용 횟수를 별도로 계측했다는 주장은 하지 않는다.
- Debug-regression-v1, Release-regression-v1: 기본 제품 경로의 구성별 독립 프로세스 2회 × 캡처 2회 회귀 통과. 실패 0·정상 종료·강제 종료 없음.
- Debug-before-after-v1, Release-before-after-v1, Debug-Release-v1: 직전 통합 기준선 대비 및 구성 간 각 16개 이미지 maxError/RMSE/changedPixels/exceededPixels 모두 0. 동일 inputHash/full graphHash.
- current-baseline-phase-complete-v1.json: 현재 소스·Editor executable/runtime 해시 확인 후 phaseComplete=true.
- rg5-mixed-completion-v1.json: sliceComplete=true, mixedStreamGpuAcceptance=true, versionedProductGpuAcceptance=false, rg5Complete=false. 전체 제품 SceneRenderer의 versioned GPU 수용과 RG6 전환을 혼합 pass 수용과 구분한다.

## 수정 과정의 기록

Debug-gpu-v1은 Volume 출력이 기존 lighting과 같은 자원이어야 한다는 잘못된 테스트 단언으로 중단됐다. Volume의 별도 출력 계약과 최신 writer 검증으로 수정했다. Debug-gpu-v2는 실제 GPU 72 frames/maxError=0/validation=0/exitCode=0을 마쳤으나 결과 수집기의 Windows CRLF 성공 표식 처리 때문에 wrapper가 실패했다. 줄바꿈 처리를 수정한 v3에서 native 및 wrapper가 모두 통과했다. 이전 로그는 보존한다.

Release 빌드의 기존 라이브러리 PDB 관련 LNK4020 경고는 남아 있으며 빌드·GPU 실행은 통과했다. 디버거의 일부 심볼 제한은 이번 렌더링 수용과 별개다.

## 남은 조건

나머지 제품·Editor·캡처·fixture 소비자/ReadWrite 이관 및 legacy 추론·임시 adapter 제거, 전체 기본 SceneRenderer의 versioned 수용, RG6 기본 의존성 정렬 전환은 남아 있다. 이 16×16 혼합 pass GPU 검증을 전체 제품 장면의 versioned 전환이나 MAT-9 최종 품질·성능 수용으로 확대 해석하지 않는다. Vulkan 비교는 PHASE 4.9 RenderDoc 캡처→리소스 확인→픽셀별 비교에서만 진행한다.
현행 기준선은 후속 [RG5-10 Decal 이관 검증](RenderRg5DecalMigration.md)으로 갱신했다. 이 문서의 RG5-9 증거는 보존한다.
