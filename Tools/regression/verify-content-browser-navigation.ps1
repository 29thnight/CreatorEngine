# Content Browser 탐색·신원·생성 경로 (PHASE 21 W2-B)
#
# 왜 필요한가
# ───────────
# 브라우저의 이력은 경로 목록이었다. 뒤로 가면 무엇을 찾고 있었는지(검색어)·무엇을
# 골랐는지·어디까지 내렸는지가 사라졌고, 끌어 온 자산은 파일 이름만 실어 받는 자리
# 17곳이 저마다 `<유형 폴더>\이름` 으로 경로를 다시 지었다 — 전체 자산에서
# `Animation/Cha_Mon_5.fbx` 를 끌면 `Models/Cha_Mon_5.fbx` 가 열린다. Volume Profile
# 은 OS 저장 대화상자에서 이름만 가져와 다른 폴더에 썼고, 같은 이름이면 말없이 숫자를
# 붙였으며, 취소도 실패도 반환값을 버린 호출자 앞에서 같은 침묵이었다.
#
# 착수 시점에 이 상태를 밖에서 읽거나 일으킬 명령이 0 개였다. `editor.browser` 가
# 그 창구다(요청함 → 프레임 머리 적용 → 프레임 끝 게시).
#
# ── 판정 항목 ──
#
# 실행 축 (에디터 한 번, 창 숨김)
#   1  기준 — 뿌리 · 이력 0/1 · 뒤로/위로 불가
#   2  ★ Volume Profile 생성 — 이름을 받아 VolumeProfile 폴더에 만들고 고른다.
#      같은 이름은 거부(말없는 숫자 붙이기 금지) · 잘못된 이름 거부 · 다른 폴더 거부 ·
#      가상 위치 거부. 거부해도 디스크에 아무것도 늘지 않는다
#   3  ★ 방문 복원 — 검색·선택한 채 떠났다가 뒤로 오면 검색어·선택이 돌아온다.
#      정상 이동은 검색어를 비운다
#   4  ★ 앞으로 이력 제거 — A→B→뒤로→C 뒤 앞으로가 없다
#   5  스크롤 복원 — 내린 채 떠났다가 뒤로 오면 그 위치다. 새 방문은 0 에서 시작한다
#   6  ★ 사라진 폴더 — 이력의 폴더가 지워졌으면 Assets 안의 가장 가까운 조상으로 가고
#      이유를 적는다
#   7  최근 항목 정리 — 파일이 사라진 최근 항목은 목록에서 빠지고 저장 파일에서도 빠진다
#   8  ★ 전체 자산 — 한 프레임에 다 훑지 않고 차오른다(첫 표본 pending > 0) · 끝나면
#      pending 0 · 만들기/위로 불가 · 동명 두 파일이 경로로 구별되어 둘 다 나온다 ·
#      보이는 줄만 그린다(그린 타일 < 결과 수). Release 실측 4,274 개 p95 15.9ms 가 이유다 ·
#      목록·범위·검색이 그대로인 프레임에는 결과를 다시 모으지 않는다(`resultRebuilds`) ·
#      그러나 목록 **내용**이 바뀌면(폴더에 선 채 밖에서 파일이 생기면) 다시 모은다
#   9  가상 위치 이력 — 최근→뒤로가 전체 자산과 그 검색어로 돌아온다
#  10  폴더 만들기 — 같은 길로 만들고 들어간다 · 같은 이름 거부
#  11  엔진이 정상 종료했다
#
# 소스 축 (런타임이 못 보는 것 — 끌어다 놓기는 사람 손으로만 일어난다)
#  12  ★ 브라우저 타일의 payload 는 `editor::asset_drag::set_payload` 하나다.
#      `SetDragDropPayload` 를 직접 부르지 않는다
#  13  ★ 브라우저 유형을 받는 자리의 **집합**이 기록과 같고, 그 전부가 `path_of` 로
#      읽으며 `payload->Data` 를 직접 읽거나 `PathFinder::Relative` 로 경로를 다시
#      짓지 않는다. 수를 세지 않고 (파일, 유형) 목록을 맞댄다 — 한 자리가 빠지고 다른
#      자리가 늘어도 수는 같다
#  14  이름만 저장하는 소비자(데칼 셋 · 스프라이트 시트 · 폴리지)는 다른 폴더의 동명
#      파일을 거부한다
#  15  Volume Profile 생성 본문에 OS 대화상자·숫자 붙이기가 없고, 브라우저의 두 메뉴가
#      같은 술어(`CanCreateVolumeProfileIn`)와 같은 생성 경로(`CreateNamedAsset`)를 쓴다
#  16  "Show in Folder" 는 원래 폴더로 가서 그 자산을 고른다
#
# 프로젝트 전환 축 (임시 프로젝트 둘 · 같은 작업 공간 · 에디터 3 회)
#  17  ★ 이전 프로젝트의 이력·선택·최근 항목이 새지 않는다. 계약 6 의 *"프로젝트 변경 때
#      이전 프로젝트의 이력/선택을 재사용하지 않는다"* 다. 이 제품에는 세션 안에서 프로젝트를
#      바꾸는 길이 없다 — 뿌리는 기동 때 `--development-project` 로 한 번 정해진다(App.cpp).
#      그래서 전환은 **다른 프로젝트로 다시 띄우는 것**이고, 새어 나갈 수 있는 통로는 둘이다:
#      같은 작업 공간(`CREATOR_EDITOR_WORKSPACE_DIR`)과 최근 항목 저장 파일.
#      A 에서 Volume Profile 을 만들어 최근에 올리고 하위 폴더로 이동한다 → B 를 같은 작업
#      공간으로 띄운다 → B 는 뿌리·이력 0/1·선택 없음·최근 0 이어야 하고, 결과는 B 의 폴더다.
#      B 에도 **같은 상대 경로** `VolumeProfile/Keep.volume` 을 둔다 — 최근 항목이 프로젝트가
#      아니라 상대 경로로만 묶이면 B 에서 그것이 살아난다. 다시 A 를 띄우면 A 의 최근 1 이
#      그대로다(B 가 덮지 않았다). A 의 저장 파일 바이트는 B 회차 앞뒤로 같아야 한다.
#
# 사용
#   pwsh -File Tools/regression/verify-content-browser-navigation.ps1 -Exe <CreatorEditor.exe>
#   -SkipRuntime            소스 축만(변이 확인용)
#   -SkipProjectSwitch      프로젝트 전환 축을 건너뛴다
#   -SourceRoot <dir>       소스 축이 읽을 저장소 뿌리(변이 사본)
param(
    [string]$Exe = (Join-Path $PSScriptRoot "..\..\Bin\x64-Debug\Editor\CreatorEditor.exe"),
    [string]$ProjectRoot = (Join-Path $PSScriptRoot "..\..\Dynamic_CPP"),
    [string]$SourceRoot = (Join-Path $PSScriptRoot "..\.."),
    [string]$Work = "",
    [switch]$SkipRuntime,
    [switch]$SkipProjectSwitch
)

