#Requires -Version 7.0
param([Parameter(Mandatory)][string]$Root, [string]$Profile="GeneralProfile")
$ErrorActionPreference='Stop'
$Root=[IO.Path]::GetFullPath($Root)
$artifactRoot=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../Artifacts'))+[IO.Path]::DirectorySeparatorChar
if(!$Root.StartsWith($artifactRoot,[StringComparison]::OrdinalIgnoreCase)) {throw 'ETW output must be inside repository Artifacts.'}
if((Get-Item -LiteralPath $Root).Attributes -band [IO.FileAttributes]::ReparsePoint) {throw 'ETW output cannot be a reparse point.'}

$owned=$false
$active=''
$last=''
function Write-Reply($value) {
    $temp=Join-Path $Root 'etw-reply.tmp'
    $value|ConvertTo-Json|Set-Content -LiteralPath $temp -Encoding utf8
    Move-Item -LiteralPath $temp -Destination (Join-Path $Root 'etw-reply.json') -Force
}
try {
    Write-Reply @{id='ready';administrator=([Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)}
    $deadline=[DateTime]::UtcNow.AddHours(2)
    while([DateTime]::UtcNow -lt $deadline) {
        $file=Join-Path $Root 'etw-request.json'
        if(!(Test-Path -LiteralPath $file)){Start-Sleep -Milliseconds 150;continue}
        try{$request=Get-Content -LiteralPath $file -Raw|ConvertFrom-Json}catch{Start-Sleep -Milliseconds 150;continue}
        if($request.id -eq $last){Start-Sleep -Milliseconds 150;continue}
        $last=$request.id
        try {
            if($request.action -eq 'exit') {if($owned){throw 'Cannot exit an active trace'};Write-Reply @{id=$last;exitCode=0};break}
            if($request.name -notmatch '^(before|after)-(same|distinct)-(a|b)-(on|off)$'){throw 'Invalid fixed measurement name'}
            $dir=Join-Path $Root $request.name
            if(!(Test-Path -LiteralPath $dir -PathType Container)){throw 'Measurement directory missing'}
            if($request.action -eq 'start') {
                if($owned){throw 'Owned trace already active'}
                $status=@(& wpr.exe -status 2>&1)
                if($LASTEXITCODE -ne 0 -or ($status -join "`n") -notmatch 'WPR is not recording'){throw 'Existing WPR recording preserved'}
                & wpr.exe -start $Profile -filemode *> (Join-Path $dir 'wpr-start.log')
                $code=$LASTEXITCODE
                if($code -eq 0){$owned=$true;$active=$dir}
            } elseif($request.action -eq 'stop') {
                if(!$owned -or $active -ne $dir){throw 'Trace ownership mismatch'}
                & wpr.exe -stop (Join-Path $dir 'kernel.etl') -skipPdbGen *> (Join-Path $dir 'wpr-stop.log')
                $code=$LASTEXITCODE
                if($code -eq 0){$owned=$false;$active=''}
            } else {throw 'Unsupported action'}
            Write-Reply @{id=$last;exitCode=$code;action=$request.action;name=$request.name}
        } catch {Write-Reply @{id=$last;exitCode=1;error=$_.Exception.Message}}
    }
} finally {if($owned){& wpr.exe -stop (Join-Path $active 'kernel.etl') -skipPdbGen *> (Join-Path $active 'wpr-finally.log')}}
