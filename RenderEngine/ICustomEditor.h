#pragma once
#include <windows.h>

interface ICustomEditor
{
    virtual ~ICustomEditor() = default;
    virtual void OnInspectorGUI() = 0;
};