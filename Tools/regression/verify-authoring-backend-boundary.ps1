[CmdletBinding()]
param()

# SerializationPlan D3-b-2b-1b — backend 경계 래칫.
#
# 읽기 경로를 ryml로 옮기는 일은 한 번에 못 한다. 그래서 아직 backend 노드를
# 만지는 자리마다 **이름이 흉한 탈출구**를 두었다(`BackendNodeDuringTransition`,
# `ExtractTypeFromYAML`의 backend 오버로드). 개수가 곧 진행률이다.
#
# ★ 이 게이트가 막는 것은 **역행**이다. 새 코드가 어댑터 대신 backend 노드를
#   직접 잡으면 전환이 조용히 뒤로 간다 — 빌드는 통과하고 게이트도 통과하므로
#   아무도 모른다. 그래서 기준선보다 **늘면 실패**한다.
#
# ★ 줄어드는 것은 통과다. 다만 기준선을 갱신하라고 말한다 — 숫자가 낡으면
#   래칫이 풀린다.
#
# ★ 0이 되면 **이 게이트를 은퇴시키라**고 말한다. 탈출구가 0이면 전환이 끝난
#   것이고, 그때까지 살아 있는 검사는 남은 일을 세는 자다.

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))

# 2026-08-30 실측 기준선.
$baselineEscapes = 10
$baselineExtractCallers = 14

$searchRoots = @('Engine', 'Editor', 'Tools') |
    ForEach-Object { Join-Path $repoRoot $_ } |
    Where-Object { Test-Path -LiteralPath $_ -PathType Container }

if ($searchRoots.Count -eq 0) {
    '검색 루트가 없다'
    exit 1
}

$sources = @(
    $searchRoots | ForEach-Object {
        Get-ChildItem -LiteralPath $_ -Recurse -File -Include *.cpp, *.h
    }
)
if ($sources.Count -eq 0) {
    # 0개를 세고 "탈출구 0"을 통과로 읽지 않는다.
    '검사한 소스가 0개다 — 잴 것이 없다'
    exit 1
}

$escapes = 0
$extractCallers = 0
$escapeFiles = New-Object System.Collections.Generic.List[string]

foreach ($file in $sources) {
    $name = $file.Name
    # CP949 파일이 섞여 있으므로 바이트로 읽어 Latin1로 훑는다(내용을 고치지 않는다).
    $text = [Text.Encoding]::GetEncoding(28591).GetString([IO.File]::ReadAllBytes($file.FullName))

    if ($name -ne 'AuthoringReadNode.h') {
        $hits = [regex]::Matches($text, 'BackendNodeDuringTransition').Count
        if ($hits -gt 0) {
            $escapes += $hits
            $escapeFiles.Add(("  {0,3}  {1}" -f $hits, $file.FullName.Substring($repoRoot.Length).TrimStart([char]92)))
        }
    }
    if ($name -ne 'ReflectionYml.h') {
        $extractCallers += [regex]::Matches($text, 'ExtractTypeFromYAML\(').Count
    }
}

$failures = New-Object System.Collections.Generic.List[string]

if ($escapes -gt $baselineEscapes) {
    $failures.Add("backend 탈출구가 $escapes 곳 — 기준선 $baselineEscapes 보다 늘었다(전환이 역행했다)")
}
if ($extractCallers -gt $baselineExtractCallers) {
    $failures.Add("ExtractTypeFromYAML backend 호출부가 $extractCallers 곳 — 기준선 $baselineExtractCallers 보다 늘었다")
}

$notes = New-Object System.Collections.Generic.List[string]
if ($escapes -lt $baselineEscapes -or $extractCallers -lt $baselineExtractCallers) {
    $notes.Add("기준선을 갱신하라: baselineEscapes=$escapes baselineExtractCallers=$extractCallers")
}
if ($escapes -eq 0 -and $extractCallers -eq 0) {
    $notes.Add('탈출구 0 — 전환이 끝났다. 이 게이트를 은퇴시켜라.')
}

"escapes=$escapes/$baselineEscapes extractCallers=$extractCallers/$baselineExtractCallers sources=$($sources.Count)"
if ($escapeFiles.Count -gt 0) {
    '탈출구 위치:'
    $escapeFiles | Sort-Object | ForEach-Object { $_ }
}
$notes | ForEach-Object { "참고: $_" }

if ($failures.Count -gt 0) {
    ''
    '실패:'
    $failures | ForEach-Object { "  - $_" }
    exit 1
}

'전체 통과 — backend 탈출구가 기준선을 넘지 않는다'
exit 0
