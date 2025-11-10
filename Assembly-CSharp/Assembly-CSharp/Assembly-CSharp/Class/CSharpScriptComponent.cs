using System;
using System.Runtime.CompilerServices;

namespace CreatorEngine
{
    public class CSharpScriptComponent : Component
    {
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string ICall_GetScriptName(IntPtr self);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string ICall_GetScriptGuid(IntPtr self);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool ICall_HasManagedInstance(IntPtr self);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern uint ICall_GetBehaviorHandle(IntPtr self);

        public string ScriptName => ICall_GetScriptName(m_NativePtr);

        public string ScriptGuid => ICall_GetScriptGuid(m_NativePtr);

        public bool HasManagedInstance => ICall_HasManagedInstance(m_NativePtr);

        public uint BehaviorHandle => ICall_GetBehaviorHandle(m_NativePtr);
    }
}
