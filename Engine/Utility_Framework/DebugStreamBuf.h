#pragma once
#include "LogSystem.h"
#include <streambuf>
#include <string>
#include <iostream>

class DebugStreamBuf : public std::streambuf
{
public:
    explicit DebugStreamBuf(std::streambuf* originalBuf)
        : m_originalBuf(originalBuf) {
    }

protected:
    int overflow(int c) override
    {
        if (c == '\n')
        {
            FlushBuffer();
        }
        else if (c != EOF)
        {
            m_buffer += static_cast<char>(c);
        }

        return c;
    }

    int sync() override
    {
        FlushBuffer();
        return 0;
    }

private:
    void FlushBuffer()
    {
        if (!m_buffer.empty())
        {
            Debug->LogDebug(m_buffer);
            m_buffer.clear();
        }
    }

    std::string m_buffer;
    std::streambuf* m_originalBuf;
};