$ErrorActionPreference = "Stop"

$failures = New-Object System.Collections.Generic.List[string]
$checks = 0
function Assert([bool]$ok, [string]$what) {
    $script:checks++
    if (-not $ok) { $script:failures.Add($what) }
}

# ═══════════════════════════════════════════════════════════════════════════
# 소스 축
# ═══════════════════════════════════════════════════════════════════════════
$SourceRoot = (Resolve-Path -LiteralPath $SourceRoot).Path
$browserCpp = Join-Path $SourceRoot "Editor\EngineGUIWindow\ContentsBrowserWindow.cpp"
$databaseCpp = Join-Path $SourceRoot "Editor\EngineEntry\EditorAssetDatabase.cpp"
$browserText = [IO.File]::ReadAllText($browserCpp)

# 12 — payload 원천은 하나
$directPayload = [regex]::Matches($browserText, 'SetDragDropPayload\s*\(').Count
Assert ($directPayload -eq 0) "12 브라우저가 SetDragDropPayload 를 직접 부른다($directPayload 곳) — 이름만 싣는 옛 길이 살아 있다"
$setPayload = [regex]::Matches($browserText, 'editor::asset_drag::set_payload\s*\(\s*payloadType\s*,\s*directory\s*\)').Count
Assert ($setPayload -eq 1) "12 타일이 set_payload(payloadType, directory) 를 정확히 한 번 부르지 않는다($setPayload)"

# 13 — 받는 자리의 집합
$browserTypes = @([regex]::Matches($browserText, '\{\s*FileType::\w+\s*,\s*"(\w+)"\s*\}') | ForEach-Object { $_.Groups[1].Value })
$browserTypes += @('SPRITESHEET', 'UI_TEXTURE')
Assert ($browserTypes.Count -ge 10) "13 FileTypeStringTable 을 읽지 못했다($($browserTypes.Count))"
$typePattern = '^(?!\s*//).*AcceptDragDropPayload\s*\(\s*"(' + (($browserTypes | ForEach-Object { [regex]::Escape($_) }) -join '|') + ')"'

$expectedSites = @(
    'HierarchyWindow.cpp:Font', 'HierarchyWindow.cpp:Model', 'HierarchyWindow.cpp:SPRITESHEET',
    'HierarchyWindow.cpp:Texture', 'HierarchyWindow.cpp:UI_TEXTURE',
    'ImGuiDrawHelperMeshRenderer.cpp:Texture', 'ImGuiDrawHelperTerrainComponent.cpp:Model',
    'InspectorWindow.cpp:HDR', 'InspectorWindow.cpp:(payloadType)', 'InspectorWindow.cpp:VolumeProfile',
    'SceneViewWindow.cpp:HDR', 'SceneViewWindow.cpp:Model', 'SceneViewWindow.cpp:Prefab'
) | Sort-Object

$sites = New-Object System.Collections.Generic.List[string]
$blocks = @{}
$editorSources = Get-ChildItem -LiteralPath (Join-Path $SourceRoot "Editor") -Recurse -Filter *.cpp -File
foreach ($file in $editorSources) {
    $lines = [IO.File]::ReadAllLines($file.FullName)
    for ($i = 0; $i -lt $lines.Count; $i++) {
        # 여러 줄로 나뉜 호출(`payload =\n AcceptDragDropPayload(`)도 한 줄 뒤에서 잡힌다.
        $m = [regex]::Match($lines[$i], $typePattern)
        # W2-I4: 전용 드로어의 자산 칸은 유형을 인자로 받는 조각 하나(`DrawAssetSlot`)로 모였다.
        # 유형이 변수인 받는 자리도 같은 계약(path_of)을 지켜야 하므로 `(변수)` 로 센다.
        if (-not $m.Success) { $m = [regex]::Match($lines[$i], '^(?!\s*//).*AcceptDragDropPayload\s*\(\s*(\w+)\s*\)') }
        if (-not $m.Success) { continue }
        $key = if ($lines[$i] -match 'AcceptDragDropPayload\s*\(\s*"') { "$($file.Name):$($m.Groups[1].Value)" } else { "$($file.Name):($($m.Groups[1].Value))" }
        $sites.Add($key)
        $end = [Math]::Min($lines.Count - 1, $i + 40)
        $body = New-Object System.Text.StringBuilder
        for ($j = $i; $j -le $end; $j++) {
            if ($j -gt $i -and ($lines[$j] -match 'AcceptDragDropPayload\s*\(' -or $lines[$j] -match 'EndDragDropTarget')) { break }
            [void]$body.AppendLine($lines[$j])
        }
        $text = $body.ToString()
        $where = "$($file.Name):$($i + 1) $($m.Groups[1].Value)"
        Assert ($text -match 'asset_drag::path_of\s*\(') "13 $where 가 path_of 로 읽지 않는다"
        Assert (-not ($text -match '[Pp]ayload->Data')) "13 $where 가 payload->Data 를 직접 읽는다"
        Assert (-not ($text -match 'PathFinder::Relative\s*\(')) "13 $where 가 PathFinder::Relative 로 경로를 다시 짓는다"
        $blocks["$($file.Name):$($i + 1)"] = $text
    }
}
$actualSites = @($sites | Sort-Object)
$diff = Compare-Object -ReferenceObject $expectedSites -DifferenceObject $actualSites
Assert ($null -eq $diff) ("13 받는 자리 집합이 기록과 다르다: " + (($diff | ForEach-Object { "$($_.SideIndicator)$($_.InputObject)" }) -join ', '))

