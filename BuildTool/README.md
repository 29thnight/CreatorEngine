# CreatorBuildTool

배포본 생성, 게임 C# 컴파일, 콘텐츠 cook·PAK·Player 검증·게시를 담당하는 독립 실행 프로젝트다.
Editor와 CLI는 같은 `CreatorBuildTool.exe`를 호출한다. 빌드 구현은 이 디렉터리의 C# 코드에 있으며,
PowerShell을 내부에서 호출하거나 스크립트를 EXE에 내장하지 않는다.

## 빌드와 배포

엔진 유지보수자 환경에서 .NET 10 SDK로 빌드한다. 외부 NuGet 패키지는 사용하지 않는다.

```powershell
dotnet build BuildTool/CreatorBuildTool.csproj -c Debug
dotnet build BuildTool/CreatorBuildTool.csproj -c Release
```

솔루션의 Tools 그룹에도 프로젝트가 등록된다. 구성별 출력은 다음과 같다.

```text
Bin/x64-<Config>/Tools/CreatorBuildTool/
  CreatorBuildTool.exe
  CreatorBuildTool.dll
  CreatorBuildTool.deps.json
  CreatorBuildTool.runtimeconfig.json
```

EXE는 .NET apphost이며 실제 빌드 코드는 함께 있는 DLL에 들어 있다. 배포본에서는 상대 경로
`../../Runtime/DotNet`의 사설 .NET 10 런타임을 먼저 사용한다. 로컬 개발 환경에서는 시스템 런타임을
사용할 수 있다. 게임 제작자에게 Visual Studio·.NET SDK·PowerShell 설치를 요구하지 않는다.
파일 버전은 루트 `EngineVersion.json`에서 생성한 `EngineVersion.props`를 통해 읽으며 빌드 과정에서 원본 버전을 자동 변경하지 않는다.

## 명령

```powershell
$tool = '.\Bin\x64-Debug\Tools\CreatorBuildTool\CreatorBuildTool.exe'

# 엔진 유지보수자: 미리 빌드한 host·도구·종속성을 불변 배포본으로 묶는다.
& $tool publish-engine --repository . --config Debug
# --build는 명시적 native host 빌드, --shipping은 Shipping Player 선택이다.
# --no-pointer는 검증용 배포를 만들면서 작업 공간의 선택 포인터를 보존한다.

# 아래 두 값은 사용할 배포본과 프로젝트의 실제 절대 경로로 지정한다.
$engine = 'C:\Engines\<배포 디렉터리>'
$project = 'C:\Projects\MyGame'
$tool = "$engine\Bin\x64-Debug\Tools\CreatorBuildTool\CreatorBuildTool.exe"

& $tool verify-engine --engine-distribution $engine
& $tool select-engine --engine-distribution $engine --project $project
& $tool compile-game --engine-distribution $engine --project $project --config Debug --output "$project\Intermediate\Managed\Debug"
& $tool package-game --engine-distribution $engine --project $project --config Debug --startup-scene FT_Primitives.creator --render-backend dx12
& $tool open-project --engine-distribution $engine --development-project $project
```

- `publish-engine`: native build record·파일 버전·해시와 API를 확인하고 엔진, Roslyn, 참조 어셈블리,
  source generator, 사설 .NET 런타임, BuildTool, 라이선스를 배치한다. PowerShell 런타임·패키징 스크립트는 배포하지 않는다.
- `compile-game`: 정확한 엔진 pin을 확인하고 포함된 Roslyn으로 `Assets/Script/**/*.cs`를 컴파일한다.
  성공한 DLL의 엔진 ID·API·해시를 sidecar에 기록한다. C# 컴파일 실패는 기존 DLL을 교체하지 않는다.
  `--prebuilt-assembly`도 동일 엔진 ID와 해시가 필요하다. 별도 NuGet 복원은 지원하지 않는다.
- `package-game`: Project/Workspace/Tracked 입력, 모델 generation, cook 결과, CEDO 문서, PAK 재열거,
  격리된 Player smoke와 추출 파일 해시를 검증한다. 성공한 candidate만 불변 release로 옮긴 뒤 current pointer를 교체한다.
- `select-engine` / `open-project`: 기존 개발용 프로젝트 pin·`--development-project` adapter를 사용한다.
  정식 `.creatorproject` parser·Launcher·MSI 구현을 의미하지 않는다.

