# Source-hashed CCT migration policy

The regression emits an example policy into `Build/Obj/Phase19M1/character-policy-example.json`. It is for the P0 fixture, not blanket authorization to migrate game content.

The reviewed hash covers the old CCT and its companion Rigidbody together, ordered by component instance ID. Modified source invalidates the choice. A policy must name the speed source, external desired-velocity input, static exponential braking, explicit removal of dynamic damping, external automatic rotation, removal of the collider-free velocity carrier, the reference tick and the new fall-speed limit.

Speed is recorded for the input owner; the new CCT has no legacy maxSpeed field. The converter does not create gameplay input or rotation code. Product acceptance must verify that external owner. An independent body, a collider, old/new ownership mixed together, offsets, non-unit scale, asset references, unsupported prefab overrides or references to the retired carrier block conversion.

Reviewed CCT radius/height overrides can be remapped when equal to the effective source; unit-changing fields and carrier overrides remain blocked.
