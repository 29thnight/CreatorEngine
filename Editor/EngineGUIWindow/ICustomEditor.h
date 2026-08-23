#pragma once
#include <windows.h>

interface ICustomEditor
{
    virtual ~ICustomEditor() = default;
// Begin, End 제외한 나머지 ImGui 함수는 이 함수 안에서만 호출해야 합니다.
    virtual void OnInspectorGUI() = 0;
};