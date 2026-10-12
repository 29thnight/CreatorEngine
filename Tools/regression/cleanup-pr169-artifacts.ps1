[CmdletBinding()]
param([switch]$Apply)
$ErrorActionPreference='Stop'
. (Join-Path $PSScriptRoot 'ArtifactRetention.ps1')
$root=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../Artifacts/PR169'))
# Representative originals bound by the published checkpoints. Keep all logs,
# summaries, manifests, ceprof files and failures' metadata across every run.
$names=@(
 'closure-native-Debug-motion-12ba6a1fcbf0429594736d1a6f0a22d4',
 'closure-native-Release-motion-a741cb59d3cc42e59db30bd6aa4ebf65',
 'closure-sdk-Debug-dlss-quality-8d552dba684a4a9d9a537b3a85acd88f',
 'closure-sdk-Release-dlss-quality-cc0ecc1df5b849e9893188b5a9d5f877',
 'tr0-golden-Debug-867088f8dbe54a9fa67da72c5e982a93',
 'tr0-golden-Release-e7310c29fe014cc08b51bff160ec814a',
 'creator-temporal-overlay-437f7b3e723e4fe38434d57cf6c22194',
 'creator-temporal-overlay-9d9f2df435334a2590f177fdaaef17b7',
 'creator-mixed-rendering-38594e9596b647f58b41cf22fff4eba6',
 'creator-mixed-rendering-52a9f3081111499692aba276cdf3a754',
 'sdk-quality-matrix/Debug-fsr-quality-1400x900-retry',
 'sdk-quality-matrix/Release-fsr-quality-1400x900',
 'sdk-quality-matrix/Debug-xess-quality-1400x900',
 'sdk-quality-matrix/Release-xess-quality-1400x900',
 # First failing quality run: retain its pixels as well as the successful retry.
 'sdk-quality-matrix/Debug-fsr-quality-1400x900'
)
$keep=@($names|ForEach-Object {Join-Path $root $_}|Where-Object {Test-Path -LiteralPath $_})
$plan=@{schema='artifact.cleanup-plan.v1';root=$root;keepRawDirectories=$keep;
 preserveExtensions=@('.ceprof','.json','.jsonl','.log','.txt','.png','.dmp');
 pruneExtension='.f32';scope='PR169 artifact pixels only; no source/project/binary deletion';
 note='Nonrepresentative pixel replay will no longer be available; existing verdicts are historical summaries.'}
$plan|ConvertTo-Json -Depth 8|Set-Content -LiteralPath (Join-Path $root 'artifact-cleanup-plan-20261011.json') -Encoding utf8
if($Apply){Remove-ArtifactPixels $root $keep}else{$plan|ConvertTo-Json -Depth 8}