`package-game`의 기본 구성은 Debug, `publish-engine`과 `compile-game`의 기본 구성은 Release다.
명령 간 구성을 명시적으로 맞춘다. Shipping 배포를 패키징할 때는 `--shipping`도 지정한다.
`--input-mode Workspace|Tracked`는 엔진 소스 checkout의 `Dynamic_CPP`만 지원한다.
`--build-native`는 엔진 유지보수자용이며 일반 게임 패키징에서는 지정하지 않는다.

## 실패·취소·진단

- `--log-path <파일>`은 오류와 하위 도구 출력을 기록한다. `--json`은 schemaVersion 1의 JSON Lines를 출력한다.
  이벤트는 `log`, `stage`, `result`, `error`이며 `message`가 본문이다. JSON 출력에 일반 하위 도구 출력을 섞지 않는다.
- Ctrl+C는 작업을 취소하고 프로세스 트리를 종료한다. 각 하위 프로세스는 Windows Job에 속해
  빌드 도구 종료 시에도 후손 프로세스가 정리된다. 성공은 0, 실패는 1, 취소는 130을 반환한다.
- 파일 목록은 대소문자 중복, 경로 이탈, reparse point와 변조를 거부한다. 입력 원본과 출력 트리는 겹칠 수 없다.
- 프로젝트별 stage lock으로 동시 게시를 막는다. 실패한 candidate와 Player 로그는 진단용으로 남기고,
  기존 release와 current pointer는 보존한다. 새 release 이동 뒤 pointer 교체 실패 시 새 디렉터리만 남을 수 있다.
- `--skip-verify`는 검증되지 않은 candidate만 남기며 current pointer를 바꾸지 않는다.
- 에디터의 스크립트 컴파일은 기존 비동기 Job/로그 경로를 유지한다. 게임 빌드 버튼의 동기 대기 UI는
  기존 동작이며, 진행률 UI·취소 버튼 추가는 BuildPipelinePlan B2 운영성 후속 범위다.

## 호환 진입점

`Tools/build.ps1`과 `Tools/distribution/{publish-engine,compile-game,select-engine,open-project}.ps1`은
소스 checkout에서 기존 명령을 받아 EXE로 전달하는 얇은 adapter다. 해당 스크립트에 두 번째 빌드 구현을 두지 않는다.
native 엔진 개발용 MSBuild의 리소스 생성·배포 검증 스크립트와 `update-engine-version.ps1`은 별도 개발 도구로 유지한다.
완성된 게임은 Player와 native/managed 런타임을 사용하며 BuildTool도 필요하지 않다.

## 검증

```powershell
dotnet run --project BuildTool/Tests/CreatorBuildTool.Tests.csproj -c Debug
dotnet run --project BuildTool/Tests/CreatorBuildTool.Tests.csproj -c Debug -- --engine $engine
```

외부 테스트 패키지 없이 경로·배포 변조·pin·PAK 목록·Player 판정·원자 게시·프로세스 트리 종료를 검증한다.
`--engine`을 주면 실제 배포본의 Roslyn 컴파일, 실패 시 기존 DLL 보존, prebuilt DLL 검증도 실행한다.
실제 게임 패키지 완료 판정은 `package-game`의 Player 검증까지 통과한 결과로 별도 기록한다.
2026-09-13 결과와 모델 씬의 기존 실행 제한은 [검증 기록](../docs/analysis/CreatorBuildToolValidation.md)에 있다.

관련 정본: [BuildPipelinePlan](../docs/plans/BuildPipelinePlan.md),
[EngineDistributionAndLauncherPlan](../docs/plans/EngineDistributionAndLauncherPlan.md),
[EngineVersionPolicy](../docs/design/EngineVersionPolicy.md).

## Player Development Build

Development/Shipping is the existing `EngineShipping` axis, independent of compiler
`Debug`/`Release`. `publish-engine --config Release` publishes an optimized Development
Player; add `--shipping` to publish Shipping. `package-game` and `Tools/build.ps1`
select the same mode using `--shipping` / `-Shipping`. Packaging requires a matching
published distribution and project engine pin, and does not enable diagnostics by
changing a runtime settings file.

Editor Build Settings persists `build.development` (Development Build, default true).
Export checks that choice against the selected distribution and reports a mismatch;
select/publish the matching engine before exporting. `engine.runtime.json`, the
package manifest and current pointer record `shipping` and `developmentBuild` alongside
compiler configuration.

