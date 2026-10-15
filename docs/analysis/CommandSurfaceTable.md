# 명령 표면을 찾는 정적 등록 지도

> 기준: master `493536e300e274cbf59989cdd62b20310c8ae64f`의 소스 정적 조사. 이 문서의 재작성에서 빌드·실행·성능 측정은 하지 않았다.

## 표를 사용하는 방법

실제 실행체의 `help`, `commands.list`, `commands.describe` 또는 인증된 `GET /commands`가 최종 discovery다. 아래는 현재 GetTable이 호출하는 domain registrar와 직접 regResult/regEscaping 등록문을 읽어 만든 소스 지도다. 전처리 조건과 실행 초기화 성공을 평가한 runtime snapshot이 아니므로 행 수를 현재 사용 가능 명령 수로 인용하지 않는다.

제품 명령과 commandlet은 seed의 commandlet flag로 분리된다. cost는 dispatch 정책용 선언이고 측정 latency가 아니다. 인자 문법·named input 지원·Undo·liveness는 descriptor와 handler를 함께 확인한다. 별칭은 같은 handler에 도달하는 이름이지 독립 command count가 아니다.

[CommandSurfaceDisposition.tsv](CommandSurfaceDisposition.tsv)는 2026-09 전후의 역사적 은퇴/이관 분류다. 현재 등록 목록으로 사용하지 않는다. 예를 들어 그 표의 assets.generation 제거 판단은 현재 source에 다시 등록된 commandlet과 일치하지 않는다. TSV는 당시 판단을 보존하며 본 표와 live discovery가 현재 확인 경로다.

## Editor 등록문