# 13 이어서 — 유형을 인자로 받는 조각을 부르는 자리의 유형 집합. 조각이 받는 유형이 조용히
# 바뀌거나(예: UI_TEXTURE → Texture) 부르는 자리가 늘면 여기서 드러난다.
$inspectorText = [IO.File]::ReadAllText((Join-Path $SourceRoot "Editor\EngineGUIWindow\InspectorWindow.cpp"))
$slotTypes = @([regex]::Matches($inspectorText, 'DrawAssetSlot\((?:[^;"]|"[^"]*")*?"(\w+)"\s*,\s*sheet\.line\(') | ForEach-Object { $_.Groups[1].Value } | Sort-Object)
Assert (($slotTypes -join ',') -eq 'Texture,Texture,UI_TEXTURE') "13 DrawAssetSlot 을 부르는 자리의 유형이 기록(데칼 Texture · 스프라이트 Texture · 이미지 UI_TEXTURE)과 다르다: $($slotTypes -join ',')"

# 14 — 이름만 저장하는 소비자. 데칼 세 칸은 한 람다(`slot`)를 지나고, 람다는 Textures 폴더 검사를
# 통과한 경우에만 이름을 돌려준다. 칸마다 그 이름이 자기 Set*Texture 로 간다.
Assert ($inspectorText -match '!dropped\.empty\(\)\s*&&\s*editor::asset_drag::lives_in\(\s*dropped\s*,\s*"Textures"\s*,\s*context\s*\)\s*\?\s*dropped\.filename\(\)\.string\(\)\s*:\s*std::string\(\)') "14 데칼 칸 람다가 Textures 폴더 검사를 통과할 때만 이름을 돌려주지 않는다"
foreach ($pair in @(@('Diffuse', 'Decal', 'GetDecalTexture'), @('Normal', 'Normal', 'GetNormalTexture'), @('ORM', 'ORM', 'GetORMTexture'))) {
    $pattern = 'slot\(\s*"' + $pair[0] + '"\s*,\s*decalComponent->' + $pair[2] + '\(\)\s*,\s*"Decal ' + $pair[1] + ' texture drop"\s*\)\s*;\s*!name\.empty\(\)\s*\)\s*decalComponent->Set' + $pair[1] + 'Texture\(\s*name\.c_str\(\)\s*\)'
    Assert ($inspectorText -match $pattern) "14 데칼 $($pair[1]) 이 Textures 폴더 검사 뒤에만 이름을 넘기지 않는다"
}
$hierarchyText = [IO.File]::ReadAllText((Join-Path $SourceRoot "Editor\EngineGUIWindow\HierarchyWindow.cpp"))
Assert ($hierarchyText -match '!editor::asset_drag::lives_in\(\s*filepath\s*,\s*"SpriteSheets"[^)]*\)\s*\)\s*\{[^}]*\}\s*else if \(selectedSceneObject\)') `
    "14 하이어라키 스프라이트 시트가 SpriteSheets 폴더 검사로 막히지 않는다"
$terrainText = [IO.File]::ReadAllText((Join-Path $SourceRoot "Editor\EngineGUIWindow\ImGuiDrawHelperTerrainComponent.cpp"))
Assert ($terrainText -match 'GetStemToGuid\(\s*filepath\.stem\(\)\.string\(\)\s*\)\s*!=\s*droppedGuid\s*\)\s*\{[^}]*\}\s*else if \(auto generation') `
    "14 폴리지가 stem 이 끌어 온 파일로 돌아오는지 보지 않는다"

# 15 — Volume Profile 생성
$databaseText = [IO.File]::ReadAllText($databaseCpp)
$vp = [regex]::Match($databaseText, '(?s)bool EditorAssetDatabase::CreateVolumeProfile\(.*?\n\}')
Assert $vp.Success "15 CreateVolumeProfile 본문을 찾지 못했다"
if ($vp.Success) {
    Assert (-not ($vp.Value -match 'ShowSaveFileDialog')) "15 CreateVolumeProfile 이 OS 저장 대화상자를 연다"
    Assert (-not ($vp.Value -match 'suffix|to_string')) "15 CreateVolumeProfile 이 같은 이름에 숫자를 붙인다"
    Assert ($vp.Value -match 'already exists') "15 CreateVolumeProfile 이 같은 이름을 거부하지 않는다"
    Assert ($vp.Value -match 'std::string& error') "15 CreateVolumeProfile 이 이유를 돌려주지 않는다"
}
$createCalls = [regex]::Matches($browserText, 'CreateVolumeProfile\s*\(').Count
Assert ($createCalls -eq 1) "15 브라우저의 CreateVolumeProfile 호출이 CreateNamedAsset 한 곳이 아니다($createCalls)"
$menuItems = [regex]::Matches($browserText, 'CanCreateVolumeProfileIn\([^)]*\)\s*(\r?\n\s*)?&&\s*ImGui::MenuItem\("Create Volume Profile\.\.\."\)').Count
Assert ($menuItems -eq 2) "15 Volume Profile 메뉴 둘이 같은 술어를 쓰지 않는다($menuItems)"
$openDialogs = [regex]::Matches($browserText, 'OpenCreateDialog\(CreateKind::volume_profile').Count
Assert ($openDialogs -eq 2) "15 Volume Profile 메뉴 둘이 같은 대화상자를 열지 않는다($openDialogs)"

