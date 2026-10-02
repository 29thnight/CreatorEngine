#pragma once

// The host selects restoration before requesting simulation. Player is the default.
class SimulationSessionPolicy final
{
  public:
    enum class mode
    {
        runtime,
        editor_restore
    };

    explicit SimulationSessionPolicy(mode value = mode::runtime) noexcept : m_mode(value) {}

    bool RestoresAuthoring() const noexcept { return m_mode == mode::editor_restore; }

    template<class Capture, class Start, class Discard>
    bool Begin(Capture&& capture, Start&& start, Discard&& discard) const
    {
        if (RestoresAuthoring() && !capture())
            return false;

        try
        {
            if (start())
                return true;
        }
        catch (...)
        {
            if (RestoresAuthoring())
                discard();
            throw;
        }

        if (RestoresAuthoring())
            discard();
        return false;
    }

    template<class Stop, class Restore, class Discard>
    bool End(Stop&& stop, Restore&& restore, Discard&& discard) const
    {
        if (!stop())
            return false;
        if (RestoresAuthoring())
        {
            if (!restore())
                return false; // Keep the pre-Play document for retry.
            discard();
        }

        return true;
    }

  private:
    mode m_mode;
};
