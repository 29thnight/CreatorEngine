# LX 그림자 DX12 현재 빌드 회귀 기준선 (2026-10-11)

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- live material은 compiled graph 프로그램과 밀봉된 scene/view/geometry 입력을 소비한다. 준비·실패 상태에서 옛 instance를 대체 경로로 선택하는 구안을 현행 계약으로 사용하지 않는다.
- GPU scene packet·resident table·visibility/REAC 최적화는 구현 기반이다. shader의 물리 의미, route별 출력과 전체 비용의 수용은 소스 존재와 별개다.

### 해당 구현에서 확인한 선언·소유 경계

- [Engine/RenderEngine/Render/Passes/Geometry/EnhancedShadowPass.cpp:8](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Passes/Geometry/EnhancedShadowPass.cpp#L8) — `bool EnhancedShadowPass::Initialize(const EnhancedFrameContext& context, std::string& outError)`
- [Engine/RenderEngine/Render/Passes/Geometry/EnhancedShadowPass.cpp:19](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Passes/Geometry/EnhancedShadowPass.cpp#L19) — `void EnhancedShadowPass::ComputeCascades(const EnhancedFrameContext& context)`
- [Engine/RenderEngine/Render/Passes/Geometry/EnhancedShadowPass.cpp:63](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Passes/Geometry/EnhancedShadowPass.cpp#L63) — `if (math::length_sq(m_lightDirection) < 1e-6f)`
- [Engine/RuntimeHost/CommandCore/CommandSession.h:28](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RuntimeHost/CommandCore/CommandSession.h#L28) — `struct SessionEntry`
- [Engine/RuntimeHost/CommandCore/CommandSession.h:36](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RuntimeHost/CommandCore/CommandSession.h#L36) — `class CommandSession`
- [Engine/RuntimeHost/CommandCore/CommandSession.h:38](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RuntimeHost/CommandCore/CommandSession.h#L38) — `public:`
- [Engine/RenderEngine/Render/Passes/Geometry/EnhancedShadowPass.h:25](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Passes/Geometry/EnhancedShadowPass.h#L25) — `class EnhancedShadowPass : public EnhancedRenderPass`
- [Engine/RenderEngine/Render/Passes/Geometry/EnhancedShadowPass.h:27](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Passes/Geometry/EnhancedShadowPass.h#L27) — `public:`
- [Engine/RenderEngine/Render/Passes/Geometry/EnhancedShadowPass.h:36](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RenderEngine/Render/Passes/Geometry/EnhancedShadowPass.h#L36) — `const char* GetName() const override { return "Shadow"; }`
- [Tools/build.ps1:2](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L2) — `[CmdletBinding()]`
- [Tools/build.ps1:4](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L4) — `[ValidateSet('Game')][string]$Target = 'Game',`
- [Tools/build.ps1:21](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L21) — `$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))`
- [Engine/RuntimeHost/CommandCore/CommandSession.cpp:25](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RuntimeHost/CommandCore/CommandSession.cpp#L25) — `CommandSession& CommandSession::Batch()`
- [Engine/RuntimeHost/CommandCore/CommandSession.cpp:31](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RuntimeHost/CommandCore/CommandSession.cpp#L31) — `void CommandSession::Record(std::string_view commandId, const CommandResult& result)`
- [Engine/RuntimeHost/CommandCore/CommandSession.cpp:35](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/RuntimeHost/CommandCore/CommandSession.cpp#L35) — `entry.commandId = std::string(commandId);`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

**후속 목록 2번 완료.** 현재 작업 트리에서 DX12 Debug/Release를 빌드하고 backOff 확장 제거 변이를 실행한 뒤, 원본 소스를 바이트 단위로 복구·재빌드하여 정상 회귀를 실행했다. Vulkan 실행·교차 비교는 [PHASE 4.9](../plans/BackendParityPlan.md)에 남긴다.

## 판정

| 검사 | Debug | Release |
|---|---|---|
| `dx12.shadowquality` | 통과 | 통과 |
| `dx12.skinning` | 통과 | 통과 |
| `dx12.iblshade` | 통과 | 통과 |
| `dx12.rendergraph` | 통과 | 통과 |
| `dx12.scene` | 통과 | 통과 |
| backOff 확장 제거 변이 | 먼 판 그림자 누락으로 예상 거부 | 먼 판 그림자 누락으로 예상 거부 |
| GPU 검증 레이어 | GPU 모드 활성, 문제 0, 누락 메시지 0 | GPU 모드 활성, 문제 0, 누락 메시지 0 |
| 정상 회귀 프로세스 | 종료 코드 0, quit 성공 | 종료 코드 0, quit 성공 |

정상·변이 네 실행 모두 실행 전후 소스·실행 파일 해시가 일치했다. 복구된 소스와 최종 Debug/Release 실행 파일도 정상 회귀의 해시와 다시 대조했다. 기존 작업 트리 변경은 보존했고, 문서 갱신 전 상태 비교에서 추가된 항목은 새 검증 스크립트뿐이었다.

## 확인한 동작과 범위

- 수직 광원 아래 판 높이 1823.7: 정상 코드는 깊이 범위 2270.1, 중앙 그늘 0 → 2622. backOff 제거 변이는 깊이 범위 729.5, 그늘 0 → 0으로 실패했다. Debug/Release 결과가 같다.
- 스키닝 그림자: 첫 캐스케이드에서 맵 텍셀 45,856, 포즈에 따른 변화 40,762. 비어 있지 않음·포즈 변화의 검증이며 정확한 형상이나 3개 캐스케이드 전체 동치 수용은 아니다.
- 장면: Suzanne 모델과 카메라·방향광을 배치하고 실제 렌더 준비 완료를 기다렸다. 두 구성 모두 드로우 1, 광원 1, 커버리지 276/65,536. `dx12.scene`은 내부적으로 최대 1,024개 드로우·여러 재질·순차/병렬 기록 회귀도 실행한다.
- 렌더 그래프: RG5 sprite/screen/final 및 RG3/RG4 실행·픽셀 비교와 transient 재사용 검사가 통과했다.
- GPU validation ledger의 메시지 1건은 GPU-Based Validation 활성화 안내다. 문제 0건이며 메시지 전체가 0이라고 표현하지 않는다.
- 두 정상 실행과 변이 실행의 stderr에는 `[profiler] shutdown abandoned=1 retained=1 foreign=0 - [RHIThread]` 진단이 있다. GPU 검증 오류는 아니며, 이번 결과로 프로파일러 종료 진단까지 해결됐다고 주장하지 않는다.
- GPU 검증 레이어를 켠 기능 회귀다. 로그의 GPU/CPU 시간과 순차/병렬 배율을 일반 성능 수용에 사용하지 않는다.
- 그림자 맵 3장의 indexed/meshlet 동치, 메시렛 누락 변이, 알파 마스크·변환·경계 확대 검증, 새 compute 생존 목록 비교는 다음 작업이다. 본 검사는 GPU readback을 메모리에서 판정하며, 경로 동치 검사용 3장 원본 깊이 캡처를 확보한 것은 아니다.

## 재현과 증거

반복 실행 스크립트: verify-lx-shadow-baseline.ps1 (`../../Tools/regression/verify-lx-shadow-baseline.ps1` — 현행 master에 없는 이전/제안 경로; 당시 기록으로만 참조). 현재 Editor를 먼저 빌드하고 구성, 작은 fixture 프로젝트, 새로운 출력 폴더를 지정한다. 스크립트는 프로젝트를 복사하고 현행 셰이더·입력 manifest·소스/binary 해시·명령 결과·stdout/stderr·판정을 보관한다. 변이 옵션은 별도로 빌드한 backOff 제거 실행 파일에서만 사용한다.

아티팩트 용량 검사를 통과한 뒤 약 12 MB의 fixture를 복사했다. 시작 여유 공간은 약 179 GiB였다. 원본 fixture와 사용자 자산을 수정하지 않았으며, 이 변이 실행은 픽셀 payload 복사본을 만들지 않았다. 성공 보존 ledger와 로그·해시는 유지한다.

- Mutant-Debug: 판정 (`../../Build/Verification/ShadowBaseline-20261011T142012Z/Mutant-Debug/reviewed-verdict.json` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조), 명령 결과 (`../../Build/Verification/ShadowBaseline-20261011T142012Z/Mutant-Debug/results.jsonl` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조), 실행 신원 (`../../Build/Verification/ShadowBaseline-20261011T142012Z/Mutant-Debug/identity-before.json` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조).
- Mutant-Release: 판정 (`../../Build/Verification/ShadowBaseline-20261011T142012Z/Mutant-Release/verdict.json` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조), 명령 결과 (`../../Build/Verification/ShadowBaseline-20261011T142012Z/Mutant-Release/results.jsonl` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조), 실행 신원 (`../../Build/Verification/ShadowBaseline-20261011T142012Z/Mutant-Release/identity-before.json` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조).
- Normal-Debug: 판정 (`../../Build/Verification/ShadowBaseline-20261011T142012Z/Normal-Debug/verdict.json` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조), 명령 결과 (`../../Build/Verification/ShadowBaseline-20261011T142012Z/Normal-Debug/results.jsonl` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조), 실행 신원 (`../../Build/Verification/ShadowBaseline-20261011T142012Z/Normal-Debug/identity-before.json` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조).
- Normal-Release: 판정 (`../../Build/Verification/ShadowBaseline-20261011T142012Z/Normal-Release/verdict.json` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조), 명령 결과 (`../../Build/Verification/ShadowBaseline-20261011T142012Z/Normal-Release/results.jsonl` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조), 실행 신원 (`../../Build/Verification/ShadowBaseline-20261011T142012Z/Normal-Release/identity-before.json` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조).

- 최종 재검토 집계 (`../../Build/Verification/ShadowBaseline-20261011T142012Z/reviewed-overall.json` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조), 소스 복구 확인 (`../../Build/Verification/ShadowBaseline-20261011T142012Z/restoration.json` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조), 실행·빌드 기록 (`../../Build/Verification/ShadowBaseline-20261011T142012Z/orchestration.log` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조).
- Debug 변이의 최초 harness는 실패 종료 코드를 0/1로 잘못 예상했다. 실제 명령은 의도한 이유로 실패하고 정상 종료했으며 `CommandSession::ExitCodeFor(Failed)` 계약은 4다. 스크립트를 수정하고 원본 결과·최초 판정·최초 집계를 보존한 채 `reviewed-verdict.json`과 `reviewed-overall.json`에서 재판정했다. Release 변이와 두 정상 회귀는 수정된 스크립트로 통과했다.

최종 정상 runtime SHA-256:

- Debug: `A0A57F115873729F8D316A36D82A8C2DE44A683A33FBACD46BC55C2CF30439FD`
- Release: `DC9C0E79FCA4E3ED7B17A1A936847D36A07FC60B50F3C2BECAC71EB78F2D9162`
- 복구된 `EnhancedShadowPass.cpp`: `A75EB1D2C8F41EA226BC04F8F2686BBE4F5DB188D1177DAC309B7C418FD21625`

</details>
