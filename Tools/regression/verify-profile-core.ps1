[CmdletBinding()]
param([ValidateSet('Debug','Release','All')][string]$Configuration = 'All')

# PHASE 14 P1+P2 — 새 프로파일러 코어의 계약 검사.
#
# 엔진을 띄우지 않는다. EngineDiagnostics 가 ProjectReference 0 의 독립
# 라이브러리이고 서비스가 인스턴스로 서므로 코어만 링크해 초 단위로 돈다.
# 옛 코어는 전역 싱글톤 + 함수 지역 static thread_local 이라 이런 검사가
# 불가능했고, 그래서 selftest 가 라이브 캡처의 프레임 경계를 직접 넘겨
# 교란해야 했다(그 교란 때문에 stats 를 selftest 직후에 재면 포화로 보인다).
#
# ★ 변이를 함께 돌린다. 초록인 검사는 그 자체로는 아무것도 증명하지 않는다 —
#   틀린 코어에서 붉어지는 것을 봐야 이빨이 있다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repo = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
$vcvars = 'C:/Program Files/Microsoft Visual Studio/18/Community/VC/Auxiliary/Build/vcvars64.bat'
$core = Join-Path $repo 'Engine/EngineDiagnostics'
$configs = if ($Configuration -eq 'All') { @('Debug','Release') } else { @($Configuration) }

# 변이: 코어의 계약을 하나씩 깨뜨려 각 검사가 실제로 그것을 잡는지 본다.
# 앵커가 소스에서 사라지면 '대상이 없다' 로 붉어진다 — 검사가 낡은 것을 그때 안다.
$mutations = @(
    @{
        Name   = 'close-open-scopes'
        File   = 'ProfileThreadStream.cpp'
        Old    = "`t`tseal_current();`r`n`t}`r`n`r`n`tvoid thread_stream::finish"
        New    = "`t`twhile (m_depth > 0) { end_scope(0); }`r`n`t`tseal_current();`r`n`t}`r`n`r`n`tvoid thread_stream::finish"
        Expect = 'cross-frame/'
        Why    = '프레임 경계에서 열린 스코프를 닫으면(옛 코어가 그랬다) 프레임을 넘는 구간을 잃는다'
    },
    @{
        Name   = 'silent-drop'
        File   = 'ProfileThreadStream.cpp'
        Old    = "`t`t`t++m_droppedEvents;`r`n`t`t`treturn;"
        New    = "`t`t`treturn;"
        Expect = 'overflow/'
        Why    = '잃은 이벤트를 세지 않으면 프레임이 정상인 척한다'
    },
    @{
        # 모든 마커가 같은 id 를 받게 한다. '이름마다 구분되는 id' 가 이 코어의
        # 계약이고, 그것이 깨지면 어느 구간이 무엇인지 알 수 없게 된다.
        #
        # ★ 처음에는 intern_marker 의 중복 제거(return found->second)를 지우는
        #   변이를 썼는데 통과했다. 같은 이름의 marker_slot 은 inline 변수라
        #   한 번만 초기화되므로 id 는 그래도 안정적이고, 중복 제거는 지금
        #   **이중 안전망**이지 계약을 지탱하는 코드가 아니었기 때문이다.
        #   (동적 이름이 들어오는 P5 에서 그 경로가 비로소 자극된다.)
        Name   = 'marker-merge'
        File   = 'ProfileMarker.cpp'
        Old    = "`t`t`tconst marker_id id = static_cast<marker_id>(reg.descs.size());"
        New    = "`t`t`tconst marker_id id = 1;"
        Expect = 'marker/'
        Why    = '모든 이름이 한 id 로 뭉치면 어느 구간이 무엇인지 알 수 없다'
    },

    # ── PHASE 14 P3 집계 ────────────────────────────────────────────────────
    @{
        # 부모를 찾지 않고 전부 루트로 접는다. 트리가 사라지면 자식의 시간이
        # 루트 합에 두 번 들어가 프레임 예산이 통째로 거짓이 된다.
        Name   = 'aggregate-flatten'
        File   = 'ProfileAggregate.cpp'
        Old    = "`t`t`tconst node_key key{ stack.empty() ? 0u : stack.back(),"
        New    = "`t`t`tconst node_key key{ 0u,"
        Expect = 'aggregate/'
        Why    = '깊이를 무시하고 접으면 자식이 루트로 올라와 트리가 사라진다'
    },
    @{
        # self 를 total 그대로 둔다. 표의 모든 줄이 자기 자식의 시간을 제 것으로
        # 주장하게 되고, self 로 병목을 찾는 일이 전부 틀어진다.
        Name   = 'aggregate-self-as-total'
        File   = 'ProfileAggregate.cpp'
        Old    = "`t`t`tnode.self_ticks = (node.total_ticks > childTicks[i])"
        New    = "`t`t`tnode.self_ticks = node.total_ticks; if (false) node.self_ticks = (node.total_ticks > childTicks[i])"
        Expect = 'aggregate/'
        Why    = 'self 에서 자식을 빼지 않으면 병목을 self 로 찾는 일이 전부 틀어진다'
    },
    @{
        # 정렬을 걷는다. 이벤트는 **끝난 순서**로 기록되므로 정렬하지 않으면
        # 자식이 부모보다 먼저 나와 트리가 뒤집힌다 — 이 코어에서 가장 틀리기
        # 쉬운 가정이 그것이라 변이로 못 박는다.
        Name   = 'aggregate-unsorted'
        File   = 'ProfileAggregate.cpp'
        Old    = "`t`tstd::sort(events.begin(), events.end(), precedes);"
        New    = "`t`tif (events.size() > 1000000) std::sort(events.begin(), events.end(), precedes);"
        Expect = 'aggregate/'
        Why    = '이벤트는 끝난 순서로 들어오므로 정렬 없이는 자식이 부모보다 먼저 나온다'
    }
)

