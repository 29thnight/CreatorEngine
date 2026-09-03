[CmdletBinding()]
param(
    [string]$OutDir = ''
)

# LC0 (PHASE 14.5) — 정적 기준선 inventory.
#
# 런타임 덤프(`commands.dump`)가 못 세는 것을 여기서 센다. 등록 표에는
# "이 handler가 raw line을 다시 파싱하는가", "어디서 exit code를 직접 쓰는가",
# "어떤 소비자가 로그 문안을 정규식으로 읽는가"가 남지 않기 때문이다.
#
# ★ **여기서 소스를 긁는 것과 소비자가 소스를 긁는 것은 다른 일이다.**
#   계획이 금지하는 것은 자동화 소비자가 명령 **목록**을 C++ 리터럴에서 뽑는
#   일이다(§2.4). 이 스크립트는 목록을 런타임에서 가져오고, 소스에서는 오직
#   "고쳐야 할 자리가 몇 곳인가"만 센다. 그 수는 LC1~LC9가 0으로 밀어내는
#   래칫의 눈금이고, 소스 말고는 셀 곳이 없다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
if ([string]::IsNullOrWhiteSpace($OutDir)) {
    $OutDir = Join-Path $repoRoot 'Artifacts\Tests\Editor\lc0'
}
New-Item -ItemType Directory -Force -Path $OutDir | Out-Null

$cliSource = Join-Path $repoRoot 'Editor\EngineEntry\ConsoleCommandSystem.cpp'
if (-not (Test-Path -LiteralPath $cliSource -PathType Leaf)) {
    "CLI 소스가 없다: $cliSource"
    exit 1
}
$cliLines = Get-Content -LiteralPath $cliSource -Encoding UTF8

$rows = New-Object System.Collections.Generic.List[object]
function Add-Row([string]$Metric, [int]$Count, [string]$Note) {
    $rows.Add([pscustomobject]@{ metric = $Metric; count = $Count; note = $Note })
}

# ── 1) CLI 소스의 규모 ──────────────────────────────────────────────────
Add-Row 'cli_source_lines' $cliLines.Count 'ConsoleCommandSystem.cpp 줄 수'
Add-Row 'registration_calls' (@($cliLines | Select-String -Pattern '(?<![A-Za-z_])reg\(' -AllMatches).Count) 'reg( 호출 수 — 런타임 그룹 수와 대조하라'
Add-Row 'printf_calls' (@($cliLines | Select-String -Pattern 'std::printf' -AllMatches).Count) '결과가 값이 아니라 문자열로만 존재하는 자리'

# ── 2) LC1이 0으로 밀어야 할 것 ─────────────────────────────────────────
$exitWrites = @($cliLines | Select-String -Pattern 'EngineBootstrap::SetExitCode\(')
Add-Row 'direct_exit_writes_editor' $exitWrites.Count 'LC1 완료 시 session adapter 한 곳만 남는다(계획 §14.1)'

$playerMain = Join-Path $repoRoot 'Player\PlayerMain.cpp'
if (Test-Path -LiteralPath $playerMain -PathType Leaf) {
    $playerExit = @(Get-Content -LiteralPath $playerMain -Encoding UTF8 |
                    Select-String -Pattern 'EngineBootstrap::SetExitCode\(')
    Add-Row 'direct_exit_writes_player' $playerExit.Count 'LC8이 §5.4 표로 이관한다'
}

# 사람이 읽는 판정 문안. 소비자가 이것을 정규식으로 읽고 있어서(§2.4),
# LC9 전에는 문안을 바꾸면 안 된다 — 이 수가 "건드리면 깨지는 표면"의 크기다.
$verdictPrints = @($cliLines | Select-String -Pattern 'std::printf\(.*(통과|실패|완료)')
Add-Row 'human_verdict_prints' $verdictPrints.Count 'LC9 전까지 문안 고정 대상'

# ── 3) LC2가 0으로 밀어야 할 것 ─────────────────────────────────────────
#
# handler가 tokenizer 결과를 버리고 원문을 다시 자르는 자리. `ctx.line`이나
# 지역 `line`에 substr/rfind를 거는 형태가 그것이다(§3.2).
$rawLineAccess = @($cliLines | Select-String -Pattern '(ctx\.line|(?<![A-Za-z_])line)\.(substr|rfind|find)\(')
Add-Row 'raw_line_reinterpretation' $rawLineAccess.Count 'LC2 완료 시 migrated handler에서 0(계획 §13 LC2)'

# ── 4) 소비자 ───────────────────────────────────────────────────────────
$consumerRoots = @('Tools', 'scripts') |
    ForEach-Object { Join-Path $repoRoot $_ } |
    Where-Object { Test-Path -LiteralPath $_ -PathType Container }

