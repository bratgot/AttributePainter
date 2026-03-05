#pragma once
#include "Types.h"
#include <vector>
#include <array>
#include <algorithm>
#include <memory>
#include <span>

// USD forward decls
#include <pxr/usd/usdGeom/mesh.h>

namespace AP {

// ─────────────────────────────────────────────────────────────────────────────
//  KdNode — compact 3D KD-tree for vertex lookups within a radius.
//  Build once per mesh, query every stroke tick.
//  ~32 bytes per node; build O(n log n), query O(log n + k).
// ─────────────────────────────────────────────────────────────────────────────
struct KdNode {
    Vec3f    point;
    uint32_t pointIndex; // original vertex index
    int32_t  left  = -1;
    int32_t  right = -1;
    uint8_t  axis  = 0;
};

class KdTree {
public:
    void build(const std::vector<Vec3f>& points);

    // Fill `out` with (index, distSq) pairs where distSq <= radiusSq
    void queryRadius(const Vec3f& center, float radiusSq,
                     std::vector<std::pair<uint32_t,float>>& out) const;

    bool empty() const { return nodes_.empty(); }

private:
    std::vector<KdNode> nodes_;

    int buildRecursive(std::vector<std::pair<Vec3f,uint32_t>>& pts,
                       int lo, int hi, int depth);
    void queryRecursive(int nodeIdx, const Vec3f& center,
                        float radiusSq,
                        std::vector<std::pair<uint32_t,float>>& out) const;
};

// ─────────────────────────────────────────────────────────────────────────────
//  MeshSampler — wraps a USD mesh, owns the KD-tree and BVH for ray casting.
//  Rebuild when topology or xform changes (dirty flag driven from the Op).
// ─────────────────────────────────────────────────────────────────────────────
class MeshSampler {
public:
    MeshSampler() = default;
    ~MeshSampler() = default;

    // Non-copyable (owns large buffers)
    MeshSampler(const MeshSampler&) = delete;
    MeshSampler& operator=(const MeshSampler&) = delete;

    // Rebuild from USD mesh data (call from Op::build_handles or equivalent)
    void rebuild(const std::vector<Vec3f>& worldPoints,
                 const std::vector<int>&   faceVertCounts,
                 const std::vector<int>&   faceVertIndices);

    // Ray-cast against the mesh. Returns closest hit.
    HitResult intersect(const Ray& ray) const;

    // Return all vertex indices within world-space radius of center.
    // Results are (index, distanceSq) pairs, sorted by distance.
    void verticesInRadius(const Vec3f& center, float radius,
                          std::vector<std::pair<uint32_t,float>>& out) const;

    bool isValid() const { return !points_.empty(); }
    size_t pointCount() const { return points_.size(); }

    // Read current colour for a vertex (falls back to defaultColor if not set)
    Color3f getColor(uint32_t idx) const;
    void    setColor(uint32_t idx, Color3f c);
    void    initColors(const std::vector<Color3f>& initial);
    const std::vector<Color3f>& colors() const { return colors_; }

private:
    // ── geometry ──────────────────────────────────────────────────────────────
    std::vector<Vec3f>  points_;       // world-space vertex positions
    std::vector<int>    faceCounts_;
    std::vector<int>    faceIndices_;
    std::vector<Vec3f>  faceNormals_;  // per-face normals (precomputed)

    // ── acceleration structures ───────────────────────────────────────────────
    KdTree              kdTree_;

    // BVH: flat AABB tree over triangulated faces
    struct Triangle {
        Vec3f v0, v1, v2;
        Vec3f normal;
        uint32_t faceIndex;
    };
    struct BVHNode {
        Vec3f    bmin, bmax;
        int32_t  left  = -1;  // >=0 → child index, -1 → leaf
        int32_t  right = -1;
        uint32_t triBegin = 0;
        uint32_t triEnd   = 0;
    };
    std::vector<Triangle> tris_;
    std::vector<BVHNode>  bvh_;

    // ── colour data ───────────────────────────────────────────────────────────
    std::vector<Color3f> colors_;

    // ── helpers ───────────────────────────────────────────────────────────────
    void buildBVH();
    int  buildBVHRecursive(std::vector<uint32_t>& indices, int lo, int hi, int depth);
    bool intersectAABB(const BVHNode& node, const Ray& ray, float& tMin) const;
    bool intersectTri(const Triangle& tri, const Ray& ray, float& t, Vec3f& n) const;
    void tessellateFaces();

    static Vec3f aabbMin(const Vec3f& a, const Vec3f& b) {
        return { std::min(a.x,b.x), std::min(a.y,b.y), std::min(a.z,b.z) };
    }
    static Vec3f aabbMax(const Vec3f& a, const Vec3f& b) {
        return { std::max(a.x,b.x), std::max(a.y,b.y), std::max(a.z,b.z) };
    }
    static Vec3f cross(const Vec3f& a, const Vec3f& b) {
        return { a.y*b.z - a.z*b.y,
                 a.z*b.x - a.x*b.z,
                 a.x*b.y - a.y*b.x };
    }
    static float dot(const Vec3f& a, const Vec3f& b) {
        return a.x*b.x + a.y*b.y + a.z*b.z;
    }
};

} // namespace AP
