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
    rootIdx_ = -1;
    if (points.empty()) return;

    std::vector<std::pair<Vec3f,uint32_t>> indexed;
    indexed.reserve(points.size());
    for (uint32_t i = 0; i < (uint32_t)points.size(); ++i)
        indexed.push_back({points[i], i});

    nodes_.resize(points.size());
    rootIdx_ = buildRecursive(indexed, 0, (int)indexed.size(), 0);
}

int KdTree::buildRecursive(std::vector<std::pair<Vec3f,uint32_t>>& pts,
                            int lo, int hi, int depth) {
    if (lo >= hi) return -1;

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

    int root = build(0, (int)pts.size(), 0);
    nodes_ = tmp;
    nodes_.resize(pts.size());
    rootIdx_ = root;
    return root;
}

void KdTree::queryRadius(const Vec3f& center, float radiusSq,
                         std::vector<std::pair<uint32_t,float>>& out) const {
    if (nodes_.empty() || rootIdx_ < 0) return;
    queryRecursive(rootIdx_, center, radiusSq, out);
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

    // Preserve colours across topology-preserving rebuilds
    if (colors_.size() != worldPoints.size()) {
        colors_.assign(worldPoints.size(), Color3f{0.f, 0.f, 0.f});
    }

    // Compute bounding box and centroid
    if (!points_.empty()) {
        bboxMin_ = bboxMax_ = points_[0];
        Vec3f sum = {0,0,0};
        for (auto& p : points_) {
            bboxMin_ = aabbMin(bboxMin_, p);
            bboxMax_ = aabbMax(bboxMax_, p);
            sum = sum + p;
        }
        float inv = 1.f / (float)points_.size();
        centroid_ = sum * inv;
    }

    tessellateFaces();
    buildBVH();
    kdTree_.build(points_);
}

void MeshSampler::tessellateFaces() {
    tris_.clear();
    int offset = 0;
    for (size_t fi = 0; fi < faceCounts_.size(); ++fi) {
        int count = faceCounts_[fi];
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
// ─────────────────────────────────────────────────────────────────────────────

void MeshSampler::buildBVH() {
    bvh_.clear();
    triOrder_.clear();
    if (tris_.empty()) return;

    std::vector<uint32_t> indices(tris_.size());
    std::iota(indices.begin(), indices.end(), 0u);
    buildBVHRecursive(indices, 0, (int)indices.size(), 0);
    triOrder_ = std::move(indices);
}

int MeshSampler::buildBVHRecursive(std::vector<uint32_t>& idx, int lo, int hi, int depth) {
    BVHNode node;
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
        node.triBegin = (uint32_t)lo;
        node.triEnd   = (uint32_t)hi;
        int nodeIdx = (int)bvh_.size();
        bvh_.push_back(node);
        return nodeIdx;
    }

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
    bvh_.push_back(node);

    int leftChild  = buildBVHRecursive(idx, lo,  mid, depth+1);
    int rightChild = buildBVHRecursive(idx, mid, hi,  depth+1);
    bvh_[nodeIdx].left  = leftChild;
    bvh_[nodeIdx].right = rightChild;
    bvh_[nodeIdx].triBegin = bvh_[nodeIdx].triEnd = 0;

    return nodeIdx;
}

// ─────────────────────────────────────────────────────────────────────────────
//  Ray-AABB slab test
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

HitResult MeshSampler::intersect(const Ray& ray) const {
    HitResult result;
    if (bvh_.empty()) return result;

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
    // Flip normal to face the camera if we hit a back face.
    // This prevents the brush from flipping on closed meshes like spheres.
    if (result.hit) {
        if (dot(result.normal, ray.dir) > 0.f) {
            result.normal = result.normal * -1.f;
        }
    }
    return result;
}

void MeshSampler::verticesInRadius(const Vec3f& center, float radius,
                                    std::vector<std::pair<uint32_t,float>>& out) const {
    out.clear();
    kdTree_.queryRadius(center, radius * radius, out);
    std::sort(out.begin(), out.end(),
        [](const auto& a, const auto& b){ return a.second < b.second; });
}

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
