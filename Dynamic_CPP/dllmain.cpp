// dllmain.cpp : DLL 애플리케이션의 진입점을 정의합니다.
#include "pch.h"
#include "Export.h"
#include "funcMain.h"
#include "GlobalImGuiContext.h"

BOOL APIENTRY DllMain( HMODULE hModule,
                       DWORD  ul_reason_for_call,
                       LPVOID lpReserved
                     )
{
    switch (ul_reason_for_call)
    {
    case DLL_PROCESS_ATTACH:
    {
        InitActionFactory();
        InitConditionFactory();
        InitConditionDecoratorFactory();
        InitAniBehaviorFactory();
        InitModuleFactory();
        ImGui::SetCurrentContext(GlobalImGuiContext::GetInstance()->GetContext());

        ImGui::SetAllocatorFunctions(
            GlobalImGuiContext::GetInstance()->p_alloc_func,
            GlobalImGuiContext::GetInstance()->p_free_func,
            GlobalImGuiContext::GetInstance()->p_user_data
        );
        break;
    }
    case DLL_THREAD_ATTACH:
    case DLL_THREAD_DETACH:
    case DLL_PROCESS_DETACH:
        break;
    }
    return TRUE;
}


