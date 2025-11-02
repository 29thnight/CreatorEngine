using System;
using System.Runtime.CompilerServices;

namespace CreatorEngine
{
    public class GameObject : Object
    {
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern IntPtr ICall_Find(string name);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern IntPtr ICall_FindIndex(int index);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern IntPtr ICall_GetComponent(IntPtr self, uint typeGuid);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern IntPtr ICall_GetTransform(IntPtr self);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string ICall_GetTag(IntPtr self);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void ICall_SetTag(IntPtr self, string tag);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern string ICall_GetLayer(IntPtr self);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void ICall_SetLayer(IntPtr self, string layer);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool ICall_GetStatic(IntPtr self);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void ICall_SetStatic(IntPtr self, bool value);

        public static GameObject? Find(string name)
        {
            if (string.IsNullOrEmpty(name))
            {
                return null;
            }

            IntPtr nativePtr = ICall_Find(name);
            if (nativePtr == IntPtr.Zero)
            {
                return null;
            }

            var gameObject = new GameObject();
            gameObject.m_NativePtr = nativePtr;
            return gameObject;
        }

        public static GameObject? FindIndex(int index)
        {
            if (index < 0)
            {
                return null;
            }

            IntPtr nativePtr = ICall_FindIndex(index);
            if (nativePtr == IntPtr.Zero)
            {
                return null;
            }

            var gameObject = new GameObject();
            gameObject.m_NativePtr = nativePtr;
            return gameObject;
        }

        public Component? GetComponent(uint typeGuid)
        {
            IntPtr componentPtr = ICall_GetComponent(m_NativePtr, typeGuid);
            if (componentPtr == IntPtr.Zero)
            {
                return null;
            }

            var component = new Component();
            component.m_NativePtr = componentPtr;
            return component;
        }

        private Transform? m_CachedTransform;

        public Transform Transform
        {
            get
            {
                if (m_CachedTransform == null)
                {
                    IntPtr transformPtr = ICall_GetTransform(m_NativePtr);
                    if (transformPtr == IntPtr.Zero)
                    {
                        throw new InvalidOperationException("Native transform pointer is null.");
                    }

                    m_CachedTransform = new Transform
                    {
                        m_NativePtr = transformPtr
                    };
                }

                return m_CachedTransform;
            }
        }

        public string Tag
        {
            get => ICall_GetTag(m_NativePtr);
            set => ICall_SetTag(m_NativePtr, value ?? string.Empty);
        }

        public string Layer
        {
            get => ICall_GetLayer(m_NativePtr);
            set => ICall_SetLayer(m_NativePtr, value ?? string.Empty);
        }

        public bool IsStatic
        {
            get => ICall_GetStatic(m_NativePtr);
            set => ICall_SetStatic(m_NativePtr, value);
        }
    }
}
