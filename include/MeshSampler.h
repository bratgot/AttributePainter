#pragma once
#include "Types.h"
#include <vector>
#include <array>
#include <algorithm>
#include <memory>
#include <span>

// USD forward decls

namespace AP {

// ─────────────────────────────────────────────────────────────────────────────
//  KdNode — compact 3D KD-tree for vertex lookups within a radius.
// ─────────────────────────────────────────────────────────────────────────────
struct KdNode {
    Vec3f    point;
    uint32_t pointIndex;
    int32_t  left  = -1;
    int32_t  right = -1;
    uint8_t  axis  = 0;
};

class KdTree {
public:
    void build(const std::vector<Vec3f>& points);
    void queryRadius(const Vec3f& center, float radiusSq,
                     std::vector<std::pair<uint32_t,float>>& out) const;
    bool empty() const { return nodes_.empty(); }

private:
    std::vector<KdNode> nodes_;
    int rootIdx_ = -1;  // index of root node (median of full build)
    int buildRecursive(std::vector<std::pair<Vec3f,uint32_t>>& pts,
                       int lo, int hi, int depth);
    void queryRecursive(int nodeIdx, const Vec3f& center,
                        float radiusSq,
                        std::vector<std::pair<uint32_t,float>>& out) const;
};

// ─────────────────────────────────────────────────────────────────────────────
//  MeshSampler
// ─────────────────────────────────────────────────────────────────────────────
class MeshSampler {
public:
    MeshSampler() = default;
    ~MeshSampler() = default;

    MeshSampler(const MeshSampler&) = delete;
    MeshSampler& operator=(const MeshSampler&) = delete;

    void rebuild(const std::vector<Vec3f>& worldPoints,
                 const std::vector<int>&   faceVertCounts,
                 const std::vector<int>&   faceVertIndices);

    HitResult intersect(const Ray& ray) const;

    void verticesInRadius(const Vec3f& center, float radius,
                          std::vector<std::pair<uint32_t,float>>& out) const;

    bool isValid() const { return !points_.empty(); }
    size_t pointCount() const { return points_.size(); }

    Color3f getColor(uint32_t idx) const;
    void    setColor(uint32_t idx, Color3f c);
    void    initColors(const std::vector<Color3f>& initial);
    const std::vector<Color3f>& colors() const { return colors_; }

    // Point accessor for GL debug drawing
    Vec3f getPoint(uint32_t idx) const {
        if (idx < points_.size()) return points_[idx];
        return {0,0,0};
    }

    // Debug accessors
    Vec3f centroid() const { return centroid_; }
    Vec3f bboxMin() const  { return bboxMin_; }
    Vec3f bboxMax() const  { return bboxMax_; }
    size_t triCount() const { return tris_.size(); }
    size_t bvhNodeCount() const { return bvh_.size(); }

private:
    std::vector<Vec3f>  points_;
    std::vector<int>    faceCounts_;
    std::vector<int>    faceIndices_;
    std::vector<Vec3f>  faceNormals_;

    KdTree              kdTree_;

    struct Triangle {
        Vec3f v0, v1, v2;
        Vec3f normal;
        uint32_t faceIndex;
    };
    struct BVHNode {
        Vec3f    bmin, bmax;
        int32_t  left  = -1;
        int32_t  right = -1;
        uint32_t triBegin = 0;
        uint32_t triEnd   = 0;
    };
    std::vector<Triangle> tris_;
    std::vector<BVHNode>  bvh_;
    std::vector<uint32_t> triOrder_;

    std::vector<Color3f> colors_;

    // Cached bounds (recomputed on rebuild)
    Vec3f centroid_ = {0,0,0};
    Vec3f bboxMin_  = {0,0,0};
    Vec3f bboxMax_  = {0,0,0};

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
