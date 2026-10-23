namespace CreatorEngine.Scripts

open CreatorEngine

type SoundProbe() as this =
    inherit Component()

    let mutable sound = Unchecked.defaultof<SoundComponent>
    let mutable frame = 0

    do
        this._clipKey <- ""
        this._volume <- 0.5f
        this._pitch <- 1.25f
        this._reportInterval <- 60

    [<DefaultValue; SerializeField>]
    val mutable public _clipKey: string

    [<DefaultValue; SerializeField>]
    val mutable public _volume: float32

    [<DefaultValue; SerializeField>]
    val mutable public _pitch: float32

    [<DefaultValue; SerializeField>]
    val mutable public _reportInterval: int

    member private this.ConfigurePlayback() =
        if this._clipKey.Length > 0 then
            sound.ClipKey <- this._clipKey

        sound.Volume <- this._volume
        sound.Pitch <- this._pitch

        Component.Log($"[SoundProbe] 설정 후 clipKey='{sound.ClipKey}' volume={sound.Volume} pitch={sound.Pitch}")

        let valuesMatch =
            (this._clipKey.Length = 0 || sound.ClipKey = this._clipKey)
            && sound.Volume = this._volume
            && sound.Pitch = this._pitch

        if valuesMatch then
            Component.Log("[SoundProbe] 왕복 검증 통과")
        else
            Component.LogError("[SoundProbe] 왕복 검증 실패 — 쓴 값과 읽은 값이 다릅니다.")

    member this.OnInitialized() : unit =
        sound <- this.GetComponent<SoundComponent>()

        if obj.ReferenceEquals(sound, null) then
            Component.LogWarning("[SoundProbe] SoundComponent가 없습니다 — component.add로 먼저 붙여 주세요.")
        else
            Component.Log($"[SoundProbe] 래퍼 획득. 현재 clipKey='{sound.ClipKey}' volume={sound.Volume} pitch={sound.Pitch}")
            this.ConfigurePlayback()

            if sound.ClipKey.Length = 0 then
                Component.LogWarning("[SoundProbe] clipKey가 비어 있어 재생은 건너뜁니다(네이티브가 조용히 무시함).")
            else
                sound.Play()
                Component.Log($"[SoundProbe] Play 직후 IsPlaying={sound.IsPlaying()}")

    member this.PostPhysics(_: single) : unit =
        if not (obj.ReferenceEquals(sound, null)) && this._reportInterval > 0 then
            frame <- frame + 1

            if frame % this._reportInterval = 0 then
                Component.Log($"[SoundProbe] frame {Component.FrameCount} — IsPlaying={sound.IsPlaying()}")

    member this.OnUninitializing() : unit =
        if not (obj.ReferenceEquals(sound, null)) && this.Entity.IsAlive then
            sound.Stop()
