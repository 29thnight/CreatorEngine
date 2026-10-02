// diag_include.cpp · diag_import.cpp 가 **같은 본문**을 컴파일하게 하는 조각.
//
// 지금 소비자가 가장 많이 쓰는 표면(ProfileScope 44 곳 · ProfileCaptureFile 14 곳 ·
// ProfileService 9 곳)을 건드린다. 표준 라이브러리 이름은 쓰지 않는다 — import
// 쪽이 STL 을 따로 열지 않아도 컴파일되어야 두 쪽이 같은 일을 한다.
int cpp_module_bench_diag_body()
{
	ce::profiler_service& service = ce::profiler();
	ce::profile_scope scope{ service, ce::marker<"CppModuleBench.Scope">() };
	ce::profile_instant(ce::marker<"CppModuleBench.Instant">());

	ce::profile_event event{};
	event.flags = ce::event_flags::instant | ce::event_flags::gpu_span;

	const ce::capture_session_ptr capture = service.capture();
	const int frames = capture ? static_cast<int>(capture->frames().size()) : 0;
	return frames + static_cast<int>(sizeof(event)) + static_cast<int>(ce::kCaptureFileVersion) +
	       (ce::has_flag(event.flags, ce::event_flags::instant) ? 1 : 0);
}
