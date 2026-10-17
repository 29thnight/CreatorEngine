# RG5 통합 후 현행 기준선 갱신 — 완료

2026-10-03 커밋 6eec2b3324805b85ef0d28766e2002912b3b7baa의 물리·CSM 및 RG4/RG5-1~8 통합 코드로 DX12 Debug/Release 기준선을 갱신했다. 제품 기본 DeclarationOrder와 Forward Graph guard는 유지한다. 실제 versioned SceneHost 및 Code/Graph 혼합 GPU 수용은 다음 구현·검증이며 이번 기준선 완료에 포함하지 않는다.

## 실행 및 비교 증거

- 빌드: Build/rg5-sync-Debug-build.log, Build/rg5-sync-Release-build.log, 모두 종료 코드 0.
- 현행 증거 루트: Build/Obj/RenderRG5Integration.
- Debug-regression-v1 및 Release-regression-v2: 구성별 독립 프로세스 2회 × 캡처 2회, complete=true, 실패 0, 정상 종료 코드 0, 강제 종료 없음. native graph fixture 및 GPU validation·변이 게이트 통과.
- Debug-before-after-v1, Release-before-after-v1: 통합 이전 RG5-8 기준선과 비교. 각 16개 이미지 maxError/RMSE/changedPixels/exceededPixels 모두 0.
- Debug-Release-v1: 현행 구성 간 16개 이미지 모든 오차 0.
- inputHash: 7f3cd805e8720609af9f14c8f11af20377358623d6c2dff565a5ad3e7e31e999.
- full graphHash: bac6d96e46e89f524c5011e48d2a52e7e734942386fe72008648645aa68b7200.
- current-baseline-phase-complete-v1.json: 현행 source/executable/runtime SHA-256 검증 후 phaseComplete=true.
- rg5-integration-completion-v1.json: sliceComplete=true, rg5Complete=false, versionedProductGpuAcceptance=false.

## 실패 기록과 자산 대조

Release-regression-v1은 첫 프로세스 통과 후 두 번째 프로세스의 HTTP 제어 연결에서 로컬 소켓 주소 충돌(127.0.0.1:64425)로 실패했다. 해당 실행의 complete=false·강제 종료 기록을 보존하며 합격 증거에 사용하지 않는다. 새 디렉터리 Release-regression-v2의 두 독립 실행을 기존 게이트 그대로 재검증해 정상 종료로 수용했다.

구성 간 assetIdentity JSON은 객체 필드 순서(path/sha256)가 달라 문자열이 달랐으나 파일 경로와 SHA-256은 전부 동일했다. 원본 결과를 수정하지 않고 완료 runner에서 경로 수·중복·정확한 경로 및 SHA-256을 대조했다. 환경·tuning도 동일하다. canonical complete-render-base0.ps1은 원본 결과와 source-hashes를 읽어 현행 소스·바이너리를 확인했다.

## 다음 작업

실제 versioned SceneHost·Code/Graph 혼합 GPU 실행에서 준비·캐시 재사용·투과·SSS·Volume 조합과 갱신 출력 핸들 전달을 수용한 뒤 Forward Graph guard를 제거한다. 남은 소비자/ReadWrite 이관·legacy 추론 제거·RG6 기본 정렬 전환은 계속 열려 있다. RG5 progress/earnedDays 0, 전체 공수는 유지한다. MAT-9 품질·성능, IBL 1024/4096 및 PHASE 4.9 RenderDoc 기반 Vulkan 비교 계약도 유지한다.

후속 갱신: [RG5-9 혼합 GPU 수용](RenderRg5MixedGpuAcceptance.md)에서 실제 versioned SceneHost/Forward+ Code·Graph 혼합 검증과 ExplicitVersioned guard 제거를 완료했다. 최신 기본 제품 기준선도 이 기록을 따른다. 전체 제품 versioned 수용/RG6 전환 및 나머지 소비자 이관은 계속 열린다.
