// CPU uploads the first three rows of transpose(affine bone matrix).
// Reconstruct the original row-vector matrix before weighted skinning.
struct PackedBone
{
    float4 row0;
    float4 row1;
    float4 row2;
};

float4x4 UnpackBone(PackedBone bone)
{
    return transpose(float4x4(bone.row0, bone.row1, bone.row2,
        float4(0.0, 0.0, 0.0, 1.0)));
}
