using System;
using System.Runtime.InteropServices;

public static class PhysicsQueryCpuPlacement
{
    [DllImport("kernel32.dll", SetLastError=true)] static extern bool GetLogicalProcessorInformation(IntPtr buffer, ref uint bytes);
    [DllImport("kernel32.dll", SetLastError=true)] static extern IntPtr OpenThread(uint access, bool inherit, uint id);
    [DllImport("kernel32.dll", SetLastError=true)] static extern UIntPtr SetThreadAffinityMask(IntPtr thread, UIntPtr mask);
    [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr handle);

    public static ulong ReserveCore(ulong allowed, bool preferLast = false)
    {
        if (IntPtr.Size != 8) throw new InvalidOperationException("CPU fixture requires x64");
        uint bytes = 0;
        GetLogicalProcessorInformation(IntPtr.Zero, ref bytes);
        if (bytes == 0 || bytes % 32 != 0) throw new InvalidOperationException("CPU topology unavailable");
        IntPtr buffer = Marshal.AllocHGlobal((int)bytes);

        try
        {
            if (!GetLogicalProcessorInformation(buffer, ref bytes)) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            ulong selected = 0;
            for (int offset = 0; offset < bytes; offset += 32)
            {
                if (Marshal.ReadInt32(buffer, offset + 8) != 0) continue;
                ulong core = unchecked((ulong)Marshal.ReadInt64(buffer, offset));
                if ((core & allowed) != core || (allowed & ~core) == 0) continue;
                if (!preferLast) return core;
                selected = core;
            }
            if (selected != 0) return selected;
            throw new InvalidOperationException("No complete reservable physical core in process affinity");
        }
        finally { Marshal.FreeHGlobal(buffer); }
    }

    public static ulong Pin(uint id, ulong mask)
    {
        IntPtr handle = OpenThread(0x0020 | 0x0040, false, id);
        if (handle == IntPtr.Zero) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());

        try
        {
            ulong previous = SetThreadAffinityMask(handle, new UIntPtr(mask)).ToUInt64();
            if (previous == 0) throw new System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error());
            return previous;
        }
        finally { CloseHandle(handle); }
    }
}
