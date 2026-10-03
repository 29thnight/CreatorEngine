# MAT-7 ShaderMeta 생성 어댑터

2026-10-02. MAT-7의 첫 구현 단계이며 전체 공통 재질 통합의 완료 보고가 아니다.

## 구현 경계

`GenerateMaterialSlang` → 기존 host compile/reflection → `PublishMaterialShaderMeta` → 공통 `ShaderMetaLoader`/`ShaderMetaReflection` 검증 → immutable source pair 게시.

- 실제 schema 1 `.shadermeta`를 생성한다. LX metadata JSON의 확장자만 바꾸는 경로가 아니다.
- Float/Int/Bool/Vector/Normal/Color는 물리 선언 이름과 stable Blackboard parameter ID, 기본값, label, semantic, colorSpace, exposed를 보존한다.
- Texture2D의 asset GUID와 사용별 색공간을 보존한다. 같은 parameter를 두 색공간으로 쓰는 경우 각 reflected resource에 같은 parameter ID를 기록한다. Sampler 선언과 description/parameter ID도 공통 layout으로 확인한다.
- 기존 숫자 packing 정본 `MaterialPropertyPacker`와 실제 reflection을 사용한다. Color 값을 중복 선형화하지 않으며 generated Slang의 기존 변환 의미를 유지한다.
- 기존 code-authored ShaderMeta에는 새 optional fields/block이 없어도 동작한다. unknown field/type, 의미/타입 불일치, 잘못된 sampler, 유효하지 않은 source digest는 거부한다.
- 생성 identity는 Graph GUID, adapter/schema version, canonical metadata, source SHA-256, compiler/include 의존을 포함한 verified program semantic key에 의존한다.
- 후보에 source와 meta를 쓴 뒤 schema/reflection 검증을 통과한 디렉터리만 rename한다. 같은 generation의 두 파일이 바뀌었으면 덮어쓰지 않고 거부한다. 로더는 source SHA-256 불일치를 거부한다.
- Editor authoring 및 AssetCooker 자동 Scene compiler의 호출에 Graph GUID를 전달했다. authoring cache identity를 갱신해 이전 cache가 생성 단계를 건너뛰지 않게 한다.

## 검증

회귀 진입점: `Tools/regression/verify-material-shadermeta.ps1 -VerifyAssetCooker`.
이미 생성한 fixture로 실제 cook만 다시 검사할 때는 `-VerifyAssetCooker -CookFixtureRoot <adapter-run-directory>`를 사용한다.

| 게이트 | 결과 |
|---|---|
| 실제 Slang DXIL/SPIR-V compile·reflection, 8개 property와 독립 sampler | 통과 |
| common default packing ↔ 기존 reflected uniform packing | 바이트 단위 일치 |
| Color/Normal 의미·노출·색공간·Texture alias/source ID·문자열 이스케이프 | 통과 |
| 같은 입력의 pair 재사용, 잘못된 타입·offset·sampler·숫자 범위·texture GUID 거부 | 통과; 정상 쌍/결과 보존 |
| source SHA-256 불일치·unknown field·semantic/type 불일치 거부 | 통과 |
| 공통 CEDO metadata encode/decode | 통과; 최종 player 배포 게이트와 구분 |
| 새 Graph 생성·저장 → 실제 Scene compiler → ShaderMeta 생성 | 통과 |
| 기존 authored ShaderMeta 6개 자산 | 모두 파싱 통과 |
| 실제 Debug/Release AssetCooker, 각 구성 두 번 cook | 모두 통과; 생성 pair 1개, source digest 일치, 두 파일 해시 유지 |
| RenderEngine Debug 및 CreatorEditor Debug 전체 빌드 | 통과 |
| AssetCooker Debug/Release 최종 빌드 | 통과 |
| 대시보드 전체 parse·집계 | 통과; MAT-7 `progress`/미산정 유지 |

최종 standalone 결과는 `MAT7_SHADERMETA_ADAPTER_OK checks=56 backends=2 typedProperties=8 samplers=1 legacyFiles=6 commonPacking=bitExact deterministic=true failurePreserved=true sceneCompiler=true`다.
증거는 `Build/Obj/MaterialShaderMetaProbe/adapter.log`, `Run-3493150d6cf449fe9453c1d66557e2e2/`와 `creatoreditor-debug-final.log`, `assetcooker-debug-final.log`, `assetcooker-release-final.log`에 있다. `Cook-Debug-*/Accepted.log`·`Repeated.log`, `Cook-Release-*/Accepted.log`·`Repeated.log`는 각 두 번의 실제 cook을 기록한다.

실제 cooker 요약은 **`materialPrograms=1`, `shaderMetas=0`**다. 생성 쌍은 `Library/LXSceneCook`에 검증·게시됐지만 최종 CEMF/LXMC에 common ShaderMeta artifact를 함께 넣은 것은 아니다. 이 차이를 최종 쿠킹 완료로 세지 않는다.

CreatorEditor의 RenderTests unity object는 C1128 한도에 걸려 최종 빌드에 임시 `/bigobj`를 적용했다. 프로젝트 설정은 변경하지 않았다. Release의 기존 Utility_Framework PDB에는 LNK4020 경고가 남으며 심볼 완전성을 이 검증으로 주장하지 않는다. 오류 없이 최종 링크와 두 구성의 실제 cook은 완료했다.

검증은 CPU 생성/반사/packing 및 실제 cooker 실행 범위다. 새 에디터의 수동 조작, GPU 이미지 품질, 전체 모델 성능, 공통 소비 전환 이후 회귀는 아직 판정하지 않는다.

## 다음 통합 단계

**후속 2026-10-02:** 공통 값/바인딩 소비와 LXMC v3의 generated metadata/source 복구는 [MAT7CommonMaterialConsumption](MAT7CommonMaterialConsumption.md)에서 진행했다. 이 문서의 위 결과와 아래 LXMC 미포함 설명은 첫 구현 단계의 시점 기록이다. 공통 PSO/Forward+ 완료 증거로 확대하지 않는다.

1. Graph coverage와 host compile options/permutation/include 계약을 공통 생성 계약에 연결한다.
2. `Material`의 공통 ShaderMeta loading/override/binding/PSO 소비로 전환하고 마지막 정상 generation을 일괄 교체한다.
3. 최종 cooked generation에 Meta/Slang 및 bytecode/의존을 함께 담는다. 기존 LXMC envelope는 아직 공통 ShaderMeta 쌍을 저장하지 않는다. warm-cache/cooked Player 복구 검증은 이 단계에서 한다.
4. 공통 Forward+ light list·일반 alpha Blend 혼합 정렬/공유 depth/HDR 합성 및 제품 저장/재개방 회귀를 닫는다.

현재 생성 pass는 Scene host의 실제 stage를 서술한다. `LXSceneColor`는 공통 Forward+ 구현 완료를 뜻하지 않는다. CEDO metadata encode/decode 검증도 최종 AssetCooker/Player 통합 완료로 확대하지 않는다.

MAT-7은 `progress`, 공수는 미산정. 품질/성능 최종 판정은 MAT-9에 남으며 BRDF 1024 / environment 4096을 유지한다.