$consumerFiles = New-Object System.Collections.Generic.List[object]
foreach ($root in $consumerRoots) {
    foreach ($file in (Get-ChildItem -LiteralPath $root -Recurse -File)) {
        # 이진 파일과 산출물은 건너뛴다.
        if ($file.Extension -notin @('.ps1', '.psm1', '.txt', '.md', '.py', '.cmd', '.bat', '.yml', '.yaml', '.json')) { continue }

        # ★ LC0 자신의 계측 도구는 소비자가 아니다.
        #
        #   이 스크립트는 `ConsoleCommandSystem.cpp`라는 문자열을 담고 있어서
        #   자기 자신을 "소스를 긁는 소비자"로 셌다. 하네스가 자기를 세면 LC9의
        #   래칫이 영영 0에 못 닿는다 — 눈금이 자기 무게를 다는 저울이 된다.
        if ($file.Name -like 'lc0-*' -or $file.Name -like 'verify-cli-*') { continue }
        $text = Get-Content -LiteralPath $file.FullName -Raw -ErrorAction SilentlyContinue
        if ($null -eq $text) { continue }

        $invokesCli   = $text -match '--exec|--script|CreatorEditor\.exe|Academy_4Q\.exe'
        if (-not $invokesCli) { continue }

        # 소스 스크래핑: C++ 파일에서 명령 목록을 뽑는 소비자(§2.4). LC3의
        # discovery가 이들을 대체하고 LC9가 정적 게이트로 재도입을 막는다.
        $scrapesSource = $text -match 'ConsoleCommandSystem\.cpp'
        # 한국어 판정 문안 정규식: LC9가 JSON 결과로 옮긴다.
        $readsVerdict  = $text -match '통과\|실패|실패\|통과|\(통과\|실패\|완료\)'

        $consumerFiles.Add([pscustomobject]@{
            path          = ($file.FullName.Substring($repoRoot.Length + 1) -replace '\\', '/')
            scrapesSource = $scrapesSource
            readsVerdict  = $readsVerdict
        })
    }
}

Add-Row 'cli_consumer_files' $consumerFiles.Count '--exec/--script/에디터 exe 를 부르는 자동화 파일'
Add-Row 'consumers_scraping_source' (@($consumerFiles | Where-Object scrapesSource).Count) 'C++ 소스에서 명령 목록을 뽑는 소비자 — LC3 discovery 가 대체'
Add-Row 'consumers_reading_verdict' (@($consumerFiles | Where-Object readsVerdict).Count) '한국어 판정 문안을 정규식으로 읽는 소비자 — LC9 가 JSON 으로 이관'

# ── 산출 ────────────────────────────────────────────────────────────────
$summaryPath = Join-Path $OutDir 'lc0_static_inventory.tsv'
$lines = New-Object System.Collections.Generic.List[string]
$lines.Add('# lc0-static-inventory v1')
$lines.Add("# generated`t" + [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ'))
$lines.Add("# note`t런타임에서 셀 수 없는 것만 센다. 명령 목록은 commands.dump 가 정본이다.")
$lines.Add("metric`tcount`tnote")
foreach ($row in $rows) { $lines.Add(("{0}`t{1}`t{2}" -f $row.metric, $row.count, $row.note)) }

$lines.Add('')
$lines.Add("kind`tfile`tscrapes_source`treads_verdict")
foreach ($consumer in ($consumerFiles | Sort-Object path)) {
    $lines.Add(("consumer`t{0}`t{1}`t{2}" -f $consumer.path,
        $(if ($consumer.scrapesSource) { 'yes' } else { 'no' }),
        $(if ($consumer.readsVerdict) { 'yes' } else { 'no' })))
}

$lines.Add('')
$lines.Add("kind`tfile`tline`ttext")
foreach ($hit in $exitWrites) {
    $lines.Add(("exit-write`tEditor/EngineEntry/ConsoleCommandSystem.cpp`t{0}`t{1}" -f $hit.LineNumber, $hit.Line.Trim()))
}
foreach ($hit in $rawLineAccess) {
    $lines.Add(("raw-line`tEditor/EngineEntry/ConsoleCommandSystem.cpp`t{0}`t{1}" -f $hit.LineNumber, $hit.Line.Trim()))
}

($lines -join "`n") + "`n" | Set-Content -LiteralPath $summaryPath -Encoding UTF8 -NoNewline

$rows | Format-Table -AutoSize | Out-String | Write-Host
"정적 inventory: $summaryPath"
exit 0
