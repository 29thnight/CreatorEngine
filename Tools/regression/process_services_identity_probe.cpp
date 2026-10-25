import std;
import Creator.Core.Progress;
import Creator.Core.EngineMode;

ce::progress::sink* sink_from_other_unit()
{
    return &ce::progress::get_sink();
}
EngineRunMode mode_from_other_unit()
{
    return EngineMode::Get();
}
void launch_from_other_unit()
{
    ce::progress::launch();
}
