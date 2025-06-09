#pragma once
#include <Windows.h>

class FenceFlag
{
public:
    FenceFlag() : m_signaled(false)
    {
        InitializeConditionVariable(&m_cv);
        InitializeCriticalSection(&m_cs);
    }

    ~FenceFlag()
    {
        DeleteCriticalSection(&m_cs);
    }

    void Signal()
    {
        EnterCriticalSection(&m_cs);
        m_signaled = true;
        WakeAllConditionVariable(&m_cv);
        LeaveCriticalSection(&m_cs);
    }

    void Wait()
    {
        EnterCriticalSection(&m_cs);
        while (!m_signaled)
        {
            SleepConditionVariableCS(&m_cv, &m_cs, INFINITE);
        }
        LeaveCriticalSection(&m_cs);
    }

    void Reset()
    {
        EnterCriticalSection(&m_cs);
        m_signaled = false;
        LeaveCriticalSection(&m_cs);
    }

private:
    CONDITION_VARIABLE m_cv;
    CRITICAL_SECTION   m_cs;
    bool               m_signaled;
};
