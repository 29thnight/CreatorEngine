namespace CreatorEngine.Scripts

open CreatorEngine

type AniStateProbe() =
    inherit AniBehavior()
    let mutable updates = 0

    override this.Enter() =
        let owner = if this.IsAlive then this.Entity.Name else "(없음)"

        let position =
            if obj.ReferenceEquals(this.Transform, null) then
                ""
            else
                string this.Transform.WorldPosition

        AniBehavior.Log($"[AniStateProbe] Enter — 소유 오브젝트 '{owner}' pos={position} frame {AniBehavior.FrameCount}")

        if not this.IsAlive then
            AniBehavior.LogError("[AniStateProbe] 소유 오브젝트가 연결되지 않았습니다.")
        else
            AniBehavior.Log(
                $"[AniStateProbe] Animator 있음={not (obj.ReferenceEquals(this.GetComponent<Animator>(), null))}"
            )

    override _.Update(dt) =
        updates <- updates + 1

        if updates <= 2 || updates % 60 = 0 then
            let delta = dt.ToString("0.####")
            AniBehavior.Log($"[AniStateProbe] Update #{updates} (tick={delta}) frame {AniBehavior.FrameCount}")

    override _.Exit() =
        AniBehavior.Log($"[AniStateProbe] Exit — Update {updates}회 받음")
