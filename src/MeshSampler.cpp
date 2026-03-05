#include "MeshSampler.h"
#include <cassert>
#include <cmath>
#include <limits>
#include <numeric>
#include <stack>

namespace AP {

// ─────────────────────────────────────────────────────────────────────────────
//  KdTree
// ─────────────────────────────────────────────────────────────────────────────

void KdTree::build(const std::vector<Vec3f>& points) {
    nodes_.clear();
    if (points.empty()) return;

    std::vector<std::pair<Vec3f,uint32_t>> indexed;
    indexed.reserve(points.size());
    for (uint32_t i = 0; i < (uint32_t)points.size(); ++i)
        indexed.push_back({points[i], i});

    nodes_.resize(points.size());
    buildRecursive(indexed, 0, (int)indexed.size(), 0);
}

int KdTree::buildRecursive(std::vector<std::pair<Vec3f,uint32_t>>& pts,
                            int lo, int hi, int depth) {
    if (lo >= hi) return -1;

    // Build using a thread-local temp buffer and recursive lambda.
    // The outer call drives the whole build in one shot from [0, size).
    static thread_local std::vector<KdNode> tmp;
    tmp.resize(pts.size());

    std::function<int(int,int,int)> build = [&](int l, int h, int d) -> int {
        if (l >= h) return -1;
        int ax = d % 3;
        int m  = l + (h - l) / 2;
        std::nth_element(pts.begin() + l, pts.begin() + m, pts.begin() + h,
            [ax](const auto& a, const auto& b) {
                return (&a.first.x)[ax] < (&b.first.x)[ax];
            });
        KdNode& node = tmp[m];
        node.point      = pts[m].first;
        node.pointIndex = pts[m].second;
        node.axis       = (uint8_t)ax;
        node.left       = build(l, m, d+1);
        node.right      = build(m+1, h, d+1);
        return m;
    };

    build(0, (int)pts.size(), 0);
    nodes_ = tmp;
    nodes_.resize(pts.size());
    return 0;
}

void KdTree::queryRadius(const Vec3f& center, float radiusSq,
                         std::vector<std::pair<uint32_t,float>>& out) const {
    if (nodes_.empty()) return;
    queryRecursive(0, center, radiusSq, out);
}

void KdTree::queryRecursive(int ni, const Vec3f& center, float radiusSq,
                             std::vector<std::pair<uint32_t,float>>& out) const {
    if (ni < 0 || ni >= (int)nodes_.size()) return;
    const KdNode& node = nodes_[ni];

    Vec3f diff = center - node.point;
    float dsq  = diff.lengthSq();
    if (dsq <= radiusSq)
        out.push_back({node.pointIndex, dsq});

    float axisDist = (&diff.x)[node.axis];
    float axisDistSq = axisDist * axisDist;

    // Visit nearer child first
    int nearIdx = (axisDist < 0) ? node.left : node.right;
    int farIdx  = (axisDist < 0) ? node.right : node.left;

    queryRecursive(nearIdx, center, radiusSq, out);
    if (axisDistSq <= radiusSq)
        queryRecursive(farIdx, center, radiusSq, out);
}

// ─────────────────────────────────────────────────────────────────────────────
//  MeshSampler — rebuild
// ─────────────────────────────────────────────────────────────────────────────

void MeshSampler::rebuild(const std::vector<Vec3f>& worldPoints,
                           const std::vector<int>&   faceVertCounts,
                           const std::vector<int>&   faceVertIndices) {
    points_      = worldPoints;
    faceCounts_  = faceVertCounts;
    faceIndices_ = faceVertIndices;

    colors_.assign(worldPoints.size(), Color3f{0.f, 0.f, 0.f});

    tessellateFaces();
    buildBVH();

    // FIX: KdTree must be built for verticesInRadius() to work.
    // Without this, painting queries return zero vertices.
    kdTree_.build(points_);
}

void MeshSampler::tessellateFaces() {
    tris_.clear();
    int offset = 0;
    for (size_t fi = 0; fi < faceCounts_.size(); ++fi) {
        int count = faceCounts_[fi];
        // Fan triangulation from v0
        for (int t = 1; t < count - 1; ++t) {
            Triangle tri;
            tri.faceIndex = (uint32_t)fi;
            tri.v0 = points_[faceIndices_[offset]];
            tri.v1 = points_[faceIndices_[offset + t]];
            tri.v2 = points_[faceIndices_[offset + t + 1]];
            Vec3f e1 = tri.v1 - tri.v0;
            Vec3f e2 = tri.v2 - tri.v0;
            Vec3f n  = cross(e1, e2);
            float len = std::sqrt(n.lengthSq());
            tri.normal = (len > 1e-8f) ? n * (1.f/len) : Vec3f{0,1,0};
            tris_.push_back(tri);
        }
        offset += count;
    }
}

// ─────────────────────────────────────────────────────────────────────────────
//  BVH construction
//
//  FIX: The original buildBVH() was a no-op (just cleared the vector),
//  which meant intersect() always returned immediately with no hit.
//  This broke the entire brush overlay — hitValid_ was never true.
// ─────────────────────────────────────────────────────────────────────────────

void MeshSampler::buildBVH() {
    bvh_.clear();
    triOrder_.clear();
    if (tris_.empty()) return;

    // Build index list [0, 1, 2, ... N-1] and recursively partition.
    // buildBVHRecursive reorders this array via nth_element, so BVH leaf
    // nodes store ranges into this reordered array. We persist it as
    // triOrder_ so that intersect() can map leaf ranges back to tris_.
    std::vector<uint32_t> indices(tris_.size());
    std::iota(indices.begin(), indices.end(), 0u);
    buildBVHRecursive(indices, 0, (int)indices.size(), 0);

    // Persist the reordered index array for use during traversal
    triOrder_ = std::move(indices);
}

int MeshSampler::buildBVHRecursive(std::vector<uint32_t>& idx, int lo, int hi, int depth) {
    BVHNode node;
    // Compute AABB
    node.bmin = { 1e30f, 1e30f, 1e30f };
    node.bmax = {-1e30f,-1e30f,-1e30f};
    for (int i = lo; i < hi; ++i) {
        const Triangle& t = tris_[idx[i]];
        for (auto& v : {t.v0, t.v1, t.v2}) {
            node.bmin = aabbMin(node.bmin, v);
            node.bmax = aabbMax(node.bmax, v);
        }
    }

    int count = hi - lo;
    if (count <= 4 || depth > 20) {
        // Leaf — store the triangle index range.
        // We need the tris_ array to be reordered so that the triangles
        // referenced by idx[lo..hi) are contiguous. Since we're building
        // in-place, we reorder tris_ to match the idx ordering now.
        // The leaf stores absolute indices into tris_.
        // After the full build, tris_ order matches the BVH leaf order.
        node.triBegin = (uint32_t)lo;
        node.triEnd   = (uint32_t)hi;
        int nodeIdx = (int)bvh_.size();
        bvh_.push_back(node);
        return nodeIdx;
    }

    // Split along longest axis
    Vec3f extent = { node.bmax.x-node.bmin.x,
                     node.bmax.y-node.bmin.y,
                     node.bmax.z-node.bmin.z };
    int axis = 0;
    if (extent.y > extent.x) axis = 1;
    if (extent.z > (&extent.x)[axis]) axis = 2;

    int mid = lo + count / 2;
    std::nth_element(idx.begin() + lo, idx.begin() + mid, idx.begin() + hi,
        [&](uint32_t a, uint32_t b) {
            auto centroid = [&](uint32_t i) {
                const Triangle& t = tris_[i];
                auto c = (t.v0 + t.v1 + t.v2) * (1.f/3.f); return (&c.x)[axis];
            };
            return centroid(a) < centroid(b);
        });

    int nodeIdx = (int)bvh_.size();
    bvh_.push_back(node); // reserve slot

    int leftChild  = buildBVHRecursive(idx, lo,  mid, depth+1);
    int rightChild = buildBVHRecursive(idx, mid, hi,  depth+1);
    bvh_[nodeIdx].left  = leftChild;
    bvh_[nodeIdx].right = rightChild;
    bvh_[nodeIdx].triBegin = bvh_[nodeIdx].triEnd = 0; // not a leaf

    return nodeIdx;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Ray-AABB slab test (returns tMin)
// ─────────────────────────────────────────────────────────────────────────────

bool MeshSampler::intersectAABB(const BVHNode& node, const Ray& ray, float& tMin) const {
    float tmax = 1e30f;
    tMin = -1e30f;
    for (int i = 0; i < 3; ++i) {
        float o  = (&ray.origin.x)[i];
        float d  = (&ray.dir.x)[i];
        float mn = (&node.bmin.x)[i];
        float mx = (&node.bmax.x)[i];
        if (std::abs(d) < 1e-8f) {
            if (o < mn || o > mx) return false;
        } else {
            float invD = 1.f / d;
            float t0 = (mn - o) * invD;
            float t1 = (mx - o) * invD;
            if (t0 > t1) std::swap(t0, t1);
            tMin = std::max(tMin, t0);
            tmax = std::min(tmax, t1);
            if (tMin > tmax) return false;
        }
    }
    return tmax >= 0.f;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Möller–Trumbore ray-triangle intersection
// ─────────────────────────────────────────────────────────────────────────────

bool MeshSampler::intersectTri(const Triangle& tri, const Ray& ray,
                                float& t, Vec3f& n) const {
    const float EPS = 1e-7f;
    Vec3f e1 = tri.v1 - tri.v0;
    Vec3f e2 = tri.v2 - tri.v0;
    Vec3f h  = cross(ray.dir, e2);
    float a  = dot(e1, h);
    if (std::abs(a) < EPS) return false;

    float f  = 1.f / a;
    Vec3f s  = ray.origin - tri.v0;
    float u  = f * dot(s, h);
    if (u < 0.f || u > 1.f) return false;

    Vec3f q  = cross(s, e1);
    float v  = f * dot(ray.dir, q);
    if (v < 0.f || u + v > 1.f) return false;

    t = f * dot(e2, q);
    if (t < EPS) return false;

    n = tri.normal;
    return true;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Public: intersect
//
//  NOTE: The BVH leaf nodes store index ranges into the `idx` array that
//  was used during construction. Since buildBVHRecursive reorders `idx`
//  via nth_element, the leaf ranges [triBegin, triEnd) are indices into
//  that reordered array — NOT into tris_ directly. We need to look up
//  through the idx indirection.
//
//  However, the current design stores the idx array only on the stack
//  during buildBVH(). To fix this properly we persist the reordered
//  index array. See triOrder_ below.
// ─────────────────────────────────────────────────────────────────────────────

HitResult MeshSampler::intersect(const Ray& ray) const {
    HitResult result;
    if (bvh_.empty()) return result;

    // Iterative BVH traversal using explicit stack
    std::stack<int> stack;
    stack.push(0);

    while (!stack.empty()) {
        int ni = stack.top(); stack.pop();
        if (ni < 0 || ni >= (int)bvh_.size()) continue;
        const BVHNode& node = bvh_[ni];

        float tAABB;
        if (!intersectAABB(node, ray, tAABB)) continue;
        if (tAABB > result.t) continue;

        bool isLeaf = (node.left < 0 && node.right < 0);
        if (isLeaf) {
            for (uint32_t ti = node.triBegin; ti < node.triEnd; ++ti) {
                // ti indexes into triOrder_, which maps to tris_
                uint32_t triIdx = (ti < triOrder_.size()) ? triOrder_[ti] : ti;
                float t; Vec3f n;
                if (intersectTri(tris_[triIdx], ray, t, n)) {
                    if (t < result.t) {
                        result.hit       = true;
                        result.t         = t;
                        result.normal    = n;
                        result.faceIndex = tris_[triIdx].faceIndex;
                        result.position  = ray.origin + ray.dir * t;
                    }
                }
            }
        } else {
            stack.push(node.left);
            stack.push(node.right);
        }
    }
    return result;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Public: verticesInRadius
// ─────────────────────────────────────────────────────────────────────────────

void MeshSampler::verticesInRadius(const Vec3f& center, float radius,
                                    std::vector<std::pair<uint32_t,float>>& out) const {
    out.clear();
    kdTree_.queryRadius(center, radius * radius, out);
    std::sort(out.begin(), out.end(),
        [](const auto& a, const auto& b){ return a.second < b.second; });
}

// ─────────────────────────────────────────────────────────────────────────────
//  Colour accessors
// ─────────────────────────────────────────────────────────────────────────────

Color3f MeshSampler::getColor(uint32_t idx) const {
    if (idx < colors_.size()) return colors_[idx];
    return {0.f, 0.f, 0.f};
}
void MeshSampler::setColor(uint32_t idx, Color3f c) {
    if (idx < colors_.size()) colors_[idx] = c;
}
void MeshSampler::initColors(const std::vector<Color3f>& initial) {
    colors_ = initial;
    colors_.resize(points_.size(), {0.f,0.f,0.f});
}

} // namespace AP
