# 끌기 payload 규약 — 경로를 나르는 것만 경로로 읽는다 (PHASE 21 W2-B)
#
# 왜 필요한가
# ───────────
# 2026-09-18 15:38, 에디터가 창 탭을 끄는 도중 죽었다(CreatorEditor_20260918_153849.dmp).
# 덤프의 ImGui 컨텍스트가 원인을 그대로 들고 있었다:
#
#     DragDropPayload.DataType = "_IMWINDOW"   ← ImGui 가 도킹 창을 끌 때 세우는 내부 payload
#     DragDropPayload.DataSize = 8             ← 내용은 ImGuiWindow* 하나
#     payload bytes            = 60 9c 28 a5 fa 01 00 00
#
# 콘텐츠 브라우저의 스냅샷 코드가 그 8바이트를 **자산 경로**로 읽었다. 필터가
# 배제 목록이었기 때문이다 — `!IsDataType("SCENE_OBJECT")`. 모르는 것은 전부 통과한다.
# 통과한 뒤 `path_of` 의 방어도 전부 지나갔다: 크기 8 은 2 이상이고, 포인터의 상위
# 바이트가 0 이라 "끝이 NUL 인가" 도 참이었다. 남은 7바이트 `60 9c 28 a5 fa 01 00` 을
# `std::filesystem::path` 에 넣자 `0x9c` — 선두 없는 연속 바이트 — 에서 변환이 실패했고,
# path 생성자는 빈 경로 대신 **예외를 던졌다**. 그리는 스레드에는 그것을 받을 자리가
# 없어 terminate → abort.
#
# 세 겹이 동시에 틀려야 죽는 결함이라, 한 겹만 고치면 다음에 다른 모양으로 돌아온다.
# 그래서 세 겹을 각각 못으로 박는다.
#
# ── 판정 항목 ──
#
#   1  ★ 타입 무관 조회는 허용 목록으로만 — `GetDragDropPayload()` 는 타입을 모르고
#      가져온다. 그 자리를 거르는 것은 `asset_drag::carries_path` 여야 한다.
#      호출 지점 단위로 센다(면제 단위 = 강제 단위)
#   2  ★ 배제 목록 금지 — 타입 무관 조회 조건에 `!...IsDataType(...)` 이 있으면 실패.
#      그것이 9-18 에 죽은 바로 그 모양이다
#   3  `path_of` 는 path 를 만들기 **전에** UTF-8 유효성을 본다 — 순서까지 본다
#   4  `set_payload` 는 표에 없는 타입을 싣지 않고 이유를 남긴다 — 표가 조용히
#      어긋나지 않게 하는 장치
#   5  ★ 두 표가 어긋나지 않는다 — 브라우저가 payload 이름으로 쓰는
#      `FileTypeStringTable` 의 문자열 전부가 `kPathPayloadTypes` 안에 있다
#   6  번들 창은 싣는 객체의 크기로 싣는다 — `&entry, sizeof(entry)`. 예전에는
#      `sizeof(AssetEntry)`(다른 타입, 40바이트)로 실어 1028바이트 구조체를 잘라
#      보냈고, 받는 쪽(:266)은 `AssetEntryPayload*` 로 **버퍼 밖까지** 읽었다
param(
    [switch]$Verbose_
)

$ErrorActionPreference = "Stop"

$repoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..\..")).Path
$failures = New-Object System.Collections.Generic.List[string]
$checks = 0
function Assert([bool]$ok, [string]$what) {
    $script:checks++
    if (-not $ok) { $script:failures.Add($what) }
}

function Read-Source([string]$relative) {
    $full = Join-Path $repoRoot $relative
    if (-not (Test-Path -LiteralPath $full)) {
        $script:failures.Add("source is missing: $relative")
        return ""
    }
    return [IO.File]::ReadAllText($full)
}

$kDragPayloadCpp = 'Editor\EngineGUIWindow\EditorAssetDragPayload.cpp'
$kDragPayloadH   = 'Editor\EngineGUIWindow\EditorAssetDragPayload.h'
$kBrowserCpp     = 'Editor\EngineGUIWindow\ContentsBrowserWindow.cpp'
$kBundleCpp      = 'Editor\EngineGUIWindow\AssetBundleWindow.cpp'

$dragPayload = Read-Source $kDragPayloadCpp
$dragHeader  = Read-Source $kDragPayloadH
$browser     = Read-Source $kBrowserCpp
$bundle      = Read-Source $kBundleCpp

