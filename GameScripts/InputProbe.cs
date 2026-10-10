namespace CreatorEngine.Scripts
{
    /// <summary>Diagnostic consumer of the same sealed frame as gameplay subscribers.
    /// Attach alongside an InputSessionComponent with a compiled LX InputGraph.</summary>
    public sealed partial class InputProbe : Component
    {
        [SerializeField]
        private bool _watch;
        private InputSession? _session;
        private ulong _lastSequence;

        public override void PrePhysics(float tick)
        {
            if (!_watch)
            {
                return;
            }
            if (_session is null && !InputSession.TryFor(this, out _session))
            {
                return;
            }
            var frame = _session!.GetFrame();
            if (frame.Sequence == _lastSequence)
            {
                return;
            }
            _lastSequence = frame.Sequence;
            if (frame.HasHistoryGap)
            {
                LogError($"[InputProbe] History gap at input frame {frame.Sequence}; input was canceled/resynchronized.");
            }
            foreach (var inputEvent in frame.Events)
            {
                Log($"[InputProbe] frame={frame.Sequence} signal={inputEvent.Signal} phase={inputEvent.Phase} " +
                    $"source={inputEvent.SourceTime} effective={inputEvent.EffectiveTime} sequence={inputEvent.Sequence} " +
                    $"reason={inputEvent.Reason}");
            }
        }

        public override void OnEndSimulation()
        {
            _session = null;
            _lastSequence = 0;
        }
    }
}
