#pragma once

// Internal bridge: keep log/dump implementation headers out of the public module.
namespace ce::platform::winapi::crash
{
    using WindowProvider = void* (*)();
    using TextSink = void (*)(void* output, const char* text, unsigned long long size);

    void InstallWriter(bool full, WindowProvider windowProvider);
    void WriteDump(void* exceptionPointers, const char* reason);
    void SetUnattended(bool unattended);
    bool IsUnattended();
    void EnableHeapValidation();
    void SuppressCrtDialogs();
    void CopyStackTrace(const void* capturedContext, int skipFrames, void* output, TextSink sink);
}
