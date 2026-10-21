#requires -Version 7.0
[CmdletBinding()]
param(
    [ValidateSet('Debug','Release')][string]$Configuration='Debug',
    [string]$DevelopmentProject='',
    [string]$OutputRoot=''
)
Set-StrictMode -Version Latest
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
if(!$DevelopmentProject){$DevelopmentProject=Join-Path $repo 'Dynamic_CPP'}
$editor=Join-Path $repo "Bin/x64-$Configuration/Editor/CreatorEditor.exe"
if(!(Test-Path -LiteralPath $editor)){throw 'Build CreatorEditor before generating its preview package.'}
if(!$OutputRoot){$OutputRoot=Join-Path $repo ('Build/EditorPreviewPackage/'+[guid]::NewGuid().ToString('N'))}
$run=[IO.Path]::GetFullPath($OutputRoot)
New-Item -ItemType Directory -Path $run -ErrorAction Stop | Out-Null
$staging=Join-Path $run 'package'
$script=Join-Path $run 'commands.txt'
[IO.File]::WriteAllLines($script,@(('model.preview.bake "'+$staging+'"'),'quit'),[Text.UTF8Encoding]::new($false))
$start=[Diagnostics.ProcessStartInfo]::new($editor)
$start.UseShellExecute=$false
$start.CreateNoWindow=$true
$start.WindowStyle=[Diagnostics.ProcessWindowStyle]::Hidden
$start.WorkingDirectory=$repo
$start.RedirectStandardOutput=$true
$start.RedirectStandardError=$true
$start.Environment['LOCALAPPDATA']=Join-Path $run 'cache'
$start.Environment['CE_TRACE_SHADER_COMPILE']='1'
$start.Environment['CE_MATERIAL_CACHE_ROOT']=Join-Path $run 'cache/MaterialPrograms'
foreach($arg in @('--development-project',$DevelopmentProject,'--smoke-offscreen','--commandlet-script',$script,'--result-file',(Join-Path $run 'results.jsonl'))){$start.ArgumentList.Add($arg)}
$process=[Diagnostics.Process]::Start($start)
$stdout=$process.StandardOutput.ReadToEndAsync()
$stderr=$process.StandardError.ReadToEndAsync()
if(!$process.WaitForExit(180000)){$process.Kill();throw 'Preview package build timed out.'}
[IO.File]::WriteAllText((Join-Path $run 'stdout.txt'),$stdout.GetAwaiter().GetResult())
[IO.File]::WriteAllText((Join-Path $run 'stderr.txt'),$stderr.GetAwaiter().GetResult())
$results=@(Get-Content -LiteralPath (Join-Path $run 'results.jsonl') | ForEach-Object {$_ | ConvertFrom-Json})
if($process.ExitCode -ne 0 -or !$results.Count -or @($results | Where-Object status -ne succeeded).Count){throw "Preview package build failed; inspect $run"}
$products=@(Get-ChildItem -LiteralPath $staging -File -Filter '*.slang.scene-cache')
if($products.Count -ne 1){throw 'Expected exactly one verified preview product.'}
$destination=Join-Path ([IO.Path]::GetDirectoryName($editor)) 'EditorResources/Preview'
New-Item -ItemType Directory -Path $destination -Force | Out-Null
Copy-Item -LiteralPath $products[0].FullName -Destination (Join-Path $destination $products[0].Name) -Force
$manifest=[ordered]@{schemaVersion=1;backend='dxil';recipe='editor-preview-minimal-checker-v3';runtimeSha256=(Get-FileHash -LiteralPath (Join-Path ([IO.Path]::GetDirectoryName($editor)) 'CreatorEditor.runtime.dll')).Hash;file=$products[0].Name;sha256=(Get-FileHash -LiteralPath $products[0].FullName).Hash;bytes=$products[0].Length}
$manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $destination 'package.json') -Encoding utf8
$manifest | ConvertTo-Json | Set-Content -LiteralPath (Join-Path $run 'summary.json') -Encoding utf8
Write-Output "EDITOR_PREVIEW_PACKAGE_OK output=$destination evidence=$run"
