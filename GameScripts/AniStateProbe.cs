namespace CreatorEngine.Scripts;

/// <summary>
/// 애니메이션 상태 스크립트 바인딩 검증.
///
/// 상태에 진입하면 Enter, 유지되는 동안 Update, 빠져나갈 때 Exit가 와야 한다.
/// 소유 오브젝트가 제대로 연결되는지도 함께 본다 — 상태 생성 시점에는 컨트롤러가
/// 아직 붙어 있지 않아 이벤트마다 실려 오는 값이다.
/// </summary>
public sealed class AniStateProbe : AniBehaviour
{
    private int _updateCount;

    public override void Enter()
    {
        Log($"[AniStateProbe] Enter — 소유 오브젝트 '{(IsAlive ? Entity.Name : "(없음)")}' " +
            $"pos={Transform.WorldPosition} frame {FrameCount}");

        if (!IsAlive)
        {
            LogError("[AniStateProbe] 소유 오브젝트가 연결되지 않았습니다.");
            return;
        }

        // 같은 오브젝트의 다른 컴포넌트에 닿는지 확인한다.
        Log($"[AniStateProbe] Animator 있음={GetComponent<Animator>() is not null}");
    }

    public override void Update(float tick)
    {
        // 매 프레임 찍으면 로그가 폭주하므로 처음 몇 번과 이후 주기만 남긴다.
        ++_updateCount;
        if (_updateCount <= 2 || _updateCount % 60 == 0)
        {
            Log($"[AniStateProbe] Update #{_updateCount} (tick={tick:0.####}) frame {FrameCount}");
        }
    }

    public override void Exit()
    {
        Log($"[AniStateProbe] Exit — Update {_updateCount}회 받음");
    }
}

