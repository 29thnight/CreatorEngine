export module Creator.Core.EngineMode;

import std;
import Creator.Contracts;

export enum class EngineRunMode : std::uint8_t
{
    Unset = 0,
    Editor,
    Player,
};

export class EngineMode final
{
  public:
    static void Set(EngineRunMode mode)
    {
        ::ce::contracts::assert_that(
            [&]() -> bool
            { return static_cast<bool>((EngineRunMode::Unset != mode && "EngineMode: Unset으로 되돌릴 수 없다")); },
            "EngineRunMode::Unset != mode && \"EngineMode: Unset\uc73c\ub85c \ub418\ub3cc\ub9b4 \uc218 \uc5c6\ub2e4\"");
        ::ce::contracts::assert_that(
            [&]() -> bool
            {
                return static_cast<bool>(
                    ((EngineRunMode::Unset == s_mode || mode == s_mode) && "EngineMode: 부팅 후 모드 변경은 금지다"));
            },
            "(EngineRunMode::Unset == s_mode || mode == s_mode) && \"EngineMode: \ubd80\ud305 \ud6c4 \ubaa8\ub4dc "
            "\ubcc0\uacbd\uc740 \uae08\uc9c0\ub2e4\"");
        s_mode = mode;
    }

    static EngineRunMode Get()
    {
        ::ce::contracts::assert_that(
            [&]() -> bool
            {
                return static_cast<bool>((EngineRunMode::Unset != s_mode &&
                                          "EngineMode: 진입점이 아직 모드를 정하지 않았다 (EngineBootstrap::Run)"));
            },
            "EngineRunMode::Unset != s_mode && \"EngineMode: \uc9c4\uc785\uc810\uc774 \uc544\uc9c1 \ubaa8\ub4dc\ub97c "
            "\uc815\ud558\uc9c0 \uc54a\uc558\ub2e4 (EngineBootstrap::Run)\"");
        return s_mode;
    }

    static bool IsEditor() { return EngineRunMode::Editor == Get(); }
    static bool IsPlayer() { return EngineRunMode::Player == Get(); }

  private:
    EngineMode() = delete;

    static inline EngineRunMode s_mode = EngineRunMode::Unset;
};
