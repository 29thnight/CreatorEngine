using System;
using System.Runtime.CompilerServices;
using Mathf;

namespace CreatorEngine
{
    public class Transform
    {
        internal IntPtr m_NativePtr;

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 ICall_GetLocalPosition(IntPtr self);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void ICall_SetLocalPosition(IntPtr self, Vector3 position);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Quaternion ICall_GetLocalRotation(IntPtr self);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void ICall_SetLocalRotation(IntPtr self, Quaternion rotation);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 ICall_GetLocalScale(IntPtr self);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void ICall_SetLocalScale(IntPtr self, Vector3 scale);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 ICall_GetWorldPosition(IntPtr self);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void ICall_SetWorldPosition(IntPtr self, Vector3 position);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Quaternion ICall_GetWorldRotation(IntPtr self);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void ICall_SetWorldRotation(IntPtr self, Quaternion rotation);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 ICall_GetWorldScale(IntPtr self);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void ICall_SetWorldScale(IntPtr self, Vector3 scale);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 ICall_GetForward(IntPtr self);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 ICall_GetRight(IntPtr self);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector3 ICall_GetUp(IntPtr self);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern IntPtr ICall_GetOwner(IntPtr self);

        public Vector3 LocalPosition
        {
            get => ICall_GetLocalPosition(m_NativePtr);
            set => ICall_SetLocalPosition(m_NativePtr, value);
        }

        public Quaternion LocalRotation
        {
            get => ICall_GetLocalRotation(m_NativePtr);
            set => ICall_SetLocalRotation(m_NativePtr, value);
        }

        public Vector3 LocalScale
        {
            get => ICall_GetLocalScale(m_NativePtr);
            set => ICall_SetLocalScale(m_NativePtr, value);
        }

        public Vector3 WorldPosition
        {
            get => ICall_GetWorldPosition(m_NativePtr);
            set => ICall_SetWorldPosition(m_NativePtr, value);
        }

        public Quaternion WorldRotation
        {
            get => ICall_GetWorldRotation(m_NativePtr);
            set => ICall_SetWorldRotation(m_NativePtr, value);
        }

        public Vector3 WorldScale
        {
            get => ICall_GetWorldScale(m_NativePtr);
            set => ICall_SetWorldScale(m_NativePtr, value);
        }

        public Vector3 Forward => ICall_GetForward(m_NativePtr);
        public Vector3 Right => ICall_GetRight(m_NativePtr);
        public Vector3 Up => ICall_GetUp(m_NativePtr);

        public GameObject? Owner
        {
            get
            {
                IntPtr ownerPtr = ICall_GetOwner(m_NativePtr);
                if (ownerPtr == IntPtr.Zero)
                {
                    return null;
                }

                var gameObject = new GameObject();
                gameObject.m_NativePtr = ownerPtr;
                return gameObject;
            }
        }
    }
}
