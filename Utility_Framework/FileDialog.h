#pragma once
#include <commdlg.h>

#pragma comment(lib, "comdlg32.lib")

inline std::wstring ShowOpenFileDialog(LPCWCHAR filterString, LPCWCHAR title = L"Open File", const std::wstring& initialDirectory = L"")
{
    wchar_t filename[MAX_PATH] = L"";
    wchar_t defaultExt[MAX_PATH] = L"";

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFilter = filterString;
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_FILEMUSTEXIST | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = title;

    if (!initialDirectory.empty())
    {
        ofn.lpstrInitialDir = initialDirectory.c_str();
    }

    if (GetOpenFileNameW(&ofn))
    {
        // 사용자가 확장자를 입력하지 않은 경우, 선택된 필터의 확장자를 추가
        if (wcsrchr(filename, L'.') == nullptr)
        {
            // 선택된 필터의 확장자 가져오기
            LPCWCHAR filter = filterString;
            int filterIndex = ofn.nFilterIndex - 1; // nFilterIndex는 1부터 시작
            while (filterIndex > 0 && *filter != L'\0')
            {
                filter += wcslen(filter) + 1; // 다음 필터로 이동
                filter += wcslen(filter) + 1; // 확장자 부분으로 이동
                --filterIndex;
            }

            if (*filter != L'\0')
            {
                // 확장자 부분에서 첫 번째 확장자 추출
                LPCWCHAR extStart = wcsrchr(filter, L'*');
                if (extStart && *(extStart + 1) == L'.')
                {
                    wcscpy_s(defaultExt, extStart + 1);
                    wcscat_s(filename, L".");
                    wcscat_s(filename, defaultExt);
                }
            }
        }

        return filename;
    }

    return L""; // 취소 또는 실패
}

inline std::wstring ShowSaveFileDialog(LPCWCHAR filterString, LPCWCHAR title = L"Save File", const std::wstring& initialDirectory = L"")
{
    wchar_t filename[MAX_PATH] = L"";
    wchar_t defaultExt[MAX_PATH] = L"";

    OPENFILENAMEW ofn = {};
    ofn.lStructSize = sizeof(ofn);
    ofn.hwndOwner = nullptr;
    ofn.lpstrFilter = filterString;
    ofn.lpstrFile = filename;
    ofn.nMaxFile = MAX_PATH;
    ofn.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;
    ofn.lpstrTitle = title;

    if (!initialDirectory.empty())
    {
        ofn.lpstrInitialDir = initialDirectory.c_str();
    }

    if (GetSaveFileNameW(&ofn))
    {
        // 개선된 조건: 확장자가 없거나 '.'로 끝나는 경우도 포함
        LPCWCHAR lastDot = wcsrchr(filename, L'.');
        if (lastDot == nullptr || *(lastDot + 1) == L'\0')
        {
            // 선택된 필터의 확장자 가져오기
            LPCWCHAR filter = filterString;
            int filterIndex = ofn.nFilterIndex - 1; // nFilterIndex는 1부터 시작
            while (filterIndex > 0 && *filter != L'\0')
            {
                filter += wcslen(filter) + 1;
                filter += wcslen(filter) + 1;
                --filterIndex;
            }

            if (*filter != L'\0')
            {
                LPCWCHAR extStart = wcsrchr(filter, L'*');
                if (extStart && *(extStart + 1) == L'.')
                {
                    wcscpy_s(defaultExt, extStart + 2);

                    size_t len = wcslen(defaultExt);
                    if (len > 0 && defaultExt[len - 1] == L')')
                    {
                        defaultExt[len - 1] = L'\0';
                    }

                    wcscat_s(filename, L".");
                    wcscat_s(filename, defaultExt);
                }
            }
        }

        return filename;
    }

    return L""; // 취소 또는 실패
}