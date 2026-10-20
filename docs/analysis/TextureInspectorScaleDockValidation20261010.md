# Texture Inspector UI 배율·내부 분리 검증 (2026-10-10)

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- DataSystem의 typed 요청은 resolver 세대와 취소/게시 상태를 보존한다. descriptor·payload·이미지 owner와 frame pin은 분리된 수명이다.
- ownership_cpp 기반 자원 소유와 논리적 unmount를 GC 객체 수거·GPU 완료와 혼동하지 않는다. DirectXTex cook와 GPU-ready 소비의 제품 범위는 텍스처/AssetDepot 계획에서 수용한다.

### 해당 구현에서 확인한 선언·소유 경계

- [Editor/HostImGuiPresentation/RHI/ImGuiHost.cpp:17](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/HostImGuiPresentation/RHI/ImGuiHost.cpp#L17) — `class ImGuiHost final : public IImGuiHost`
- [Editor/HostImGuiPresentation/RHI/ImGuiHost.cpp:25](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/HostImGuiPresentation/RHI/ImGuiHost.cpp#L25) — `ImGuiWin32Cursor::Reset(hwnd);`
- [Editor/HostImGuiPresentation/RHI/ImGuiHost.cpp:38](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/HostImGuiPresentation/RHI/ImGuiHost.cpp#L38) — `throw std::runtime_error(`
- [Engine/Utility_Framework/Sha256.h:28](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/Sha256.h#L28) — `class Sha256`
- [Engine/Utility_Framework/Sha256.h:30](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/Sha256.h#L30) — `public:`
- [Engine/Utility_Framework/Sha256.h:40](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/Utility_Framework/Sha256.h#L40) — `std::memcpy(m_buffer + m_bufferUsed, p, take);`
- [Tools/build.ps1:2](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L2) — `[CmdletBinding()]`
- [Tools/build.ps1:4](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L4) — `[ValidateSet('Game')][string]$Target = 'Game',`
- [Tools/build.ps1:21](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L21) — `$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

Debug UI 배율0.8과 Release1.5에서 실제 Inspector 내부 분리·숫자 입력0→2·다른 텍스처 선택·원래 선택 복귀·제목 표시줄 X 종료를 확인했다. Debug0.8 재도킹도 확인했다. Release1.5는 여러 이동 시도 후에도 결합된 상태를 확인하지 못했으므로 재도킹 수용에서 제외한다. 제품 결함 원인은 확정하지 않았다.

| 구성 | 실행 폴더 | 실제 UI 관찰 | 파일·종료 판정 |
|---|---|---|---|
| Debug0.8 | `Build/TAUI-Debug-manual-2aae48dd` | 내부 분리, 편집, 선택 복귀, 재도킹에서2 유지 | 12 checks 통과, exit0 |
| Release1.5 | `Build/TAUI-Release-manual-81e5cd18` | 내부 분리·패널 크기 조정, 편집, 선택 복귀에서2 유지; 재도킹 미확인 | 12 checks 통과, exit0 |

## 기계 판정과 실행 신원

`Build/TextureInspectorScale/result.json`에 두 실행의 최종 `editor.theme` receipt, runtime SHA256, 입력4파일 전후 hash와 sourceDrift0을 저장했다. HEAD는 `d3c85faf182e93842217dd46eee3d3aabf4afc13`이다. 각12 checks는 exit0, theme/geometry/scale/DPI 일치, 실제 적용 배율, 종료 후 저장 배율, GUID, maxDimension2, Tiny4 PNG hash, 원래 저장 씬의 프로젝트 경로 치환 후 정확한 내용, Finalize, WM_CLOSE 수신, DX12 pending0/0/0, runtime hash 일치다. UI 조작 관찰은 기계 판정과 별도 필드다.

두 실행 모두 OS DPI1.5였다. UI 배율은0.8/1.5, renderedFontSize는19/36으로 관측됐으며 glyph 누락0, geometryMatches/clean=true였다. 실행기는 격리 프로젝트에만 선택한 `imguiScale`을 기록하고 두 `editor.theme` 스냅샷을 추가했다. 제품 코드 변경이나 추가 빌드는 없다.

Release의 최초240초 프로세스 관찰은 수동 조작 중 제한을 넘겨 `exit.json`에 exited=false/exitCode=null과 관찰기 오류를 남겼다. 프로세스를 강제 종료하지 않고 새 대기 핸들을 확보한 뒤 실제 X로 닫았다. `exit-after-ui.json`은 exited=true/exitCode0이며 이전 제한 초과를 보존한다. 최초 receipt를 정상 종료로 해석하지 않는다.

## 수용 경계와 잔여

`ImGuiHost.cpp`의 DockingEnable은 활성화돼 있지만 ViewportsEnable은 창 생성 스레드와 메시지 펌프 소유권 문제로 보류돼 있다. 이번 검증은 메인 OS 창 내부의 floating Inspector다. 별도 OS 창, 다중 모니터, 실행 중 DPI 전환, 전체 Debug/Release×0.8/1.5 조합 및 GPU pixel 결과는 수용하지 않는다. Release1.5 재도킹과 좁은 dock에서의 읽기·편집도 후속으로 남긴다.

12-0-D의 후속 범위로 기록하며 추가 가중치를 얻지 않는다. PHASE12 진행률66.25%(표시66%), T1b 대기·T3 중단을 유지한다. 400ms 안 실제 닫기 클릭, 실패 유형별 반복 확장, 전체 소비자·포맷·Shipping GPU는 기존 잔여다.

</details>
