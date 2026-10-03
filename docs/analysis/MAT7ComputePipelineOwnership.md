# MAT-7 보조 compute 소유권·프레임 조회 분기 정리

**2026-10-02 · 여섯 번째 구현 단계 · MAT-7/PHASE 4.25 진행 중**

## 구현 범위

- `LX::Runtime::ComputePipeline`이 immutable `ComputeGeneration`을 게시한다. 세대는 CS 바이트코드 사본, layout/PSO 핸들, backend·entry/profile·실제 permutation/options·dependency 또는 cooked sealed identity를 함께 보관한다. renderer helper는 material schema를 요구하지 않는다. generated material compute는 기존 공통 `ShaderGeneration` 계약도 유지한다.
- 새 후보 생성 실패는 기존 소유자를 유지한다. 공유 native handle은 교체 시 무효화하지 않으며, 다른 holder가 있는 renderer는 `invalidatePrevious=false`로 명시적으로 retirement를 맡는다. native PSO는 RHI cache가 소유하고 completion/idle 기준으로 회수한다. CPU bytecode owner 자체가 native handle의 영구 유효성을 보장하지 않는다.
- Scene의 mesh transform, lookup bake/clear, SSS bake/filter, 굴절과 Volume 굴절 변형, Volume composite·generated coefficient dispatch를 연결했다. 공통 Forward+의 광원 culling도 기존 `cs_5_0` profile과 tile defines를 보존하며 LX owner를 사용한다.
- Scene frame·RenderGraph callback·Volume binding은 compute generation을 보관한다. 독립 경로의 IBL bake, mesh transform/sample, surface evaluation, raster resolve도 반환된 batch/result에 소유자를 남긴다. sampler 즉시 기록 결과도 해당 세대를 유지한다.
- `DescribeComputeShader`는 검증된 product의 유일한 backend/entry CS target과 owned cooked bytes를 선택한다. source 접근·reflection·compiler 호출 없이 sealed identity를 복구하며, 잘못된 stage 요청은 이전 출력값을 보존한다. shader ABI와 SceneHost identity는 바꾸지 않았다. 기존 identity 10 산출물은 앞선 단계에서 요구한 대로 identity 11로 재쿠킹해야 한다.
- GBuffer/Forward의 `ResolveShaderVariant`는 프레임이 보관한 정확한 graphics generation만 소비한다. 소유자가 비었을 때 최신 pass registry를 다시 조회하던 호환 분기를 제거했다. 렌더 테스트의 snapshot도 pass/variant 준비 이후 명시적으로 소유자를 저장한다. generic Code property 지원은 유지한다.

## 유지하는 경계

- ShaderMeta registry와 pass preparation cache는 Code 입력 계약 로딩·variant 준비·진단용으로 남는다. RT draw의 대체 재질 공급 경로로 쓰지 않는다. 이 단계는 모든 ShaderMeta API/cache 삭제를 뜻하지 않는다.
- 외부에서 이미 컴파일한 bytecode를 받는 독립 IBL/mesh/raster helper는 계약 없는 입력을 계속 허용한다. LX가 bytes/layout/handle을 소유하지만 호출자가 identity를 주지 않으면 source/dependency identity는 복원할 수 없다. 제품 Scene 경로는 실제 compile/cooked identity를 준다. helper에 Principled/BSDF 계약을 강제하지 않는다.
- compute PSO 생성은 기존 동기 cache API를 사용한다. 비동기 compute 생성이나 GPU-driven 구조 변경은 이번 범위에 포함하지 않았다.
- frame/frame result 수명과 native cache retirement 정책을 유지하며, global eviction 정책을 바꾸지 않았다. initialization의 bytecode 복사와 reflection 준비 비용을 전체 프레임 성능 통과로 환산하지 않는다.

## 검증

로그는 `Build/Obj/MaterialProductProbe/compute-*.log`에 남겼다.

