// Object.cs (C# 측 래퍼)
// 네임스페이스/타입명은 C++ 등록 문자열과 반드시 일치해야 함: CreatorEngine.Object
using System;
using System.Runtime.CompilerServices;

namespace CreatorEngine
{
    public class Object
    {
        // 네이티브 포인터 보관 (엔진 생성/보유 쪽에서 세팅)
        internal IntPtr m_NativePtr;

        // --- Internal Calls ---
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern ulong ICall_GetInstanceID(IntPtr self);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern ulong ICall_GetTypeID(IntPtr self);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string ICall_ToString(IntPtr self);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string ICall_GetName(IntPtr self);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void ICall_SetName(IntPtr self, string name);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool ICall_GetEnabled(IntPtr self);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void ICall_SetEnabled(IntPtr self, bool v);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void ICall_Destroy(IntPtr self);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void ICall_SetDontDestroyOnLoad(IntPtr self);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern IntPtr ICall_Instantiate(IntPtr original, string newName);

        // --- 공개 API (C# 편의 래핑) ---
        public ulong InstanceID => ICall_GetInstanceID(m_NativePtr);
        public ulong TypeID => ICall_GetTypeID(m_NativePtr);

        public override string ToString() => ICall_ToString(m_NativePtr);

        public string Name
        {
            get => ICall_GetName(m_NativePtr);
            set => ICall_SetName(m_NativePtr, value);
        }

        public bool enabled
        {
            get => ICall_GetEnabled(m_NativePtr);
            set => ICall_SetEnabled(m_NativePtr, value);
        }

        public void Destroy() => ICall_Destroy(m_NativePtr);
        public void SetDontDestroyOnLoad() => ICall_SetDontDestroyOnLoad(m_NativePtr);

        public static Object Instantiate(Object original, string newName)
        {
            if (original == null) return null;
            IntPtr newPtr = ICall_Instantiate(original.m_NativePtr, newName);
            if (newPtr == IntPtr.Zero) return null;

            // 엔진의 오브젝트-프록시 생성 규칙에 맞게 인스턴스화
            // (예: ScriptingObjectFactory.Create<T>(nativePtr); 로 통일)
            var obj = new Object();
            obj.m_NativePtr = newPtr;
            return obj;
        }
    }
}
