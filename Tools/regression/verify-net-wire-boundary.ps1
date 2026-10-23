[CmdletBinding()]
param([ValidateSet('Debug','Release','All')][string]$Configuration='All', [string]$BaseRef=$env:CREATOR_NET_CONTRACT_BASE)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$out=Join-Path $repo ('Build/Verification/NDA/Boundary-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force $out|Out-Null
Push-Location $repo
try {
    if(!$BaseRef){$BaseRef='HEAD'}
    & python "$PSScriptRoot/net_wire_boundary.py" --root $repo --base-ref $BaseRef --output "$out/boundary.json" *> "$out/boundary.log"
    if($LASTEXITCODE){Get-Content "$out/boundary.log";throw 'ND-a source/freeze gate failed'}
    & python "$PSScriptRoot/net_wire_boundary_tests.py" --root $repo --output "$out/canaries.json" *> "$out/canaries.log"
    if($LASTEXITCODE){Get-Content "$out/canaries.log";throw 'ND-a mutation gate failed'}
    Get-Content "$out/boundary.log","$out/canaries.log"
    $shell=(Get-Process -Id $PID).Path
    & $shell -NoProfile -File "$PSScriptRoot/verify-net-regression-wiring.ps1" -EvidenceRoot $out *> "$out/wiring.log"
    if($LASTEXITCODE){Get-Content "$out/wiring.log" -Tail 15;throw 'Mandatory regression wiring mutation failed'}
    Get-Content "$out/wiring.log"
    $vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio/Installer/vswhere.exe'
    $vs=& $vswhere -latest -products '*' -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath
    if(!$vs){throw 'MSVC C++ toolchain unavailable'}
    $headers=@('NetWireIds.h','INetworkTransport.h','NetProtocolHeader.h','INetChannelPolicy.h','INetTelemetrySink.h','INetworkEntityRegistry.h')
    $configs=if($Configuration -eq 'All'){@('Debug','Release')}else{@($Configuration)}
    foreach($config in $configs) {
        $dir=Join-Path $out $config;New-Item -ItemType Directory -Force $dir|Out-Null
        $flags=if($config -eq 'Debug'){'/Od /MDd /Zi'}else{'/O2 /MD /DNDEBUG'}
        $lines=@('@echo off',"call `"$vs\Common7\Tools\VsDevCmd.bat`" -arch=x64 -host_arch=x64",'if errorlevel 1 exit /b 1')
        foreach($header in $headers) {
            $source=Join-Path $dir ($header+'.cpp')
            $include=(Join-Path $repo "Engine/Network/Contracts/$header").Replace('\','/')
            "#include `"$include`"`nint main() { return 0; }" | Set-Content $source -Encoding utf8
            $lines+="cl /nologo /std:c++20 /permissive- /W4 /WX /utf-8 /EHsc $flags /c `"$source`" /Fo`"$dir\$header.obj`" /Fd`"$dir\headers.pdb`""
            $lines+='if errorlevel 1 exit /b 1'
        }
        $lines+="cl /nologo /std:c++20 /permissive- /W4 /WX /utf-8 /EHsc $flags `"$PSScriptRoot\NetContractProbe.cpp`" /Fo`"$dir\contracts.obj`" /Fd`"$dir\contracts.pdb`" /Fe`"$dir\contracts.exe`""
        $lines+='if errorlevel 1 exit /b 1'
        $lines+="`"$dir\contracts.exe`""
        $lines+='if errorlevel 1 exit /b 1'
        $batch=Join-Path $dir 'contracts.cmd';$lines|Set-Content $batch -Encoding ascii
        & $env:ComSpec /d /c $batch *> "$dir/native.log"
        if($LASTEXITCODE){Get-Content "$dir/native.log" -Tail 20;throw "ND-a native contract failed: $config"}
        Get-Content "$dir/native.log"|Where-Object {$_ -like 'NET_CONTRACTS_OK*'}
    }
    # Mutate only a disposable copy; prove golden bytes detect endian reversal.
    $mutant=Join-Path $out 'EndianMutant'
    New-Item -ItemType Directory -Force "$mutant/Engine/Network/Contracts","$mutant/Tools/regression"|Out-Null
    foreach($header in $headers){Copy-Item "$repo/Engine/Network/Contracts/$header" "$mutant/Engine/Network/Contracts"}
    Copy-Item "$PSScriptRoot/NetContractProbe.cpp" "$mutant/Tools/regression"
    $path=Join-Path $mutant 'Engine/Network/Contracts/NetProtocolHeader.h'
    $text=Get-Content $path -Raw
    if(!$text.Contains('value >> (i << 3)')){throw 'Endian mutation anchor missing'}
    $text.Replace('value >> (i << 3)','value >> ((bytes - i - 1) << 3)')|Set-Content $path -Encoding utf8
    $batch=Join-Path $mutant 'mutant.cmd'
    @('@echo off',"call `"$vs\Common7\Tools\VsDevCmd.bat`" -arch=x64 -host_arch=x64",'if errorlevel 1 exit /b 1',
        "cl /nologo /std:c++20 /EHsc /utf-8 `"$mutant\Tools\regression\NetContractProbe.cpp`" /Fo`"$mutant\mutant.obj`" /Fe`"$mutant\mutant.exe`"",
        'if errorlevel 1 exit /b 1',"`"$mutant\mutant.exe`" > `"$mutant\failure.log`" 2>&1",'if not errorlevel 1 exit /b 1',
        "findstr /c:`"NET_CONTRACTS_FAILED`" `"$mutant\failure.log`" >nul",'if errorlevel 1 exit /b 1','echo NET_PROTOCOL_ENDIAN_CANARY_OK')|Set-Content $batch -Encoding ascii
    & $env:ComSpec /d /c $batch *> "$mutant/build.log"
    if($LASTEXITCODE){Get-Content "$mutant/build.log" -Tail 15;throw 'Endian canary did not fail as intended'}
    $hashes=[ordered]@{}
    foreach($file in Get-ChildItem "$repo/Engine/Network/Contracts" -File){$hashes[$file.Name]=(Get-FileHash $file.FullName).Hash}
    @{result='NDA_BOUNDARY_ACCEPTANCE_OK';configuration=$configs;headers=6;standaloneHeaderCompiles=6*$configs.Count;endianCanary='failed as expected';sourceHashes=$hashes;scope='Six frozen headers, local fixture consumers and negative mutations; production networking transport/registry remain N2/N4'}|ConvertTo-Json -Depth 8|Set-Content "$out/result.json" -Encoding utf8
    "NDA_BOUNDARY_ACCEPTANCE_OK evidence=$out"
} finally {Pop-Location}
