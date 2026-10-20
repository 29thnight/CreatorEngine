# MAT-7 공통 LX graphics shader·PSO 소유권

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- live material은 compiled graph 프로그램과 밀봉된 scene/view/geometry 입력을 소비한다. 준비·실패 상태에서 옛 instance를 대체 경로로 선택하는 구안을 현행 계약으로 사용하지 않는다.
- GPU scene packet·resident table·visibility/REAC 최적화는 구현 기반이다. shader의 물리 의미, route별 출력과 전체 비용의 수용은 소스 존재와 별개다.

### 해당 구현에서 확인한 선언·소유 경계

- [Tools/build.ps1:2](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L2) — `[CmdletBinding()]`
- [Tools/build.ps1:4](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L4) — `[ValidateSet('Game')][string]$Target = 'Game',`
- [Tools/build.ps1:21](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L21) — `$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))`
- [Tools/AssetCooker/AssetCooker.cpp:123](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/AssetCooker/AssetCooker.cpp#L123) — `const auto size = std::filesystem::file_size(path, error);`
- [Tools/AssetCooker/AssetCooker.cpp:139](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/AssetCooker/AssetCooker.cpp#L139) — `while (std::getline(lines, line))`
- [Tools/AssetCooker/AssetCooker.cpp:156](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/AssetCooker/AssetCooker.cpp#L156) — `tokens.push_back(std::move(token));`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

**2026-10-02 · 네 번째 구현 단계 · MAT-7 전체는 진행 중**

## 구현 범위

`LX::Runtime::GraphicsGeneration`이 shader 계약 owner, 실제 graphics compile 식별값, 소유 바이트코드·입력 레이아웃·고정 상태·PSO 요청을 함께 보관한다. RHI 캐시는 native 객체 생성·공유·GPU 완료 후 폐기를 계속 담당한다. ShaderMeta는 저작 Code 입력 또는 Graph 생성 계약의 표현이다.

- Code GBuffer/Forward와 Graph Scene host·Scene packet·low-level product slot이 동일 `GraphicsPipeline`을 사용한다. Graph의 shadow·GBuffer·color·lookup·SSS·refraction graphics 요청도 연결했다.
- Code의 실제 effective permutation으로 VS/PS 바이트코드와 reflection을 함께 얻는다. keyword 외에 모델 vertex mask, Forward tile 설정과 Reference define, backend, stage/profile, compiler 옵션과 각 stage의 실제 dependency identity를 보존한다. 별도의 unmasked reflection으로 모델 변형을 검증하던 중복 경로를 제거했다.
- cooked Graph는 검증된 전체 target table·bytecode와 compiler/include/options를 포함한 sealed program identity를 사용한다. Player 복구 시 소스 파일·reflection·Slang 로딩이 필요하지 않다. LXMC v3와 기존 계약 형식은 유지한다.
- Code draw snapshot이 accepted graphics generation을 보관한다. GBuffer/Forward는 snapshot의 handle·material permutation·layout·vertex mask·Reference 조건과 일치하는 owner에서 PSO를 선택한다. 소유자 목록이 없는 기존 격리 fixture의 lookup 경로는 유지한다.
- 새 shader/PSO 후보 전체가 성공한 뒤만 native pass의 준비 세대를 바꾼다. 그 뒤 material/texture sealing이 실패할 수 있으므로 이전 기본 세대도 variant owner로 보관한다. 전체 frame sealing 성공 후 `CommitShaderMetaFrame`에서 사용하지 않는 세대를 제거하고, 남은 cache holder를 검사한 뒤 native PSO를 fence 기준으로 retire한다.
- 비동기 후보는 모든 요청이 Ready일 때만 게시한다. Pending request 저장소는 재준비·Create·Replace로 덮어쓸 수 없고, RHI 요청이 임시 bytecode/input semantic 문자열을 복사한다.

## 검증

실제 빌드·실행 결과와 로그는 아래에 기록한다. CPU 소유권/실패 주입 검사와 실제 GPU 렌더 회귀는 서로 구분한다.

| 게이트 | 결과 |
|---|---|
| 실제 Code GBuffer/Forward DXIL·SPIR-V compile/reflection·모델/keyword/Reference 변형 | 아래 소유권 검사와 함께 Debug/Release 각각 186항목 통과 |
| 임시 bytecode/semantic 소유·실패 보존·shared handle·async 후보·frame commit retirement | Debug/Release 각각 위 186항목에 포함해 통과 |
| source-free cooked Graph graphics identity·compiler 미로딩 | Debug/Release 각각 4항목 통과. 기존 실제 AssetCooker Derived 패키지를 새 consumer로 복구 |
| 기존 Code 값/자원/frame sealing 및 Graph 공통 소비·warm cache | Debug/Release 각각 35항목·25항목 통과 |
| 실제 DX12 Scene packet GPU 경로 | Debug/Release 각각 169항목·GPU 16성분·in-flight 2제출·compiled 10 통과, GPU validation 활성화 |
| 실제 Vulkan Scene host 비동기 PSO 준비·교체 | Debug/Release 모두 768픽셀·Scene program 2개·Ready 요청 18개·PSO worker 13개, stale 거부 65·실패 주입 2·validation 0 통과 |
| 기존 low-level product/runtime 회귀 | product 121항목·compiled 10·GPU 60성분, runtime 43항목·동시 load 8 통과 |
| Debug/Release 엔진·consumer 및 Debug 에디터 빌드 | 두 구성 엔진·consumer, Debug 전체 에디터·RenderTests·실행 런처 빌드 통과 |
| 대시보드·게이트 구문 | JS 1개 script와 42개 phase 진행률 finite, MAT-7 progress/days:null 유지. PowerShell 게이트 구문 통과 |

로그는 `Build/Obj/MaterialShaderMetaProbe/lx-pipeline-*`에 보존한다. Code/Graph CPU 검사는 `debug-consumer.log`·`release-consumer.log`, 실제 DX12 렌더는 `debug-gpu.log`·`release-gpu.log`, Scene host는 `vulkan-debug.log`·`vulkan-release.log`, low-level 회귀는 `product-gate.log`, 전체 에디터는 `creatoreditor-debug-build.log`다. 이 Vulkan fixture는 기존 보조 컴파일과 비동기 Scene host 경로를 검사하며, source-free generated Graph의 전체 제품 이미지 수용은 후속 게이트다. 검사의 polling 단정 수는 Debug 82,774·Release 9,311로 실행 타이밍에 따라 달라지므로 고정 픽셀·준비/거부 수와 validation을 판정값으로 사용한다.

source-free fixture는 `Run-7cd61e88a7aa4d408d28c6a88b693025/Pipeline-Debug-e41262e55a76437b9dea8d877bf1ab78/`와 `Pipeline-Release-e2d20bb367fb4570bff87b0cea4f6138/`다. 이전 쿠커 산출물과의 소비 호환을 검사했고 이번 단계에서 쿠커를 다시 실행하거나 encrypted PAK/전체 모델 성능을 재검증했다고 세지 않는다.

Release probe 링크에서 기존 `Utility_Framework.pdb`의 LNK4020 형식 레코드 경고가 관찰됐다. 실제 CPU/GPU 실행은 통과했으나 Release 디버거의 일부 타입 정보는 제한될 수 있으며 이 캐시의 원인을 해결했다고 선언하지 않는다. Debug 에디터 빌드의 `/bigobj`는 빌드 환경에만 적용했고, 기존 Vulkan delay-load LNK4229 경고가 남아 있다.

## 다음 통합과 수용 범위

1. 일반 alpha Blend Graph와 Code를 공통 Forward+ light list·혼합 정렬·depth/HDR 합성에 연결한다.
2. Volume coefficient와 공통 IBL/lookup compute 요청은 기존 보조 owner를 유지한다. 공통 compute 계약의 남은 연결과 native pass compatibility key/registry 분기 정리는 MAT-7 통합 범위에 남긴다. RHI의 실제 bytecode/fixed-state 캐시와 LX의 full compile identity 보존을 authored handle lookup 제거 완료로 확대하지 않는다.
3. Preview/Scene/Game·cooked Player의 mixed Graph/Code draw, GPU 이미지·재import/실패 복구·encrypted PAK 재실행 및 전체 모델 성능을 제품 게이트로 검증한다. 이 문서의 native API 실패 검사를 제품 전체 hot reload 재검증으로 세지 않는다.

MAT-7 `progress`/미산정, PHASE 4.25 열림, BRDF 1024 / environment 4096을 유지한다.

</details>
