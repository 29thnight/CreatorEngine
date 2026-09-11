# PHASE 21 W1 — ImGui 1.92 obsolete API 의 잔존을 소스에서 센다.
#
# ── 왜 매크로가 아니라 소스 대조인가 ──────────────────────────────────────
#
# 계획서 W1 의 판정은 `IMGUI_DISABLE_OBSOLETE_FUNCTIONS` 를 켠 채로 Editor 가
# **빌드·기동**되는 것이었다. 실측으로 그 판정은 이 저장소에서 성립하지 않는다.
#
#   그 매크로는 함수만 걷는 것이 아니라 **`ImGuiIO` 의 레이아웃을 바꾼다**
#   (`imgui.h` 의 `struct ImGuiIO` 안에 `#ifndef IMGUI_DISABLE_OBSOLETE_FUNCTIONS`
#   블록이 있고 `FontGlobalScale` 이 그 안에 있다). imgui 는 vcpkg 가 미리
#   빌드한 .lib 로 오므로 소비자 TU 에만 매크로를 켜면 `sizeof(ImGuiIO)` 가
#   갈리고, `ImGui::DebugCheckVersionAndDataLayout`(imgui.cpp:11499)의
#   "Mismatched struct layout!" 어서션에서 기동이 죽는다. 2026-09-11 에
#   Editor.vcxproj·HostImGuiPresentation.vcxproj 에 켜서 **빌드는 exit 0,
#   기동은 0x80000003** 로 실측했다. Release 라면 어서션이 사라져 레이아웃이
#   어긋난 채 돈다 — 더 나쁘다.
#
# 그래서 매크로는 제품 구성에 넣지 않는다. 대신 그 매크로가 막아 주었을 호출을
# 여기서 소스로 센다. 켠 채 한 번 컴파일해 받은 목록(22자리·API 여덟)이 이
# 게이트의 패턴 목록의 출처다 — 짐작으로 고른 것이 아니다.
#
# ── 이 게이트가 잡지 못하는 것 ────────────────────────────────────────────
#
# `AddRect`/`AddPolyline`/`PathStroke` 는 1.92.8 에서 `thickness` 와 `flags` 가
# 자리를 맞바꿨다. 인자 **순서**가 문제라 이름만으로는 갈리지 않으므로, 여기서는
# 인자를 최상위 쉼표로 갈라 "flags 로 보이는 인자가 마지막이 아닌" 경우만 잡는다.
# `flags` 자리에 `ImDrawFlags_*` 도 `true/false` 도 아닌 **맨 정수 리터럴**을 쓴
# 호출(예: `AddRect(a, b, c, 0, 15, 2.f)`)은 이 방법으로 갈리지 않는다. 실제로
# 그런 자리가 `drawing.cpp` 의 죽은 `#else` 가지에 남아 있다(imgui <= 1.81 용).
# 잡지 못한다는 것을 여기 적어 둔다 — 변이로 증명되지 않는 단정은 세우지 않는다.

[CmdletBinding()]
param()

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

$repoRoot = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..\..'))
$sourceRoots = @('Editor', 'Engine', 'Player') |
    ForEach-Object { Join-Path $repoRoot $_ } |
    Where-Object { Test-Path -LiteralPath $_ -PathType Container }
$sources = @($sourceRoots | ForEach-Object {
    Get-ChildItem -LiteralPath $_ -Recurse -File -Include *.cpp, *.cxx, *.h, *.hpp
})
if ($sources.Count -eq 0) {
    throw 'ImGui obsolete gate inspected zero C++ sources.'
}

function Remove-CppComments([string]$Text) {
    $withoutBlocks = [regex]::Replace($Text, '(?s)/\*.*?\*/', '')
    return [regex]::Replace($withoutBlocks, '(?m)//.*$', '')
}

# 여는 괄호 다음부터 짝이 맞는 닫는 괄호까지를 최상위 쉼표로 가른다.
# 문자열·문자 리터럴 안의 괄호와 쉼표는 세지 않는다.
function Split-CallArguments([string]$Code, [int]$OpenParenIndex) {
    $depth = 0
    $parts = [Collections.Generic.List[string]]::new()
    $current = [Text.StringBuilder]::new()
    for ($i = $OpenParenIndex; $i -lt $Code.Length; $i++) {
        $ch = $Code[$i]
        if (($ch -eq '"') -or ($ch -eq "'")) {
            $quote = $ch
            [void]$current.Append($ch)
            $i++
            while ($i -lt $Code.Length) {
                [void]$current.Append($Code[$i])
                if ($Code[$i] -eq [char]92) { $i += 2; continue }
                if ($Code[$i] -eq $quote) { break }
                $i++
            }
            continue
        }
        if (($ch -eq '(') -or ($ch -eq '[') -or ($ch -eq '{')) {
            $depth++
            if (1 -eq $depth) { continue }
        }
        elseif (($ch -eq ')') -or ($ch -eq ']') -or ($ch -eq '}')) {
            $depth--
            if (0 -eq $depth) {
                $tail = $current.ToString().Trim()
                if ($tail -or ($parts.Count -gt 0)) { $parts.Add($tail) }
                # 쉼표를 붙여 반환한다 — 안 붙이면 원소 하나짜리 리스트가
                # 스칼라로 풀려 부르는 쪽의 `.Count` 가 없는 속성이 된다.
                return ,$parts
            }
        }
        elseif ((1 -eq $depth) -and ($ch -eq ',')) {
            $parts.Add($current.ToString().Trim())
            [void]$current.Clear()
            continue
        }
        [void]$current.Append($ch)
    }
    return $null   # 괄호가 닫히지 않았다 — 판정하지 않는다.
}