| 검증 | 확인된 결과 |
| --- | --- |
| 공통 pipeline runtime | Debug/Release 각각 212 checks 통과. DXIL/SPIR-V 컴파일 및 mock cache의 수명·실패 보존 검증이며 native Vulkan 실행 통과를 뜻하지 않는다. |
| DX12 Scene integration | 2 frames, 16,591,221 checks, stale/fallback/abort 0 |
| DX12 SSS / 굴절 / Volume | 각각 12 / 24 / 33 frames 통과, validation 0 |
| DX12 공통 Forward+ Blend | 12 frames, 21,173 checks, 혼합 정렬·병렬 기록·65광원 overflow 통과, validation 0 |
| DX12 독립 IBL bake | 19,351 checks, 최대 normalized error 5.45189e-06 |
| DX12 독립 MeshSurface | 최종 Debug/Release 각각 1,117,767 checks, 454,560 GPU 성분, 8 masks × 3 poses, 32 submissions 통과. 최대 normalized error 5.89937e-05, SRGB error 0.000689566 |
| DX12 독립 SurfaceBatch / ScenePacket | Debug 각각 19,387 / 169 checks, GPU 19,092 / 16 성분 통과. ScenePacket은 두 in-flight owner 보존을 검사한다. |
| 최종 빌드 | sampler 소유자 분리를 포함한 Debug/Release CreatorEditor 전체 빌드·runtime DLL/launcher 배치 성공. Release 공통 consumer 및 Debug/Release MeshSurfaceProbe 빌드 성공. 에디터 UI의 실제 저장/reload 조작 검증을 뜻하지 않는다. |

독립 MeshSurfaceProbe의 초기 실패 원인은 테스트의 IBL model 지정 불일치였다. 현재 `SurfaceEvaluator`는 Core/Layered 그래프 모두 compensated Principled GGX model 1로 bake한다. 테스트는 Core reference의 `viewTier.w`와 최종 `LXSurfacePS`의 constant를 Core/Layered product index 0/1로 지정했다. reference를 model 1로 고친 뒤에도 Core 픽셀 consumer는 model 0을 받아 `actual=0.457316`, `expected=0.457128`로 실패했다. 최종 픽셀 constant도 model 1로 맞춘 뒤 Debug/Release 전체 MeshSurface 검증이 통과했다. 제품 shader/수학·샘플 설정·오차 tolerance를 바꾸지 않았다. sibling SurfaceBatchProbe의 기존 constant도 model 1이며 같은 계약을 확인했다.

즉시 기록한 sampler 결과는 별도 `samplePipeline_`에 compute generation을 보관한다. transform용 `pipeline_`에 보관하면 `IsPreparedForGraph()`가 true가 되어 즉시 결과를 deferred world transform으로 오인하므로 분리했다. `!IsPreparedForGraph() && IsReadyForEvaluation()` 회귀 검사는 최종 Debug/Release MeshSurface 실행에 포함한다.

최종 실행·빌드는 `compute-final2-*.log`, 해당 코드 27개 파일의 검증 대상 hash는 `compute-final2-source-hashes.json`에 남긴다. 이 검사는 독립 helper와 기존 Scene의 compute 연결을 확인하며 제품 UI 저장/reload·성능 수용까지 확장하지 않는다.

이번 MAT-7 범위를 벗어나 native Vulkan Volume 실행 검증을 추가한 것은 범위 판단 오류였다. 해당 실행은 중단했고 전체 통과로 집계하지 않는다. 재실행하지 않으며 native Vulkan 전체 제품 수용은 기존 PHASE 4.9 범위에 둔다.

성능 측정과 제품 저장/reload/재import/실제 package 재쿠킹 수용은 이 단계와 구분한다. 후속 제품 회귀와 LXMC v4/parser-free 복구는 [MAT7ProductEditingRecovery](MAT7ProductEditingRecovery.md)를 따른다.

## 남은 게이트

MAT-7은 `progress`, 잔여 공수는 미산정이다. 후속 제품 회귀 결과는 위 검증 기록에서 확인하며 일반 alpha와 특수 transport의 혼합 순서는 기존 통합 게이트에 남는다. LX UI 마감은 LX-3, 렌더 결과·전체 CPU/GPU 비용은 MAT-9, Vulkan 전체 제품 수용은 PHASE 4.9가 소유한다. BRDF 1024/environment 4096과 PHASE 4.25 열린 상태를 유지한다.
