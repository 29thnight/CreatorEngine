#pragma once
#include <windows.h>
#include <functional>
#include <unordered_map>
#include <directxtk/Keyboard.h>
#include <imgui_internal.h>
#include <shellapi.h> // 추가
#include "DumpHandler.h"
#include "Resource.h"

#include "imgui.h"
#include "imgui_impl_win32.h"
#include "imgui_impl_dx11.h"

#pragma warning(disable: 28251)
#define MAIN_ENTRY int WINAPI

class CoreWindow
{
public:
    using MessageHandler = std::function<LRESULT(HWND, WPARAM, LPARAM)>;

    CoreWindow(HINSTANCE hInstance, const wchar_t* title, int width, int height)
        : m_hInstance(hInstance), m_width(width), m_height(height)
    {
        RegisterWindowClass();
        CreateAppWindow(title);
        SetUnhandledExceptionFilter(ErrorDumpHandeler);
        s_instance = this;
    }

    ~CoreWindow()
    {
        if (m_hWnd)
        {
            DestroyWindow(m_hWnd);
        }
        UnregisterClass(L"CoreWindowApp", m_hInstance);
    }

    template <typename Instance>
    void RegisterHandler(UINT message, Instance* instance, LRESULT(Instance::* handler)(HWND, WPARAM, LPARAM))
    {
        m_handlers[message] = [=](HWND hWnd, WPARAM wParam, LPARAM lParam)
            {
                return (instance->*handler)(hWnd, wParam, lParam);
            };
    }

    template <typename Initializer>
    CoreWindow InitializeTask(Initializer fn_initializer)
    {
        fn_initializer();

        return *this;
    }

    template <typename MessageLoop>
    void Then(MessageLoop fn_messageLoop)
    {
        MSG msg = {};
        while (true)
        {
            if (PeekMessage(&msg, nullptr, 0, 0, PM_REMOVE))
            {
                if (msg.message == WM_QUIT)
                {
                    break;
                }
                else
                {
                    DispatchMessage(&msg);
                }
            }
            else
            {
                fn_messageLoop();
            }
        }
    }

    static LONG WINAPI ErrorDumpHandeler(EXCEPTION_POINTERS* pExceptionPointers)
    {
        int msgResult = MessageBox(NULL, L"Should Create Dump ?", L"Exception", MB_YESNO | MB_ICONQUESTION);

        if (msgResult == IDYES)
        {
            CreateDump(pExceptionPointers, g_dumpType);
        }

        return msgResult;
    }

    static void SetDumpType(DUMP_TYPE dumpType)
    {
        g_dumpType = dumpType;
    }

    HWND GetHandle() const { return m_hWnd; }
    int GetWidth() const { return m_width; }
    int GetHeight() const { return m_height; }

    static CoreWindow* GetForCurrentInstance()
    {
        return s_instance;
    }
    
	static void RegisterCreateEventHandler(MessageHandler handler)
	{
		m_CreateEventHandler = handler;
	}

private:
    static CoreWindow* s_instance;
    static DUMP_TYPE g_dumpType;
    HINSTANCE m_hInstance = nullptr;
    HWND m_hWnd = nullptr;
    int m_width = 800;
    int m_height = 600;
    std::unordered_map<UINT, MessageHandler> m_handlers;
	static MessageHandler m_CreateEventHandler;

    void RegisterWindowClass() const
    {
        WNDCLASS wc = {};
        wc.lpfnWndProc = CoreWindow::WndProc;
        wc.hInstance = m_hInstance;
        wc.lpszClassName = L"CoreWindowApp";
        wc.hIcon = LoadIcon(m_hInstance, MAKEINTRESOURCE(IDI_ACADEMY4Q));      // 큰 아이콘

        RegisterClass(&wc);
    }

    void CreateAppWindow(const wchar_t* title)
    {
        RECT rect{};
        GetWindowRect(GetDesktopWindow(), &rect);
        int x = (rect.right - rect.left - m_width) / 2;
        int y = (rect.bottom - rect.top - m_height) / 2;

        // 제목 표시줄 높이 가져오기
        int titleBarHeight = GetSystemMetrics(SM_CYCAPTION); // 제목 표시줄 높이
        int borderHeight = GetSystemMetrics(SM_CYFRAME);     // 상단 프레임 높이
        int borderWidth = GetSystemMetrics(SM_CXFRAME);      // 좌우 프레임 너비

        // 클라이언트 영역 조정
        rect = { 0, 0, m_width, m_height };
        AdjustWindowRect(&rect, WS_OVERLAPPEDWINDOW, FALSE);

        m_hWnd = CreateWindowEx(
            0,
            L"CoreWindowApp",
            title,
            WS_OVERLAPPEDWINDOW,
            x, y,
            rect.right - rect.left,
            rect.bottom - rect.top /*+ titleBarHeight + borderHeight*/,
            nullptr,
            nullptr,
            m_hInstance,
            this);

        if (m_hWnd)
        {
            DragAcceptFiles(m_hWnd, TRUE);

            ShowWindow(m_hWnd, SW_SHOWNORMAL);
            UpdateWindow(m_hWnd);
        }
    }

public:
    static LRESULT CALLBACK WndProc(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);

private:
    LRESULT HandleMessage(HWND hWnd, UINT message, WPARAM wParam, LPARAM lParam);
};