function Get-LineNumber([string]$Text, [int]$Index) {
    return ([regex]::Matches($Text.Substring(0, $Index), "`n")).Count + 1
}

# ── 이름만으로 갈리는 obsolete ────────────────────────────────────────────
$nameRules = @(
    @{ Pattern = '\bFontGlobalScale\b'
       Name    = 'io.FontGlobalScale (1.92: style.FontScaleMain 을 쓴다)' },
    @{ Pattern = '\bFonts\s*->\s*Build\s*\('
       Name    = 'ImFontAtlas::Build (1.92 의 아틀라스는 동적이라 부를 필요가 없다)' },
    @{ Pattern = '\bGetTexDataAs(?:Alpha8|RGBA32)\s*\('
       Name    = 'ImFontAtlas::GetTexDataAs* (1.92: 백엔드가 텍스처를 받는다)' },
    @{ Pattern = '\b(?:Push|Pop)TextureID\s*\('
       Name    = 'Push/PopTextureID (1.92: Push/PopTexture 로 이름이 바뀌었다)' }
)

# ── 인자 개수·자리로 갈리는 obsolete ──────────────────────────────────────
#   PushFont(font)            → PushFont(font, size)   인자 하나면 obsolete
#   AddRect / AddPolyline / PathStroke → flags 가 마지막이어야 한다
$flagsLast = @{
    'AddRect'     = 6
    'AddPolyline' = 5
    'PathStroke'  = 3
}

$violations = [Collections.Generic.List[string]]::new()
$inspected = 0
foreach ($file in $sources) {
    $text = [Text.Encoding]::UTF8.GetString([IO.File]::ReadAllBytes($file.FullName))
    $code = Remove-CppComments $text
    $relative = $file.FullName.Substring($repoRoot.Length).TrimStart([char]92)

    foreach ($rule in $nameRules) {
        foreach ($hit in [regex]::Matches($code, $rule.Pattern)) {
            $inspected++
            $line = Get-LineNumber $code $hit.Index
            $violations.Add("$($rule.Name) — ${relative}:$line")
        }
    }

    foreach ($hit in [regex]::Matches($code, '\bPushFont\s*\(')) {
        $inspected++
        $callArgs = Split-CallArguments $code ($hit.Index + $hit.Length - 1)
        if (($null -ne $callArgs) -and (1 -eq $callArgs.Count)) {
            $line = Get-LineNumber $code $hit.Index
            $violations.Add(
                "PushFont 인자 하나 (1.92: PushFont(font, size) — 0.0f 가 '현재 크기 유지') — ${relative}:$line")
        }
    }

    foreach ($name in $flagsLast.Keys) {
        foreach ($hit in [regex]::Matches($code, "\b$name\s*\(")) {
            $inspected++
            $callArgs = Split-CallArguments $code ($hit.Index + $hit.Length - 1)
            if ($null -eq $callArgs) { continue }
            if ($callArgs.Count -ne $flagsLast[$name]) { continue }
            # `ImDrawFlags_None` 처럼 밑줄이 이어지면 단어 경계가 서지 않으므로
            # 뒤쪽 `\b` 를 붙이지 않는다.
            for ($a = 0; $a -lt ($callArgs.Count - 1); $a++) {
                if ($callArgs[$a] -match '\bImDrawFlags|^\s*(?:true|false)\s*$') {
                    $line = Get-LineNumber $code $hit.Index
                    $violations.Add(
                        "$name 의 flags 가 마지막이 아니다 (1.92.8 에서 thickness 와 자리를 맞바꿨다) — ${relative}:$line")
                    break
                }
            }
        }
    }
}

"sources=$($sources.Count) callSitesInspected=$inspected violations=$($violations.Count)"
if ($violations.Count -gt 0) {
    $violations | Sort-Object -Unique | ForEach-Object { "  $_" }
    exit 1
}

'전체 통과 — ImGui 1.92 obsolete 호출 잔존 0 (이름 넷 · 인자 자리 넷)'
exit 0
