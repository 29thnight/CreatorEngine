namespace CreatorEngine.Scripts

open CreatorEngine

type HierarchyProbe() =
    inherit Component()

    let mutable passed = 0
    let mutable failed = 0

    member private _.Assert(name, valid, detail) =
        if valid then
            passed <- passed + 1
        else
            failed <- failed + 1
            Component.LogError($"[HierarchyProbe] 실패: {name} — {detail}")

    member this.OnInitialized() : unit =
        let owner = this.Entity
        let childCount = owner.ChildCount
        Component.Log($"[HierarchyProbe] {owner.Name} (index={owner.Index}) 자식 {childCount}개")
        this.Assert("ChildCount >= 0", childCount >= 0, string childCount)
        this.Assert("Index >= 0", owner.Index >= 0, string owner.Index)
        let mutable walked = 0

        for child in owner.Children do
            walked <- walked + 1
            Component.Log($"[HierarchyProbe]   자식 {child.Name} (index={child.Index})")
            this.Assert($"'{child.Name}'의 부모가 자신", child.Parent = owner, $"{child.Parent.Name} != {owner.Name}")

        this.Assert("Children 순회 개수 일치", (walked = childCount), $"{walked} vs {childCount}")
        this.Assert("범위 밖 자식은 무효", not (owner.GetChild(childCount + 10).IsAlive), "살아 있다고 나옴")
        this.Assert("FindByIndex 왕복", Entity.FindByIndex(owner.Index) = owner, "다른 오브젝트가 나옴")
        let withSelf = owner.GetComponentsInChildren<HierarchyProbe>()
        let withoutSelf = owner.GetComponentsInChildren<HierarchyProbe>(includeSelf = false)
        Component.Log($"[HierarchyProbe] 자손 HierarchyProbe — 자신 포함 {withSelf.Count}개 / 제외 {withoutSelf.Count}개")

        this.Assert(
            "GetComponentsInChildren이 자신을 포함",
            withSelf.Count = withoutSelf.Count + 1,
            $"{withSelf.Count} vs {withoutSelf.Count}"
        )

        this.Assert(
            "GetComponentInParent가 자신을 찾음",
            not (obj.ReferenceEquals(owner.GetComponentInParent<HierarchyProbe>(), null)),
            "null이 나옴"
        )

        if failed = 0 then
            Component.Log($"[HierarchyProbe] 전체 통과 ({passed}건)")
        else
            Component.LogError($"[HierarchyProbe] {failed}건 실패 / {passed}건 통과")
