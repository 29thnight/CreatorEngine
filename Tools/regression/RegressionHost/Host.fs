namespace global

open System
open System.Runtime.InteropServices

module private HostNative =
    [<DllImport("kernel32.dll", SetLastError = true)>]
    extern bool GetLogicalProcessorInformation(nativeint buffer, uint32& bytes)
    [<DllImport("kernel32.dll", SetLastError = true)>]
    extern nativeint OpenThread(uint32 access, bool inheritHandle, uint32 id)
    [<DllImport("kernel32.dll", SetLastError = true)>]
    extern unativeint SetThreadAffinityMask(nativeint thread, unativeint mask)
    [<DllImport("kernel32.dll")>]
    extern bool CloseHandle(nativeint handle)
    [<DllImport("user32.dll")>]
    extern bool SetWindowPos(nativeint window, nativeint after, int x, int y, int width, int height, uint32 flags)
    [<DllImport("user32.dll")>]
    extern uint32 GetWindowThreadProcessId(nativeint window, uint32& processId)

[<AbstractClass; Sealed>]
type PhysicsB2ProbeWindow private () =
    static member SetWindowPos(window, after, x, y, width, height, flags) =
        HostNative.SetWindowPos(window, after, x, y, width, height, flags)
    static member GetWindowThreadProcessId(window, processId: byref<uint32>) =
        HostNative.GetWindowThreadProcessId(window, &processId)

[<AbstractClass; Sealed>]
type PhysicsQueryCpuPlacement private () =
    static member ReserveCore(allowed: uint64, preferLast: bool) =
        if IntPtr.Size <> 8 then invalidOp "CPU fixture requires x64"
        let mutable bytes = 0u
        HostNative.GetLogicalProcessorInformation(0n, &bytes) |> ignore
        if bytes = 0u || bytes % 32u <> 0u then invalidOp "CPU topology unavailable"
        let buffer = Marshal.AllocHGlobal(int bytes)
        try
            if not (HostNative.GetLogicalProcessorInformation(buffer, &bytes)) then
                raise (System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error()))
            let mutable selected = 0UL
            let mutable offset = 0
            while offset < int bytes && (preferLast || selected = 0UL) do
                if Marshal.ReadInt32(buffer, offset + 8) = 0 then
                    let core = uint64 (Marshal.ReadInt64(buffer, offset))
                    if (core &&& allowed) = core && (allowed &&& ~~~core) <> 0UL then selected <- core
                offset <- offset + 32
            if selected = 0UL then invalidOp "No complete reservable physical core in process affinity"
            selected
        finally
            Marshal.FreeHGlobal(buffer)

    static member ReserveCore(allowed: uint64) = PhysicsQueryCpuPlacement.ReserveCore(allowed, false)

    static member Pin(id: uint32, mask: uint64) =
        let handle = HostNative.OpenThread(0x0020u ||| 0x0040u, false, id)
        if handle = 0n then raise (System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error()))
        try
            let previous = HostNative.SetThreadAffinityMask(handle, unativeint mask)
            if previous = 0un then raise (System.ComponentModel.Win32Exception(Marshal.GetLastWin32Error()))
            uint64 previous
        finally
            HostNative.CloseHandle(handle) |> ignore