# ─────────────────────────────────────────────────────────────────────────
# 1·2 — 타입 무관 조회는 허용 목록으로만
# ─────────────────────────────────────────────────────────────────────────
#
# `GetDragDropPayload()` 를 부르는 자리를 소스 트리 전체에서 찾는다. 한 자리라도
# `carries_path` 없이 통과하면 9-18 이 돌아온다. ThirdParty 와 vcpkg 는 남의 코드다.
$sourceFiles = Get-ChildItem -Path (Join-Path $repoRoot 'Editor'), (Join-Path $repoRoot 'Engine') `
    -Recurse -Include *.cpp, *.h -File |
    Where-Object { $_.FullName -notmatch '\\(ThirdParty|vcpkg\w*|Build|Bin)\\' }

$typeBlindSites = New-Object System.Collections.Generic.List[string]
$guardedSites = 0
$excludeListSites = New-Object System.Collections.Generic.List[string]

foreach ($file in $sourceFiles) {
    $lines = [IO.File]::ReadAllLines($file.FullName)
    for ($i = 0; $i -lt $lines.Length; $i++) {
        if ($lines[$i] -notmatch 'GetDragDropPayload\s*\(') { continue }

        $relative = $file.FullName.Substring($repoRoot.Length + 1)
        $site = "$relative$([char]58)$($i + 1)"
        $typeBlindSites.Add($site)

        # 조건식은 여러 줄에 걸친다 — 여는 줄부터 블록이 열릴 때까지 모아 본다.
        $window = ""
        for ($k = $i; $k -lt [Math]::Min($i + 6, $lines.Length); $k++) {
            $window += $lines[$k] + "`n"
            if ($lines[$k] -match '\{\s*$') { break }
        }

        if ($window -match 'carries_path\s*\(') { $guardedSites++ }
        if ($window -match '!\s*[\w:>\.\-]*IsDataType\s*\(') { $excludeListSites.Add($site) }
    }
}

Assert ($typeBlindSites.Count -ge 1) `
    "GetDragDropPayload call sites vanished — the gate would pass on an empty set (found $($typeBlindSites.Count))"
Assert ($guardedSites -eq $typeBlindSites.Count) `
    "type-blind payload reads not guarded by asset_drag::carries_path: $($typeBlindSites.Count - $guardedSites) of $($typeBlindSites.Count) [$($typeBlindSites -join ', ')]"
Assert ($excludeListSites.Count -eq 0) `
    "type-blind payload read filtered by an exclude list (!IsDataType) — that is the 9-18 crash shape: $($excludeListSites -join ', ')"

# ─────────────────────────────────────────────────────────────────────────
# 3 — path_of 는 path 를 만들기 전에 UTF-8 을 본다
# ─────────────────────────────────────────────────────────────────────────
$pathOfBody = ""
if ($dragPayload -match '(?s)path_of\s*\(\s*const\s+ImGuiPayload&\s*payload\s*\)\s*\{(.*?)\n    \}') {
    $pathOfBody = $Matches[1]
}
Assert (-not [string]::IsNullOrWhiteSpace($pathOfBody)) `
    "path_of body not found in $kDragPayloadCpp — the gate cannot see what it claims to check"

$validateAt = $pathOfBody.IndexOf('is_valid_utf8')
$constructAt = $pathOfBody.IndexOf('std::filesystem::path(')
Assert ($validateAt -ge 0) `
    "path_of does not validate UTF-8 — std::filesystem::path throws on invalid bytes instead of returning empty"
Assert ($validateAt -ge 0 -and $constructAt -ge 0 -and $validateAt -lt $constructAt) `
    "path_of validates UTF-8 after constructing the path — the throw happens in the constructor, so the order is the contract"
Assert ($dragHeader -match 'UTF-8') `
    "$kDragPayloadH does not state the UTF-8 half of the path_of contract"

# UTF-8 검사기가 MultiByteToWideChar 가 거절하는 것들을 같이 거절하는가.
Assert ($dragPayload -match '0xD800' -and $dragPayload -match '0x10FFFF') `
    "is_valid_utf8 does not reject surrogates / out-of-range code points"
Assert ($dragPayload -match '0xC2') `
    "is_valid_utf8 accepts overlong two-byte leads (0xC0/0xC1)"

# ─────────────────────────────────────────────────────────────────────────
# 4 — set_payload 는 표 밖의 타입을 거부하고 이유를 남긴다
# ─────────────────────────────────────────────────────────────────────────
$setPayloadBody = ""
if ($dragPayload -match '(?s)void\s+set_payload\s*\([^)]*\)\s*\{(.*?)\n    \}') {
    $setPayloadBody = $Matches[1]
}
Assert (-not [string]::IsNullOrWhiteSpace($setPayloadBody)) `
    "set_payload body not found in $kDragPayloadCpp"
