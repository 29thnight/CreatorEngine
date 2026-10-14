// Each frame supplies immutable root descriptors; GPU visibility supplies work.
// No material instance or mesh is submitted separately by the raster consumer.
cbuffer LXIndirectBatchConstants : register(b0) { uint lx_command_count; uint lx_indexed_replay; };
StructuredBuffer<uint4> lx_commands : register(t0);
ByteAddressBuffer lx_templates : register(t1);
ByteAddressBuffer lx_visible_arguments : register(t2);
RWStructuredBuffer<uint> lx_indirect_commands : register(u0);

[numthreads(64, 1, 1)]
void LXBuildIndirectCommands(uint3 id : SV_DispatchThreadID)
{
    if (id.x >= lx_command_count) return;
    uint4 command = lx_commands[id.x]; // template byte offset, output byte offset, root words, args byte offset
    for (uint word = 0; word < command.z; ++word)
        lx_indirect_commands[command.y / 4 + word] = lx_templates.Load(command.x + word * 4);
    // Temporal replays all sealed indices; the template includes IBV and draw args.
    if (lx_indexed_replay == 1) return;
    if (lx_indexed_replay == 2) {
        // Sorted alpha preserves command order; visibility supplies indexed work.
        for (uint word = 0; word < 5; ++word)
            lx_indirect_commands[command.y / 4 + command.z - 5 + word] = lx_visible_arguments.Load(command.w + word * 4);
        return;
    }
    uint3 groups = lx_visible_arguments.Load3(command.w);
    for (uint axis = 0; axis < 3; ++axis) lx_indirect_commands[command.y / 4 + command.z + axis] = groups[axis];
}
