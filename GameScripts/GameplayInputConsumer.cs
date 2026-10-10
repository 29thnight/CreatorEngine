using CreatorEngine.Generated;

namespace CreatorEngine.Scripts
{
    /// <summary>Attach with the Gameplay InputGraph session. This example has one
    /// command producer: the ordered Jump events from each sealed frame.</summary>
    public sealed partial class GameplayInputConsumer : Component
    {
        private InputSession? _input;
        private ulong _lastSequence;
        public Float2 Move { get; private set; }
        public Float2 Look { get; private set; }
        public int JumpCommands { get; private set; }

        public override void PrePhysics(float tick)
        {
            // AssetDepot loading is asynchronous. An absent frame is not fabricated
            // as neutral input, and the first ready frame's events are preserved.
            if (_input is null && !InputSession.TryFor(this, out _input))
            {
                return;
            }
            var frame = _input!.GetFrame();
            if (frame.Sequence == _lastSequence)
            {
                return;
            }
            _lastSequence = frame.Sequence;
            Move = frame.Read(GameplayInputs.Move).Value;
            Look = frame.Read(GameplayInputs.Look).Value;
            JumpCommands = 0;
            for (int i = 0; i < frame.Events.Length; ++i)
            {
                if (frame.Events[i].Signal != GameplayInputs.Jump.ID)
                {
                    continue;
                }
                var inputEvent = frame.ReadEvent(i, GameplayInputs.Jump);
                if (inputEvent.Phase == InputEventPhase.Performed)
                {
                    ++JumpCommands;
                }
                if (inputEvent.Phase == InputEventPhase.Canceled)
                {
                    JumpCommands = 0;
                }
            }
            // Gameplay decides whether to execute each command. A subscriber could
            // consume these events instead; never use both paths to produce Jump.
        }

        public override void OnEndSimulation()
        {
            _input = null;
            _lastSequence = 0;
            Move = default;
            Look = default;
            JumpCommands = 0;
        }
    }
}