Development bundles the matching `Player.runtime.pdb` through the native runtime
manifest. Attach the existing native debugger to Player.exe; optimized Release code
retains its optimization, and engine-module symbol coverage follows each module's
existing compiler policy. Existing logs and profiler markers remain available.
This does not add a remote debugger protocol or a managed/C# debugger integration.

Local Development Player inputs use the same role-filtered registry as HTTP:

- `Player.exe --exec "player.status" --exec quit`
- `Player.exe --exec-args player.object "Big Boss" -- --exec quit`
- `Player.exe --script commands.txt` (UTF-8, one command per line, `#`/`//` comment lines)
- `Player.exe --commandlet player.status` (run one registered Player command and exit)
- `Player.exe --commandlet-script commands.txt --fail-fast --result-file results.jsonl`

Structured `--exec-args` and `--commandlet` arguments run until `--` or argv end.
`--exec`/`--script` remain live until an explicit `quit`; commandlets exit after the
batch. Command results use the existing schema-v1 JSONL envelope, with optional
`--result-format jsonl` and `--result-file`. Stdout also contains engine logs; use the
result file for a pure JSONL stream. Session exit codes aggregate failures
(0 success, 2 arguments, 3 precondition, 4 failed/cancelled/timeout, 5 internal error).
Local batches cannot be mixed with smoke or HTTP service execution. These are
Player runtime commands, not Editor authoring/test commandlets.

HTTP still requires explicit `--command-service`; Development does not open a
listener automatically. Existing loopback binding, token authentication and user-code
policy are unchanged. Shipping compile-excludes the registry, CLI execution, profiler
command handlers and HTTP implementation, and rejects local developer switches.

## Independent source AssetSets

`CreatorBuildTool build-asset-set --engine-distribution C:\Engine\Distribution --project C:\Game --asset-set C:\Game\Content\textures.assetset.yml --output C:\Game\Build\Textures-r1`

This command invokes the verified distribution's AssetCooker independently. It does not build Player, native code, shaders, or managed game scripts. Existing `package-game` retains its legacy CEMF v2 path; it does not silently combine v3 AssetSets with an old package. The distribution must contain the new AssetCooker command.

Supported kinds are Texture (canonical UUIDv4), and source-authored Model descriptors, Mesh, Skeleton and AnimationClip (canonical UUIDv8 from the schema-v2 model sidecar). Each declaration uses that independently addressable identity as `assetId`; a nested `subassetId` YAML field is not supported. Texture output is a source image (`.png`, `.jpg`, `.hdr`, `.dds`); the other kinds use independent bounded descriptor (`.cemd`), geometry (`.cege`), skeleton (`.cesl`) and clip (`.cean`) formats. Material/scene/audio kinds, external manifest edges, transcoding and mip-generation settings still fail explicitly. Runtime support is a separate per-kind contract; a successful cook alone does not establish full animated geometry/render integration.

Every invocation reads authoring source and canonical identity data, including cache reuse. Model import uses immutable captured root and external dependency bytes (including glTF buffers/images and FBX side inputs); each consumed file contributes to the source-import fingerprint. Source sidecars and the identity epoch are validated without issuing IDs or advancing canonical generation state. Old `generation.asset`, `model.cemc` and CEMF v2 packages are never the source of a v3 recook. A missing or changed stable identity requires the authoring workflow to reconcile it first.

Model/mesh/skeleton/clip selections share one source import. Only selected meshes are packed and receive meshlet/LOD generation; persisted `importSettings.buildMeshlets` and `importSettings.lodLevels` are read from the captured canonical sidecar. A static Mesh has no dependencies; a skinned Mesh has exactly one Hard Skeleton edge. Geometry contains only shareable packed layout/vertex/index/bounds/meshlet/LOD data plus its required bone count and full skin-binding digest. It contains no mesh/model/material IDs, names, source paths or authoring generation. The full binding digest includes ordered bone names/parents/root and all bind/root transforms. A clip still uses the distinct pose-independent ordered bone-layout digest. The build validates each Mesh and clip against the actual selected Skeleton artifact, including packed bone indices 0–254 (255 is unused). Model descriptors list skeleton, all clips and all meshes as Loadable references, and retain ordered small node/mesh/material summaries without geometry arrays. The definition must declare those exact typed edges, rather than importing old untyped dependencies. Selecting only a clip root includes that clip and its skeleton; selecting a Mesh root includes only that Mesh and its required Skeleton, if skinned. Unrelated sibling artifacts are not emitted. This source import can inspect the entire authoring model, but runtime readers consume independent selected artifacts, not the monolithic CEMC.