Assert ($setPayloadBody -match 'is_path_payload_type') `
    "set_payload does not check the path payload table — the table can drift away from the browser silently"
Assert ($setPayloadBody -match 'PrintLog') `
    "set_payload rejects unknown types without saying why"

# ─────────────────────────────────────────────────────────────────────────
# 5 — 두 표가 어긋나지 않는다
# ─────────────────────────────────────────────────────────────────────────
#
# 브라우저 타일은 `FileTypeToString(fileType)` 이 돌려준 이름으로 payload 를 싣고,
# 두 폴더에만 다른 이름을 쓴다. 그 이름 전부가 asset_drag 의 표에 있어야 한다.
$browserTypes = New-Object System.Collections.Generic.List[string]
if ($browser -match '(?s)FileTypeStringTable\s*\{\s*\{(.*?)\}\s*\}\s*;') {
    foreach ($m in [regex]::Matches($Matches[1], '"([^"]+)"')) {
        $browserTypes.Add($m.Groups[1].Value)
    }
}
# 폴더로 갈리는 두 이름은 표 밖에서 정해진다(ContentsBrowserWindow.cpp 타일 코드).
foreach ($m in [regex]::Matches($browser, 'payloadType\s*=\s*"([^"]+)"')) {
    $browserTypes.Add($m.Groups[1].Value)
}

$tableTypes = New-Object System.Collections.Generic.List[string]
if ($dragPayload -match '(?s)kPathPayloadTypes\[\]\s*=\s*\{(.*?)\}\s*;') {
    foreach ($m in [regex]::Matches($Matches[1], '"([^"]+)"')) {
        $tableTypes.Add($m.Groups[1].Value)
    }
}

Assert ($browserTypes.Count -ge 13) `
    "browser payload type names not found (got $($browserTypes.Count)) — an empty set would pass the comparison below"
Assert ($tableTypes.Count -ge 13) `
    "kPathPayloadTypes not found in $kDragPayloadCpp (got $($tableTypes.Count))"

$missing = @($browserTypes | Where-Object { $tableTypes -notcontains $_ } | Sort-Object -Unique)
Assert ($missing.Count -eq 0) `
    "browser drags payload types the table does not know — those drags are refused at the source: $($missing -join ', ')"

# 표에만 있고 브라우저가 안 쓰는 이름은 결함이 아니다(다른 창이 실을 수 있다) —
# 세어서 보이기만 한다.
$unusedInBrowser = @($tableTypes | Where-Object { $browserTypes -notcontains $_ } | Sort-Object -Unique)

# ─────────────────────────────────────────────────────────────────────────
# 6 — 번들 창은 싣는 객체의 크기로 싣는다
# ─────────────────────────────────────────────────────────────────────────
$bundleCalls = [regex]::Matches($bundle, 'SetDragDropPayload\s*\(\s*"[^"]*"\s*,\s*&(\w+)\s*,\s*sizeof\(\s*(\w+)\s*\)\s*\)')
Assert ($bundleCalls.Count -eq 3) `
    "expected 3 ASSET_ENTRY drag sources in $kBundleCpp, found $($bundleCalls.Count)"
$sizeMismatch = New-Object System.Collections.Generic.List[string]
foreach ($call in $bundleCalls) {
    if ($call.Groups[1].Value -ne $call.Groups[2].Value) {
        $sizeMismatch.Add("&$($call.Groups[1].Value) sent as sizeof($($call.Groups[2].Value))")
    }
}
Assert ($sizeMismatch.Count -eq 0) `
    "drag payload size does not match the object being sent — the receiver reads past the buffer: $($sizeMismatch -join '; ')"

# ─────────────────────────────────────────────────────────────────────────
# 판정
# ─────────────────────────────────────────────────────────────────────────
Write-Host ""
Write-Host "  type-blind payload reads : $($typeBlindSites.Count) (guarded $guardedSites)"
Write-Host "  browser payload names    : $($browserTypes.Count) -> table $($tableTypes.Count), table-only $($unusedInBrowser.Count)"
Write-Host "  bundle drag sources      : $($bundleCalls.Count)"
Write-Host ""

if ($failures.Count -gt 0) {
    Write-Host "FAIL  drag payload contract ($($failures.Count) of $checks)" -ForegroundColor Red
    foreach ($f in $failures) { Write-Host "  - $f" -ForegroundColor Red }
    exit 1
}

Write-Host "PASS  drag payload contract ($checks checks)" -ForegroundColor Green
exit 0
