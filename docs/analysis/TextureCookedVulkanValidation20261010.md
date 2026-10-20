# PHASE 12 CECT DX12·Vulkan 동일 fixture GPU 검증 — 2026-10-10

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- DataSystem의 typed 요청은 resolver 세대와 취소/게시 상태를 보존한다. descriptor·payload·이미지 owner와 frame pin은 분리된 수명이다.
- ownership_cpp 기반 자원 소유와 논리적 unmount를 GC 객체 수거·GPU 완료와 혼동하지 않는다. DirectXTex cook와 GPU-ready 소비의 제품 범위는 텍스처/AssetDepot 계획에서 수용한다.

### 해당 구현에서 확인한 선언·소유 경계

- [Tools/regression/verify-texture-cooked-gpu.ps1:4](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-texture-cooked-gpu.ps1#L4) — `$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))`
- [Tools/regression/verify-texture-cooked-gpu.ps1:5](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-texture-cooked-gpu.ps1#L5) — `$root = Join-Path $repo ('Build/Obj/TextureCookedGpu/' + [Guid]::NewGuid().ToString('N'))`
- [Tools/regression/verify-texture-cooked-gpu.ps1:57](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/verify-texture-cooked-gpu.ps1#L57) — `[regex]::Matches($log, 'TEXTURE_PREPARED_BINDING_OK cases=6 unmountedOwner=1').Count -ne 3) {`
- [Engine/RenderEngine/RHI/Vulkan/VulkanBindingTable.h:38](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/RHI/Vulkan/VulkanBindingTable.h#L38) — `m_requests.push_back(std::move(request));`
- [Engine/RenderEngine/RHI/Vulkan/VulkanBindingTable.h:116](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/RHI/Vulkan/VulkanBindingTable.h#L116) — `if (VK_SUCCESS != VulkanApi::vkCreateSampler(`
- [Engine/RenderEngine/RHI/Vulkan/VulkanBindingTable.h:120](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/RHI/Vulkan/VulkanBindingTable.h#L120) — `VulkanApi::vkDestroySampler(device, made, nullptr);`
- [Tools/build.ps1:2](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L2) — `[CmdletBinding()]`
- [Tools/build.ps1:4](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L4) — `[ValidateSet('Game')][string]$Target = 'Game',`
- [Tools/build.ps1:21](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L21) — `$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

## 판정과 실행 근거

Release의 제품 VulkanTextureCache에서도 CECT typed acquisition→정확한 image 재취득→업로드→SampleLevel→GPU readback을 한정 수용한다. 동일한 offline cooked fixture를 DX12와 Vulkan에 각각 전달했다. BC5Unorm·BC7Unorm·BC7UnormSrgb 각8×8·4mip(8/4/2/1), 백엔드별1020성분, 총2040성분 비교가 통과했다. runtime 입력에는 원본 이미지가 없다.

명령: `Tools/regression/verify-texture-cooked-gpu.ps1 -Backend All`.

최종 case: `Build/Obj/TextureCookedGpu/0eee1a63ee054a11bff4dde31b57b424`. `result.json`, `fixture.log`, `dx12-probe.log`, `vulkan-probe.log`, 두 `*-oracle-negative.log` 및 네 build log가 근거다. 최신 위치는 `Build/Phase12CoreVerification/cooked-gpu-root.txt`다. 현재 RenderEngine→fixture→DX12 probe→Vulkan probe를 순차 Release Build한 뒤 실행했다.

| 형식 | 각 백엔드 mip/성분 | DX12 최대 linear 절대차 | Vulkan 최대 linear 절대차 |
|---|---:|---:|---:|
| BC5Unorm | 4/340 | 0.0104634 | 0.0104634 |
| BC7Unorm | 4/340 | 5.96046e-08 | 5.96046e-08 |
| BC7UnormSrgb | 4/340 | 0.00169456 | 0.00169456 |

두 실행 각각 exit0, DX12 GPUValidation 및 Vulkan validation layer 활성화, 제품 진단의 경고/오류0, 실제 submission 완료/readback 및 종료 pending task/batch/retirement0/0/0을 확인했다. Vulkan의 미구현 device/encoder 연산도0이다. GPU 캐시의 중립 텍스처 대체/업로드 실패 없이 전체4mip가 유지됐다. 입력17파일 sourceDrift0·fixture6파일 hash 보존을 확인했다. 최대차가 같은 것은 각 CPU oracle 비교의 최대값 관찰이며 모든 DX12/Vulkan readback의 직접 bit 비교 결과는 아니다.

## 무결성과 비교기 음성 검사

각 백엔드에서 세 형식 각각 같은 크기·strict CECT 구조가 유효한 payload 한 바이트 손상을 만들었다. metadata mount 성공 뒤 실제 RequestAsync<Texture>가 Failed/IntegrityFailed·SHA-256 원인·descriptor 없음으로 거부했다. 총6건이다. 정상 artifact를 새 mount로 취득하고 unmount 후 정확한 image를 재취득한 다음 GPU에 소비했다.

각 백엔드의 별도 복사본에서 첫 BC5 reference를1.0으로 바꿨다. 실제0.098039와의 mip0/x0/y0/component0 불일치로 두 실행 모두 exit1을 반환했다. 원래 fixture는 보존했다.

비교 기준은 [직전 DX12 검증 기록](TextureCookedGpuValidation20261010.md)의 CPU decompression oracle와 형식별 제품 회귀 기준을 유지했다. BC5 endpoint span 기준, BC7 float epsilon1e-7, sRGB RGB의 역변환 후0.5 ULP 및 alpha/끝점 엄격 비교다. 이번 Vulkan 결과를 Vulkan 규격 전체 conformance 수용으로 확대하지 않는다.

## 변경과 남은 경계

공유 probe에 Vulkan device/cache/pipeline/pool, SPIR-V 및 실제 업로드 완료 상태 ShaderResource 분기를 추가했다. DX12는 기존 PixelShaderResource 상태를 유지한다. Vulkan 프로젝트를 추가하고 실행기가 선택 백엔드 또는 All을 같은 fixture로 검사하며 백엔드별 바이너리 SHA/로그/음성 결과를 기록하도록 확장했다. 제품 코드 수정은 없다. 최초 실행기의 잘못된 VulkanBindingTable.cpp hash 경로는 실제 .h로 수정했다. 해당 실행은 runtime 시작 전에 중단돼 수용 근거에서 제외했다.

메모리 cooked AssetSet의 standalone2D Release 검증이다. Shipping PAK에서 동일 BC 형식의 실제 소비, Debug·cube/array·다른 dimensions/포맷·전체 소비자/모델 역할·GLB 원본 제거·BC5 normal Z 재질 결합·압축 품질/시간/메모리는 잔여다. 이전 Shipping PNG/HDR 원본 부재·누락/잘림 검증과 이번 BC sampling은 별도 경계다. T2 전체 gate를 닫지 않아 추가 가중치 없이66.25%, T1b 대기·T3 중단을 유지한다.

</details>
