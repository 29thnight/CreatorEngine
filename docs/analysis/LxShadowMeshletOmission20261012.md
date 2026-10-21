# LX 보이는 그림자 메시렛 누락 변이 (2026-10-12)

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- live material은 compiled graph 프로그램과 밀봉된 scene/view/geometry 입력을 소비한다. 준비·실패 상태에서 옛 instance를 대체 경로로 선택하는 구안을 현행 계약으로 사용하지 않는다.
- GPU scene packet·resident table·visibility/REAC 최적화는 구현 기반이다. shader의 물리 의미, route별 출력과 전체 비용의 수용은 소스 존재와 별개다.

### 해당 구현에서 확인한 선언·소유 경계

- [Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/MaterialGraphSceneHost.slang:18](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/MaterialGraphSceneHost.slang#L18) — `struct LXSceneLight`
- [Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/MaterialGraphSceneHost.slang:82](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/MaterialGraphSceneHost.slang#L82) — `uint LXSceneOwner(uint record) { return record != 0u ? lx_sceneRecords[record].material.z : lx_drawOwner; }`
- [Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/MaterialGraphSceneHost.slang:471](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Dynamic_CPP/Assets/Shaders/DefaultPassShader/Includes/MaterialGraphSceneHost.slang#L471) — `void LXSceneShadowMS(uint3 group : SV_GroupID, uint lane : SV_GroupIndex,`
- [Engine/RenderEngine/DataSystem.h:30](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/DataSystem.h#L30) — `own::shared_owner<const Texture> descriptor;`
- [Engine/RenderEngine/DataSystem.h:44](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/DataSystem.h#L44) — `std::vector<own::shared_owner<const Material>> materials;`
- [Engine/RenderEngine/DataSystem.h:45](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/DataSystem.h#L45) — `std::vector<own::shared_owner<const Texture>> textures;`
- [Engine/Utility_Framework/PathFinder.h:9](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/PathFinder.h#L9) — `class InternalPath : public Singleton<InternalPath>`
- [Engine/Utility_Framework/PathFinder.h:75](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/PathFinder.h#L75) — `file::create_directories(runtimeDirectory, runtimeDirError);`
- [Engine/Utility_Framework/PathFinder.h:77](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/PathFinder.h#L77) — `!file::is_directory(runtimeDirectory, runtimeDirError) || runtimeDirError)`
- [Engine/RenderEngine/MaterialGraphSceneHost.h:98](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/MaterialGraphSceneHost.h#L98) — `class SceneHost final : private IRHIUploadTransactionListener`
- [Engine/RenderEngine/MaterialGraphSceneHost.h:101](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/MaterialGraphSceneHost.h#L101) — `explicit SceneHost(job_scheduler& scheduler = ce::get_job_scheduler());`
- [Engine/RenderEngine/MaterialGraphSceneHost.h:108](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/MaterialGraphSceneHost.h#L108) — `bool RequestProgram(const EnhancedFrameContext& context, own::shared_owner<const Generation> generation,`
- [Tools/build.ps1:2](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L2) — `[CmdletBinding()]`
- [Tools/build.ps1:4](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L4) — `[ValidateSet('Game')][string]$Target = 'Game',`
- [Tools/build.ps1:21](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L21) — `$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))`
- [Engine/RenderEngine/DataSystem.cpp:3551](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/DataSystem.cpp#L3551) — `const auto shaderDirectory = PathFinder::RelativeToShader("DefaultPassShader");`
- [Engine/RenderEngine/DataSystem.cpp:4355](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/DataSystem.cpp#L4355) — `std::string& error, const file::path& shaderDirectory = PathFinder::RelativeToShader("DefaultPassShader"),`
- [Engine/RenderEngine/DataSystem.cpp:4478](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/DataSystem.cpp#L4478) — `own::shared_owner<const material_graph::Generation> DataSystem::CompileMaterialGraphAuthoring(`
- [Engine/RenderEngine/MaterialGraphSceneHost.cpp:61](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/MaterialGraphSceneHost.cpp#L61) — `std::memcpy(local, static_cast<const std::byte*>(geometry.vertexData) + index * geometry.vertexStride,`
- [Engine/RenderEngine/MaterialGraphSceneHost.cpp:87](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/MaterialGraphSceneHost.cpp#L87) — `sphere.radius = std::hypot(high[0] - low[0], high[1] - low[1], high[2] - low[2]) * .5f * 1.0001f + 1e-4f;`
- [Engine/RenderEngine/MaterialGraphSceneHost.cpp:89](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/MaterialGraphSceneHost.cpp#L89) — `if (!shadow_math::IntersectsClip(sphere, lightViewProjection))`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

후속 목록 4번의 변이는 실제 GPU 그림자 셰이더에서 비스킨드 사각형의 첫 메시렛을 제외한다. 깊이 파일을 사후 편집하는 검사가 아니다. 저장소의 원본 셰이더·C++ 소스·실행 파일을 유지하고, 실행마다 복사한 격리 프로젝트의 `Includes/MaterialGraphSceneHost.slang`만 변경한다. SceneHost는 그 프로젝트의 셰이더를 실제 컴파일한다.

```slang
const bool visible = LXSceneMeshletVisible(remap, vertexCount, lane, lx_shadowViewProjection)
    && !(group.x == 0u && vertexCount == 4u);
```

변경 대상은 `LXSceneShadowMS` 안의 유일한 판정이다. 네 정점 사각형의 메시렛은 생략하지만 스킨드 스트립·indexed 경로·카메라 셰이더·캐스케이드 행렬은 그대로 유지한다. 각 캐스케이드에서 일부 깊이만 누락되고 남은 스킨드 그림자는 계속 존재해야 예상 거부로 인정한다.

## 판정

**후속 목록 4번 완료.** Debug/Release 변이를 모두 의도한 깊이 누락으로 거부했다. 새 격리 프로젝트에서 실행한 두 정상 대조군은 6개 깊이·행렬 비교 모두 비트 동치로 통과했다. 네 실행 모두 GPU validation 활성·문제 0·누락 메시지 0, quit 성공, 실행 전후 소스·binary·격리 셰이더 해시 일치를 확인했다. 변이는 종료 코드 4, 정상은 0이다.

| 포즈 | 캐스케이드 | 참조 깊이 텍셀 | 누락 = 불일치 | 추가 | 최대 절대 오차 |
|---|---|---:|---:|---:|---:|
| 바인드 | C0 | 45,878 | 15,292 | 0 | 0.438993335 |
| 바인드 | C1 | 8,381 | 2,797 | 0 | 0.535633683 |
| 바인드 | C2 | 449 | 143 | 0 | 0.563969493 |
| 본 이동 | C0 | 45,856 | 15,292 | 0 | 0.438993335 |
| 본 이동 | C1 | 8,379 | 2,797 | 0 | 0.535633683 |
| 본 이동 | C2 | 439 | 143 | 0 | 0.563969493 |

6개 비교 모두 행렬 비트 동치다. 각 meshlet 프레임의 캐스케이드별 제출 횟수는 2, indexed는 0이다. 제출은 성공했지만 셰이더가 실제 깊이 일부를 생략했다. 기존 첫 캐스케이드 포즈 변화는 여전히 40,762 텍셀이므로, 포즈 변화 검사만으로는 이 누락을 잡지 못한다. 새 3장 동치 oracle이 `dx12.skinning`을 실패시킨다.

변이의 commandlet 종료 코드 4는 의도한 `CommandStatus::Failed`다. `quit`은 성공하고 GPU validation 활성·문제 0·누락 메시지 0이어야 한다. 단순 실행 실패나 검증 레이어 오류는 예상 거부로 인정하지 않는다. 스크립트의 `verdict.passed=true`는 **예상 거부 검사 성공**이며, 렌더 테스트 자체의 `passed=false` 및 `equivalencePassed=false`와 구별한다.

## 재현·증거

현재 Editor Debug/Release 빌드를 준비한 뒤 실행한다. C++·기본 셰이더는 이전 [3장 동치 검증](LxShadowEquivalence20261012.md)의 빌드와 같고, 이번 변경은 검증 스크립트에 있다.

```powershell
& Tools/regression/verify-lx-shadow-baseline.ps1 `
    -Configuration Debug `
    -FixtureProject Build/Obj/LxShadowP0/Fixture `
    -OutputDirectory Build/Verification/ShadowOmission-New/Mutant-Debug `
    -ExpectedVisibleMeshletRejection
```

Release는 구성·출력 디렉터리를 바꾼다. 정상 대조군은 변이 스위치 대신 `-ShadowEquivalenceOnly`로 새로운 격리 프로젝트에서 실행한다. 변이 플래그는 동치 모드를 자동 활성화하고 backOff 변이 모드와 동시 사용을 거부한다.

- 스크립트 (`../../Tools/regression/verify-lx-shadow-baseline.ps1` — 현행 master에 없는 이전/제안 경로; 당시 기록으로만 참조)
- Debug 변이 판정 (`../../Build/Verification/ShadowMeshletOmission-20261012/Mutant-Debug/verdict.json` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조), 비교 결과 (`../../Build/Verification/ShadowMeshletOmission-20261012/Mutant-Debug/equivalence.json` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조), 변이 원본·규칙·해시 (`../../Build/Verification/ShadowMeshletOmission-20261012/Mutant-Debug/mutation.json` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조), 명령별 결과 (`../../Build/Verification/ShadowMeshletOmission-20261012/Mutant-Debug/results.jsonl` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)
- Release 변이 판정 (`../../Build/Verification/ShadowMeshletOmission-20261012/Mutant-Release/verdict.json` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조), 비교 결과 (`../../Build/Verification/ShadowMeshletOmission-20261012/Mutant-Release/equivalence.json` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조), 변이 원본·규칙·해시 (`../../Build/Verification/ShadowMeshletOmission-20261012/Mutant-Release/mutation.json` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조), 명령별 결과 (`../../Build/Verification/ShadowMeshletOmission-20261012/Mutant-Release/results.jsonl` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)
- Debug 정상 판정 (`../../Build/Verification/ShadowMeshletOmission-20261012/Normal-Debug/verdict.json` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조), 동치 상세 (`../../Build/Verification/ShadowMeshletOmission-20261012/Normal-Debug/equivalence.json` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)
- Release 정상 판정 (`../../Build/Verification/ShadowMeshletOmission-20261012/Normal-Release/verdict.json` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조), 동치 상세 (`../../Build/Verification/ShadowMeshletOmission-20261012/Normal-Release/equivalence.json` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)
- 전체 판정·원본 대조 (`../../Build/Verification/ShadowMeshletOmission-20261012/overall.json` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)
- 문서 구조·링크·스크립트 검사 (`../../Build/Verification/ShadowMeshletOmission-20261012/document-verification.json` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)

각 실행의 `identity-before.json`·`identity-after.json`은 저장소 소스, exe/runtime, 실제 사용한 격리 셰이더와 실행 스크립트 해시를 기록한다. `fixture-inputs.json`은 복사·변이 적용 후 입력을 기록한다. `capture-manifest.json`은 삭제 전 깊이·행렬 파일의 크기·해시다.

AGENTS.md에 따라 예상 거부를 확인한 직후 각 변이 실행의 `.f32` 파일 12개를 삭제했다. 로그·판정·행렬·해시·변이 셰이더는 보존하고, `pixel-retention-*.json`과 `.result.json`에 삭제 목록 및 실제 회수 공간을 남겼다. 변이의 역사적 판정은 보존되지만 삭제한 픽셀은 재생할 수 없다. 이전 정상 원본에는 변경·삭제를 적용하지 않았다.

변이의 indexed 깊이 6장 및 행렬 12개는 이전 3장 동치 검사의 동일 구성 정상 원본과 SHA256이 같다. meshlet 깊이 6장은 모두 다르다. 새 정상 재실행의 깊이·행렬 24개도 이전 원본과 모두 같다. 이 대조 후 새 정상의 중복 `.f32` 12개씩도 삭제 기록을 남겼다. 정상 픽셀 재생은 이전 검사의 `Debug/captures`·`Release-Final/captures`에서 가능하며, 각 새 정상의 retention 기록에 대표 원본 경로를 명시했다. 네 번의 정리마다 실제 디스크 회수량은 약 0.19GiB로 기록되었다. 이는 파일 길이의 합이 아닌 삭제 전후 가용 공간 차이다.

이전 검사 이후 저장소 C++·셰이더·Debug/Release exe/runtime 해시는 바뀌지 않았다. 검증 스크립트만 확장했으므로 C++ 재빌드를 반복하지 않았다. 격리된 변이 셰이더를 읽고 컴파일하는 경로는 `GraphShadowCasterFixture::Initialize` → `PathFinder::RelativeToShader` → `DataSystem::CompileMaterialGraphAuthoring`이며, 실제 누락 깊이와 정상 재실행이 변경 적용·제거를 확인한다.

## 범위

DX12의 두 합성 메시·두 포즈에서 비스킨드 사각형 메시렛 하나의 누락을 검증기가 거부하는 증거다. mesh dispatch 전체 실패·맵 전체 소실로 대체하지 않는다. 복잡한 메시의 중간 메시렛, 알파 마스크·면 판정·변환·캐스케이드 경계는 후속 5번의 확대 장면 범위다. compute 사전 컬링의 생존 목록 검증이나 성능 수용은 아니다. Vulkan은 PHASE 4.9에 남긴다.

네 실행의 GPU validation 메시지 1건은 레이어 활성 안내다. 기존 profiler 종료 진단은 이번 범위에서 해결하지 않는다. 검증 레이어를 켠 실행 시간은 성능 수용에 사용하지 않는다.

문서 갱신 중 이전 단계의 두 분석 HTML에서 문자 단위 치환 오류를 발견해 복구했다. 대화 실행 기록의 손상 전 원본과 대조하여 CSS 일치와 승인된 설명 변경만 남은 것을 확인했다. 세 HTML의 시작·종료 루트, 실제 태그 중첩, 로컬 링크 및 스크립트 구문·diff 검사를 다시 통과했다. GPU 테스트 소스·셰이더 원본·실행 결과에는 이 문서 오류가 적용되지 않았다.

</details>