| canonical | alias | seed의 실행 표면 | cost | 등록 위치 |
|---|---|---|---|---|
| `ai.status` | 없음 | 제품 명령 | Immediate | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `animation.baseline.probe` | 없음 | commandlet | Long | [ScriptUiAnimatorCommands.cpp](../../Editor/EngineEntry/Commands/ScriptUiAnimatorCommands.cpp) |
| `animation.playback.probe` | 없음 | 제품 명령 | Long | [ScriptUiAnimatorCommands.cpp](../../Editor/EngineEntry/Commands/ScriptUiAnimatorCommands.cpp) |
| `animation.visual.probe` | 없음 | commandlet | Long | [ScriptUiAnimatorCommands.cpp](../../Editor/EngineEntry/Commands/ScriptUiAnimatorCommands.cpp) |
| `animator.param` | 없음 | 제품 명령 | Frames | [ScriptUiAnimatorCommands.cpp](../../Editor/EngineEntry/Commands/ScriptUiAnimatorCommands.cpp) |
| `animator.status` | 없음 | 제품 명령 | Frames | [AssetAuthoringCommands.cpp](../../Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp) |
| `assets.decodeab` | 없음 | commandlet | Long | [AssetAuthoringCommands.cpp](../../Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp) |
| `assets.decodeabhdr` | 없음 | commandlet | Long | [AssetAuthoringCommands.cpp](../../Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp) |
| `assets.generation` | 없음 | commandlet | Long | [AssetAuthoringCommands.cpp](../../Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp) |
| `assets.model.lods` | 없음 | 제품 명령 | seed 확인 필요 | [AssetAuthoringCommands.cpp](../../Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp) |
| `assets.model.meshlets` | 없음 | 제품 명령 | seed 확인 필요 | [AssetAuthoringCommands.cpp](../../Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp) |
| `assets.model.reimport` | 없음 | 제품 명령 | Long | [AssetAuthoringCommands.cpp](../../Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp) |
| `assets.modeldiag` | 없음 | 제품 명령 | Immediate | [AssetAuthoringCommands.cpp](../../Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp) |
| `assets.scenemodel` | 없음 | commandlet | Frames | [AssetAuthoringCommands.cpp](../../Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp) |
| `assets.texture` | 없음 | 제품 명령 | Immediate | [AssetAuthoringCommands.cpp](../../Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp) |
| `assets.texture.pendingprobe` | 없음 | commandlet | Long | [AssetAuthoringCommands.cpp](../../Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp) |
| `assets.texture.reimportprobe` | 없음 | commandlet | Long | [AssetAuthoringCommands.cpp](../../Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp) |
| `assets.texturebench` | 없음 | commandlet | Long | [AssetAuthoringCommands.cpp](../../Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp) |
| `assets.unload` | 없음 | 제품 명령 | Frames | [AssetAuthoringCommands.cpp](../../Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp) |
| `audio.authoringprobe` | 없음 | commandlet | Frames | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `audio.status` | 없음 | 제품 명령 | Immediate | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `blackboard.authoring.probe` | 없음 | commandlet | Frames | [AssetAuthoringCommands.cpp](../../Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp) |
| `bt.status` | `bt.reset` | 제품 명령 | Immediate | [AssetAuthoringCommands.cpp](../../Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp) |
| `camera.cut` | 없음 | 제품 명령 | Immediate | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `camera.editor` | 없음 | 제품 명령 | Frames | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `camera.lens.probe` | 없음 | 제품 명령 | Immediate | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `character.state` | `character.velocity`, `character.teleport`, `character.jump`, `character.force`, `character.cancel` | 제품 명령 | Immediate | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `cli.drain.budget` | 없음 | 제품 명령 | Immediate | [CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp) |
| `cli.echo.args` | 없음 | 제품 명령 | Immediate | [CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp) |
| `collisionmatrix.authoring.probe` | 없음 | commandlet | Frames | [AssetAuthoringCommands.cpp](../../Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp) |
| `commands.describe` | 없음 | 제품 명령 | Immediate | [CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp) |
| `commands.list` | 없음 | 제품 명령 | Immediate | [CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp) |
| `commands.selftest` | 없음 | 제품 명령 | Immediate | [CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp) |
| `component.add` | 없음 | commandlet | Frames | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `component.list` | 없음 | 제품 명령 | Immediate | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `component.remove` | 없음 | commandlet | Frames | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `crash.status` | 없음 | 제품 명령 | Immediate | [DiagnosticsCommands.cpp](../../Editor/EngineEntry/Commands/DiagnosticsCommands.cpp) |
| `crash.test` | 없음 | commandlet | Immediate | [DiagnosticsCommands.cpp](../../Editor/EngineEntry/Commands/DiagnosticsCommands.cpp) |
| `dump.list` | 없음 | 제품 명령 | Immediate | [DiagnosticsCommands.cpp](../../Editor/EngineEntry/Commands/DiagnosticsCommands.cpp) |
| `dump.show` | 없음 | 제품 명령 | Immediate | [DiagnosticsCommands.cpp](../../Editor/EngineEntry/Commands/DiagnosticsCommands.cpp) |
| `dx12.blended` | 없음 | commandlet | Long | [RenderTestCommands.cpp](../../Editor/EngineEntry/Commands/RenderTestCommands.cpp) |
| `dx12.decal` | 없음 | commandlet | Frames | [RenderTestCommands.cpp](../../Editor/EngineEntry/Commands/RenderTestCommands.cpp) |
| `dx12.descriptorheap` | 없음 | commandlet | Frames | [RenderTestCommands.cpp](../../Editor/EngineEntry/Commands/RenderTestCommands.cpp) |
| `dx12.fog` | 없음 | commandlet | Frames | [RenderTestCommands.cpp](../../Editor/EngineEntry/Commands/RenderTestCommands.cpp) |
| `dx12.forward` | 없음 | commandlet | Long | [RenderTestCommands.cpp](../../Editor/EngineEntry/Commands/RenderTestCommands.cpp) |
| `dx12.gizmoicon` | 없음 | commandlet | Frames | [RenderTestCommands.cpp](../../Editor/EngineEntry/Commands/RenderTestCommands.cpp) |
| `dx12.gizmoline` | 없음 | commandlet | Frames | [RenderTestCommands.cpp](../../Editor/EngineEntry/Commands/RenderTestCommands.cpp) |
| `dx12.gizmoscene` | 없음 | commandlet | Frames | [RenderTestCommands.cpp](../../Editor/EngineEntry/Commands/RenderTestCommands.cpp) |
| `dx12.grid` | 없음 | commandlet | Frames | [RenderTestCommands.cpp](../../Editor/EngineEntry/Commands/RenderTestCommands.cpp) |
| `dx12.ibl` | 없음 | commandlet | Frames | [RenderTestCommands.cpp](../../Editor/EngineEntry/Commands/RenderTestCommands.cpp) |
| `dx12.live` | 없음 | 제품 명령 | Immediate | [RenderDebugCommands.cpp](../../Editor/EngineEntry/Commands/RenderDebugCommands.cpp) |
| `dx12.meshletcontract` | 없음 | 제품 명령 | Immediate | [RenderTestCommands.cpp](../../Editor/EngineEntry/Commands/RenderTestCommands.cpp) |
| `dx12.parallel` | 없음 | commandlet | Frames | [RenderTestCommands.cpp](../../Editor/EngineEntry/Commands/RenderTestCommands.cpp) |
| `dx12.post` | 없음 | commandlet | Frames | [RenderTestCommands.cpp](../../Editor/EngineEntry/Commands/RenderTestCommands.cpp) |
| `dx12.psocache` | 없음 | commandlet | Frames | [RenderTestCommands.cpp](../../Editor/EngineEntry/Commands/RenderTestCommands.cpp) |
| `dx12.rendergraph` | 없음 | commandlet | Frames | [RenderTestCommands.cpp](../../Editor/EngineEntry/Commands/RenderTestCommands.cpp) |
| `dx12.resize` | 없음 | commandlet | Frames | [RenderTestCommands.cpp](../../Editor/EngineEntry/Commands/RenderTestCommands.cpp) |
| `dx12.selftest` | 없음 | commandlet | Frames | [RenderTestCommands.cpp](../../Editor/EngineEntry/Commands/RenderTestCommands.cpp) |
| `dx12.skinning` | 없음 | commandlet | Frames | [RenderTestCommands.cpp](../../Editor/EngineEntry/Commands/RenderTestCommands.cpp) |
| `dx12.skybox` | 없음 | commandlet | Frames | [RenderTestCommands.cpp](../../Editor/EngineEntry/Commands/RenderTestCommands.cpp) |
| `dx12.ssao` | 없음 | commandlet | Frames | [RenderTestCommands.cpp](../../Editor/EngineEntry/Commands/RenderTestCommands.cpp) |
| `dx12.ssgi` | 없음 | commandlet | Frames | [RenderTestCommands.cpp](../../Editor/EngineEntry/Commands/RenderTestCommands.cpp) |
| `dx12.ssr` | 없음 | commandlet | Frames | [RenderTestCommands.cpp](../../Editor/EngineEntry/Commands/RenderTestCommands.cpp) |
| `dx12.sss` | 없음 | commandlet | Frames | [RenderTestCommands.cpp](../../Editor/EngineEntry/Commands/RenderTestCommands.cpp) |
| `dx12.temporalhistory` | 없음 | 제품 명령 | seed 확인 필요 | [RenderTestCommands.cpp](../../Editor/EngineEntry/Commands/RenderTestCommands.cpp) |
| `dx12.ui` | 없음 | commandlet | Frames | [RenderTestCommands.cpp](../../Editor/EngineEntry/Commands/RenderTestCommands.cpp) |
| `dx12.validation` | 없음 | 제품 명령 | Immediate | [RenderDebugCommands.cpp](../../Editor/EngineEntry/Commands/RenderDebugCommands.cpp) |
| `dx12.wireframe` | 없음 | commandlet | Frames | [RenderTestCommands.cpp](../../Editor/EngineEntry/Commands/RenderTestCommands.cpp) |
| `editor.browser` | 없음 | 제품 명령 | Immediate | [CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp) |
| `editor.clipping` | 없음 | 제품 명령 | Immediate | [CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp) |
| `editor.dock` | 없음 | 제품 명령 | Immediate | [CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp) |
| `editor.inspector` | 없음 | 제품 명령 | Immediate | [CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp) |
| `editor.layout` | 없음 | 제품 명령 | Immediate | [CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp) |
| `editor.menu` | 없음 | 제품 명령 | Immediate | [CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp) |
| `editor.nav` | 없음 | 제품 명령 | Immediate | [CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp) |
| `editor.panelcost` | 없음 | 제품 명령 | Immediate | [CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp) |
| `editor.renderscale` | 없음 | 제품 명령 | Immediate | [CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp) |
| `editor.sceneview` | 없음 | 제품 명령 | Immediate | [CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp) |
| `editor.selftest` | 없음 | 제품 명령 | Immediate | [CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp) |
| `editor.state` | 없음 | 제품 명령 | Immediate | [CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp) |
| `editor.theme` | 없음 | 제품 명령 | Immediate | [CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp) |
| `editor.thumbnail` | 없음 | 제품 명령 | Immediate | [CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp) |
| `editor.viewport` | 없음 | 제품 명령 | Immediate | [CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp) |
| `editor.window` | 없음 | 제품 명령 | Immediate | [CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp) |
| `editor.windows` | 없음 | 제품 명령 | Immediate | [CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp) |
| `editor.workspace` | 없음 | 제품 명령 | Immediate | [CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp) |
| `entity.layer` | 없음 | commandlet | Immediate | [AssetAuthoringCommands.cpp](../../Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp) |
| `experiment.cooked` | 없음 | commandlet | Long | [AssetAuthoringCommands.cpp](../../Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp) |
| `foliage.authoring.probe` | 없음 | commandlet | Frames | [AssetAuthoringCommands.cpp](../../Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp) |
| `game.pak` | 없음 | 제품 명령 | Long | [CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp) |
| `gc.collect` | 없음 | 제품 명령 | Frames | [DiagnosticsCommands.cpp](../../Editor/EngineEntry/Commands/DiagnosticsCommands.cpp) |
| `gc.stats` | `gc.delta` | 제품 명령 | Immediate | [DiagnosticsCommands.cpp](../../Editor/EngineEntry/Commands/DiagnosticsCommands.cpp) |
| `geometry.create` | 없음 | 제품 명령 | Long | [AssetAuthoringCommands.cpp](../../Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp) |
| `geometry.update` | 없음 | 제품 명령 | Long | [AssetAuthoringCommands.cpp](../../Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp) |
| `gpu.baseline` | 없음 | 제품 명령 | Frames | [DiagnosticsCommands.cpp](../../Editor/EngineEntry/Commands/DiagnosticsCommands.cpp) |
| `gpu.census` | `gpu.delta` | 제품 명령 | Frames | [DiagnosticsCommands.cpp](../../Editor/EngineEntry/Commands/DiagnosticsCommands.cpp) |
| `help` | 없음 | 제품 명령 | Immediate | [CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp) |
| `inputmap.authoring.probe` | 없음 | commandlet | Frames | [AssetAuthoringCommands.cpp](../../Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp) |
| `inputmap.corpus.probe` | 없음 | commandlet | Long | [AssetAuthoringCommands.cpp](../../Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp) |
| `layer.add` | 없음 | 제품 명령 | Immediate | [AssetAuthoringCommands.cpp](../../Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp) |
| `layer.collision` | 없음 | 제품 명령 | Immediate | [AssetAuthoringCommands.cpp](../../Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp) |
| `layer.list` | 없음 | 제품 명령 | Immediate | [AssetAuthoringCommands.cpp](../../Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp) |
| `layer.rename` | 없음 | commandlet | Immediate | [AssetAuthoringCommands.cpp](../../Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp) |
| `lifecycle.dump` | 없음 | 제품 명령 | Immediate | [CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp) |
| `lifecycle.registry` | 없음 | 제품 명령 | Frames | [CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp) |
| `lifecycle.stress` | 없음 | commandlet | Frames | [CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp) |
| `lifecycle.trace` | 없음 | 제품 명령 | Frames | [CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp) |
| `light.proxy` | 없음 | 제품 명령 | Frames | [RenderDebugCommands.cpp](../../Editor/EngineEntry/Commands/RenderDebugCommands.cpp) |
| `log.flush` | 없음 | 제품 명령 | Immediate | [CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp) |
| `material.corpus.probe` | 없음 | commandlet | Long | [AssetAuthoringCommands.cpp](../../Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp) |
| `material.override` | 없음 | 제품 명령 | Frames | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `mem.delta` | 없음 | 제품 명령 | Immediate | [DiagnosticsCommands.cpp](../../Editor/EngineEntry/Commands/DiagnosticsCommands.cpp) |
| `mem.hook` | 없음 | 제품 명령 | Immediate | [DiagnosticsCommands.cpp](../../Editor/EngineEntry/Commands/DiagnosticsCommands.cpp) |
| `mem.reset` | 없음 | 제품 명령 | Immediate | [DiagnosticsCommands.cpp](../../Editor/EngineEntry/Commands/DiagnosticsCommands.cpp) |
| `mem.stats` | 없음 | 제품 명령 | Immediate | [DiagnosticsCommands.cpp](../../Editor/EngineEntry/Commands/DiagnosticsCommands.cpp) |
| `memory.capture` | 없음 | 제품 명령 | Frames | [DiagnosticsCommands.cpp](../../Editor/EngineEntry/Commands/DiagnosticsCommands.cpp) |
| `memory.snapshot` | 없음 | 제품 명령 | Immediate | [DiagnosticsCommands.cpp](../../Editor/EngineEntry/Commands/DiagnosticsCommands.cpp) |
| `model.async` | 없음 | commandlet | Long | [AssetAuthoringCommands.cpp](../../Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp) |
| `model.load` | 없음 | 제품 명령 | Long | [AssetAuthoringCommands.cpp](../../Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp) |
| `model.loadcached` | 없음 | 제품 명령 | Long | [AssetAuthoringCommands.cpp](../../Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp) |
| `model.place` | 없음 | 제품 명령 | Frames | [AssetAuthoringCommands.cpp](../../Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp) |
| `object.create` | 없음 | commandlet | Frames | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `object.delete` | 없음 | 제품 명령 | Frames | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `object.describe` | 없음 | 제품 명령 | Immediate | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `object.duplicate` | 없음 | commandlet | Frames | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `object.enable` | 없음 | 제품 명령 | Immediate | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `object.icon` | 없음 | commandlet | Immediate | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `object.lock` | 없음 | commandlet | Immediate | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `object.order` | 없음 | 제품 명령 | seed 확인 필요 | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `object.parent` | 없음 | commandlet | Frames | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `object.properties` | 없음 | 제품 명령 | Immediate | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `object.property` | 없음 | 제품 명령 | Frames | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `object.rename` | 없음 | commandlet | Immediate | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `object.rootref` | 없음 | commandlet | Frames | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `object.transform` | 없음 | 제품 명령 | Frames | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `physics.shapes` | 없음 | 제품 명령 | Frames | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `pipeline.nodes` | 없음 | 제품 명령 | Immediate | [RenderDebugCommands.cpp](../../Editor/EngineEntry/Commands/RenderDebugCommands.cpp) |
| `pix.capture` | 없음 | 제품 명령 | Frames | [DiagnosticsCommands.cpp](../../Editor/EngineEntry/Commands/DiagnosticsCommands.cpp) |
| `play` | `stop` | 제품 명령 | Frames | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `play.cursor` | 없음 | 제품 명령 | Immediate | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `play.foreground_override` | 없음 | 제품 명령 | Immediate | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `play.inject_snapshot_failure` | 없음 | 제품 명령 | Immediate | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `play.pause` | `play.resume` | 제품 명령 | Frames | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `play.possess` | `play.eject` | 제품 명령 | Frames | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `play.state` | 없음 | 제품 명령 | Immediate | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `prefab.corpus.digest` | 없음 | commandlet | Long | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `prefab.create` | 없음 | 제품 명령 | Frames | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `prefab.instantiate` | 없음 | commandlet | Frames | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `prefab.objectguid` | 없음 | 제품 명령 | Immediate | [ConsoleCommandSystem.cpp](../../Editor/EngineEntry/ConsoleCommandSystem.cpp) |
| `prefab.overrides` | 없음 | 제품 명령 | Immediate | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `prefab.status` | 없음 | 제품 명령 | Immediate | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `prefab.update` | 없음 | 제품 명령 | Frames | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `profile.counter-mask` | 없음 | 제품 명령 | Immediate | [DiagnosticsCommands.cpp](../../Editor/EngineEntry/Commands/DiagnosticsCommands.cpp) |
| `profile.deep.start` | 없음 | 제품 명령 | Immediate | [DiagnosticsCommands.cpp](../../Editor/EngineEntry/Commands/DiagnosticsCommands.cpp) |
| `profile.deep.status` | 없음 | 제품 명령 | Immediate | [DiagnosticsCommands.cpp](../../Editor/EngineEntry/Commands/DiagnosticsCommands.cpp) |
| `profile.deep.stop` | 없음 | 제품 명령 | Immediate | [DiagnosticsCommands.cpp](../../Editor/EngineEntry/Commands/DiagnosticsCommands.cpp) |
| `profile.frame` | 없음 | 제품 명령 | Immediate | [DiagnosticsCommands.cpp](../../Editor/EngineEntry/Commands/DiagnosticsCommands.cpp) |
| `profile.pause` | 없음 | 제품 명령 | Immediate | [DiagnosticsCommands.cpp](../../Editor/EngineEntry/Commands/DiagnosticsCommands.cpp) |
| `profile.record` | 없음 | 제품 명령 | Immediate | [DiagnosticsCommands.cpp](../../Editor/EngineEntry/Commands/DiagnosticsCommands.cpp) |
| `profile.save` | 없음 | 제품 명령 | Immediate | [DiagnosticsCommands.cpp](../../Editor/EngineEntry/Commands/DiagnosticsCommands.cpp) |
| `profile.stats` | 없음 | 제품 명령 | Immediate | [DiagnosticsCommands.cpp](../../Editor/EngineEntry/Commands/DiagnosticsCommands.cpp) |
| `quit` | `exit` | 제품 명령 | Immediate | [CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp) |
| `render.backend` | 없음 | 제품 명령 | Immediate | [RenderDebugCommands.cpp](../../Editor/EngineEntry/Commands/RenderDebugCommands.cpp) |
| `render.environment` | 없음 | 제품 명령 | Immediate | [RenderDebugCommands.cpp](../../Editor/EngineEntry/Commands/RenderDebugCommands.cpp) |
| `render.graph` | 없음 | 제품 명령 | Immediate | [RenderDebugCommands.cpp](../../Editor/EngineEntry/Commands/RenderDebugCommands.cpp) |
| `render.live.capture` | 없음 | 제품 명령 | Long | [RenderTestCommands.cpp](../../Editor/EngineEntry/Commands/RenderTestCommands.cpp) |
| `render.live.fence` | 없음 | 제품 명령 | Long | [RenderDebugCommands.cpp](../../Editor/EngineEntry/Commands/RenderDebugCommands.cpp) |
| `render.live.ready` | 없음 | 제품 명령 | seed 확인 필요 | [RenderDebugCommands.cpp](../../Editor/EngineEntry/Commands/RenderDebugCommands.cpp) |
| `render.live.wait` | 없음 | commandlet | Long | [RenderDebugCommands.cpp](../../Editor/EngineEntry/Commands/RenderDebugCommands.cpp) |
| `render.livecheck` | 없음 | commandlet | Immediate | [RenderTestCommands.cpp](../../Editor/EngineEntry/Commands/RenderTestCommands.cpp) |
| `render.matmode` | 없음 | commandlet | Immediate | [RenderDebugCommands.cpp](../../Editor/EngineEntry/Commands/RenderDebugCommands.cpp) |
| `render.pacing` | 없음 | 제품 명령 | Immediate | [RenderDebugCommands.cpp](../../Editor/EngineEntry/Commands/RenderDebugCommands.cpp) |
| `render.pbr.capture` | 없음 | commandlet | Long | [RenderTestCommands.cpp](../../Editor/EngineEntry/Commands/RenderTestCommands.cpp) |
| `render.pbr.compare` | 없음 | commandlet | Long | [RenderTestCommands.cpp](../../Editor/EngineEntry/Commands/RenderTestCommands.cpp) |
| `render.pbr.normalpair` | 없음 | commandlet | Immediate | [RenderTestCommands.cpp](../../Editor/EngineEntry/Commands/RenderTestCommands.cpp) |
| `render.pbr.seal` | 없음 | commandlet | Immediate | [RenderTestCommands.cpp](../../Editor/EngineEntry/Commands/RenderTestCommands.cpp) |
| `render.pbr.sealstatus` | 없음 | commandlet | Immediate | [RenderTestCommands.cpp](../../Editor/EngineEntry/Commands/RenderTestCommands.cpp) |
| `render.queue.mode` | 없음 | 제품 명령 | Long | [RenderDebugCommands.cpp](../../Editor/EngineEntry/Commands/RenderDebugCommands.cpp) |
| `render.rtinfo` | 없음 | 제품 명령 | Immediate | [RenderDebugCommands.cpp](../../Editor/EngineEntry/Commands/RenderDebugCommands.cpp) |
| `render.shadowinfo` | 없음 | 제품 명령 | Immediate | [RenderDebugCommands.cpp](../../Editor/EngineEntry/Commands/RenderDebugCommands.cpp) |
| `render.ssgi.intensity` | 없음 | 제품 명령 | Immediate | [RenderDebugCommands.cpp](../../Editor/EngineEntry/Commands/RenderDebugCommands.cpp) |
| `render.temporal.history.capture` | 없음 | 제품 명령 | seed 확인 필요 | [RenderTestCommands.cpp](../../Editor/EngineEntry/Commands/RenderTestCommands.cpp) |
| `render.temporal.motion.fixture` | 없음 | 제품 명령 | Long | [RenderTestCommands.cpp](../../Editor/EngineEntry/Commands/RenderTestCommands.cpp) |
| `scene.bonedump` | 없음 | 제품 명령 | Frames | [ConsoleCommandSystem.cpp](../../Editor/EngineEntry/ConsoleCommandSystem.cpp) |
| `scene.ddol` | 없음 | 제품 명령 | Frames | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `scene.dump` | 없음 | 제품 명령 | Immediate | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `scene.flag` | 없음 | 제품 명령 | Immediate | [ConsoleCommandSystem.cpp](../../Editor/EngineEntry/ConsoleCommandSystem.cpp) |
| `scene.gc.selftest` | 없음 | 제품 명령 | Long | [RenderTestCommands.cpp](../../Editor/EngineEntry/Commands/RenderTestCommands.cpp) |
| `scene.hierarchycheck` | 없음 | 제품 명령 | Frames | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `scene.load` | `scene.switch` | 제품 명령 | Immediate | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `scene.load.status` | 없음 | 제품 명령 | Immediate | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `scene.loadjobs` | 없음 | commandlet | Long | [AssetAuthoringCommands.cpp](../../Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp) |
| `scene.lxtransform.fixture` | 없음 | 제품 명령 | seed 확인 필요 | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `scene.navigate` | 없음 | 제품 명령 | Immediate | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `scene.new` | 없음 | 제품 명령 | Frames | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `scene.open_async` | 없음 | 제품 명령 | Immediate | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `scene.order` | 없음 | 제품 명령 | seed 확인 필요 | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `scene.populate` | 없음 | 제품 명령 | Frames | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `scene.save` | 없음 | 제품 명령 | Frames | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `scene.select` | 없음 | 제품 명령 | Immediate | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `scene.selection` | 없음 | 제품 명령 | Immediate | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `scene.sparseresolver` | 없음 | 제품 명령 | Frames | [ConsoleCommandSystem.cpp](../../Editor/EngineEntry/ConsoleCommandSystem.cpp) |
| `scene.transformdigest` | 없음 | 제품 명령 | Frames | [ConsoleCommandSystem.cpp](../../Editor/EngineEntry/ConsoleCommandSystem.cpp) |
| `scene.transformpull` | 없음 | 제품 명령 | Frames | [ConsoleCommandSystem.cpp](../../Editor/EngineEntry/ConsoleCommandSystem.cpp) |
| `scene.transformstats` | 없음 | 제품 명령 | Frames | [ConsoleCommandSystem.cpp](../../Editor/EngineEntry/ConsoleCommandSystem.cpp) |
| `scene.transformwritestats` | 없음 | 제품 명령 | Frames | [ConsoleCommandSystem.cpp](../../Editor/EngineEntry/ConsoleCommandSystem.cpp) |
| `script.add` | 없음 | 제품 명령 | Frames | [ScriptUiAnimatorCommands.cpp](../../Editor/EngineEntry/Commands/ScriptUiAnimatorCommands.cpp) |
| `script.create` | 없음 | 제품 명령 | Frames | [ScriptUiAnimatorCommands.cpp](../../Editor/EngineEntry/Commands/ScriptUiAnimatorCommands.cpp) |
| `script.creation` | 없음 | 제품 명령 | Immediate | [ScriptUiAnimatorCommands.cpp](../../Editor/EngineEntry/Commands/ScriptUiAnimatorCommands.cpp) |
| `script.fields` | 없음 | 제품 명령 | Frames | [ScriptUiAnimatorCommands.cpp](../../Editor/EngineEntry/Commands/ScriptUiAnimatorCommands.cpp) |
| `script.invoke` | 없음 | 제품 명령 | Long | [ScriptUiAnimatorCommands.cpp](../../Editor/EngineEntry/Commands/ScriptUiAnimatorCommands.cpp) |
| `script.reload` | 없음 | 제품 명령 | Frames | [ScriptUiAnimatorCommands.cpp](../../Editor/EngineEntry/Commands/ScriptUiAnimatorCommands.cpp) |
| `script.set` | 없음 | 제품 명령 | Frames | [ScriptUiAnimatorCommands.cpp](../../Editor/EngineEntry/Commands/ScriptUiAnimatorCommands.cpp) |
| `script.status` | 없음 | 제품 명령 | Immediate | [ScriptUiAnimatorCommands.cpp](../../Editor/EngineEntry/Commands/ScriptUiAnimatorCommands.cpp) |
| `shadermeta.probe` | 없음 | commandlet | Frames | [DiagnosticsCommands.cpp](../../Editor/EngineEntry/Commands/DiagnosticsCommands.cpp) |
| `tag.add` | 없음 | 제품 명령 | Immediate | [AssetAuthoringCommands.cpp](../../Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp) |
| `tag.has` | 없음 | 제품 명령 | Immediate | [AssetAuthoringCommands.cpp](../../Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp) |
| `tag.list` | 없음 | 제품 명령 | Immediate | [AssetAuthoringCommands.cpp](../../Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp) |
| `tag.remove` | 없음 | 제품 명령 | Immediate | [AssetAuthoringCommands.cpp](../../Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp) |
| `temporal.deepdvc` | 없음 | 제품 명령 | Immediate | [CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp) |
| `temporal.fallback` | 없음 | 제품 명령 | Immediate | [CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp) |
| `temporal.fault` | 없음 | 제품 명령 | Immediate | [CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp) |
| `temporal.fg` | 없음 | 제품 명령 | Immediate | [CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp) |
| `temporal.latency` | 없음 | 제품 명령 | Immediate | [CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp) |
| `temporal.metadata` | 없음 | 제품 명령 | Immediate | [CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp) |
| `temporal.motion` | 없음 | 제품 명령 | Immediate | [CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp) |
| `temporal.nis` | 없음 | 제품 명령 | Immediate | [CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp) |
| `temporal.reflex` | 없음 | 제품 명령 | Immediate | [CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp) |
| `temporal.reset` | 없음 | 제품 명령 | Immediate | [CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp) |
| `temporal.runtime` | 없음 | 제품 명령 | Immediate | [CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp) |
| `temporal.status` | 없음 | 제품 명령 | Immediate | [CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp) |
| `temporal.support` | 없음 | 제품 명령 | Immediate | [CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp) |
| `temporal.upscale` | 없음 | 제품 명령 | Immediate | [CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp) |
| `terrain.authoring.probe` | 없음 | commandlet | Frames | [AssetAuthoringCommands.cpp](../../Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp) |
| `ui.anchor` | 없음 | 제품 명령 | Frames | [ScriptUiAnimatorCommands.cpp](../../Editor/EngineEntry/Commands/ScriptUiAnimatorCommands.cpp) |
| `ui.drawitems` | 없음 | 제품 명령 | Immediate | [ScriptUiAnimatorCommands.cpp](../../Editor/EngineEntry/Commands/ScriptUiAnimatorCommands.cpp) |
| `ui.hitbox` | 없음 | 제품 명령 | Frames | [ScriptUiAnimatorCommands.cpp](../../Editor/EngineEntry/Commands/ScriptUiAnimatorCommands.cpp) |
| `ui.navprobe` | 없음 | commandlet | Frames | [ScriptUiAnimatorCommands.cpp](../../Editor/EngineEntry/Commands/ScriptUiAnimatorCommands.cpp) |
| `ui.pos` | 없음 | 제품 명령 | Frames | [ScriptUiAnimatorCommands.cpp](../../Editor/EngineEntry/Commands/ScriptUiAnimatorCommands.cpp) |
| `ui.rect` | 없음 | 제품 명령 | Frames | [ScriptUiAnimatorCommands.cpp](../../Editor/EngineEntry/Commands/ScriptUiAnimatorCommands.cpp) |
| `ui.screenpos` | 없음 | 제품 명령 | Frames | [ScriptUiAnimatorCommands.cpp](../../Editor/EngineEntry/Commands/ScriptUiAnimatorCommands.cpp) |
| `ui.size` | 없음 | 제품 명령 | Frames | [ScriptUiAnimatorCommands.cpp](../../Editor/EngineEntry/Commands/ScriptUiAnimatorCommands.cpp) |
| `ui.status` | 없음 | 제품 명령 | Immediate | [ScriptUiAnimatorCommands.cpp](../../Editor/EngineEntry/Commands/ScriptUiAnimatorCommands.cpp) |
| `undo` | `redo` | 제품 명령 | Frames | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `undo.state` | 없음 | 제품 명령 | Immediate | [SceneObjectCommands.cpp](../../Editor/EngineEntry/Commands/SceneObjectCommands.cpp) |
| `vk.grid` | 없음 | commandlet | Frames | [RenderTestCommands.cpp](../../Editor/EngineEntry/Commands/RenderTestCommands.cpp) |
| `vk.parallel` | 없음 | commandlet | Frames | [RenderTestCommands.cpp](../../Editor/EngineEntry/Commands/RenderTestCommands.cpp) |
| `vk.texturecodec` | 없음 | commandlet | Frames | [RenderTestCommands.cpp](../../Editor/EngineEntry/Commands/RenderTestCommands.cpp) |
| `wait` | 없음 | 제품 명령 | Immediate | [CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp) |
| `window.info` | 없음 | 제품 명령 | Immediate | [CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp) |
| `window.resize` | 없음 | 제품 명령 | Frames | [CoreCommands.cpp](../../Editor/EngineEntry/Commands/CoreCommands.cpp) |
| `worker.pool.probe` | 없음 | commandlet | Long | [AssetAuthoringCommands.cpp](../../Editor/EngineEntry/Commands/AssetAuthoringCommands.cpp) |

