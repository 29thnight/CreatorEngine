#pragma once

#include "../RHISubmissionThread.h"
#include "../RHIBackendExecution.h"
#include <stdexcept>

template<class Function>
HRESULT DX12InvokeBackend(const void* owner, const char* label, Function&& function)
{
    if (RHIBackendExecutionScope::IsActive() || GetRHISubmissionThread().IsCurrentThread())
    {
        return function();
    }
    HRESULT result = E_FAIL;
    std::string error;
    if (!GetRHISubmissionThread().ExecuteAndWait(
            owner, label,
            [&](std::string&)
            {
                RHIBackendExecutionScope scope;
                result = function();
                return true;
            },
            error))
    {
        throw std::runtime_error(error);
    }
    return result;
}