$sources = @(
    (Join-Path $core 'ProfileMarker.cpp'),
    (Join-Path $core 'ProfileThreadStream.cpp'),
    (Join-Path $core 'ProfileCapture.cpp'),
    (Join-Path $core 'ProfileAggregate.cpp'),
    (Join-Path $core 'ProfileService.cpp'),
    (Join-Path $PSScriptRoot 'profile_core_probe.cpp')
)

function Invoke-Probe {
    param(
        [string]   $Config,
        [string]   $Name,
        [string[]] $SourceList,
        [string]   $OutDir
    )

    $exe = Join-Path $OutDir "$Name.exe"
    $flags = if ($Config -eq 'Debug') { '/MDd /Od /RTC1 /D_DEBUG' } else { '/MD /O2 /DNDEBUG' }
    $quoted = ($SourceList | ForEach-Object { '"' + $_ + '"' }) -join ' '

    # /WX — 새 코어는 경고 0 이 계약이다.
    $command = 'call "' + $vcvars + '" >nul && cl /nologo /EHsc /std:c++latest /utf-8 /W4 /WX ' +
        $flags + ' /I"' + $core + '" /Fo"' + $OutDir + '/" /Fd"' + (Join-Path $OutDir "$Name.pdb") +
        '" /Fe"' + $exe + '" ' + $quoted

    $log = & $env:ComSpec /d /s /c $command 2>&1
    if ($LASTEXITCODE -ne 0) {
        return @{ Compiled = $false; ExitCode = -1; StdOut = ''; StdErr = ($log -join "`n") }
    }

    $outFile = Join-Path $OutDir "$Name.out"
    $errFile = Join-Path $OutDir "$Name.err"
    $proc = Start-Process -FilePath $exe -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput $outFile -RedirectStandardError $errFile
    if (-not $proc.WaitForExit(60000)) {
        $proc.Kill()
        $proc.WaitForExit()
        throw "$Config $Name timed out"
    }
    $proc.WaitForExit()

    return @{
        Compiled = $true
        ExitCode = $proc.ExitCode
        StdOut   = (Get-Content -LiteralPath $outFile -Raw -ErrorAction SilentlyContinue)
        StdErr   = (Get-Content -LiteralPath $errFile -Raw -ErrorAction SilentlyContinue)
    }
}

