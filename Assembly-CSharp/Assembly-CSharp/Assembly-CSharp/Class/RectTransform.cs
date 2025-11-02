using System;
using System.Runtime.CompilerServices;
using Mathf;

namespace CreatorEngine
{
    public enum AnchorPreset
    {
        TopLeft, TopCenter, TopRight,
        MiddleLeft, MiddleCenter, MiddleRight,
        BottomLeft, BottomCenter, BottomRight,
        StretchLeft, StretchCenter, StretchRight,
        StretchTop, StretchMiddle, StretchBottom,
        StretchAll
    }

    public class RectTransform : Component
    {
        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector2 ICall_GetAnchorMin(IntPtr self);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void ICall_SetAnchorMin(IntPtr self, Vector2 anchorMin);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector2 ICall_GetAnchorMax(IntPtr self);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void ICall_SetAnchorMax(IntPtr self, Vector2 anchorMax);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector2 ICall_GetAnchoredPosition(IntPtr self);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void ICall_SetAnchoredPosition(IntPtr self, Vector2 position);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector2 ICall_GetSizeDelta(IntPtr self);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void ICall_SetSizeDelta(IntPtr self, Vector2 sizeDelta);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Vector2 ICall_GetPivot(IntPtr self);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void ICall_SetPivot(IntPtr self, Vector2 pivot);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern Rect ICall_GetWorldRect(IntPtr self);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void ICall_SetAnchorPreset(IntPtr self, AnchorPreset preset);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void ICall_SetAnchorsPivotKeepWorld(IntPtr self, Vector2 newAnchorMin, Vector2 newAnchorMax, Vector2 newPivot, Rect parentRect);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern void ICall_SetParentKeepWorldPosition(IntPtr self, IntPtr parentPtr);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern bool ICall_IsDirty(IntPtr self);

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern ulong ICall_GetTypeID();

        [MethodImpl(MethodImplOptions.InternalCall)]
        private static extern IntPtr ICall_GetFromGameObject(IntPtr gameObjectPtr);

        public Vector2 AnchorMin
        {
            get => ICall_GetAnchorMin(m_NativePtr);
            set => ICall_SetAnchorMin(m_NativePtr, value);
        }

        public Vector2 AnchorMax
        {
            get => ICall_GetAnchorMax(m_NativePtr);
            set => ICall_SetAnchorMax(m_NativePtr, value);
        }

        public Vector2 AnchoredPosition
        {
            get => ICall_GetAnchoredPosition(m_NativePtr);
            set => ICall_SetAnchoredPosition(m_NativePtr, value);
        }

        public Vector2 SizeDelta
        {
            get => ICall_GetSizeDelta(m_NativePtr);
            set => ICall_SetSizeDelta(m_NativePtr, value);
        }

        public Vector2 Pivot
        {
            get => ICall_GetPivot(m_NativePtr);
            set => ICall_SetPivot(m_NativePtr, value);
        }

        public Rect WorldRect => ICall_GetWorldRect(m_NativePtr);

        public bool IsDirty => ICall_IsDirty(m_NativePtr);

        public void SetAnchorPreset(AnchorPreset preset) => ICall_SetAnchorPreset(m_NativePtr, preset);

        public void SetAnchorsPivotKeepWorld(Vector2 anchorMin, Vector2 anchorMax, Vector2 pivot, Rect parentRect)
            => ICall_SetAnchorsPivotKeepWorld(m_NativePtr, anchorMin, anchorMax, pivot, parentRect);

        public void SetParentKeepWorldPosition(GameObject? newParent)
            => ICall_SetParentKeepWorldPosition(m_NativePtr, newParent?.m_NativePtr ?? IntPtr.Zero);

        private static readonly ulong s_ComponentTypeId = ICall_GetTypeID();

        public static ulong ComponentTypeId => s_ComponentTypeId;

        internal static RectTransform? CreateFromNative(IntPtr nativePtr)
        {
            if (nativePtr == IntPtr.Zero)
            {
                return null;
            }

            return new RectTransform
            {
                m_NativePtr = nativePtr
            };
        }

        public static RectTransform? Get(GameObject? gameObject)
        {
            if (gameObject == null)
            {
                return null;
            }

            IntPtr rectPtr = ICall_GetFromGameObject(gameObject.m_NativePtr);
            return CreateFromNative(rectPtr);
        }

        public static RectTransform? FromComponent(Component? component)
        {
            if (component == null || component.TypeID != ComponentTypeId)
            {
                return null;
            }

            return new RectTransform
            {
                m_NativePtr = component.m_NativePtr
            };
        }
    }
}