Descriptor wire schema is now 2 (representation remains 2). Schema-1 `.cemd` artifacts must be recooked; they are rejected rather than treated as empty geometry descriptors. Independent geometry is representation 1/schema 1. Material summaries are a bridge, not a Material producer or prepared runtime material. General source-free authored-model boot, material graphs and embedded model textures remain a separate integration requirement; an explicitly prepared material is needed for the initial geometry consumer.

A source definition is strict YAML. Unknown or duplicate fields fail. Every source declares its typed dependencies explicitly, even when empty. Roots and both Hard/Loadable edge kinds form the included closure; only hard ownership SCCs are rejected. A loadable-only cycle is allowed. These edges are authored v3 source declarations; legacy CEMF v2 dependency lists are never reinterpreted or converted.

```yaml
schemaVersion: 1
assetSetId: 11111111-1111-4111-8111-111111111111
revision: 1
inclusion: HardAndLoadable
target:
  platform: win-x64
  abi: creator-texture-v1
settings:
  textureEncoding: Source
roots:
  - assetId: 22222222-2222-4222-8222-222222222222
    kind: Texture
assets:
  - assetId: 22222222-2222-4222-8222-222222222222
    kind: Texture
    source: Textures/Root.png
    dependencies:
      - assetId: 33333333-3333-4333-8333-333333333333
        kind: Texture
        dependency: Hard
      - assetId: 44444444-4444-4444-8444-444444444444
        kind: Texture
        dependency: Loadable
  - assetId: 33333333-3333-4333-8333-333333333333
    kind: Texture
    source: Textures/Hard.png
    dependencies: []
  - assetId: 44444444-4444-4444-8444-444444444444
    kind: Texture
    source: Textures/Later.png
    dependencies: []
```

A selected clip definition can name the already-authored skeleton and clip from one source. Replace these sample UUIDv8 values with the canonical sidecar IDs:

```yaml
schemaVersion: 1
assetSetId: 11111111-1111-4111-8111-111111111111
revision: 1
inclusion: HardAndLoadable
target:
  platform: win-x64
  abi: creator-animation-v1
settings:
  textureEncoding: Source
roots:
  - assetId: 22222222-2222-8222-8222-222222222222
    kind: AnimationClip
assets:
  - assetId: 22222222-2222-8222-8222-222222222222
    kind: AnimationClip
    source: Models/Character.glb
    dependencies:
      - assetId: 33333333-3333-8333-8333-333333333333
        kind: Skeleton
        dependency: Hard
  - assetId: 33333333-3333-8333-8333-333333333333
    kind: Skeleton
    source: Models/Character.glb
    dependencies: []
```

A static selected-Mesh definition has no Skeleton edge. The IDs must be copied from that model's canonical sidecar (the example values are placeholders):

```yaml
schemaVersion: 1
assetSetId: 11111111-1111-4111-8111-111111111111
revision: 1
inclusion: HardAndLoadable
target:
  platform: win-x64
  abi: creator-geometry-v1
settings:
  textureEncoding: Source
roots:
  - assetId: 55555555-5555-8555-8555-555555555555
    kind: Mesh
assets:
  - assetId: 55555555-5555-8555-8555-555555555555
    kind: Mesh
    source: Models/Prop.glb
    dependencies: []
```

For a skinned Mesh, replace `dependencies: []` with exactly one `kind: Skeleton`, `dependency: Hard` entry and declare that Skeleton source with `dependencies: []`, as in the clip example. For a Model root, declare `dependency: Loadable` edges to every source Mesh, clip and the Skeleton if present; the producer checks the complete edge inventory and source-order summary metadata. Material identities stay in descriptor summaries and are not geometry ownership edges.

`target.abi` is an explicit compatibility token which the mount caller must agree with. The sample token is illustrative, not a claim that every existing Player accepts this ABI. Encoded texture representation is `TextureSourceImage = 1`, with schema `kTextureArtifactVersion = 1`. Source container extension and decoder signature must agree.

