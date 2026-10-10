namespace CreatorEngine
{
    /// <summary>Platform cursor output. Device input is consumed through InputSession.</summary>
    public static class Cursor
    {
        public static void SetVisible(bool visible) => Native.CursorSetVisible(visible);
    }
}