$failures = New-Object System.Collections.Generic.List[string]

foreach ($config in $configs) {
    $out = Join-Path $repo "Build/Obj/ProfileCore/$config"
    New-Item -ItemType Directory -Force $out | Out-Null

    # --- 기준: 손대지 않은 코어 ---------------------------------------
    $baseline = Invoke-Probe -Config $config -Name 'profile-core' -SourceList $sources -OutDir $out
    if (-not $baseline.Compiled) {
        $failures.Add("$config baseline 컴파일 실패 (경고를 오류로 다룬다)`n$($baseline.StdErr)")
        continue
    }
    if ($baseline.ExitCode -ne 0) {
        $failures.Add("$config baseline 실패 (exit $($baseline.ExitCode))`n$($baseline.StdErr)")
        continue
    }
    if ($baseline.StdOut -notmatch 'PROFILE_CORE_OK=true') {
        $failures.Add("$config baseline 성공 마커가 없다")
        continue
    }
    $summary = ($baseline.StdOut -split "`n" | Where-Object { $_ -match 'checks' } | Select-Object -First 1)
    Write-Host ("[OK]   $config baseline — " + $summary.Trim())

    # --- 변이: 계약을 깨뜨리면 붉어져야 한다 ---------------------------
    foreach ($mutation in $mutations) {
        $original = Join-Path $core $mutation.File
        $text = [IO.File]::ReadAllText($original)
        if (-not $text.Contains($mutation.Old)) {
            $failures.Add("$config 변이 '$($mutation.Name)' 의 대상이 소스에 없다 — 검사가 낡았다")
            continue
        }

        $mutantFile = Join-Path $out ("Mutant-" + $mutation.Name + '-' + $mutation.File)
        [IO.File]::WriteAllText($mutantFile, $text.Replace($mutation.Old, $mutation.New))

        $mutantSources = $sources | ForEach-Object {
            if ($_ -eq $original) { $mutantFile } else { $_ }
        }

        $result = Invoke-Probe -Config $config -Name ("mutant-" + $mutation.Name) -SourceList $mutantSources -OutDir $out
        if (-not $result.Compiled) {
            $failures.Add("$config 변이 '$($mutation.Name)' 가 컴파일되지 않는다 — 변이가 대상을 잘못 짚었다`n$($result.StdErr)")
            continue
        }
        if ($result.ExitCode -eq 0) {
            $failures.Add("$config 변이 '$($mutation.Name)' 가 통과했다 — 검사에 이빨이 없다. $($mutation.Why)")
            continue
        }
        if ($result.StdErr -notmatch [regex]::Escape($mutation.Expect)) {
            $failures.Add("$config 변이 '$($mutation.Name)' 가 붉어졌지만 다른 곳에서다. 기대='$($mutation.Expect)'`n$($result.StdErr)")
            continue
        }
        Write-Host ("[OK]   $config 변이 '" + $mutation.Name + "' — " + $mutation.Expect + ' 가 잡았다')
    }
}

Write-Host ''
if ($failures.Count -gt 0) {
    Write-Host '-- 판정 -----------------------------'
    foreach ($item in $failures) { Write-Host "[FAIL] $item" -ForegroundColor Red }
    Write-Host ("프로파일러 코어 검사 실패 " + $failures.Count + '건')
    exit 1
}

Write-Host '-- 판정 -----------------------------'
Write-Host ("프로파일러 코어 통과 — 계약이 서고, 변이 {0} 이 각각 제 검사에서 붉어진다" -f $mutations.Count)

# 종료 코드를 명시한다. 판정이 종료 코드뿐인 집중 검사 방식에서는 성공
# 경로가 남의 $LASTEXITCODE 를 흘리면 게이트가 판정 능력을 잃는다
# (verify-editor-icon-resources.ps1 이 git grep 의 1 을 흘리고 있었다).
exit 0
