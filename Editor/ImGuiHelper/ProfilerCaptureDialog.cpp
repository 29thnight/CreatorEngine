// PHASE 14 P6-3 — `.ceprof` 파일 대화상자.
//
// ★ Windows 헤더를 **이 한 파일에** 가둔다. FileDialog.h 는 <commdlg.h> 를
//   들이고 그것이 <windows.h> 를 요구하는데, windows.h 의 min/max 매크로가
//   유니티 blob 을 타고 옆 파일(프레임 개요·타임라인)의 std::min/std::max 를
//   깨뜨린다. 그래서 이 파일은 blob 에서 뺐다(vcxproj 의 IncludeInUnityFile).
//
// ★ 제목과 필터는 ASCII 다. Win32 대화상자로 넘어가는 넓은 문자열이 소스
//   인코딩을 한 번 더 타지 않게 한다.
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <string>   // FileDialog.h 가 std::wstring 을 쓰면서 스스로 들이지 않는다

#include "FileDialog.h"
#include "ProfilerView.h"

namespace editor::profiler_view
{
	std::filesystem::path pick_capture_to_open()
	{
		constexpr wchar_t filter[] = L"Creator Profiler Capture (*.ceprof)\0*.ceprof\0";
		return std::filesystem::path(ShowOpenFileDialog(filter, L"Open Profiler Capture"));
	}

	std::filesystem::path pick_capture_to_save()
	{
		constexpr wchar_t filter[] = L"Creator Profiler Capture (*.ceprof)\0*.ceprof\0";
		return std::filesystem::path(ShowSaveFileDialog(filter, L"Save Profiler Capture"));
	}
}
