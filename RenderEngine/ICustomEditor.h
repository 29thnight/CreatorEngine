#pragma once
#include <windows.h>

interface ICustomEditor
{
    virtual ~ICustomEditor() = default;
// Begin, End ������ ������ ImGui �Լ��� �� �Լ� �ȿ����� ȣ���ؾ� �մϴ�.
    virtual void OnInspectorGUI() = 0;
};