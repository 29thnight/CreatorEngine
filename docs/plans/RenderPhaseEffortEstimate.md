# 렌더 계획 공수의 비교 원장

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 원장의 의미

이 문서는 과거 범위 추정을 보존하는 비교 기준이다. 이번 소스 감사는 작업 시간·현재 잔여·earned value를 재측정하지 않았다. 오래된 통과 보고를 현재 커밋의 수용으로 이월하거나 구현 파일 수를 완료율로 환산하지 않는다. 단위는 1인 전담 엔지니어의 계획 인일이며 확정 납기·실측 생산성이 아니다.

## 보존하는 계획 총량

| 계획 묶음 | 당시 추정 인일 | 현재 재산정 시 주의 |
|---|---:|---|
| PBR 입력·배선 | 18 | 현재 graph-only METEOR·granular generation·temporal capture로 범위 갱신 |
| Material | 40 + 별도 미산정 | 재질 전용 원장과 실제 소비자 수용을 대조 |
| RenderGraph/RHI 확장 | 100 | versioned/queue 구현 존재와 품질·수명·채택 잔여 분리 |
| Temporal/TU/FG | 71 | SDK build flag·API·GPU·품질·실제 생성 프레임별 지원 분리 |
| C# pipeline 저작 | 32 | C++ assembly를 현재 소비자 기준으로 재계수 |
| Lightmap | 35 | UV·baker·worker·artifact 실구현과 RT/queue 선택 필요 |
| Renderer quality/환경 | 28 | 기존 AgX/Bloom/CSM과 추가 품질 목표 분리 |
| GPU 설계 | 9 | 설계 산출물 예산이며 기능 구현 예산이 아님 |
| PT/Hybrid RT | 208 | 기본 116 + 확장/연구 92; 첫 prototype 후 재산정 |
| Backend parity | 22 | 실제 두 backend capture·resource·픽셀·수명 비교 |
| 합계 | 563 + 별도 미산정 | 현재 잔여 합계나 실적을 뜻하지 않음 |

표는 2026-10-01 및 10-08에 정한 범위의 역사적 계획 수치다. 이전 기록의 138 기성·425 잔여 등은 당시 완료 판정에 종속된 스냅샷으로, 이번 개정의 현행 수치로 게시하지 않는다. 원본 실행 산출물과 범위 변경을 함께 재검토해야 현재 잔여를 만들 수 있다.

## 집계 규칙

완료는 해당 행의 선언·배선·정확성·지원 범위·운영 수명을 만족한 증거가 있을 때만 인정한다. prototype·선택 fixture·소스 감사는 그 범위만 기록한다. 성능 이득 미수용인 queue/meshlet 작업을 구현되었다는 이유로 제품 채택 완료로 처리하지 않는다.

EXP 효과 실험은 RTP-0/5/13에 포함하고 AS 공통 기반과 통합 수명을 재계상하지 않는다. 옛 Asset-first SRP 33인일/설계 실적을 새 CSRP 32인일에 자동 이전하지 않는다. Vulkan 교차 비교는 parity 묶음이 소유하되 실제 Vulkan RT 기능 개발은 비교 작업에 숨기지 않는다.

후속 재산정은 작업별 current entry point, 미구현 요구, 정확한 수용 산출물, 위험·외부 dependency, 범위 중복과 불확실성 근거를 남긴다. 0 또는 미산정은 기능 부재/무료 구현/완료를 뜻하지 않는다.

## 소스 근거

- [Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp](../../Engine/RenderEngine/Render/Scene/EnhancedSceneRenderer.cpp)
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraph.cpp)
- [Engine/RenderEngine/Render/Graph/EnhancedRenderGraphQueues.cpp](../../Engine/RenderEngine/Render/Graph/EnhancedRenderGraphQueues.cpp)
- [Engine/RenderEngine/Render/Temporal/TemporalUpscalerHost.cpp](../../Engine/RenderEngine/Render/Temporal/TemporalUpscalerHost.cpp)
- [Engine/RenderEngine/MaterialGraphSceneHost.cpp](../../Engine/RenderEngine/MaterialGraphSceneHost.cpp)
