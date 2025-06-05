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
        // ����ڰ� Ȯ���ڸ� �Է����� ���� ���, ���õ� ������ Ȯ���ڸ� �߰�
        if (wcsrchr(filename, L'.') == nullptr)
        {
            // ���õ� ������ Ȯ���� ��������
            LPCWCHAR filter = filterString;
            int filterIndex = ofn.nFilterIndex - 1; // nFilterIndex�� 1���� ����
            while (filterIndex > 0 && *filter != L'\0')
            {
                filter += wcslen(filter) + 1; // ���� ���ͷ� �̵�
                filter += wcslen(filter) + 1; // Ȯ���� �κ����� �̵�
                --filterIndex;
            }

            if (*filter != L'\0')
            {
                // Ȯ���� �κп��� ù ��° Ȯ���� ����
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

    return L""; // ��� �Ǵ� ����
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
        // ����ڰ� Ȯ���ڸ� �Է����� ���� ���, ���õ� ������ Ȯ���ڸ� �߰�
        if (wcsrchr(filename, L'.') == nullptr)
        {
            // ���õ� ������ Ȯ���� ��������
            LPCWCHAR filter = filterString;
            int filterIndex = ofn.nFilterIndex - 1; // nFilterIndex�� 1���� ����
            while (filterIndex > 0 && *filter != L'\0')
            {
                filter += wcslen(filter) + 1; // ���� ���ͷ� �̵�
                filter += wcslen(filter) + 1; // Ȯ���� �κ����� �̵�
                --filterIndex;
            }

            if (*filter != L'\0')
            {
                // Ȯ���� �κп��� ù ��° Ȯ���� ����
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

    return L""; // ��� �Ǵ� ����
}