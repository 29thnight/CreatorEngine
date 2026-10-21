# 자동 Inspector 저장 대기 중 디스크 충돌 검증

<!-- master-doc-review:start -->
## 2026-10-20 master 구현 대조

기준: `27de3f5a02aadde7456aab680db3184c36ee735e`. 대시보드는 변경하지 않는다. 아래 현행 대조와 소스 선언이 이전 시점의 구조·미구현 표기보다 우선한다.

[전체 대조·링크 정리 원장](MasterDocumentationAudit20261020.html)

### 현행 계약과 이 문서의 적용 범위

- DataSystem의 typed 요청은 resolver 세대와 취소/게시 상태를 보존한다. descriptor·payload·이미지 owner와 frame pin은 분리된 수명이다.
- ownership_cpp 기반 자원 소유와 논리적 unmount를 GC 객체 수거·GPU 완료와 혼동하지 않는다. DirectXTex cook와 GPU-ready 소비의 제품 범위는 텍스처/AssetDepot 계획에서 수용한다.

### 해당 구현에서 확인한 선언·소유 경계

- [Editor/EngineEntry/EditorAssetDatabase.cpp:56](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineEntry/EditorAssetDatabase.cpp#L56) — `own::shared_owner<const LX::Runtime::ShaderGeneration> CompileCodeReload(`
- [Editor/EngineEntry/EditorAssetDatabase.cpp:62](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineEntry/EditorAssetDatabase.cpp#L62) — `const auto document = Authoring::ParsedDocument::ParseFile(metadataPath.string(), error);`
- [Editor/EngineEntry/EditorAssetDatabase.cpp:64](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineEntry/EditorAssetDatabase.cpp#L64) — `if (!document || !ShaderMetaLoader::Parse(ReadModelRecoveryText(metadataPath), metadataPath,`
- [Editor/EngineGUIWindow/DrawYamlNodeEditor.cpp:19](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/DrawYamlNodeEditor.cpp#L19) — `if (!EditorAssetDatabase::IsTextureSource(source))`
- [Editor/EngineGUIWindow/DrawYamlNodeEditor.cpp:40](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/DrawYamlNodeEditor.cpp#L40) — `(void)cooked::ParseTextureImportSettings(document, draft.settings, draft.parseError);`
- [Editor/EngineGUIWindow/DrawYamlNodeEditor.cpp:45](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineGUIWindow/DrawYamlNodeEditor.cpp#L45) — `ImGui::TextWrapped("Invalid texture import settings: %s", draft.parseError.c_str());`
- [Tools/regression/texture_auto_ui_pending_probe.inl:15](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/texture_auto_ui_pending_probe.inl#L15) — `const auto source = file::path(ctx.parts[1]);`
- [Tools/regression/texture_auto_ui_pending_probe.inl:17](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/texture_auto_ui_pending_probe.inl#L17) — `if (source.filename() != "fixture.png" || !file::exists(project / ".texture-auto-ui-race"))`
- [Tools/regression/texture_auto_ui_pending_probe.inl:21](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/regression/texture_auto_ui_pending_probe.inl#L21) — `const auto sidecar = file::path(source.string() + ".meta");`
- [Engine/SceneRuntime/ImageComponent.h:14](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/ImageComponent.h#L14) — `class [[reflgen::reflect]] ImageComponent : public meta::identity<ImageComponent, UIComponent>`
- [Engine/SceneRuntime/ImageComponent.h:19](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/ImageComponent.h#L19) — `void gc_trace(gc::tracer& tracer) const override`
- [Engine/SceneRuntime/ImageComponent.h:21](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/ImageComponent.h#L21) — `UIComponent::gc_trace(tracer);`
- [Editor/EngineEntry/EditorAssetDatabase.h:107](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineEntry/EditorAssetDatabase.h#L107) — `own::shared_owner<const Material> publishedMaterial;`
- [Editor/EngineEntry/EditorAssetDatabase.h:112](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineEntry/EditorAssetDatabase.h#L112) — `bool SaveMaterialGraph(own::shared_owner<Material> preparedMaterial,`
- [Editor/EngineEntry/EditorAssetDatabase.h:113](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Editor/EngineEntry/EditorAssetDatabase.h#L113) — `const own::shared_owner<const Material>& expectedMaterial,`
- [Tools/build.ps1:2](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L2) — `[CmdletBinding()]`
- [Tools/build.ps1:4](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L4) — `[ValidateSet('Game')][string]$Target = 'Game',`
- [Tools/build.ps1:21](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Tools/build.ps1#L21) — `$repository = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))`
- [Engine/SceneRuntime/ImageComponent.cpp:19](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/ImageComponent.cpp#L19) — `ImageComponent::ImageComponent()`
- [Engine/SceneRuntime/ImageComponent.cpp:24](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/ImageComponent.cpp#L24) — `bool ImageComponent::ReplaceTextureOwner(const Texture* previous,`
- [Engine/SceneRuntime/ImageComponent.cpp:25](https://github.com/29thnight/CreatorEngine/blob/27de3f5a02aadde7456aab680db3184c36ee735e/Engine/SceneRuntime/ImageComponent.cpp#L25) — `const own::shared_owner<const Texture>& replacement)`

### 이전 근거의 유효 범위

기존 세부 계약은 위 현행 구현과 모순되지 않는 범위에서 유지한다. 과거 수치·실행 통과·해시·장치 조건은 당시 기록이다. 로컬 산출물이 사라진 항목은 링크 대신 경로와 미보존 상태를 표시하며, 현재 master의 재실행 통과로 승격하지 않는다.

<!-- master-doc-review:end -->

<details data-master-history><summary>세부 계약·원래 분석과 당시 측정 기록</summary>

2026-10-10, PHASE 12의 다음 잔여 범위인 저장 대기 중 외부 설정 변경을 검증했다.
검증 대상은 실제 Release Inspector 입력으로 생성한 400ms 저장 대기와 기존
자동 저장 서비스다. T0/T1a/T2 진행 중 및 earnedDays 0, T1b 대기, T3 중단은 유지한다.

## 확인한 문제와 수정

첫 Release 실행에서 실제 값 2 붙여넣기가 저장 대기열에 들어온 age 0ms에 외부
설정 1을 주입했다. 기존 저장 직전 디스크 비교는 정상적으로 외부 설정을 보존하고
대기 편집을 거부했다. 그러나 포커스를 해제해도 Inspector는 편집 값 2를 표시하며
충돌 설명을 표시하지 않았다. 정상 저장으로 오해할 수 있는 UI 결함이다.
수정 전 화면 (`../../Build/TAUI-Release-conflict-cde28d7e/conflict-before-ui-fix.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)과
로그 (`../../Build/TAUI-Release-conflict-cde28d7e/editor.out` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)에 재현 근거를 남겼다.
이 실행은 최종 수정 UI의 수용 증거로 사용하지 않는다.

EditorAssetDatabase는 자산 경로별 자동 저장 실패를 mutex로 보호하는 snapshot으로
보관한다. 새 편집을 대기열에 넣으면 이전 오류를 해제한다. Inspector는 오류와
`Reload Disk Settings`를 표시한다. 이 버튼은 유효한 디스크 설정을 draft에 읽고
오류를 해제하며 디스크 쓰기나 저장 대기 요청을 만들지 않는다. 읽기/파싱 실패 시
기존 draft를 유지하고 실패를 표시한다. 정상 Retry Import 성공도 이전 오류를 해제한다.
기존 400ms 대기와 저장 직전 디스크 비교를 변경하지 않았다.

관련 소스는 [자동 저장 서비스](../../Editor/EngineEntry/EditorAssetDatabase.cpp),
[Inspector 설정 UI](../../Editor/EngineGUIWindow/DrawYamlNodeEditor.cpp),
[격리 관찰 명령](../../Tools/regression/texture_auto_ui_pending_probe.inl)이다.
관찰 명령의 conflict/conflictshutdown 모드는 실제 UI 입력을 기다린 뒤 외부 설정만
주입한다. local UI 편집이나 대기 저장을 생성하지 않는다. 일반 conflict는 저장
대기가 제거된 뒤 750ms 동안 주입한 bytes가 그대로인지 검사한다. conflictshutdown은
주입 직후 정상 RequestQuit을 요청하여 Finalize의 대기 저장 경로를 검사한다.
`[AUTOSAVE_CONFLICT]` 로그는 비교 거부 시점의 실행 증거다.

## 최종 Release 실행 결과

HEAD `b2f918c196d02c648eef5ef064e11ae4f8d7846b`의 로컬 수정 소스로 빌드한
`Bin/x64-Release/Editor/CreatorEditor.exe`를 사용했다. 최종 runtime DLL SHA-256은
`43B76B0069FD9EF23B6FA5A523B1A5F5225B4837ED7B9E27D5DED474702888BF`다.
Editor 빌드 (`../../Build/phase12-conflict-ui-editor-final-build.log` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)와
host 빌드 (`../../Build/phase12-conflict-ui-host-build.log` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)는 성공했다.
Utility_Framework.pdb의 기존 LNK4020 경고는 남았으며 해당 디버그 형식 정보의
완전성 수용은 아니다. texture Editor source 정적 검사 5개를 통과했다.

| 검사 | 결과 | 근거 |
|---|---|---|
| 일반 저장 대기 중 외부 변경 | 실제 숫자 2 입력 후 age 8ms에 외부 값 1 주입; 저장 거부 및 외부 bytes 보존 | receipt (`../../Build/TAUI-Release-conflict-64d24b9d/commands.jsonl` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조), 로그 (`../../Build/TAUI-Release-conflict-64d24b9d/editor.out` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조) |
| 충돌 오류 표시 | Inspector에 저장 거부 설명과 Reload Disk Settings 표시 | 화면 (`../../Build/TAUI-Release-conflict-64d24b9d/conflict-visible.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조) |
| 실제 Reload Disk Settings 클릭 | Inspector 1 복원, 디스크 hash 불변, 오류 해제 | 화면 (`../../Build/TAUI-Release-conflict-64d24b9d/disk-reloaded.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조), hash receipt (`../../Build/TAUI-Release-conflict-64d24b9d/reload-receipt.json` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조) |
| 충돌 해소 후 실제 숫자 2 입력 | 추가 저장 버튼/씬 재로드 없이 최종 metadata 2, 오류 재발 없음 | 화면 (`../../Build/TAUI-Release-conflict-64d24b9d/edit-after-conflict.png` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조) |
| 정상 종료 시 외부 충돌 | age 8ms에 외부 값 1 주입 후 RequestQuit; Finalize age 32ms flush에서 저장 거부, 최종 metadata 1 | receipt (`../../Build/TAUI-Release-conflictshutdown-3daac159/commands.jsonl` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조), 로그 (`../../Build/TAUI-Release-conflictshutdown-3daac159/editor.out` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조) |

일반 fixture는 native 창 닫기로 종료했다. 종료 충돌 fixture는 제어된 RequestQuit을
사용했다. 둘 다 원본 PNG·GUID 및 저장 씬을 보존했고 Finalize 완료와 DX12
pending task/batch/retirement 0/0/0을 확인했다. 일반 실행은 미저장 엔티티를 유지했다.
Output Log의 오류 건수 1은 이전 충돌 기록이며 복구 후 새 충돌이 발생했다는 의미가 아니다.
`wait 36000` 명령은 화면 관찰을 위한 유지 요청이고 장시간 완료 증거가 아니다.

통합 결과와 소스 manifest (`../../Build/TextureAutomaticConflictUi/result.json` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조) 및
수집 스크립트 (`../../Build/collect-texture-conflict-ui.ps1` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)에 디스크·로그 단정을 기록했다.
Build 아래 fixture·화면·로그는 로컬 실행 증거다. 이번 검사로 일반 대기/종료 flush의
제어된 유효 외부 설정 충돌, 오류 표시와 디스크 재읽기/재편집을 수용한다.
실패한 디스크 재읽기와 정상 Retry Import의 오류 해제는 소스 구현 확인이며 이번
UI 실행에서 직접 클릭한 수용으로 확대하지 않는다.

동일 최종 Release runtime의 소비자 회귀도 282단정을 통과했다. 자동 4→2 연결,
미저장 씬·Undo 보존, cook/reload 실패 보존과 복구 및 프리팹 자동 Apply/Undo/Redo를
ImageComponent/SpriteRenderer에서 확인했다.
회귀 receipt (`../../Build/TRS-Release-dffb9166/results.jsonl` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)와
통합 결과 (`../../Build/TextureReimportScene/result.json` — 로컬 실행 산출물 미보존; 당시 기록으로만 참조)의 sourceDrift는 0이다.

## 수용 경계

각 모드의 단일 제어된 외부 쓰기 수용이며 모든 디스크 충돌 수용은 아니다.
최종 디스크 비교와 실제 쓰기 사이에 다른 쓰기가 끼어드는 경합, 파일 잠금/삭제/
손상된 외부 설정, 반복·장시간·다중 창·배율 및 Debug 실행은 별도 잔여다.
일반 conflict의 bytes 보존과 종료 시 최종 설정 보존은 서로 다른 단정이다.
화면은 Inspector/씬 관찰이며 독립 GPU pixel oracle이나 전체 소비자 계측을 대신하지 않는다.
미저장 엔티티는 열린 씬 유지 검사이고 저장 씬 파일은 변경하지 않는다.

</details>
