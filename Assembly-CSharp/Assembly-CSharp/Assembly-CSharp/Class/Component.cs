using System;
using System.Runtime.CompilerServices;

namespace CreatorEngine
{
    public class Component : Object
    {
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern IntPtr ICall_GetOwner(IntPtr self);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern IntPtr ICall_GetComponent(IntPtr self, ulong typeGuid);

        public GameObject? Owner
        {
            get
            {
                IntPtr ownerPtr = ICall_GetOwner(m_NativePtr);
                if (ownerPtr == IntPtr.Zero)
                {
                    return null;
                }

                var owner = new GameObject();
                owner.m_NativePtr = ownerPtr;
                return owner;
            }
        }

        public Component? GetComponent(ulong typeGuid)
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
    }
}
