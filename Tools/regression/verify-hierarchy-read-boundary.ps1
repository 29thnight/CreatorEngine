# H3: Entity에는 계층 복사본이 없고 HierarchyStore가 유일 정본이어야 한다.
#
# 동적 hierarchycheck는 topology와 Store 슬롯 점유를 검증한다. 이 정적 경계는
# 계층 멤버/리플렉션 필드와 shadow 동기화의 재유입을 별도로 막는다.

$repoRoot = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$failures = @()

function Mask-CppNonCode {
    param([string]$Text)

    $chars = $Text.ToCharArray()
    $masked = New-Object char[] $chars.Length
    $state = 'code'
    $escaped = $false

    for ($i = 0; $i -lt $chars.Length; $i++) {
        $ch = $chars[$i]
        $next = if ($i + 1 -lt $chars.Length) { $chars[$i + 1] } else { [char]0 }

        if ($ch -eq "`r" -or $ch -eq "`n") {
            $masked[$i] = $ch
            if ($state -eq 'line') { $state = 'code' }
            if ($state -eq 'string' -or $state -eq 'char') { $escaped = $false }
            continue
        }

        switch ($state) {
            'code' {
                if ($ch -eq '/' -and $next -eq '/') {
                    $masked[$i] = ' '; $masked[$i + 1] = ' '; $i++; $state = 'line'
                }
                elseif ($ch -eq '/' -and $next -eq '*') {
                    $masked[$i] = ' '; $masked[$i + 1] = ' '; $i++; $state = 'block'
                }
                elseif ($ch -eq '"') { $masked[$i] = ' '; $state = 'string'; $escaped = $false }
                elseif ($ch -eq "'") { $masked[$i] = ' '; $state = 'char'; $escaped = $false }
                else { $masked[$i] = $ch }
            }
            'line' { $masked[$i] = ' ' }
            'block' {
                $masked[$i] = ' '
                if ($ch -eq '*' -and $next -eq '/') {
                    $masked[$i + 1] = ' '; $i++; $state = 'code'
                }
            }
            'string' {
                $masked[$i] = ' '
                if ($escaped) { $escaped = $false }
                elseif ($ch -eq '\') { $escaped = $true }
                elseif ($ch -eq '"') { $state = 'code' }
            }
            'char' {
                $masked[$i] = ' '
                if ($escaped) { $escaped = $false }
                elseif ($ch -eq '\') { $escaped = $true }
                elseif ($ch -eq "'") { $state = 'code' }
            }
        }
    }

    return -join $masked
}

# ModelNode/Bone의 동명 필드는 Entity와 무관하므로 표현식 단위로만 허용한다.
#
# 스트림 변수 이름은 \w+ 로 둔다. 이 게이트가 지키려는 성질은 "어떤 필드를 만지는가"이지
# "지역 변수 이름이 무엇인가"가 아닌데, 이름을 규칙에 박아 두었다가 실제로 낡았다 —
# 76be6ff1이 저작 writer를 Editor로 옮기며 ofstream& outfile 을 ostream& output 으로
# 바꾸자 허용 2건이 조용히 위반으로 뒤집혔고, 그 뒤로 세트가 붉은 채 지나갔다.
# 필드 이름과 sizeof 대상은 그대로 못 박아 두므로 엄격함은 잃지 않는다.
$allowed = @{
    'Engine\RenderEngine\ModelLoader.cpp' = @(
        '^\s*nodeObj->m_parentIndex = parentIndex;\s*$',
        '^\s*\w+\.write\(reinterpret_cast<(?:const )?char\*>\(&node->m_parentIndex\), sizeof\(node->m_parentIndex\)\);\s*$',
        '^\s*bone->m_parentIndex = parent;\s*$',
        '^\s*\w+\.write\(reinterpret_cast<(?:const )?char\*>\(&bone->m_parentIndex\), sizeof\(bone->m_parentIndex\)\);\s*$',
        '^\s*\w+\.read\(reinterpret_cast<(?:const )?char\*>\(&node->m_parentIndex\), sizeof\(node->m_parentIndex\)\);\s*$',
        '^\s*\w+\.read\(reinterpret_cast<(?:const )?char\*>\(&bone->m_parentIndex\), sizeof\(bone->m_parentIndex\)\);\s*$',
        '^\s*if \(bone->m_parentIndex >= 0 && bone->m_parentIndex < static_cast<int>\(boneCount\)\)\s*$',
        '^\s*skeleton->m_bones\[bone->m_parentIndex\]->m_children\.push_back\(bone\);\s*$'
    )
}

$entityHeaderPath = Join-Path $repoRoot 'Engine\ScriptBinder\Entity.h'
$entityHeader = Mask-CppNonCode (Get-Content $entityHeaderPath -Raw -Encoding UTF8)
foreach ($field in @('m_parentIndex', 'm_rootIndex', 'm_childrenIndices')) {
    if ($entityHeader -match "\b$field\b") {
        $failures += "Entity.h에 제거된 계층 필드가 다시 등장함: $field"
    }
}

$hierarchyStoreHeader = Mask-CppNonCode (
    Get-Content (Join-Path $repoRoot 'Engine\ScriptBinder\HierarchyStore.h') -Raw -Encoding UTF8)
if ($hierarchyStoreHeader -match '\b(?:SyncSlot|Matches)\s*\(') {
    $failures += 'HierarchyStore에 H1 shadow 동기화 API(SyncSlot/Matches)가 다시 등장함'
}

$sceneSources = (Get-Content (Join-Path $repoRoot 'Engine\ScriptBinder\Scene.h') -Raw -Encoding UTF8) +
    (Get-Content (Join-Path $repoRoot 'Engine\ScriptBinder\Scene.cpp') -Raw -Encoding UTF8)
if ((Mask-CppNonCode $sceneSources) -match '\bSyncEntityHierarchy\b') {
    $failures += 'Scene에 H1 SyncEntityHierarchy가 다시 등장함'
}

$sourceRoots = @('EngineEntry', 'EngineGUIWindow', 'RenderEngine', 'ScriptBinder', 'Dynamic_CPP') |
    ForEach-Object { Join-Path $repoRoot $_ } |
    Where-Object { Test-Path $_ }
$sourceFiles = Get-ChildItem $sourceRoots -Recurse -File -Include *.h,*.hpp,*.cpp,*.inl |
    Where-Object {
        $_.FullName -notmatch '\\(\.git|ThirdParty|vcpkg_installed|x64|Bin|Generated|packages)\\'
    }
$directPattern = '(?:->|\.)\s*m_(?:parentIndex|childrenIndices|rootIndex)\b'

foreach ($file in $sourceFiles) {
    $relative = [System.IO.Path]::GetRelativePath($repoRoot, $file.FullName)
    $maskedLines = (Mask-CppNonCode (Get-Content $file.FullName -Raw -Encoding UTF8)) -split "`r?`n"

    for ($i = 0; $i -lt $maskedLines.Count; $i++) {
        $line = $maskedLines[$i]
        if ($line -notmatch $directPattern) { continue }

        $isAllowed = $false
        if ($allowed.ContainsKey($relative)) {
            foreach ($rule in $allowed[$relative]) {
                if ($line -match $rule) { $isAllowed = $true; break }
            }
        }
        if (-not $isAllowed) {
            $failures += "$relative`:$($i + 1): $($line.Trim())"
        }
    }
}

$entitySource = Get-Content (Join-Path $repoRoot 'Engine\ScriptBinder\Entity.cpp') -Raw -Encoding UTF8
$accessorRules = @(
    @('GetParentIndex', 'ParentOf'),
    @('GetRootIndex', 'RootOf'),
    @('GetChildrenIndices', 'ChildrenOf')
)
foreach ($rule in $accessorRules) {
    $method = [regex]::Escape($rule[0])
    $storeRead = [regex]::Escape($rule[1])
    $pattern = "Entity::$method\(\) const[\s\S]*?GetEntityRaw\(m_index\) == this[\s\S]*?IsOccupied\(slot\)[\s\S]*?$storeRead\(slot\)"
    if ($entitySource -notmatch $pattern) {
        $failures += "Entity::$($rule[0])가 점유자 검증 후 HierarchyStore::$($rule[1])를 읽지 않는다"
    }
}

$writerRules = @(
    @('SetParentIndex', 'SetParent'),
    @('AttachChildIndex', 'AttachChild'),
    @('DetachChildIndex', 'DetachChild'),
    @('ClearChildren', 'ClearChildren'),
    @('SetChildrenIndices', 'SetChildren'),
    @('SetRootIndex', 'SetRoot')
)
foreach ($rule in $writerRules) {
    $method = [regex]::Escape($rule[0])
    $storeWrite = [regex]::Escape($rule[1])
    $pattern = "Entity::$method\([^)]*\)[\s\S]*?GetEntityRaw\(m_index\) == this[\s\S]*?m_hierarchyStore\.$storeWrite\("
    if ($entitySource -notmatch $pattern) {
        $failures += "Entity::$($rule[0])가 점유자 검증 후 HierarchyStore::$($rule[1])를 쓰지 않는다"
    }
}

$sceneSource = Get-Content (Join-Path $repoRoot 'Engine\ScriptBinder\Scene.cpp') -Raw -Encoding UTF8
if ($sceneSource -notmatch 'SerializeEntityHierarchy[\s\S]*?ParentOf\(index\)[\s\S]*?RootOf\(index\)[\s\S]*?ChildrenOf\(index\)') {
    $failures += 'Scene 저장 어댑터가 Store의 parent/root/children을 모두 읽지 않는다'
}

$loaderSource = Mask-CppNonCode (
    Get-Content (Join-Path $repoRoot 'Engine\ScriptBinder\SceneManager.cpp') -Raw -Encoding UTF8)
foreach ($field in @('fileParentIndex', 'fileRootIndex', 'fileChildrenIndices')) {
    if ($loaderSource -notmatch "entry\.$field") {
        $failures += "로드 리맵이 DTO $field 값을 사용하지 않는다"
    }
}

if ($failures.Count -gt 0) {
    "H3 계층 단일 정본 경계 실패 $($failures.Count)건:"
    $failures | ForEach-Object { "  $_" }
    exit 1
}

"전체 통과 — Entity 계층 필드/shadow 0건, Store 읽기·쓰기·직렬화·로드 DTO 경계 고정"
exit 0