# 16 — Show in Folder
Assert ($browserText -match '(?s)MenuItem\("Show in Folder"\)\)\s*\{\s*const file::path asset = directory;\s*if \(Navigate\(asset\.parent_path\(\)\)\) SelectAsset\(asset\);') `
    "16 Show in Folder 가 원래 폴더로 가서 그 자산을 고르지 않는다"

# ═══════════════════════════════════════════════════════════════════════════
# 실행 축
# ═══════════════════════════════════════════════════════════════════════════
$runtimeSummary = "실행 축 건너뜀"
if (-not $SkipRuntime) {
    if (-not (Test-Path -LiteralPath $Exe)) { throw "Editor executable is missing: $Exe" }
    $ProjectRoot = (Resolve-Path -LiteralPath $ProjectRoot).Path
    $assets = Join-Path $ProjectRoot "Assets"
    $volumeDir = Join-Path $assets "VolumeProfile"
    $recentsFile = Join-Path $ProjectRoot "Library\EditorState\ContentBrowserRecents.txt"
    if (-not (Test-Path -LiteralPath $volumeDir)) { throw "VolumeProfile folder is missing: $volumeDir" }
    if ([string]::IsNullOrWhiteSpace($Work)) {
        $Work = Join-Path ([IO.Path]::GetTempPath()) ("CE_BrowserNav_" + [guid]::NewGuid().ToString("N"))
    }
    New-Item -ItemType Directory -Force -Path $Work | Out-Null

    # ── 검사용 자산 ──
    # 이름에 실행마다 다른 꼬리를 붙인다. 앞선 실행이 죽어 남긴 것과 섞이지 않게.
    $tag = "W2BCheck_" + [guid]::NewGuid().ToString("N").Substring(0, 8)
    $fixture = Join-Path $assets $tag
    $profileName = "${tag}_Profile"
    $profilePath = Join-Path $volumeDir "$profileName.volume"
    $seedVolume = Get-ChildItem -LiteralPath $volumeDir -Filter *.volume -File | Select-Object -First 1
    if ($null -eq $seedVolume) { throw "no .volume file to copy as a fixture" }
    foreach ($dir in @('Alpha', 'Beta', 'Gamma', 'Doomed\Inner', 'Many')) {
        New-Item -ItemType Directory -Force -Path (Join-Path $fixture $dir) | Out-Null
    }
    Copy-Item -LiteralPath $seedVolume.FullName -Destination (Join-Path $fixture 'Alpha\Twin.volume')
    Copy-Item -LiteralPath $seedVolume.FullName -Destination (Join-Path $fixture 'Beta\Twin.volume')
    for ($n = 0; $n -lt 120; $n++) {
        Copy-Item -LiteralPath $seedVolume.FullName -Destination (Join-Path $fixture ('Many\Item_{0:D3}.volume' -f $n))
    }
    $volumeCountBefore = @(Get-ChildItem -LiteralPath $volumeDir -File).Count

    # 최근 항목 파일은 개발자 것이다 — 통째로 보관했다가 되돌린다.
    $recentsBackup = $null
    if (Test-Path -LiteralPath $recentsFile) { $recentsBackup = [IO.File]::ReadAllBytes($recentsFile) }

    $B = 'editor.browser'
    $lines = @(
        'wait 240',
        $B,                                                          # 0 기준
        "$B go VolumeProfile", 'wait 5',
        "$B create volume $profileName", 'wait 5',
        $B,                                                          # 1 생성·선택
        "$B create volume $profileName", 'wait 5',
        $B,                                                          # 2 같은 이름
        "$B create volume bad|name", 'wait 5',
        $B,                                                          # 3 잘못된 이름
        "$B search $tag", 'wait 5',
        "$B select VolumeProfile/$profileName.volume", 'wait 5',
        $B,                                                          # 4 검색·선택
        "$B go $tag/Gamma", 'wait 5',
        $B,                                                          # 5 검색어 비움
        "$B create volume Nope", 'wait 5',
        $B,                                                          # 6 다른 폴더 거부
        "$B back", 'wait 5',
        $B,                                                          # 7 복원
        "$B go $tag/Alpha", 'wait 5',
        $B,                                                          # 8 앞으로 이력 제거
        "$B go $tag/Many", 'wait 20',
        "$B scroll 300", 'wait 20',
        $B,                                                          # 9 내렸다
        "$B go $tag/Gamma", 'wait 20',
        $B,                                                          # 10 새 방문은 0
        "$B back", 'wait 20',
        $B,                                                          # 11 스크롤 복원
        "$B go $tag/Doomed/Inner", 'wait 5',
        "$B go $tag/Gamma", 'wait 5',
        # 표지: 에디터가 디스크에 폴더를 만든다 → 검사가 그것을 보고 지운다. 결과 파일은
        # 에디터가 공유 없이 열어 실행 중에는 읽히지 않으므로 표지로 쓸 수 없다.
        # ★ 표지 폴더에 검사가 넣은 파일은 목록 재검증(나이 1 초) 뒤에야 보인다. 프레임
        #   수 하나로 기다리면 Release 에서 1 초에 못 미친다(900 프레임으로 붉었다).
        #   400 프레임씩 여섯 번 표집하고 그 가운데 하나라도 보이면 된다. 표집 줄은 끝에
        #   공백을 붙여 번호 붙은 표본과 가른다(명령 해석은 같다).
        "$B create folder Signal",
        'wait 400', "$B ", 'wait 400', "$B ", 'wait 400', "$B ",
        'wait 400', "$B ", 'wait 400', "$B ", 'wait 400',
        $B,                                                          # 12 표지 폴더 안 — 늦게 생긴 파일
        "$B back", 'wait 10',
        "$B back", 'wait 30',
        $B,                                                          # 13 사라진 폴더
        # 목록 캐시는 나이(1 초)로 다시 훑는다. Release 는 프레임이 빨라 그 안에 여기까지
        # 온다 — 프레임 수로 기다리면 기계 속도에 묶인다. 같은 이름 폴더 만들기는
        # 실패해도 캐시를 버린다(디스크가 움직였을 수 있다) — 그것으로 다시 훑게 한다.
        "$B create folder Gamma", 'wait 5',
        "$B go @recent", 'wait 30',
        $B,                                                          # 14 최근 정리
        "$B go @everything", 'wait 1',
        $B,                                                          # 15 차오르는 중
        'wait 400',
        $B,                                                          # 16 다 찼다
        'editor.panelcost',
        'wait 200',
        $B,                                                          # 17 대기 — 다시 모으지 않는다
        "$B search Twin", 'wait 5',
        $B,                                                          # 18 동명 둘
        "$B create folder Nope", 'wait 5',
        $B,                                                          # 19 가상 위치 거부
        "$B go @recent", 'wait 5',
        "$B back", 'wait 5',
        $B,                                                          # 20 가상 위치 이력
        "$B go $tag/Gamma", 'wait 5',
        "$B create folder Sub", 'wait 5',
        $B,                                                          # 21 폴더 만들기
        "$B up", 'wait 5',
        "$B create folder Sub", 'wait 5',
        $B,                                                          # 22 같은 이름 폴더
        'quit'
    )
    $signalDir = Join-Path $fixture 'Gamma\Signal'
    $scriptPath = Join-Path $Work "browser.txt"
    $resultPath = Join-Path $Work "browser.jsonl"
    Set-Content -LiteralPath $scriptPath -Encoding UTF8 -Value $lines

    $savedWorkspaceDir = $env:CREATOR_EDITOR_WORKSPACE_DIR
    $savedLegacyIni = $env:CREATOR_EDITOR_LEGACY_INI
    $env:CREATOR_EDITOR_WORKSPACE_DIR = $Work
    $env:CREATOR_EDITOR_LEGACY_INI = Join-Path $Work "legacy.ini"
    $exitCode = -1
    $deletedAtMarker = $false
    try {
        $proc = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe) -PassThru -WindowStyle Hidden `
            -ArgumentList @('--script', ('"' + $scriptPath + '"'), '--result-format', 'jsonl',
                            '--result-file', ('"' + $resultPath + '"')) `
            -RedirectStandardOutput (Join-Path $Work 'stdout.txt') -RedirectStandardError (Join-Path $Work 'stderr.txt')
        # 표지 폴더가 생기면 지운다 — 에디터는 그 뒤 `wait 400` 동안 기다리고 있다.
        $deadline = [DateTime]::UtcNow.AddMinutes(10)
        while (-not $proc.HasExited) {
            if (-not $deletedAtMarker -and (Test-Path -LiteralPath $signalDir)) {
                Remove-Item -LiteralPath (Join-Path $fixture 'Doomed') -Recurse -Force
                Remove-Item -LiteralPath $profilePath -Force -ErrorAction SilentlyContinue
                Remove-Item -LiteralPath "$profilePath.meta" -Force -ErrorAction SilentlyContinue
                # 결과 기억이 **내용이 바뀐 목록**을 알아채는지 — 에디터는 지금 이 폴더에
                # 서 있고, 목록·범위·검색은 그대로다. 바뀌는 것은 디스크의 내용뿐이다.
                Copy-Item -LiteralPath $seedVolume.FullName -Destination (Join-Path $signalDir 'Late.volume')
                $deletedAtMarker = $true
            }
            if ([DateTime]::UtcNow -gt $deadline) { $proc.Kill(); throw "editor did not exit in time" }
            Start-Sleep -Milliseconds 100
        }
        $proc.WaitForExit()
        $exitCode = $proc.ExitCode
    }
    finally {
        if ($null -ne $savedWorkspaceDir) { $env:CREATOR_EDITOR_WORKSPACE_DIR = $savedWorkspaceDir }
        else { Remove-Item Env:CREATOR_EDITOR_WORKSPACE_DIR -ErrorAction SilentlyContinue }
        if ($null -ne $savedLegacyIni) { $env:CREATOR_EDITOR_LEGACY_INI = $savedLegacyIni }
        else { Remove-Item Env:CREATOR_EDITOR_LEGACY_INI -ErrorAction SilentlyContinue }

        $recentsAfter = if (Test-Path -LiteralPath $recentsFile) { [IO.File]::ReadAllText($recentsFile) } else { "" }
        Remove-Item -LiteralPath $fixture -Recurse -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath "$fixture.meta" -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath $profilePath -Force -ErrorAction SilentlyContinue
        Remove-Item -LiteralPath "$profilePath.meta" -Force -ErrorAction SilentlyContinue
        if ($null -ne $recentsBackup) { [IO.File]::WriteAllBytes($recentsFile, $recentsBackup) }
        elseif (Test-Path -LiteralPath $recentsFile) { Remove-Item -LiteralPath $recentsFile -Force }
    }

    Assert $deletedAtMarker "6 표지 스냅샷을 못 봐서 폴더를 지우지 못했다 — 6·7 은 자극되지 않았다"
    if (-not (Test-Path -LiteralPath $resultPath)) { throw "result file missing: $resultPath" }
    $rows = @(Get-Content -LiteralPath $resultPath | Where-Object { $_.Trim() } | ForEach-Object { $_ | ConvertFrom-Json })
    # 결과 줄은 스크립트 줄과 1:1 이다. 인자 없는 호출만 표본으로 쓴다 — 인자 준 호출이
    # 돌려주는 게시본은 요청이 적용되기 전 것이다.
    Assert ($rows.Count -eq $lines.Count) "결과 줄 수 $($rows.Count) 가 스크립트 줄 수 $($lines.Count) 와 다르다"
    $snaps = @()
    for ($k = 0; $k -lt [Math]::Min($rows.Count, $lines.Count); $k++) {
        if ($lines[$k] -eq $B) { $snaps += $rows[$k] }
    }
    Assert ($snaps.Count -eq 23) "스냅샷 표본이 23 개가 아니다($($snaps.Count)) — 판정할 수 없다"
    if ($snaps.Count -eq 23) {
        $s = @($snaps | ForEach-Object { $_.data })
        $vp = "VolumeProfile/$profileName.volume"

        # 1
        Assert ($snaps[0].status -eq 'succeeded') "1 첫 표본이 실패했다: $($snaps[0].code)"
        Assert ($s[0].scope -eq 'folder' -and $s[0].directory -eq '.') "1 기준 위치가 뿌리가 아니다($($s[0].scope) $($s[0].directory))"
        Assert (-not $s[0].canBack -and -not $s[0].canUp) "1 뿌리에서 뒤로/위로가 열려 있다"

        # 2
        Assert ($s[1].selected -eq $vp) "2 만든 프로파일이 선택되지 않았다(selected='$($s[1].selected)')"
        Assert ($s[1].canCreateVolumeProfile) "2 VolumeProfile 폴더에서 생성 술어가 거짓이다"
        Assert ([int]$s[1].recentCount -ge 1) "2 만든 프로파일이 최근 항목에 오르지 않았다"
        Assert ([int]$s[2].requestsRejected -eq [int]$s[1].requestsRejected + 1 -and $s[2].lastRejection -match 'already exists') `
            "2 같은 이름이 거부되지 않았다(last='$($s[2].lastRejection)')"
        Assert ([int]$s[3].requestsRejected -eq [int]$s[2].requestsRejected + 1 -and $s[3].lastRejection -match 'valid volume profile name') `
            "2 잘못된 이름이 거부되지 않았다(last='$($s[3].lastRejection)')"
        Assert (-not $s[5].canCreateVolumeProfile) "2 다른 폴더에서 생성 술어가 참이다"
        Assert ($s[6].lastRejection -match 'VolumeProfile folder') "2 다른 폴더의 생성이 거부되지 않았다(last='$($s[6].lastRejection)')"
        Assert ($s[19].lastRejection -match 'open a folder') "2 가상 위치의 생성이 거부되지 않았다(last='$($s[19].lastRejection)')"

        # 3
        Assert ($s[4].search -eq $tag -and $s[4].selected -eq $vp) "3 검색·선택이 서지 않았다('$($s[4].search)' '$($s[4].selected)')"
        Assert ($s[5].search -eq '' -and $s[5].directory -eq "$tag/Gamma") "3 정상 이동이 검색어를 비우지 않았다('$($s[5].search)')"
        Assert ($s[7].directory -eq 'VolumeProfile' -and $s[7].search -eq $tag -and $s[7].selected -eq $vp) `
            "3 뒤로가 방문을 복원하지 않았다(dir='$($s[7].directory)' search='$($s[7].search)' selected='$($s[7].selected)')"
        Assert ($s[7].canForward) "3 뒤로 간 뒤 앞으로가 닫혀 있다"

        # 4
        $h8 = @($s[8].history)
        Assert (-not $s[8].canForward -and [int]$s[8].historyIndex + 1 -eq [int]$s[8].historySize) "4 새 이동 뒤 앞으로 이력이 남았다"
        Assert ($h8.Count -ge 2 -and $h8[$h8.Count - 1] -eq "folder:$tag/Alpha" -and $h8[$h8.Count - 2] -eq 'folder:VolumeProfile') `
            "4 이력 끝이 VolumeProfile→Alpha 가 아니다([$($h8 -join ', ')])"
        Assert (-not ($h8 -contains "folder:$tag/Gamma")) "4 버려야 할 앞으로 칸(Gamma)이 이력에 남았다([$($h8 -join ', ')])"

        # 5
        $expectedScroll = [Math]::Min(300.0, [double]$s[9].scrollMaxY)
        Assert ([double]$s[9].scrollMaxY -gt 0) "5 Many 목록이 스크롤되지 않는다(max=$($s[9].scrollMaxY)) — 자극되지 않았다"
        Assert ([Math]::Abs([double]$s[9].scrollY - $expectedScroll) -le 1) "5 스크롤 요청이 먹지 않았다($($s[9].scrollY) ≠ $expectedScroll)"
        Assert ([double]$s[10].scrollY -eq 0) "5 새 방문이 0 에서 시작하지 않았다($($s[10].scrollY))"
        Assert ($s[11].directory -eq "$tag/Many" -and [Math]::Abs([double]$s[11].scrollY - $expectedScroll) -le 1) `
            "5 뒤로가 스크롤을 복원하지 않았다(dir='$($s[11].directory)' y=$($s[11].scrollY), 기대 $expectedScroll)"

        # 6
        Assert ($s[12].directory -eq "$tag/Gamma/Signal") "6 표지 폴더로 들어가지 않았다(dir='$($s[12].directory)')"
        # 8 — 결과 기억은 목록 **내용**이 바뀌면 버려진다(캐시 세대).
        $latePolls = @()
        for ($k = 0; $k -lt [Math]::Min($rows.Count, $lines.Count); $k++) {
            if ($lines[$k] -eq "$B ") { $latePolls += $rows[$k].data }
        }
        $lateSeen = @($latePolls + @($s[12]) | Where-Object {
            $_.directory -eq "$tag/Gamma/Signal" -and (@($_.results) -contains "$tag/Gamma/Signal/Late.volume") })
        Write-Host ("  늦게 생긴 파일: 표집 {0} 가운데 {1} 에서 보였다" -f ($latePolls.Count + 1), $lateSeen.Count)
        Assert ($latePolls.Count -eq 5) "8 늦은 파일 표집이 5 개가 아니다($($latePolls.Count))"
        Assert ([int]$s[12].resultCount -eq 1 -and (@($s[12].results) -contains "$tag/Gamma/Signal/Late.volume")) `
            "8 표지 폴더에 늦게 생긴 파일이 마지막 표집에 없다([$(@($s[12].results) -join ' | ')]) — 결과 기억이 바뀐 목록을 못 알아챘다"
        Assert ($s[13].directory -eq $tag) "6 사라진 폴더에서 가장 가까운 조상으로 가지 않았다(dir='$($s[13].directory)')"
        Assert ($s[13].error -match 'no longer exists') "6 사라진 폴더의 이유가 없다(error='$($s[13].error)')"

        # 7
        Assert ($s[14].scope -eq 'recent') "7 최근 항목으로 가지 않았다($($s[14].scope))"
        Assert (-not (@($s[14].results) -contains $vp)) "7 지운 프로파일이 최근 결과에 남았다"
        Assert ([int]$s[14].recentCount -lt [int]$s[7].recentCount -or [int]$s[7].recentCount -eq 0) `
            "7 최근 항목 수가 줄지 않았다($($s[7].recentCount) → $($s[14].recentCount))"
        Assert (-not ($recentsAfter -match [regex]::Escape($profileName))) "7 저장된 최근 항목 파일에 지운 프로파일이 남았다"

        # 8
        Assert ($s[15].scope -eq 'everything' -and -not $s[15].canCreate -and -not $s[15].canUp) "8 전체 자산에서 만들기/위로가 열려 있다"
        Assert ([int]$s[15].everythingPending -gt 0) "8 전체 자산이 첫 프레임에 다 찼다(pending=$($s[15].everythingPending)) — 예산 없이 훑는다"
        Assert ($s[18].everythingComplete -and [int]$s[18].everythingPending -eq 0) "8 전체 자산이 끝나지 않았다(pending=$($s[18].everythingPending))"
        # ★ 잘라 그리기 — 결과가 한 화면을 넘으면 그린 타일이 결과보다 적다.
        $cost = @($rows | Where-Object { $_.command -eq 'editor.panelcost' }) | Select-Object -First 1
        $filesCost = if ($null -ne $cost) { @($cost.data.panels | Where-Object { $_.slot -eq 'browser_files' }) | Select-Object -First 1 } else { $null }
        Assert ($null -ne $filesCost -and [int]$filesCost.lastUnits -gt 0) "8 browser_files 표본이 없다"
        if ($null -ne $filesCost) {
            Assert ([int]$s[16].resultCount -gt 200) "8 전체 자산 결과가 200 개를 넘지 않아 잘라 그리기를 자극하지 못했다($($s[16].resultCount))"
            Assert ([int]$filesCost.lastUnits -lt [int]$s[16].resultCount) `
                "8 결과 $($s[16].resultCount) 개를 전부 그렸다(units=$($filesCost.lastUnits)) — 잘라 그리지 않는다"
        }
        # ★ 결과 기억 — 목록·범위·검색이 그대로인 200 프레임 동안 다시 모으지 않는다.
        #   Release 실측으로 전체 자산 4,274 개에서 매 프레임 모으고 정렬하던 비용이
        #   p95 3.25ms 였다. 감시자가 검사용 자산의 .meta 를 만들며 목록이 바뀌면
        #   정당하게 몇 번 오를 수 있어 0 이 아니라 **프레임 수의 10 분의 1** 로 막는다.
        $idleRebuilds = [int64]$s[17].resultRebuilds - [int64]$s[16].resultRebuilds
        $idleFrames = [int64]$s[17].frames - [int64]$s[16].frames
        Write-Host ("  결과 기억: 대기 {0} 프레임 · 다시 모음 {1} 회 · 결과 {2}" -f $idleFrames, $idleRebuilds, $s[17].resultCount)
        Assert ($idleFrames -ge 100) "8 대기 구간의 창 프레임이 $idleFrames 이다 — 판정할 수 없다"
        Assert ($idleRebuilds -le [Math]::Max(2, [int]($idleFrames / 10))) `
            "8 대기 $idleFrames 프레임 동안 결과를 $idleRebuilds 번 다시 모았다 — 결과 기억이 죽었다"
        Assert ([int]$s[17].resultCount -eq [int]$s[16].resultCount -and $s[17].everythingComplete) `
            "8 대기 뒤 결과 수가 바뀌었다($($s[16].resultCount) → $($s[17].resultCount))"
        $twins = @($s[18].results)
        Assert ($twins.Count -eq 2 -and $twins[0] -eq "$tag/Alpha/Twin.volume" -and $twins[1] -eq "$tag/Beta/Twin.volume") `
            "8 동명 두 파일이 경로 순서로 둘 다 나오지 않았다([$($twins -join ' | ')])"

        # 9
        Assert ($s[20].scope -eq 'everything' -and $s[20].search -eq 'Twin') "9 최근→뒤로가 전체 자산·검색어로 돌아오지 않았다($($s[20].scope) '$($s[20].search)')"

        # 10
        Assert ($s[21].directory -eq "$tag/Gamma/Sub") "10 만든 폴더로 들어가지 않았다(dir='$($s[21].directory)')"
        Assert ($s[22].lastRejection -match 'already exists') "10 같은 이름 폴더가 거부되지 않았다(last='$($s[22].lastRejection)')"
    }

    # 2 — 거부가 디스크에 흔적을 남기지 않았다(표지에서 지운 하나를 빼면 원래 수)
    $volumeCountAfter = @(Get-ChildItem -LiteralPath $volumeDir -File).Count
    Assert ($volumeCountAfter -eq $volumeCountBefore) "2 VolumeProfile 폴더의 파일 수가 달라졌다($volumeCountBefore → $volumeCountAfter)"
    Assert (-not (Get-ChildItem -LiteralPath $volumeDir -File | Where-Object { $_.Name -match 'Nope|bad' })) "2 거부된 이름의 파일이 생겼다"

    # 11
    Assert ($exitCode -eq 0) "11 에디터 종료 코드가 $exitCode 다"
    $runtimeSummary = "실행 표본 $($snaps.Count)"
}

