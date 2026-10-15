# Temporal SDK 빌드·실행 통합 경계

기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스와 실제 호출 배선. 이번 개정에서는 빌드·테스트·GPU 실행을 하지 않았다. 구현 존재와 실행 수용을 구분한다.

## 옵션과 외부 입력

TemporalSdk.props의 CreatorEnableFsrDx12, CreatorEnableFsrVulkan, CreatorEnableDlssStreamline, CreatorEnableXeSS, CreatorEnableXeSSVulkan, CreatorEnableAntiLag2는 기본 false다. 옵션을 켠다고 SDK 다운로드·빌드·runtime 배포를 자동 수행하지 않는다. x64, 외부 root와 필요한 library 경로를 검증한다.

FSR static DX12/Vulkan backend는 같은 바이너리에 동시에 켤 수 없다. props는 symbol 충돌을 이유로 명시 오류를 낸다. FSR 입력은 FidelityFX SDK 1.1.4/FSR 3.1.4, Streamline은 2.14.1, XeSS는 3.0.2를 기준으로 검사한다. 헤더 버전과 runtime에서 선택된 implementation string은 별도 정보다.

## 공용 계약과 adapter

TemporalReconstruction.h는 provider, backend, extent, jitter/motion/camera, TU/FG 입력과 실패 상태를 정의한다. ITemporalUpscaler와 ITemporalFrameGenerator는 SDK-native type을 노출하지 않는다. RHI/Temporal 아래 adapter가 실제 native resource·SDK context와 동기화 경계를 관리한다.

TemporalUpscalerHost는 라이브 뷰에서 provider를 구성하고 실제 render extent를 질의한다. 미지원·runtime 누락·SDK 실패는 각각 상태로 보고하며 FSR 또는 native fallback을 시도한다. DLSS host는 DX12 session을 요구한다. XeSS Vulkan TU와 FSR Vulkan은 해당 옵션과 장치 bootstrap이 필요하다. FG는 Player presentation과 마지막 자원 소비 수명을 갖춰야 하며 Editor viewport에는 적용하지 않는다.

## 통합 시 확인 순서

1. 정확한 SDK root·library·runtime 출처와 버전, 빌드 옵션을 기록한다
2. 선택 API의 device 생성 전 prerequisite와 SDK bootstrap 결과를 확인한다
3. capability와 context 생성, 실제 input format·extent·state를 검사한다
4. graph에서 실제 Evaluate가 성공하고 제출된 active 상태가 관찰되는지 확인한다
5. runtime/dispatch 실패 뒤 fallback extent·history를 재구성하고 SDK context 해제 전에 owner retirement를 확인한다
6. startup·전환·품질·Player FG/UI·latency와 여러 장치/API를 별도 수용한다

현재 어댑터 구현 존재는 vendor 전체 기능 지원이나 라이선스·runtime 재배포 권한을 보장하지 않는다. 이전 실행 문서의 수치는 해당 hash와 장치에 한정되며 이번 문서 개정에서 SDK 또는 GPU 검사를 수행하지 않았다.

## 소스 근거

- [Engine/RenderEngine/RHI/Temporal/TemporalSdk.props](../../Engine/RenderEngine/RHI/Temporal/TemporalSdk.props)
- [Engine/RenderEngine/Render/Temporal/TemporalReconstruction.h](../../Engine/RenderEngine/Render/Temporal/TemporalReconstruction.h)
- [Engine/RenderEngine/Render/Temporal/TemporalUpscalerHost.cpp](../../Engine/RenderEngine/Render/Temporal/TemporalUpscalerHost.cpp)
- [Engine/RenderEngine/RHI/Temporal/DlssTemporalAdapter.cpp](../../Engine/RenderEngine/RHI/Temporal/DlssTemporalAdapter.cpp)
- [Engine/RenderEngine/RHI/Temporal/Fsr/FsrBackendDX12.cpp](../../Engine/RenderEngine/RHI/Temporal/Fsr/FsrBackendDX12.cpp)
- [Engine/RenderEngine/RHI/Temporal/Fsr/FsrBackendVulkan.cpp](../../Engine/RenderEngine/RHI/Temporal/Fsr/FsrBackendVulkan.cpp)
- [Engine/RenderEngine/RHI/Temporal/XeSSUpscaler.cpp](../../Engine/RenderEngine/RHI/Temporal/XeSSUpscaler.cpp)
- [Engine/RenderEngine/RHI/Temporal/XeSSFrameGeneration.cpp](../../Engine/RenderEngine/RHI/Temporal/XeSSFrameGeneration.cpp)