Outputs are new immutable directories containing `Derived/asset-set-manifest.cemf`, `Derived/AssetBlobs/<compatibility-sha256>/<content-sha256>.<extension>`, `build-keys.txt`, and `build-report.txt`. Native manifest serialization/readback and payload hash/size/format validation complete before the candidate is renamed to the requested output. BuildTool verifies the completion hashes and source-free file boundary. Existing output paths are rejected; no current-release pointer is changed. No Player/source files are copied into the result.

The default CAS is `<project>/Library/AssetSetArtifacts`; override with `--artifact-cache PATH`. Source root, output, cache, and the verified engine distribution must not overlap as enforced by the command. Same compatible encoded bytes use one blob record and file per output. Outputs copy verified CAS blobs so cache cleanup cannot remove a published output's backing. The cache never overwrites existing blobs or build-key records; corruption fails rather than quietly repairing an immutable address. Builds sharing the same cache acquire a Windows exclusive file handle on `.asset-set-build.guard`. The file may remain, but only the live handle owns exclusion; forced cancellation, timeout, or process death releases it automatically. A leftover prototype `.asset-set-build.lock` directory is ignored and is not automatically deleted. Normal failures clean only the current invocation's private work directories. Forced termination can leave uniquely named `.asset-set-work-<nonce>.incomplete` cache work directories or `.asset-set-<nonce>.candidate` output siblings. Later builds neither enumerate these as reusable results nor resume or delete them. Only exact final hash-addressed blob/build-key paths are eligible for verified reuse. The requested immutable output path is published by the final rename and is never automatically deleted, including when cancellation races with publication.

Build keys include source SHA256, sidecar SHA256, importer/build version pins, verified toolchain payload digest, build-tool implementation digest, normalized settings, container extension, representation/schema and target platform/ABI. Roots, revision, and dependency hashes do not perturb texture payload keys because source-image bytes contain no dependency hashes. Their typed declarations still update the new manifest. Texture passthrough rereads source and metadata on every invocation. Granular model builds separately record a source-import key and a selected typed artifact build key; unrelated sibling or sidecar-generation changes do not perturb unchanged selected mesh/clip artifact records. Incremental storage reuse avoids rewriting an existing compatible CAS payload; it does not imply that all source/import/validation work was skipped.

Source/static implementation only until explicitly validated on Windows: no build, executable test, or source-free runtime result is implied by the presence of this command.

### Attach prebuilt AssetSets to a game package

`package-game --asset-set-list <directories.txt> --asset-set-abi <host-token>` attaches
1–64 already-built immutable AssetSet outputs. Each nonempty UTF-8 list line is a
source output directory (relative entries resolve beside the list file). The ABI is
an explicit host choice; it is never inferred from an untrusted incoming manifest.

The packager verifies receipts and every CAS byte hash before and after copying.
Its native `--copy-asset-set` step holds a shared source-store lease for enrolled
sources throughout traversal and identity revalidation; unmanaged sources remain
noncollectible. Cancellation/process exit releases the acquired OS lease.
The output is always a new directory in the host-owned private package candidate,
never a mutation of a managed release. It records exact manifest identities under `Assets/AssetSets/<sha256>`, and emits a
bounded `Assets/Derived/asset-set-activation.ceas` policy. Native preflight validates
schema, ABI, duplicate definitions and the complete cross-set hard graph before the
existing final package transaction can publish. The Player mounts the whole group
atomically during DataSystem initialization; it reads policy/manifests, not root
payloads. A cyclic set-level ordering is legal if the actual hard asset graph is a DAG.

This is currently an overlay on the existing package closure: legacy scene/audio
content and CEMF v2 source-identity boot remain. It does not claim a v3-only package
or eliminate the legacy package cook. Runtime async startup separately prepares cold
mesh/image/material work before scene activation. Copied/extracted store roots are
unenrolled and noncollectible until freshly published under the storage lease protocol.

AssetSet source dependencies may also specify `scope: External`; omitted scope means
`Internal`. External targets are not included or imported by this build and need not
have a source record here. Exact type and Hard/Loadable declarations still match each
producer recipe. Internal missing targets/cycles remain errors; the final mounted
union validates external missing/type/hard-cycle and cross-artifact binding rules.

Lattice Material sources use `.asset`; MaterialProgram sources name both their current
`.lxmaterial` graph and an explicit `verifiedProgram` input. The producer checks source
and carried backend agreement; it does not silently use a previous package or compile
shaders during runtime. Model descriptor schema3 adds Loadable Material references;
selected model materials and embedded textures produce independent artifacts.