# ═══════════════════════════════════════════════════════════════════════════
# 프로젝트 전환 축
# ═══════════════════════════════════════════════════════════════════════════
$switchSummary = "전환 축 건너뜀"
if (-not $SkipRuntime -and -not $SkipProjectSwitch) {
    $switchRoot = Join-Path ([IO.Path]::GetTempPath()) ("CE_BrowserProjectSwitch_" + [guid]::NewGuid().ToString("N").Substring(0, 8))
    $projectA = Join-Path $switchRoot 'ProjA'
    $projectB = Join-Path $switchRoot 'ProjB'
    $switchWorkspace = Join-Path $switchRoot 'workspace'
    foreach ($dir in @("$projectA\Assets\VolumeProfile", "$projectA\Assets\OnlyA", "$projectB\Assets\VolumeProfile", "$projectB\Assets\OnlyB", $switchWorkspace)) {
        New-Item -ItemType Directory -Force -Path $dir | Out-Null
    }
    $recentsA = Join-Path $projectA 'Library\EditorState\ContentBrowserRecents.txt'
    $recentsB = Join-Path $projectB 'Library\EditorState\ContentBrowserRecents.txt'

    function Invoke-ProjectRun([string]$project, [string]$name, [string[]]$commands) {
        $scriptFile = Join-Path $switchRoot "$name.txt"
        $resultFile = Join-Path $switchRoot "$name.jsonl"
        $all = @('wait 240') + $commands + @('quit')
        Set-Content -LiteralPath $scriptFile -Encoding UTF8 -Value $all
        $priorWorkspace = $env:CREATOR_EDITOR_WORKSPACE_DIR
        $priorLegacy = $env:CREATOR_EDITOR_LEGACY_INI
        # ★ 세 회차가 **같은** 작업 공간을 쓴다 — 그것이 새는 통로 하나다.
        $env:CREATOR_EDITOR_WORKSPACE_DIR = $switchWorkspace
        $env:CREATOR_EDITOR_LEGACY_INI = Join-Path $switchWorkspace 'legacy.ini'
        try {
            $proc = Start-Process -FilePath $Exe -WorkingDirectory (Split-Path $Exe) -PassThru -WindowStyle Hidden `
                -ArgumentList @('--development-project', ('"' + $project + '"'), '--script', ('"' + $scriptFile + '"'),
                                '--result-format', 'jsonl', '--result-file', ('"' + $resultFile + '"')) `
                -RedirectStandardOutput (Join-Path $switchRoot "$name.out") -RedirectStandardError (Join-Path $switchRoot "$name.err")
            if (-not $proc.WaitForExit(600000)) { $proc.Kill(); throw "project run $name did not exit" }
        }
        finally {
            if ($null -ne $priorWorkspace) { $env:CREATOR_EDITOR_WORKSPACE_DIR = $priorWorkspace }
            else { Remove-Item Env:CREATOR_EDITOR_WORKSPACE_DIR -ErrorAction SilentlyContinue }
            if ($null -ne $priorLegacy) { $env:CREATOR_EDITOR_LEGACY_INI = $priorLegacy }
            else { Remove-Item Env:CREATOR_EDITOR_LEGACY_INI -ErrorAction SilentlyContinue }
        }
        Assert ($proc.ExitCode -eq 0) "17 $name 에디터 종료 코드가 $($proc.ExitCode) 다"
        if (-not (Test-Path -LiteralPath $resultFile)) { throw "project run $name wrote no result file" }
        $rows = @(Get-Content -LiteralPath $resultFile | Where-Object { $_.Trim() } | ForEach-Object { $_ | ConvertFrom-Json })
        Assert ($rows.Count -eq $all.Count) "17 $name 결과 줄 수 $($rows.Count) 가 스크립트 줄 수 $($all.Count) 와 다르다"
        $snapshots = @()
        for ($k = 0; $k -lt [Math]::Min($rows.Count, $all.Count); $k++) {
            if ($all[$k] -eq 'editor.browser') { $snapshots += $rows[$k].data }
        }
        return ,$snapshots
    }

    try {
        # A — 최근 항목을 만들고, 이력을 늘리고, 선택한 채 끝낸다.
        $a1 = Invoke-ProjectRun $projectA 'a1' @(
            'editor.browser go VolumeProfile', 'wait 5',
            'editor.browser create volume Keep', 'wait 5',
            'editor.browser go OnlyA', 'wait 5',
            'editor.browser')
        Assert ($a1.Count -eq 1) "17 a1 표본이 1 개가 아니다($($a1.Count))"
        if ($a1.Count -eq 1) {
            Assert ([int]$a1[0].recentCount -eq 1 -and [int]$a1[0].historySize -eq 3 -and $a1[0].directory -eq 'OnlyA') `
                "17 a1 이 자극을 만들지 못했다(recent=$($a1[0].recentCount) hist=$($a1[0].historySize) dir='$($a1[0].directory)')"
        }
        Assert (Test-Path -LiteralPath $recentsA) "17 A 의 최근 항목 파일이 생기지 않았다 — 비교할 것이 없다"
        $recentsABytes = if (Test-Path -LiteralPath $recentsA) { [Convert]::ToBase64String([IO.File]::ReadAllBytes($recentsA)) } else { '' }

        # B 에 같은 상대 경로의 파일을 둔다(메타는 없다 — B 에서 고른 적이 없는 파일이다).
        Copy-Item -LiteralPath (Join-Path $projectA 'Assets\VolumeProfile\Keep.volume') -Destination (Join-Path $projectB 'Assets\VolumeProfile\Keep.volume')

        $b1 = Invoke-ProjectRun $projectB 'b1' @(
            'editor.browser',
            'editor.browser go @recent', 'wait 10',
            'editor.browser')
        Assert ($b1.Count -eq 2) "17 b1 표본이 2 개가 아니다($($b1.Count))"
        if ($b1.Count -eq 2) {
            $first = $b1[0]
            Assert ($first.scope -eq 'folder' -and $first.directory -eq '.') "17 B 가 뿌리에서 시작하지 않았다($($first.scope) '$($first.directory)')"
            Assert ([int]$first.historySize -eq 1 -and [int]$first.historyIndex -eq 0) "17 B 에 A 의 이력이 남았다($($first.historyIndex)/$($first.historySize) [$(@($first.history) -join ', ')])"
            Assert ($first.selected -eq '') "17 B 에 A 의 선택이 남았다('$($first.selected)')"
            Assert ([int]$first.recentCount -eq 0) "17 B 에 A 의 최근 항목이 보인다(recent=$($first.recentCount))"
            $firstResults = @($first.results)
            Assert (($firstResults -contains 'OnlyB') -and -not ($firstResults -contains 'OnlyA')) `
                "17 B 의 결과가 B 의 폴더가 아니다([$($firstResults -join ' | ')])"
            Assert ($b1[1].scope -eq 'recent' -and [int]$b1[1].resultCount -eq 0) `
                "17 B 의 최근 항목에 결과가 있다($($b1[1].resultCount) [$(@($b1[1].results) -join ' | ')])"
        }
        $recentsAAfterB = if (Test-Path -LiteralPath $recentsA) { [Convert]::ToBase64String([IO.File]::ReadAllBytes($recentsA)) } else { '' }
        Assert ($recentsABytes -eq $recentsAAfterB) "17 B 회차가 A 의 최근 항목 파일을 바꿨다"
        $bText = if (Test-Path -LiteralPath $recentsB) { [IO.File]::ReadAllText($recentsB) } else { '' }
        Assert ($bText.Trim() -eq '') "17 B 의 최근 항목 파일에 내용이 생겼다('$($bText.Trim())')"

        $a2 = Invoke-ProjectRun $projectA 'a2' @('editor.browser')
        Assert ($a2.Count -eq 1) "17 a2 표본이 1 개가 아니다($($a2.Count))"
        if ($a2.Count -eq 1) {
            Assert ([int]$a2[0].recentCount -eq 1) "17 A 로 돌아왔는데 최근 항목이 $($a2[0].recentCount) 개다 — 1 이어야 한다"
            Assert ($a2[0].directory -eq '.' -and [int]$a2[0].historySize -eq 1 -and $a2[0].selected -eq '') `
                "17 A 의 다음 세션이 뿌리·빈 이력·빈 선택으로 시작하지 않았다('$($a2[0].directory)' $($a2[0].historySize) '$($a2[0].selected)')"
        }
        $switchSummary = "전환 회차 3"
    }
    finally {
        Remove-Item -LiteralPath $switchRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}

"Content Browser 탐색 검사: 단정 $checks · $runtimeSummary · $switchSummary · 받는 자리 $($sites.Count)"
if ($failures.Count -gt 0) {
    $failures | ForEach-Object { "  FAIL $_" }
    throw "Content Browser 탐색 검사 실패 $($failures.Count) 건"
}
"PASS"
