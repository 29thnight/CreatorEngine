import std;
import Creator.Concurrency.JobGraph;

job_graph_diagnostics* echo_graph_type(job_graph_diagnostics* value)
{
    return value;
}
std::uint64_t graph_clock_from_other_unit()
{
    return job_timestamp_ns();
}
