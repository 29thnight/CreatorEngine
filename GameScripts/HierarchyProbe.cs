namespace CreatorEngine.Scripts;

/// <summary>계층 접근(자식·부모·인덱스) 검증.</summary>
public sealed partial class HierarchyProbe : Component
{
    private int _passed;
    private int _failed;

    public override void OnInitialized()
    {
        int childCount = Entity.ChildCount;
        Log($"[HierarchyProbe] {Entity.Name} (index={Entity.Index}) 자식 {childCount}개");

        Assert("ChildCount >= 0", childCount >= 0, $"{childCount}");
        Assert("Index >= 0", Entity.Index >= 0, $"{Entity.Index}");

        // Children 순회 개수가 ChildCount와 맞아야 한다.
        int walked = 0;
        foreach (Entity child in Entity.Children)
        {
            ++walked;
            Log($"[HierarchyProbe]   자식 {child.Name} (index={child.Index})");

            // 자식의 부모는 다시 나여야 한다 — 핸들 왕복 검증.
            Assert($"'{child.Name}'의 부모가 자신", child.Parent == Entity,
                $"{child.Parent.Name} != {Entity.Name}");
        }
        Assert("Children 순회 개수 일치", walked == childCount, $"{walked} vs {childCount}");

        // 범위를 벗어난 인덱스는 죽지 않고 무효 핸들을 준다.
        Assert("범위 밖 자식은 무효", !Entity.GetChild(childCount + 10).IsAlive, "살아 있다고 나옴");

        // 인덱스로 되찾기.
        Assert("FindByIndex 왕복", Entity.FindByIndex(Entity.Index) == Entity, "다른 오브젝트가 나옴");

        // 자손 탐색. 자기 자신 포함 여부에 따라 결과가 달라야 한다.
        var withSelf = Entity.GetComponentsInChildren<HierarchyProbe>();
        var withoutSelf = Entity.GetComponentsInChildren<HierarchyProbe>(includeSelf: false);
        Log($"[HierarchyProbe] 자손 HierarchyProbe — 자신 포함 {withSelf.Count}개 / 제외 {withoutSelf.Count}개");
        Assert("GetComponentsInChildren이 자신을 포함", withSelf.Count == withoutSelf.Count + 1,
            $"{withSelf.Count} vs {withoutSelf.Count}");

        // 조상 탐색은 자기 자신부터 본다.
        Assert("GetComponentInParent가 자신을 찾음",
            Entity.GetComponentInParent<HierarchyProbe>() is not null, "null이 나옴");

        if (_failed == 0) Log($"[HierarchyProbe] 전체 통과 ({_passed}건)");
        else LogError($"[HierarchyProbe] {_failed}건 실패 / {_passed}건 통과");
    }

    private void Assert(string name, bool ok, string detail)
    {
        if (ok) { ++_passed; return; }

        ++_failed;
        LogError($"[HierarchyProbe] 실패: {name} — {detail}");
    }
}
