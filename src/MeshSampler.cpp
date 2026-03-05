#include "MeshSampler.h"
#include <cassert>
#include <cmath>
#include <limits>
#include <numeric>
#include <stack>

namespace AP {

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  KdTree
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

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
    int axis = depth % 3;
    int mid  = lo + (hi - lo) / 2;

    // Partial sort on median
    std::nth_element(pts.begin() + lo, pts.begin() + mid, pts.begin() + hi,
        [axis](const auto& a, const auto& b) {
            return (&a.first.x)[axis] < (&b.first.x)[axis];
        });

    int nodeIdx = (int)nodes_.size() - (int)pts.size() + mid;
    // We pre-reserved, so reuse the mid position
    nodeIdx = mid; // nodes_ is indexed the same as pts after this approach
    // Use a simpler index-stable approach:
    static thread_local std::vector<KdNode> tmp;
    tmp.resize(pts.size());

    // Iterative median-split build into contiguous array
    // (recursive lambda via std::function for clarity)
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
    int nearIdx = (axisDist < 0) ? node.left : node.right; // nearIdx
    int farIdx  = (axisDist < 0) ? node.right : node.left;

    queryRecursive(nearIdx, center, radiusSq, out);
    if (axisDistSq <= radiusSq)
        queryRecursive(farIdx, center, radiusSq, out);
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  MeshSampler â€” rebuild
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void MeshSampler::rebuild(const std::vector<Vec3f>& worldPoints,
                           const std::vector<int>&   faceVertCounts,
                           const std::vector<int>&   faceVertIndices) {
    points_      = worldPoints;
    faceCounts_  = faceVertCounts;
    faceIndices_ = faceVertIndices;

    colors_.assign(worldPoints.size(), Color3f{0.f, 0.f, 0.f});

    tessellateFaces();
    buildBVH();
    //kdTree_.build(points_);
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

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  BVH construction
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void MeshSampler::buildBVH() { bvh_.clear(); }

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
        // Leaf
        node.triBegin = (uint32_t)bvh_.size() + 1; // placeholder, set after push
        // Store triangle indices contiguously after reordering
        // (we store absolute triangle indices in leaf range)
        node.triBegin = (uint32_t)lo;
        node.triEnd   = (uint32_t)hi;
        // Reorder tris_ to match idx order in [lo,hi]
        // (done once at end â€” for now store idx range)
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

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  Ray-AABB slab test (returns tMin)
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

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

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  MÃ¶llerâ€“Trumbore ray-triangle intersection
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

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

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  Public: intersect
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

HitResult MeshSampler::intersect(const Ray& ray) const {
    HitResult result;
    if (bvh_.empty()) return result;

    // Iterative BVH traversal using explicit stack
    std::stack<int> stack;
    stack.push(0);

    while (!stack.empty()) {
        int ni = stack.top(); stack.pop();
        if (ni < 0) continue;
        const BVHNode& node = bvh_[ni];

        float tAABB;
        if (!intersectAABB(node, ray, tAABB)) continue;
        if (tAABB > result.t) continue;

        bool isLeaf = (node.left < 0 && node.right < 0);
        if (isLeaf) {
            for (uint32_t ti = node.triBegin; ti < node.triEnd; ++ti) {
                float t; Vec3f n;
                if (intersectTri(tris_[ti], ray, t, n)) {
                    if (t < result.t) {
                        result.hit       = true;
                        result.t         = t;
                        result.normal    = n;
                        result.faceIndex = tris_[ti].faceIndex;
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

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  Public: verticesInRadius
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

void MeshSampler::verticesInRadius(const Vec3f& center, float radius,
                                    std::vector<std::pair<uint32_t,float>>& out) const {
    out.clear();
    kdTree_.queryRadius(center, radius * radius, out);
    std::sort(out.begin(), out.end(),
        [](const auto& a, const auto& b){ return a.second < b.second; });
}

// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€
//  Colour accessors
// â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€â”€

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