## Development Player whitelist

다음 이름은 Player의 kPlayerCommands에 선언되어 있다. EnsureRegistered는 seed 존재·Player role도 검사한다. non-Shipping 조건을 평가하고 해당 실행체 discovery로 재확인해야 한다.

`temporal.deepdvc`, `temporal.fallback`, `temporal.fault`, `temporal.fg`, `temporal.latency`, `temporal.metadata`, `temporal.motion`, `temporal.nis`, `temporal.reflex`, `temporal.reset`, `temporal.runtime`, `temporal.status`, `temporal.support`, `temporal.upscale`, `render.status`, `profile.record`, `profile.pause`, `profile.save`, `player.device-remove`, `help`, `quit`, `player.status`, `player.scene`, `player.objects`, `player.object`, `player.animation`, `player.move`

Shipping에는 동일 개발 표면을 약속하지 않는다. Player local batch와 service/smoke 조합에는 입력 충돌 검사가 있으며 `--commandlet list`도 Editor의 모든 검사를 Player로 복제하지 않는다.

## 특수 commandlet dispatch

EditorCommandlets::Run에는 `experiment.matmigrate`, `experiment.matresolve`, `experiment.matscript`의 synthetic/real 검사 연결이 별도로 있다. `list`는 이 명시적 항목과 commandlet registry를 합친다. 파일명이나 Run*SelfTest 존재 여부만으로 명령을 추가해서는 안 된다.

## 구현 근거

- [Editor/EngineEntry/ConsoleCommandSystem.cpp](../../Editor/EngineEntry/ConsoleCommandSystem.cpp)
- [Editor/EngineEntry/Commands/CommandRegistrar.h](../../Editor/EngineEntry/Commands/CommandRegistrar.h)
- [Engine/RuntimeHost/CommandCore/CommandDescriptorSeeds.cpp](../../Engine/RuntimeHost/CommandCore/CommandDescriptorSeeds.cpp)
- [Player/PlayerCommands.cpp](../../Player/PlayerCommands.cpp)
- [Editor/EngineEntry/Commandlets/EditorCommandlets.cpp](../../Editor/EngineEntry/Commandlets/EditorCommandlets.cpp)
