#pragma once

// CullingBox.h
//
// This file contains helper classes for performing frustum culling on an Octree.
// It defines a CullingBox (an Axis-Aligned Bounding Box), a Plane, and a Frustum.
//
// Requires the DirectXMath library for vector and matrix operations.
// Make sure to link against it in your project.

#include <DirectXMath.h>

// Represents a plane in 3D space, defined by a normal vector and a distance from the origin.
// The plane equation is Ax + By + Cz + D = 0, where (A, B, C) is the normal and D is the distance.
struct Plane
{
    DirectX::XMFLOAT3 normal;
    float d;

    Plane() : normal(0, 1, 0), d(0) {}

    Plane(const DirectX::XMFLOAT3& n, float dist) : normal(n), d(dist) {}

    // Normalizes the plane coefficients.
    void Normalize()
    {
        DirectX::XMVECTOR p = DirectX::XMLoadFloat4(&DirectX::XMFLOAT4(normal.x, normal.y, normal.z, d));
        p = DirectX::XMPlaneNormalize(p);
        DirectX::XMStoreFloat4((DirectX::XMFLOAT4*)&this->normal, p);
    }
};

// Represents the view frustum, which is defined by 6 planes.
class Frustum
{
public:
    // The 6 planes of the frustum: [0]Left, [1]Right, [2]Bottom, [3]Top, [4]Near, [5]Far
    Plane planes[6];

public:
    Frustum() {}

    // Extracts the 6 frustum planes from a given view-projection matrix.
    void ExtractFromMatrix(const DirectX::XMFLOAT4X4& viewProjMatrix)
    {
        const DirectX::XMFLOAT4X4& m = viewProjMatrix;

        // Left plane
        planes[0].normal.x = m._14 + m._11;
        planes[0].normal.y = m._24 + m._21;
        planes[0].normal.z = m._34 + m._31;
        planes[0].d = m._44 + m._41;

        // Right plane
        planes[1].normal.x = m._14 - m._11;
        planes[1].normal.y = m._24 - m._21;
        planes[1].normal.z = m._34 - m._31;
        planes[1].d = m._44 - m._41;

        // Bottom plane
        planes[2].normal.x = m._14 + m._12;
        planes[2].normal.y = m._24 + m._22;
        planes[2].normal.z = m._34 + m._32;
        planes[2].d = m._44 + m._42;

        // Top plane
        planes[3].normal.x = m._14 - m._12;
        planes[3].normal.y = m._24 - m._22;
        planes[3].normal.z = m._34 - m._32;
        planes[3].d = m._44 - m._42;

        // Near plane
        planes[4].normal.x = m._13;
        planes[4].normal.y = m._23;
        planes[4].normal.z = m._33;
        planes[4].d = m._43;

        // Far plane
        planes[5].normal.x = m._14 - m._13;
        planes[5].normal.y = m._24 - m._23;
        planes[5].normal.z = m._34 - m._33;
        planes[5].d = m._44 - m._43;

        // Normalize all planes
        for (int i = 0; i < 6; ++i)
        {
            planes[i].Normalize();
        }
    }
};


// Represents an Axis-Aligned Bounding Box (AABB) for an Octree node,
// used for culling.
class CullingBox
{
public:
    DirectX::XMFLOAT3 minPoint;
    DirectX::XMFLOAT3 maxPoint;

public:
    // Constructor that takes the min and max points from the Octree node.
    CullingBox(const float pmin[3], const float pmax[3])
        : minPoint(pmin[0], pmin[1], pmin[2]), maxPoint(pmax[0], pmax[1], pmax[2])
    {
    }

    // Checks if the bounding box is inside or intersects with the given frustum.
    // Returns 'true' if the box is visible, 'false' if it should be culled.
    bool IsInFrustum(const Frustum& frustum) const
    {
        DirectX::XMVECTOR boxMin = DirectX::XMLoadFloat3(&minPoint);
        DirectX::XMVECTOR boxMax = DirectX::XMLoadFloat3(&maxPoint);

        // Check the box against each of the 6 frustum planes.
        for (int i = 0; i < 6; ++i)
        {
            const Plane& plane = frustum.planes[i];
            DirectX::XMVECTOR planeNormal = DirectX::XMLoadFloat3(&plane.normal);
            DirectX::XMVECTOR planeWithDist = DirectX::XMVectorSetW(planeNormal, plane.d);

            // Find the vertex of the box that is "most positive" with respect to the plane normal (the p-vertex).
            DirectX::XMVECTOR pVertex = boxMin;
            if (plane.normal.x >= 0) pVertex = DirectX::XMVectorSetX(pVertex, DirectX::XMVectorGetX(boxMax));
            if (plane.normal.y >= 0) pVertex = DirectX::XMVectorSetY(pVertex, DirectX::XMVectorGetY(boxMax));
            if (plane.normal.z >= 0) pVertex = DirectX::XMVectorSetZ(pVertex, DirectX::XMVectorGetZ(boxMax));

            // Check if the p-vertex is outside the plane.
            // If dot(p-vertex, plane_normal) + plane_d < 0, the whole box is outside.
            if (DirectX::XMVectorGetX(DirectX::XMPlaneDotCoord(planeWithDist, pVertex)) < 0.0f)
            {
                // The box is completely outside this plane, so it's outside the frustum.
                return false;
            }
        }

        // If the box is not completely outside any plane, it's either intersecting or fully inside.
        // In either case, it's considered visible.
        // Note: This test can have some false positives where the box is not intersecting but still passes.
        // A more accurate test would be a Separating Axis Theorem (SAT) test, but this is much more complex.
        // For most culling purposes, this plane check is sufficient and very fast.
        return true;
    }
};
