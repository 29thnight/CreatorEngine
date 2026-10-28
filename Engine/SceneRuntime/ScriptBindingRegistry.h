#pragma once

#include "ScriptApiTable.h"

namespace ce::script::binding
{
    void RegisterEntityApi(ScriptApiTable& table);
    void RegisterTransformApi(ScriptApiTable& table);
    void RegisterInputApi(ScriptApiTable& table);
}
